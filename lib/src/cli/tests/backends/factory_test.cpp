#include "backends/factory.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "harness/config.h"
#include "harness/harness.h"

using apogee::backends::build_providers;
using apogee::backends::make_provider;
using apogee::harness::BackendConfig;
using apogee::harness::BackendType;
using apogee::harness::Config;
using apogee::harness::Harness;

namespace {

Config config_from(std::string_view yaml) {
    return apogee::harness::parse_config(yaml, "<test>");
}

}  // namespace

TEST_CASE("a mock entry builds", "[backends][factory]") {
    BackendConfig config;
    config.type = BackendType::Mock;
    config.model = "mock-9";

    std::string reason;
    const auto provider = make_provider("m", config, reason);
    REQUIRE(provider != nullptr);
    CHECK(provider->backend_name() == "m");
    CHECK(reason.empty());
}

TEST_CASE("an Anthropic entry with no key is skipped with a reason", "[backends][factory]") {
    BackendConfig config;
    config.type = BackendType::Anthropic;

    std::string reason;
    CHECK(make_provider("claude", config, reason) == nullptr);
    CHECK(reason.find("api_key") != std::string::npos);
}

TEST_CASE("an unimplemented backend type names itself in the reason", "[backends][factory]") {
    // The message a user sees when they configure a backend that has not
    // landed. "could not be constructed" would send them to check their key.
    // Only llamacpp remains; OpenAI and Google shipped 2026-08-26.
    BackendConfig config;
    config.type = BackendType::LlamaCpp;
    std::string reason;
    CHECK(make_provider("x", config, reason) == nullptr);
    CHECK(reason.find("has not landed yet") != std::string::npos);
}

TEST_CASE("every cloud type builds when it has a key", "[backends][factory]") {
    // The three API-billing backends are ordinary config types with no special
    // casing anywhere -- the payoff of the LLMProvider seam.
    struct Case {
        BackendType type;
        const char* name;
    };

    for (const Case& c :
         {Case{BackendType::Anthropic, "anthropic"}, Case{BackendType::OpenAI, "openai"},
          Case{BackendType::Google, "google"}}) {
        INFO(c.name);
        BackendConfig config;
        config.type = c.type;
        config.api_key = "a-key";

        std::string reason;
        const auto provider = make_provider(c.name, config, reason);
        REQUIRE(provider != nullptr);
        CHECK(provider->backend_name() == c.name);
        CHECK(reason.empty());
    }
}

TEST_CASE("a cloud type with no key names ITS OWN environment variable", "[backends][factory]") {
    // Telling an OpenAI user to set ANTHROPIC_API_KEY would be worse than
    // saying nothing at all.
    struct Case {
        BackendType type;
        const char* expected;
    };

    for (const Case& c : {Case{BackendType::Anthropic, "ANTHROPIC_API_KEY"},
                          Case{BackendType::OpenAI, "OPENAI_API_KEY"},
                          Case{BackendType::Google, "GEMINI_API_KEY"}}) {
        INFO(c.expected);
        BackendConfig config;
        config.type = c.type;

        std::string reason;
        CHECK(make_provider("x", config, reason) == nullptr);
        CHECK(reason.find(c.expected) != std::string::npos);
    }
}

TEST_CASE("one unbuildable backend does not stop the others", "[backends][factory]") {
    // A config with an unconfigured cloud entry beside a working one must let
    // the working one run. Otherwise a single missing key takes down every
    // backend the user has.
    Harness harness{config_from(R"(
backends:
  broken:
    type: anthropic
  working:
    type: mock
)")};

    const auto result = build_providers(harness);

    CHECK(result.constructed_count() == 1);
    CHECK(result.statuses.size() == 2);
    CHECK(harness.route("working").backend_name() == "working");
    CHECK(result.skipped_summary().find("broken") != std::string::npos);
}

TEST_CASE("an empty config builds nothing and routes nothing", "[backends][factory]") {
    Harness harness{Config{}};
    const auto result = build_providers(harness);

    CHECK(result.constructed_count() == 0);
    CHECK(result.statuses.empty());
    CHECK(result.skipped_summary().empty());
    // The router still exists -- it just refuses.
    CHECK_THROWS(harness.route("anything"));
}

TEST_CASE("build_providers installs a working router", "[backends][factory]") {
    Harness harness{config_from(R"(
models:
  default: a
backends:
  a:
    type: mock
    model: model-a
  b:
    type: mock
    model: model-b
)")};

    (void)build_providers(harness);

    CHECK(harness.route("a").backend_name() == "a");
    CHECK(harness.route("model-b").backend_name() == "b");
    CHECK(harness.route("").backend_name() == "a");
}
