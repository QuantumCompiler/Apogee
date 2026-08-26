#include "harness/paths.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <stdexcept>

#include "support/env_guard.h"

using apogee::harness::apogee_home;
using apogee::testing::EnvGuard;
using apogee::testing::EnvUnsetGuard;

TEST_CASE("APOGEE_HOME relocates the whole tree", "[paths]") {
    const EnvGuard home{"APOGEE_HOME", "/tmp/apogee-somewhere"};

    CHECK(apogee_home() == std::filesystem::path{"/tmp/apogee-somewhere"});
    CHECK(apogee::harness::config_dir() == std::filesystem::path{"/tmp/apogee-somewhere/config"});
    CHECK(apogee::harness::default_config_path() ==
          std::filesystem::path{"/tmp/apogee-somewhere/config/config.yaml"});
}

TEST_CASE("without the override the tree is ~/.apogee", "[paths]") {
    const EnvUnsetGuard no_override{"APOGEE_HOME"};
    const EnvGuard home{"HOME", "/home/tester"};

#if !defined(_WIN32)
    CHECK(apogee_home() == std::filesystem::path{"/home/tester/.apogee"});
    CHECK(apogee::harness::default_config_path() ==
          std::filesystem::path{"/home/tester/.apogee/config/config.yaml"});
#else
    SUCCEED("HOME is not how Windows reports a home directory");
#endif
}

TEST_CASE("an empty APOGEE_HOME is ignored rather than rooting at the filesystem root", "[paths]") {
    // Treating "" as a valid root would put the config at /config/config.yaml.
    const EnvGuard empty{"APOGEE_HOME", ""};
    const EnvGuard home{"HOME", "/home/tester"};

#if !defined(_WIN32)
    CHECK(apogee_home() == std::filesystem::path{"/home/tester/.apogee"});
#else
    SUCCEED("HOME is not how Windows reports a home directory");
#endif
}

TEST_CASE("no home and no override is an error, not a guess", "[paths]") {
    // The caller is about to write here. A plausible-but-wrong directory is
    // worse than a message naming the fix.
    const EnvUnsetGuard no_override{"APOGEE_HOME"};
    const EnvUnsetGuard no_home{"HOME"};
    const EnvUnsetGuard no_profile{"USERPROFILE"};
    const EnvUnsetGuard no_drive{"HOMEDRIVE"};

    try {
        (void)apogee_home();
        FAIL("expected a runtime_error");
    } catch (const std::runtime_error& e) {
        CHECK(std::string{e.what()}.find("APOGEE_HOME") != std::string::npos);
    }
}

TEST_CASE("an explicit --config wins over the default location", "[paths]") {
    const EnvGuard home{"APOGEE_HOME", "/tmp/apogee-somewhere"};

    CHECK(apogee::harness::resolve_config_path("/elsewhere/mine.yaml") ==
          std::filesystem::path{"/elsewhere/mine.yaml"});
    CHECK(apogee::harness::resolve_config_path("") ==
          std::filesystem::path{"/tmp/apogee-somewhere/config/config.yaml"});
}
