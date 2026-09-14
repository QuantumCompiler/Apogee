#include "tools/shell.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <random>
#include <string>

#include "agent/tool.h"
#include "platform/child_process.h"
#include "support/env_guard.h"

/// The shell toolset: the trailers, the timeout as a result, and the gate.
namespace {

using apogee::agent::ToolOutcome;
using apogee::agent::ToolRegistry;

}  // namespace

TEST_CASE("run_command renders stdout, stderr and the exit status, and is gated",
          "[tools][shell][permission]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("no child processes on this platform");
    }
    const apogee::testing::TempDir temp{"tools-shell-" + std::to_string(std::random_device{}())};
    ToolRegistry registry;
    apogee::tools::register_shell_tool(registry, temp.path(), std::chrono::seconds{10});
    const apogee::agent::Tool* tool = registry.find("run_command");
    REQUIRE(tool != nullptr);
    CHECK(tool->writes);  // the divergence from Ommi: the shell goes through the gate
    CHECK(tool->describe_target(R"({"command":"echo hi"})") == "echo hi");

    const ToolOutcome ok = tool->run(R"({"command":"echo out; echo err 1>&2; exit 3"})");
    CHECK_FALSE(ok.is_error);
    CHECK(ok.content == "out\n[stderr]\nerr\n[exit 3]");

    // The working directory: the default, and an override.
    CHECK(tool->run(R"({"command":"pwd"})").content.find(temp.path().filename().string()) !=
          std::string::npos);
    std::filesystem::create_directories(temp.path() / "elsewhere");
    CHECK(tool->run(R"({"command":"pwd","cwd":")" + (temp.path() / "elsewhere").string() + R"("})")
              .content.find("elsewhere") != std::string::npos);
    CHECK(tool->run(R"({"command":"pwd","cwd":"/nonexistent/dir"})").is_error);
    CHECK(tool->run(R"({"command":""})").is_error);
    CHECK(tool->run("{}").is_error);

    // A quote in the command cannot escape its argument: it is data.
    CHECK(tool->run(R"({"command":"echo \"$1\" done"})").content.starts_with(" done"));
}

TEST_CASE("a command that outlives the timeout is a result, not a hang", "[tools][shell]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("no child processes on this platform");
    }
    const apogee::testing::TempDir temp{"tools-shell-t-" + std::to_string(std::random_device{}())};
    const auto started = std::chrono::steady_clock::now();
    const apogee::tools::ShellResult result =
        apogee::tools::run_shell("sleep 30", temp.path(), std::chrono::milliseconds{300});
    CHECK(result.timed_out);
    CHECK(result.rendered == "[timed out after 0s]");
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds{10});
}
