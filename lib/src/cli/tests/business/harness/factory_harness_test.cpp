#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "backends/codex_cli.h"
#include "backends/factory.h"
#include "backends/mock.h"
#include "backends/provider_cache.h"
#include "contracts/config.h"
#include "contracts/errors.h"
#include "harness/harness.h"
#include "secrets/store.h"
#include "support/env_guard.h"
#include "support/fake_child.h"

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

TEST_CASE("a suite switch rebuilds exactly the backends it re-pins",
          "[backends][factory][suites]") {
    // 27d: a window is a construction parameter, so a backend whose pin a
    // switch moves is built again; every other provider is left alone.
    Harness harness{config_from(R"(
models:
  default: root
backends:
  root:
    type: mock
  helper:
    type: mock
suites:
  small:
    members:
      utility:
        backend: helper
        context_size: 2048
)")};
    (void)build_providers(harness, no_env());
    apogee::harness::LLMProvider* root = &harness.provider("root");
    apogee::harness::LLMProvider* helper = &harness.provider("helper");

    harness.set_active_suite("small");
    const auto rebuilt = apogee::backends::rebuild_providers(harness, {"helper"}, no_env());
    REQUIRE(rebuilt.statuses.size() == 1);
    CHECK(rebuilt.statuses.front().constructed);
    CHECK(&harness.provider("root") == root);
    CHECK(&harness.provider("helper") != helper);
    CHECK(harness.route("helper").backend_name() == "helper");

    // A name the config does not have is said, and nothing is registered.
    const auto missing = apogee::backends::rebuild_providers(harness, {"ghost"}, no_env());
    CHECK_FALSE(missing.statuses.front().constructed);
    CHECK(missing.statuses.front().reason == "not configured");
}

TEST_CASE("a pinned model is the Harness's view of its entry: rebuilt to run, restored exactly",
          "[backends][factory][roster]") {
    // 33: `-m` or `/model` naming a vendor roster's model runs it on its
    // owner's entry -- the Harness's config, never the file -- and the
    // entry rebuilt is the one that runs it.
    Harness harness{config_from(R"(
models:
  default: root
backends:
  root:
    type: mock
    model: root-own
  helper:
    type: mock
)")};
    (void)build_providers(harness, no_env());
    const auto runs = [&harness](const std::string& name) {
        return harness.provider(name).list_models({}).front().id;
    };
    REQUIRE(runs("root") == "root-own");
    apogee::harness::LLMProvider* helper = &harness.provider("helper");

    CHECK(harness.pin_model("Root", "mock-pro"));  // the key as the file has it
    CHECK(harness.config().find_backend("root")->model == "mock-pro");
    CHECK(runs("root") == "root-own");  // built before the pin: rebuilt to run it
    REQUIRE(apogee::backends::rebuild_providers(harness, {"root"}, no_env())
                .statuses.front()
                .constructed);
    CHECK(runs("root") == "mock-pro");
    CHECK(harness.route("mock-pro").backend_name() == "root");  // routed by its model too
    CHECK(&harness.provider("helper") == helper);

    // A second pin replaces the first; restoring gives back the file's model,
    // not the pin before.
    CHECK(harness.pin_model("root", "mock-mini"));
    CHECK(harness.pin_model("root", ""));
    CHECK(harness.config().find_backend("root")->model == "root-own");
    (void)apogee::backends::rebuild_providers(harness, {"root"}, no_env());
    CHECK(runs("root") == "root-own");

    // Restoring an entry never pinned changes nothing; a name with no entry
    // is refused.
    CHECK(harness.pin_model("helper", ""));
    CHECK(harness.config().find_backend("helper")->model.empty());
    CHECK_FALSE(harness.pin_model("ghost", "mock-pro"));
}

TEST_CASE("the Codex CLI's catalog is a capability the Harness finds; a mock has none",
          "[backends][codex][catalog]") {
    // 34: providers scan asks every built entry through `catalog_for`; the
    // Codex backend answers it by running its own CLI.
    Harness harness{config_from(R"(
models:
  default: codex
backends:
  codex:
    type: codex-cli
  root:
    type: mock
)")};
    auto spawner = std::make_shared<apogee::testing::FakeSpawner>();
    spawner->stdout_scripts = {R"({"models": [{"slug": "gpt-5.5", "visibility": "list"}]})"};
    apogee::backends::CodexCliProvider::Options options;
    options.backend_name = "codex";
    harness.register_provider(
        "codex", std::make_shared<apogee::backends::CodexCliProvider>(
                     std::move(options),
                     [spawner](const apogee::platform::ChildCommand& command, std::string& error) {
                         return (*spawner)(command, error);
                     }));
    apogee::backends::MockProvider::Options mock_options;
    mock_options.backend_name = "root";
    harness.register_provider(
        "root", std::make_shared<apogee::backends::MockProvider>(std::move(mock_options)));
    harness.use_default_router();

    apogee::harness::CatalogListing* catalog = harness.catalog_for("codex");
    REQUIRE(catalog != nullptr);
    const auto models = catalog->list_catalog({});
    REQUIRE(models.size() == 1);
    CHECK(models.front().id == "gpt-5.5");
    CHECK_FALSE(harness.can_list_catalog("root"));
}

// --- 28c: the verified record, passively ---------------------------------------

namespace {

[[nodiscard]] apogee::harness::ChatRequest ask(std::string model) {
    apogee::harness::ChatRequest request;
    request.model = std::move(model);
    request.messages.push_back(apogee::harness::ChatMessage::user("hello"));
    return request;
}

[[nodiscard]] std::shared_ptr<apogee::backends::MockProvider> mock_named(
    const std::string& name, std::function<void(const apogee::harness::ChatRequest&)> hook = {}) {
    apogee::backends::MockProvider::Options options;
    options.backend_name = name;
    options.on_request = std::move(hook);
    return std::make_shared<apogee::backends::MockProvider>(std::move(options));
}

}  // namespace

TEST_CASE("the Harness tells its observer which backend answered, and only on success",
          "[harness][observer]") {
    Harness harness{config_from(R"(
backends:
  a:
    type: mock
  b:
    type: mock
)")};
    harness.register_provider("a", mock_named("a"));
    harness.register_provider("b", mock_named("b", [](const apogee::harness::ChatRequest&) {
                                  throw apogee::harness::ProviderError("b", "down");
                              }));
    harness.use_default_router();
    std::vector<std::string> heard;
    harness.observe_turns([&heard](const std::string& backend) { heard.push_back(backend); });

    (void)harness.chat(ask("a"));
    (void)harness.complete(ask("a"));
    (void)harness.stream_chat(ask("a"), apogee::harness::StreamOptions{});
    CHECK(heard == std::vector<std::string>{"a", "a", "a"});
    CHECK_THROWS(harness.chat(ask("b")));
    CHECK(heard.size() == 3);

    // An observer that throws costs its record, never the turn.
    harness.observe_turns([](const std::string&) { throw std::runtime_error("full disk"); });
    CHECK_NOTHROW(harness.chat(ask("a")));
    harness.observe_turns({});
    CHECK_NOTHROW(harness.chat(ask("a")));
}

TEST_CASE("a provider backend's successful turn is recorded in its install's cache",
          "[backends][factory][verified]") {
    const apogee::testing::TempDir root{"verified-" + std::to_string(std::random_device{}())};
    const std::filesystem::path config_path = root.path() / "config" / "config.yaml";
    std::filesystem::create_directories(config_path.parent_path());
    Harness harness{config_from(R"(
backends:
  cloud:
    type: anthropic
    api_key: sk-test
  local:
    type: mock
)")};
    apogee::backends::BuildOptions options = no_env();
    options.config_path = config_path;
    (void)build_providers(harness, options);
    // The anthropic entry answered by a mock under its name: the factory's
    // observer knows `cloud` as an anthropic backend, and no network is used.
    harness.register_provider("cloud", mock_named("cloud"));
    harness.use_default_router();

    const std::filesystem::path cache = root.path() / "cache" / "providers.json";
    (void)harness.chat(ask("local"));
    CHECK_FALSE(std::filesystem::exists(cache));  // a local backend is not a provider
    (void)harness.chat(ask("cloud"));
    const apogee::backends::ProviderCache recorded = apogee::backends::load_provider_cache(cache);
    REQUIRE(recorded.verified.contains("anthropic"));
    CHECK(recorded.verified.at("anthropic").backend == "cloud");
    CHECK(recorded.verified.at("anthropic").date ==
          apogee::backends::cache_day(std::chrono::system_clock::now()));
}

TEST_CASE("a config outside an install's config directory records nothing anywhere",
          "[backends][factory][verified]") {
    const apogee::testing::TempDir root{"verified-bare-" + std::to_string(std::random_device{}())};
    // A name no other run uses, so a record anywhere it could land is seen.
    const std::string name = "cloud" + std::to_string(std::random_device{}());
    Harness harness{config_from("backends:\n  " + name + ":\n    type: anthropic\n" +
                                "    api_key: sk-test\n")};
    apogee::backends::BuildOptions options = no_env();
    options.config_path = root.path() / "my.yaml";  // not <root>/config/<file>
    (void)build_providers(harness, options);
    harness.register_provider(name, mock_named(name));
    harness.use_default_router();
    (void)harness.chat(ask(name));
    CHECK_FALSE(std::filesystem::exists(root.path() / "cache"));
    // Where a parent-of-parent derivation would have put it -- the temp root.
    const apogee::backends::ProviderCache outside = apogee::backends::load_provider_cache(
        root.path().parent_path() / "cache" / "providers.json");
    for (const auto& [provider, record] : outside.verified) {
        CHECK(record.backend != name);
    }
}
