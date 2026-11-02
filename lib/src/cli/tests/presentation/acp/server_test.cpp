#include "acp/server.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <ostream>
#include <random>
#include <sstream>
#include <streambuf>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "platform/child_process.h"
#include "support/env_guard.h"

/// The agent's sessions over their lifetime (56a): a host that keeps one
/// `apogee acp` across chats opens a session per chat, and each one whose
/// child has ended is let go at the next `session/new` -- unless the host
/// still holds a question of it, which would answer into a session gone.
/// The protocol itself is `cli.acp_conformance`'s, on the real binary. And
/// the host's MCP servers (56b) reach a session's child in a handover, the
/// option and a path on its argv and the values in the file alone.
namespace {

using Catch::Matchers::ContainsSubstring;

/// A stream written from the sessions' reader threads and read from the
/// test's: every write and read under one lock.
class LockedSink : public std::streambuf {
public:
    [[nodiscard]] std::string text() {
        const std::lock_guard lock{mutex_};
        return text_;
    }

protected:
    int_type overflow(int_type c) override {
        if (!traits_type::eq_int_type(c, traits_type::eof())) {
            const std::lock_guard lock{mutex_};
            text_ += traits_type::to_char_type(c);
        }
        return traits_type::not_eof(c);
    }

    std::streamsize xsputn(const char* bytes, std::streamsize count) override {
        const std::lock_guard lock{mutex_};
        text_.append(bytes, static_cast<std::size_t>(count));
        return count;
    }

private:
    std::mutex mutex_;
    std::string text_;
};

[[nodiscard]] std::size_t count_of(const std::string& text, std::string_view needle) {
    std::size_t count = 0;
    for (std::size_t at = text.find(needle); at != std::string::npos;
         at = text.find(needle, at + needle.size())) {
        ++count;
    }
    return count;
}

/// Whether `sink` says `needle` `times` times within 20 s.
[[nodiscard]] bool says(LockedSink& sink, std::string_view needle, std::size_t times = 1) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{20};
    while (std::chrono::steady_clock::now() < deadline) {
        if (count_of(sink.text(), needle) >= times) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
    }
    return false;
}

/// The `sessionId` that answered request `id`, from the frames sent.
[[nodiscard]] std::string session_answering(const std::string& frames, int id) {
    std::size_t start = 0;
    while (start < frames.size()) {
        const std::size_t end = frames.find('\n', start);
        const nlohmann::json frame = nlohmann::json::parse(
            frames.substr(start, end == std::string::npos ? std::string::npos : end - start),
            nullptr, false);
        if (frame.is_object() && frame.value("id", nlohmann::json{}) == id &&
            frame.contains("result")) {
            return frame.at("result").value("sessionId", std::string{});
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return {};
}

[[nodiscard]] std::string open_request(int id, const std::filesystem::path& cwd) {
    return nlohmann::json{
        {"jsonrpc", "2.0"},
        {"id", id},
        {"method", "session/new"},
        {"params", {{"cwd", cwd.string()}, {"mcpServers", nlohmann::json::array()}}}}
        .dump();
}

}  // namespace

TEST_CASE("a session that could not start is let go at the next session/new",
          "[acp][server][sessions]") {
    // Every platform: a child that is not there fails its start at once.
    const apogee::testing::TempDir folder{"acp-server-" + std::to_string(std::random_device{}())};
    apogee::acp::AgentSetup setup;
    setup.child.program = "apogee-acp-test-no-such-program";
    LockedSink out;
    LockedSink err;
    std::ostream out_stream{&out};
    std::ostream err_stream{&err};
    apogee::acp::Agent agent{setup, out_stream, err_stream};

    for (int id = 1; id <= 3; ++id) {
        agent.handle(open_request(id, folder.path()));
        // The one just opened is held; every one before it, ended, is gone.
        CHECK(agent.sessions_held() == 1);
    }
    CHECK_THAT(out.text(), ContainsSubstring("could not start"));
    agent.finish();
}

TEST_CASE(
    "a replaced session is let go once its child has ended and the host holds no question "
    "of it",
    "[acp][server][sessions]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("this platform spawns no child processes");
    }
    const apogee::testing::TempDir folder{"acp-server-" + std::to_string(std::random_device{}())};
    // A child that opens, asks the host one permission question and ends.
    apogee::acp::AgentSetup setup;
    setup.child.program = "sh";
    setup.child.arguments = {
        "-c",
        "printf '%s\\n' '{\"type\":\"session\",\"model\":\"m\"}' "
        "'{\"type\":\"question\",\"kind\":\"permission\",\"tool\":\"write_file\",\"target\":"
        "\"a.txt\",\"tool_call_id\":\"c1\",\"questions\":[]}'"};
    setup.model = "m";
    LockedSink out;
    LockedSink err;
    std::ostream out_stream{&out};
    std::ostream err_stream{&err};
    apogee::acp::Agent agent{setup, out_stream, err_stream};

    // Two sessions, each opened, asking and ended, neither question answered.
    agent.handle(open_request(1, folder.path()));
    REQUIRE(says(out, "session/request_permission"));
    REQUIRE(says(err, "apogee's session ended"));
    const std::string first = session_answering(out.text(), 1);
    REQUIRE_FALSE(first.empty());
    agent.handle(open_request(2, folder.path()));
    REQUIRE(says(out, "session/request_permission", 2));
    REQUIRE(says(err, "apogee's session ended", 2));
    CHECK(agent.sessions_held() == 2);

    // The first's question answered: the next session/new lets it go, and
    // keeps the second, whose question the host still holds.
    agent.handle(
        R"({"jsonrpc":"2.0","id":1,"result":{"outcome":{"outcome":"selected","optionId":"yes"}}})");
    agent.handle(open_request(3, folder.path()));
    CHECK(agent.sessions_held() == 2);
    // A session let go is still known as replaced.
    agent.handle(
        nlohmann::json{{"jsonrpc", "2.0"},
                       {"id", 4},
                       {"method", "session/prompt"},
                       {"params", {{"sessionId", first}, {"prompt", nlohmann::json::array()}}}}
            .dump());
    CHECK(says(out, "has ended: a newer session replaced it"));

    // The second's and the third's answered, after their sessions ended:
    // each reaches nobody, and both sessions go at the next session/new.
    REQUIRE(says(out, "session/request_permission", 3));
    REQUIRE(says(err, "apogee's session ended", 3));
    agent.handle(
        R"({"jsonrpc":"2.0","id":2,"result":{"outcome":{"outcome":"selected","optionId":"no"}}})");
    agent.handle(R"({"jsonrpc":"2.0","id":3,"error":{"code":-32603,"message":"gone"}})");
    agent.handle(open_request(5, folder.path()));
    CHECK(agent.sessions_held() == 1);
    agent.finish();
}

TEST_CASE("the host's MCP servers reach the session's child in a handover, never on its argv",
          "[acp][server][host-servers]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("this platform spawns no child processes");
    }
    const apogee::testing::TempDir folder{"acp-server-" + std::to_string(std::random_device{}())};
    const std::filesystem::path seen = folder.path() / "seen.txt";
    // A child that writes down its arguments and what the handover holds --
    // and how it may be read -- then opens.
    apogee::acp::AgentSetup setup;
    setup.child.program = "sh";
    setup.child.arguments = {
        "-c",
        "printf '%s\\n' \"$@\" > \"$0\"; ls -l \"$2\" >> \"$0\"; "
        "ls -ld \"$(dirname \"$2\")\" >> \"$0\"; cat \"$2\" >> \"$0\"; "
        "printf '%s\\n' '{\"type\":\"session\",\"model\":\"m\"}'; cat >/dev/null",
        seen.string()};
    setup.model = "m";
    setup.handover_root = folder.path();
    LockedSink out;
    LockedSink err;
    std::ostream out_stream{&out};
    std::ostream err_stream{&err};
    apogee::acp::Agent agent{setup, out_stream, err_stream};

    agent.handle(nlohmann::json{
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "session/new"},
        {"params",
         {{"cwd", folder.path().string()},
          {"mcpServers",
           nlohmann::json::parse(
               R"([{"name":"xcode-tools","command":"/usr/bin/xcrun","args":["mcpbridge"],
                        "env":[{"name":"MCP_XCODE_SESSION_ID","value":"s3cr3t-value"}]},
                       {"type":"http","name":"remote","url":"http://x/","headers":[]}])")}}}}
                     .dump());
    REQUIRE(says(out, "\"sessionId\""));
    std::string recorded;
    {
        std::ifstream in{seen};
        std::ostringstream buffer;
        buffer << in.rdbuf();
        recorded = buffer.str();
    }
    // The option and a path on argv; the value in the file alone.
    REQUIRE_THAT(recorded, ContainsSubstring("--acp-host-servers\n"));
    const std::size_t at = recorded.find("--acp-host-servers\n") + 19;
    const std::string handover = recorded.substr(at, recorded.find('\n', at) - at);
    CHECK_THAT(recorded.substr(0, at + handover.size()), !ContainsSubstring("s3cr3t-value"));
    CHECK(std::filesystem::path{handover}.parent_path().parent_path() == folder.path());
    CHECK_THAT(recorded, ContainsSubstring("-rw-------"));
    CHECK_THAT(recorded, ContainsSubstring("drwx------"));
    CHECK_THAT(recorded, ContainsSubstring("MCP_XCODE_SESSION_ID=s3cr3t-value"));
    CHECK_THAT(recorded, ContainsSubstring("skipped: remote"));
    // The child opened without removing it: the session did.
    CHECK_FALSE(std::filesystem::exists(handover));
    CHECK_FALSE(std::filesystem::exists(std::filesystem::path{handover}.parent_path()));
    // stderr names the server and the env variable, never the value.
    CHECK_THAT(err.text(),
               ContainsSubstring("the host's MCP servers: xcode-tools (/usr/bin/xcrun; env "
                                 "MCP_XCODE_SESSION_ID)"));
    CHECK_THAT(err.text(), !ContainsSubstring("s3cr3t-value"));
    CHECK_THAT(out.text(), !ContainsSubstring("s3cr3t-value"));
    agent.finish();
}

TEST_CASE("a session with no host servers gets no handover", "[acp][server][host-servers]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("this platform spawns no child processes");
    }
    const apogee::testing::TempDir folder{"acp-server-" + std::to_string(std::random_device{}())};
    const std::filesystem::path seen = folder.path() / "seen.txt";
    apogee::acp::AgentSetup setup;
    setup.child.program = "sh";
    setup.child.arguments = {"-c",
                             "printf '%s\\n' \"$@\" > \"$0\"; printf '%s\\n' "
                             "'{\"type\":\"session\",\"model\":\"m\"}'; cat >/dev/null",
                             seen.string()};
    setup.model = "m";
    setup.handover_root = folder.path();
    LockedSink out;
    LockedSink err;
    std::ostream out_stream{&out};
    std::ostream err_stream{&err};
    apogee::acp::Agent agent{setup, out_stream, err_stream};
    agent.handle(open_request(1, folder.path()));
    REQUIRE(says(out, "\"sessionId\""));
    // Its argv is who began the chat and where (56c), and no handover.
    std::ifstream in{seen};
    std::ostringstream argv;
    argv << in.rdbuf();
    CHECK(argv.str() == "--acp-origin\nacp\n--acp-cwd\n" + folder.path().string() + "\n");
    CHECK_THAT(err.text(), !ContainsSubstring("MCP"));
    agent.finish();
}

namespace {

/// Every frame sent, in order.
[[nodiscard]] std::vector<nlohmann::json> frames_of(const std::string& text) {
    std::vector<nlohmann::json> frames;
    std::istringstream in{text};
    for (std::string line; std::getline(in, line);) {
        const nlohmann::json frame = nlohmann::json::parse(line, nullptr, false);
        if (frame.is_object()) {
            frames.push_back(frame);
        }
    }
    return frames;
}

/// Where in `frames` request `id` was answered, or -1.
[[nodiscard]] std::ptrdiff_t answered_at(const std::vector<nlohmann::json>& frames, int id) {
    for (std::size_t at = 0; at < frames.size(); ++at) {
        if (frames[at].value("id", nlohmann::json{}) == id &&
            (frames[at].contains("result") || frames[at].contains("error"))) {
            return static_cast<std::ptrdiff_t>(at);
        }
    }
    return -1;
}

[[nodiscard]] std::string request(int id, std::string_view method, const nlohmann::json& params) {
    return nlohmann::json{
        {"jsonrpc", "2.0"}, {"id", id}, {"method", std::string{method}}, {"params", params}}
        .dump();
}

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
    std::ifstream in{path};
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

/// A setup whose child writes down its arguments, then opens as the chat
/// `chat_id` and reads until its stdin ends -- as execute does.
[[nodiscard]] apogee::acp::AgentSetup recording_setup(const std::filesystem::path& seen,
                                                      std::string_view chat_id) {
    apogee::acp::AgentSetup setup;
    setup.child.program = "sh";
    setup.child.arguments = {"-c",
                             "printf '%s\\n' \"$@\" '---' >> \"$0\"; printf '%s\\n' "
                             "'{\"type\":\"session\",\"model\":\"m\",\"chat_id\":\"" +
                                 std::string{chat_id} + "\"}'; cat >/dev/null",
                             seen.string()};
    setup.model = "m";
    // The store, as the composition root hands it in: one chat, two turns.
    setup.chats.list = [] {
        return std::vector<apogee::acp::ListedChat>{
            {.id = "chat-2",
             .name = "Elsewhere",
             .updated_at = "2026-11-01T12:02:00Z",
             .origin = "acp-test",
             .cwd = "/nowhere"},
            {.id = "chat-1",
             .name = "Fix the build",
             .updated_at = "2026-11-01T12:01:00Z",
             .origin = "acp-test",
             .cwd = "/work"},
            {.id = "chat-0", .name = "From a terminal", .updated_at = "2026-11-01T12:00:00Z"}};
    };
    // Read as `--resume` reads a chat: by its id, or by its name.
    setup.chats.read = [](const std::string& id,
                          std::string& error) -> std::optional<apogee::acp::SavedChat> {
        if (id != "chat-1" && id != "Fix the build") {
            error = "no chat named '" + id + "'";
            return std::nullopt;
        }
        std::vector<apogee::acp::SavedEntry> entries(2);
        entries[0].user = "first";
        entries[0].answer = {"one"};
        entries[1].user = "second";
        entries[1].answer = {"two"};
        return apogee::acp::SavedChat{.id = "chat-1", .entries = std::move(entries)};
    };
    return setup;
}

}  // namespace

TEST_CASE("initialize advertises load, list, resume and close, now that each works",
          "[acp][server][sessions][saved]") {
    apogee::acp::AgentSetup setup;
    LockedSink out;
    LockedSink err;
    std::ostream out_stream{&out};
    std::ostream err_stream{&err};
    apogee::acp::Agent agent{setup, out_stream, err_stream};
    agent.handle(request(1, "initialize", {{"protocolVersion", 1}}));
    const std::vector<nlohmann::json> frames = frames_of(out.text());
    REQUIRE(frames.size() == 1);
    const nlohmann::json& capabilities = frames[0].at("result").at("agentCapabilities");
    CHECK(capabilities.at("loadSession") == true);
    CHECK(capabilities.at("sessionCapabilities") ==
          nlohmann::json{{"list", nlohmann::json::object()},
                         {"resume", nlohmann::json::object()},
                         {"close", nlohmann::json::object()}});
    CHECK_FALSE(capabilities.at("sessionCapabilities").contains("delete"));
    agent.finish();
}

TEST_CASE("a new session is its chat: its child told who began it and where, its id the chat's",
          "[acp][server][sessions][saved]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("this platform spawns no child processes");
    }
    const apogee::testing::TempDir folder{"acp-server-" + std::to_string(std::random_device{}())};
    const std::filesystem::path seen = folder.path() / "seen.txt";
    for (const bool named : {true, false}) {
        INFO(named);
        std::filesystem::remove(seen);
        LockedSink out;
        LockedSink err;
        std::ostream out_stream{&out};
        std::ostream err_stream{&err};
        apogee::acp::Agent agent{recording_setup(seen, "20261101-120000-abcd"), out_stream,
                                 err_stream};
        nlohmann::json init{{"protocolVersion", 1}};
        if (named) {
            init["clientInfo"] = {{"name", "acp-test"}, {"version", "1"}};
        }
        agent.handle(request(1, "initialize", init));
        agent.handle(open_request(2, folder.path()));
        REQUIRE(says(out, "\"sessionId\""));
        CHECK(session_answering(out.text(), 2) == "20261101-120000-abcd");
        // The folder as the host named it, beside who began it: `acp` when
        // the host gave no name.
        CHECK_THAT(read_file(seen),
                   ContainsSubstring(std::string{"--acp-origin\n"} + (named ? "acp-test" : "acp") +
                                     "\n--acp-cwd\n" + folder.path().string() + "\n---\n"));
        CHECK_THAT(read_file(seen), !ContainsSubstring("--resume"));
        // The session is named by the chat from here: a prompt reaches it.
        agent.handle(request(3, "session/set_mode",
                             {{"sessionId", "20261101-120000-abcd"}, {"modeId", "plan"}}));
        REQUIRE(says(out, "\"id\":3"));
        CHECK(frames_of(out.text())
                  .at(static_cast<std::size_t>(answered_at(frames_of(out.text()), 3)))
                  .contains("result"));
        agent.finish();
    }
}

TEST_CASE("session/load resumes the chat through execute's own resume and replays it first",
          "[acp][server][sessions][saved]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("this platform spawns no child processes");
    }
    const apogee::testing::TempDir folder{"acp-server-" + std::to_string(std::random_device{}())};
    const std::filesystem::path seen = folder.path() / "seen.txt";
    LockedSink out;
    LockedSink err;
    std::ostream out_stream{&out};
    std::ostream err_stream{&err};
    apogee::acp::Agent agent{recording_setup(seen, "chat-1"), out_stream, err_stream};
    agent.handle(request(1, "initialize", {{"protocolVersion", 1}}));
    agent.handle(request(2, "session/load",
                         {{"sessionId", "chat-1"},
                          {"cwd", folder.path().string()},
                          {"mcpServers", nlohmann::json::array()}}));
    REQUIRE(says(out, "\"id\":2"));
    const std::vector<nlohmann::json> frames = frames_of(out.text());
    const std::ptrdiff_t answer = answered_at(frames, 2);
    REQUIRE(answer > 0);
    // Exactly the two turns, in order, every update before the answer.
    std::vector<std::string> said;
    for (std::ptrdiff_t at = 0; at < answer; ++at) {
        const nlohmann::json& frame = frames[static_cast<std::size_t>(at)];
        if (frame.value("method", "") == "session/update") {
            CHECK(frame.at("params").at("sessionId") == "chat-1");
            const nlohmann::json& update = frame.at("params").at("update");
            said.push_back(update.at("messageId").get<std::string>() + "=" +
                           update.at("content").at("text").get<std::string>());
        }
    }
    CHECK(said == std::vector<std::string>{"entry-0-user=first", "entry-0-agent-1=one",
                                           "entry-1-user=second", "entry-1-agent-1=two"});
    for (std::size_t at = static_cast<std::size_t>(answer) + 1; at < frames.size(); ++at) {
        CHECK(frames[at].value("method", "") != "session/update");
    }
    const nlohmann::json& result = frames[static_cast<std::size_t>(answer)].at("result");
    CHECK(result.contains("modes"));
    CHECK(result.contains("configOptions"));
    CHECK_FALSE(result.contains("sessionId"));
    // Execute's own resume, never a new chat's origin.
    CHECK_THAT(read_file(seen), ContainsSubstring("--resume\nchat-1\n---\n"));
    CHECK_THAT(read_file(seen), !ContainsSubstring("--acp-origin"));
    agent.finish();
}

TEST_CASE("session/resume reopens the chat and replays nothing; an unknown id is refused",
          "[acp][server][sessions][saved]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("this platform spawns no child processes");
    }
    const apogee::testing::TempDir folder{"acp-server-" + std::to_string(std::random_device{}())};
    const std::filesystem::path seen = folder.path() / "seen.txt";
    LockedSink out;
    LockedSink err;
    std::ostream out_stream{&out};
    std::ostream err_stream{&err};
    apogee::acp::Agent agent{recording_setup(seen, "chat-1"), out_stream, err_stream};

    // An id no chat answers to: -32002 naming it, its id the error's data,
    // and nothing started.
    for (const std::string_view method : {"session/load", "session/resume"}) {
        INFO(method);
        agent.handle(request(1, method,
                             {{"sessionId", "no-such-chat"},
                              {"cwd", folder.path().string()},
                              {"mcpServers", nlohmann::json::array()}}));
    }
    std::vector<nlohmann::json> frames = frames_of(out.text());
    REQUIRE(frames.size() == 2);
    for (const nlohmann::json& frame : frames) {
        CHECK(frame.at("error").at("code") == -32002);
        CHECK_THAT(frame.at("error").at("message").get<std::string>(),
                   ContainsSubstring("no-such-chat"));
        CHECK(frame.at("error").at("data") == nlohmann::json{{"sessionId", "no-such-chat"}});
    }
    CHECK(agent.sessions_held() == 0);
    CHECK_FALSE(std::filesystem::exists(seen));

    agent.handle(
        request(2, "session/resume", {{"sessionId", "chat-1"}, {"cwd", folder.path().string()}}));
    REQUIRE(says(out, "\"id\":2"));
    frames = frames_of(out.text());
    const std::ptrdiff_t answer = answered_at(frames, 2);
    REQUIRE(answer == 2);
    CHECK(frames[2].contains("result"));
    CHECK_THAT(read_file(seen), ContainsSubstring("--resume\nchat-1\n---\n"));
    agent.finish();
    for (const nlohmann::json& frame : frames_of(out.text())) {
        CHECK(frame.value("method", "") != "session/update");
    }
}

TEST_CASE("session/list answers the host's chats from the store, by folder, in one page",
          "[acp][server][sessions][saved]") {
    const apogee::testing::TempDir folder{"acp-server-" + std::to_string(std::random_device{}())};
    LockedSink out;
    LockedSink err;
    std::ostream out_stream{&out};
    std::ostream err_stream{&err};
    apogee::acp::Agent agent{recording_setup(folder.path() / "seen.txt", "chat-1"), out_stream,
                             err_stream};
    agent.handle(request(1, "session/list", nlohmann::json::object()));
    agent.handle(request(2, "session/list", {{"cwd", "/work"}}));
    agent.handle(request(3, "session/list", {{"cursor", "anything"}}));
    agent.handle(request(4, "session/list", {{"cwd", "relative"}}));
    const std::vector<nlohmann::json> frames = frames_of(out.text());
    REQUIRE(frames.size() == 4);
    std::vector<std::string> ids;
    for (const nlohmann::json& entry : frames[0].at("result").at("sessions")) {
        ids.push_back(entry.at("sessionId").get<std::string>());
    }
    CHECK(ids == std::vector<std::string>{"chat-2", "chat-1"});
    CHECK_FALSE(frames[0].at("result").contains("nextCursor"));
    CHECK(frames[1].at("result").at("sessions") ==
          nlohmann::json::parse(R"([{"sessionId": "chat-1", "cwd": "/work",
                                     "title": "Fix the build",
                                     "updatedAt": "2026-11-01T12:01:00Z"}])"));
    CHECK(frames[2].at("result").at("sessions").empty());
    CHECK(frames[3].at("error").at("code") == -32602);
    agent.finish();
}

TEST_CASE("session/close ends the session, answers success twice, and the session says so",
          "[acp][server][sessions][saved]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("this platform spawns no child processes");
    }
    const apogee::testing::TempDir folder{"acp-server-" + std::to_string(std::random_device{}())};
    LockedSink out;
    LockedSink err;
    std::ostream out_stream{&out};
    std::ostream err_stream{&err};
    apogee::acp::Agent agent{recording_setup(folder.path() / "seen.txt", "chat-9"), out_stream,
                             err_stream};
    agent.handle(open_request(1, folder.path()));
    REQUIRE(says(out, "\"sessionId\""));
    agent.handle(request(2, "session/close", {{"sessionId", "chat-9"}}));
    agent.handle(request(3, "session/close", {{"sessionId", "chat-9"}}));
    agent.handle(request(4, "session/close", {{"sessionId", "never-opened"}}));
    agent.handle(request(5, "session/set_mode", {{"sessionId", "chat-9"}, {"modeId", "plan"}}));
    const std::vector<nlohmann::json> frames = frames_of(out.text());
    for (const int id : {2, 3, 4}) {
        INFO(id);
        const std::ptrdiff_t at = answered_at(frames, id);
        REQUIRE(at >= 0);
        CHECK(frames[static_cast<std::size_t>(at)].at("result") == nlohmann::json::object());
    }
    const std::ptrdiff_t refused = answered_at(frames, 5);
    REQUIRE(refused >= 0);
    CHECK_THAT(
        frames[static_cast<std::size_t>(refused)].at("error").at("message").get<std::string>(),
        ContainsSubstring("has ended: the host closed it"));
    agent.finish();
}

TEST_CASE("loading a chat this process still holds ends that session and waits for it first",
          "[acp][server][sessions][saved]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("this platform spawns no child processes");
    }
    const apogee::testing::TempDir folder{"acp-server-" + std::to_string(std::random_device{}())};
    const std::filesystem::path seen = folder.path() / "seen.txt";
    LockedSink out;
    LockedSink err;
    std::ostream out_stream{&out};
    std::ostream err_stream{&err};
    apogee::acp::Agent agent{recording_setup(seen, "chat-1"), out_stream, err_stream};
    agent.handle(open_request(1, folder.path()));
    REQUIRE(says(out, "\"sessionId\""));
    REQUIRE(session_answering(out.text(), 1) == "chat-1");
    // One chat, one writer: the new session's child ended and let go before
    // the loaded one starts.
    agent.handle(request(2, "session/load",
                         {{"sessionId", "chat-1"},
                          {"cwd", folder.path().string()},
                          {"mcpServers", nlohmann::json::array()}}));
    CHECK(agent.sessions_held() == 1);
    REQUIRE(says(out, "\"id\":2"));
    CHECK_THAT(read_file(seen), ContainsSubstring("--acp-cwd\n" + folder.path().string() +
                                                  "\n---\n--resume\nchat-1\n---\n"));
    // The id names the loaded session, never the one it ended.
    agent.handle(request(3, "session/set_mode", {{"sessionId", "chat-1"}, {"modeId", "plan"}}));
    REQUIRE(says(out, "\"id\":3"));
    const std::vector<nlohmann::json> frames = frames_of(out.text());
    CHECK(frames.at(static_cast<std::size_t>(answered_at(frames, 3))).contains("result"));
    agent.finish();
}

TEST_CASE("a chat opens by its id alone: by its name or a path it is refused, its holder untouched",
          "[acp][server][sessions][saved]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("this platform spawns no child processes");
    }
    const apogee::testing::TempDir folder{"acp-server-" + std::to_string(std::random_device{}())};
    const std::filesystem::path seen = folder.path() / "seen.txt";
    LockedSink out;
    LockedSink err;
    std::ostream out_stream{&out};
    std::ostream err_stream{&err};
    // What the store is asked: a path never reaches it.
    std::vector<std::string> asked;
    apogee::acp::AgentSetup setup = recording_setup(seen, "chat-1");
    setup.chats.read = [&asked, read = setup.chats.read](const std::string& id,
                                                         std::string& error) {
        asked.push_back(id);
        return read(id, error);
    };
    apogee::acp::Agent agent{std::move(setup), out_stream, err_stream};
    agent.handle(open_request(1, folder.path()));
    REQUIRE(says(out, "\"sessionId\""));
    REQUIRE(session_answering(out.text(), 1) == "chat-1");

    // The chat held live, reopened by its name -- which the store reads, as
    // `--resume` does -- and by a path to its file: each refused -32002
    // naming what was asked, so no second child ever writes it.
    const std::vector<std::string> spellings{"Fix the build", "../sessions/chat-1"};
    int request_id = 2;
    for (const std::string_view method : {"session/load", "session/resume"}) {
        for (const std::string& spelling : spellings) {
            agent.handle(request(request_id++, method,
                                 {{"sessionId", spelling},
                                  {"cwd", folder.path().string()},
                                  {"mcpServers", nlohmann::json::array()}}));
        }
    }
    const std::vector<nlohmann::json> frames = frames_of(out.text());
    for (int id = 2; id < request_id; ++id) {
        INFO(id);
        const std::ptrdiff_t at = answered_at(frames, id);
        REQUIRE(at >= 0);
        const nlohmann::json& error = frames[static_cast<std::size_t>(at)].at("error");
        const std::string& spelling = spellings[static_cast<std::size_t>(id - 2) % 2];
        CHECK(error.at("code") == -32002);
        CHECK_THAT(error.at("message").get<std::string>(),
                   ContainsSubstring("no chat has the id '" + spelling + "'"));
        CHECK(error.at("data") == nlohmann::json{{"sessionId", spelling}});
    }
    CHECK(asked == std::vector<std::string>{"Fix the build", "Fix the build"});

    // The session holding it goes on: still held, still live, its child the
    // only one started.
    CHECK(agent.sessions_held() == 1);
    agent.handle(
        request(request_id, "session/set_mode", {{"sessionId", "chat-1"}, {"modeId", "plan"}}));
    REQUIRE(says(out, "\"id\":" + std::to_string(request_id)));
    const std::vector<nlohmann::json> after = frames_of(out.text());
    CHECK(after.at(static_cast<std::size_t>(answered_at(after, request_id))).contains("result"));
    CHECK(count_of(read_file(seen), "---\n") == 1);
    agent.finish();
}

TEST_CASE("a prompt to a session still opening names the request it waits on",
          "[acp][server][sessions][saved]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("this platform spawns no child processes");
    }
    const apogee::testing::TempDir folder{"acp-server-" + std::to_string(std::random_device{}())};
    LockedSink out;
    LockedSink err;
    std::ostream out_stream{&out};
    std::ostream err_stream{&err};
    // A child that never says it is open.
    apogee::acp::AgentSetup setup = recording_setup(folder.path() / "seen.txt", "chat-1");
    setup.child.arguments = {"-c", "cat >/dev/null"};
    apogee::acp::Agent agent{std::move(setup), out_stream, err_stream};
    agent.handle(request(1, "session/load",
                         {{"sessionId", "chat-1"},
                          {"cwd", folder.path().string()},
                          {"mcpServers", nlohmann::json::array()}}));
    agent.handle(
        request(2, "session/prompt",
                {{"sessionId", "chat-1"},
                 {"prompt", nlohmann::json::array({{{"type", "text"}, {"text", "hi"}}})}}));
    const std::vector<nlohmann::json> frames = frames_of(out.text());
    const std::ptrdiff_t at = answered_at(frames, 2);
    REQUIRE(at >= 0);
    CHECK_THAT(frames[static_cast<std::size_t>(at)].at("error").at("message").get<std::string>(),
               ContainsSubstring("is still opening: wait for session/load's answer"));
    agent.finish();
}
