#include "cli/acp_cmd.h"

#include <CLI/CLI.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "cli/chat_session.h"
#include "cli/tui_child.h"
#include "contracts/types.h"
#include "logger/session.h"
#include "platform/child_process.h"
#include "support/cli_home.h"

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
