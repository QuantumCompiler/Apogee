#include "commands/permissions.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>

#include "agent/tool.h"
#include "commands/helpers.h"
#include "commands/json_reporter.h"
#include "commands/status_line.h"
#include "commands/terminal.h"
#include "harness/config.h"
#include "platform/platform.h"
#include "support/env_guard.h"

/// The gate's two halves as the surfaces make them: the checker's precedence
/// and the machine-mode prompt, driven over string streams.
namespace {

using apogee::agent::Permission;
using apogee::commands::make_driver_confirm_fn;
using apogee::commands::make_permission_checker;
using apogee::commands::SessionApprovals;
using apogee::harness::Config;
using apogee::harness::PermissionLevel;

std::string read_all(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

}  // namespace

TEST_CASE("the checker: config level, then the session's answers, then ask",
          "[commands][permissions]") {
    Config config;
    config.permissions.levels["write_file"] = PermissionLevel::Allow;
    config.permissions.levels["run_command"] = PermissionLevel::Deny;
    const auto approvals = std::make_shared<SessionApprovals>();
    const apogee::agent::PermissionChecker check = make_permission_checker(config, approvals);

    CHECK(check("write_file", "x") == Permission::Allow);
    CHECK(check("run_command", "x") == Permission::Deny);
    CHECK(check("delete_file", "x") == Permission::Ask);  // unlisted: the default
    approvals->insert("delete_file");
    CHECK(check("delete_file", "x") == Permission::Allow);
    // A session answer never overrides a config deny.
    approvals->insert("run_command");
    CHECK(check("run_command", "x") == Permission::Deny);
    // No approvals at all is fine: a served run has none.
    CHECK(make_permission_checker(config, nullptr)("delete_file", "x") == Permission::Ask);
}

TEST_CASE("the machine-mode prompt is a permission question answered by one line",
          "[commands][permissions][machine]") {
    const apogee::testing::TempDir temp{"permissions-driver-" +
                                        std::to_string(std::random_device{}())};
    const std::filesystem::path config_path = temp.path() / "config.yaml";
    std::ofstream{config_path} << apogee::harness::config_template();
    const std::string shipped = read_all(config_path);

    std::ostringstream out;
    apogee::commands::JsonReporter reporter{out};
    const auto approvals = std::make_shared<SessionApprovals>();

    SECTION("yes allows once, and the event carries kind, tool and target") {
        std::istringstream in{R"({"type":"answer","text":"yes"})"
                              "\n"};
        const apogee::agent::ConfirmFn confirm =
            make_driver_confirm_fn(reporter, in, config_path, approvals);
        CHECK(confirm("write_file", "/tmp/x"));
        const nlohmann::json event = nlohmann::json::parse(out.str());
        CHECK(event["type"] == "question");
        CHECK(event["kind"] == "permission");
        CHECK(event["tool"] == "write_file");
        CHECK(event["target"] == "/tmp/x");
        REQUIRE(event["questions"].size() == 1);
        CHECK(event["questions"][0]["options"].size() == 4);
        CHECK(approvals->empty());
        CHECK(read_all(config_path) == shipped);
    }
    SECTION("no denies; an unknown word denies; a non-answer line is skipped") {
        std::istringstream in{R"({"type":"user","text":"ignored"})"
                              "\n"
                              R"({"type":"answer","text":"nope"})"
                              "\n"};
        CHECK_FALSE(make_driver_confirm_fn(reporter, in, config_path, approvals)("write_file", ""));
    }
    SECTION("session is remembered for the run and not written") {
        std::istringstream in{R"({"type":"answer","text":"session"})"
                              "\n"};
        CHECK(make_driver_confirm_fn(reporter, in, config_path, approvals)("run_command", "ls"));
        CHECK(approvals->contains("run_command"));
        CHECK(read_all(config_path) == shipped);
    }
    SECTION("always is written through the config editor: the shipped file plus one changed line") {
        std::istringstream in{R"({"type":"answer","text":"always"})"
                              "\n"};
        CHECK(make_driver_confirm_fn(reporter, in, config_path, approvals)("write_file", "x"));
        CHECK(approvals->contains("write_file"));
        const std::string after = read_all(config_path);
        CHECK(after != shipped);
        std::string expected = shipped;
        const std::size_t at = expected.find("  write_file: ask");
        REQUIRE(at != std::string::npos);
        expected.replace(at, std::string{"  write_file: ask"}.size(), "  write_file: allow");
        CHECK(after == expected);
    }
    SECTION("a driver that hangs up with the prompt outstanding fails the turn") {
        std::istringstream in{""};
        CHECK_THROWS_AS(
            make_driver_confirm_fn(reporter, in, config_path, approvals)("write_file", ""),
            std::runtime_error);
    }
}

TEST_CASE("the terminal prompt is null where there is no terminal", "[commands][permissions]") {
    // Under ctest stdin is a pipe: the prompt half must be absent, so that the
    // gate resolves ask to deny rather than blocking on a read nobody answers.
    if (apogee::platform::is_terminal(apogee::platform::StandardStream::In) &&
        apogee::platform::is_terminal(apogee::platform::StandardStream::Err)) {
        SKIP("running on a terminal");
    }
    apogee::commands::TerminalWriter writer{std::cerr};
    apogee::commands::StatusLine status{writer, apogee::commands::StatusLine::Options{}};
    CHECK(apogee::commands::terminal_confirm_fn(status, apogee::ansi::Style{}, {}, nullptr) ==
          nullptr);
}

TEST_CASE("the built-in registry honours tools.disabled and tools.fs_root",
          "[commands][permissions][tools]") {
    Config config;
    config.tools.disabled = {"shell", "rag"};
    config.tools.fs_root = "/srv/sandbox";
    const apogee::agent::ToolRegistry registry = apogee::commands::make_built_in_tools(
        apogee::commands::BuiltInToolOptions{.config = &config});
    CHECK(registry.find("fetch_url") != nullptr);
    CHECK(registry.find("read_file") != nullptr);
    CHECK(registry.find("git_diff") != nullptr);
    CHECK(registry.find("write_note") != nullptr);
    CHECK(registry.find("run_command") == nullptr);
    CHECK(registry.find("search_documents") == nullptr);
    CHECK(registry.find("read_file")->description.find("/srv/sandbox") != std::string::npos);
    // Every toolset on, with nothing configured.
    CHECK(apogee::commands::make_built_in_tools({}).find("run_command") != nullptr);
}
