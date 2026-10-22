#include "backends/llama_bases.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "backends/llamacpp.h"
#include "contracts/config.h"
#include "contracts/types.h"
#include "modelstore/kv_cache.h"
#include "modelstore/sidecar.h"
#include "support/env_guard.h"
#include "support/fake_llama.h"

/// One base, many roles (53d), over the scripted runtime: providers on one
/// base file share one loaded model, each serving through its own adapters
/// on every context it uses -- interleaved turns, side requests, images and
/// embeddings never applying another entry's set -- the model freed when the
/// last sharer lets go and not before, sharers taking turns on it from two
/// threads, and entries on distinct bases, or loaded otherwise, sharing
/// nothing. The registry's own rules first.
namespace {

namespace fs = std::filesystem;
using apogee::backends::AdapterLoad;
using apogee::backends::LlamaBases;
using apogee::backends::LlamaCppProvider;
using apogee::backends::LlamaModel;
using apogee::backends::ModelLoad;
using apogee::backends::SharedBase;
using apogee::backends::SharedBaseKey;
using apogee::harness::AdapterConfig;
using apogee::harness::ChatMessage;
using apogee::harness::ChatRequest;
using apogee::harness::KvCacheType;
using apogee::testing::FakeLlamaContext;
using apogee::testing::FakeLlamaModel;
using apogee::testing::FakeLlamaRuntime;
using Catch::Matchers::ContainsSubstring;
using AdapterSet = std::vector<AdapterLoad>;

void write(const fs::path& path, std::string_view bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream{path, std::ios::binary} << bytes;
}

/// A base, another, two adapters and a hash memo in a directory of their own,
/// and the registry the entries over them share.
struct Bench {
    apogee::testing::TempDir dir{"llama-bases-" + std::to_string(std::random_device{}())};
    fs::path base = dir.path() / "base.gguf";
    fs::path other = dir.path() / "other.gguf";
    fs::path style = dir.path() / "style.gguf";
    fs::path tone = dir.path() / "tone.gguf";
    fs::path memo = dir.path() / "cache" / "weight-hashes.json";
    std::shared_ptr<LlamaBases> bases = std::make_shared<LlamaBases>();

    Bench() {
        write(base, "base weights");
        write(other, "other weights");
        write(style, "style delta");
        write(tone, "tone delta");
    }

    /// `adapter` pinned to the base as it is now.
    [[nodiscard]] AdapterConfig pinned(const fs::path& adapter, double scale = 1.0) const {
        return {.path = adapter.string(),
                .scale = scale,
                .base_sha256 = apogee::models::file_sha256(base)};
    }

    /// The set `pinned(adapter, scale)` loads.
    [[nodiscard]] static AdapterSet set(const fs::path& adapter, double scale = 1.0) {
        return {{.path = adapter.string(), .scale = scale}};
    }
};

/// How an entry differs from the plain one on the bench's base.
struct Shape {
    fs::path model{};
    std::int64_t context_size = 0;
    std::string mmproj{};
    std::optional<KvCacheType> cache_type{};
};

/// One backend entry: a provider over a scripted runtime of its own, its base
/// shared through the bench's registry. Every context samples the script from
/// its start: a fresh one answers "answered", and a conversation's context
/// has spent it after its first turn.
struct Entry {
    FakeLlamaRuntime* runtime = nullptr;
    std::unique_ptr<LlamaCppProvider> provider{};

    Entry(const Bench& bench, const std::string& name, std::vector<AdapterConfig> adapters,
          const Shape& shape = {}) {
        auto owned = std::make_unique<FakeLlamaRuntime>();
        owned->chat_template = true;
        owned->eog_token = -1;
        owned->script_text = {"answered"};
        owned->context_capacity = 0;
        owned->vision = true;
        owned->decodes_media = true;
        runtime = owned.get();
        LlamaCppProvider::Options options;
        options.backend_name = name;
        options.model = name + "-model";
        options.model_path = shape.model.empty() ? bench.base.string() : shape.model.string();
        options.adapters = std::move(adapters);
        options.weight_hashing.memo = bench.memo;
        options.context_size = shape.context_size;
        options.mmproj_path = shape.mmproj;
        if (shape.cache_type.has_value()) {
            options.cache_type = *shape.cache_type;
            options.cache_type_named = true;
        }
        provider =
            std::make_unique<LlamaCppProvider>(std::move(options), std::move(owned), bench.bases);
    }

    [[nodiscard]] std::string ask(const std::string& text) const {
        ChatRequest request;
        request.messages = {ChatMessage::user(text)};
        return provider->chat(request, {}).message.content.plain_text();
    }

    /// A one-off call on a throwaway context: a title, a clerk's question.
    [[nodiscard]] std::string side(const std::string& text) const {
        ChatRequest request;
        request.messages = {ChatMessage::user(text)};
        request.transient.side_request = true;
        return provider->chat(request, {}).message.content.plain_text();
    }

    /// A turn carrying a picture: a fresh context of its own.
    [[nodiscard]] std::string look(const std::string& text) const {
        ChatMessage asked = ChatMessage::user("");
        asked.content = apogee::harness::MessageContent::from_parts(
            {apogee::harness::ContentPart::from_image_url("data:image/png;base64,UE5H"),
             apogee::harness::ContentPart::from_text(text)});
        ChatRequest request;
        request.messages = {asked};
        return provider->chat(request, {}).message.content.plain_text();
    }

    [[nodiscard]] std::size_t embed(const std::string& text) const {
        return provider->embed({text}, {}).size();
    }
};

/// A scripted model, counted when freed.
[[nodiscard]] std::unique_ptr<LlamaModel> scripted(const std::shared_ptr<int>& freed) {
    auto model = std::make_unique<FakeLlamaModel>();
    model->freed = freed;
    return model;
}

[[nodiscard]] SharedBaseKey key_of(const std::string& model) {
    return SharedBaseKey{.base = apogee::models::base_key(model, "", KvCacheType::Q8_0, false),
                         .size = 1,
                         .modified = 1,
                         .gpu_layers = 999};
}

}  // namespace

TEST_CASE("the registry shares a held base, frees it with its last holder, and holds nothing",
          "[backends][llamacpp][shared-base]") {
    LlamaBases bases;
    const auto freed = std::make_shared<int>(0);
    int loads = 0;
    const LlamaBases::Loader load = [&loads, &freed](std::string& /*error*/) {
        ++loads;
        return scripted(freed);
    };
    std::string error;

    // A held base is the one handed out, loaded once.
    std::shared_ptr<SharedBase> first = bases.acquire(key_of("/m/base.gguf"), load, error);
    std::shared_ptr<SharedBase> second = bases.acquire(key_of("/m/base.gguf"), load, error);
    REQUIRE(first != nullptr);
    CHECK(first == second);
    CHECK(loads == 1);
    CHECK(bases.resident() == 1);

    // Another key is another base.
    std::shared_ptr<SharedBase> apart = bases.acquire(key_of("/m/other.gguf"), load, error);
    CHECK(apart != first);
    CHECK(loads == 2);
    CHECK(bases.resident() == 2);
    apart.reset();

    // Weak-held: the registry keeps nothing loaded, and the last holder's
    // letting go frees the model.
    first.reset();
    CHECK(*freed == 1);  // the other base
    CHECK(bases.resident() == 1);
    second.reset();
    CHECK(*freed == 2);
    CHECK(bases.resident() == 0);
    const std::shared_ptr<SharedBase> again = bases.acquire(key_of("/m/base.gguf"), load, error);
    CHECK(again != nullptr);
    CHECK(loads == 3);

    // No key -- a file that could not be read -- is never shared.
    const std::shared_ptr<SharedBase> alone = bases.acquire(std::nullopt, load, error);
    const std::shared_ptr<SharedBase> also = bases.acquire(std::nullopt, load, error);
    CHECK(alone != also);
    CHECK(loads == 5);
    CHECK(bases.resident() == 1);

    // A failed load holds nothing and says why.
    const LlamaBases::Loader failing = [](std::string& why) {
        why = "could not load the model";
        return std::unique_ptr<LlamaModel>{};
    };
    CHECK(bases.acquire(key_of("/m/broken.gguf"), failing, error) == nullptr);
    CHECK(error == "could not load the model");
    CHECK(bases.resident() == 1);
}

TEST_CASE("two loads landing for one base at once: the first is shared, the second freed",
          "[backends][llamacpp][shared-base]") {
    LlamaBases bases;
    const auto freed = std::make_shared<int>(0);
    std::string error;
    std::shared_ptr<SharedBase> inner;
    // The outer load runs with no lock held, so another acquire for the same
    // key completes inside it -- the race, made deterministic.
    const std::shared_ptr<SharedBase> outer = bases.acquire(
        key_of("/m/base.gguf"),
        [&](std::string& why) {
            inner = bases.acquire(
                key_of("/m/base.gguf"), [&freed](std::string&) { return scripted(freed); }, why);
            return scripted(freed);
        },
        error);
    REQUIRE(inner != nullptr);
    CHECK(outer == inner);
    CHECK(*freed == 1);
    CHECK(bases.resident() == 1);
}

TEST_CASE("a base's key is its file as the prompt cache knows it and everything it loads with",
          "[backends][llamacpp][shared-base]") {
    const Bench bench;
    ModelLoad load;
    load.path = bench.base.string();
    const std::optional<SharedBaseKey> key = apogee::backends::shared_base_key(load);
    REQUIRE(key.has_value());
    CHECK(key->base.model == bench.base.lexically_normal().string());
    CHECK(key->size == fs::file_size(bench.base));

    // Another spelling of the same file is the same base; its window's fit is
    // each entry's own, never the base's.
    ModelLoad same = load;
    same.path = (bench.dir.path() / "." / "base.gguf").string();
    same.fit_window = !load.fit_window;
    CHECK(apogee::backends::shared_base_key(same) == key);

    // Anything else it loads with is another load.
    const auto differs = [&load, &key](auto change) {
        ModelLoad changed = load;
        change(changed);
        return apogee::backends::shared_base_key(changed) != key;
    };
    CHECK(differs([](ModelLoad& l) { l.gpu_layers = 0; }));
    CHECK(differs([](ModelLoad& l) { l.mmproj_path = "/m/mmproj.gguf"; }));
    CHECK(differs([](ModelLoad& l) { l.cache_type = KvCacheType::F16; }));
    CHECK(differs([](ModelLoad& l) { l.cache_type_named = true; }));
    CHECK(differs([&bench](ModelLoad& l) { l.path = bench.other.string(); }));

    // The file rewritten is another base -- as the prompt cache tells it,
    // by its modification time when its size is the same -- and a file not
    // there has no key.
    fs::last_write_time(bench.base, fs::last_write_time(bench.base) + std::chrono::seconds{1});
    CHECK(apogee::backends::shared_base_key(load) != key);
    write(bench.base, "retrained base weights");
    CHECK(apogee::backends::shared_base_key(load) != key);
    load.path = (bench.dir.path() / "gone.gguf").string();
    CHECK_FALSE(apogee::backends::shared_base_key(load).has_value());
}

TEST_CASE("two entries on one base load it once, each context applying its own adapters",
          "[backends][llamacpp][shared-base]") {
    const Bench bench;
    const Entry chat{bench, "chat", {bench.pinned(bench.style, 0.5)}};
    const Entry helper{bench, "helper", {bench.pinned(bench.tone)}};
    CHECK(chat.ask("hello") == "answered");
    CHECK(helper.ask("hello") == "answered");

    // One load, by whichever entry came first; the other joined it.
    CHECK(chat.runtime->loads == 1);
    CHECK(helper.runtime->loads == 0);
    CHECK(helper.runtime->model == nullptr);
    CHECK(bench.bases->resident() == 1);
    CHECK(chat.provider->model_loaded());
    CHECK(helper.provider->model_loaded());

    // Each entry's set loaded over it once, and each conversation's context
    // applying its own entry's.
    const FakeLlamaModel& model = *chat.runtime->model;
    CHECK(model.adapter_sets ==
          std::vector<AdapterSet>{Bench::set(bench.style, 0.5), Bench::set(bench.tone)});
    REQUIRE(model.contexts.size() == 2);
    CHECK(model.contexts[0]->adapters == Bench::set(bench.style, 0.5));
    CHECK(model.contexts[1]->adapters == Bench::set(bench.tone));
}

TEST_CASE("no bleed: interleaved turns across entries on one base, each context's set pinned",
          "[backends][llamacpp][shared-base]") {
    const Bench bench;
    const Shape seeing{.mmproj = (bench.dir.path() / "mmproj.gguf").string()};
    const Entry styled{bench, "styled", {bench.pinned(bench.style, 0.5)}, seeing};
    const Entry toned{
        bench, "toned", {bench.pinned(bench.tone), bench.pinned(bench.style, 0.25)}, seeing};
    const Entry bare{bench, "bare", {}, seeing};
    const std::map<std::string, AdapterSet> sets{{"styled", Bench::set(bench.style, 0.5)},
                                                 {"toned",
                                                  {{.path = bench.tone.string(), .scale = 1.0},
                                                   {.path = bench.style.string(), .scale = 0.25}}},
                                                 {"bare", {}}};

    // The first turn loads the base; every context after is attributed to
    // the entry whose operation made it.
    CHECK(styled.ask("one") == "answered");
    FakeLlamaModel& model = *styled.runtime->model;
    std::vector<std::pair<std::string, std::shared_ptr<FakeLlamaContext>>> owned{
        {"styled", model.contexts.front()}};
    std::vector<AdapterSet> embedded;
    std::map<std::string, std::size_t> turns{{"styled", 1}};
    const auto run = [&](const Entry& entry, const std::string& name, char what) {
        const std::size_t before = model.contexts.size();
        switch (what) {
            case 't':
                (void)entry.ask(name + " turn");
                ++turns[name];
                break;
            case 's':
                CHECK(entry.side(name + " side") == "answered");
                break;
            case 'i':
                CHECK(entry.look(name + " look") == "answered");
                break;
            default:
                CHECK(entry.embed(name + " passage") == 1);
                embedded.push_back(sets.at(name));
                break;
        }
        for (std::size_t i = before; i < model.contexts.size(); ++i) {
            owned.emplace_back(name, model.contexts[i]);
        }
    };
    run(toned, "toned", 't');
    run(bare, "bare", 't');
    run(styled, "styled", 's');
    run(toned, "toned", 'e');
    run(styled, "styled", 't');
    run(bare, "bare", 'e');
    run(toned, "toned", 's');
    run(styled, "styled", 'e');
    run(bare, "bare", 's');
    run(toned, "toned", 't');
    run(styled, "styled", 'i');
    run(bare, "bare", 't');
    run(toned, "toned", 'i');
    run(bare, "bare", 'e');
    run(styled, "styled", 't');
    run(toned, "toned", 'e');

    CHECK(styled.runtime->loads == 1);
    CHECK(toned.runtime->loads + bare.runtime->loads == 0);
    // Three sessions, three side requests, two images: one context each.
    REQUIRE(owned.size() == 8);
    for (const auto& [name, context] : owned) {
        INFO(name);
        // Never a decode under another entry's set, nor under none for an
        // entry that has one, nor any other set ever asked of the context.
        REQUIRE_FALSE(context->decodes.empty());
        CHECK(context->decode_adapters.size() == context->decodes.size());
        for (const AdapterSet& under : context->decode_adapters) {
            CHECK(under == sets.at(name));
        }
        for (const AdapterSet& asked : context->asserted) {
            CHECK(asked == sets.at(name));
        }
        CHECK(context->adapters == sets.at(name));
    }
    // A conversation's context is set again on every turn, not trusted to
    // have kept it.
    for (const auto& [name, count] : turns) {
        const auto session = std::ranges::find_if(
            owned, [&owner = name](const auto& entry) { return entry.first == owner; });
        INFO(name);
        CHECK(session->second->asserted.size() >= count);
        CHECK(session->second->decodes.size() >= count);
    }
    // The one embedding context ran each call under its caller's set, the
    // bare base's included, nothing left over from the call before.
    CHECK(model.embedding_sets == embedded);
}

TEST_CASE("the base is freed when its last sharer lets go, and not before",
          "[backends][llamacpp][shared-base]") {
    const Bench bench;
    const std::shared_ptr<int> freed = [&bench] {
        const Entry chat{bench, "chat", {bench.pinned(bench.style)}};
        const Entry helper{bench, "helper", {bench.pinned(bench.tone)}};
        (void)chat.ask("hello");
        (void)helper.ask("hello");
        const std::shared_ptr<int> counted = chat.runtime->freed;

        // One lets go: the other still holds the base, and answers on it.
        chat.provider->unload();
        CHECK_FALSE(chat.provider->model_loaded());
        CHECK(*counted == 0);
        CHECK(bench.bases->resident() == 1);
        CHECK(helper.side("still here") == "answered");
        CHECK(chat.runtime->loads == 1);

        // It comes back to the base still held: its adapters load again over
        // it, nothing else does.
        CHECK(chat.ask("back") == "answered");
        CHECK(chat.runtime->loads == 1);
        CHECK(chat.runtime->model->adapter_sets ==
              std::vector<AdapterSet>{Bench::set(bench.style), Bench::set(bench.tone),
                                      Bench::set(bench.style)});

        // Both let go: the model goes with the last.
        helper.provider->unload();
        CHECK(*counted == 0);
        chat.provider->unload();
        CHECK(*counted == 1);
        CHECK(bench.bases->resident() == 0);

        // The next use loads it again, whichever entry asks.
        (void)helper.ask("anew");
        CHECK(helper.runtime->loads == 1);
        return helper.runtime->freed;
    }();
    // A provider's end lets go of its share as an unload does.
    CHECK(*freed == 1);
    CHECK(bench.bases->resident() == 0);

    const Entry first{bench, "first", {}};
    auto second = std::make_unique<Entry>(bench, "second", std::vector<AdapterConfig>{});
    (void)first.ask("hello");
    (void)second->ask("hello");
    const std::shared_ptr<int> counted = first.runtime->freed;
    second.reset();
    CHECK(*counted == 0);
    CHECK(first.side("alone now") == "answered");
    first.provider->unload();
    CHECK(*counted == 1);
}

TEST_CASE("entries on distinct bases, or loading one otherwise, share nothing",
          "[backends][llamacpp][shared-base]") {
    const Bench bench;
    const auto loads_apart = [&bench](const Shape& first, const Shape& second) {
        const Entry one{bench, "one", {}, first};
        const Entry two{bench, "two", {}, second};
        (void)one.ask("hello");
        (void)two.ask("hello");
        const bool apart = one.runtime->loads == 1 && two.runtime->loads == 1 &&
                           one.runtime->model != two.runtime->model;
        return apart;
    };
    CHECK(loads_apart({}, {.model = bench.other}));
    CHECK(loads_apart({}, {.mmproj = (bench.dir.path() / "mmproj.gguf").string()}));
    CHECK(loads_apart({}, {.cache_type = KvCacheType::F16}));
    // A named cache type is used or refused; the default gives way. Not one
    // load, even at the same type.
    CHECK(loads_apart({}, {.cache_type = KvCacheType::Q8_0}));
    // A file that cannot be read has no key: each loads, and fails, alone.
    const fs::path gone = bench.dir.path() / "gone.gguf";
    CHECK(loads_apart({.model = gone}, {.model = gone}));
    // And the plain case shares, as the cases above do not.
    CHECK_FALSE(loads_apart({}, {}));
}

TEST_CASE("a base rewritten on disk while one entry holds it is another base",
          "[backends][llamacpp][shared-base]") {
    const Bench bench;
    const Entry first{bench, "first", {}};
    (void)first.ask("hello");
    write(bench.base, "retrained base weights");
    const Entry second{bench, "second", {}};
    (void)second.ask("hello");
    CHECK(second.runtime->loads == 1);
    CHECK(bench.bases->resident() == 2);
    // The first still answers on what it loaded.
    CHECK(first.side("again") == "answered");
    CHECK(first.runtime->loads == 1);
}

TEST_CASE("an entry joining a loaded base fits its own default window, as its own load would",
          "[backends][llamacpp][shared-base]") {
    const Bench bench;
    const std::int64_t trained = 4096;

    SECTION("joining a base loaded at a written window") {
        const Entry pinned{bench, "pinned", {}, {.context_size = 2048}};
        const Entry roomy{bench, "roomy", {}};
        roomy.runtime->fitted = 3000;
        (void)pinned.ask("hello");
        (void)roomy.ask("hello");
        CHECK(roomy.runtime->loads == 0);
        CHECK(roomy.runtime->fits == 1);
        CHECK(pinned.runtime->fits == 0);
        CHECK(pinned.runtime->model->context_sizes ==
              std::vector<std::int64_t>{2048, apogee::models::default_local_window(trained, 3000)});
        CHECK(roomy.provider->context_window() == 3000);
    }
    SECTION("joining a base another default window fitted: its own measure, not that one") {
        const Entry loader{bench, "loader", {}};
        const Entry joiner{bench, "joiner", {}};
        loader.runtime->fitted = 2500;
        joiner.runtime->fitted = 1800;
        (void)loader.ask("hello");
        (void)joiner.ask("hello");
        // The loader's came with its load; the joiner measured its own.
        CHECK(loader.runtime->fits == 0);
        CHECK(joiner.runtime->fits == 1);
        CHECK(loader.provider->context_window() == 2500);
        CHECK(joiner.provider->context_window() == 1800);
    }
    SECTION("a written window joins with nothing to measure") {
        const Entry loader{bench, "loader", {}};
        const Entry written{bench, "written", {}, {.context_size = 1024}};
        (void)loader.ask("hello");
        (void)written.ask("hello");
        CHECK(written.runtime->fits == 0);
        CHECK(written.provider->context_window() == 1024);
    }
}

TEST_CASE("an adapter that will not load over a shared base fails its entry alone",
          "[backends][llamacpp][shared-base][refusal]") {
    const Bench bench;
    const Entry chat{bench, "chat", {bench.pinned(bench.style)}};
    const Entry helper{bench, "helper", {bench.pinned(bench.tone)}};
    (void)chat.ask("hello");
    chat.runtime->model->adapter_error_path = bench.tone.string();
    CHECK_THROWS_WITH(helper.ask("hello"), ContainsSubstring("could not load the adapter at '" +
                                                             bench.tone.string() + "'"));
    CHECK_FALSE(helper.provider->model_loaded());
    CHECK(chat.side("still") == "answered");
    CHECK(chat.runtime->loads == 1);
    CHECK(bench.bases->resident() == 1);
}

TEST_CASE("entries sharing a base take turns on it from several threads",
          "[backends][llamacpp][shared-base][threads]") {
    const Bench bench;
    const Entry chat{bench, "chat", {bench.pinned(bench.style)}};
    const Entry helper{bench, "helper", {bench.pinned(bench.tone)}};
    const Entry clerk{bench, "clerk", {}};
    (void)chat.ask("warm");
    (void)helper.ask("warm");
    (void)clerk.ask("warm");
    FakeLlamaModel& model = *chat.runtime->model;
    model.overlap->hold = std::chrono::milliseconds{2};

    // Three entries on one base, each driven by a thread of its own -- one
    // embedding, one taking turns, one asking side questions -- each busy
    // throughout: anything not serialized on the base collides.
    std::vector<std::string> answers;
    std::vector<std::string> titles;
    std::size_t vectors = 0;
    {
        std::thread embedding{[&chat, &vectors] {
            for (int round = 0; round < 12; ++round) {
                vectors += chat.embed("passage " + std::to_string(round));
            }
        }};
        std::thread turns{[&helper, &answers] {
            for (int round = 0; round < 6; ++round) {
                (void)helper.ask("turn " + std::to_string(round));
                answers.push_back(helper.side("again " + std::to_string(round)));
            }
        }};
        std::thread sides{[&clerk, &titles] {
            for (int round = 0; round < 6; ++round) {
                titles.push_back(clerk.side("title " + std::to_string(round)));
            }
        }};
        embedding.join();
        turns.join();
        sides.join();
    }
    CHECK(vectors == 12);
    CHECK(answers == std::vector<std::string>(6, "answered"));
    CHECK(titles == std::vector<std::string>(6, "answered"));
    // Never two decodes, or an embedding batch and a decode, on the model at once.
    CHECK(model.overlap->most.load() == 1);
    CHECK(chat.runtime->loads == 1);
}
