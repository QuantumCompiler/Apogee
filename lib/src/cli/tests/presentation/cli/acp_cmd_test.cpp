#include "cli/acp_cmd.h"

#include <CLI/CLI.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cli/chat_session.h"
#include "cli/tui_child.h"
#include "contracts/types.h"
#include "logger/session.h"
#include "operations/acp_registration.h"
#include "platform/child_process.h"
#include "platform/platform.h"
#include "support/channel_guard.h"
#include "support/cli_home.h"
#include "support/env_guard.h"

/// `apogee acp` (56a) as a verb: its help, its completion -- execute's own
/// flags, by reference -- the protocol's flags refused in its words, the
/// models it offers and the flags it hands each session. The protocol itself
/// is `cli.acp_conformance`'s, on the real binary.
namespace {

using apogee::testing::CliHome;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;

constexpr std::string_view kConfig = R"(backends:
  local:
    type: mock
    model: mock-1
  spare:
    type: mock
    model: mock-2
  claude:
    type: claude-cli

models:
  default: spare
)";

}  // namespace

TEST_CASE("acp is a verb with help, execute's flags complete for it, the protocol's hidden",
          "[cli][acp][completion]") {
    const CliHome home{std::string{kConfig}};
    std::string out;
    std::string err;
    REQUIRE(home.run({"--help"}, &out, &err) == 0);
    CHECK_THAT(out, ContainsSubstring("acp "));
    CHECK_THAT(out, ContainsSubstring("Agent Client Protocol"));
    REQUIRE(home.run({"acp", "--help"}, &out, &err) == 0);
    CHECK_THAT(out, ContainsSubstring("Add an Agent"));
    CHECK_THAT(out, ContainsSubstring("--permission-mode"));
    CHECK_THAT(out, ContainsSubstring("--allow-bypass"));
    CHECK_THAT(out, !ContainsSubstring("--output-format"));
    CHECK_THAT(out, !ContainsSubstring("--resume"));

    REQUIRE(home.run({"__complete", "a"}, &out, &err) == 0);
    CHECK_THAT(out, ContainsSubstring("acp\n"));
    REQUIRE(home.run({"__complete", "acp", "--"}, &out, &err) == 0);
    for (const char* offered :
         {"--model", "--suite", "--permission-mode", "--allow-bypass", "--allow", "--deny"}) {
        INFO(offered);
        CHECK_THAT(out, ContainsSubstring(std::string{offered} + "\n"));
    }
    for (const std::string_view hidden : apogee::commands::acp_protocol_flags()) {
        INFO(hidden);
        CHECK_THAT(out, !ContainsSubstring(std::string{hidden} + "\n"));
    }
    REQUIRE(home.run({"__complete", "acp", "--permission-mode", ""}, &out, &err) == 0);
    CHECK_THAT(out, ContainsSubstring("accept-edits\n"));
    REQUIRE(home.run({"__complete", "acp", "-m", ""}, &out, &err) == 0);
    CHECK_THAT(out, ContainsSubstring("spare"));
}

TEST_CASE("the flags the protocol owns are refused at launch, in the command's words",
          "[cli][acp]") {
    const CliHome home{std::string{kConfig}};
    for (const std::vector<std::string>& line :
         std::vector<std::vector<std::string>>{{"acp", "--output-format", "text"},
                                               {"acp", "--input-format", "stream-json"},
                                               {"acp", "--resume", "x"},
                                               {"acp", "-c"},
                                               {"acp", "--private"},
                                               {"acp", "--no-color"},
                                               {"acp", "--raw"}}) {
        std::string out;
        std::string err;
        INFO(line.at(1));
        CHECK(home.run(line, &out, &err) == 1);
        CHECK(out.empty());  // stdout is the protocol's, even refusing
        CHECK_THAT(err, StartsWith("apogee acp: --"));
        CHECK_THAT(err, ContainsSubstring(" is refused: "));
    }
}

TEST_CASE("an install with nothing to chat with is refused before the protocol starts",
          "[cli][acp]") {
    const CliHome home{"backends: {}\n"};
    std::string out;
    std::string err;
    CHECK(home.run({"acp"}, &out, &err) == 1);
    CHECK(out.empty());
    if (!apogee::platform::supports_child_processes()) {
        // Windows: no session can start there, and that is said first.
        CHECK_THAT(err, ContainsSubstring("this platform cannot start yet"));
        return;
    }
    CHECK_THAT(err, ContainsSubstring("no backend is configured to chat with"));
}

TEST_CASE("the models offered: every chat backend, the default first, -m over it",
          "[cli][acp][models]") {
    apogee::harness::Config config;
    config.backends["local"].type = apogee::harness::BackendType::Mock;
    config.backends["local"].model = "mock-1";
    config.backends["spare"].type = apogee::harness::BackendType::Mock;
    config.backends["claude"].type = apogee::harness::BackendType::ClaudeCli;
    config.models.default_backend = "spare";

    std::vector<std::string> values;
    for (const apogee::acp::ModelChoice& choice : apogee::commands::acp_models(config, {})) {
        values.push_back(choice.value);
    }
    // The subscription CLI among them, as the macOS app offers it.
    CHECK(values == std::vector<std::string>{"spare", "claude", "local"});
    CHECK(apogee::commands::acp_models(config, {}).at(2).description == "mock · mock-1");
    CHECK(apogee::commands::acp_models(config, "local").front().value == "local");
    // A model pinned on a backend is what the session runs: offered first.
    CHECK(apogee::commands::acp_models(config, "local:other").front().value == "local:other");
}

TEST_CASE("each session's child gets the flags the acp line carries, as given", "[cli][acp]") {
    CLI::App app{"t"};
    CLI::App* acp = app.add_subcommand("acp");
    auto flags = std::make_shared<apogee::commands::SessionFlags>();
    apogee::commands::bind_session_flags(*acp, flags, apogee::commands::SessionMode::Execute);
    app.parse(
        "acp -m spare --permission-mode plan --allow-bypass --allow write_file --allow edit_file "
        "--tools -v");
    const std::vector<std::string> words = apogee::commands::forwarded_flags(*acp);
    const auto has = [&words](const std::vector<std::string>& run) {
        return std::search(words.begin(), words.end(), run.begin(), run.end()) != words.end();
    };
    CHECK(has({"--model", "spare"}));
    CHECK(has({"--permission-mode", "plan"}));
    CHECK(has({"--allow-bypass"}));
    CHECK(has({"--allow", "write_file", "--allow", "edit_file"}));
    CHECK(has({"--verbose"}));
    // Every session has --tools already.
    CHECK(std::ranges::find(words, "--tools") == words.end());
    CHECK(std::ranges::find(words, "true") == words.end());
}

TEST_CASE("a relative --config reaches each session's child absolute, though it starts elsewhere",
          "[cli][acp]") {
    // Each session's child starts in the host's project folder: the root
    // `acp` read its models from has to be the file the child reads too.
    apogee::commands::RootContext context;
    context.config_path = "home/config/config.json";
    const apogee::platform::ChildCommand child =
        apogee::commands::self_command(context, "/bin/apogee", {"execute"});
    REQUIRE(child.arguments.size() == 3);
    CHECK(child.arguments.at(0) == "--config");
    const std::filesystem::path forwarded{child.arguments.at(1)};
    CHECK(forwarded.is_absolute());
    CHECK(forwarded == std::filesystem::absolute("home/config/config.json"));
    CHECK(child.arguments.at(2) == "execute");

    // An absolute one is forwarded as given; none, none.
    context.config_path = (std::filesystem::temp_directory_path() / "c.json").string();
    CHECK(
        std::filesystem::path{
            apogee::commands::self_command(context, "/bin/apogee", {"execute"}).arguments.at(1)} ==
        std::filesystem::path{context.config_path});
    context.config_path.clear();
    CHECK(apogee::commands::self_command(context, "/bin/apogee", {"execute"}).arguments ==
          std::vector<std::string>{"execute"});
}

TEST_CASE("the store acp reads is the one chats list and chats show read", "[cli][acp][sessions]") {
    // 56c: session/list's chats are chats list's, with who began each and
    // where; a load's entries are chats show's turns in its words.
    const CliHome home{std::string{kConfig}};
    apogee::logger::Session hosted;
    hosted.chat_id = "20261101-120000-acp0";
    hosted.title = "Read the notes";
    hosted.backend = "local";
    hosted.origin = "acp-test";
    hosted.cwd = "/work/app";
    apogee::harness::ChatMessage calls = apogee::harness::ChatMessage::assistant("Reading it.");
    calls.tool_calls = {apogee::harness::ToolCall{"c1", "read_file", R"({"path": "notes.md"})"}};
    hosted.messages = {
        apogee::harness::ChatMessage::user("What is in notes.md?"),
        calls,
        apogee::harness::ChatMessage::from_tool_result(
            apogee::harness::ToolResult{"c1", "read_file", "line one\n", false}),
        apogee::harness::ChatMessage::assistant("One line."),
        apogee::harness::ChatMessage::user("and then?"),
    };
    hosted.turns = 1;
    hosted.attachments = {apogee::logger::Attachment{.name = "notes.md",
                                                     .files = {{.name = "notes.md",
                                                                .path = "/work/app/notes.md",
                                                                .sha256 = "ab12",
                                                                .reader = "text",
                                                                .bytes = 9}},
                                                     .inline_at = 0,
                                                     .map_at = {},
                                                     .graph = {}}};
    apogee::logger::save(hosted);
    apogee::logger::Session plain;
    plain.chat_id = "20261101-110000-0000";
    plain.backend = "local";
    plain.turns = 1;
    plain.messages = {apogee::harness::ChatMessage::user("hi"),
                      apogee::harness::ChatMessage::assistant("hello")};
    apogee::logger::save(plain);

    const apogee::acp::SavedChats chats = apogee::commands::acp_saved_chats();
    const std::vector<apogee::acp::ListedChat> listed = chats.list();
    REQUIRE(listed.size() == 2);
    const auto found = std::ranges::find(listed, hosted.chat_id, &apogee::acp::ListedChat::id);
    REQUIRE(found != listed.end());
    CHECK(found->name == "Read the notes");
    CHECK(found->origin == "acp-test");
    CHECK(found->cwd == "/work/app");
    CHECK(apogee::acp::listed_sessions(listed, "").size() == 1);

    std::string error;
    const std::optional<apogee::acp::SavedChat> chat = chats.read(hosted.chat_id, error);
    REQUIRE(chat.has_value());
    CHECK(error.empty());
    CHECK(chat->id == hosted.chat_id);
    const std::vector<apogee::acp::SavedEntry>& entries = chat->entries;
    REQUIRE(entries.size() == 2);
    CHECK(entries.at(0).user == "What is in notes.md?\nattached: notes.md (1 file, 9 B)");
    REQUIRE(entries.at(0).tools.size() == 1);
    CHECK(entries.at(0).tools[0].name == "read_file");
    CHECK(entries.at(0).tools[0].summary == R"({"path":"notes.md"})");
    CHECK(entries.at(0).answer == std::vector<std::string>{"Reading it.", "One line."});
    // A turn left unanswered: the user's words, nothing after.
    CHECK(entries.at(1).user == "and then?");
    CHECK(entries.at(1).answer.empty());

    // Read as `--resume` reads: its title answers too, and the chat's own id
    // says it was not the id (the agent opens by the id alone).
    const std::optional<apogee::acp::SavedChat> by_title = chats.read("Read the notes", error);
    REQUIRE(by_title.has_value());
    CHECK(by_title->id == hosted.chat_id);

    error.clear();
    CHECK_FALSE(chats.read("no-such-chat", error).has_value());
    CHECK(error == "no chat named 'no-such-chat'");

    // The protocol's flags still refused, in words that say what changed.
    std::string out;
    std::string err;
    CHECK(home.run({"acp", "--private"}, &out, &err) == 1);
    CHECK_THAT(err, ContainsSubstring("saved as a chat"));
    CHECK(home.run({"acp", "--resume", "x"}, &out, &err) == 1);
    CHECK_THAT(err, ContainsSubstring("session/load or session/resume"));
}

// ---------------------------------------------------------------------------
// `acp registration` (56d): the fields Xcode's Add an Agent sheet takes.
// ---------------------------------------------------------------------------

TEST_CASE("acp registration prints this binary's fields, as text and as one document",
          "[cli][acp][registration]") {
    const CliHome home{std::string{kConfig}};
    const apogee::testing::EnvUnsetGuard no_config{"APOGEE_CONFIG"};
    const apogee::testing::EnvGuard path{"PATH", "/opt/test/bin:/usr/bin:/bin"};
    std::string out;
    std::string err;
    REQUIRE(home.run({"acp", "registration", "--output-format", "json"}, &out, &err) == 0);
    CHECK(err.empty());
    // stdout is the one document and nothing else.
    const nlohmann::json document = nlohmann::json::parse(out);
    CHECK(document["object"] == "acp.registration");
    CHECK(document["where"] == "Settings › Intelligence › Agents › Add an Agent");
    CHECK(document["name"] == "Apogee");
    CHECK(document["channel"] == "release");
    CHECK(document["interpreter"].is_null());
    CHECK(document["xcode_minimum"] == "26.6");
    CHECK(document["approval"] == std::string{apogee::operations::kXcodeApproval});
    // The running binary, absolute and there.
    const std::filesystem::path executable{document["executable"].get<std::string>()};
    CHECK(executable == apogee::platform::executable_path());
    CHECK(executable.is_absolute());
    CHECK(std::filesystem::exists(executable));
    // The --config this run was given, absolute, ahead of acp; the root's
    // variable and PATH in Environment, as this process has them.
    CHECK(document["arguments"] ==
          nlohmann::json::array({"--config", home.config_path().string(), "acp"}));
    CHECK(document["environment"] == nlohmann::json{{"APOGEE_HOME", home.home().string()},
                                                    {"PATH", "/opt/test/bin:/usr/bin:/bin"}});

    REQUIRE(home.run({"acp", "registration"}, &out, &err) == 0);
    CHECK_THAT(out, StartsWith("Add Apogee to Xcode 26.6 or later: Settings › Intelligence › "
                               "Agents › Add an Agent\n  Name:         Apogee\n  Executable:   " +
                               executable.string() + "\n  Interpreter:  (leave empty)\n"));
    CHECK_THAT(out, ContainsSubstring("  Arguments:    --config\n                " +
                                      home.config_path().string() + "\n                acp\n"));
    CHECK_THAT(out, ContainsSubstring("  Environment:  APOGEE_HOME=" + home.home().string() +
                                      "\n                PATH=/opt/test/bin:/usr/bin:/bin\n"));
    CHECK_THAT(out, ContainsSubstring("print these fields again after it changes"));
    CHECK_THAT(out, ContainsSubstring(std::string{apogee::operations::kXcodeApproval} + "\n"));
}

TEST_CASE("the registration's Name follows the build's channel", "[cli][acp][registration]") {
    const CliHome home{std::string{kConfig}};
    std::string out;
    std::string err;
    for (const auto& [channel, name] :
         std::vector<std::pair<apogee::harness::Channel, std::string>>{
             {apogee::harness::Channel::Dev, "Apogee (dev)"},
             {apogee::harness::Channel::Test, "Apogee (test)"},
             {apogee::harness::Channel::Release, "Apogee"}}) {
        const apogee::testing::BakedChannelGuard baked{channel};
        INFO(name);
        REQUIRE(home.run({"acp", "registration", "--output-format", "json"}, &out, &err) == 0);
        const nlohmann::json document = nlohmann::json::parse(out);
        CHECK(document["name"] == name);
        CHECK(document["channel"] == std::string{apogee::harness::channel_name(channel)});
    }
}

TEST_CASE("a root flag lands ahead of acp, absolute; acp's own flags follow it",
          "[cli][acp][registration]") {
    const CliHome home{std::string{kConfig}};
    std::string out;
    std::string err;
    // --custom names the install and its config: no second --config.
    REQUIRE(home.run_default({"--custom", home.config_path().string(), "acp", "--allow-bypass",
                              "-m", "local", "registration", "--output-format", "json"},
                             &out, &err) == 0);
    const nlohmann::json document = nlohmann::json::parse(out);
    CHECK(document["arguments"] ==
          nlohmann::json::array({"--custom", home.config_path().string(), "acp", "--model", "local",
                                 "--allow-bypass"}));
    // The protocol's flags are refused here as at launch.
    CHECK(home.run({"acp", "--resume", "x", "registration"}, &out, &err) == 1);
    CHECK_THAT(err, ContainsSubstring("--resume is refused"));
    CHECK(out.empty());
}

TEST_CASE("acp registration completes, and is no protocol verb",
          "[cli][acp][registration][completion]") {
    const CliHome home{std::string{kConfig}};
    std::string out;
    std::string err;
    REQUIRE(home.run({"__complete", "acp", ""}, &out, &err) == 0);
    CHECK_THAT(out, ContainsSubstring("registration\n"));
    REQUIRE(home.run({"__complete", "acp", "registration", "--output-format", ""}, &out, &err) ==
            0);
    CHECK_THAT(out, ContainsSubstring("json\n"));
    REQUIRE(home.run({"acp", "registration", "--help"}, &out, &err) == 0);
    CHECK_THAT(out, ContainsSubstring("Add an Agent"));
    CHECK_THAT(out, ContainsSubstring("--output-format"));
}

TEST_CASE("the registration's root and environment follow exactly what is set",
          "[cli][acp][registration]") {
    using apogee::operations::AcpRegistration;
    using apogee::operations::AcpRegistrationInputs;
    AcpRegistrationInputs inputs;
    inputs.executable = "/Users/you/.local/bin/apogee-dev";
    inputs.channel = apogee::harness::Channel::Dev;
    // A baked root: nothing but acp, nothing in Environment.
    AcpRegistration bare = apogee::operations::acp_registration(inputs);
    CHECK(bare.name == "Apogee (dev)");
    CHECK(bare.arguments == std::vector<std::string>{"acp"});
    CHECK(bare.environment.empty());
    CHECK(apogee::operations::render_acp_registration(bare) ==
          "Add Apogee (dev) to Xcode 26.6 or later: Settings › Intelligence › Agents › Add an "
          "Agent\n"
          "  Name:         Apogee (dev)\n"
          "  Executable:   /Users/you/.local/bin/apogee-dev\n"
          "  Interpreter:  (leave empty)\n"
          "  Arguments:    acp\n"
          "  Environment:  (none)\n"
          "Arguments and Environment Variables take one entry a line.\n" +
              std::string{apogee::operations::kXcodeApproval} + "\n");
    const nlohmann::json document = apogee::operations::acp_registration_document(bare);
    CHECK(document["environment"] == nlohmann::json::object());
    CHECK(document["arguments"] == nlohmann::json::array({"acp"}));

    // Each variable exactly when set, made absolute; PATH as given.
    inputs.apogee_home = "relative/home";
    AcpRegistration set = apogee::operations::acp_registration(inputs);
    REQUIRE(set.environment.size() == 1);
    CHECK(set.environment[0].first == "APOGEE_HOME");
    CHECK(std::filesystem::path{set.environment[0].second}.is_absolute());
    CHECK(set.environment[0].second ==
          (std::filesystem::current_path() / "relative/home").lexically_normal().string());
    // A rooted path is made absolute as the registration makes it: as given
    // on POSIX, with the current drive on Windows, where `/srv/...` is not.
    const auto made_absolute = [](const std::string& path) {
        return std::filesystem::absolute(path).lexically_normal().string();
    };
    inputs.apogee_config = "/srv/apogee/config/config.json";
    inputs.path = "/opt/homebrew/bin:/usr/bin";
    set = apogee::operations::acp_registration(inputs);
    CHECK(set.environment == std::vector<std::pair<std::string, std::string>>{
                                 {"APOGEE_HOME", set.environment[0].second},
                                 {"APOGEE_CONFIG", made_absolute("/srv/apogee/config/config.json")},
                                 {"PATH", "/opt/homebrew/bin:/usr/bin"}});

    // A channel flag, and --config beside it, ahead of acp.
    inputs = AcpRegistrationInputs{};
    inputs.executable = "/usr/local/bin/apogee";
    inputs.root_flag =
        apogee::harness::RootFlag{.channel = apogee::harness::Channel::Test, .custom_config = {}};
    inputs.config_flag = "/etc/apogee.json";
    CHECK(apogee::operations::acp_registration(inputs).arguments ==
          std::vector<std::string>{"--test", "--config", made_absolute("/etc/apogee.json"), "acp"});

    // No path to name: no registration.
    inputs.executable.clear();
    CHECK_THROWS_WITH(apogee::operations::acp_registration(inputs),
                      ContainsSubstring("no Executable to register"));
}
