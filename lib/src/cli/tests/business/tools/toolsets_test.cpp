#include "tools/toolsets.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/config.h"

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
