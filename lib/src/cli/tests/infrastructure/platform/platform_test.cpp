#include "platform/platform.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#include "support/env_guard.h"

namespace {

// The exact six names shared by `cicd.sh --platform`, the CMake presets, the
// CI matrix, and the binary. This test is what keeps them one vocabulary --
// drift here means a preset the script cannot select, or a CI leg that builds
// something other than what it claims.
constexpr std::array<std::string_view, 6> kReleaseTargets{
    "linux-x64", "linux-arm64", "macos-arm64", "windows-x64", "windows-arm64",
};

}  // namespace

TEST_CASE("host_target is one of the five release targets", "[platform]") {
    const std::string target = apogee::platform::host_target();

    const bool known = std::ranges::find(kReleaseTargets, target) != kReleaseTargets.end();
    INFO("host_target() returned: " << target);
    REQUIRE(known);
}

TEST_CASE("host_target is the OS and architecture joined by a hyphen", "[platform]") {
    const std::string expected =
        std::string{apogee::platform::to_string(apogee::platform::host_os())} + "-" +
        std::string{apogee::platform::to_string(apogee::platform::host_architecture())};

    REQUIRE(apogee::platform::host_target() == expected);
}

TEST_CASE("every OS and architecture has a name", "[platform]") {
    using apogee::platform::Architecture;
    using apogee::platform::OperatingSystem;

    REQUIRE(apogee::platform::to_string(OperatingSystem::Linux) == "linux");
    REQUIRE(apogee::platform::to_string(OperatingSystem::MacOS) == "macos");
    REQUIRE(apogee::platform::to_string(OperatingSystem::Windows) == "windows");

    REQUIRE(apogee::platform::to_string(Architecture::X64) == "x64");
    REQUIRE(apogee::platform::to_string(Architecture::Arm64) == "arm64");
}

TEST_CASE("host detection is stable across calls", "[platform]") {
    REQUIRE(apogee::platform::host_os() == apogee::platform::host_os());
    REQUIRE(apogee::platform::host_architecture() == apogee::platform::host_architecture());
    REQUIRE(apogee::platform::host_target() == apogee::platform::host_target());
}

TEST_CASE("a process is running while it runs, and an id nothing holds is not", "[platform]") {
    // What tells a live `models convert`'s staging directory from one an
    // interrupted run left behind.
    CHECK(apogee::platform::process_running(apogee::platform::current_process_id()));
    CHECK_FALSE(apogee::platform::process_running(0));
    CHECK_FALSE(apogee::platform::process_running(-1));
    CHECK_FALSE(apogee::platform::process_running(999999999));
}

TEST_CASE("local_date is the calendar day at the offset it reports", "[platform][environment]") {
    // Checked against the standard calendar rather than a fixed zone: the
    // day, the month, the year and the weekday must all be the UTC moment
    // moved by the offset the same call reported.
    using namespace std::chrono;
    for (const system_clock::time_point when :
         {system_clock::time_point{}, system_clock::time_point{seconds{1'790'000'000}},
          system_clock::now()}) {
        const apogee::platform::LocalDate date = apogee::platform::local_date(when);
        const sys_days day = floor<days>(when + minutes{date.utc_offset_minutes});
        const year_month_day calendar{day};
        CHECK(static_cast<int>(calendar.year()) == date.year);
        CHECK(static_cast<unsigned>(calendar.month()) == static_cast<unsigned>(date.month));
        CHECK(static_cast<unsigned>(calendar.day()) == static_cast<unsigned>(date.day));
        CHECK(weekday{day}.c_encoding() == static_cast<unsigned>(date.weekday));
        CHECK(date.utc_offset_minutes >= -12 * 60);
        CHECK(date.utc_offset_minutes <= 14 * 60);
    }
}

TEST_CASE("a standard stream given another buffer is not a terminal", "[platform][terminal]") {
    // What every in-process command test does to feed input and capture
    // output: the stream's buffer swapped. Then it is answered as a pipe,
    // whatever the descriptor is -- so a test run from a developer's terminal
    // neither paints into the captured output nor waits on the real keyboard.
    using apogee::platform::is_terminal;
    using apogee::platform::StandardStream;
    std::istringstream in;
    std::ostringstream out;
    std::ostringstream err;
    std::streambuf* const old_in = std::cin.rdbuf(in.rdbuf());
    std::streambuf* const old_out = std::cout.rdbuf(out.rdbuf());
    std::streambuf* const old_err = std::cerr.rdbuf(err.rdbuf());
    const bool in_terminal = is_terminal(StandardStream::In);
    const bool out_terminal = is_terminal(StandardStream::Out);
    const bool err_terminal = is_terminal(StandardStream::Err);
    std::cin.rdbuf(old_in);
    std::cout.rdbuf(old_out);
    std::cerr.rdbuf(old_err);
    CHECK_FALSE(in_terminal);
    CHECK_FALSE(out_terminal);
    CHECK_FALSE(err_terminal);
}

TEST_CASE("a terminal that cannot place the cursor is refused a full screen, with why",
          "[platform][terminal]") {
    if (apogee::platform::host_os() == apogee::platform::OperatingSystem::Windows) {
        // The console is asked, not TERM: under ctest stdout is no console,
        // and that is said rather than drawn into.
        CHECK_FALSE(apogee::platform::full_screen_refusal().empty());
        return;
    }
    {
        const apogee::testing::EnvGuard term{"TERM", "dumb"};
        CHECK(apogee::platform::full_screen_refusal() ==
              "this terminal (TERM=dumb) cannot place the cursor");
    }
    {
        const apogee::testing::EnvUnsetGuard term{"TERM"};
        CHECK(apogee::platform::full_screen_refusal().find("TERM is not set") == 0);
    }
    {
        const apogee::testing::EnvGuard term{"TERM", "xterm-256color"};
        CHECK(apogee::platform::full_screen_refusal().empty());
    }
}
