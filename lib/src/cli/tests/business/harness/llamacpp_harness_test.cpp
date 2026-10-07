#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "agentloop/content.h"
#include "backends/llamacpp.h"
#include "backends/llamacpp_tokens.h"
#include "contracts/errors.h"
#include "harness/harness.h"
#include "support/fake_llama.h"
#include "support/gguf_builder.h"

/// The local backend driven through the Harness and the loop: its capability
/// probes, the window a chat measures against, compaction. Moved from the
/// backend's own suite (A4): a test of the two together lives in the higher
/// layer (ADR 0004).
///
/// The local backend's contract, asserted against a scripted runtime.
///
/// Every claim this item makes about the KV cache is a COUNTING claim -- how
/// many tokens were decoded, on which context, after which trim -- so the fake
/// records decodes and the assertions are exact integers. That is stronger than
/// what a real model could give us here: a wall-clock speedup is a measurement
/// that varies by machine, while "turn two decoded four tokens" either holds or
/// does not.
namespace {

using apogee::backends::LlamaCppProvider;
using apogee::harness::ChatMessage;
using apogee::harness::ChatRequest;
using apogee::testing::FakeLlamaRuntime;

struct Fixture {
    FakeLlamaRuntime* runtime = nullptr;
    std::unique_ptr<LlamaCppProvider> provider;

    explicit Fixture(std::vector<std::int32_t> script = {}) {
        auto owned = std::make_unique<FakeLlamaRuntime>();
        owned->script = std::move(script);
        owned->eog_token = -1;
        runtime = owned.get();

        LlamaCppProvider::Options options;
        options.backend_name = "local";
        options.model = "test-model";
        options.model_path = "/models/test.gguf";
        provider = std::make_unique<LlamaCppProvider>(std::move(options), std::move(owned));
    }
};

ChatRequest turn(std::vector<ChatMessage> messages) {
    ChatRequest request;
    request.messages = std::move(messages);
    return request;
}

/// A provider over a model trained for `trained` positions, with the
/// backend's `context_size` and what a load finds free memory holds.
struct Sized {
    FakeLlamaRuntime* runtime = nullptr;
    std::unique_ptr<LlamaCppProvider> provider;

    explicit Sized(std::int64_t trained, std::int64_t context_size = 0, std::int64_t fitted = 0,
                   std::string model_path = "/models/test.gguf") {
        auto owned = std::make_unique<FakeLlamaRuntime>();
        owned->trained_length = trained;
        owned->fitted = fitted;
        runtime = owned.get();
        LlamaCppProvider::Options options;
        options.backend_name = "local";
        options.model = "test-model";
        options.model_path = std::move(model_path);
        options.context_size = context_size;
        provider = std::make_unique<LlamaCppProvider>(std::move(options), std::move(owned));
    }

    /// The window the conversation's context was made with.
    [[nodiscard]] std::int64_t session_window() {
        (void)provider->chat(turn({ChatMessage::user("alpha beta")}), {});
        REQUIRE_FALSE(runtime->model->context_sizes.empty());
        return runtime->model->context_sizes.front();
    }
};

}  // namespace

TEST_CASE("the harness routes an exact count through its probe",
          "[backends][llamacpp][usage][harness]") {
    // No `dynamic_cast` at the call site: the surface asks the Harness a plain
    // typed question, which is what keeps this from becoming a type switch.
    Fixture fixture;
    const ChatRequest request = turn({ChatMessage::user("alpha beta")});
    (void)fixture.provider->chat(request, {});

    apogee::harness::Harness harness{apogee::harness::Config{}};
    harness.register_provider("local", std::move(fixture.provider));
    harness.use_default_router();

    const auto counted = harness.count_prompt_tokens("local", request);
    REQUIRE(counted.has_value());
    CHECK(*counted > 0);

    // An unroutable model falls back to the estimate rather than throwing.
    CHECK_FALSE(harness.count_prompt_tokens("nope", request).has_value());
}

TEST_CASE("an unknown backend is assumed capable", "[backends][llamacpp][capability]") {
    Fixture fixture;
    apogee::harness::Harness harness{apogee::harness::Config{}};
    harness.register_provider("local", std::move(fixture.provider));
    harness.use_default_router();

    CHECK_FALSE(harness.accepts_images("local"));
    // Pre-refusing on a backend we cannot ask is the expensive direction of a
    // wrong guess: it blocks a capability that probably works.
    CHECK(harness.accepts_images("some-cloud-model"));
}

TEST_CASE("the chat measures against the window the backend allocated",
          "[backends][llamacpp][window][harness]") {
    // Context monitoring warns at 80% and compacts at 90% of this number; for
    // a local model the fallback table has no row, so without the backend's
    // answer a long chat was never warned or compacted at all.
    apogee::harness::Config config;
    apogee::harness::BackendConfig entry;
    entry.type = apogee::harness::BackendType::LlamaCpp;
    entry.model_path = "/models/test.gguf";
    config.backends["local"] = entry;
    entry.context_size = 4096;
    config.backends["pinned"] = entry;

    Sized local{262144};
    // Pinned in the config alone: the entry's context_size is the harness's
    // to honour, whatever the provider would say.
    Sized pinned{262144};
    LlamaCppProvider& loaded = *local.provider;
    LlamaCppProvider& pinned_provider = *pinned.provider;
    apogee::harness::Harness harness{config};
    harness.register_provider("local", std::move(local.provider));
    harness.register_provider("pinned", std::move(pinned.provider));
    harness.use_default_router();

    CHECK(harness.context_window_for_model("local") == 0);  // no header, not loaded
    (void)loaded.chat(turn({ChatMessage::user("alpha")}), {});
    (void)pinned_provider.chat(turn({ChatMessage::user("alpha")}), {});
    CHECK(harness.context_window_for_model("local") == 32768);
    CHECK(harness.context_window_for_model("pinned") == 4096);
}

TEST_CASE("a local chat past 90% of its default window is compacted, not run into the wall",
          "[backends][llamacpp][window][harness]") {
    // Before 26a a backend with no context_size had no window the chat knew
    // of -- the table has no local rows -- so it was never warned or
    // compacted, and a long chat ran into the wall instead.
    apogee::harness::Config config;
    apogee::harness::BackendConfig entry;
    entry.type = apogee::harness::BackendType::LlamaCpp;
    entry.model_path = "/models/test.gguf";
    config.backends["local"] = entry;

    Sized small{4096};
    LlamaCppProvider& provider = *small.provider;
    apogee::harness::Harness harness{config};
    harness.register_provider("local", std::move(small.provider));
    harness.use_default_router();
    (void)provider.chat(turn({ChatMessage::user("alpha")}), {});

    std::string words;
    for (int i = 0; i < 3800; ++i) {
        words += "word ";
    }
    const std::vector<ChatMessage> history{ChatMessage::user(words)};
    const apogee::agentloop::ContextUsage usage =
        apogee::agentloop::measure_context(harness, history, "local");
    CHECK(usage.window == 4096);
    CHECK(usage.exact);
    CHECK(usage.should_warn());
    CHECK(usage.should_compact());

    const std::vector<ChatMessage> short_history{ChatMessage::user("alpha beta")};
    CHECK_FALSE(apogee::agentloop::measure_context(harness, short_history, "local").should_warn());
}

TEST_CASE("a projector with an audio encoder is audio-capable, asked of the harness",
          "[backends][llamacpp][capability][helpers]") {
    // From the projector's own header, without a load; and only where this
    // build can run a local model at all -- as `accepts_images` answers.
    const auto projector = [](bool vision, bool audio, const std::string& name) {
        const std::filesystem::path path =
            std::filesystem::temp_directory_path() / ("apogee-projector-" + name + ".gguf");
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        const std::string bytes = apogee::testing::projector_gguf(vision, audio);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return path;
    };
    const std::filesystem::path hears = projector(false, true, "hears");
    const std::filesystem::path sees = projector(true, false, "sees");

    const auto provider_with = [](const std::string& mmproj) {
        LlamaCppProvider::Options options;
        options.backend_name = "local";
        options.model_path = "/models/test.gguf";
        options.mmproj_path = mmproj;
        return std::make_shared<LlamaCppProvider>(std::move(options),
                                                  std::make_unique<FakeLlamaRuntime>());
    };

    apogee::harness::Harness harness{apogee::harness::Config{}};
    harness.register_provider("ears", provider_with(hears.string()));
    harness.register_provider("eyes", provider_with(sees.string()));
    harness.register_provider("plain", provider_with(""));
    harness.use_default_router();

    CHECK(harness.accepts_audio("ears") == apogee::backends::llama_available());
    CHECK_FALSE(harness.accepts_audio("eyes"));
    CHECK_FALSE(harness.accepts_audio("plain"));
    // Unknown is no -- nothing is sent audio that has not said it reads it.
    CHECK_FALSE(harness.accepts_audio("nowhere"));

    std::error_code code;
    std::filesystem::remove(hears, code);
    std::filesystem::remove(sees, code);
}

TEST_CASE("a projector that sees reads a clip as its frames, asked of the harness",
          "[backends][llamacpp][capability][media]") {
    const auto projector = [](bool vision, bool audio, const std::string& name) {
        const std::filesystem::path path =
            std::filesystem::temp_directory_path() / ("apogee-projector-video-" + name + ".gguf");
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        const std::string bytes = apogee::testing::projector_gguf(vision, audio);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return path;
    };
    const std::filesystem::path sees = projector(true, false, "sees");
    const std::filesystem::path hears = projector(false, true, "hears");
    const auto provider_with = [](const std::string& mmproj) {
        LlamaCppProvider::Options options;
        options.backend_name = "local";
        options.model_path = "/models/test.gguf";
        options.mmproj_path = mmproj;
        return std::make_shared<LlamaCppProvider>(std::move(options),
                                                  std::make_unique<FakeLlamaRuntime>());
    };
    apogee::harness::Harness harness{apogee::harness::Config{}};
    harness.register_provider("eyes", provider_with(sees.string()));
    harness.register_provider("ears", provider_with(hears.string()));
    harness.register_provider("plain", provider_with(""));
    harness.use_default_router();

    CHECK(harness.accepts_video("eyes") == apogee::backends::llama_available());
    CHECK(harness.can_read("eyes", apogee::harness::Medium::Video) ==
          apogee::backends::llama_available());
    CHECK_FALSE(harness.accepts_video("ears"));
    CHECK_FALSE(harness.accepts_video("plain"));
    CHECK_FALSE(harness.accepts_video("nowhere"));
    CHECK(harness.can_read("ears", apogee::harness::Medium::Audio) ==
          apogee::backends::llama_available());

    std::error_code code;
    std::filesystem::remove(sees, code);
    std::filesystem::remove(hears, code);
}

namespace {

/// A backend with no conversation state of its own.
class Stateless final : public apogee::harness::LLMProvider {
public:
    [[nodiscard]] std::string_view backend_name() const noexcept override {
        return "plain";
    }

    [[nodiscard]] apogee::harness::ChatResponse chat(
        const ChatRequest& /*request*/,
        const apogee::harness::CancellationToken& /*cancel*/) override {
        return {};
    }

    [[nodiscard]] apogee::harness::ChatResponse stream_chat(
        const ChatRequest& /*request*/,
        const apogee::harness::StreamOptions& /*options*/) override {
        return {};
    }

    [[nodiscard]] std::vector<apogee::harness::ModelInfo> list_models(
        const apogee::harness::CancellationToken& /*cancel*/) override {
        return {};
    }
};

}  // namespace

TEST_CASE("the harness names and saves a conversation only where a backend keeps one",
          "[harness][prompt-cache]") {
    // 26j: discovered as a capability, never asked of a backend by type.
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("apogee-harness-prompt-cache-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(root);
    std::ofstream{root / "model.gguf", std::ios::binary} << "weights";

    auto owned = std::make_unique<FakeLlamaRuntime>();
    owned->eog_token = -1;
    owned->script_text = {"answered"};
    LlamaCppProvider::Options options;
    options.backend_name = "local";
    options.model = "test-model";
    options.model_path = (root / "model.gguf").string();
    options.prompt_cache_dir = root / "cache";
    apogee::harness::Harness harness{apogee::harness::Config{}};
    harness.register_provider(
        "local", std::make_shared<LlamaCppProvider>(std::move(options), std::move(owned)));
    harness.register_provider("plain", std::make_shared<Stateless>());
    harness.use_default_router();

    std::string words;
    for (int index = 0; index < 2100; ++index) {
        words += " said" + std::to_string(index);
    }
    ChatRequest request = turn({ChatMessage::user(words)});
    request.model = "local";
    harness.resume_conversation("local", "chat-7");
    (void)harness.chat(request);
    std::vector<std::string> said;
    harness.save_conversation(
        "local", "chat-7",
        [&said](const apogee::harness::StatusEvent& event) { said.push_back(event.detail); });
    REQUIRE(said.size() == 1);
    CHECK(said.front().find("saved this chat's state") != std::string::npos);
    CHECK(std::filesystem::exists(root / "cache" / "chats" / "chat-7.state"));

    // A backend that keeps none, and one nobody configured: nothing happens.
    said.clear();
    harness.resume_conversation("plain", "chat-7");
    harness.save_conversation("plain", "chat-7",
                              [&said](const auto& event) { said.push_back(event.detail); });
    harness.save_conversation("nowhere", "chat-7",
                              [&said](const auto& event) { said.push_back(event.detail); });
    CHECK(said.empty());

    std::error_code code;
    std::filesystem::remove_all(root, code);
}

TEST_CASE("a suite member pinned to 4096 runs its calls at that window",
          "[backends][llamacpp][window][harness][suites]") {
    // 27d's acceptance on the wire: the context llama.cpp is asked to make
    // for the member's backend is the member's window -- not the backend's
    // own default -- exactly while the suite is active. The factory builds
    // every provider from `backend_as_run`; this builds one the same way
    // over the scripted runtime.
    apogee::harness::Config config = apogee::harness::parse_config(R"(
models:
  default: helper
backends:
  helper:
    type: llamacpp
    model_path: /models/test.gguf
suites:
  research:
    members:
      utility:
        backend: helper
        context_size: 4096
)",
                                                                   "<test>");
    for (const bool active : {false, true}) {
        CAPTURE(active);
        config.models.default_suite = active ? "research" : "";
        auto owned = std::make_unique<FakeLlamaRuntime>();
        owned->trained_length = 262144;
        FakeLlamaRuntime* runtime = owned.get();
        LlamaCppProvider::Options options = LlamaCppProvider::options_from(
            "helper", apogee::harness::backend_as_run(config, "helper"));
        options.prompt_cache_dir.clear();  // hermetic: no state kept on disk
        apogee::harness::Harness harness{config};
        harness.register_provider(
            "helper", std::make_shared<LlamaCppProvider>(std::move(options), std::move(owned)));
        harness.use_default_router();

        ChatRequest request = turn({ChatMessage::user("alpha")});
        request.model = "helper";
        (void)harness.chat(request);
        REQUIRE(runtime->model != nullptr);
        REQUIRE_FALSE(runtime->model->context_sizes.empty());
        CHECK(runtime->model->context_sizes.front() == (active ? 4096 : 32768));
        CHECK(harness.context_window_for_model("helper") == (active ? 4096 : 32768));
    }
}
