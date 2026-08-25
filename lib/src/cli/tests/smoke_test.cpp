// Skeleton smoke tests: the build is wired correctly and its dependencies are
// reachable. Thin on purpose -- these exist so that "the toolchain works" is a
// claim the suite makes rather than one a contributor assumes.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <string>

#include "platform/platform.h"
#include "version/version.h"

using Catch::Matchers::ContainsSubstring;

TEST_CASE("version reports a semantic version stamped in at configure time", "[version]") {
    const std::string semantic{apogee::version::semantic()};

    REQUIRE_FALSE(semantic.empty());
    // Loose on purpose: this asserts the CMake stamping path works, not what
    // the current version number happens to be.
    REQUIRE(semantic.find('.') != std::string::npos);
}

TEST_CASE("version reports a build date and a commit", "[version]") {
    REQUIRE_FALSE(apogee::version::build_date().empty());
    REQUIRE_FALSE(apogee::version::git_commit().empty());
}

TEST_CASE("the full version line names the binary and its target", "[version]") {
    const std::string line = apogee::version::full();

    REQUIRE_THAT(line, ContainsSubstring("apogee "));
    REQUIRE_THAT(line, ContainsSubstring(std::string{apogee::version::semantic()}));
    // The release-target name belongs in --version output: a bug report that
    // does not say which of the six builds it came from costs a round trip.
    REQUIRE_THAT(line, ContainsSubstring(apogee::platform::host_target()));
}

TEST_CASE("nlohmann/json is linked and round-trips", "[dependencies]") {
    const nlohmann::json parsed = nlohmann::json::parse(R"({"model":"claude","tokens":128})");

    REQUIRE(parsed.at("model").get<std::string>() == "claude");
    REQUIRE(parsed.at("tokens").get<int>() == 128);
    REQUIRE(nlohmann::json::parse(parsed.dump()) == parsed);
}
