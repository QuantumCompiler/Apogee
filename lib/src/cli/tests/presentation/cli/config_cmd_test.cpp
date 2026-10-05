#include "cli/config_cmd.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "contracts/config.h"
#include "support/cli_home.h"
#include "support/gguf_builder.h"

/// `config add-backend` filling itself from the model store (M7): a stored
/// GGUF's name is enough to register it, and what it writes is what the
/// hand-typed command would have.
namespace {

using apogee::testing::CliHome;

/// A config whose comments are most of its documentation -- what the one
/// editor exists to keep.
constexpr const char* kConfig = R"(# my models
backends:
  # the cloud one
  claude:
    type: anthropic
    model: claude-sonnet-5
)";

void write_file(const std::filesystem::path& path, const std::string& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream{path, std::ios::binary} << bytes;
}

/// `<models>/org--m/gguf/<id>/<name>`, with a projector beside it when asked.
std::filesystem::path store_gguf(const CliHome& home, const std::string& id,
                                 const std::string& name, bool projector = false) {
    const std::filesystem::path file = home.models() / "org--m" / "gguf" / id / name;
    write_file(file, apogee::testing::minimal_gguf("llama"));
    if (projector) {
        write_file(file.parent_path() / (file.stem().string() + "-mmproj.gguf"), "projector");
    }
    return file;
}

}  // namespace

TEST_CASE("a stored GGUF's name registers it, as the hand-typed command would, comments kept",
          "[commands][config][add-backend][store]") {
    const CliHome filled{kConfig};
    const std::filesystem::path file = store_gguf(filled, "111111111111", "m-F16.gguf", true);
    const std::filesystem::path projector = file.parent_path() / "m-F16-mmproj.gguf";
    std::string out;
    REQUIRE(filled.run({"config", "add-backend", "m-F16"}, &out) == 0);
    INFO(out);
    // It says what it filled, as the arguments it stands for.
    CHECK(out.find("filled from the store: org--m/gguf/111111111111\n  --type llamacpp "
                   "--model-path " +
                   file.string() + " --mmproj-path " + projector.string() + "\n") !=
          std::string::npos);

    // Byte for byte the entry a hand-typed add-backend writes, in a twin.
    const CliHome typed{kConfig};
    REQUIRE(typed.run({"config", "add-backend", "m-F16", "--type", "llamacpp", "--model-path",
                       file.string(), "--mmproj-path", projector.string()},
                      &out) == 0);
    CHECK(filled.config_text() == typed.config_text());
    CHECK(filled.config_text().starts_with("# my models\nbackends:\n  # the cloud one\n"));
}

TEST_CASE("a flag given wins over the store", "[commands][config][add-backend][store]") {
    const CliHome home{kConfig};
    (void)store_gguf(home, "111111111111", "m-F16.gguf", true);
    std::string out;

    // A model path given: nothing is filled, the projector included.
    REQUIRE(home.run({"config", "add-backend", "m-F16", "--type", "llamacpp", "--model-path",
                      "/elsewhere/m.gguf"},
                     &out) == 0);
    CHECK(out.find("filled from the store") == std::string::npos);
    apogee::harness::Config config = apogee::harness::load_config(home.config_path());
    CHECK(config.backends.at("m-F16").model_path == "/elsewhere/m.gguf");
    CHECK(config.backends.at("m-F16").mmproj_path.empty());

    // Another type: the name is just a name.
    REQUIRE(home.run({"config", "add-backend", "m-F16", "--type", "anthropic", "--model", "x",
                      "--force"},
                     &out) == 0);
    CHECK(out.find("filled from the store") == std::string::npos);
    config = apogee::harness::load_config(home.config_path());
    CHECK(config.backends.at("m-F16").type == apogee::harness::BackendType::Anthropic);

    // The type given as the store's: the path still fills, a projector given stays.
    REQUIRE(home.run({"config", "add-backend", "m-F16", "--type", "llamacpp", "--mmproj-path",
                      "/mine/p.gguf", "--force"},
                     &out) == 0);
    config = apogee::harness::load_config(home.config_path());
    CHECK(config.backends.at("m-F16").type == apogee::harness::BackendType::LlamaCpp);
    CHECK(config.backends.at("m-F16").model_path.ends_with("m-F16.gguf"));
    CHECK(config.backends.at("m-F16").mmproj_path == "/mine/p.gguf");
}

TEST_CASE("a name the store does not know is refused exactly as before",
          "[commands][config][add-backend][store]") {
    const CliHome home{kConfig};
    (void)store_gguf(home, "111111111111", "m-F16.gguf");
    std::string out;
    // The parser's own words and code, as when --type was required by it.
    CHECK(home.run({"config", "add-backend", "something-else"}, &out) == 106);
    CHECK(out == "--type is required\nRun with --help for more information.\n");
    CHECK(home.config_text() == kConfig);
}

TEST_CASE("a name two stored GGUFs share is refused with both listed",
          "[commands][config][add-backend][store]") {
    const CliHome home{kConfig};
    const std::filesystem::path one = store_gguf(home, "111111111111", "m.gguf");
    const std::filesystem::path two = store_gguf(home, "222222222222", "m.gguf");
    std::string out;
    CHECK(home.run({"config", "add-backend", "m"}, &out) == 1);
    CHECK(out.find("'m' is the name of 2 stored GGUFs -- pass --model-path with one of:") !=
          std::string::npos);
    CHECK(out.find("  " + one.string()) != std::string::npos);
    CHECK(out.find("  " + two.string()) != std::string::npos);
    CHECK(home.config_text() == kConfig);
}

TEST_CASE("add-backend takes a local model's sampling, and the config's rules refuse a bad one",
          "[commands][config][add-backend][sampling]") {
    // 26h: the knobs the config parses, settable where every other backend
    // field is -- and checked by the config's own rules, not a second copy.
    const CliHome home{kConfig};
    std::string out;
    REQUIRE(home.run({"config",    "add-backend",
                      "local",     "--type",
                      "llamacpp",  "--model-path",
                      "/m/a.gguf", "--temperature",
                      "0.7",       "--top-p",
                      "0.8",       "--top-k",
                      "20",        "--min-p",
                      "0.05",      "--repeat-penalty",
                      "1.1",       "--presence-penalty",
                      "1.5",       "--seed",
                      "42"},
                     &out) == 0);
    const apogee::harness::Config config =
        apogee::harness::parse_config(home.config_text(), "<test>");
    const auto* local = config.find_backend("local");
    REQUIRE(local != nullptr);
    CHECK(local->top_p == 0.8);
    CHECK(local->top_k == 20);
    CHECK(local->min_p == 0.05);
    CHECK(local->repeat_penalty == 1.1);
    CHECK(local->presence_penalty == 1.5);
    CHECK(local->seed == 42);

    const std::string before = home.config_text();
    CHECK(home.run({"config", "add-backend", "bad", "--type", "llamacpp", "--model-path",
                    "/m/a.gguf", "--top-p", "1.5"},
                   &out) != 0);
    CHECK(out.find("top_p") != std::string::npos);
    CHECK(home.config_text() == before);
}

TEST_CASE("add-backend takes a thinking default and budget, and refuses a bad one",
          "[commands][config][add-backend][thinking]") {
    const CliHome home{kConfig};
    std::string out;
    REQUIRE(home.run({"config", "add-backend", "local", "--type", "llamacpp", "--model-path",
                      "/m/a.gguf", "--thinking", "auto", "--thinking-budget", "2048"},
                     &out) == 0);
    const apogee::harness::Config config =
        apogee::harness::parse_config(home.config_text(), "<test>");
    const auto* local = config.find_backend("local");
    REQUIRE(local != nullptr);
    CHECK(local->thinking == apogee::harness::ThinkingMode::Auto);
    CHECK(local->thinking_budget == 2048);
    // `config get` reads both back.
    out.clear();
    REQUIRE(home.run({"config", "get", "backends.local.thinking"}, &out) == 0);
    CHECK(out == "auto\n");
    out.clear();
    REQUIRE(home.run({"config", "get", "backends.local.thinking_budget"}, &out) == 0);
    CHECK(out == "2048\n");

    const std::string before = home.config_text();
    CHECK(home.run({"config", "add-backend", "bad", "--type", "llamacpp", "--model-path",
                    "/m/a.gguf", "--thinking", "sometimes"},
                   &out) != 0);
    CHECK(home.run({"config", "add-backend", "bad", "--type", "llamacpp", "--model-path",
                    "/m/a.gguf", "--thinking-budget", "-1"},
                   &out) != 0);
    CHECK(out.find("thinking_budget") != std::string::npos);
    CHECK(home.config_text() == before);
}

namespace {

/// Three backends and a comment the suite verbs must leave where it is.
constexpr const char* kSuiteConfig = R"(# my models
models:
  default: root   # the big one
backends:
  root:
    type: mock
  helper:
    type: mock
  embedder:
    type: mock
)";

}  // namespace

TEST_CASE("add-suite writes the block through the one editor, comments kept",
          "[commands][config][suites]") {
    // 27d's first acceptance criterion, at the command.
    const CliHome home{kSuiteConfig};
    std::string out;
    REQUIRE(home.run({"config", "add-suite", "research", "--chat", "root", "--utility", "helper",
                      "--embedding", "embedder"},
                     &out) == 0);
    INFO(out);
    CHECK(home.config_text() == std::string{kSuiteConfig} +
                                    "\nsuites:\n  research:\n    members:\n      chat: root\n"
                                    "      embedding: embedder\n      utility: helper\n");
    CHECK(out.find("added suite 'research'") != std::string::npos);

    // The knobs, pinned per member; the CLI's words for each refusal.
    REQUIRE(home.run({"config", "add-suite", "fast", "--chat", "helper", "--context-size",
                      "chat=4096", "--toolset", "chat=fs,git", "--description", "All small"},
                     &out) == 0);
    const auto config = apogee::harness::parse_config(home.config_text(), "<test>");
    const apogee::harness::SuiteMember& chat = config.find_suite("fast")->members.at("chat");
    CHECK(chat.backend == "helper");
    CHECK(chat.context_size == 4096);
    CHECK(chat.toolset == std::vector<std::string>{"fs", "git"});
    CHECK(config.find_suite("fast")->description == "All small");

    const std::vector<std::pair<std::vector<std::string>, std::string>> refused{
        {{"config", "add-suite", "empty"}, "names at least one member"},
        {{"config", "add-suite", "ghost", "--chat", "nope"}, "no backend named 'nope'"},
        {{"config", "add-suite", "off", "--chat", "root"}, "'off' is reserved"},
        {{"config", "add-suite", "Research", "--chat", "root"}, "collides with existing"},
        {{"config", "add-suite", "x", "--chat", "root", "--context-size", "utility=4096"},
         "the suite has no utility member"},
        {{"config", "add-suite", "x", "--chat", "root", "--context-size", "chat=lots"},
         "is not a positive number of tokens"},
        {{"config", "add-suite", "x", "--chat", "root", "--toolset", "chat=fs,browser"},
         "'browser' is not a toolset"},
        {{"config", "add-suite", "x", "--chat", "root", "--toolset", "root=fs"},
         "'root' is not a role"},
    };
    const std::string before = home.config_text();
    for (const auto& [args, said] : refused) {
        INFO(said);
        CHECK(home.run(args, &out) != 0);
        CHECK(out.find(said) != std::string::npos);
        CHECK(home.config_text() == before);
    }
}

TEST_CASE("set-suite edits one member in place; delete-suite and the default suite",
          "[commands][config][suites]") {
    const CliHome home{kSuiteConfig};
    std::string out;
    REQUIRE(home.run({"config", "add-suite", "research", "--chat", "root", "--utility", "helper"},
                     &out) == 0);
    REQUIRE(home.run({"config", "set-suite", "research", "--utility", "embedder", "--context-size",
                      "utility=2048", "--embedding", "embedder"},
                     &out) == 0);
    INFO(out);
    CHECK(home.config_text() ==
          std::string{kSuiteConfig} +
              "\nsuites:\n  research:\n    members:\n      chat: root\n"
              "      embedding: embedder\n      utility:\n        backend: embedder\n"
              "        context_size: 2048\n");
    // --unpin drops the knobs, --remove the member.
    REQUIRE(
        home.run({"config", "set-suite", "research", "--unpin", "utility", "--remove", "embedding"},
                 &out) == 0);
    CHECK(home.config_text() ==
          std::string{kSuiteConfig} +
              "\nsuites:\n  research:\n    members:\n      chat: root\n      utility: embedder\n");
    CHECK(home.run({"config", "set-suite", "research"}, &out) != 0);
    CHECK(out.find("nothing to change") != std::string::npos);
    CHECK(home.run({"config", "set-suite", "nope", "--chat", "root"}, &out) != 0);
    CHECK(out.find("no suite named 'nope' (configured: research)") != std::string::npos);

    // The default suite: set, read back through config get, refused while
    // set from deletion, cleared with off.
    REQUIRE(home.run({"config", "set-default-suite", "RESEARCH"}, &out) == 0);
    CHECK(out.find("models.default_suite = research") != std::string::npos);
    REQUIRE(home.run({"config", "get", "models.default_suite"}, &out) == 0);
    CHECK(out == "research\n");
    REQUIRE(home.run({"config", "get", "suites.research"}, &out) == 0);
    CHECK(out == "chat: root\nutility: embedder\n");
    REQUIRE(home.run({"config", "get", "suites.research.utility"}, &out) == 0);
    CHECK(out == "embedder\n");
    CHECK(home.run({"config", "delete-suite", "research"}, &out) != 0);
    CHECK(out.find("'research' is the default suite (models.default_suite)") != std::string::npos);
    CHECK(home.run({"config", "set-default-suite", "nope"}, &out) != 0);
    REQUIRE(home.run({"config", "set-default-suite", "off"}, &out) == 0);
    REQUIRE(home.run({"config", "delete-suite", "research"}, &out) == 0);
    CHECK(apogee::harness::parse_config(home.config_text(), "<test>").suites.empty());
}

TEST_CASE("the suite verbs name consultable members, refusing one billed per call",
          "[commands][config][suites][consult]") {
    const CliHome probe{""};
    // A mock whose script says each call is billed: the provider's own word.
    const std::filesystem::path script = probe.home() / "paid.json";
    {
        std::ofstream out{script};
        out << R"({"metered": true, "turns": [{"text": "paid"}]})";
    }
    const CliHome home{std::string{kSuiteConfig} +
                       "  paid:\n    type: mock\n    model_path: " + script.string() + "\n"};
    std::string out;
    REQUIRE(
        home.run({"config", "add-suite", "research", "--chat", "root", "--utility", "helper",
                  "--vision", "paid", "--consultable", "utility", "--consult-cap", "per_turn=2"},
                 &out) == 0);
    INFO(out);
    CHECK(home.config_text().ends_with(
        "\nsuites:\n  research:\n    members:\n      chat: root\n      vision: paid\n"
        "      utility: helper\n    consultable: [utility]\n    consult_caps:\n"
        "      per_turn: 2\n"));
    REQUIRE(home.run({"config", "get", "suites.research.consultable"}, &out) == 0);
    CHECK(out == "utility\n");
    REQUIRE(home.run({"config", "get", "suites.research.consult_caps"}, &out) == 0);
    CHECK(out == "per_turn 2, brief_tokens 1024 (default), answer_tokens 512 (default)\n");
    REQUIRE(home.run({"config", "get", "suites.research"}, &out) == 0);
    CHECK(out ==
          "chat: root\nvision: paid\nutility: helper\nconsultable: utility\n"
          "consult_caps: per_turn 2, brief_tokens 1024 (default), answer_tokens 512 (default)\n");

    // At config time, through the one editor's verbs: a member billed per
    // call is refused with the reason, and the file is left as it was.
    const std::string before = home.config_text();
    const std::vector<std::pair<std::vector<std::string>, std::string>> refused{
        {{"config", "set-suite", "research", "--consultable", "utility,vision"},
         "consultable vision: 'paid' is billed per call -- a consult runs on the model's "
         "initiative, which never spends"},
        {{"config", "set-suite", "research", "--utility", "paid"},
         "consultable utility: 'paid' is billed per call"},
        {{"config", "set-suite", "research", "--remove", "utility"},
         "consultable utility: the suite has no utility member"},
        {{"config", "set-suite", "research", "--consultable", "chat"}, "the root itself"},
        {{"config", "set-suite", "research", "--consult-cap", "per_call=2"},
         "--consult-cap per_call=2: not NAME=N with NAME one of per_turn, brief_tokens, "
         "answer_tokens"},
        {{"config", "set-suite", "research", "--consult-cap", "per_turn=0"},
         "is not a positive whole number"},
        {{"config", "add-suite", "x", "--chat", "root", "--consultable", "utility"},
         "consultable utility: the suite has no utility member"},
    };
    for (const auto& [args, said] : refused) {
        INFO(said);
        CHECK(home.run(args, &out) != 0);
        CHECK(out.find(said) != std::string::npos);
        CHECK(home.config_text() == before);
    }

    // Changed in place, and cleared, every other line as it was.
    REQUIRE(home.run({"config", "set-suite", "research", "--consult-cap", "per_turn="}, &out) == 0);
    REQUIRE(home.run({"config", "set-suite", "research", "--consultable", ""}, &out) == 0);
    CHECK(home.config_text().ends_with("      utility: helper\n"));
    CHECK(apogee::harness::parse_config(home.config_text(), "<test>")
              .find_suite("research")
              ->consultable.empty());
}
