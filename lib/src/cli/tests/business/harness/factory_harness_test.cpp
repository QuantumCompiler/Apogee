#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "backends/claude_cli.h"
#include "backends/codex_cli.h"
#include "backends/factory.h"
#include "backends/gemini_cli.h"
#include "backends/mock.h"
#include "backends/provider_cache.h"
#include "backends/provider_status.h"
#include "backends/provider_table.h"
#include "contracts/config.h"
#include "contracts/errors.h"
#include "harness/harness.h"
#include "platform/child_process.h"
#include "platform/platform.h"
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

TEST_CASE(
    "an mlx entry over an embedding model embeds, discovered by the harness, and chats with "
    "nothing",
    "[backends][factory][embed][capability][mlx]") {
    // 53h: the factory builds the embedding provider for an entry whose
    // directory mlx-embeddings built; the Harness discovers EmbeddingCapable
    // on the object, never a type. A chat entry beside it is no embedder.
    // Construction asks files only -- nothing is spawned here.
    if (apogee::platform::host_target() != "macos-arm64") {
        SKIP("the mlx backend's ladder refuses every host but Apple silicon");
    }
    const apogee::testing::TempDir home{"factory-mlx-embed-" +
                                        std::to_string(std::random_device{}())};
    const apogee::testing::EnvGuard root{"APOGEE_HOME", home.path().string()};
    const std::filesystem::path site =
        home.path() / "training" / "venv" / "lib" / "python3.99" / "site-packages";
    const auto touch = [](const std::filesystem::path& path, const std::string& text) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream{path, std::ios::binary} << text;
    };
    touch(home.path() / "training" / "venv" / "bin" / "python", "#!/bin/sh\n");
    touch(site / "mlx_lm" / "__init__.py", "");
    touch(site / "mlx_embeddings" / "__init__.py", "");
    touch(home.path() / "training" / "scripts" / "mlx_generate.py", "#\n");
    touch(home.path() / "training" / "scripts" / "mlx_embed.py", "#\n");
    const std::filesystem::path embed = home.path() / "models" / "embed";
    touch(embed / "config.json", R"({"model_type": "gemma3_text", "hidden_size": 768})");
    touch(embed / "apogee-snapshot.json",
          R"({"source": "convert", "transform": "mlx_embeddings.convert 8bit", "files": [],)"
          R"( "kind": "embedding"})");
    const std::filesystem::path talk = home.path() / "models" / "talk";
    touch(talk / "config.json", R"({"model_type": "llama"})");

    apogee::harness::Config config;
    apogee::harness::BackendConfig entry;
    entry.type = apogee::harness::BackendType::Mlx;
    entry.model_path = embed.string();
    config.backends.emplace("embed", entry);
    entry.model_path = talk.string();
    config.backends.emplace("talk", entry);
    apogee::harness::Harness harness{config};
    const auto built = apogee::backends::build_providers(harness, {});
    INFO(built.skipped_summary());
    REQUIRE(built.constructed_count() == 2);

    CHECK(harness.can_embed("embed"));
    CHECK_FALSE(harness.can_embed("talk"));
    apogee::harness::EmbeddingCapable* embedder = harness.embedder_for("embed");
    REQUIRE(embedder != nullptr);
    CHECK(embedder->embedding_model_name() == "mlx:" + embed.string());
    CHECK(embedder->embedding_dimensions() == 768);
    CHECK_FALSE(embedder->embedding_is_metered());
    CHECK_FALSE(harness.generation_is_metered("embed"));

    // Without mlx-embeddings the entry is skipped with its rung and its fix.
    std::filesystem::remove_all(site / "mlx_embeddings");
    apogee::harness::Harness bare{config};
    const auto skipped = apogee::backends::build_providers(bare, {});
    CHECK(skipped.constructed_count() == 1);
    CHECK(skipped.skipped_summary().find("apogee train setup --with mlx-embeddings") !=
          std::string::npos);
    CHECK_FALSE(bare.can_embed("embed"));
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

TEST_CASE("the built providers say whose reasoning is already its vendor's summary",
          "[backends][factory][behavior][summary]") {
    // 43: the cloud APIs and the vendor CLIs stream a summary of the
    // reasoning, never the chain of thought -- shown as it is; Claude 3.7
    // streamed its reasoning whole, and the mock, a local model's stand-in,
    // says nothing, so its raw reasoning is summarised.
    Harness harness{config_from(R"(
backends:
  claude:
    type: anthropic
    api_key: sk-ant-test
    model: claude-sonnet-5
  claude37:
    type: anthropic
    api_key: sk-ant-test
    model: claude-3-7-sonnet-20250219
  gpt:
    type: openai
    api_key: sk-test
  gemini:
    type: google
    api_key: AIza-test
  local:
    type: mock
)")};
    REQUIRE(build_providers(harness, no_env()).constructed_count() == 5);
    CHECK(harness.model_behavior_for("claude").reasoning_summarized);
    CHECK_FALSE(harness.model_behavior_for("claude37").reasoning_summarized);
    CHECK(harness.model_behavior_for("gpt").reasoning_summarized);
    CHECK(harness.model_behavior_for("gemini").reasoning_summarized);
    CHECK_FALSE(harness.model_behavior_for("local").reasoning_summarized);

    // The vendor CLIs, each over a fake child: built by hand, as the factory
    // would need each one installed.
    Harness clis{config_from(R"(
backends:
  claude-cli:
    type: claude-cli
  codex:
    type: codex-cli
  gemini-cli:
    type: gemini-cli
)")};
    auto spawner = std::make_shared<apogee::testing::FakeSpawner>();
    const auto spawn = [spawner](const apogee::platform::ChildCommand& command,
                                 std::string& error) { return (*spawner)(command, error); };
    apogee::backends::ClaudeCliProvider::Options claude_cli;
    claude_cli.backend_name = "claude-cli";
    clis.register_provider("claude-cli", std::make_shared<apogee::backends::ClaudeCliProvider>(
                                             std::move(claude_cli), spawn));
    apogee::backends::CodexCliProvider::Options codex_cli;
    codex_cli.backend_name = "codex";
    clis.register_provider(
        "codex", std::make_shared<apogee::backends::CodexCliProvider>(std::move(codex_cli), spawn));
    apogee::backends::GeminiCliProvider::Options gemini_cli;
    gemini_cli.backend_name = "gemini-cli";
    clis.register_provider("gemini-cli",
                           std::make_shared<apogee::backends::GeminiCliProvider>(
                               std::move(gemini_cli), spawn, [] { return std::string{"id"}; }));
    clis.use_default_router();
    for (const char* name : {"claude-cli", "codex", "gemini-cli"}) {
        INFO(name);
        CHECK(clis.model_behavior_for(name).reasoning_summarized);
    }
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

TEST_CASE("the user's run: a vendor CLI default that could not start is refused with why",
          "[backends][factory][skipped]") {
    // M26, the fixture the item names: a claude-cli default not found, beside
    // a local backend that started. The words were "no backend serves
    // 'claude' (configured backends: Local-Mock)"; the backend is configured,
    // and why it did not start was known to the factory all along.
    const apogee::testing::TempDir root{"skipped-default-" +
                                        std::to_string(std::random_device{}())};
    const std::filesystem::path binary = root.path() / "bin" / "claude";
    Harness harness{
        config_from("models:\n  default: claude\nbackends:\n  claude:\n"
                    "    type: claude-cli\n    binary: \"" +
                    binary.generic_string() + "\"\n  Local-Mock:\n    type: mock\n")};
    const auto built = build_providers(harness, no_env());
    REQUIRE(built.constructed_count() == 1);  // still non-fatal
    const auto skipped = std::ranges::find_if(
        built.statuses, [](const apogee::backends::BackendStatus& s) { return !s.constructed; });
    REQUIRE(skipped != built.statuses.end());
    const std::string reason = skipped->reason.substr(std::string{"claude: "}.size());
    REQUIRE(skipped->reason == "claude: " + reason);

    // Asked for by name -- what `complete` and the app's sessions route,
    // having resolved the default themselves -- and by default.
    for (const std::string_view asked : {"claude", ""}) {
        INFO("asked for '" << asked << "'");
        try {
            (void)harness.route(asked);
            FAIL("expected NoAvailableBackendError");
        } catch (const apogee::harness::NoAvailableBackendError& e) {
            CHECK(std::string{e.what()} ==
                  "the default backend 'claude' is configured but could not be started: " + reason +
                      " (started backends: Local-Mock)");
            CHECK(e.skipped_backend() == "claude");
            CHECK(e.reason() == reason);
            if (apogee::platform::supports_child_processes()) {
                // The doctor's words (28c), never a second wording.
                CHECK(reason ==
                      apogee::backends::not_on_path(binary.generic_string()) + ". " +
                          std::string{
                              apogee::backends::provider_for_type(BackendType::ClaudeCli)->remedy});
            }
        }
    }
    CHECK(harness.route("Local-Mock").backend_name() == "Local-Mock");

    if (!apogee::platform::supports_child_processes()) {
        return;
    }
    // Installed since, and rebuilt (27d's path): it starts, routes, and no
    // refusal names it any more. Construction only checks the path; nothing
    // is spawned.
    std::filesystem::create_directories(binary.parent_path());
    std::ofstream{binary} << "#!/bin/sh\nexit 1\n";
    REQUIRE(apogee::backends::rebuild_providers(harness, {"claude"}, no_env())
                .statuses.front()
                .constructed);
    CHECK(harness.route("").backend_name() == "claude");
}
