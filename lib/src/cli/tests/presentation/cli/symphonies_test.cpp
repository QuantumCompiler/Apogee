#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "cli/symphonies_cmd.h"
#include "contracts/config.h"
#include "contracts/config_edit.h"
#include "support/cli_home.h"
#include "symphony/definition.h"
#include "symphony/view.h"

/// `apogee symphonies` in process (27q): list shows the shipped and the
/// user's distinctly, show prints a definition's stages and contracts,
/// create goes through the scaffold and refuses a backend at parse, delete
/// is the inverse, and play runs the stages over scripted members -- each
/// one said, the output printed, one JSON document with --output-format.
namespace {

using apogee::testing::CliHome;

/// Two mock members, each echoing the brief it was sent, under suite `duo`.
std::string config_with(const std::filesystem::path& scripts, const std::string& extra = {}) {
    std::filesystem::create_directories(scripts);
    std::ofstream{scripts / "root.json"} << R"({"turns": [{"text": "ROOT<{{last_user}}>"}]})";
    std::ofstream{scripts / "helper.json"} << R"({"turns": [{"text": "HELPER<{{last_user}}>"}]})";
    std::ofstream{scripts / "mute.json"} << R"({"turns": [{"text": ""}]})";
    return "# PRESERVE-ME\nbackends:\n"
           "  root:\n    type: mock\n    model_path: " +
           (scripts / "root.json").generic_string() +
           "\n  helper:\n    type: mock\n    model_path: " +
           (scripts / "helper.json").generic_string() +
           "\n  mute:\n    type: mock\n    model_path: " +
           (scripts / "mute.json").generic_string() +
           "\nmodels:\n  default: root\n  default_suite: duo\nmemory:\n  recall: false\n"
           "suites:\n  duo:\n    members:\n      chat: root\n      utility: helper\n"
           "  muted:\n    members:\n      chat: mute\n      utility: helper\n" +
           extra;
}

struct Home {
    CliHome home{std::string{}};

    explicit Home(const std::string& extra = {}) {
        std::ofstream{home.config_path(), std::ios::binary | std::ios::trunc}
            << config_with(home.home() / "scripts", extra);
    }

    [[nodiscard]] int run(const std::vector<std::string>& args, std::string* out, std::string* err,
                          const std::string& stdin_text = {}) const {
        const std::istringstream fed{stdin_text};
        std::streambuf* old_in = std::cin.rdbuf(fed.rdbuf());
        const int code = home.run(args, out, err);
        std::cin.rdbuf(old_in);
        return code;
    }

    [[nodiscard]] int run(const std::vector<std::string>& args) const {
        std::string out;
        std::string err;
        return run(args, &out, &err);
    }
};

}  // namespace

TEST_CASE("list shows the shipped starters and the user's definitions distinctly",
          "[commands][symphonies]") {
    const Home home{
        "symphonies:\n  mine:\n    description: Mine.\n    stages:\n      - {name: a, role: "
        "utility, prompt: '{{input}}'}\n  extract-facts:\n    stages:\n      - {name: a, role: "
        "extraction, prompt: '{{input}}'}\n"};
    std::filesystem::create_directories(home.home.home() / "symphonies");
    std::ofstream{home.home.home() / "symphonies" / "dropped.yaml"}
        << "stages:\n  - {name: a, role: chat, prompt: '{{input}}'}\n";
    std::string out;
    std::string err;
    REQUIRE(home.run({"symphonies", "list"}, &out, &err) == 0);
    CHECK_THAT(out, Catch::Matchers::StartsWith("NAME"));
    CHECK_THAT(out,
               Catch::Matchers::Matches(
                   "(?:.|\n)*describe-answer +vision → chat +shipped +Describe an image(?:.|\n)*"));
    CHECK_THAT(out,
               Catch::Matchers::Matches(
                   "(?:.|\n)*extract-facts +extraction +config \\(overrides shipped\\)(?:.|\n)*"));
    CHECK_THAT(out, Catch::Matchers::Matches("(?:.|\n)*mine +utility +config +Mine\\.(?:.|\n)*"));
    CHECK_THAT(out, Catch::Matchers::Matches("(?:.|\n)*dropped +chat +file(?:.|\n)*"));

    // The JSON read is the one view, byte for byte.
    REQUIRE(home.run({"symphonies", "list", "--output-format", "json"}, &out, &err) == 0);
    const apogee::harness::Config config = apogee::harness::load_config(home.home.config_path());
    CHECK(out == apogee::symphony::list_document(
                     apogee::symphony::catalog(config, home.home.home() / "symphonies"))
                         .dump() +
                     "\n");
    CHECK(home.run({"symphonies", "list", "--output-format", "stream-json"}) != 0);
}

TEST_CASE("show prints a definition's input, stages and contracts", "[commands][symphonies]") {
    const Home home;
    std::string out;
    std::string err;
    REQUIRE(home.run({"symphonies", "show", "extract-facts"}, &out, &err) == 0);
    CHECK_THAT(out, Catch::Matchers::StartsWith("extract-facts  ·  shipped\n"));
    CHECK_THAT(out, Catch::Matchers::ContainsSubstring("input: The text to read.\n"));
    CHECK_THAT(out, Catch::Matchers::ContainsSubstring(
                        "stage 1/1  extract  ·  role extraction  ·  brief up to 4096 tokens, "
                        "answer up to 800 tokens\n"));
    CHECK_THAT(out, Catch::Matchers::ContainsSubstring("  answer held to its schema:\n    {\n"));
    REQUIRE(home.run({"symphonies", "show", "summarize-verify", "--output-format", "json"}, &out,
                     &err) == 0);
    const nlohmann::json document = nlohmann::json::parse(out);
    CHECK(document["object"] == "symphony");
    CHECK(document["stages"].size() == 2);
    CHECK(home.run({"symphonies", "show", "nope"}, &out, &err) == 1);
    CHECK_THAT(err, Catch::Matchers::ContainsSubstring("no symphony named 'nope'"));
}

TEST_CASE("create writes the entry through the scaffold; a backend is refused at parse",
          "[commands][symphonies]") {
    const Home home;
    const std::string before = home.home.config_text();
    std::string out;
    std::string err;
    CHECK(home.run({"symphonies", "create", "bad", "--stage", "one:helper:Do {{input}}"}, &out,
                   &err) == 1);
    CHECK_THAT(err, Catch::Matchers::ContainsSubstring(
                        "'helper' is a backend -- a stage names the role it plays"));
    CHECK(home.run({"symphonies", "create", "bad", "--stage", "one:wizard:Do {{input}}"}, &out,
                   &err) == 1);
    CHECK_THAT(err, Catch::Matchers::ContainsSubstring("'wizard' is not a role"));
    CHECK(home.run({"symphonies", "create", "bad", "--stage", "no-colons"}, &out, &err) == 1);
    CHECK(home.home.config_text() == before);

    REQUIRE(home.run({"symphonies", "create", "pair", "--description", "Two steps.", "--stage",
                      "first:utility:Do: {{input}}", "--stage",
                      "second:chat:Check {{first}} against {{input}}"},
                     &out, &err) == 0);
    CHECK_THAT(out, Catch::Matchers::StartsWith("created symphony 'pair' (utility → chat)\n"));
    CHECK(home.home.config_text() == before + R"YAML(
symphonies:
  pair:
    description: Two steps.
    stages:
      - name: first
        role: utility
        prompt: "Do: {{input}}"
      - name: second
        role: chat
        prompt: "Check {{first}} against {{input}}"
)YAML");
    CHECK(home.run({"symphonies", "create", "pair", "--stage", "a:chat:{{input}}"}, &out, &err) ==
          1);
    CHECK_THAT(err, Catch::Matchers::ContainsSubstring("already exists"));
    REQUIRE(home.run({"symphonies", "create", "copy", "--from", "summarize-verify"}, &out, &err) ==
            0);
    const apogee::harness::Config config = apogee::harness::load_config(home.home.config_path());
    REQUIRE(config.find_symphony("copy") != nullptr);
    CHECK(config.find_symphony("copy")->stages.size() == 2);
    CHECK(home.run({"symphonies", "create", "x", "--from", "copy", "--stage", "a:chat:{{input}}"},
                   &out, &err) == 1);

    // Delete is the exact inverse; a starter has no entry to delete.
    REQUIRE(home.run({"symphonies", "delete", "copy"}) == 0);
    REQUIRE(home.run({"symphonies", "delete", "pair"}, &out, &err) == 0);
    CHECK(home.home.config_text() == before + "\nsymphonies:\n");
    CHECK(home.run({"symphonies", "delete", "summarize-verify"}, &out, &err) == 1);
    CHECK_THAT(err, Catch::Matchers::ContainsSubstring("is a shipped starter"));
}

TEST_CASE("play runs the stages under the active suite, says each, and prints the output",
          "[commands][symphonies]") {
    const Home home;
    std::string out;
    std::string err;
    REQUIRE(home.run({"symphonies", "play", "summarize-verify", "--input", "The cat sat."}, &out,
                     &err) == 0);
    // The chat member's echo of its brief holds the utility member's echo of
    // its own: stage one's answer reached stage two.
    CHECK_THAT(out, Catch::Matchers::StartsWith("ROOT<Here is a passage and a summary of it."));
    CHECK_THAT(out, Catch::Matchers::ContainsSubstring(
                        "Summary:\nHELPER<Summarize the passage below in at most three "
                        "sentences."));
    CHECK_THAT(out, Catch::Matchers::EndsWith(">\n"));
    // Each stage said, on stderr, as 26n's side calls are.
    CHECK_THAT(err, Catch::Matchers::StartsWith(
                        "stage 1/2 summarize — asking utility (helper): Summarize the passage "
                        "below in at most three sentences. Keep…"));
    CHECK_THAT(err, Catch::Matchers::ContainsSubstring(
                        "\nstage 2/2 verify — asking chat (root): Here is a passage and a "
                        "summary of it."));

    SECTION("the input from stdin") {
        REQUIRE(home.run({"symphonies", "play", "summarize-verify", "-q"}, &out, &err,
                         "Piped passage.\n") == 0);
        CHECK_THAT(out, Catch::Matchers::ContainsSubstring("Passage:\nPiped passage.\n\n"));
        CHECK(err.empty());
    }
    SECTION("one JSON document, and nothing else on stdout") {
        REQUIRE(home.run({"symphonies", "play", "summarize-verify", "--input", "x",
                          "--output-format", "json"},
                         &out, &err) == 0);
        const nlohmann::json document = nlohmann::json::parse(out);
        CHECK(document["object"] == "symphony.play");
        CHECK(document["suite"] == "duo");
        CHECK(document["stages"].size() == 2);
        CHECK(document["stages"][0]["backend"] == "helper");
        CHECK(document["output"] == document["stages"][1]["answer"]);
        CHECK(err.empty());
    }
    SECTION("--suite picks the members for this run") {
        CHECK(
            home.run({"symphonies", "play", "summarize-verify", "--input", "x", "--suite", "muted"},
                     &out, &err) == 2);
        CHECK(out.empty());
        CHECK_THAT(err, Catch::Matchers::ContainsSubstring(
                            "apogee symphonies: stage 2/2 verify (chat): 'mute' gave no answer"));
        CHECK(
            home.run({"symphonies", "play", "summarize-verify", "--input", "x", "--suite", "nope"},
                     &out, &err) == 1);
        CHECK_THAT(err, Catch::Matchers::ContainsSubstring("no suite named 'nope'"));
    }
    SECTION("an input the definition cannot take is refused before anything runs") {
        CHECK(home.run({"symphonies", "play", "summarize-verify"}, &out, &err) == 1);
        CHECK_THAT(err, Catch::Matchers::ContainsSubstring(
                            "reads its input (The passage to summarize.), and none was given -- "
                            "pass --input"));
        CHECK(home.run({"symphonies", "play", "describe-answer", "--input", "how many?"}, &out,
                       &err) == 1);
        CHECK_THAT(err, Catch::Matchers::ContainsSubstring("takes an image with its input"));
        CHECK(out.empty());
    }
    SECTION("a spec file by its path") {
        const std::filesystem::path spec = home.home.home() / "one.yaml";
        std::ofstream{spec} << "stages:\n  - {name: echo, role: chat, prompt: 'Say {{input}}'}\n";
        REQUIRE(home.run({"symphonies", "play", spec.string(), "--input", "hi", "-q"}, &out,
                         &err) == 0);
        CHECK(out == "ROOT<Say hi>\n");
    }
}
