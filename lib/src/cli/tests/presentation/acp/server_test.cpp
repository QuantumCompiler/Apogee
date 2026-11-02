#include "acp/server.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <ostream>
#include <random>
#include <sstream>
#include <streambuf>
#include <string>
#include <string_view>
#include <thread>

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
                             "printf '%s\\n' \"$#\" > \"$0\"; printf '%s\\n' "
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
    std::ifstream in{seen};
    std::string count;
    std::getline(in, count);
    CHECK(count == "0");
    CHECK_THAT(err.text(), !ContainsSubstring("MCP"));
    agent.finish();
}
