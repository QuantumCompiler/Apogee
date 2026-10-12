#include "cli/config_cmd.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "cli/complete_protocol.h"
#include "cli/registry.h"
#include "cli/root.h"
#include "contracts/config.h"
#include "contracts/config_edit.h"
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

TEST_CASE("the suite verbs switch validation seams and name the verifier, refusing a billed one",
          "[commands][config][suites][validate]") {
    const CliHome probe{""};
    const std::filesystem::path script = probe.home() / "paid.json";
    {
        std::ofstream out{script};
        out << R"({"metered": true, "turns": [{"text": "paid"}]})";
    }
    const CliHome home{std::string{kSuiteConfig} +
                       "  paid:\n    type: mock\n    model_path: " + script.string() + "\n"};
    std::string out;
    // `--validate` is repeatable: each seam its own occurrence.
    REQUIRE(
        home.run({"config", "add-suite", "research", "--chat", "root", "--utility", "helper",
                  "--vision", "paid", "--validate", "tool_args=on", "--validate", "answers=always"},
                 &out) == 0);
    INFO(out);
    CHECK(home.config_text().ends_with(
        "\nsuites:\n  research:\n    members:\n      chat: root\n      vision: paid\n"
        "      utility: helper\n    validate:\n      tool_args: on\n      answers: always\n"));
    REQUIRE(home.run({"config", "get", "suites.research.validate"}, &out) == 0);
    CHECK(out ==
          "verifier utility (default), tool_args on, extraction off (default), answers always\n");
    REQUIRE(home.run({"config", "get", "suites.research"}, &out) == 0);
    CHECK(
        out.ends_with("validate: verifier utility (default), tool_args on, extraction off "
                      "(default), answers always\n"));

    const std::string before = home.config_text();
    const std::vector<std::pair<std::vector<std::string>, std::string>> refused{
        {{"config", "set-suite", "research", "--verifier", "vision"},
         "validate: 'paid' is billed per call -- a check runs on Apogee's initiative, which "
         "never spends"},
        {{"config", "set-suite", "research", "--utility", "paid"},
         "validate: 'paid' is billed per call"},
        {{"config", "set-suite", "research", "--remove", "utility"},
         "validate: the verifier is the utility member, and the suite has none"},
        {{"config", "set-suite", "research", "--verifier", "chat"}, "the root itself"},
        {{"config", "set-suite", "research", "--validate", "quorum=2"},
         "--validate quorum=2: not SEAM=VALUE with SEAM one of tool_args, extraction, answers, "
         "nor off"},
        {{"config", "set-suite", "research", "--validate", "tool_args=maybe"},
         "--validate tool_args=maybe: tool_args is on or off"},
        {{"config", "set-suite", "research", "--validate", "answers=often"},
         "--validate answers=often: answers is request or always"},
        {{"config", "add-suite", "x", "--chat", "root", "--validate", "tool_args=on"},
         "validate: the verifier is the utility member, and the suite has none"},
    };
    for (const auto& [args, said] : refused) {
        INFO(said);
        CHECK(home.run(args, &out) != 0);
        CHECK(out.find(said) != std::string::npos);
        CHECK(home.config_text() == before);
    }

    // Changed in place, a seam reset to its default, and the block cleared.
    REQUIRE(home.run({"config", "set-suite", "research", "--validate", "extraction=on",
                      "--validate", "answers="},
                     &out) == 0);
    CHECK(
        home.config_text().ends_with("    validate:\n      tool_args: on\n      extraction: on\n"));
    REQUIRE(home.run({"config", "set-suite", "research", "--validate", "tool_args=off"}, &out) ==
            0);
    CHECK(home.config_text().ends_with(
        "    validate:\n      tool_args: off\n      extraction: on\n"));
    REQUIRE(home.run({"config", "set-suite", "research", "--validate", "off"}, &out) == 0);
    CHECK(home.config_text().ends_with("      utility: helper\n"));
    CHECK_FALSE(apogee::harness::parse_config(home.config_text(), "<test>")
                    .find_suite("research")
                    ->validate.any());
}

TEST_CASE(
    "the suite verbs switch orchestration, refusing a suite whose symphonies reach a billed "
    "member",
    "[commands][config][suites][orchestrate]") {
    const CliHome probe{""};
    const std::filesystem::path script = probe.home() / "paid.json";
    {
        std::ofstream out{script};
        out << R"({"metered": true, "turns": [{"text": "paid"}]})";
    }
    const CliHome home{std::string{kSuiteConfig} +
                       "  paid:\n    type: mock\n    model_path: " + script.string() + "\n"};
    std::string out;
    REQUIRE(home.run({"config", "add-suite", "research", "--chat", "root", "--utility", "helper",
                      "--orchestrate", "on"},
                     &out) == 0);
    INFO(out);
    CHECK(
        home.config_text().ends_with("\nsuites:\n  research:\n    members:\n      chat: root\n"
                                     "      utility: helper\n    orchestrate: true\n"));
    REQUIRE(home.run({"config", "get", "suites.research.orchestrate"}, &out) == 0);
    CHECK(out == "on\n");
    REQUIRE(home.run({"config", "get", "suites.research"}, &out) == 0);
    CHECK(out == "chat: root\nutility: helper\norchestrate: on\n");

    // At config time, through the one editor's verbs: a member a symphony
    // reaches that bills per call is refused, naming member and symphony, and
    // the file is left as it was.
    const std::string before = home.config_text();
    const std::vector<std::pair<std::vector<std::string>, std::string>> refused{
        {{"config", "set-suite", "research", "--utility", "paid"},
         "set-suite: orchestrate: 'summarize-verify' reaches utility ('paid') through its "
         "summarize stage (utility), and 'paid' is billed per call -- a play the model starts "
         "runs on its initiative, which never spends"},
        {{"config", "add-suite", "x", "--chat", "paid", "--orchestrate", "on"},
         "add-suite: orchestrate: 'summarize-verify' reaches utility ('paid') through its "
         "summarize stage (utility)"},
        {{"config", "set-suite", "research", "--orchestrate", "maybe"},
         "--orchestrate maybe: on or off"},
    };
    for (const auto& [args, said] : refused) {
        INFO(said);
        CHECK(home.run(args, &out) != 0);
        CHECK(out.find(said) != std::string::npos);
        CHECK(home.config_text() == before);
    }
    // Off removes the key; on writes it back in place.
    REQUIRE(home.run({"config", "set-suite", "research", "--orchestrate", "off"}, &out) == 0);
    CHECK(home.config_text().ends_with("      utility: helper\n"));
    REQUIRE(home.run({"config", "get", "suites.research.orchestrate"}, &out) == 0);
    CHECK(out == "off\n");
    // Off, the billed member is the suite's business alone.
    REQUIRE(home.run({"config", "set-suite", "research", "--utility", "paid"}, &out) == 0);
    CHECK(home.run({"config", "set-suite", "research", "--orchestrate", "on"}, &out) != 0);
    CHECK(out.find("'paid' is billed per call") != std::string::npos);
}

TEST_CASE("config get reads attachments.graph: empty unset, the word the editor wrote",
          "[commands][config][attachments]") {
    // 27p: no verb writes the block -- the one editor does, or a hand edit --
    // and `config get` reads it back.
    const CliHome home{kConfig};
    std::string out;
    REQUIRE(home.run({"config", "get", "attachments.graph"}, &out) == 0);
    CHECK(out == "\n");
    apogee::harness::edit_config_file(home.config_path(), [](std::string_view content) {
        return apogee::harness::set_attachments_graph(content, "off");
    });
    CHECK(home.config_text() == std::string{kConfig} + "\nattachments:\n  graph: off\n");
    out.clear();
    REQUIRE(home.run({"config", "get", "attachments.graph"}, &out) == 0);
    CHECK(out == "off\n");
}

TEST_CASE("every word the suite member flags complete to, the suite verbs take",
          "[commands][config][suites][completion]") {
    // ADR 0007: the offer is a contract. The words read from the live parser,
    // as `__complete` reads them; each one given to add-suite on a suite with
    // every role a member, so the role is never what refuses.
    const CliHome home{kSuiteConfig};
    const apogee::commands::RootCommand root{apogee::commands::default_registry()};
    const apogee::commands::CommandSpec tree = apogee::commands::specs_from_app(root.app());
    const apogee::harness::Config config = apogee::harness::load_config(home.config_path());
    const auto offered = [&](const std::string& flag, const std::string& typed) {
        apogee::commands::CompletionRequest request;
        request.words = {"config", "add-suite", "s", flag};
        request.current = typed;
        return apogee::commands::completion_candidates(request, config, tree);
    };
    int made = 0;
    const auto takes = [&](const std::string& flag, const std::string& word) {
        std::string out;
        const int code =
            home.run({"config", "add-suite", "s" + std::to_string(made++), "--chat", "root",
                      "--embedding", "embedder", "--extraction", "helper", "--vision", "helper",
                      "--transcription", "helper", "--utility", "helper", flag, word},
                     &out);
        INFO(flag << " " << word << ": " << out);
        return code == 0;
    };

    const std::vector<std::string> seams = offered("--validate", "");
    CHECK(seams.size() > 4);
    for (const std::string& word : seams) {
        CHECK(takes("--validate", word));
    }
    const std::vector<std::string> keys = offered("--toolset", "");
    REQUIRE_FALSE(keys.empty());
    for (const std::string& key : keys) {
        const std::vector<std::string> values = offered("--toolset", key);
        REQUIRE_FALSE(values.empty());
        for (const std::string& word : values) {
            CHECK(takes("--toolset", word));
        }
        // And past a comma, the rest of the list.
        const std::vector<std::string> more = offered("--toolset", values.front() + ",");
        REQUIRE_FALSE(more.empty());
        CHECK(takes("--toolset", more.back()));
    }
    for (const std::string& role : offered("--consultable", "")) {
        CHECK(takes("--consultable", role));
        for (const std::string& pair : offered("--consultable", role + ",")) {
            CHECK(takes("--consultable", pair));
        }
    }
}

// --- config scan (M12) -------------------------------------------------------

namespace {

/// A stored GGUF at any model/id, for the scan's multi-model cases.
std::filesystem::path scan_gguf(const apogee::testing::CliHome& home, const std::string& model,
                                const std::string& id, const std::string& name,
                                bool projector = false) {
    const std::filesystem::path file = home.models() / model / "gguf" / id / name;
    write_file(file, apogee::testing::minimal_gguf("llama"));
    if (projector) {
        write_file(file.parent_path() / (file.stem().string() + "-mmproj.gguf"), "projector");
    }
    return file;
}

}  // namespace

TEST_CASE("config scan: the store's rows, and --register as the batch add-backend, byte for byte",
          "[commands][config][scan][store]") {
    const CliHome home{kConfig};
    const std::filesystem::path f16 = scan_gguf(home, "org--a", "111111111111", "a-F16.gguf",
                                                /*projector=*/true);
    const std::filesystem::path projector = f16.parent_path() / "a-F16-mmproj.gguf";
    const std::filesystem::path q4 = scan_gguf(home, "org--a", "222222222222", "a-Q4_K_M.gguf");
    write_file(home.models() / "org--a" / "safetensors" / "333333333333" / "model.safetensors",
               "weights");
    write_file(home.models() / "org--a" / "safetensors" / "333333333333" / "config.json", "{}");

    std::string out;
    REQUIRE(home.run({"config", "scan"}, &out) == 0);
    INFO(out);
    // Default names in the chain's spelling (M3): the level's underscores gone.
    CHECK(out.find("a-F16") != std::string::npos);
    CHECK(out.find("a-Q4KM") != std::string::npos);
    CHECK(out.find("not registered") != std::string::npos);
    CHECK(out.find("(projector beside it)") != std::string::npos);
    CHECK(out.find("1 SafeTensors snapshot(s) not listed") != std::string::npos);
    CHECK(out.find("apogee config scan --register") != std::string::npos);
    // A report is not a write.
    CHECK(home.config_text() == kConfig);

    // The composition pin: --register equals the hand-typed add-backends.
    const CliHome typed{kConfig};
    REQUIRE(home.run({"config", "scan", "--register"}, &out) == 0);
    INFO(out);
    CHECK(out.find("added backend 'a-F16'") != std::string::npos);
    CHECK(out.find("added backend 'a-Q4KM'") != std::string::npos);
    CHECK(out.find("registered 2 backend(s)") != std::string::npos);
    REQUIRE(typed.run({"config", "add-backend", "a-F16", "--type", "llamacpp", "--model-path",
                       f16.lexically_normal().string(), "--mmproj-path", projector.string()},
                      &out) == 0);
    REQUIRE(typed.run({"config", "add-backend", "a-Q4KM", "--type", "llamacpp", "--model-path",
                       q4.lexically_normal().string()},
                      &out) == 0);
    CHECK(home.config_text() == typed.config_text());
    CHECK(home.config_text().starts_with("# my models\nbackends:\n  # the cloud one\n"));

    // Idempotent: the registered rows say their backend, nothing is written.
    const std::string before = home.config_text();
    REQUIRE(home.run({"config", "scan", "--register"}, &out) == 0);
    INFO(out);
    CHECK(out.find("backend: a-F16") != std::string::npos);
    CHECK(out.find("backend: a-Q4KM") != std::string::npos);
    CHECK(out.find("nothing to register") != std::string::npos);
    CHECK(home.config_text() == before);
}

TEST_CASE("config scan: a taken name and a shared default name are skipped and said",
          "[commands][config][scan][store]") {
    constexpr const char* kTaken = R"(backends:
  b-F16:
    type: anthropic
    model: x
)";
    const CliHome home{kTaken};
    (void)scan_gguf(home, "org--b", "111111111111", "b-F16.gguf");
    (void)scan_gguf(home, "org--c", "222222222222", "c-F16.gguf");
    (void)scan_gguf(home, "org--c", "333333333333", "c-F16.gguf");

    std::string out;
    REQUIRE(home.run({"config", "scan", "--register"}, &out) == 0);
    INFO(out);
    CHECK(out.find("its default name belongs to a backend on another model") != std::string::npos);
    CHECK(out.find("2 stored models share this default name") != std::string::npos);
    CHECK(out.find("added backend") == std::string::npos);
    CHECK(home.config_text() == kTaken);
}

TEST_CASE("config scan without a config: the rows still render, init is named, --register refuses",
          "[commands][config][scan]") {
    const CliHome home{kConfig};
    (void)scan_gguf(home, "org--d", "111111111111", "d-F16.gguf");
    std::filesystem::remove(home.config_path());

    std::string out;
    std::string err;
    REQUIRE(home.run_default({"config", "scan"}, &out, &err) == 0);
    INFO(out);
    CHECK(out.find("d-F16") != std::string::npos);
    CHECK(out.find("no config file yet -- 'apogee config init' writes one") != std::string::npos);

    REQUIRE(home.run_default({"config", "scan", "--register"}, &out, &err) != 0);
    INFO(err);
    CHECK(err.find("run 'apogee config init' first") != std::string::npos);
}
