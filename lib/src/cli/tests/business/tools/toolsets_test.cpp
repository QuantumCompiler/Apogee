#include "tools/toolsets.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "agent/tool.h"
#include "contracts/config.h"
#include "support/env_guard.h"

/// The words a suite's `toolset:` pins (27d), held to the toolsets that
/// exist: every native toolset is one, and every registered tool files under
/// one -- read off the registration itself, so a tool added to a set is pinned
/// with it.
namespace {

using apogee::tools::toolset_of;

}  // namespace

TEST_CASE("every native toolset is a word a suite can pin", "[tools][toolsets][suites]") {
    const auto words = apogee::harness::suite_toolset_names();
    for (const std::string_view toolset : apogee::tools::toolset_names()) {
        INFO(toolset);
        CHECK(std::ranges::find(words, toolset) != words.end());
    }
}

TEST_CASE("each registered tool files under its toolset", "[tools][toolsets][suites]") {
    const auto words = apogee::harness::suite_toolset_names();
    for (const std::string_view toolset : apogee::tools::toolset_names()) {
        apogee::tools::ToolsetOptions options;
        options.child_processes = true;  // every set, on any host (57k)
        for (const std::string_view other : apogee::tools::toolset_names()) {
            if (other != toolset) {
                options.disabled.emplace_back(other);
            }
        }
        const std::vector<std::string> names = apogee::tools::native_tool_names(options);
        REQUIRE_FALSE(names.empty());
        for (const std::string& name : names) {
            INFO(name);
            CHECK(toolset_of(name) == toolset);
        }
    }
    CHECK(toolset_of("read_file") == "fs");
    CHECK(toolset_of("run_command") == "shell");
    CHECK(toolset_of("git_diff") == "git");
    CHECK(toolset_of("fetch_url") == "web");
    CHECK(toolset_of("web_search") == "web");
    CHECK(toolset_of("mcp__tickets__create_ticket") == "mcp");
    // A tool no word names is in no toolset: a pin never offers it.
    CHECK(toolset_of("ask_user").empty());
    CHECK(toolset_of("no_such_tool").empty());
    for (const std::string_view word : {"web", "mcp"}) {
        CHECK(std::ranges::find(words, word) != words.end());
    }
}

TEST_CASE("the git tools are bounded by the file tools' root", "[tools][toolsets][git][57o]") {
    // 57o: the registration handed git the working folder alone, so a
    // `repo` anywhere on the machine was read. It now hands the effective
    // fs_root too, and a repository named outside it is refused in the
    // sandbox's words -- before any git starts, so on any host.
    const apogee::testing::TempDir temp{"toolsets-git-" + std::to_string(std::random_device{}())};
    std::filesystem::create_directories(temp.path() / "root" / "sub");
    apogee::agent::ToolRegistry registry;
    apogee::tools::ToolsetOptions options;
    options.fs_root = temp.path() / "root";
    options.working_directory = temp.path() / "root" / "sub";
    options.disabled = {"rag", "graph"};
    options.child_processes = true;
    apogee::tools::register_native_toolsets(registry, options);
    const apogee::agent::Tool* show = registry.find("git_show");
    REQUIRE(show != nullptr);
    const apogee::agent::ToolOutcome outside =
        show->run(R"({"repo":")" + (temp.path() / "elsewhere").generic_string() + R"("})");
    CHECK(outside.is_error);
    CHECK(outside.content.find("outside the allowed root") != std::string::npos);
    // A relative `repo` is read against the root, as a file tool's path is.
    CHECK(registry.find("git_status")
              ->run(R"({"repo":".."})")
              .content.find("outside the allowed root") != std::string::npos);
}
