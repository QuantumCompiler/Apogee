#include "tools/environment.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <random>
#include <string>

#include "agent/tool.h"
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

    apogee::agent::ToolRegistry all;
    apogee::tools::register_native_toolsets(all, options);
    const std::string note = all.environment();
    CHECK(note.starts_with("Environment:\n- Today is "));
    CHECK(note.find("- Working directory: " + temp.path().string() + ". Commands run there") !=
          std::string::npos);
    CHECK(note.find("- The file tools reach " + (temp.path() / "files").string()) !=
          std::string::npos);

    options.disabled = {"fs", "shell", "git", "notes", "rag"};
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
