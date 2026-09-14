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
#include "agentloop/loop.h"
#include "commands/helpers.h"
#include "commands/json_reporter.h"
#include "commands/status_line.h"
#include "commands/terminal.h"
#include "harness/config.h"
#include "platform/platform.h"
#include "support/env_guard.h"
#include "support/fake_mcp_server.h"

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

TEST_CASE("the built-in registry connects the configured MCP servers and registers their tools",
          "[commands][mcp]") {
    Config config;
    apogee::harness::McpServerConfig well;
    well.command = "well";
    config.mcp_servers.emplace("srv", well);
    apogee::harness::McpServerConfig off;
    off.command = "well";
    off.enabled = false;
    config.mcp_servers.emplace("off", off);
    std::vector<std::string> lines;
    const auto mcp = std::make_shared<apogee::mcp::Registry>();
    const apogee::agent::ToolRegistry registry =
        apogee::commands::make_built_in_tools(apogee::commands::BuiltInToolOptions{
            .config = &config,
            .mcp = mcp,
            .mcp_status = [&lines](std::string_view line) { lines.emplace_back(line); },
            .mcp_spawn = apogee::testing::fake_fleet()});
    REQUIRE(registry.find("mcp__srv__echo") != nullptr);
    CHECK_FALSE(registry.find("mcp__srv__echo")->writes);
    CHECK(registry.find("mcp__srv__write")->writes);
    CHECK(registry.find("mcp__off__echo") == nullptr);  // disabled: never dialled
    CHECK(registry.find("read_file") != nullptr);       // built-ins regardless
    CHECK(mcp->connected_count() == 1);
    bool connecting_first = false;
    for (const std::string& line : lines) {
        if (line == "[mcp] connecting: srv...") {
            connecting_first = true;
            break;
        }
        if (line.find("connected") != std::string::npos) {
            break;
        }
    }
    CHECK(connecting_first);
}

TEST_CASE("a read-only policy keeps only tools that never write, MCP included; none keeps nothing",
          "[commands][permissions][policy]") {
    Config config;
    apogee::harness::McpServerConfig well;
    well.command = "well";
    config.mcp_servers.emplace("srv", well);
    const auto mcp = std::make_shared<apogee::mcp::Registry>();
    const apogee::agent::ToolRegistry read_only = apogee::commands::make_built_in_tools(
        apogee::commands::BuiltInToolOptions{.config = &config,
                                             .mcp = mcp,
                                             .mcp_spawn = apogee::testing::fake_fleet(),
                                             .policy = apogee::harness::AgentToolPolicy::ReadOnly,
                                             .mcp_servers = std::vector<std::string>{"srv"}});
    // Nothing that writes is registered -- so nothing can ever prompt.
    for (const std::string& name : read_only.names()) {
        INFO(name);
        CHECK_FALSE(read_only.find(name)->writes);
    }
    CHECK(read_only.find("read_file") != nullptr);
    CHECK(read_only.find("git_diff") != nullptr);
    CHECK(read_only.find("write_file") == nullptr);
    CHECK(read_only.find("run_command") == nullptr);
    CHECK(read_only.find("delete_note") == nullptr);
    CHECK(read_only.find("mcp__srv__echo") != nullptr);   // the server said read-only
    CHECK(read_only.find("mcp__srv__write") == nullptr);  // it did not
    // What the loop advertises IS the filtered set.
    apogee::agentloop::Options options;
    options.tools = &read_only;
    const std::vector<apogee::harness::Tool> advertised =
        apogee::agentloop::advertised_tools(options);
    CHECK(advertised.size() == read_only.size());
    for (const apogee::harness::Tool& tool : advertised) {
        CHECK(read_only.find(tool.name) != nullptr);
    }

    // None: an empty registry, and no server dialled at all.
    const auto untouched = std::make_shared<apogee::mcp::Registry>();
    const apogee::agent::ToolRegistry none = apogee::commands::make_built_in_tools(
        apogee::commands::BuiltInToolOptions{.config = &config,
                                             .mcp = untouched,
                                             .mcp_spawn = apogee::testing::fake_fleet(),
                                             .policy = apogee::harness::AgentToolPolicy::None,
                                             .mcp_servers = std::vector<std::string>{"srv"}});
    CHECK(none.empty());
    CHECK(untouched->connected_count() == 0);

    // The filter itself, over a hand-built registry.
    apogee::agent::ToolRegistry mixed;
    apogee::agent::Tool reads;
    reads.name = "r";
    reads.run = [](std::string_view) { return apogee::agent::ToolOutcome{"r", false}; };
    apogee::agent::Tool writes;
    writes.name = "w";
    writes.writes = true;
    writes.run = [](std::string_view) { return apogee::agent::ToolOutcome{"w", false}; };
    mixed.add(reads);
    mixed.add(writes);
    CHECK(
        apogee::commands::apply_tool_policy(mixed, apogee::harness::AgentToolPolicy::All).size() ==
        2);
    CHECK(apogee::commands::apply_tool_policy(mixed, apogee::harness::AgentToolPolicy::ReadOnly)
              .names() == std::vector<std::string>{"r"});
    CHECK(
        apogee::commands::apply_tool_policy(mixed, apogee::harness::AgentToolPolicy::None).empty());
}

TEST_CASE("an agent names the servers it connects; an unknown name is reported and skipped",
          "[commands][permissions][policy][mcp]") {
    Config config;
    apogee::harness::McpServerConfig well;
    well.command = "well";
    config.mcp_servers.emplace("srv", well);
    config.mcp_servers.emplace("other", well);
    std::vector<std::string> lines;
    const auto mcp = std::make_shared<apogee::mcp::Registry>();
    const apogee::agent::ToolRegistry registry =
        apogee::commands::make_built_in_tools(apogee::commands::BuiltInToolOptions{
            .config = &config,
            .mcp = mcp,
            .mcp_status = [&lines](std::string_view line) { lines.emplace_back(line); },
            .mcp_spawn = apogee::testing::fake_fleet(),
            .mcp_servers = std::vector<std::string>{"SRV", "ghost"}});
    CHECK(registry.find("mcp__srv__echo") != nullptr);    // named, case-insensitively
    CHECK(registry.find("mcp__other__echo") == nullptr);  // configured but not named
    CHECK(mcp->connected_count() == 1);
    bool warned = false;
    for (const std::string& line : lines) {
        warned = warned || line.find("no server named 'ghost'") != std::string::npos;
    }
    CHECK(warned);
    // An empty list connects nothing; null (the interactive surfaces) connects all.
    const auto none = std::make_shared<apogee::mcp::Registry>();
    (void)apogee::commands::make_built_in_tools(
        apogee::commands::BuiltInToolOptions{.config = &config,
                                             .mcp = none,
                                             .mcp_spawn = apogee::testing::fake_fleet(),
                                             .mcp_servers = std::vector<std::string>{}});
    CHECK(none->connected_count() == 0);
    const auto all = std::make_shared<apogee::mcp::Registry>();
    (void)apogee::commands::make_built_in_tools(apogee::commands::BuiltInToolOptions{
        .config = &config, .mcp = all, .mcp_spawn = apogee::testing::fake_fleet()});
    CHECK(all->connected_count() == 2);
}
