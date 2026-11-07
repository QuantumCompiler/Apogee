#include "tools/environment.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "agent/fetch_url.h"
#include "agent/tool.h"
#include "agent/web_search.h"
#include "platform/child_process.h"
#include "support/env_guard.h"
#include "tools/toolsets.h"

/// The environment note (25d): what it says, and that the one place tools
/// are registered is the one place it is set.
namespace {

using apogee::platform::Architecture;
using apogee::platform::LocalDate;
using apogee::platform::OperatingSystem;
using apogee::tools::Environment;
using apogee::tools::render_environment_note;
using apogee::tools::render_tool_use_policy;
using apogee::tools::ToolReach;

LocalDate monday() {
    LocalDate date;
    date.year = 2026;
    date.month = 9;
    date.day = 28;
    date.weekday = 1;
    date.zone = "MDT";
    date.utc_offset_minutes = -6 * 60;
    return date;
}

}  // namespace

TEST_CASE("the environment note says the date, the zone, the system and the folders",
          "[tools][environment]") {
    Environment environment;
    environment.operating_system = OperatingSystem::MacOS;
    environment.architecture = Architecture::Arm64;
    environment.working_directory = "/work/project";
    environment.shell = "/bin/sh";
    environment.fs_root = "/work/project";

    CHECK(render_environment_note(environment, monday()) ==
          "Environment:\n"
          "- Today is Monday, 2026-09-28 (time zone MDT, UTC-06:00).\n"
          "- Operating system: macOS on arm64.\n"
          "- Working directory: /work/project. Commands run there, through /bin/sh.\n"
          "- The file tools reach /work/project and everything under it; a relative path "
          "starts there.");

    SECTION("a toolset switched off drops its line, and nothing else") {
        environment.shell.clear();
        environment.fs_root.clear();
        CHECK(render_environment_note(environment, monday()) ==
              "Environment:\n"
              "- Today is Monday, 2026-09-28 (time zone MDT, UTC-06:00).\n"
              "- Operating system: macOS on arm64.\n"
              "- Working directory: /work/project.");
    }
    SECTION("offsets east, half hours, and a zone the system does not name") {
        LocalDate kolkata = monday();
        kolkata.zone.clear();
        kolkata.utc_offset_minutes = 5 * 60 + 30;
        kolkata.weekday = 0;
        kolkata.day = 4;
        kolkata.month = 1;
        const std::string note = render_environment_note(environment, kolkata);
        CHECK(note.find("- Today is Sunday, 2026-01-04 (time zone UTC+05:30).") !=
              std::string::npos);
        LocalDate utc = monday();
        utc.zone = "UTC";
        utc.utc_offset_minutes = 0;
        CHECK(render_environment_note(environment, utc).find("(time zone UTC, UTC+00:00)") !=
              std::string::npos);
    }
    SECTION("no time of day: two moments on one day render one note") {
        environment.operating_system = OperatingSystem::Linux;
        environment.architecture = Architecture::X64;
        CHECK(render_environment_note(environment, monday()) ==
              render_environment_note(environment, monday()));
        CHECK(render_environment_note(environment, monday()).find("Linux on x64") !=
              std::string::npos);
    }
}

TEST_CASE("the native toolsets set the note on the registry they fill, whatever is switched off",
          "[tools][environment]") {
    const apogee::testing::TempDir temp{"tools-env-" + std::to_string(std::random_device{}())};
    apogee::tools::ToolsetOptions options;
    options.fs_root = temp.path() / "files";
    options.working_directory = temp.path();
    options.notes_dir = temp.path() / "notes";
    options.disabled = {"rag"};
    options.child_processes = true;  // a shell to name, on any host (57k)

    apogee::agent::ToolRegistry all;
    apogee::tools::register_native_toolsets(all, options);
    const std::string note = all.environment();
    CHECK(note.starts_with("Environment:\n- Today is "));
    CHECK(note.find("- Working directory: " + temp.path().string() + ". Commands run there") !=
          std::string::npos);
    CHECK(note.find("- The file tools reach " + (temp.path() / "files").string()) !=
          std::string::npos);

    options.disabled = {"fs", "shell", "git", "notes", "rag", "graph"};
    apogee::agent::ToolRegistry none;
    apogee::tools::register_native_toolsets(none, options);
    CHECK(none.empty());
    // Still set: fetch_url and MCP tools ride the same registry.
    CHECK(none.environment().starts_with("Environment:\n- Today is "));
    CHECK(none.environment().find("Commands run there") == std::string::npos);
    CHECK(none.environment().find("file tools") == std::string::npos);

    // A registry nobody set a note on renders nothing.
    CHECK(apogee::agent::ToolRegistry{}.environment().empty());
}

TEST_CASE("where no program can be started, no shell or git tool is offered and none is claimed",
          "[tools][environment][57k]") {
    // A model is never offered a tool that can only fail (25e): on a build
    // that cannot start a program -- Windows, today -- run_command and the
    // git tools were registered, advertised ("Commands run there, through
    // cmd") and then refused every call with advice meant for a vendor CLI.
    const apogee::testing::TempDir temp{"tools-env-procs-" +
                                        std::to_string(std::random_device{}())};
    apogee::tools::ToolsetOptions options;
    options.fs_root = temp.path();
    options.working_directory = temp.path();
    options.notes_dir = temp.path() / "notes";
    options.disabled = {"rag"};
    const auto git_tools = [](const apogee::agent::ToolRegistry& registry) {
        std::vector<std::string> found;
        for (const std::string& name : registry.names()) {
            if (name.starts_with("git_")) {
                found.push_back(name);
            }
        }
        return found;
    };

    options.child_processes = false;
    apogee::agent::ToolRegistry without;
    apogee::tools::register_native_toolsets(without, options);
    CHECK(without.find("run_command") == nullptr);
    CHECK(git_tools(without).empty());
    CHECK(without.environment().find("Commands run there") == std::string::npos);
    // Everything else is there, and the names stay toolsets a pin or
    // `tools.disabled` may name.
    CHECK(without.find("read_file") != nullptr);
    CHECK(without.find("write_note") != nullptr);
    CHECK(apogee::tools::toolset_of("run_command") == "shell");
    CHECK(apogee::tools::toolset_of("git_status") == "git");
    const std::vector<std::string> names = apogee::tools::native_tool_names(options);
    CHECK(std::ranges::find(names, "run_command") == names.end());

    options.child_processes = true;
    apogee::agent::ToolRegistry with;
    apogee::tools::register_native_toolsets(with, options);
    CHECK(with.find("run_command") != nullptr);
    CHECK_FALSE(git_tools(with).empty());
    CHECK(with.environment().find("Commands run there, through ") != std::string::npos);

    // The default is the platform's own answer.
    CHECK(apogee::tools::ToolsetOptions{}.child_processes ==
          apogee::platform::supports_child_processes());
}

TEST_CASE("a shell or git tool this build never offers is named as such, and nothing else is",
          "[tools][environment][57k]") {
    // What `--allow run_command`, `/deny git_log` or a task's grant is told on
    // a build that cannot start a program: why the tool is not here -- not
    // that the name is no tool, which reads as a typo.
    const std::string shell = apogee::tools::why_not_offered("run_command", false);
    CHECK(shell.find("'run_command' is a shell tool") != std::string::npos);
    CHECK(shell.find("cannot start other programs") != std::string::npos);
    CHECK(apogee::tools::why_not_offered("git_status", false).find("'git_status' is a git tool") !=
          std::string::npos);
    // Every other name, and every name where programs start, is no concern
    // of it: a typo, a file tool, a website.
    for (const std::string_view other :
         {"read_file", "write_file", "fetch_url", "mcp__srv__x", "wrte_file", "example.org"}) {
        INFO(other);
        CHECK(apogee::tools::why_not_offered(other, false).empty());
    }
    CHECK(apogee::tools::why_not_offered("run_command", true).empty());
    CHECK(apogee::tools::why_not_offered("git_log", true).empty());
    // By default, the platform's answer.
    CHECK(apogee::tools::why_not_offered("run_command").empty() ==
          apogee::platform::supports_child_processes());
}

TEST_CASE("the tool-use policy says only what the tools can reach, and nothing without them",
          "[tools][environment][policy]") {
    // Search and a reader: the whole paragraph.
    CHECK(render_tool_use_policy(ToolReach{.search = true, .read_pages = true}) ==
          "How to use these tools: when a question turns on something current, recent or beyond "
          "what you can know -- the weather, news, prices, scores, schedules, what a web page "
          "says now -- search the web and read the pages you find before you answer, rather "
          "than answering from memory or saying you cannot. Never say you lack access to "
          "information one of your tools can get. When you already know the answer, just "
          "answer.");
    // A search with no reader promises no page.
    CHECK(render_tool_use_policy(ToolReach{.search = true, .read_pages = false}) ==
          "How to use these tools: when a question turns on something current, recent or beyond "
          "what you can know -- the weather, news, prices, scores, schedules -- search the web "
          "before you answer, rather than answering from memory or saying you cannot. Never say "
          "you lack access to information one of your tools can get. When you already know the "
          "answer, just answer.");
    // A reader with no search promises no search: "I can't check that" stays
    // the honest answer to a weather question.
    const std::string reading = render_tool_use_policy(ToolReach{.read_pages = true});
    CHECK(reading ==
          "How to use these tools: when a question turns on what a web page says now -- a link "
          "the user gives you, or a page whose address you know -- read the page before you "
          "answer, rather than answering from memory or saying you cannot. Never say you lack "
          "access to information one of your tools can get. When you already know the answer, "
          "just answer.");
    CHECK(reading.find("search") == std::string::npos);
    // Neither: no paragraph at all.
    CHECK(render_tool_use_policy(ToolReach{}).empty());
    // One paragraph, never pressure on the note the budget never trims (26c).
    CHECK(render_tool_use_policy(ToolReach{.search = true, .read_pages = true}).size() < 512);
}

TEST_CASE("the note carries the policy for the tools its registry holds, read when it is asked",
          "[tools][environment][policy]") {
    const apogee::testing::TempDir temp{"tools-policy-" + std::to_string(std::random_device{}())};
    apogee::tools::ToolsetOptions options;
    options.fs_root = temp.path();
    options.working_directory = temp.path();
    options.notes_dir = temp.path() / "notes";
    options.disabled = {"rag"};
    apogee::agent::ToolRegistry registry;
    apogee::tools::register_native_toolsets(registry, options);

    // The native toolsets reach nothing on the web: the note stands alone.
    CHECK(apogee::tools::tool_reach(registry).search == false);
    CHECK(registry.environment().find("How to use these tools") == std::string::npos);
    CHECK_FALSE(registry.environment().ends_with("\n"));

    // A reader registered after the note was set is still read: the policy
    // is composed when the note is asked for, from the registry asking.
    registry.add(apogee::agent::make_fetch_url_tool(
        [](std::string_view) { return apogee::agent::FetchResult{}; }));
    const std::string reading = registry.environment();
    CHECK(reading.ends_with("\n\n" + render_tool_use_policy(ToolReach{.read_pages = true})));

    registry.add(apogee::agent::make_web_search_tool(
        [](const apogee::agent::SearchRequest&) { return apogee::agent::SearchResponse{}; },
        "127.0.0.1", 5));
    const std::string full = registry.environment();
    CHECK(full.starts_with("Environment:\n- Today is "));
    CHECK(full.ends_with("\n\n" +
                         render_tool_use_policy(ToolReach{.search = true, .read_pages = true})));
    // Asked again, the same bytes: what a local model's cached prompt needs.
    CHECK(registry.environment() == full);
}
