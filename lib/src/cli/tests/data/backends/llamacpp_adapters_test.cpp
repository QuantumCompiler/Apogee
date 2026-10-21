#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "backends/llamacpp.h"
#include "backends/prompt_cache.h"
#include "contracts/config.h"
#include "contracts/errors.h"
#include "contracts/layout.h"
#include "contracts/sha256.h"
#include "modelstore/sidecar.h"
#include "support/env_guard.h"
#include "support/fake_llama.h"

/// Adapters at the llamacpp runtime (53a), over the scripted runtime: the
/// entry's set reaching the load in order with its scales, each adapter
/// loaded once, every context the model makes applying it -- and, before
/// any of that, the refusal table a load asks, then the prompt cache telling
/// one set from another. Without adapters, everything exactly as before.
namespace {

namespace fs = std::filesystem;
using apogee::backends::AdapterLoad;
using apogee::backends::LlamaCppProvider;
using apogee::harness::AdapterConfig;
using apogee::harness::ChatMessage;
using apogee::harness::ChatRequest;
using apogee::harness::StatusEvent;
using apogee::testing::FakeLlamaContext;
using apogee::testing::FakeLlamaRuntime;
using Catch::Matchers::ContainsSubstring;

void write(const fs::path& path, std::string_view bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream{path, std::ios::binary} << bytes;
}

/// A base, two adapters, a memo and a prompt cache, in a directory of their
/// own.
struct Bench {
    apogee::testing::TempDir dir{"llama-adapters-" + std::to_string(std::random_device{}())};
    fs::path base = dir.path() / "base.gguf";
    fs::path style = dir.path() / "style.gguf";
    fs::path tone = dir.path() / "tone.gguf";
    fs::path memo = dir.path() / "cache" / "weight-hashes.json";
    fs::path cache = dir.path() / "cache" / "prompt";

    Bench() {
        write(base, "base weights");
        write(style, "style delta");
        write(tone, "tone delta");
    }

    /// `adapter` pinned to the base as it is now.
    [[nodiscard]] AdapterConfig pinned(const fs::path& adapter, double scale = 1.0) const {
        return {.path = adapter.string(),
                .scale = scale,
                .base_sha256 = apogee::models::file_sha256(base)};
    }

    [[nodiscard]] std::vector<fs::path> prefix_files() const {
        std::vector<fs::path> found;
        std::error_code code;
        for (fs::recursive_directory_iterator it{cache, code}, end; !code && it != end;
             it.increment(code)) {
            if (it->is_regular_file() && it->path().extension() == ".state") {
                found.push_back(it->path());
            }
        }
        return found;
    }
};

/// One process over the bench: a fresh runtime and provider.
struct Process {
    FakeLlamaRuntime* runtime = nullptr;
    std::unique_ptr<LlamaCppProvider> provider{};
    std::vector<StatusEvent> statuses{};
    apogee::harness::StreamOptions options{};

    Process(const Bench& bench, std::vector<AdapterConfig> adapters, bool prompt_cache = false) {
        auto owned = std::make_unique<FakeLlamaRuntime>();
        owned->chat_template = true;
        owned->eog_token = -1;
        owned->script_text = {"answered"};
        owned->context_capacity = 0;
        runtime = owned.get();
        LlamaCppProvider::Options settings;
        settings.backend_name = "local";
        settings.model = "test-model";
        settings.model_path = bench.base.string();
        settings.adapters = std::move(adapters);
        settings.weight_hashing.memo = bench.memo;
        if (prompt_cache) {
            settings.prompt_cache_dir = bench.cache;
        }
        provider = std::make_unique<LlamaCppProvider>(std::move(settings), std::move(owned));
        options.on_status = [this](const StatusEvent& event) { statuses.push_back(event); };
    }

    [[nodiscard]] std::string ask(const ChatRequest& request) {
        return provider->stream_chat(request, options).message.content.plain_text();
    }

    [[nodiscard]] bool said(std::string_view what) const {
        return std::ranges::any_of(statuses, [what](const StatusEvent& event) {
            return event.detail.find(what) != std::string::npos;
        });
    }
};

[[nodiscard]] ChatRequest question(const std::string& text) {
    ChatRequest request;
    request.messages = {ChatMessage::user(text)};
    return request;
}

/// `count` words of system prompt: long enough for a prefix file.
[[nodiscard]] ChatRequest opening(std::size_t count, const std::string& text) {
    std::string system;
    for (std::size_t index = 0; index < count; ++index) {
        system += (system.empty() ? "" : " ") + std::string{"rule"} + std::to_string(index);
    }
    ChatRequest request;
    request.messages = {ChatMessage::system(system), ChatMessage::user(text)};
    return request;
}

}  // namespace

TEST_CASE("an entry's adapters reach the load in order with their scales, each loaded once",
          "[backends][llamacpp][adapters]") {
    const Bench bench;
    Process process{bench, {bench.pinned(bench.style, 0.5), bench.pinned(bench.tone)}};
    CHECK(process.ask(question("first")) == "answered");
    (void)process.ask(question("second"));
    ChatRequest title = question("name this chat");
    title.transient.side_request = true;
    (void)process.ask(title);

    CHECK(process.runtime->loads == 1);
    // Once each, however many turns and contexts follow.
    CHECK(process.runtime->adapter_loads ==
          std::vector<std::string>{bench.style.string(), bench.tone.string()});
    CHECK(process.runtime->last_load.adapters ==
          std::vector<AdapterLoad>{{.path = bench.style.string(), .scale = 0.5},
                                   {.path = bench.tone.string(), .scale = 1.0}});
    // The base was read once for the pins, then remembered.
    CHECK(fs::exists(bench.memo));
}

TEST_CASE("every context the model makes applies the entry's set, the embedding one too",
          "[backends][llamacpp][adapters]") {
    const Bench bench;
    const std::vector<AdapterLoad> set{{.path = bench.tone.string(), .scale = 1.0},
                                       {.path = bench.style.string(), .scale = 0.25}};
    Process process{bench, {bench.pinned(bench.tone), bench.pinned(bench.style, 0.25)}};
    (void)process.ask(question("hello"));
    ChatRequest side = question("a title");
    side.transient.side_request = true;
    (void)process.ask(side);
    REQUIRE(process.runtime->model != nullptr);
    // The conversation's context and the side request's: one model, each
    // context set with the entry's adapters at their scales, in order.
    REQUIRE(process.runtime->model->contexts.size() == 2);
    for (const std::shared_ptr<FakeLlamaContext>& context : process.runtime->model->contexts) {
        CHECK(context->adapters == set);
    }
    (void)process.provider->embed({"a passage"}, {});
    CHECK(process.runtime->model->embedding_adapters == set);
    CHECK(process.runtime->loads == 1);
}

TEST_CASE("a reload loads the set again, once, over the new model",
          "[backends][llamacpp][adapters]") {
    const Bench bench;
    Process process{bench, {bench.pinned(bench.style)}};
    (void)process.ask(question("hello"));
    process.provider->unload();
    (void)process.ask(question("hello again"));
    CHECK(process.runtime->loads == 2);
    CHECK(process.runtime->adapter_loads ==
          std::vector<std::string>{bench.style.string(), bench.style.string()});
    CHECK(process.runtime->model->contexts.front()->adapters ==
          std::vector<AdapterLoad>{{.path = bench.style.string(), .scale = 1.0}});
}

TEST_CASE("without adapters the load and every context are what they were",
          "[backends][llamacpp][adapters]") {
    const Bench bench;
    Process process{bench, {}, true};
    (void)process.ask(opening(600, "hello"));
    CHECK(process.runtime->last_load.adapters.empty());
    CHECK(process.runtime->adapter_loads.empty());
    CHECK(process.runtime->model->contexts.front()->adapters.empty());
    // Nothing was pinned, so no weights were read for it.
    CHECK_FALSE(fs::exists(bench.memo));
    // The cache's record and directory are the bare base's, as before 53a.
    const auto fingerprint = apogee::backends::fingerprint_of(bench.base);
    REQUIRE(fingerprint.has_value());
    const apogee::backends::PromptCache cache{bench.cache};
    const fs::path dir =
        cache.prefix_path(*fingerprint, apogee::harness::KvCacheType::Q8_0, 0, {}).parent_path();
    REQUIRE(bench.prefix_files().size() == 1);
    CHECK(bench.prefix_files().front().parent_path() == dir);
    std::ifstream in{dir / "model.json"};
    const std::string record{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    CHECK(record.find("\"adapters\"") == std::string::npos);
}

TEST_CASE("the refusal table: file facts before any load, each named with its fix",
          "[backends][llamacpp][adapters][refusal]") {
    const Bench bench;
    const auto refusal = [&bench](std::vector<AdapterConfig> adapters) {
        Process process{bench, std::move(adapters)};
        try {
            (void)process.ask(question("hello"));
        } catch (const apogee::harness::ProviderError& e) {
            // Nothing was read into memory, and the load said why it failed.
            CHECK(process.runtime->loads == 0);
            CHECK(process.runtime->adapter_loads.empty());
            const auto failed =
                std::ranges::find_if(process.statuses, [](const StatusEvent& event) {
                    return event.type == StatusEvent::Type::ModelLoading &&
                           event.phase == StatusEvent::Phase::Error;
                });
            REQUIRE(failed != process.statuses.end());
            CHECK_THAT(std::string{e.what()}, ContainsSubstring(failed->detail));
            return std::string{e.what()};
        }
        FAIL("the load was not refused");
        return std::string{};
    };
    CHECK_THAT(refusal({{.path = (bench.dir.path() / "gone.gguf").string(),
                         .base_sha256 = apogee::models::file_sha256(bench.base)}}),
               ContainsSubstring("is not there -- restore it, or take the adapters off with "
                                 "apogee config set-backend local --adapters \"\""));
    CHECK_THAT(refusal({{.path = bench.style.string()}}),
               ContainsSubstring("is pinned to no base"));
    // The base changed under a pinned adapter: refused by name.
    AdapterConfig stale = bench.pinned(bench.style);
    stale.base_sha256 = apogee::models::sha256_hex("another base");
    CHECK_THAT(refusal({bench.pinned(bench.tone), stale}),
               ContainsSubstring("the adapter " + bench.style.string() +
                                 " was pinned to other base weights"));
}

TEST_CASE("an adapter llama.cpp will not load fails the load, its word relayed after the facts",
          "[backends][llamacpp][adapters][refusal]") {
    const Bench bench;
    Process process{bench, {bench.pinned(bench.style), bench.pinned(bench.tone)}};
    process.runtime->adapter_error_path = bench.tone.string();
    CHECK_THROWS_WITH(
        process.ask(question("hello")),
        ContainsSubstring("could not load the adapter at '" + bench.tone.string() + "'"));
    CHECK_FALSE(process.provider->model_loaded());
}

TEST_CASE("a base replaced while its model was unloaded is refused at the next load",
          "[backends][llamacpp][adapters][refusal]") {
    const Bench bench;
    Process process{bench, {bench.pinned(bench.style)}};
    (void)process.ask(question("hello"));
    process.provider->unload();
    write(bench.base, "retrained base weights");
    CHECK_THROWS_WITH(process.ask(question("hello again")),
                      ContainsSubstring("was pinned to other base weights"));
    CHECK(process.runtime->loads == 1);
}

TEST_CASE("a backend entry's adapters reach the provider as the loader read them",
          "[backends][llamacpp][adapters][config]") {
    apogee::harness::BackendConfig config;
    config.type = apogee::harness::BackendType::LlamaCpp;
    config.model_path = "/models/base.gguf";
    config.adapters = {
        {.path = "/a/style.gguf", .scale = 0.5, .base_sha256 = std::string(64, 'b')}};
    const LlamaCppProvider::Options options = LlamaCppProvider::options_from("local", config);
    CHECK(options.adapters == config.adapters);
    CHECK(options.weight_hashing.memo == apogee::harness::weight_hash_cache_path());
    CHECK(LlamaCppProvider::options_from("local", {}).adapters.empty());
}

TEST_CASE("the prompt cache misses across adapter sets and hits within one",
          "[backends][llamacpp][adapters][prompt-cache]") {
    const Bench bench;
    const std::vector<AdapterConfig> style{bench.pinned(bench.style)};
    {
        Process first{bench, style, true};
        (void)first.ask(opening(600, "first"));
        REQUIRE(bench.prefix_files().size() == 1);
    }
    // Another set over the same base: a miss, never a restore -- and its
    // own file beside the first, which it leaves alone.
    {
        Process other{bench, {bench.pinned(bench.tone)}, true};
        (void)other.ask(opening(600, "second"));
        CHECK(other.runtime->model->contexts.front()->loaded_states.empty());
        CHECK_FALSE(other.said("cleared"));
        CHECK(bench.prefix_files().size() == 2);
    }
    // The same adapter at another scale is another set.
    {
        Process scaled{bench, {bench.pinned(bench.style, 0.5)}, true};
        (void)scaled.ask(opening(600, "third"));
        CHECK(scaled.runtime->model->contexts.front()->loaded_states.empty());
    }
    // The bare base is another model again.
    {
        Process bare{bench, {}, true};
        (void)bare.ask(opening(600, "fourth"));
        CHECK(bare.runtime->model->contexts.front()->loaded_states.empty());
    }
    // A byte-equal set: a hit, the system prompt never read again.
    Process again{bench, style, true};
    (void)again.ask(opening(600, "fifth"));
    const std::shared_ptr<FakeLlamaContext> session = again.runtime->model->contexts.front();
    CHECK(session->loaded_states.size() == 1);
    CHECK(session->prompt_tokens_decoded() < 10);
}

TEST_CASE("an adapter file replaced in place clears its set's prefix cache, said once",
          "[backends][llamacpp][adapters][prompt-cache]") {
    const Bench bench;
    {
        Process first{bench, {bench.pinned(bench.style)}, true};
        (void)first.ask(opening(600, "first"));
    }
    write(bench.style, "style delta, retrained");
    Process second{bench, {bench.pinned(bench.style)}, true};
    (void)second.ask(opening(600, "first"));
    CHECK(
        second.said("the prompt cache for test-model was made with a different adapter file; "
                    "cleared"));
    CHECK(second.runtime->model->contexts.front()->loaded_states.empty());
    CHECK(bench.prefix_files().size() == 1);
}

TEST_CASE("a saved chat made under another adapter set is discarded and never restored",
          "[backends][llamacpp][adapters][prompt-cache]") {
    const Bench bench;
    std::string history;
    for (int index = 0; index < 2100; ++index) {
        history += " said" + std::to_string(index);
    }
    ChatRequest turn;
    turn.messages = {ChatMessage::system("be brief"), ChatMessage::user(history)};
    {
        Process first{bench, {bench.pinned(bench.style)}, true};
        (void)first.ask(turn);
        first.provider->save_conversation("chat-1", {});
    }
    REQUIRE(fs::exists(bench.cache / "chats" / "chat-1.state"));
    // The same set resumes it...
    {
        Process same{bench, {bench.pinned(bench.style)}, true};
        same.provider->resume_conversation("chat-1");
        (void)same.ask(turn);
        CHECK(same.runtime->model->contexts.front()->loaded_states.size() == 1);
    }
    // ...another never does.
    Process other{bench, {bench.pinned(bench.style, 0.5)}, true};
    other.provider->resume_conversation("chat-1");
    (void)other.ask(turn);
    CHECK(other.said("it was made under a different set of adapters"));
    CHECK(other.runtime->model->contexts.front()->loaded_states.empty());
    CHECK_FALSE(fs::exists(bench.cache / "chats" / "chat-1.state"));
}
