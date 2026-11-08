#include "cli/permissions.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "agent/tool.h"
#include "agent/web_search.h"
#include "agentloop/loop.h"
#include "ansi/inert.h"
#include "cli/helpers.h"
#include "contracts/config.h"
#include "contracts/host.h"
#include "machine/driver_input.h"
#include "machine/json_reporter.h"
#include "platform/platform.h"
#include "support/env_guard.h"
#include "support/fake_mcp_server.h"
#include "support/fake_transport.h"
#include "tools/environment.h"
#include "tools/fs.h"
#include "tools/shell.h"
#include "tools/toolsets.h"
#include "views/status_line.h"
#include "views/terminal.h"

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
        apogee::commands::DriverInput driver{in};
        const apogee::agent::ConfirmFn confirm =
            make_driver_confirm_fn(reporter, driver, config_path, approvals);
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
        apogee::commands::DriverInput driver{in};
        CHECK_FALSE(make_driver_confirm_fn(reporter, driver, config_path,
                                           approvals)(GateRequest{"write_file", ""}));
    }
    SECTION("session is remembered for the run and not written") {
        std::istringstream in{R"({"type":"answer","text":"session"})"
                              "\n"};
        apogee::commands::DriverInput driver{in};
        CHECK(make_driver_confirm_fn(reporter, driver, config_path,
                                     approvals)(GateRequest{"run_command", "ls"}));
        CHECK(approvals->tools.contains("run_command"));
        CHECK(read_all(config_path) == shipped);
    }
    SECTION("always is written through the config editor: the shipped file plus one changed line") {
        std::istringstream in{R"({"type":"answer","text":"always"})"
                              "\n"};
        apogee::commands::DriverInput driver{in};
        CHECK(make_driver_confirm_fn(reporter, driver, config_path,
                                     approvals)(GateRequest{"write_file", "x"}));
        CHECK(approvals->tools.contains("write_file"));
        const std::string after = read_all(config_path);
        CHECK(after != shipped);
        std::string expected = shipped;
        const std::size_t at = expected.find("\"write_file\": \"ask\"");
        REQUIRE(at != std::string::npos);
        expected.replace(at, std::string{"\"write_file\": \"ask\""}.size(),
                         "\"write_file\": \"allow\"");
        CHECK(after == expected);
    }
    SECTION("a driver that hangs up with the prompt outstanding fails the turn") {
        std::istringstream in{""};
        apogee::commands::DriverInput driver{in};
        CHECK_THROWS_AS(make_driver_confirm_fn(reporter, driver, config_path,
                                               approvals)(GateRequest{"write_file", ""}),
                        std::runtime_error);
    }

    const GateRequest website{"fetch_url", "docs.python.org", "https://docs.python.org/3/", true};
    SECTION("an outbound session answer remembers the host, not the tool") {
        std::istringstream in{R"({"type":"answer","text":"session"})"
                              "\n"};
        apogee::commands::DriverInput driver{in};
        CHECK(make_driver_confirm_fn(reporter, driver, config_path, approvals)(website));
        CHECK(approvals->hosts.contains("docs.python.org"));
        CHECK_FALSE(approvals->tools.contains("fetch_url"));
        CHECK(read_all(config_path) == shipped);
    }
    SECTION("an outbound always adds the host through the editor, byte-exact") {
        std::istringstream in{R"({"type":"answer","text":"always"})"
                              "\n"};
        apogee::commands::DriverInput driver{in};
        CHECK(make_driver_confirm_fn(reporter, driver, config_path, approvals)(website));
        CHECK(approvals->hosts.contains("docs.python.org"));
        std::string expected = shipped;
        const std::size_t at = expected.find("\"allowed_hosts\": []");
        REQUIRE(at != std::string::npos);
        expected.replace(at, std::string{"\"allowed_hosts\": []"}.size(),
                         "\"allowed_hosts\": [\"docs.python.org\"]");
        CHECK(read_all(config_path) == expected);
        // The permissions section is untouched: no `fetch_url: allow` line.
        CHECK(read_all(config_path).find("\"fetch_url\": \"allow\"") == std::string::npos);
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

TEST_CASE("the search instance's host is trusted by configuration; what it finds is not",
          "[commands][permissions][outbound][search]") {
    Config config;
    config.tools.search.url = "http://searx.home.example:8888";
    const apogee::agent::PermissionChecker check = make_permission_checker(config, nullptr);
    const auto outbound = [](std::string_view tool, std::string_view host) {
        return GateRequest{tool, host, "", true};
    };
    CHECK(check(outbound("web_search", "searx.home.example")) == Permission::Allow);
    CHECK(check(outbound("web_search", "other.example")) == Permission::Ask);
    CHECK(check(outbound("fetch_url", "github.com")) == Permission::Ask);
    // A section that cannot be used trusts nothing.
    config.tools.search.provider = "brave";
    CHECK(make_permission_checker(config, nullptr)(outbound("web_search", "searx.home.example")) ==
          Permission::Ask);
}

TEST_CASE("web_search is registered only where tools.search names a usable instance",
          "[commands][permissions][tools][search]") {
    CHECK(apogee::commands::make_built_in_tools({}).find("web_search") == nullptr);
    Config config;
    CHECK(apogee::commands::make_built_in_tools(
              apogee::commands::BuiltInToolOptions{.config = &config})
              .find("web_search") == nullptr);
    config.tools.search.url = "not a url";
    CHECK(apogee::commands::make_built_in_tools(
              apogee::commands::BuiltInToolOptions{.config = &config})
              .find("web_search") == nullptr);
    config.tools.search.url = "http://127.0.0.1:8888";
    config.tools.search.results = 7;
    const apogee::agent::ToolRegistry registry = apogee::commands::make_built_in_tools(
        apogee::commands::BuiltInToolOptions{.config = &config});
    const apogee::agent::Tool* search = registry.find("web_search");
    REQUIRE(search != nullptr);
    CHECK(search->outbound);
    CHECK(search->describe_target("{}") == "127.0.0.1");
    CHECK(search->description.find("top 7 results") != std::string::npos);
    // A read-only agent keeps it, as it keeps fetch_url: it writes nothing.
    CHECK(apogee::commands::make_built_in_tools(
              apogee::commands::BuiltInToolOptions{
                  .config = &config, .policy = apogee::harness::AgentToolPolicy::ReadOnly})
              .find("web_search") != nullptr);
}

TEST_CASE("a search runs unasked, and the page it found is asked about like any website",
          "[commands][permissions][outbound][search]") {
    Config config;
    config.tools.search.url = "http://127.0.0.1:8888";
    std::vector<std::string> fetched;
    const apogee::agent::UrlFetcher web = [&fetched](std::string_view url) {
        fetched.emplace_back(url);
        if (url.starts_with("http://127.0.0.1:8888/")) {
            return apogee::agent::FetchResult{
                200,
                R"({"results":[{"title":"b6000","url":"https://github.com/ggml-org/llama.cpp/releases/tag/b6000","content":"notes"}]})",
                "", ""};
        }
        return apogee::agent::FetchResult{200, "<p>release notes</p>", "", ""};
    };
    std::string problem;
    const std::optional<apogee::agent::SearchInstance> instance =
        apogee::agent::search_instance(config.tools.search, problem);
    REQUIRE(instance.has_value());
    apogee::agent::ToolRegistry registry;
    registry.add(apogee::agent::make_web_search_tool(
        apogee::agent::make_searxng_provider(instance->base, web), instance->base.host, 5));
    registry.add(apogee::agent::make_fetch_url_tool(web));

    std::vector<std::string> asked;
    apogee::agent::DispatchContext context;
    context.permission = make_permission_checker(config, std::make_shared<SessionApprovals>());
    context.confirm = [&asked](const GateRequest& request) {
        asked.emplace_back(request.target);
        return true;
    };

    const apogee::agent::ToolOutcome found = apogee::agent::dispatch(
        registry, apogee::harness::ToolCall{"s", "web_search", R"({"query":"b6000"})"}, context);
    REQUIRE_FALSE(found.is_error);
    CHECK(asked.empty());  // the configured instance: never asked
    CHECK(found.content.find("https://github.com/ggml-org/llama.cpp/releases/tag/b6000") !=
          std::string::npos);

    const apogee::agent::ToolOutcome opened = apogee::agent::dispatch(
        registry,
        apogee::harness::ToolCall{
            "f", "fetch_url",
            R"({"url":"https://github.com/ggml-org/llama.cpp/releases/tag/b6000"})"},
        context);
    CHECK_FALSE(opened.is_error);
    CHECK(asked == std::vector<std::string>{"github.com"});  // the page: asked, by its host
    CHECK(fetched.size() == 2);

    // With nobody to answer, the search still runs and the page is refused.
    context.confirm = nullptr;
    CHECK_FALSE(apogee::agent::dispatch(
                    registry, apogee::harness::ToolCall{"s2", "web_search", R"({"query":"b6000"})"},
                    context)
                    .is_error);
    CHECK(apogee::agent::dispatch(
              registry,
              apogee::harness::ToolCall{
                  "f2", "fetch_url",
                  R"({"url":"https://github.com/ggml-org/llama.cpp/releases/tag/b6000"})"},
              context)
              .is_error);
    CHECK(fetched.size() == 3);
}

TEST_CASE("the real fetcher asks for one hop and a bounded body",
          "[commands][permissions][fetch]") {
    apogee::testing::FakeTransport::Reply moved;
    moved.status = 302;
    moved.location = "https://elsewhere.example/";
    apogee::testing::FakeTransport::Reply huge;
    huge.body = std::string(apogee::commands::kFetchMaxBodyBytes + 1, 'x');
    apogee::testing::FakeTransport::Reply typed;
    typed.body = "<p>page</p>";
    typed.content_type = "text/html; charset=utf-8";
    auto transport = std::make_unique<apogee::testing::FakeTransport>(
        std::vector<apogee::testing::FakeTransport::Reply>{moved, huge, typed});
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

    // The type travels with the body: the reader decides by it (25f).
    const apogee::agent::FetchResult page = fetcher("https://a.example/page");
    CHECK(page.content_type == "text/html; charset=utf-8");
    CHECK(page.body == "<p>page</p>");
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
    // Where no program can be started (Windows, today) the git and shell
    // tools are never registered at all (57k).
    const bool programs = apogee::tools::ToolsetOptions{}.child_processes;
    CHECK(registry.find("fetch_url") != nullptr);
    CHECK(registry.find("read_file") != nullptr);
    CHECK((registry.find("git_diff") != nullptr) == programs);
    CHECK(registry.find("write_note") != nullptr);
    CHECK(registry.find("run_command") == nullptr);
    CHECK(registry.find("search_documents") == nullptr);
    CHECK(registry.find("read_file")->description.find("/srv/sandbox") != std::string::npos);
    // Every toolset on, with nothing configured.
    CHECK((apogee::commands::make_built_in_tools({}).find("run_command") != nullptr) == programs);
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
    CHECK((read_only.find("git_diff") != nullptr) ==
          apogee::tools::ToolsetOptions{}.child_processes);  // 57k
    CHECK(read_only.find("write_file") == nullptr);
    CHECK(read_only.find("edit_file") == nullptr);
    CHECK(read_only.find("run_command") == nullptr);
    CHECK(read_only.find("delete_note") == nullptr);
    CHECK(read_only.find("grep_files") != nullptr);
    // The filtered registry keeps the environment note: a read-only agent
    // needs the date as much as any other.
    CHECK(read_only.environment().starts_with("Environment:\n- Today is "));
    // And its policy is read off what it holds (26p): it kept fetch_url, and
    // this config names no search.
    CHECK(read_only.environment().ends_with(apogee::tools::render_tool_use_policy(
        apogee::tools::ToolReach{.search = false, .read_pages = true})));
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

// ---------------------------------------------------------------------------
// Session presets (26o): the `session` answer, given early
// ---------------------------------------------------------------------------

namespace {

using apogee::commands::GatedName;
using apogee::commands::PermissionPresets;

/// The gated set the preset cases name.
std::vector<std::string> gated() {
    return {"delete_file", "run_command", "write_file"};
}

GateRequest website(std::string_view host) {
    GateRequest request{"fetch_url", host};
    request.outbound = true;
    return request;
}

}  // namespace

TEST_CASE("a preset resolves exactly as the session answer it gives early",
          "[commands][permissions][presets]") {
    // The equivalence table, over every config level: a flag-seeded session
    // and one where the user pressed `s` resolve identically -- and a config
    // deny is loosened by neither.
    for (const std::optional<PermissionLevel> level :
         {std::optional<PermissionLevel>{}, std::optional{PermissionLevel::Ask},
          std::optional{PermissionLevel::Allow}, std::optional{PermissionLevel::Deny}}) {
        Config config;
        if (level.has_value()) {
            config.permissions.levels["write_file"] = *level;
        }
        INFO("config level " << (level.has_value() ? static_cast<int>(*level) : -1));
        const auto seeded = std::make_shared<SessionApprovals>();
        REQUIRE(apogee::commands::seed_approvals(
                    *seeded, PermissionPresets{.allow = {"write_file"}}, gated())
                    .empty());
        const auto answered = std::make_shared<SessionApprovals>();
        answered->tools.insert("write_file");  // what the prompt's `s` does
        const Permission by_flag =
            make_permission_checker(config, seeded)(GateRequest{"write_file", "x"});
        CHECK(by_flag == make_permission_checker(config, answered)(GateRequest{"write_file", "x"}));
        CHECK(by_flag == (level == PermissionLevel::Deny ? Permission::Deny : Permission::Allow));
        // And another tool is still asked about.
        CHECK(make_permission_checker(config, seeded)(GateRequest{"run_command", "x"}) ==
              Permission::Ask);
    }
}

TEST_CASE("a session's no tightens, even over the config's allow",
          "[commands][permissions][presets]") {
    Config config;
    config.permissions.levels["write_file"] = PermissionLevel::Allow;
    config.tools.allowed_hosts = {"docs.python.org"};
    const auto approvals = std::make_shared<SessionApprovals>();
    REQUIRE(apogee::commands::seed_approvals(
                *approvals, PermissionPresets{.deny = {"write_file", "docs.python.org"}}, gated())
                .empty());
    const apogee::agent::PermissionChecker check = make_permission_checker(config, approvals);
    CHECK(check(GateRequest{"write_file", "x"}) == Permission::Deny);
    CHECK(check(website("docs.python.org")) == Permission::Deny);
    // Both said in one run: no wins.
    const auto both = std::make_shared<SessionApprovals>();
    REQUIRE(
        apogee::commands::seed_approvals(
            *both, PermissionPresets{.allow = {"run_command"}, .deny = {"run_command"}}, gated())
            .empty());
    CHECK(make_permission_checker({}, both)(GateRequest{"run_command", "x"}) == Permission::Deny);
}

TEST_CASE("an --allow-host preset answers one website's ask, and no other",
          "[commands][permissions][presets]") {
    const auto approvals = std::make_shared<SessionApprovals>();
    REQUIRE(apogee::commands::seed_approvals(
                *approvals, PermissionPresets{.allow_hosts = {"Example.org"}}, gated())
                .empty());
    const apogee::agent::PermissionChecker check = make_permission_checker({}, approvals);
    CHECK(check(website("example.org")) == Permission::Allow);
    CHECK(check(website("other.org")) == Permission::Ask);
    CHECK(check(website("www.example.org")) == Permission::Ask);
}

TEST_CASE("a name that is neither a gated tool nor a website is refused, naming the set",
          "[commands][permissions][presets]") {
    SessionApprovals approvals;
    const std::string typo = apogee::commands::seed_approvals(
        approvals, PermissionPresets{.allow = {"wrte_file"}}, gated());
    CHECK(typo.find("--allow: 'wrte_file' is not a tool this chat asks about") !=
          std::string::npos);
    CHECK(typo.find("delete_file, run_command, write_file") != std::string::npos);
    // `--allow` is for tools: a website has its own flag.
    CHECK(apogee::commands::seed_approvals(approvals, PermissionPresets{.allow = {"example.org"}},
                                           gated())
              .find("--allow-host") != std::string::npos);
    CHECK_FALSE(apogee::commands::seed_approvals(
                    approvals, PermissionPresets{.allow_hosts = {"not a host"}}, gated())
                    .empty());
    CHECK(approvals.tools.empty());
    CHECK(apogee::commands::name_gated("example.org", gated()).host);
    CHECK_FALSE(apogee::commands::name_gated("example.org", gated(), false).error.empty());
}

TEST_CASE("the launch flag and the slash verbs deny every host --allow-host takes",
          "[commands][permissions][presets][57o]") {
    // 26o's "--deny takes either kind", regressed by a dot test (57o): each
    // name `canonical_host` accepts is a website unless it is shaped like a
    // tool -- dotless with a `_`.
    for (const auto& [given, key] : std::vector<std::pair<std::string, std::string>>{
             {"localhost", "localhost"},
             {"LocalHost", "localhost"},
             {"intranet", "intranet"},
             {"[::1]", "::1"},
             {"::1", "::1"},
             {"[::ffff:127.0.0.1]", "::ffff:127.0.0.1"},
             {"docs.python.org", "docs.python.org"},
             {"a_b.example", "a_b.example"}}) {
        INFO(given);
        const GatedName name = apogee::commands::name_gated(given, gated());
        CHECK(name.error.empty());
        CHECK(name.host);
        CHECK(name.key == key);
        // The same canonical form `--allow-host` stores.
        CHECK(apogee::harness::canonical_host(given) == key);
    }
    // Tool-shaped names are still typos, and a gated tool is still a tool.
    for (const char* typo : {"write_flie", "mcp__srv__echo", "run_comand"}) {
        INFO(typo);
        const GatedName name = apogee::commands::name_gated(typo, gated());
        CHECK_FALSE(name.host);
        CHECK(name.error.find("is not a tool this chat asks about") != std::string::npos);
    }
    CHECK_FALSE(apogee::commands::name_gated("write_file", gated()).host);
    // Not a host at all: refused as before.
    CHECK_FALSE(apogee::commands::name_gated("not a host", gated()).error.empty());
    CHECK_FALSE(apogee::commands::name_gated("[::1", gated()).error.empty());
    // `--allow` stays tools only.
    CHECK_FALSE(apogee::commands::name_gated("localhost", gated(), false).error.empty());

    // The launch flag, and what the checker then does in each mode: a
    // session's no comes before bypass.
    for (const char* host : {"localhost", "[::1]", "::1", "intranet"}) {
        INFO(host);
        const auto approvals = std::make_shared<SessionApprovals>();
        REQUIRE(
            apogee::commands::seed_approvals(*approvals, PermissionPresets{.deny = {host}}, gated())
                .empty());
        approvals->mode = apogee::commands::PermissionMode::Bypass;
        const apogee::agent::PermissionChecker check = make_permission_checker({}, approvals);
        CHECK(check(website(*apogee::harness::canonical_host(host))) == Permission::Deny);
        CHECK(check(website(host)) == Permission::Deny);
        CHECK(check(website("example.org")) == Permission::Allow);
    }
    SessionApprovals approvals;
    CHECK(apogee::commands::seed_approvals(approvals, PermissionPresets{.deny = {"write_flie"}},
                                           gated())
              .starts_with("--deny: 'write_flie' is not a tool this chat asks about"));

    // A session's yes for localhost -- the prompt's `s` -- is taken back.
    const auto answered = std::make_shared<SessionApprovals>();
    std::string note;
    GateRequest asked{"fetch_url", "localhost"};
    asked.outbound = true;
    REQUIRE(apogee::commands::answer_permission("s", asked, {}, answered, note));
    CHECK(answered->hosts.contains("localhost"));
    const GatedName localhost = apogee::commands::name_gated("localhost", gated());
    apogee::commands::deny_for_session(*answered, localhost);
    CHECK(make_permission_checker({}, answered)(website("localhost")) == Permission::Deny);
    CHECK(apogee::commands::revoke_for_session(*answered, localhost));
    CHECK(make_permission_checker({}, answered)(website("localhost")) == Permission::Ask);
}

TEST_CASE("the write prompt names the file written, on the terminal and in machine mode",
          "[commands][permissions][machine][57o]") {
    // The sweep's PTY probe (57o): the model's path repainted the prompt line
    // and `..` dropped a component that does not exist, so the person saw
    // `write_file -> notes.txt` and victim.txt was overwritten.
    const apogee::testing::TempDir temp{"permissions-target-" +
                                        std::to_string(std::random_device{}())};
    const std::filesystem::path root = temp.path() / "work2";
    std::filesystem::create_directories(root);
    std::ofstream{root / "victim.txt"} << "original";
    apogee::agent::ToolRegistry registry;
    apogee::tools::register_fs_tools(registry, root);
    const std::string probe =
        "x\r\x1b[2K\x1b[35m[perm]\x1b[0m write_file -> notes.txt\x1b[8m/../victim.txt";

    std::vector<std::string> printed;
    std::ostringstream machine;
    apogee::commands::JsonReporter reporter{machine};
    apogee::agent::DispatchContext context;
    context.permission = make_permission_checker({}, nullptr);
    context.confirm = [&](const GateRequest& request) {
        // What the terminal prints first, and what machine mode sends.
        printed.push_back(apogee::commands::permission_prompt_line(request, apogee::ansi::Style{}));
        reporter.emit_permission_question(request);
        return true;
    };
    const apogee::agent::ToolOutcome wrote = apogee::agent::dispatch(
        registry,
        apogee::harness::ToolCall{
            "w", "write_file", nlohmann::json{{"path", probe}, {"content", "overwritten"}}.dump()},
        context);
    REQUIRE_FALSE(wrote.is_error);
    REQUIRE(printed.size() == 1);
    CHECK(printed.front().ends_with(" write_file -> victim.txt"));
    CHECK(printed.front().find("notes.txt") == std::string::npos);
    CHECK(printed.front().find('\x1b') == std::string::npos);
    CHECK(printed.front().find('\r') == std::string::npos);
    const nlohmann::json event = nlohmann::json::parse(machine.str());
    CHECK(event["kind"] == "permission");
    CHECK(event["target"] == "victim.txt");
    std::ifstream victim{root / "victim.txt"};
    CHECK(std::string{std::istreambuf_iterator<char>{victim}, {}} == "overwritten");
}

namespace {

/// `text` with every SGR sequence of the prompt's own styling taken out.
std::string without_sgr(std::string text) {
    for (std::size_t at = text.find('\x1b'); at != std::string::npos; at = text.find('\x1b', at)) {
        std::size_t end = at + 1;
        if (end < text.size() && text[end] == '[') {
            ++end;
            while (end < text.size() &&
                   (std::isdigit(static_cast<unsigned char>(text[end])) != 0 || text[end] == ';')) {
                ++end;
            }
            if (end < text.size() && text[end] == 'm') {
                text.erase(at, end + 1 - at);
                continue;
            }
        }
        ++at;
    }
    return text;
}

const std::string kFffd{apogee::ansi::kReplacementCharacter};

}  // namespace

TEST_CASE("a forged command is drawn as it is: the person cannot be shown ls -la for touch PWNED",
          "[commands][permissions][57p]") {
    // The sweep's entry 1 (rated high), its PTY probe's very command: the CR
    // and the erase drew `[permission] run_command -> ls -la` over the real
    // prompt line, the person typed y, and touch PWNED ran.
    const std::string probe =
        "touch PWNED #\r\x1b[2K\x1b[1;35m[permission]\x1b[0m run_command -> ls -la";
    REQUIRE(probe.size() < 80);  // not cut: the target is the whole command
    const std::string detail = probe + "\ncwd: /work";
    const GateRequest request{"run_command", probe, detail};

    const std::vector<std::string> plain =
        apogee::commands::permission_prompt_lines(request, apogee::ansi::Style{});
    const std::string drawn = "touch PWNED #" + kFffd + kFffd + "[2K" + kFffd +
                              "[1;35m[permission]" + kFffd + "[0m run_command -> ls -la";
    REQUIRE(plain.size() == 3);
    CHECK(plain[0] == "[perm] run_command -> " + drawn);
    // The detail as text: the CR dropped, the escapes shown, every line under
    // the prompt.
    CHECK(plain[1] == "  touch PWNED #" + kFffd + "[2K" + kFffd + "[1;35m[permission]" + kFffd +
                          "[0m run_command -> ls -la");
    CHECK(plain[2] == "  cwd: /work");
    for (const std::string& line : plain) {
        INFO(line);
        CHECK(line.find('\x1b') == std::string::npos);
        CHECK(line.find('\r') == std::string::npos);
    }

    // In colour, the prompt's own styling and nothing of the command's.
    const std::vector<std::string> coloured =
        apogee::commands::permission_prompt_lines(request, apogee::ansi::Style{true});
    REQUIRE(coloured.size() == plain.size());
    for (std::size_t i = 0; i < coloured.size(); ++i) {
        CHECK(without_sgr(coloured[i]) == plain[i]);
        CHECK(coloured[i].find('\r') == std::string::npos);
    }
    CHECK(coloured[0].find("\x1b[2K") == std::string::npos);
    CHECK(coloured[0].find("\x1b[1;35m[permission]") == std::string::npos);
}

TEST_CASE(
    "a search query's lines stay under the prompt, and a target already whole is not repeated",
    "[commands][permissions][57p]") {
    // web_search's detail, `search: <query>`, through the same lines: a
    // newline in the query cannot start a forged prompt line at column 0.
    GateRequest search{"web_search", "search.example",
                       "search: cats\n[permission] fetch_url -> evil.example\x1b[8m"};
    search.outbound = true;
    const std::vector<std::string> lines =
        apogee::commands::permission_prompt_lines(search, apogee::ansi::Style{});
    REQUIRE(lines.size() == 3);
    CHECK(lines[0] == "[perm] web_search -> search.example");
    CHECK(lines[1] == "  search: cats");
    CHECK(lines[2] == "  [permission] fetch_url -> evil.example" + kFffd + "[8m");

    // An MCP tool whose arguments fit: the target is them, said once.
    const GateRequest whole{"mcp__srv__write", R"({"path":"a"})", R"({"path":"a"})"};
    CHECK(apogee::commands::permission_prompt_lines(whole, apogee::ansi::Style{}).size() == 1);
    // A file writer's change, still by its counts (52a).
    GateRequest edit{"edit_file", "x", "--- a/x\n+++ b/x\n@@ -1 +1 @@\n-a\n+b\n"};
    edit.edits = true;
    const std::vector<std::string> counted =
        apogee::commands::permission_prompt_lines(edit, apogee::ansi::Style{});
    REQUIRE(counted.size() == 2);
    CHECK(counted[1].starts_with("  +1"));
}

TEST_CASE(
    "run_command is asked about with its whole command and cwd, on the terminal and in "
    "machine mode",
    "[commands][permissions][machine][57p]") {
    // The sweep's entry 2: a command past 80 characters was asked about by
    // its first 77, no detail, and nothing earlier in the stream said more.
    const apogee::testing::TempDir temp{"permissions-detail-" +
                                        std::to_string(std::random_device{}())};
    apogee::agent::ToolRegistry registry;
    apogee::tools::register_shell_tool(registry, temp.path(), std::chrono::seconds{10});
    const std::string command =
        "echo checking the build status of the project before continuing with tests;  "
        "touch PWNED-by-the-hidden-tail-of-the-command";
    REQUIRE(command.size() > 120);

    std::vector<std::string> printed;
    std::ostringstream machine;
    apogee::commands::JsonReporter reporter{machine};
    apogee::agent::DispatchContext context;
    context.permission = make_permission_checker({}, nullptr);
    context.confirm = [&](const GateRequest& request) {
        printed = apogee::commands::permission_prompt_lines(request, apogee::ansi::Style{});
        reporter.emit_permission_question(request);
        return false;  // denied: nothing runs
    };
    const apogee::agent::ToolOutcome denied = apogee::agent::dispatch(
        registry,
        apogee::harness::ToolCall{
            "c", "run_command",
            nlohmann::json{{"command", command}, {"cwd", temp.path().string()}}.dump()},
        context);
    CHECK(denied.is_error);
    const std::string cut = command.substr(0, 77) + "...";
    REQUIRE(printed.size() == 3);
    CHECK(printed[0] == "[perm] run_command -> " + cut);
    CHECK(printed[1] == "  " + command);
    CHECK(printed[2] == "  cwd: " + temp.path().string());
    const nlohmann::json event = nlohmann::json::parse(machine.str());
    CHECK(event["kind"] == "permission");
    CHECK(event["target"] == cut);  // the short form, unchanged
    CHECK(event["detail"] == command + "\ncwd: " + temp.path().string());
    CHECK_FALSE(std::filesystem::exists(temp.path() / "PWNED-by-the-hidden-tail-of-the-command"));
}

namespace {

/// A registry built as a `--tools` session builds it, over one fake server
/// whose `echo` says readOnlyHint and whose `write` says nothing -- with the
/// config's level for `mcp__srv__echo`, when given. Built in place: the tools
/// keep pointers to the config and the server registry.
struct McpWorld {
    Config config;
    std::vector<std::shared_ptr<apogee::testing::FakeMcpTransport>> made;
    std::shared_ptr<apogee::mcp::Registry> mcp = std::make_shared<apogee::mcp::Registry>();
    apogee::agent::ToolRegistry registry;

    explicit McpWorld(
        std::optional<PermissionLevel> level,
        apogee::harness::AgentToolPolicy policy = apogee::harness::AgentToolPolicy::All) {
        apogee::harness::McpServerConfig well;
        well.command = "well";
        config.mcp_servers.emplace("srv", well);
        if (level.has_value()) {
            config.permissions.levels["mcp__srv__echo"] = *level;
        }
        registry = apogee::commands::make_built_in_tools(
            apogee::commands::BuiltInToolOptions{.config = &config,
                                                 .policy = policy,
                                                 .mcp = mcp,
                                                 .mcp_spawn = apogee::testing::fake_fleet(&made)});
    }

    /// How many `tools/call` frames reached the server.
    [[nodiscard]] int calls() const {
        int calls = 0;
        for (const auto& transport : made) {
            for (const nlohmann::json& frame : transport->sent()) {
                if (frame.value("method", "") == "tools/call") {
                    ++calls;
                }
            }
        }
        return calls;
    }

    [[nodiscard]] apogee::agent::ToolOutcome call(
        const apogee::agent::DispatchContext& context) const {
        return apogee::agent::dispatch(
            registry, apogee::harness::ToolCall{"e", "mcp__srv__echo", R"({"text":"hi"})"},
            context);
    }
};

}  // namespace

TEST_CASE("a config level on an MCP tool binds even when its server calls it read-only",
          "[commands][permissions][mcp][57o]") {
    // The sweep (57o): `deny` on a tool whose server sends readOnlyHint was
    // accepted, shown by `check` as `ok ... deny`, and the tool still ran. A
    // level the person set now gates it; with none, the hint still decides.
    SECTION("deny: refused unrun, with or without someone to ask") {
        const McpWorld world{PermissionLevel::Deny};
        REQUIRE(world.registry.find("mcp__srv__echo") != nullptr);
        CHECK(world.registry.find("mcp__srv__echo")->writes);
        int asked = 0;
        apogee::agent::DispatchContext context;
        context.permission = make_permission_checker(world.config, nullptr);
        context.confirm = [&asked](const GateRequest&) {
            ++asked;
            return true;
        };
        const apogee::agent::ToolOutcome refused = world.call(context);
        CHECK(refused.is_error);
        CHECK(refused.content.find("denied permission") != std::string::npos);
        CHECK(asked == 0);
        // Bypass does not loosen a config deny either.
        const auto bypass = std::make_shared<SessionApprovals>();
        bypass->mode = apogee::commands::PermissionMode::Bypass;
        context.permission = make_permission_checker(world.config, bypass);
        CHECK(world.call(context).is_error);
        CHECK(world.calls() == 0);
        // And the session's gated set names it, so /allow and /deny can.
        const std::vector<std::string> named = apogee::commands::gated_tools(world.registry);
        CHECK(std::ranges::find(named, "mcp__srv__echo") != named.end());
    }
    SECTION("ask: asked where someone can answer, denied where nobody can") {
        const McpWorld world{PermissionLevel::Ask};
        apogee::agent::DispatchContext pipe;
        pipe.permission = make_permission_checker(world.config, nullptr);
        CHECK(world.call(pipe).is_error);
        CHECK(world.calls() == 0);
        int asked = 0;
        apogee::agent::DispatchContext terminal = pipe;
        terminal.confirm = [&asked](const GateRequest& request) {
            CHECK(request.tool == "mcp__srv__echo");
            ++asked;
            return true;
        };
        CHECK_FALSE(world.call(terminal).is_error);
        CHECK(asked == 1);
        CHECK(world.calls() == 1);
    }
    SECTION("allow: runs unasked -- the hint's default, so in plan mode too") {
        // `allow` gates nothing the hint left ungated (the review): plan
        // mode refuses every writer over any allow, so a read tool made a
        // writer by its `allow` was refused there for a false reason.
        const McpWorld world{PermissionLevel::Allow};
        CHECK_FALSE(world.registry.find("mcp__srv__echo")->writes);
        apogee::agent::DispatchContext pipe;
        pipe.permission = make_permission_checker(world.config, nullptr);
        CHECK_FALSE(world.call(pipe).is_error);
        CHECK(world.calls() == 1);
        const auto plan = std::make_shared<SessionApprovals>();
        plan->mode = apogee::commands::PermissionMode::Plan;
        apogee::agent::DispatchContext planning;
        planning.permission = make_permission_checker(world.config, plan);
        const apogee::agent::ToolOutcome ran = world.call(planning);
        CHECK_FALSE(ran.is_error);
        CHECK(ran.content.find("plan mode") == std::string::npos);
        CHECK(world.calls() == 2);
        // A tool with no hint is a writer as ever: `allow` runs it, and plan
        // mode still refuses it over that allow.
        McpWorld writer{std::nullopt};
        writer.config.permissions.levels["mcp__srv__write"] = PermissionLevel::Allow;
        CHECK(writer.registry.find("mcp__srv__write")->writes);
        planning.permission = make_permission_checker(writer.config, plan);
        CHECK(apogee::agent::dispatch(
                  writer.registry,
                  apogee::harness::ToolCall{"w", "mcp__srv__write", R"({"text":"hi"})"}, planning)
                  .is_error);
        // And a deny stays a deny in plan mode.
        const McpWorld denied{PermissionLevel::Deny};
        planning.permission = make_permission_checker(denied.config, plan);
        CHECK(denied.call(planning).is_error);
        CHECK(denied.calls() == 0);
    }
    SECTION("no level: the hint's default -- ungated, runs unprompted") {
        const McpWorld world{std::nullopt};
        CHECK_FALSE(world.registry.find("mcp__srv__echo")->writes);
        CHECK(world.registry.find("mcp__srv__write")->writes);  // no hint: gated as ever
        apogee::agent::DispatchContext pipe;
        pipe.permission = make_permission_checker(world.config, nullptr);
        CHECK_FALSE(world.call(pipe).is_error);
        CHECK(world.calls() == 1);
    }
    SECTION("a read-only agent drops a denied or asked tool and keeps an allowed one") {
        // `deny` and `ask` make the tool gated, so a read-only agent treats
        // it as one with no hint; `allow`, like no level, keeps the hint.
        for (const PermissionLevel level : {PermissionLevel::Deny, PermissionLevel::Ask}) {
            const McpWorld gated{level, apogee::harness::AgentToolPolicy::ReadOnly};
            CHECK(gated.registry.find("mcp__srv__echo") == nullptr);
        }
        const McpWorld allowed{PermissionLevel::Allow, apogee::harness::AgentToolPolicy::ReadOnly};
        CHECK(allowed.registry.find("mcp__srv__echo") != nullptr);
        const McpWorld unnamed{std::nullopt, apogee::harness::AgentToolPolicy::ReadOnly};
        CHECK(unnamed.registry.find("mcp__srv__echo") != nullptr);
    }
}

TEST_CASE("a shell or git tool this build never offers is refused saying why, not as a typo",
          "[commands][permissions][presets][57k]") {
    // On a build that cannot start a program (Windows, today) run_command and
    // the git tools are never registered, so `--allow run_command`, `--deny
    // git_log` and `/allow run_command` name a tool the chat lacks: the
    // refusal says why it is not here, and never sends the user to
    // --allow-host.
    const std::vector<std::string> without = {"delete_file", "write_file"};
    const GatedName shell = apogee::commands::name_gated("run_command", without, true, false);
    CHECK(shell.unoffered);
    CHECK(shell.key.empty());
    CHECK(shell.error.find("'run_command' is a shell tool") != std::string::npos);
    CHECK(shell.error.find("cannot start other programs") != std::string::npos);
    CHECK(apogee::commands::name_gated("git_log", without, false, false).unoffered);
    // A typo is still a typo there, and run_command where programs start is
    // the plain refusal naming the set.
    const GatedName typo = apogee::commands::name_gated("wrte_file", without, true, false);
    CHECK_FALSE(typo.unoffered);
    CHECK(typo.error.find("is not a tool this chat asks about") != std::string::npos);
    const GatedName elsewhere = apogee::commands::name_gated("run_command", without, true, true);
    CHECK_FALSE(elsewhere.unoffered);
    CHECK(elsewhere.error.find("delete_file, write_file") != std::string::npos);

    // The launch flags, on this build: where run_command is not offered for
    // the reason above, the refusal says it, with no website hint.
    if (!apogee::tools::ToolsetOptions{}.child_processes) {
        SessionApprovals approvals;
        const std::string allowed = apogee::commands::seed_approvals(
            approvals, PermissionPresets{.allow = {"run_command"}}, without);
        CHECK(allowed.starts_with("--allow: 'run_command' is a shell tool"));
        CHECK(allowed.find("--allow-host") == std::string::npos);
        CHECK(apogee::commands::seed_approvals(approvals, PermissionPresets{.deny = {"git_log"}},
                                               without)
                  .starts_with("--deny: 'git_log' is a git tool"));
    }
}

TEST_CASE("revoke forgets the session's own answer, never the config's",
          "[commands][permissions][presets]") {
    SessionApprovals approvals;
    const GatedName tool = apogee::commands::name_gated("run_command", gated());
    apogee::commands::allow_for_session(approvals, tool);
    CHECK(approvals.tools.contains("run_command"));
    apogee::commands::deny_for_session(approvals, tool);
    CHECK_FALSE(approvals.tools.contains("run_command"));
    CHECK(approvals.denied_tools.contains("run_command"));
    // Allowed again, the denial is lifted: one answer per name.
    apogee::commands::allow_for_session(approvals, tool);
    CHECK(approvals.denied_tools.empty());
    CHECK(apogee::commands::revoke_for_session(approvals, tool));
    CHECK_FALSE(apogee::commands::revoke_for_session(approvals, tool));
    CHECK(approvals.tools.empty());
    const GatedName host = apogee::commands::name_gated("docs.python.org", gated());
    apogee::commands::allow_for_session(approvals, host);
    CHECK(apogee::commands::revoke_for_session(approvals, host));
    CHECK(approvals.hosts.empty());
}

TEST_CASE("/permissions says each answer and where it comes from",
          "[commands][permissions][presets]") {
    Config config;
    config.permissions.levels["delete_file"] = PermissionLevel::Allow;
    config.permissions.levels["run_command"] = PermissionLevel::Ask;
    config.tools.allowed_hosts = {"api.github.com"};
    SessionApprovals approvals;
    apogee::commands::allow_for_session(approvals,
                                        apogee::commands::name_gated("write_file", gated()));
    apogee::commands::deny_for_session(approvals,
                                       apogee::commands::name_gated("example.org", gated()));
    const std::vector<std::string> lines =
        apogee::commands::describe_permissions(config, approvals, gated());
    CHECK(lines == std::vector<std::string>{"delete_file  allow  (config)",
                                            "run_command  ask    (config)",
                                            "write_file   allow  (this session)",
                                            "website example.org: deny (this session)",
                                            "website api.github.com: allow (config)"});
    CHECK(apogee::commands::describe_permissions({}, {}, {}) ==
          std::vector<std::string>{"no tool in this chat asks permission (--tools)"});
}

// ---------------------------------------------------------------------------
// The session's permission mode (39b)
// ---------------------------------------------------------------------------

TEST_CASE("each permission mode answers what manual would ask, and a deny wins in all five",
          "[commands][permissions][modes]") {
    using apogee::commands::PermissionMode;
    Config config;
    config.permissions.levels["delete_note"] = PermissionLevel::Allow;
    config.permissions.levels["run_command"] = PermissionLevel::Deny;
    const auto approvals = std::make_shared<SessionApprovals>();
    approvals->denied_tools.insert("delete_file");
    approvals->denied_hosts.insert("evil.example");
    const apogee::agent::PermissionChecker check = make_permission_checker(config, approvals);
    // An edit, an edit the config allows, a command the config denies, an
    // edit the session denies, an MCP writer, a website, a denied website.
    const GateRequest edit{"write_file", "notes.txt", {}, false, true};
    const GateRequest allowed_edit{"delete_note", "todo", {}, false, true};
    const GateRequest command{"run_command", "ls"};
    const GateRequest denied_edit{"delete_file", "x", {}, false, true};
    const GateRequest mcp{"mcp_create_issue", "repo"};
    const GateRequest page{"fetch_url", "docs.python.org", "https://docs.python.org/", true};
    const GateRequest denied_site{"fetch_url", "evil.example", "https://evil.example/", true};
    const auto answers = [&]() {
        return std::vector<Permission>{check(edit),        check(allowed_edit), check(command),
                                       check(denied_edit), check(mcp),          check(page),
                                       check(denied_site)};
    };
    const Permission A = Permission::Allow;
    const Permission D = Permission::Deny;
    const Permission Q = Permission::Ask;

    // Manual: the gate as it was.
    CHECK(answers() == std::vector<Permission>{Q, A, D, D, Q, Q, D});
    // Accept-edits: only the unanswered edit changes.
    approvals->mode = PermissionMode::AcceptEdits;
    CHECK(answers() == std::vector<Permission>{A, A, D, D, Q, Q, D});
    // Plan: every writer refused, the config's allow included; websites ask.
    approvals->mode = PermissionMode::Plan;
    CHECK(answers() == std::vector<Permission>{D, D, D, D, D, Q, D});
    // Dont-ask: what would ask is refused; what is allowed still runs.
    approvals->mode = PermissionMode::DontAsk;
    CHECK(answers() == std::vector<Permission>{D, A, D, D, D, D, D});
    // Bypass: nothing asks -- and every deny still refuses.
    approvals->mode = PermissionMode::Bypass;
    CHECK(answers() == std::vector<Permission>{A, A, D, D, A, A, D});
}

TEST_CASE("Shift+Tab's cycle: manual, accept-edits, plan, bypass when allowed, back to manual",
          "[commands][permissions][modes]") {
    using apogee::commands::next_permission_mode;
    using apogee::commands::PermissionMode;
    CHECK(next_permission_mode(PermissionMode::Manual, false) == PermissionMode::AcceptEdits);
    CHECK(next_permission_mode(PermissionMode::AcceptEdits, false) == PermissionMode::Plan);
    CHECK(next_permission_mode(PermissionMode::Plan, false) == PermissionMode::Manual);
    CHECK(next_permission_mode(PermissionMode::Plan, true) == PermissionMode::Bypass);
    CHECK(next_permission_mode(PermissionMode::Bypass, true) == PermissionMode::Manual);
    // Dont-ask is never in the cycle: leaving it goes to manual.
    CHECK(next_permission_mode(PermissionMode::DontAsk, true) == PermissionMode::Manual);

    for (const PermissionMode mode : apogee::commands::kPermissionModes) {
        CHECK(apogee::commands::permission_mode_from_string(apogee::commands::to_string(mode)) ==
              mode);
        CHECK_FALSE(apogee::commands::permission_mode_description(mode).empty());
    }
    CHECK_FALSE(apogee::commands::permission_mode_from_string("acceptEdits").has_value());
    CHECK(apogee::commands::permission_mode_words() ==
          std::vector<std::string>{"manual", "accept-edits", "plan", "dont-ask", "bypass"});
    // Claude Code's status words; manual shows nothing.
    const apogee::ansi::Style plain{false};
    CHECK(apogee::commands::permission_mode_label(PermissionMode::Manual, plain).empty());
    CHECK(apogee::commands::permission_mode_label(PermissionMode::AcceptEdits, plain) ==
          "⏵⏵ accept edits on");
    CHECK(apogee::commands::permission_mode_label(PermissionMode::Plan, plain) == "⏸ plan mode on");
    CHECK(apogee::commands::permission_mode_label(PermissionMode::DontAsk, plain) ==
          "⏵⏵ don't ask on");
    CHECK(apogee::commands::permission_mode_label(PermissionMode::Bypass, plain) ==
          "⏵⏵ bypass permissions on");
}

TEST_CASE("plan mode's note and a mode's refusal are said only in their mode",
          "[commands][permissions][modes]") {
    using apogee::commands::PermissionMode;
    const auto approvals = std::make_shared<SessionApprovals>();
    const apogee::agent::RefusalNote refusal = apogee::commands::mode_refusal(approvals);
    const GateRequest write{"write_file", "x"};
    const GateRequest page{"fetch_url", "pypi.org", {}, true};
    CHECK(apogee::commands::plan_note(*approvals).empty());
    CHECK(refusal(write).empty());
    approvals->mode = PermissionMode::Plan;
    CHECK(apogee::commands::plan_note(*approvals).find("Plan mode is on") != std::string::npos);
    CHECK(refusal(write).find("plan mode is on") != std::string::npos);
    CHECK(refusal(page).empty());  // plan mode still asks about websites
    approvals->mode = PermissionMode::DontAsk;
    CHECK(apogee::commands::plan_note(*approvals).empty());
    CHECK(refusal(page).find("dont-ask") != std::string::npos);
    CHECK_FALSE(apogee::commands::mode_refusal(nullptr));
}

TEST_CASE("/permissions leads with the mode and says what it does to each answer",
          "[commands][permissions][modes]") {
    using apogee::commands::PermissionMode;
    Config config;
    config.permissions.levels["run_command"] = PermissionLevel::Ask;
    SessionApprovals approvals;
    const std::vector<std::string> edits{"delete_file", "write_file"};
    approvals.mode = PermissionMode::AcceptEdits;
    CHECK(apogee::commands::describe_permissions(config, approvals, gated(), edits) ==
          std::vector<std::string>{
              "mode: accept-edits -- file and note edits run without asking; commands and "
              "websites still ask",
              "delete_file  allow  (accept-edits mode)", "run_command  ask    (config)",
              "write_file   allow  (accept-edits mode)"});
    approvals.mode = PermissionMode::Plan;
    const std::vector<std::string> plan =
        apogee::commands::describe_permissions(config, approvals, gated(), edits);
    REQUIRE(plan.size() == 4);
    CHECK(plan.at(0) == "mode: plan -- nothing that writes runs; the model plans");
    CHECK(plan.at(2) == "run_command  deny   (plan mode)");
    approvals.mode = PermissionMode::Bypass;
    CHECK(apogee::commands::describe_permissions(config, approvals, gated(), edits).at(2) ==
          "run_command  allow  (bypass mode)");
    approvals.mode = PermissionMode::DontAsk;
    CHECK(apogee::commands::describe_permissions(config, approvals, gated(), edits).at(2) ==
          "run_command  deny   (dont-ask mode)");
}
