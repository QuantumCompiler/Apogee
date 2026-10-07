#include "operations/suites.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/config.h"
#include "symphony/definition.h"

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

TEST_CASE("a consultable member is written only when its provider says it is local and unmetered",
          "[operations][suites][consult]") {
    using apogee::commands::MeteredAnswer;
    using apogee::commands::MeteredProbe;
    using apogee::commands::validate_suite_consult;
    const Config config = two_backends();
    SuiteConfig suite;
    suite.members["chat"] = {.backend = "root"};
    suite.members["utility"] = {.backend = "helper"};
    suite.consultable = {"utility"};
    std::vector<std::string> asked;
    const MeteredProbe local = [&asked](const Config&, std::string_view backend) {
        asked.emplace_back(backend);
        return MeteredAnswer{.metered = false};
    };
    const MeteredProbe billed = [](const Config&, std::string_view) {
        return MeteredAnswer{.metered = true};
    };
    const MeteredProbe unknown = [](const Config&, std::string_view) {
        return MeteredAnswer{.metered = false, .unknown = "no API key"};
    };
    CHECK(validate_suite(config, "s", suite, local).empty());
    // The member's backend is what is asked about, never the role.
    CHECK(asked == std::vector<std::string>{"helper"});
    CHECK(validate_suite(config, "s", suite, billed) ==
          "consultable utility: 'helper' is billed per call -- a consult runs on the model's "
          "initiative, which never spends: only a local, unmetered member can be consulted");
    CHECK(validate_suite_consult(config, suite, unknown) ==
          "consultable utility: whether 'helper' is billed per call cannot be told (no API key) "
          "-- unknown is metered, and only a local, unmetered member can be consulted");
    // No probe tells nothing, and unknown is metered.
    CHECK(validate_suite(config, "s", suite).find("cannot be told") != std::string::npos);
    // Without consultable members, no probe is needed.
    SuiteConfig plain = suite;
    plain.consultable.clear();
    CHECK(validate_suite(config, "s", plain).empty());

    // The shape, as the loader holds it, in the write's words.
    SuiteConfig shaped = suite;
    shaped.consultable = {"chat"};
    CHECK(validate_suite_consult(config, shaped, local).find("the root itself") !=
          std::string::npos);
    shaped.consultable = {"embedding"};
    CHECK(validate_suite_consult(config, shaped, local).find("not a role a suite can consult") !=
          std::string::npos);
    shaped.consultable = {"vision"};
    CHECK(validate_suite_consult(config, shaped, local) ==
          "consultable vision: the suite has no vision member -- name its backend with --vision");
    shaped.consultable = {"utility", "utility"};
    CHECK(validate_suite_consult(config, shaped, local) == "consultable utility: listed twice");
    shaped.consultable = {"utility"};
    shaped.consult_caps.per_turn = 0;
    CHECK(validate_suite_consult(config, shaped, local) ==
          "consult caps must be positive whole numbers");
}

TEST_CASE("a verifier is written only when its provider says it is local and unmetered",
          "[operations][suites][validate]") {
    using apogee::commands::MeteredAnswer;
    using apogee::commands::MeteredProbe;
    using apogee::commands::validate_suite_validation;
    const Config config = two_backends();
    SuiteConfig suite;
    suite.members["chat"] = {.backend = "root"};
    suite.members["utility"] = {.backend = "helper"};
    std::vector<std::string> asked;
    const MeteredProbe local = [&asked](const Config&, std::string_view backend) {
        asked.emplace_back(backend);
        return MeteredAnswer{.metered = false};
    };
    const MeteredProbe billed = [](const Config&, std::string_view) {
        return MeteredAnswer{.metered = true};
    };
    const MeteredProbe unknown = [](const Config&, std::string_view) {
        return MeteredAnswer{.metered = false, .unknown = "no API key"};
    };
    // No block: nothing to hold, no probe asked.
    CHECK(validate_suite_validation(config, suite, {}).empty());
    suite.validate.tool_args = true;
    CHECK(validate_suite(config, "s", suite, local).empty());
    // The default verifier's backend -- the utility member's -- is asked.
    CHECK(asked == std::vector<std::string>{"helper"});
    CHECK(validate_suite(config, "s", suite, billed) ==
          "validate: 'helper' is billed per call -- a check runs on Apogee's initiative, which "
          "never spends: only a local, unmetered member can be the verifier");
    CHECK(validate_suite_validation(config, suite, unknown) ==
          "validate: whether 'helper' is billed per call cannot be told (no API key) -- unknown "
          "is metered, and only a local, unmetered member can check");
    CHECK(validate_suite(config, "s", suite).find("cannot be told") != std::string::npos);

    // The shape, in the write's words.
    SuiteConfig shaped = suite;
    shaped.validate.verifier = "chat";
    CHECK(validate_suite_validation(config, shaped, local).find("the root itself") !=
          std::string::npos);
    shaped.validate.verifier = "vision";
    CHECK(validate_suite_validation(config, shaped, local) ==
          "validate: the verifier is the vision member, and the suite has none -- name its "
          "backend with --vision, or another verifier with --verifier");
    shaped.validate.verifier.reset();
    shaped.validate.answers = "sometimes";
    CHECK(validate_suite_validation(config, shaped, local) ==
          "validate: answers is request, always, not 'sometimes'");
    // Removing the verifier's member is refused while the block stands.
    SuiteConfig removed = suite;
    removed.members.erase("utility");
    CHECK(validate_suite(config, "s", removed, local).find("the verifier is the utility member") !=
          std::string::npos);
}

TEST_CASE("orchestrate is written only when every member a symphony reaches is local and unmetered",
          "[operations][suites][orchestrate]") {
    using apogee::commands::MeteredAnswer;
    using apogee::commands::MeteredProbe;
    using apogee::commands::validate_suite_orchestrate;
    const Config config = apogee::harness::parse_config(
        "models:\n  default: root\nbackends:\n  root:\n    type: mock\n  helper:\n    type: mock\n"
        "  paid:\n    type: mock\n  eyes:\n    type: mock\n",
        "<test>");
    // The starters, compiled in: extract-facts (extraction), summarize-verify
    // (utility, chat) and describe-answer (vision, chat) -- that one takes an
    // image, so it is never offered and never asked about.
    const apogee::symphony::Catalog symphonies =
        apogee::symphony::catalog(config, "/nonexistent/symphonies");
    SuiteConfig suite;
    suite.members["chat"] = {.backend = "root"};
    suite.members["utility"] = {.backend = "helper"};
    suite.members["vision"] = {.backend = "eyes"};
    suite.orchestrate = true;
    std::vector<std::string> asked;
    const MeteredProbe paid_billed = [&asked](const Config&, std::string_view backend) {
        asked.emplace_back(backend);
        return MeteredAnswer{.metered = backend == "paid" || backend == "eyes"};
    };
    CHECK(validate_suite(config, "s", suite, paid_billed, &symphonies).empty());
    // Each backend asked once, the one a role resolves to -- extraction, with
    // no member, falls to the conversation -- and never the vision member,
    // which nothing offered reaches.
    std::ranges::sort(asked);
    CHECK(asked == std::vector<std::string>{"helper", "root"});

    // A billed member a symphony reaches: refused, naming the symphony, its
    // stage, the role and the backend.
    SuiteConfig billed = suite;
    billed.members["utility"] = {.backend = "paid"};
    CHECK(validate_suite(config, "s", billed, paid_billed, &symphonies) ==
          "orchestrate: 'summarize-verify' reaches utility ('paid') through its summarize stage "
          "(utility), and 'paid' is billed per call -- a play the model starts runs on its "
          "initiative, which never spends: only a suite whose symphonies reach local, unmetered "
          "members can orchestrate");
    // Off is always writable; the same suite without orchestration passes.
    billed.orchestrate = false;
    CHECK(validate_suite(config, "s", billed, paid_billed, &symphonies).empty());
    // Unknown is metered, and no catalog is unknown.
    const MeteredProbe unknown = [](const Config&, std::string_view) {
        return MeteredAnswer{.metered = false, .unknown = "no API key"};
    };
    CHECK(validate_suite_orchestrate(config, "s", suite, unknown, &symphonies)
              .find("cannot be told (no API key) -- unknown is metered") != std::string::npos);
    CHECK(validate_suite_orchestrate(config, "s", suite, paid_billed, nullptr)
              .starts_with("orchestrate: the symphonies cannot be read here"));
    CHECK(validate_suite(config, "s", suite).starts_with("orchestrate: "));
}
