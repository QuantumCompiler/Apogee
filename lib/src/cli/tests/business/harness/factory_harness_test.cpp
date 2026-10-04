#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <random>
#include <string>

#include "backends/factory.h"
#include "contracts/config.h"
#include "harness/harness.h"
#include "secrets/store.h"
#include "support/env_guard.h"

/// The provider factory filling a real Harness: the registrations, the router
/// it installs, the capability the Harness discovers. Moved from the
/// factory's own suite (A4): the Harness is Business and the factory Data, so
/// a test of the two together lives in the higher layer (ADR 0004).
using apogee::backends::build_providers;
using apogee::harness::BackendConfig;
using apogee::harness::BackendType;
using apogee::harness::Config;
using apogee::harness::Harness;

namespace {

Config config_from(std::string_view yaml) {
    return apogee::harness::parse_config(yaml, "<test>");
}

/// No environment: the developer's own ANTHROPIC_API_KEY must not turn a
/// "no key" case into a built provider.
apogee::backends::BuildOptions no_env() {
    static const apogee::secrets::EnvSnapshot empty;
    apogee::backends::BuildOptions options;
    options.env = &empty;
    return options;
}

}  // namespace

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

    const auto result = build_providers(harness, no_env());

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

TEST_CASE("embedding is a per-entry capability the harness discovers, and Anthropic has none",
          "[backends][factory][embed][capability]") {
    // The Core constraint, asserted against BUILT providers rather than hand-
    // made ones: which entries can embed is answered by the objects the factory
    // produced, never by a list of types. Anthropic has no embeddings endpoint
    // and must say so; OpenAI and Google do and must say so too.
    apogee::harness::Config config;
    apogee::harness::BackendConfig anthropic;
    anthropic.type = apogee::harness::BackendType::Anthropic;
    anthropic.api_key = "sk-ant-test";
    apogee::harness::BackendConfig openai;
    openai.type = apogee::harness::BackendType::OpenAI;
    openai.api_key = "sk-test";
    apogee::harness::BackendConfig google;
    google.type = apogee::harness::BackendType::Google;
    google.api_key = "AIza-test";
    apogee::harness::BackendConfig mock;
    mock.type = apogee::harness::BackendType::Mock;
    config.backends.emplace("claude", anthropic);
    config.backends.emplace("gpt", openai);
    config.backends.emplace("gemini", google);
    config.backends.emplace("mock", mock);

    apogee::harness::Harness harness{config};
    const auto built = apogee::backends::build_providers(harness, {});
    REQUIRE(built.constructed_count() == 4);

    CHECK_FALSE(harness.can_embed("claude"));
    CHECK_FALSE(harness.can_embed("mock"));
    CHECK(harness.can_embed("gpt"));
    CHECK(harness.can_embed("gemini"));

    // Metered generation is stated by the BUILT providers too: every
    // API-billing vendor answers yes, the mock no, and nothing here names a
    // type. (The local backend answers no as well; it needs the llama build.)
    CHECK(harness.generation_is_metered("claude"));
    CHECK(harness.generation_is_metered("gpt"));
    CHECK(harness.generation_is_metered("gemini"));
    CHECK_FALSE(harness.generation_is_metered("mock"));
}
