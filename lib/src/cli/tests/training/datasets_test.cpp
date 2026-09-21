#include "training/datasets.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "harness/types.h"
#include "support/env_guard.h"

/// The dataset store and the miners: one chat-line shape with role before
/// content, the template's lines, atomic writes refusing an existing
/// dataset without force, listing with shapes, and sessions mined one
/// exchange at a time with blank sides skipped, tool-only turns waiting
/// for their answer, and the filters honoured.
namespace {

using apogee::harness::ChatMessage;
using apogee::training::chat_line;
using apogee::training::CreateSource;
using apogee::training::DatasetInfo;
using apogee::training::DatasetStore;
using apogee::training::mine_sessions;
using apogee::training::MinedSessions;
using apogee::training::SessionFilter;
using apogee::training::SessionView;
using apogee::training::template_lines;
using apogee::training::valid_dataset_name;
using apogee::training::validate_session_filter;

std::string bytes(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

}  // namespace

TEST_CASE("dataset names are plain", "[training][datasets]") {
    CHECK(valid_dataset_name("my-set_1.v2"));
    CHECK_FALSE(valid_dataset_name(""));
    CHECK_FALSE(valid_dataset_name(".hidden"));
    CHECK_FALSE(valid_dataset_name("a/b"));
    CHECK_FALSE(valid_dataset_name("a b"));
    CHECK_FALSE(valid_dataset_name(std::string(129, 'a')));
}

TEST_CASE("a chat line puts role before content, the order every producer writes",
          "[training][datasets]") {
    CHECK(
        chat_line("hi", "hello") ==
        R"({"messages":[{"role":"user","content":"hi"},{"role":"assistant","content":"hello"}]})");
    REQUIRE(template_lines().size() == 2);
    CHECK(template_lines()[0].find("\"role\":\"user\"") != std::string::npos);
}

TEST_CASE(
    "the store writes atomically, refuses an existing dataset without force, lists and "
    "removes",
    "[training][datasets][store]") {
    const apogee::testing::TempDir root{"datasets-" + std::to_string(std::random_device{}())};
    const DatasetStore store{root.path() / "datasets"};
    CHECK(store.list().empty());
    CHECK_FALSE(store.exists("a"));

    std::filesystem::path path;
    REQUIRE(store.write("a", template_lines(), false, &path).empty());
    CHECK(path == store.path_for("a"));
    CHECK(path == root.path() / "datasets" / "a.jsonl");
    CHECK(bytes(path) == template_lines()[0] + "\n" + template_lines()[1] + "\n");
    CHECK(store.exists("a"));
    CHECK(store.exists("a.jsonl"));

    const std::string refused = store.write("a", {}, false);
    CHECK(refused.find("already exists") != std::string::npos);
    CHECK(refused.find("--force") != std::string::npos);
    REQUIRE(store.write("a", {chat_line("x", "y")}, true).empty());
    CHECK(bytes(path) == chat_line("x", "y") + "\n");

    CHECK_FALSE(store.write("../escape", {}, false).empty());
    CHECK_FALSE(store.write(".dot", {}, false).empty());

    REQUIRE(store.write("flat", {R"({"prompt":"p","completion":"c"})"}, false).empty());
    REQUIRE(store.write("suite", {R"({"prompt":"p","expected":"e"})"}, false).empty());
    REQUIRE(store.write("none", {}, false).empty());
    std::ofstream{root.path() / "datasets" / "notes.txt"} << "ignored";

    const std::vector<DatasetInfo> listed = store.list();
    REQUIRE(listed.size() == 4);
    CHECK(listed[0].name == "a");
    CHECK(listed[0].shape == "chat");
    CHECK(listed[0].lines == 1);
    CHECK(listed[1].name == "flat");
    CHECK(listed[1].shape == "flat");
    CHECK(listed[2].name == "none");
    CHECK(listed[2].shape == "empty");
    CHECK(listed[3].name == "suite");
    CHECK(listed[3].shape == "eval");

    const std::optional<DatasetInfo> info = store.info("flat");
    REQUIRE(info.has_value());
    CHECK(info->bytes > 0);
    CHECK_FALSE(store.info("missing").has_value());

    REQUIRE(store.remove("flat").empty());
    CHECK_FALSE(store.exists("flat"));
    CHECK(store.remove("flat").find("no dataset named") != std::string::npos);

    // No temp file survives a write.
    for (const auto& entry : std::filesystem::directory_iterator(root.path() / "datasets")) {
        CHECK(entry.path().string().find(".tmp-") == std::string::npos);
    }
}

TEST_CASE("sessions are mined one completed exchange at a time", "[training][datasets][sessions]") {
    std::vector<ChatMessage> messages{
        ChatMessage::system("You are helpful."),
        ChatMessage::user("What is 2+2?"),
        ChatMessage::assistant("4"),
        ChatMessage::user("   "),  // a blank question: skipped
        ChatMessage::assistant("I cannot answer nothing."),
        ChatMessage::user("Read the file."),
    };
    // An assistant turn that only called a tool, then the tool's result,
    // then the answer: one exchange, pairing the question with the answer.
    ChatMessage tool_call = ChatMessage::assistant("");
    tool_call.tool_calls.push_back(apogee::harness::ToolCall{.id = "c1", .name = "read_file"});
    messages.push_back(tool_call);
    apogee::harness::ToolResult tool_result;
    tool_result.tool_call_id = "c1";
    tool_result.content = "file text";
    messages.push_back(ChatMessage::from_tool_result(tool_result));
    messages.push_back(ChatMessage::assistant("The file says hello."));
    messages.push_back(ChatMessage::user("Unanswered?"));

    std::vector<ChatMessage> other{ChatMessage::user("Q"), ChatMessage::assistant("A")};
    const std::vector<SessionView> sessions{
        {"mock", "2026-09-19T10:00:00Z", &messages},
        {"paid", "2026-09-01T10:00:00Z", &other},
    };

    MinedSessions mined = mine_sessions(sessions, {});
    REQUIRE(mined.lines.size() == 3);
    CHECK(mined.lines[0] == chat_line("What is 2+2?", "4"));
    CHECK(mined.lines[1] == chat_line("Read the file.", "The file says hello."));
    CHECK(mined.lines[2] == chat_line("Q", "A"));
    CHECK(mined.sessions == 2);
    CHECK(mined.skipped == 2);  // the blank question, the unanswered one

    mined = mine_sessions(sessions, SessionFilter{.backend = "paid"});
    REQUIRE(mined.lines.size() == 1);
    CHECK(mined.lines[0] == chat_line("Q", "A"));

    mined = mine_sessions(sessions, SessionFilter{.since = "2026-09-10"});
    CHECK(mined.lines.size() == 2);
    mined = mine_sessions(sessions, SessionFilter{.until = "2026-09-01"});
    CHECK(mined.lines.size() == 1);
    mined = mine_sessions(sessions, SessionFilter{.since = "2026-09-02", .until = "2026-09-18"});
    CHECK(mined.lines.empty());

    CHECK(validate_session_filter(SessionFilter{.since = "2026-9-1"}).find("--since") !=
          std::string::npos);
    CHECK(validate_session_filter(SessionFilter{.until = "yesterday"}).find("--until") !=
          std::string::npos);
    CHECK(validate_session_filter(SessionFilter{.since = "2026-09-01"}).empty());
}

TEST_CASE("create sources are named", "[training][datasets]") {
    CHECK(apogee::training::create_source_from_string("sessions") == CreateSource::Sessions);
    CHECK(apogee::training::create_source_from_string("chat-logs") == std::nullopt);
    CHECK(apogee::training::to_string(CreateSource::Empty) == "empty");
}
