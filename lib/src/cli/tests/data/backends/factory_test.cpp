#include "backends/factory.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <random>
#include <string>

#include "contracts/config.h"
#include "secrets/store.h"
#include "support/env_guard.h"

using apogee::backends::build_providers;
using apogee::backends::make_provider;
using apogee::harness::BackendConfig;
using apogee::harness::BackendType;

namespace {

/// No environment: the developer's own ANTHROPIC_API_KEY must not turn a
/// "no key" case into a built provider.
apogee::backends::BuildOptions no_env() {
    static const apogee::secrets::EnvSnapshot empty;
    apogee::backends::BuildOptions options;
    options.env = &empty;
    return options;
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
    CHECK(make_provider("claude", config, reason, no_env()) == nullptr);
    CHECK(reason.find("api_key") != std::string::npos);
    CHECK(reason.find("apogee auth add anthropic") != std::string::npos);
}

TEST_CASE("an unimplemented backend type names itself in the reason", "[backends][factory]") {
    // The message a user sees when they configure a backend that has not
    // landed. "could not be constructed" would send them to check their key.
    // Every backend type in v0.1.0 has now landed, so nothing reaches this
    // message any more -- it survives for the NEXT unimplemented type, and this
    // case pins that the phrase is not accidentally reachable today.
    for (const auto type : {BackendType::Mock, BackendType::Anthropic, BackendType::OpenAI,
                            BackendType::Google, BackendType::LlamaCpp}) {
        BackendConfig config;
        config.type = type;
        std::string reason;
        (void)make_provider("x", config, reason);
        CHECK(reason.find("has not landed yet") == std::string::npos);
    }
}

TEST_CASE("a local backend with no model_path says which key to set", "[backends][factory]") {
    // The llamacpp equivalent of the missing-key message: name the setting, not
    // the failure. "could not be constructed" sends the user nowhere.
    BackendConfig config;
    config.type = BackendType::LlamaCpp;

    std::string reason;
    CHECK(make_provider("local", config, reason) == nullptr);
    CHECK(reason.find("model_path") != std::string::npos);
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
        CHECK(make_provider("x", config, reason, no_env()) == nullptr);
        CHECK(reason.find(c.expected) != std::string::npos);
    }
}

TEST_CASE("the factory resolves through the store and the environment, config first",
          "[backends][factory][secrets]") {
    // The one chain, seen from the factory: a keyless entry builds from the
    // store beside the config, or from the snapshot it was handed, and a
    // config key wins over both. The provider never knows which rung answered.
    const apogee::testing::TempDir home{"factory-secrets-" +
                                        std::to_string(std::random_device{}())};
    const std::filesystem::path config_path = home.path() / "config.yaml";
    apogee::secrets::CredentialStore store{apogee::secrets::credentials_path(config_path)};
    const apogee::secrets::EnvSnapshot env = apogee::secrets::EnvSnapshot::capture(
        [](std::string_view name) { return name == "OPENAI_API_KEY" ? "env-key" : ""; });

    BackendConfig config;
    config.type = BackendType::OpenAI;
    std::string reason;

    // Store only.
    store.put("openai", "store-key");
    apogee::backends::BuildOptions from_store = no_env();
    from_store.config_path = config_path;
    CHECK(make_provider("gpt", config, reason, from_store) != nullptr);
    CHECK(reason.empty());
    // No store path: the store is not consulted, and an empty snapshot has
    // nothing -- so this is the "no key" case again.
    CHECK(make_provider("gpt", config, reason, no_env()) == nullptr);
    CHECK(reason.find("OPENAI_API_KEY") != std::string::npos);
    // Environment only.
    apogee::backends::BuildOptions from_env;
    from_env.env = &env;
    reason.clear();
    CHECK(make_provider("gpt", config, reason, from_env) != nullptr);
    CHECK(reason.empty());
    // A different vendor's slot answers nothing for this one.
    BackendConfig other;
    other.type = BackendType::Anthropic;
    CHECK(make_provider("claude", other, reason, from_store) == nullptr);
}
