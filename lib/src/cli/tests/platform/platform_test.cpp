#include "platform/platform.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

namespace {

// The exact six names shared by `cicd.sh --platform`, the CMake presets, the
// CI matrix, and the binary. This test is what keeps them one vocabulary --
// drift here means a preset the script cannot select, or a CI leg that builds
// something other than what it claims.
constexpr std::array<std::string_view, 6> kReleaseTargets{
    "linux-x64", "linux-arm64", "macos-x64", "macos-arm64", "windows-x64", "windows-arm64",
};

}  // namespace

TEST_CASE("host_target is one of the six release targets", "[platform]") {
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
