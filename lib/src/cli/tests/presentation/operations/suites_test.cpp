#include "operations/suites.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "contracts/config.h"

/// The suite rules both surfaces share (27d): what a write may say, what a
/// run under the active suite may rely on, and what may not be deleted.
namespace {

using apogee::commands::validate_active_suite;
using apogee::commands::validate_suite;
using apogee::commands::validate_suite_delete;
using apogee::commands::validate_suite_member;
using apogee::harness::Config;
using apogee::harness::SuiteConfig;
using apogee::harness::SuiteMember;

Config two_backends() {
    return apogee::harness::parse_config(
        "models:\n  default: root\nbackends:\n  root:\n    type: mock\n  helper:\n    type: mock\n"
        "suites:\n  research:\n    members:\n      chat: root\n      utility: ghost\n"
        "  fine:\n    members:\n      utility: helper\n",
        "<test>");
}

}  // namespace

TEST_CASE("a suite is written only with members that name configured backends",
          "[operations][suites]") {
    const Config config = two_backends();
    SuiteConfig suite;
    CHECK(validate_suite(config, "s", suite).find("names at least one member") !=
          std::string::npos);
    suite.members["chat"] = {.backend = "root"};
    CHECK(validate_suite(config, "s", suite).empty());
    CHECK(validate_suite(config, "", suite) == "a suite needs a name");
    CHECK(validate_suite(config, "OFF", suite).find("is reserved") != std::string::npos);
    suite.members["utility"] = {.backend = "ghost"};
    CHECK(validate_suite(config, "s", suite) ==
          "utility: no backend named 'ghost' in this config (known backends: helper, root)");

    CHECK(validate_suite_member(config, "root", {.backend = "root"}).find("is not a role") !=
          std::string::npos);
    CHECK(validate_suite_member(config, "chat", {.backend = "  "}) == "chat: names no backend");
    CHECK(validate_suite_member(config, "chat", {.backend = "root", .context_size = 0}) ==
          "chat: context_size must be a positive number of tokens");
    CHECK(validate_suite_member(
              config, "chat",
              SuiteMember{.backend = "root", .toolset = std::vector<std::string>{"browser"}})
              .find("'browser' is not a toolset") != std::string::npos);
}

TEST_CASE("a run under a suite whose member names nothing is refused in the existing words",
          "[operations][suites]") {
    Config config = two_backends();
    // No suite active: nothing to refuse.
    CHECK(validate_active_suite(config).empty());
    config.models.default_suite = "fine";
    CHECK(validate_active_suite(config).empty());
    config.models.default_suite = "research";
    CHECK(validate_active_suite(config) ==
          "no backend named 'ghost' (configured: helper, root) -- suite research's utility "
          "member; fix it with: apogee config set-suite research --utility <backend>");
}

TEST_CASE("the default suite cannot be deleted from under the config", "[operations][suites]") {
    Config config = two_backends();
    CHECK(validate_suite_delete(config, "research").empty());
    config.models.default_suite = "research";
    CHECK(validate_suite_delete(config, "Research").find("is the default suite") !=
          std::string::npos);
    CHECK(validate_suite_delete(config, "fine").empty());
    CHECK(validate_suite_delete(config, "nope").empty());
}
