#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "agent/tool.h"
#include "contracts/assets.h"
#include "mcp/registry.h"
#include "platform/child_process.h"
#include "platform/platform.h"
#include "support/env_guard.h"

/// The code-execution server (47b), the REAL script -- this build's
/// compiled-in copy, seeded where seeding puts it -- driven through the
/// shipped MCP client: the handshake and the gate it declares, a run and its
/// files, each bound and guard against a planted violation (the time cap,
/// the output cap, the disk cap, a write outside the working directory, the
/// socket import, a key in the environment), the server's stderr held in the
/// sink, and the session directory gone when the session ends. Skipped by
/// name where there is no python3, or no child processes.
namespace {

using Catch::Matchers::ContainsSubstring;

[[nodiscard]] std::string python3() {
    return apogee::platform::find_on_path("python3");
}

/// The skip, said: both halves, as the training scripts' tests have them.
[[nodiscard]] bool runnable() {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("no child processes on this platform");
    }
    if (python3().empty()) {
        SKIP("no python3 on PATH");
    }
    return true;
}

[[nodiscard]] std::string server_text() {
    for (const apogee::harness::BundledScript& script : apogee::harness::bundled_mcp_scripts()) {
        if (script.name == "code_exec_server.py") {
            return std::string{script.text};
        }
    }
    FAIL("code_exec_server.py is not compiled in");
    return {};
}

/// A data directory with the server seeded in it, and one session on it.
class Session {
public:
    explicit Session(std::vector<std::string> extra = {}, std::vector<std::string> env = {})
        : dir_{"code-exec-" + std::to_string(std::random_device{}())} {
        script_ = dir_.path() / "home" / apogee::harness::bundled_code_exec_server_relative_path();
        std::filesystem::create_directories(script_.parent_path());
        std::ofstream{script_, std::ios::binary} << server_text();
        apogee::mcp::ServerSpec spec;
        spec.name = "code";
        spec.command = python3();
        spec.args = {script_.string()};
        spec.args.insert(spec.args.end(), extra.begin(), extra.end());
        spec.env = std::move(env);
        apogee::mcp::RegistryOptions options;
        options.connect_timeout = std::chrono::seconds{20};
        options.call_timeout = std::chrono::seconds{60};
        options.server_log = [this](std::string_view bytes) {
            const std::lock_guard lock{mutex_};
            log_.append(bytes);
        };
        registry_.connect_all({spec}, options);
    }

    ~Session() {
        registry_.close_all();
    }

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;

    [[nodiscard]] apogee::mcp::ToolCallResult call(std::string_view tool,
                                                   const nlohmann::json& arguments) {
        std::string error;
        apogee::mcp::ToolCallResult result =
            registry_.call("mcp__code__" + std::string{tool}, arguments.dump(), error);
        INFO(error);
        CHECK(error.empty());
        return result;
    }

    [[nodiscard]] apogee::mcp::ToolCallResult run(const std::string& code) {
        return call("run_python", {{"code", code}});
    }

    [[nodiscard]] apogee::mcp::Registry& registry() noexcept {
        return registry_;
    }

    [[nodiscard]] std::filesystem::path home() const {
        return dir_.path() / "home";
    }

    /// `<home>/cache/code-exec`, where session directories are made.
    [[nodiscard]] std::filesystem::path root() const {
        return home() / "cache" / "code-exec";
    }

    /// The one session directory under the root.
    [[nodiscard]] std::filesystem::path work() const {
        std::vector<std::filesystem::path> dirs;
        for (const auto& entry : std::filesystem::directory_iterator(root())) {
            if (entry.is_directory()) {
                dirs.push_back(entry.path());
            }
        }
        REQUIRE(dirs.size() == 1);
        return std::filesystem::canonical(dirs.front());
    }

    [[nodiscard]] std::string log() {
        const std::lock_guard lock{mutex_};
        return log_;
    }

    void close() {
        registry_.close_all();
    }

private:
    apogee::testing::TempDir dir_;
    std::filesystem::path script_;
    apogee::mcp::Registry registry_;
    std::mutex mutex_;
    std::string log_;
};

/// Waits up to `seconds` for `done`, polling: a server ends on its own time
/// once its stdin closes.
template <typename Done>
bool eventually(Done done, int seconds = 10) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds{seconds};
    while (std::chrono::steady_clock::now() < until) {
        if (done()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
    }
    return done();
}

}  // namespace

TEST_CASE("the code-execution server connects with two tools, both gated as writers",
          "[mcp][code-exec][python]") {
    if (!runnable()) {
        return;
    }
    Session session;
    REQUIRE(session.registry().connected_count() == 1);
    const std::vector<apogee::mcp::ServerStatus> statuses = session.registry().status();
    REQUIRE(statuses.size() == 1);
    CHECK(statuses.front().server_name == "apogee-code-exec");
    CHECK(statuses.front().tools == std::vector<std::string>{"run_python", "write_input"});

    // The permission gate is the boundary: neither tool says it is read-only,
    // so both are asked like any writer (39b's modes govern them).
    apogee::agent::ToolRegistry tools;
    session.registry().register_into(tools);
    REQUIRE(tools.find("mcp__code__run_python") != nullptr);
    REQUIRE(tools.find("mcp__code__write_input") != nullptr);
    CHECK(tools.find("mcp__code__run_python")->writes);
    CHECK(tools.find("mcp__code__write_input")->writes);

    // The session directory sits under the data directory it was seeded in.
    CHECK(session.work().parent_path() == std::filesystem::canonical(session.root()));
}

TEST_CASE("a run returns its output, its exit status and the files it wrote, as paths",
          "[mcp][code-exec][python]") {
    if (!runnable()) {
        return;
    }
    Session session;
    const apogee::mcp::ToolCallResult printed = session.run("print(6 * 7)");
    CHECK_FALSE(printed.is_error);
    CHECK(printed.text == "42\n[exit 0]");

    const apogee::mcp::ToolCallResult wrote =
        session.run("with open('plot.txt', 'w') as out:\n    out.write('x' * 10)\nprint('done')");
    CHECK_FALSE(wrote.is_error);
    const std::filesystem::path artifact = session.work() / "plot.txt";
    CHECK_THAT(wrote.text, ContainsSubstring("done\n[exit 0]\n[files written]\n" +
                                             artifact.string() + " (10 bytes)"));
    CHECK(std::filesystem::exists(artifact));

    // Files persist between calls, variables do not; an input written by
    // write_input is read by its relative path, a module by import.
    const apogee::mcp::ToolCallResult input =
        session.call("write_input", {{"path", "data/in.csv"}, {"content", "a,b\n1,2\n"}});
    CHECK_FALSE(input.is_error);
    CHECK_THAT(input.text, ContainsSubstring((session.work() / "data" / "in.csv").string()));
    CHECK(
        session.call("write_input", {{"path", "helper.py"}, {"content", "VALUE = 7\n"}}).is_error ==
        false);
    CHECK(session.run("import helper\nprint(open('data/in.csv').read().strip(), helper.VALUE)")
              .text == "a,b\n1,2 7\n[exit 0]");
    CHECK(
        session
            .call("write_input", {{"path", "raw.bin"}, {"content", "AAEC"}, {"encoding", "base64"}})
            .is_error == false);
    CHECK(session.run("print(list(open('raw.bin', 'rb').read()))").text == "[0, 1, 2]\n[exit 0]");

    // An error is the interpreter's own traceback, the code's lines only.
    const apogee::mcp::ToolCallResult raised = session.run("x = 1\nraise ValueError('boom')");
    CHECK_FALSE(raised.is_error);  // an exit status is data, not a tool failure
    CHECK_THAT(raised.text, ContainsSubstring("[stderr]\nTraceback (most recent call last):\n"
                                              "  File \"<cell>\", line 2, in <module>"));
    CHECK_THAT(raised.text, ContainsSubstring("ValueError: boom\n[exit 1]"));
    CHECK(raised.text.find("<string>") == std::string::npos);
}

TEST_CASE("each bound holds against a planted violation and names its cap",
          "[mcp][code-exec][python]") {
    if (!runnable()) {
        return;
    }
    Session session{{"--max-seconds", "2", "--max-session-bytes", "100000"}};

    SECTION("the wall clock: the run and what it started are stopped") {
        const auto started = std::chrono::steady_clock::now();
        const apogee::mcp::ToolCallResult result = session.run(
            "import subprocess, sys, time\n"
            "subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(60)'])\n"
            "time.sleep(60)");
        CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds{20});
        CHECK(result.is_error);
        CHECK_THAT(result.text, ContainsSubstring("[stopped after 2 s: run_python's wall-clock cap "
                                                  "(MAX_SECONDS) -- the code and every process "
                                                  "it started were stopped]"));
        // The session goes on.
        CHECK(session.run("print('next')").text == "next\n[exit 0]");
    }
    SECTION("the output: the first and last 8 KB, the rest counted") {
        const apogee::mcp::ToolCallResult result =
            session.run("print('A' * 20000 + 'B' * 20000 + 'C' * 20000)");
        CHECK_FALSE(result.is_error);
        CHECK(result.text.size() < 17000);
        CHECK(result.text.starts_with(std::string(8192, 'A')));
        CHECK_THAT(result.text, ContainsSubstring("bytes omitted: run_python keeps the first and "
                                                  "last 8 KB of output (OUTPUT_KEEP)"));
        CHECK_THAT(result.text, ContainsSubstring("CCCC\n[exit 0]"));
        CHECK(result.text.find("BBBB") == std::string::npos);
    }
    SECTION("the disk: what the run wrote past the cap is removed, and said") {
        const apogee::mcp::ToolCallResult result =
            session.run("open('big.bin', 'wb').write(b'x' * 200000)");
        CHECK(result.is_error);
        CHECK_THAT(result.text, ContainsSubstring("grew past its 100000 bytes cap "
                                                  "(MAX_SESSION_BYTES): the 1 file(s) this run "
                                                  "wrote or changed were removed"));
        CHECK_FALSE(std::filesystem::exists(session.work() / "big.bin"));
        const apogee::mcp::ToolCallResult input = session.call(
            "write_input", {{"path", "big.txt"}, {"content", std::string(150000, 'y')}});
        CHECK(input.is_error);
        CHECK_THAT(input.text, ContainsSubstring("past its 100000 bytes cap (MAX_SESSION_BYTES)"));
    }
}

TEST_CASE("the jail and the network guard refuse by name, and no key reaches the code",
          "[mcp][code-exec][python]") {
    if (!runnable()) {
        return;
    }
    Session session{{}, {"ANTHROPIC_API_KEY=sk-planted-code-exec"}};
    const std::filesystem::path outside = session.home() / "outside.txt";

    // write_input's path never leaves the working directory.
    for (const std::string& escape :
         {std::string{"../escape.txt"}, std::string{"data/../../escape.txt"}, outside.string()}) {
        const apogee::mcp::ToolCallResult refused =
            session.call("write_input", {{"path", escape}, {"content", "x"}});
        CHECK(refused.is_error);
        CHECK_THAT(refused.text, ContainsSubstring("write_input refused '" + escape +
                                                   "': it resolves outside the working "
                                                   "directory"));
    }
    CHECK_FALSE(std::filesystem::exists(session.root() / "escape.txt"));

    // Code's writes stay inside too, best effort: the audit hook names the rule.
    const apogee::mcp::ToolCallResult wrote =
        session.run("open(" + nlohmann::json(outside.string()).dump() + ", 'w').write('x')");
    CHECK_THAT(wrote.text, ContainsSubstring("PermissionError: Apogee's code-exec sandbox writes "
                                             "only inside its working directory"));
    CHECK_FALSE(std::filesystem::exists(outside));
    // /dev/null stays writable: subprocess's DEVNULL needs it.
    CHECK(session
              .run("import subprocess, sys\n"
                   "print(subprocess.run([sys.executable, '-c', 'print(1)'], "
                   "stdout=subprocess.DEVNULL).returncode)")
              .text == "0\n[exit 0]");

    // The socket module cannot be imported, nor what is built on it.
    for (const std::string& module : {std::string{"socket"}, std::string{"urllib.request"}}) {
        const apogee::mcp::ToolCallResult refused = session.run("import " + module);
        CHECK_THAT(refused.text, ContainsSubstring("ImportError: the network is off in Apogee's "
                                                   "code-exec sandbox: 'socket' cannot be "
                                                   "imported (a best-effort guard, not a "
                                                   "boundary)"));
    }

    // The environment is an allow-list: the key Apogee's process held never
    // reaches the code, and HOME is the working directory.
    const apogee::mcp::ToolCallResult env = session.run(
        "import os\nprint(os.environ.get('ANTHROPIC_API_KEY'))\n"
        "print(os.path.realpath(os.environ['HOME']))");
    CHECK(env.text == "None\n" + session.work().string() + "\n[exit 0]");
}

TEST_CASE("the server's own stderr stays in the sink, the code's in the result",
          "[mcp][code-exec][python]") {
    if (!runnable()) {
        return;
    }
    Session session;
    const apogee::mcp::ToolCallResult noisy = session.run(
        "import sys\nfor i in range(3):\n    print('NOISE_OUT', i)\n"
        "    print('NOISE_ERR', i, file=sys.stderr)");
    CHECK_THAT(noisy.text, ContainsSubstring("NOISE_OUT 2\n[stderr]\nNOISE_ERR 0"));
    CHECK_THAT(noisy.text, ContainsSubstring("NOISE_ERR 2\n[exit 0]"));
    // What the server said went to the captured sink -- never the terminal --
    // and none of the code's output did.
    CHECK(eventually([&session] { return !session.log().empty(); }));
    CHECK_THAT(session.log(), ContainsSubstring("code-exec: session directory"));
    CHECK(session.log().find("NOISE") == std::string::npos);
}

TEST_CASE("the session directory goes when the session ends, and a dead one is swept",
          "[mcp][code-exec][python]") {
    if (!runnable()) {
        return;
    }
    Session first;
    const std::filesystem::path live = first.work();
    CHECK(first.run("open('kept.txt', 'w').write('x')").text.find("[exit 0]") != std::string::npos);
    // A session a killed server left: its directory and a lock nobody holds.
    // Planted after this server's start, so only the next one can sweep it.
    const std::filesystem::path dead = first.root() / "session-deadbeef0000";
    std::filesystem::create_directories(dead / "sub");
    std::ofstream{dead / "sub" / "left.txt"} << "left";
    std::ofstream{first.root() / "session-deadbeef0000.lock"} << "";
    first.close();
    CHECK(eventually([&live] { return !std::filesystem::exists(live); }));
    CHECK(std::filesystem::exists(dead));

    // The next server on the same data directory sweeps it at start.
    apogee::mcp::ServerSpec spec;
    spec.name = "code";
    spec.command = python3();
    spec.args = {
        (first.home() / apogee::harness::bundled_code_exec_server_relative_path()).string()};
    apogee::mcp::Registry again;
    apogee::mcp::RegistryOptions options;
    options.connect_timeout = std::chrono::seconds{20};
    again.connect_all({spec}, options);
    REQUIRE(again.connected_count() == 1);
    CHECK_FALSE(std::filesystem::exists(dead));
    CHECK_FALSE(std::filesystem::exists(first.root() / "session-deadbeef0000.lock"));
    again.close_all();
}
