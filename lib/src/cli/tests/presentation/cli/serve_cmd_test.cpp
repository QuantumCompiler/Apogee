#include "cli/serve_cmd.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <random>
#include <string>
#include <utility>

#include "agent/tool.h"
#include "cli/helpers.h"
#include "cli/permissions.h"
#include "contracts/config.h"
#include "platform/child_process.h"
#include "support/cli_home.h"
#include "support/env_guard.h"
#include "tools/todo.h"

/// What `apogee serve` tells its operator about the tools it offers (57n):
/// the startup line built from the registry `--tools` really builds -- the
/// one chat and `complete` build -- and the flag's help saying the same. It
/// once said `fetch_url` while every native toolset was served.
namespace {

using apogee::commands::serve_summary;
using apogee::commands::ServeSummaryInput;
using Catch::Matchers::ContainsSubstring;

struct Sandbox {
    apogee::testing::TempDir home{"serve-cmd-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};
    std::filesystem::path root = home.path() / "project";

    Sandbox() {
        std::filesystem::create_directories(root);
    }

    [[nodiscard]] apogee::harness::Config config(const std::string& extra = {}) const {
        return apogee::harness::parse_config(
            "backends:\n  mock:\n    type: mock\n    model: mock-1\nmodels:\n  default: mock\n"
            "tools:\n  fs_root: " +
                root.generic_string() + "\n" + extra,
            "<test>");
    }
};

/// The startup line for a server built as `serve --tools` builds one.
std::string summary_for(const Sandbox& sandbox, const apogee::harness::Config& config) {
    apogee::agent::ToolRegistry registry =
        apogee::commands::make_built_in_tools({.config = &config});
    apogee::tools::register_todo_tool(registry);
    ServeSummaryInput input;
    input.served = {"mock"};
    input.default_backend = "mock";
    input.tools = &registry;
    input.permission = apogee::commands::make_permission_checker(config, nullptr);
    input.fs_root = sandbox.root;
    const std::string line = serve_summary(input);
    CHECK_THAT(line, ContainsSubstring("tools: " + std::to_string(registry.size()) + " ("));
    return line;
}

}  // namespace

TEST_CASE("serve's startup line counts the tools it offers and names each set",
          "[cli][serve][tools]") {
    const Sandbox sandbox;
    const std::string line = summary_for(sandbox, sandbox.config());
    CAPTURE(line);
    CHECK_THAT(line, ContainsSubstring("serving mock (default mock)"));
    CHECK_THAT(line, ContainsSubstring("files under " + sandbox.root.string()));
    CHECK_THAT(line, ContainsSubstring("notes"));
    CHECK_THAT(line, ContainsSubstring("rag"));
    CHECK_THAT(line, ContainsSubstring("graph"));
    // A website is reached unasked only when the config lists it.
    CHECK_THAT(line, ContainsSubstring("fetch_url to listed hosts"));
    CHECK_THAT(line, ContainsSubstring("todo_write"));
    if (apogee::platform::supports_child_processes()) {
        CHECK_THAT(line, ContainsSubstring("shell"));
        CHECK_THAT(line, ContainsSubstring("git"));
    }
    CHECK(line.find("tools: fetch_url") == std::string::npos);
    CHECK_THAT(line, ContainsSubstring("-- runs unasked: none"));
    CHECK_THAT(line, ContainsSubstring("-- rag: off"));
    CHECK_THAT(line, ContainsSubstring("-- sessions idle out after 60 min"));
}

TEST_CASE("serve's startup line names every tool the config lets run unasked",
          "[cli][serve][tools][permission]") {
    const Sandbox sandbox;
    const std::string line = summary_for(
        sandbox, sandbox.config("permissions:\n  run_command: allow\n  write_file: allow\n"
                                "  delete_file: deny\n  edit_file: ask\n"));
    CAPTURE(line);
    if (apogee::platform::supports_child_processes()) {
        CHECK_THAT(line, ContainsSubstring("-- runs unasked: run_command, write_file"));
    } else {
        // No shell where no program can be started (57k): only what is offered.
        CHECK_THAT(line, ContainsSubstring("-- runs unasked: write_file"));
    }
    CHECK(line.find("delete_file") == std::string::npos);
    CHECK(line.find("edit_file") == std::string::npos);
}

TEST_CASE("serve's startup line groups MCP tools by server and says when there are no tools",
          "[cli][serve][tools][mcp]") {
    apogee::agent::ToolRegistry registry;
    for (const char* name : {"mcp__srv__lookup", "mcp__srv__write", "mcp__docs__search_docs"}) {
        apogee::agent::Tool tool;
        tool.name = name;
        tool.writes = std::string_view{name} == "mcp__srv__write";
        tool.run = [](std::string_view) { return apogee::agent::ToolOutcome{"", false}; };
        registry.add(std::move(tool));
    }
    ServeSummaryInput input;
    input.served = {"a", "b"};
    input.default_backend = "a";
    input.tools = &registry;
    // The config's word for a server's tool, by its namespaced name.
    input.permission = [](const apogee::agent::GateRequest& request) {
        return request.tool == "mcp__srv__write" ? apogee::agent::Permission::Allow
                                                 : apogee::agent::Permission::Ask;
    };
    const std::string line = serve_summary(input);
    CAPTURE(line);
    CHECK_THAT(line, ContainsSubstring("serving a, b (default a) -- tools: 3 (MCP: docs, srv)"));
    CHECK_THAT(line, ContainsSubstring("-- runs unasked: mcp__srv__write"));

    ServeSummaryInput none;
    none.served = {"a"};
    none.default_backend = "a";
    none.rag.collection = "notes";
    none.rag.source = apogee::commands::RagSource::Config;
    none.session_ttl_minutes = 0;
    const std::string bare = serve_summary(none);
    CHECK(bare ==
          "serving a (default a) -- tools: none -- rag: notes (auto_rag) -- sessions never idle "
          "out");
}

TEST_CASE("serve's --tools help names the real set, never fetch_url alone",
          "[cli][serve][tools][help]") {
    const apogee::testing::CliHome home{
        "backends:\n  mock:\n    type: mock\n    model: mock-1\nmodels:\n  default: mock\n"};
    std::string out;
    std::string err;
    REQUIRE(home.run({"serve", "--help"}, &out, &err) == 0);
    CAPTURE(out);
    CHECK(out.find("(fetch_url)") == std::string::npos);
    CHECK_THAT(out, ContainsSubstring("tools.fs_root"));
    CHECK_THAT(out, ContainsSubstring("allow"));
    // The website tools as http-api.md and the startup line name them.
    CHECK_THAT(out, ContainsSubstring("tools.allowed_hosts"));
    CHECK_THAT(out, ContainsSubstring("web_search"));
    // --allow-remote is also the switch for a reverse proxy in front.
    CHECK_THAT(out, ContainsSubstring("proxy"));
}
