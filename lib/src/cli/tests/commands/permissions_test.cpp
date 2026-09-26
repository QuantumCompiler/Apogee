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
#include "support/fake_transport.h"

/// The gate's two halves as the surfaces make them: the checker's precedence
/// and the machine-mode prompt, driven over string streams.
namespace {

using apogee::agent::GateRequest;
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

    CHECK(check(GateRequest{"write_file", "x"}) == Permission::Allow);
    CHECK(check(GateRequest{"run_command", "x"}) == Permission::Deny);
    CHECK(check(GateRequest{"delete_file", "x"}) == Permission::Ask);  // unlisted: the default
    approvals->tools.insert("delete_file");
    CHECK(check(GateRequest{"delete_file", "x"}) == Permission::Allow);
    // A session answer never overrides a config deny.
    approvals->tools.insert("run_command");
    CHECK(check(GateRequest{"run_command", "x"}) == Permission::Deny);
    // No approvals at all is fine: a served run has none.
    CHECK(make_permission_checker(config, nullptr)(GateRequest{"delete_file", "x"}) ==
          Permission::Ask);
}

TEST_CASE("an outbound call is decided by host: the allow-list, then the session, then ask",
          "[commands][permissions][outbound]") {
    Config config;
    config.tools.allowed_hosts = {"docs.python.org", "Example.COM."};
    // A level for the tool is not how websites are allowed: it has no effect.
    config.permissions.levels["fetch_url"] = PermissionLevel::Allow;
    const auto approvals = std::make_shared<SessionApprovals>();
    const apogee::agent::PermissionChecker check = make_permission_checker(config, approvals);
    const auto fetch = [](std::string_view host) {
        return GateRequest{"fetch_url", host, "https://example/", true};
    };

    CHECK(check(fetch("docs.python.org")) == Permission::Allow);
    CHECK(check(fetch("example.com")) == Permission::Allow);  // either spelling
    CHECK(check(fetch("pypi.org")) == Permission::Ask);
    // Exact hosts: no parent, child, prefix or suffix admits another.
    CHECK(check(fetch("python.org")) == Permission::Ask);
    CHECK(check(fetch("evil.docs.python.org")) == Permission::Ask);
    CHECK(check(fetch("docs.python.org.evil.example")) == Permission::Ask);
    // Not a host at all: refused outright, never asked about.
    CHECK(check(fetch("")) == Permission::Deny);
    CHECK(check(fetch("https://docs.python.org")) == Permission::Deny);

    // `session` for one host is that host, not the tool.
    approvals->hosts.insert("pypi.org");
    CHECK(check(fetch("pypi.org")) == Permission::Allow);
    CHECK(check(fetch("files.pypi.org")) == Permission::Ask);
    // And a tool-level session answer allows no website.
    approvals->tools.insert("fetch_url");
    CHECK(check(fetch("other.example")) == Permission::Ask);
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
        CHECK(confirm(GateRequest{"write_file", "/tmp/x"}));
        const nlohmann::json event = nlohmann::json::parse(out.str());
        CHECK(event["type"] == "question");
        CHECK(event["kind"] == "permission");
        CHECK(event["tool"] == "write_file");
        CHECK(event["target"] == "/tmp/x");
        REQUIRE(event["questions"].size() == 1);
        CHECK(event["questions"][0]["options"].size() == 4);
        CHECK(approvals->tools.empty());
        CHECK(approvals->hosts.empty());
        CHECK(read_all(config_path) == shipped);
    }
    SECTION("no denies; an unknown word denies; a non-answer line is skipped") {
        std::istringstream in{R"({"type":"user","text":"ignored"})"
                              "\n"
                              R"({"type":"answer","text":"nope"})"
                              "\n"};
        CHECK_FALSE(make_driver_confirm_fn(reporter, in, config_path,
                                           approvals)(GateRequest{"write_file", ""}));
    }
    SECTION("session is remembered for the run and not written") {
        std::istringstream in{R"({"type":"answer","text":"session"})"
                              "\n"};
        CHECK(make_driver_confirm_fn(reporter, in, config_path,
                                     approvals)(GateRequest{"run_command", "ls"}));
        CHECK(approvals->tools.contains("run_command"));
        CHECK(read_all(config_path) == shipped);
    }
    SECTION("always is written through the config editor: the shipped file plus one changed line") {
        std::istringstream in{R"({"type":"answer","text":"always"})"
                              "\n"};
        CHECK(make_driver_confirm_fn(reporter, in, config_path,
                                     approvals)(GateRequest{"write_file", "x"}));
        CHECK(approvals->tools.contains("write_file"));
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
        CHECK_THROWS_AS(make_driver_confirm_fn(reporter, in, config_path,
                                               approvals)(GateRequest{"write_file", ""}),
                        std::runtime_error);
    }

    const GateRequest website{"fetch_url", "docs.python.org", "https://docs.python.org/3/", true};
    SECTION("an outbound session answer remembers the host, not the tool") {
        std::istringstream in{R"({"type":"answer","text":"session"})"
                              "\n"};
        CHECK(make_driver_confirm_fn(reporter, in, config_path, approvals)(website));
        CHECK(approvals->hosts.contains("docs.python.org"));
        CHECK_FALSE(approvals->tools.contains("fetch_url"));
        CHECK(read_all(config_path) == shipped);
    }
    SECTION("an outbound always adds the host through the editor, byte-exact") {
        std::istringstream in{R"({"type":"answer","text":"always"})"
                              "\n"};
        CHECK(make_driver_confirm_fn(reporter, in, config_path, approvals)(website));
        CHECK(approvals->hosts.contains("docs.python.org"));
        std::string expected = shipped;
        const std::size_t at = expected.find("  allowed_hosts: []");
        REQUIRE(at != std::string::npos);
        expected.replace(at, std::string{"  allowed_hosts: []"}.size(),
                         "  allowed_hosts: [docs.python.org]");
        CHECK(read_all(config_path) == expected);
        // The permissions section is untouched: no `fetch_url: allow` line.
        CHECK(read_all(config_path).find("fetch_url: allow") == std::string::npos);
        const nlohmann::json event = nlohmann::json::parse(out.str());
        CHECK(event["target"] == "docs.python.org");
        CHECK(event["outbound"] == true);
    }
}

TEST_CASE("a pipe and a driverless session reach only the listed websites",
          "[commands][permissions][outbound]") {
    // Where nobody can answer, the surfaces pass no confirm function: the
    // checker alone decides, and ask is deny. `complete` on a pipe, chat's
    // machine mode without a driver and `serve` all wire it this way.
    Config config;
    config.tools.allowed_hosts = {"docs.python.org"};
    std::vector<std::string> fetched;
    apogee::agent::ToolRegistry registry;
    registry.add(apogee::agent::make_fetch_url_tool([&fetched](std::string_view url) {
        fetched.emplace_back(url);
        return apogee::agent::FetchResult{200, "<p>text</p>", "", ""};
    }));
    apogee::agent::DispatchContext context;
    context.permission = make_permission_checker(config, std::make_shared<SessionApprovals>());

    const auto call = [](std::string_view url) {
        return apogee::harness::ToolCall{"c", "fetch_url",
                                         R"({"url":")" + std::string{url} + R"("})"};
    };
    const apogee::agent::ToolOutcome refused =
        apogee::agent::dispatch(registry, call("https://pypi.org/simple/"), context);
    CHECK(refused.is_error);
    CHECK(fetched.empty());
    const apogee::agent::ToolOutcome listed =
        apogee::agent::dispatch(registry, call("https://docs.python.org/3/"), context);
    CHECK_FALSE(listed.is_error);
    CHECK(fetched == std::vector<std::string>{"https://docs.python.org/3/"});
}

TEST_CASE("the real fetcher asks for one hop and a bounded body",
          "[commands][permissions][fetch]") {
    apogee::testing::FakeTransport::Reply moved;
    moved.status = 302;
    moved.location = "https://elsewhere.example/";
    apogee::testing::FakeTransport::Reply huge;
    huge.body = std::string(apogee::commands::kFetchMaxBodyBytes + 1, 'x');
    auto transport = std::make_unique<apogee::testing::FakeTransport>(
        std::vector<apogee::testing::FakeTransport::Reply>{moved, huge});
    const apogee::testing::FakeTransport& seen = *transport;
    const apogee::agent::UrlFetcher fetcher = apogee::commands::make_http_fetcher(
        std::make_shared<apogee::backends::HttpClient>(std::move(transport)));

    const apogee::agent::FetchResult redirect = fetcher("https://a.example/");
    CHECK(redirect.status == 302);
    CHECK(redirect.location == "https://elsewhere.example/");
    REQUIRE(seen.requests().size() == 1);
    CHECK_FALSE(seen.requests()[0].follow_redirects);
    CHECK(seen.requests()[0].max_body_bytes == apogee::commands::kFetchMaxBodyBytes);
    CHECK(seen.requests()[0].method == "GET");

    const apogee::agent::FetchResult too_big = fetcher("https://a.example/big");
    CHECK(too_big.error.find("larger than 5 MB") != std::string::npos);
    CHECK(too_big.body.empty());
    CHECK(seen.requests().size() == 2);  // stopping is not a failure: never retried
}

TEST_CASE("the file tools default to the folder Apogee was started in",
          "[commands][permissions][tools]") {
    const apogee::agent::ToolRegistry registry = apogee::commands::make_built_in_tools({});
    const std::string launch = std::filesystem::current_path().string();
    REQUIRE(registry.find("read_file") != nullptr);
    CHECK(registry.find("read_file")->description.find(launch) != std::string::npos);
    const std::optional<std::string> home = apogee::platform::home_directory();
    if (home.has_value() && *home != launch) {
        CHECK(registry.find("read_file")->description.find(*home + " ") == std::string::npos);
    }
    // Set, it wins; unset, `check` and the tools agree on where it came from.
    CHECK(apogee::tools::effective_fs_root("/srv/sandbox").path == "/srv/sandbox");
    CHECK(apogee::tools::effective_fs_root("/srv/sandbox").from_config);
    CHECK(apogee::tools::effective_fs_root("").path == std::filesystem::current_path());
    CHECK_FALSE(apogee::tools::effective_fs_root("").from_config);
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
                                             .policy = apogee::harness::AgentToolPolicy::ReadOnly,
                                             .mcp_servers = std::vector<std::string>{"srv"},
                                             .mcp = mcp,
                                             .mcp_spawn = apogee::testing::fake_fleet()});
    // Nothing that writes is registered. The outbound fetch stays, gated per
    // website -- with nobody to ask it reaches only tools.allowed_hosts, so a
    // non-interactive agent still never blocks.
    for (const std::string& name : read_only.names()) {
        INFO(name);
        CHECK_FALSE(read_only.find(name)->writes);
    }
    REQUIRE(read_only.find("fetch_url") != nullptr);
    CHECK(read_only.find("fetch_url")->outbound);
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
                                             .policy = apogee::harness::AgentToolPolicy::None,
                                             .mcp_servers = std::vector<std::string>{"srv"},
                                             .mcp = untouched,
                                             .mcp_spawn = apogee::testing::fake_fleet()});
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
            .mcp_servers = std::vector<std::string>{"SRV", "ghost"},
            .mcp = mcp,
            .mcp_status = [&lines](std::string_view line) { lines.emplace_back(line); },
            .mcp_spawn = apogee::testing::fake_fleet()});
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
                                             .mcp_servers = std::vector<std::string>{},
                                             .mcp = none,
                                             .mcp_spawn = apogee::testing::fake_fleet()});
    CHECK(none->connected_count() == 0);
    const auto all = std::make_shared<apogee::mcp::Registry>();
    (void)apogee::commands::make_built_in_tools(apogee::commands::BuiltInToolOptions{
        .config = &config, .mcp = all, .mcp_spawn = apogee::testing::fake_fleet()});
    CHECK(all->connected_count() == 2);
}
