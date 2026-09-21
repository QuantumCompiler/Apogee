#include "backends/llamacpp_embed.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "backends/llamacpp.h"
#include "harness/errors.h"
#include "harness/harness.h"
#include "support/fake_llama.h"

/// In-process embeddings, through the provider and the scripted runtime. The
/// merge-blocking target has no llama.cpp in it, so this is the only place the
/// provider side of embedding is tested at all; the real runtime's packing is
/// exercised by the llama-enabled CI job.
namespace {

using apogee::backends::LlamaCppProvider;
using apogee::harness::ProviderError;
using apogee::testing::FakeLlamaRuntime;

struct Fixture {
    FakeLlamaRuntime* runtime = nullptr;
    std::unique_ptr<LlamaCppProvider> provider;

    explicit Fixture(std::size_t width = 4) {
        auto owned = std::make_unique<FakeLlamaRuntime>();
        runtime = owned.get();
        LlamaCppProvider::Options options;
        options.backend_name = "local";
        options.model = "embedder";
        options.model_path = "/models/embedder.gguf";
        provider = std::make_unique<LlamaCppProvider>(std::move(options), std::move(owned));
        (void)width;
    }
};

}  // namespace

TEST_CASE("a local model embeds a batch of texts, one vector each, in order",
          "[backends][llamacpp][embed]") {
    Fixture f;
    const auto vectors = f.provider->embed({"alpha", "beta", "alpha"}, {});
    REQUIRE(vectors.size() == 3);
    CHECK(vectors[0].size() == 4);
    // Deterministic from the text: the same input embeds the same way and a
    // different one does not.
    CHECK(vectors[0] == vectors[2]);
    CHECK(vectors[0] != vectors[1]);
}

TEST_CASE("texts are handed to the runtime in bounded slices",
          "[backends][llamacpp][embed][batch]") {
    // 130 inputs at 64 per call is three calls of 64, 64, 2 -- and never one
    // call of 130, which is how Ctrl-C during an ingest would go unheard until
    // the whole corpus was done.
    Fixture f;
    std::vector<std::string> inputs(130, "x");
    const auto vectors = f.provider->embed(inputs, {});
    CHECK(vectors.size() == 130);
    REQUIRE(f.runtime->model != nullptr);
    CHECK(f.runtime->model->embed_batches == std::vector<std::size_t>{64, 64, 2});
}

TEST_CASE("an empty input embeds nothing and touches no context", "[backends][llamacpp][embed]") {
    Fixture f;
    CHECK(f.provider->embed({}, {}).empty());
    // The model loads (that is what `embed` means), but no batch was sent.
    REQUIRE(f.runtime->model != nullptr);
    CHECK(f.runtime->model->embed_batches.empty());
}

TEST_CASE("dimensions are unknown before the model loads and exact after",
          "[backends][llamacpp][embed]") {
    // 0 is the honest answer before a load: answering it by loading would turn
    // every capability probe into a multi-gigabyte page-in.
    Fixture f;
    CHECK(f.provider->embedding_dimensions() == 0);
    (void)f.provider->embed({"alpha"}, {});
    CHECK(f.provider->embedding_dimensions() == 4);
}

TEST_CASE("a runtime failure surfaces as a provider error with its message",
          "[backends][llamacpp][embed][error]") {
    Fixture f;
    (void)f.provider->embed({"warm"}, {});  // load, so the fake model exists
    f.runtime->model->embed_error = "no pooled embedding came back";
    CHECK_THROWS_AS(f.provider->embed({"alpha"}, {}), ProviderError);
    try {
        (void)f.provider->embed({"alpha"}, {});
    } catch (const ProviderError& e) {
        CHECK(std::string{e.what()}.find("no pooled embedding") != std::string::npos);
    }
}

TEST_CASE("a local entry answers can_embed through the harness",
          "[backends][llamacpp][embed][capability]") {
    Fixture f;
    apogee::harness::Harness harness{apogee::harness::Config{}};
    harness.register_provider("local", std::shared_ptr<LlamaCppProvider>(std::move(f.provider)));
    harness.use_default_router();
    CHECK(harness.can_embed("local"));
}

TEST_CASE("two models of different width are distinguishable by their dimensions",
          "[backends][llamacpp][embed][fixture]") {
    // The dimension-mismatch fixture for vector-hybrid-rerank's per-store
    // binding: two embedders, two widths, and a store must refuse to hold
    // vectors from both.
    Fixture narrow;
    (void)narrow.provider->embed({"x"}, {});
    Fixture wide;
    (void)wide.provider->embed({"x"}, {});
    wide.runtime->model->embedding_width = 8;
    CHECK(narrow.provider->embedding_dimensions() == 4);
    CHECK(wide.provider->embedding_dimensions() == 8);
    CHECK(wide.provider->embed({"x"}, {}).front().size() == 8);
}

TEST_CASE("an embedding is a use of the model for the idle timer",
          "[backends][llamacpp][embed][idle]") {
    // A long ingest is nothing but embedding calls. If those did not count as
    // use, the idle timer would unload the weights under a running ingest and
    // every later batch would pay for a reload.
    auto owned = std::make_unique<FakeLlamaRuntime>();
    auto* runtime = owned.get();
    auto now = std::chrono::steady_clock::now();
    LlamaCppProvider::Options options;
    options.backend_name = "local";
    options.model_path = "/models/embedder.gguf";
    options.idle_unload = std::chrono::seconds{60};
    options.clock = [&now] { return now; };
    LlamaCppProvider provider{std::move(options), std::move(owned)};

    (void)provider.embed({"first batch"}, {});
    CHECK(runtime->loads == 1);

    // Forty seconds on, embed again: inside the window, so still resident --
    // and the timer restarts from HERE.
    now += std::chrono::seconds{40};
    (void)provider.embed({"second batch"}, {});
    CHECK(runtime->loads == 1);

    // Another forty: eighty since the first call, forty since the last. An
    // embed that did not count as use would have unloaded at sixty.
    now += std::chrono::seconds{40};
    (void)provider.embed({"third batch"}, {});
    CHECK(runtime->loads == 1);

    // And past the window with nothing in between, it does unload.
    now += std::chrono::seconds{120};
    (void)provider.embed({"much later"}, {});
    CHECK(runtime->loads == 2);
}
