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

TEST_CASE("a chain is created, listed, shown and played as any symphony (27r)",
          "[commands][symphonies]") {
    const Home home;
    const std::string before = home.home.config_text();
    std::string out;
    std::string err;
    // --stage and --play interleave in the order they are written.
    REQUIRE(home.run({"symphonies", "create", "digest", "--play", "summary:summarize-verify",
                      "--stage", "note:chat:Note {{summary}}", "--play",
                      "again:summarize-verify:{{note}} / {{input}}"},
                     &out, &err) == 0);
    CHECK_THAT(out, Catch::Matchers::StartsWith(
                        "created symphony 'digest' (play:summarize-verify → chat → "
                        "play:summarize-verify)\n"));
    CHECK(home.home.config_text() == before + R"YAML(
symphonies:
  digest:
    stages:
      - name: summary
        play: summarize-verify
      - name: note
        role: chat
        prompt: "Note {{summary}}"
      - name: again
        play: summarize-verify
        input: "{{note}} / {{input}}"
)YAML");

    REQUIRE(home.run({"symphonies", "list"}, &out, &err) == 0);
    CHECK_THAT(out, Catch::Matchers::Matches(
                        "(?:.|\n)*digest +play:summarize-verify → chat → play:summarize-verify +"
                        "config(?:.|\n)*"));
    REQUIRE(home.run({"symphonies", "show", "digest"}, &out, &err) == 0);
    CHECK_THAT(out, Catch::Matchers::ContainsSubstring(
                        "stage 1/3  summary  ·  plays summarize-verify  ·  given the symphony's "
                        "input\n"));
    CHECK_THAT(out, Catch::Matchers::ContainsSubstring(
                        "stage 3/3  again  ·  plays summarize-verify  ·  given:\n    {{note}} / "
                        "{{input}}\n"));
    CHECK_THAT(out, Catch::Matchers::ContainsSubstring(
                        "one play: 5 member calls, one after another, 2 symphonies deep (the cap "
                        "is 4)\n"));

    // Played: every stage a member call, each line its position.
    REQUIRE(home.run({"symphonies", "play", "digest", "--input", "P"}, &out, &err) == 0);
    CHECK_THAT(err, Catch::Matchers::StartsWith(
                        "digest → summarize-verify, stage 1/2 summarize — asking utility "
                        "(helper): "));
    CHECK_THAT(err, Catch::Matchers::ContainsSubstring(
                        "\ndigest → summarize-verify, stage 2/2 verify — asking chat (root): "));
    CHECK_THAT(err, Catch::Matchers::ContainsSubstring(
                        "\ndigest, stage 2/3 note — asking chat (root): Note ROOT<"));
    // The last play's input is the note's answer and the chain's input.
    CHECK_THAT(out, Catch::Matchers::ContainsSubstring("Passage:\nROOT<Note ROOT<"));
    CHECK_THAT(out, Catch::Matchers::ContainsSubstring("> / P\n"));
    REQUIRE(home.run({"symphonies", "play", "digest", "--input", "P", "--output-format", "json"},
                     &out, &err) == 0);
    const nlohmann::json document = nlohmann::json::parse(out);
    REQUIRE(document["stages"].size() == 3);
    CHECK(document["stages"][0]["play"] == "summarize-verify");
    CHECK(document["stages"][0]["stages"].size() == 2);
    CHECK(document["stages"][1]["role"] == "chat");
    CHECK(document["output"] == document["stages"][2]["answer"]);

    // A loop through another entry is refused at create, the file untouched.
    const std::string with_digest = home.home.config_text();
    CHECK(home.run({"symphonies", "create", "summarize-verify", "--play", "x:digest"}, &out,
                   &err) == 1);
    CHECK_THAT(err, Catch::Matchers::ContainsSubstring(
                        "a loop, summarize-verify → digest → summarize-verify"));
    CHECK(home.home.config_text() == with_digest);
    CHECK(home.run({"symphonies", "create", "bad", "--play", "nocolon"}, &out, &err) == 1);
    CHECK_THAT(err, Catch::Matchers::ContainsSubstring("expected NAME:SYMPHONY[:INPUT]"));
}

TEST_CASE("a chain's budget is the config's symphony_caps, stopping it named (27r)",
          "[commands][symphonies]") {
    const Home home{
        "symphony_caps:\n  stage_calls: 3\nsymphonies:\n  twice:\n    stages:\n"
        "      - {name: a, play: summarize-verify}\n"
        "      - {name: b, play: summarize-verify}\n"};
    std::string out;
    std::string err;
    // Each play of the starter is two calls; the chain's fourth is past the cap.
    CHECK(home.run({"symphonies", "play", "twice", "--input", "P"}, &out, &err) == 1);
    CHECK(out.empty());
    CHECK_THAT(err, Catch::Matchers::ContainsSubstring(
                        "apogee symphonies: twice → summarize-verify, stage 2/2 verify (chat): "
                        "the play's budget is spent -- 3 of 3 member calls made"));
    // The starter alone is inside the same cap.
    REQUIRE(home.run({"symphonies", "play", "summarize-verify", "--input", "P", "-q"}, &out,
                     &err) == 0);
}
