#include "backends/llamacpp.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <system_error>
#include <vector>

#include "backends/llamacpp_tokens.h"
#include "contracts/errors.h"
#include "modelstore/gguf_inspect.h"
#include "support/fake_llama.h"
#include "support/gguf_builder.h"

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

}  // namespace

TEST_CASE("a second turn decodes only the new tokens", "[backends][llamacpp][kv]") {
    // THE acceptance criterion for this item: two successive calls against one
    // session process only new prompt tokens. Without a warm KV cache, turn two
    // re-decodes the entire conversation, and the whole in-process argument
    // over a spawn-per-turn model collapses.
    Fixture fixture;

    const auto first = fixture.provider->chat(turn({ChatMessage::user("alpha beta")}), {});
    REQUIRE(fixture.runtime->model != nullptr);
    REQUIRE(fixture.runtime->model->contexts.size() == 1);
    auto session = fixture.runtime->model->contexts.front();

    const std::int64_t after_first = session->prompt_tokens_decoded();
    REQUIRE(after_first > 0);

    // Turn two carries the whole prior exchange plus one new message.
    const auto second = fixture.provider->chat(
        turn({ChatMessage::user("alpha beta"), ChatMessage::assistant(first.message.content),
              ChatMessage::user("gamma")}),
        {});

    // Still ONE context: the conversation did not start over.
    CHECK(fixture.runtime->model->contexts.size() == 1);

    const std::int64_t turn_two = session->prompt_tokens_decoded() - after_first;

    // The claim, stated exactly: turn two decoded strictly FEWER tokens than
    // its own prompt contains. That is what "only new prompt tokens are
    // processed" means, and it is the assertion a broken cache fails -- with no
    // reuse, these two numbers are equal.
    //
    // (Comparing turn two against turn ONE would be the wrong test: with this
    // conversation the new suffix happens to be the same length as the first
    // prompt, so a working cache still reads as "no better".)
    CHECK(turn_two < second.usage.prompt_tokens);
    CHECK(turn_two > 0);

    // And the tokens it skipped are precisely the ones the trim kept.
    REQUIRE_FALSE(session->trims.empty());
    const std::int64_t reused = session->trims.back();
    CHECK(reused > 0);
    CHECK(turn_two == second.usage.prompt_tokens - reused);
}

TEST_CASE("a side request never touches the session KV", "[backends][llamacpp][kv]") {
    // The side-request hazard: an async titler that runs with the session's
    // prompt-cache flags can clobber the KV state of the very conversation
    // it is summarising -- and write the same file concurrently with the next
    // turn. Here a side request must run somewhere else entirely.
    Fixture fixture;

    (void)fixture.provider->chat(turn({ChatMessage::user("alpha beta gamma")}), {});
    auto session = fixture.runtime->model->contexts.front();
    const std::int64_t before = session->eval_count();
    const std::size_t trims_before = session->trims.size();
    REQUIRE(before > 0);

    ChatRequest background = turn({ChatMessage::user("summarise this conversation")});
    background.transient.side_request = true;
    (void)fixture.provider->chat(background, {});

    // It ran on its own context...
    REQUIRE(fixture.runtime->model->contexts.size() == 2);
    CHECK(fixture.runtime->model->contexts.back()->eval_count() > 0);

    // ...and the session's was not decoded into, or even trimmed.
    CHECK(session->eval_count() == before);
    CHECK(session->trims.size() == trims_before);

    // The model itself was loaded once: an isolated context is cheap precisely
    // because it shares the weights.
    CHECK(fixture.runtime->loads == 1);
}

TEST_CASE("a side request leaves the next real turn's cache warm", "[backends][llamacpp][kv]") {
    // The half that would still be broken if isolation only meant "a different
    // context": if the side request had disturbed the remembered prefix, the
    // turn after it would re-ingest the whole conversation.
    Fixture fixture;

    (void)fixture.provider->chat(turn({ChatMessage::user("alpha beta")}), {});
    auto session = fixture.runtime->model->contexts.front();
    const std::int64_t after_first = session->prompt_tokens_decoded();

    ChatRequest background = turn({ChatMessage::user("title please")});
    background.transient.side_request = true;
    (void)fixture.provider->chat(background, {});

    (void)fixture.provider->chat(
        turn({ChatMessage::user("alpha beta"), ChatMessage::user("gamma")}), {});

    const std::int64_t third = session->prompt_tokens_decoded() - after_first;
    CHECK(third > 0);
    // Something was still reusable, so the side request did not cost the
    // conversation its warm prefix.
    REQUIRE_FALSE(session->trims.empty());
    CHECK(session->trims.back() > 0);
}

TEST_CASE("a transient token is never reused by the turn after it",
          "[backends][llamacpp][kv][transient]") {
    // The item's constraint: RAG content spliced into one request must not
    // contaminate the reusable prefix. It holds because the match is
    // token-for-token -- a turn can only reuse a cached token its own prompt
    // actually contains -- so the moment the injected block stops being sent,
    // the shared prefix ends there and every transient token is trimmed away.
    //
    // This asserts that end state rather than any bookkeeping: the reused
    // prefix after a RAG turn is no longer than the durable messages that
    // preceded the injection.
    Fixture fixture;

    ChatRequest with_rag =
        turn({ChatMessage::user("alpha"), ChatMessage::system("retrieved zeta eta theta"),
              ChatMessage::user("beta")});
    with_rag.transient.start = 1;
    with_rag.transient.length = 1;
    REQUIRE(with_rag.transient.has_region());

    (void)fixture.provider->chat(with_rag, {});
    auto session = fixture.runtime->model->contexts.front();

    // The next turn carries only the durable history -- no RAG block.
    const std::size_t trims_before = session->trims.size();
    (void)fixture.provider->chat(turn({ChatMessage::user("alpha"), ChatMessage::user("beta")}), {});
    REQUIRE(session->trims.size() > trims_before);

    const auto durable_head = apogee::backends::llama_tokens::tokenize_prompt(
        *fixture.runtime->model, "test-model", {ChatMessage::user("alpha")}, false);
    CHECK(session->trims.back() <= static_cast<std::int64_t>(durable_head.size()));
}

TEST_CASE("a reused prefix never claims more than the cache holds", "[backends][llamacpp][kv]") {
    // The failure mode that makes an over-long prefix dangerous rather than
    // merely slow: claiming tokens the KV does not hold means those positions
    // are never decoded, and the model answers from a context with holes in it.
    // Every trim must land inside what was actually decoded.
    Fixture fixture;

    (void)fixture.provider->chat(turn({ChatMessage::user("alpha beta gamma")}), {});
    (void)fixture.provider->chat(
        turn({ChatMessage::user("alpha beta gamma"), ChatMessage::user("delta")}), {});
    (void)fixture.provider->chat(turn({ChatMessage::user("wholly different")}), {});

    auto session = fixture.runtime->model->contexts.front();
    std::int64_t decoded_through = 0;
    for (std::size_t i = 0; i < session->decodes.size(); ++i) {
        const auto& record = session->decodes[i];
        CHECK(record.position <= decoded_through);
        decoded_through = record.position + record.count;
    }
}

TEST_CASE("a model whose memory cannot be rewound never decodes on from an uncut prefix",
          "[backends][llamacpp][kv]") {
    // Qwen3.5 on 2026-09-23: its linear-attention layers keep a running state
    // llama.cpp can rewind only a few tokens, and a thinking model's template
    // re-renders the last answer without its reasoning -- so turn two's prompt
    // parts from the cache early, the trim is refused, and decoding on from
    // the shared prefix failed every second turn ("for M-RoPE, it is required
    // that the position satisfies: X < Y"). The cache is cleared then -- or,
    // since 25c, a checkpoint at or before the prefix restored -- and the
    // prompt decodes from exactly where the cache really ends.
    Fixture fixture;
    fixture.runtime->rewindable = false;

    // Turn one generates, so the cache holds tokens past its prompt.
    (void)fixture.provider->chat(turn({ChatMessage::user("prime thinking answer")}), {});
    auto session = fixture.runtime->model->contexts.front();
    session->script = {fixture.runtime->model->id_for("thinking"),
                       fixture.runtime->model->id_for("answer")};
    session->sampled = 0;
    (void)fixture.provider->chat(turn({ChatMessage::user("alpha beta")}), {});
    const std::size_t decodes_before = session->decodes.size();

    // The answer comes back re-rendered differently from what was generated,
    // as a template that strips reasoning does.
    const auto second = fixture.provider->chat(
        turn({ChatMessage::user("alpha beta"), ChatMessage::assistant("restated"),
              ChatMessage::user("gamma")}),
        {});
    REQUIRE(session->decodes.size() > decodes_before);
    // A shared prefix was found, and asked for...
    REQUIRE_FALSE(session->trims.empty());
    CHECK(session->trims.back() > 0);
    // ...but the cache could not be cut there: it went back to a checkpoint
    // at or before the prefix, and the prompt decoded on from exactly there.
    const apogee::backends::DecodeRecord& prompt = session->decodes.at(decodes_before);
    const std::int64_t restored = session->restores.empty() ? 0 : session->restores.back();
    CHECK(restored <= session->trims.back());
    CHECK(prompt.position == restored);
    std::int64_t decoded = 0;
    for (std::size_t i = decodes_before; i < session->decodes.size(); ++i) {
        if (session->decodes[i].position < second.usage.prompt_tokens) {
            decoded += session->decodes[i].count;
        }
    }
    CHECK(decoded == second.usage.prompt_tokens - restored);
}

TEST_CASE("a model whose memory cannot be rewound still reuses a prompt that only extends",
          "[backends][llamacpp][kv]") {
    // Nothing to cut, nothing refused: a turn that adds to what is cached
    // decodes only what is new, rewindable or not.
    Fixture fixture;
    fixture.runtime->rewindable = false;

    const auto first = fixture.provider->chat(turn({ChatMessage::user("alpha beta")}), {});
    auto session = fixture.runtime->model->contexts.front();
    const std::int64_t after_first = session->prompt_tokens_decoded();
    const auto second = fixture.provider->chat(
        turn({ChatMessage::user("alpha beta"), ChatMessage::assistant(first.message.content),
              ChatMessage::user("gamma")}),
        {});
    CHECK(session->prompt_tokens_decoded() - after_first < second.usage.prompt_tokens);
}

TEST_CASE("usage is exact on both sides", "[backends][llamacpp][usage]") {
    // A local tokenizer is the model's own, so these are measurements rather
    // than the characters/4 estimate every other path falls back to.
    Fixture fixture{{}};
    fixture.runtime->script = {};

    const auto response = fixture.provider->chat(turn({ChatMessage::user("one two three")}), {});

    CHECK(response.usage.prompt_tokens > 0);
    CHECK(response.usage.reported());
    CHECK(response.model == "test-model");
}

TEST_CASE("the counting API answers exactly once the model is warm",
          "[backends][llamacpp][usage]") {
    // The acceptance criterion, and the reason chat.cpp's context monitor stops
    // saying "(estimated)" on a local backend.
    Fixture fixture;

    const ChatRequest request = turn({ChatMessage::user("alpha beta gamma delta")});

    // Cold: declines rather than paging a multi-gigabyte model off disk to
    // answer a display detail.
    CHECK(fixture.provider->count_prompt_tokens(request) == -1);
    CHECK_FALSE(fixture.provider->model_loaded());

    (void)fixture.provider->chat(request, {});

    REQUIRE(fixture.provider->model_loaded());
    const std::int64_t exact = fixture.provider->count_prompt_tokens(request);
    CHECK(exact > 0);
    // Same request, same answer: a count that drifted between calls would make
    // the 80/90 thresholds fire unpredictably.
    CHECK(fixture.provider->count_prompt_tokens(request) == exact);
}

TEST_CASE("a load failure names the file and does not crash", "[backends][llamacpp][errors]") {
    // "A load failure yields a clear error naming the file, never a crash" --
    // stated in the acceptance criteria because a bad path is the single most
    // likely local-backend mistake, and 'could not load model' with no path
    // sends the user hunting through their config for the wrong key.
    Fixture fixture;
    fixture.runtime->load_error =
        "could not load the model at '/models/test.gguf' -- check that "
        "the file exists and is a valid GGUF";

    REQUIRE_THROWS_AS(fixture.provider->chat(turn({ChatMessage::user("hi")}), {}),
                      apogee::harness::ProviderError);

    try {
        (void)fixture.provider->chat(turn({ChatMessage::user("hi")}), {});
    } catch (const apogee::harness::ProviderError& error) {
        const std::string message = error.what();
        CHECK(message.find("/models/test.gguf") != std::string::npos);
        CHECK(message.find("local") != std::string::npos);
    }
}

TEST_CASE("a backend with no model_path refuses with an actionable message",
          "[backends][llamacpp][errors]") {
    apogee::harness::BackendConfig config;
    config.type = apogee::harness::BackendType::LlamaCpp;

    try {
        (void)LlamaCppProvider::from_config("local", config);
        FAIL("expected a ProviderError");
    } catch (const apogee::harness::ProviderError& error) {
        const std::string message = error.what();
        CHECK(message.find("model_path") != std::string::npos);
    }
}

TEST_CASE("the vision capability answers from configured state",
          "[backends][llamacpp][capability]") {
    // The truth table, over the injected runtime rather than over weights: the
    // whole point of asking from CONFIGURATION is that the answer costs nothing
    // and is available before a turn starts. Loading 16 GB to answer a yes/no
    // question would make every --image check pay for a model load.
    //
    // Note what this asserts in a build WITHOUT llama.cpp: false either way,
    // because there is no mtmd to use. That is the third row of the table and
    // it is the row this test actually exercises on the merge-blocking target.
    SECTION("no mmproj configured") {
        Fixture fixture;
        CHECK_FALSE(fixture.provider->accepts_images());
    }

    SECTION("an mmproj configured") {
        auto owned = std::make_unique<FakeLlamaRuntime>();
        LlamaCppProvider::Options options;
        options.backend_name = "local";
        options.model_path = "/models/model.gguf";
        options.mmproj_path = "/models/mmproj.gguf";
        LlamaCppProvider provider{std::move(options), std::move(owned)};

        // True only when this build HAS llama.cpp: a projector path alone
        // cannot make a build that lacks mtmd able to read a picture.
        CHECK(provider.accepts_images() == apogee::backends::llama_available());
    }
}

TEST_CASE("the configured projector reaches the runtime", "[backends][llamacpp][capability]") {
    // The field must actually be plumbed. A config key that parses, displays,
    // and is dropped on the way to the loader is the quiet failure here.
    auto owned = std::make_unique<FakeLlamaRuntime>();
    FakeLlamaRuntime* runtime = owned.get();

    LlamaCppProvider::Options options;
    options.backend_name = "local";
    options.model_path = "/models/model.gguf";
    options.mmproj_path = "/models/mmproj.gguf";
    LlamaCppProvider provider{std::move(options), std::move(owned)};

    (void)provider.list_models({});
    (void)provider.chat(turn({ChatMessage::user("hello")}), {});

    CHECK(runtime->last_mmproj_path == "/models/mmproj.gguf");
}

TEST_CASE("an idle model unloads and reloads on the next request", "[backends][llamacpp][idle]") {
    auto owned = std::make_unique<FakeLlamaRuntime>();
    auto* runtime = owned.get();

    auto now = std::chrono::steady_clock::now();
    LlamaCppProvider::Options options;
    options.backend_name = "local";
    options.model_path = "/models/test.gguf";
    options.idle_unload = std::chrono::seconds{60};
    options.clock = [&now] { return now; };
    LlamaCppProvider provider{std::move(options), std::move(owned)};

    (void)provider.chat(turn({ChatMessage::user("hello")}), {});
    CHECK(provider.model_loaded());
    CHECK(runtime->loads == 1);

    // Inside the window: still resident.
    now += std::chrono::seconds{30};
    (void)provider.chat(turn({ChatMessage::user("hello again")}), {});
    CHECK(runtime->loads == 1);

    // Past it: the next request pays for a reload, and the 16GB is given back.
    now += std::chrono::seconds{120};
    (void)provider.chat(turn({ChatMessage::user("much later")}), {});
    CHECK(runtime->loads == 2);
}

TEST_CASE("a held model outlasts its idle window, and the clock rules again once let go",
          "[backends][llamacpp][idle][residency]") {
    // 27e: a session using this model holds it -- two turns further apart
    // than the window, and no reload. Let go, the window counts from the last
    // use, at the next request as ever.
    auto owned = std::make_unique<FakeLlamaRuntime>();
    auto* runtime = owned.get();
    auto now = std::chrono::steady_clock::now();
    LlamaCppProvider::Options options;
    options.backend_name = "local";
    options.model_path = "/models/test.gguf";
    options.idle_unload = std::chrono::seconds{60};
    options.clock = [&now] { return now; };
    LlamaCppProvider provider{std::move(options), std::move(owned)};

    provider.hold_resident(true);
    CHECK(provider.held_resident());
    (void)provider.chat(turn({ChatMessage::user("hello")}), {});
    now += std::chrono::seconds{120};
    (void)provider.embed({"a question"}, {});
    (void)provider.chat(turn({ChatMessage::user("much later")}), {});
    CHECK(runtime->loads == 1);
    CHECK(provider.model_loaded());

    provider.hold_resident(false);
    CHECK_FALSE(provider.held_resident());
    now += std::chrono::seconds{30};
    (void)provider.chat(turn({ChatMessage::user("inside the window")}), {});
    CHECK(runtime->loads == 1);
    now += std::chrono::seconds{61};
    (void)provider.chat(turn({ChatMessage::user("past it")}), {});
    CHECK(runtime->loads == 2);
}

TEST_CASE("idle unload is off by default", "[backends][llamacpp][idle]") {
    // A resident model is the point of an in-process backend. Unloading has to
    // be asked for, or a long chat pays a reload it never requested.
    Fixture fixture;
    (void)fixture.provider->chat(turn({ChatMessage::user("hello")}), {});
    (void)fixture.provider->chat(turn({ChatMessage::user("hello again")}), {});
    CHECK(fixture.runtime->loads == 1);
}

TEST_CASE("streamed tokens arrive and reassemble into the answer", "[backends][llamacpp][stream]") {
    Fixture fixture;
    // Prime the vocabulary by running one turn, then script those ids back as
    // the model's output -- the fake only knows a token's text once it has
    // tokenized the word.
    (void)fixture.provider->chat(turn({ChatMessage::user("hello world")}), {});
    REQUIRE(fixture.runtime->model != nullptr);
    const std::int32_t hello = fixture.runtime->model->id_for("hello");
    const std::int32_t world = fixture.runtime->model->id_for("world");
    fixture.runtime->model->contexts.front()->script = {hello, world};
    fixture.runtime->model->contexts.front()->sampled = 0;

    std::string streamed;
    apogee::harness::StreamOptions options;
    options.on_token = [&streamed](std::string_view chunk) { streamed += chunk; };

    const auto response =
        fixture.provider->stream_chat(turn({ChatMessage::user("hello world again")}), options);

    CHECK(streamed == response.message.content.plain_text());
    CHECK(response.usage.completion_tokens == 2);
}

TEST_CASE("generation stops at the cap and says it was truncated", "[backends][llamacpp][stream]") {
    // A surface that shows a truncated answer as complete is lying to the user.
    Fixture fixture;
    (void)fixture.provider->chat(turn({ChatMessage::user("prime the vocabulary")}), {});
    const std::int32_t token = fixture.runtime->model->id_for("prime");

    // A context that never returns end-of-generation.
    fixture.runtime->model->contexts.front()->script.assign(100, token);
    fixture.runtime->model->contexts.front()->sampled = 0;

    ChatRequest request = turn({ChatMessage::user("go")});
    request.max_tokens = 5;

    const auto response = fixture.provider->chat(request, {});
    CHECK(response.usage.completion_tokens == 5);
    CHECK(response.finish_reason == apogee::harness::FinishReason::Length);
}

TEST_CASE("cancellation is honoured between tokens", "[backends][llamacpp][stream]") {
    // Not merely at entry: a local model generating a long answer is exactly
    // when a user reaches for Ctrl-C.
    Fixture fixture;
    (void)fixture.provider->chat(turn({ChatMessage::user("prime the vocabulary")}), {});
    const std::int32_t token = fixture.runtime->model->id_for("prime");
    fixture.runtime->model->contexts.front()->script.assign(100, token);
    fixture.runtime->model->contexts.front()->sampled = 0;

    const auto cancellation = apogee::harness::CancellationToken::create();

    int seen = 0;
    apogee::harness::StreamOptions options;
    options.cancellation = cancellation;
    options.on_token = [&seen, &cancellation](std::string_view) {
        if (++seen == 3) {
            cancellation.cancel();
        }
    };

    CHECK_THROWS_AS(fixture.provider->stream_chat(turn({ChatMessage::user("go")}), options),
                    apogee::harness::CancelledError);
    CHECK(seen == 3);
}

TEST_CASE("the model load reports status through the sink", "[backends][llamacpp][status]") {
    Fixture fixture;

    std::vector<apogee::harness::StatusEvent> events;
    apogee::harness::StreamOptions options;
    options.on_status = [&events](const apogee::harness::StatusEvent& event) {
        // The load's own events; the turn's cache report follows them.
        if (event.type != apogee::harness::StatusEvent::Type::PromptCache) {
            events.push_back(event);
        }
    };

    (void)fixture.provider->stream_chat(turn({ChatMessage::user("hi")}), options);

    REQUIRE(events.size() >= 2);
    CHECK(events.front().type == apogee::harness::StatusEvent::Type::ModelLoading);
    CHECK(events.back().type == apogee::harness::StatusEvent::Type::ModelReady);
}

TEST_CASE("list_models reports the one GGUF it was pointed at", "[backends][llamacpp]") {
    Fixture fixture;
    const auto models = fixture.provider->list_models({});
    REQUIRE(models.size() == 1);
    CHECK(models.front().provider == "llamacpp");
    CHECK(models.front().backend == "local");
}

TEST_CASE("a prompt longer than one batch is fed in pieces", "[backends][llamacpp][batch]") {
    // llama.cpp caps a single decode call and rejects anything larger rather
    // than splitting it. Without chunking the backend works on short prompts
    // and fails the first time someone pastes a file -- with a message about KV
    // slots that points nowhere near the cause.
    Fixture fixture;
    fixture.runtime->batch_limit = 3;

    (void)fixture.provider->chat(
        turn({ChatMessage::user("one two three four five six seven eight nine ten")}), {});

    auto session = fixture.runtime->model->contexts.front();
    REQUIRE_FALSE(session->decodes.empty());

    // No single decode exceeded the cap...
    for (const apogee::backends::DecodeRecord& record : session->decodes) {
        CHECK(record.count <= 3);
    }
    // ...and it genuinely took several, rather than the prompt being truncated.
    CHECK(session->decodes.size() > 1);

    // The pieces are contiguous: a gap would leave holes in the KV cache and
    // the model would answer from a prompt with missing spans.
    std::int64_t expected = 0;
    for (const apogee::backends::DecodeRecord& record : session->decodes) {
        CHECK(record.position == expected);
        expected = record.position + record.count;
    }
}

TEST_CASE("generation stops at the context wall instead of decoding into it",
          "[backends][llamacpp][limits]") {
    // Found on real hardware, not by this fake: `apogee chat`'s background
    // title request carries no max_tokens, so it ran to the provider default,
    // and a model that never emits end-of-generation filled the KV cache and
    // threw -- killing the turn. A truncated title is a non-event; an exception
    // mid-conversation is not.
    Fixture fixture;
    (void)fixture.provider->chat(turn({ChatMessage::user("prime the vocabulary")}), {});
    const std::int32_t token = fixture.runtime->model->id_for("prime");

    auto session = fixture.runtime->model->contexts.front();
    session->script.assign(500, token);  // never ends on its own
    session->sampled = 0;
    session->context_capacity = 12;

    ChatRequest request = turn({ChatMessage::user("go")});
    request.max_tokens = 400;  // far beyond the wall

    const auto response = fixture.provider->chat(request, {});

    CHECK(response.finish_reason == apogee::harness::FinishReason::Length);
    // It stopped at the wall, not at the token cap.
    CHECK(response.usage.completion_tokens < 400);
    CHECK(response.usage.prompt_tokens + response.usage.completion_tokens <= 12);
}

TEST_CASE("a prompt that cannot fit is refused by naming the setting",
          "[backends][llamacpp][limits]") {
    // llama.cpp's own answer is "failed to find a memory slot", which names
    // neither the prompt nor the knob that governs it. The fix is always the
    // same, so the message should say it.
    Fixture fixture;
    (void)fixture.provider->chat(turn({ChatMessage::user("prime")}), {});
    fixture.runtime->model->contexts.front()->context_capacity = 2;

    try {
        (void)fixture.provider->chat(turn({ChatMessage::user("one two three four five six seven")}),
                                     {});
        FAIL("expected a ProviderError");
    } catch (const apogee::harness::ProviderError& error) {
        const std::string message = error.what();
        CHECK(message.find("context_size") != std::string::npos);
        CHECK(message.find("tokens") != std::string::npos);
    }
}

TEST_CASE("idle unload is configured in seconds", "[backends][llamacpp][idle][config]") {
    // The acceptance criterion says the model unloads on idle "when
    // configured", so there has to BE a way to configure it: an option on a
    // struct no config file can reach is not a configurable option. This pins
    // the whole path -- YAML key to provider behaviour.
    const auto config = apogee::harness::parse_config(R"(
backends:
  local:
    type: llamacpp
    model_path: /models/test.gguf
    idle_unload_seconds: 90
)",
                                                      "test");

    const apogee::harness::BackendConfig* entry = config.find_backend("local");
    REQUIRE(entry != nullptr);
    REQUIRE(entry->idle_unload_seconds.has_value());
    CHECK(*entry->idle_unload_seconds == 90);
}

TEST_CASE("a backend that does not ask for idle unload stays resident",
          "[backends][llamacpp][idle][config]") {
    const auto config = apogee::harness::parse_config(R"(
backends:
  local:
    type: llamacpp
    model_path: /models/test.gguf
)",
                                                      "test");

    const apogee::harness::BackendConfig* entry = config.find_backend("local");
    REQUIRE(entry != nullptr);
    // Unset, not zero: a loaded model is the point of in-process inference, so
    // giving it back has to be asked for.
    CHECK_FALSE(entry->idle_unload_seconds.has_value());
}

// --- Model profiles: the framing a family leaks into its own answer ----------
//
// These drive the PROVIDER rather than the filters, because the thing they
// assert is the provider's composition order. `ThinkFilter` runs before
// `MarkupFilter` on purpose: for gpt-oss the reasoning block's opener IS a
// header, so stripping headers first would leave the model's working with no
// boundary and drop it straight into the answer. A unit test over the filters
// cannot catch that -- it would be re-implementing the order it is checking.

namespace {

/// The pieces gpt-oss-20b (MXFP4) actually emitted on 2026-09-07 for
/// "What is 2+2? Answer briefly.", split where its tokenizer split them.
const std::vector<std::string> kGptOssAnswerPieces = {
    "<|channel|>", "analysis",  "<|message|>", "The",         " answer", " is",         " 4", ".",
    "<|end|>",     "<|start|>", "assistant",   "<|channel|>", "final",   "<|message|>", "4"};

/// The same, for a tool call. `commentary` arrives in two pieces because that
/// is how the real tokenizer produced it.
const std::vector<std::string> kGptOssToolPieces = {"<|channel|>", "analysis",    "<|message|>",
                                                    "We",          " need",       " to",
                                                    " read",       ".",           "<|end|>",
                                                    "<|start|>",   "assistant",   "<|channel|>",
                                                    "comment",     "ary",         " to",
                                                    "=",           "functions",   ".read",
                                                    "_file",       " ",           "<|constrain|>",
                                                    "json",        "<|message|>", "{\"",
                                                    "path",        "\":\"",       "/tmp/notes.txt",
                                                    "\"}"};

/// A provider whose model name resolves to the gpt-oss profile.
struct GptOssFixture {
    FakeLlamaRuntime* runtime = nullptr;
    std::unique_ptr<LlamaCppProvider> provider;

    explicit GptOssFixture(std::vector<std::string> pieces) {
        auto owned = std::make_unique<FakeLlamaRuntime>();
        owned->script_text = std::move(pieces);
        owned->eog_token = -1;
        runtime = owned.get();

        LlamaCppProvider::Options options;
        options.backend_name = "local";
        // No model_path, so the profile resolves off the name hint -- the same
        // rung a user gets when they name a model rather than a file.
        options.model = "gpt-oss-20b";
        provider = std::make_unique<LlamaCppProvider>(std::move(options), std::move(owned));
    }
};

}  // namespace

TEST_CASE("gpt-oss framing never reaches the answer", "[backends][llamacpp][profile]") {
    // The bug, at the surface that had it. Before this, the reply to
    // "What is 2+2?" arrived as
    //   <|channel|>analysis<|message|>The answer is 4.<|end|>
    //   <|start|>assistant<|channel|>final<|message|>4
    // with every character shown to the user as the answer.
    GptOssFixture fixture{kGptOssAnswerPieces};

    std::string thinking;
    apogee::harness::StreamOptions options;
    options.on_thinking = [&thinking](std::string_view piece) { thinking.append(piece); };

    const auto response =
        fixture.provider->stream_chat(turn({ChatMessage::user("what is 2+2")}), options);

    CHECK(response.message.content.plain_text() == "4");
    // The reasoning was not deleted -- it went where reasoning goes.
    CHECK(thinking.find("The answer is 4.") != std::string::npos);
}

TEST_CASE("the streamed tokens and the returned text agree", "[backends][llamacpp][profile]") {
    // Filtering at one surface and not another is how a marker hidden on screen
    // reappears in a saved transcript.
    GptOssFixture fixture{kGptOssAnswerPieces};

    std::string streamed;
    apogee::harness::StreamOptions options;
    options.on_token = [&streamed](std::string_view piece) { streamed.append(piece); };

    const auto response =
        fixture.provider->stream_chat(turn({ChatMessage::user("what is 2+2")}), options);
    CHECK(streamed == response.message.content.plain_text());
}

TEST_CASE("a gpt-oss tool call dispatches instead of printing", "[backends][llamacpp][profile]") {
    GptOssFixture fixture{kGptOssToolPieces};

    std::string streamed;
    apogee::harness::StreamOptions options;
    options.on_token = [&streamed](std::string_view piece) { streamed.append(piece); };

    const auto response =
        fixture.provider->stream_chat(turn({ChatMessage::user("read it")}), options);

    REQUIRE(response.message.tool_calls.size() == 1);
    CHECK(response.message.tool_calls.front().name == "read_file");
    CHECK(response.message.tool_calls.front().arguments == R"({"path":"/tmp/notes.txt"})");
    // Nothing of the call was shown, on either surface.
    CHECK(streamed.empty());
    CHECK(response.message.content.plain_text().empty());
    // And the turn ended for the honest reason.
    CHECK(response.finish_reason == apogee::harness::FinishReason::ToolCalls);
}

TEST_CASE("the backend claims in-text tool calls only for a family that emits them",
          "[backends][llamacpp][profile]") {
    // Answered from the resolved profile, not the backend type. Claiming it
    // always would advertise parsers for grammars nobody has characterized.
    GptOssFixture gpt_oss{kGptOssAnswerPieces};
    CHECK(gpt_oss.provider->uses_in_text_tool_calls());

    Fixture unprofiled;
    CHECK_FALSE(unprofiled.provider->uses_in_text_tool_calls());
}

TEST_CASE("an unprofiled model has no framing removed from its answer",
          "[backends][llamacpp][profile]") {
    // The asymmetry with reasoning, asserted. A header is deleted outright once
    // matched, so guessing one for an uncharacterised family risks deleting its
    // answer -- the opposite of the reasoning case.
    auto owned = std::make_unique<FakeLlamaRuntime>();
    owned->script_text = {"<|channel|>", "final", "<|message|>", "hello"};
    owned->eog_token = -1;
    LlamaCppProvider::Options options;
    options.backend_name = "local";
    options.model = "some-unknown-model";
    LlamaCppProvider provider{std::move(options), std::move(owned)};

    const auto response = provider.chat(turn({ChatMessage::user("hi")}), {});
    CHECK(response.message.content.plain_text() == "<|channel|>final<|message|>hello");
}

TEST_CASE("preload loads the model before any request, and only once",
          "[backends][llamacpp][preload]") {
    // `apogee serve --preload` asks for this so the first remote client does
    // not pay the load. It is not a use: the idle window starts with the
    // first real request, not here.
    Fixture fixture;
    CHECK(fixture.runtime->loads == 0);
    CHECK_FALSE(fixture.provider->model_loaded());

    std::vector<apogee::harness::StatusEvent::Type> seen;
    fixture.provider->preload(
        [&seen](const apogee::harness::StatusEvent& event) { seen.push_back(event.type); });
    CHECK(fixture.runtime->loads == 1);
    CHECK(fixture.provider->model_loaded());
    REQUIRE_FALSE(seen.empty());
    CHECK(seen.back() == apogee::harness::StatusEvent::Type::ModelReady);

    fixture.provider->preload({});
    CHECK(fixture.runtime->loads == 1);
}

TEST_CASE("a response schema is stated once in the prompt, and never twice",
          "[backends][llamacpp][structured]") {
    // A model with no template to hold it in a grammar (the plain fixture
    // ships none) gets the schema in the system block as text. The fake
    // tokenises words, and usage.prompt_tokens is exact, so "the instruction
    // was added" and "it was not added twice" are both token arithmetic.
    Fixture plain;
    const auto without = plain.provider->chat(turn({ChatMessage::user("alpha beta gamma")}), {});

    Fixture bound;
    ChatRequest request = turn({ChatMessage::user("alpha beta gamma")});
    request.transient.response_schema = R"({"type":"object","properties":{"a":{"type":"string"}}})";
    const auto with_schema = bound.provider->chat(request, {});
    CHECK(with_schema.usage.prompt_tokens > without.usage.prompt_tokens + 10);

    // The agent runner states it once itself: a system message carrying the
    // OUTPUT FORMAT block means nothing is appended, whatever the request
    // field says -- a model that reads the schema twice is a model told to
    // follow it twice.
    Fixture stated;
    ChatRequest already = turn(
        {ChatMessage::system("OUTPUT FORMAT already here"), ChatMessage::user("alpha beta gamma")});
    const auto once = stated.provider->chat(already, {});
    Fixture stated_again;
    ChatRequest already_bound = already;
    already_bound.transient.response_schema = request.transient.response_schema;
    const auto still_once = stated_again.provider->chat(already_bound, {});
    CHECK(still_once.usage.prompt_tokens == once.usage.prompt_tokens);
}

TEST_CASE("a side request's context holds the request, not the session's window",
          "[backends][llamacpp][kv]") {
    // A title used to get a context as large as the session's: on a model
    // trained for 256K positions, gigabytes of cache for a few hundred tokens.
    const auto provider_with = [](std::int64_t window, FakeLlamaRuntime*& runtime) {
        auto owned = std::make_unique<FakeLlamaRuntime>();
        runtime = owned.get();
        LlamaCppProvider::Options options;
        options.backend_name = "local";
        options.model = "test-model";
        options.model_path = "/models/test.gguf";
        options.context_size = window;
        return std::make_unique<LlamaCppProvider>(std::move(options), std::move(owned));
    };
    ChatRequest title = turn({ChatMessage::user("name this")});
    title.max_tokens = 32;
    title.transient.side_request = true;
    ChatRequest long_side = title;
    long_side.max_tokens = 10000;

    FakeLlamaRuntime* runtime = nullptr;
    const auto provider = provider_with(200000, runtime);
    (void)provider->chat(turn({ChatMessage::user("alpha beta")}), {});
    (void)provider->chat(title, {});
    (void)provider->chat(long_side, {});
    const std::vector<std::int64_t>& sizes = runtime->model->context_sizes;
    REQUIRE(sizes.size() == 3);
    CHECK(sizes[0] == 200000);  // the session keeps its whole window
    CHECK(sizes[1] == 4096);    // a small request gets the floor
    // Room for the prompt and the cap, with slack -- and no more.
    CHECK(sizes[2] > 10000);
    CHECK(sizes[2] < 10500);

    // Never past the window the backend was given.
    const auto narrow = provider_with(5000, runtime);
    (void)narrow->chat(long_side, {});
    CHECK(runtime->model->context_sizes.back() == 5000);
}

TEST_CASE("skipping reasoning closes the family's think block before the answer",
          "[backends][llamacpp][profile]") {
    // What Qwen's own template writes for enable_thinking=false: the model's
    // first token is then the answer's, not the first of hundreds of reasoning.
    const auto last_prompt = [](std::string model, bool skip) {
        auto owned = std::make_unique<FakeLlamaRuntime>();
        FakeLlamaRuntime* runtime = owned.get();
        LlamaCppProvider::Options options;
        options.backend_name = "local";
        options.model = std::move(model);
        options.model_path = "/models/test.gguf";
        LlamaCppProvider provider{std::move(options), std::move(owned)};
        ChatRequest request = turn({ChatMessage::user("name this")});
        request.transient.side_request = true;
        request.thinking.mode =
            skip ? apogee::harness::ThinkingMode::Off : apogee::harness::ThinkingMode::On;
        (void)provider.chat(request, {});
        return runtime->model->tokenized.back();
    };
    const std::string closed = "<think>\n\n</think>\n\n";
    CHECK(last_prompt("qwen3-8b", true).ends_with(closed));
    CHECK_FALSE(last_prompt("qwen3-8b", false).ends_with(closed));
    // An uncharacterised family's switch is not guessed at: a wrong one would
    // reach the model as text.
    CHECK(last_prompt("test-model", true).find("<think>") == std::string::npos);
}

// ---------------------------------------------------------------------------
// Tools through the model's own template (25b)
// ---------------------------------------------------------------------------

namespace {

/// A provider over a model that ships a template the chat layer reads, with
/// the tool-call markers special -- rendered as nothing unless preserved.
struct TemplateFixture {
    FakeLlamaRuntime* runtime = nullptr;
    std::unique_ptr<LlamaCppProvider> provider;

    explicit TemplateFixture(std::vector<std::string> pieces) {
        auto owned = std::make_unique<FakeLlamaRuntime>();
        owned->script_text = std::move(pieces);
        owned->eog_token = -1;
        owned->chat_template = true;
        owned->special_words = {"<tool_call>", "</tool_call>"};
        runtime = owned.get();

        LlamaCppProvider::Options options;
        options.backend_name = "local";
        options.model = "qwen3-vl-8b";
        provider = std::make_unique<LlamaCppProvider>(std::move(options), std::move(owned));
    }
};

apogee::harness::Tool read_file_tool() {
    apogee::harness::Tool tool;
    tool.name = "read_file";
    tool.description = "Read a file";
    tool.parameters_schema = R"({"type":"object","properties":{"path":{"type":"string"}}})";
    return tool;
}

ChatRequest with_tools(std::vector<ChatMessage> messages) {
    ChatRequest request = turn(std::move(messages));
    request.tools = {read_file_tool()};
    return request;
}

/// What a turn showed and said, captured from every sink.
struct Captured {
    std::string streamed;
    std::string thinking;
    std::vector<std::string> notices;
    apogee::harness::StreamOptions options;

    Captured() {
        options.on_token = [this](std::string_view piece) { streamed.append(piece); };
        options.on_thinking = [this](std::string_view piece) { thinking.append(piece); };
        options.on_status = [this](const apogee::harness::StatusEvent& event) {
            if (event.type == apogee::harness::StatusEvent::Type::Notice) {
                notices.push_back(event.detail);
            }
        };
    }
};

const std::vector<std::string> kReadFileCall = {
    "<tool_call>", R"({"name":"read_file","arguments":{"path":"notes.txt"}})", "</tool_call>"};

}  // namespace

TEST_CASE("a local model is shown its tools, and its call comes back as the IR's",
          "[backends][llamacpp][tools]") {
    // The gap the spike measured: the tools never reached the prompt, so no
    // local model had ever been shown one.
    TemplateFixture fixture{kReadFileCall};
    Captured seen;
    const auto response = fixture.provider->stream_chat(
        with_tools({ChatMessage::user("what does it say")}), seen.options);

    REQUIRE(fixture.runtime->model != nullptr);
    REQUIRE_FALSE(fixture.runtime->model->chat_renders.empty());
    const auto& rendered = fixture.runtime->model->chat_renders.back();
    REQUIRE(rendered.tools.size() == 1);
    CHECK(rendered.tools.front().name == "read_file");
    CHECK(fixture.runtime->model->tokenized.back().find("tools: read_file") != std::string::npos);

    REQUIRE(response.message.tool_calls.size() == 1);
    const apogee::harness::ToolCall& call = response.message.tool_calls.front();
    CHECK(call.name == "read_file");
    CHECK(call.arguments == R"({"path":"notes.txt"})");
    // An id the next render can match the result to: nine letters and digits.
    CHECK(call.id.size() == 9);
    CHECK(std::all_of(call.id.begin(), call.id.end(),
                      [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; }));
    CHECK(response.finish_reason == apogee::harness::FinishReason::ToolCalls);
    // No markup on either surface: the call was read, never shown.
    CHECK(seen.streamed.empty());
    CHECK(response.message.content.plain_text().empty());
    CHECK(seen.notices.empty());
}

TEST_CASE("an assistant turn with calls, and their results, render back",
          "[backends][llamacpp][tools]") {
    TemplateFixture fixture{{"It", " says hi."}};
    ChatMessage assistant = ChatMessage::assistant("");
    apogee::harness::ToolCall call;
    call.id = "abc123XYZ";
    call.name = "read_file";
    call.arguments = R"({"path":"notes.txt"})";
    assistant.tool_calls = {call};
    apogee::harness::ToolResult result;
    result.tool_call_id = call.id;
    result.name = call.name;
    result.content = "hi";
    const auto response =
        fixture.provider->chat(with_tools({ChatMessage::user("what does it say"), assistant,
                                           ChatMessage::from_tool_result(result)}),
                               {});

    const std::string& prompt = fixture.runtime->model->tokenized.back();
    CHECK(prompt.find("call:read_file#abc123XYZ") != std::string::npos);
    CHECK(prompt.find("answers:abc123XYZ") != std::string::npos);
    CHECK(response.message.content.plain_text() == "It says hi.");
    CHECK(response.message.tool_calls.empty());
}

TEST_CASE("the grammar rides a request with tools and is cleared on one without",
          "[backends][llamacpp][tools]") {
    TemplateFixture fixture{kReadFileCall};
    (void)fixture.provider->chat(with_tools({ChatMessage::user("read it")}), {});
    auto session = fixture.runtime->model->contexts.front();
    REQUIRE_FALSE(session->grammars.empty());
    CHECK(session->grammars.back().gbnf == "root ::= fake-call");
    CHECK(session->grammars.back().lazy);
    CHECK(session->grammars.back().trigger_patterns == std::vector<std::string>{"<tool_call>"});

    // The session context outlives the request: the next one, with no tools,
    // must not sample under the last one's grammar.
    session->sampled = 0;
    (void)fixture.provider->chat(turn({ChatMessage::user("just talk")}), {});
    CHECK(session->grammars.back().gbnf.empty());

    // And when the next request falls back -- a template that cannot render
    // this one -- the last call's grammar is cleared there too.
    session->sampled = 0;
    (void)fixture.provider->chat(with_tools({ChatMessage::user("read it again")}), {});
    REQUIRE_FALSE(session->grammars.back().gbnf.empty());
    fixture.runtime->model->chat_template_error = "cannot render this one";
    session->sampled = 0;
    (void)fixture.provider->chat(turn({ChatMessage::user("plain")}), {});
    CHECK(session->grammars.back().gbnf.empty());
}

namespace {

constexpr const char* kAnswerSchema =
    R"({"type":"object","properties":{"a":{"type":"string"}},"required":["a"]})";

ChatRequest structured(std::vector<ChatMessage> messages) {
    ChatRequest request = turn(std::move(messages));
    request.transient.response_schema = kAnswerSchema;
    return request;
}

}  // namespace

TEST_CASE("a schema is held by a grammar, and the prompt does not state it",
          "[backends][llamacpp][structured]") {
    // 26f: the model's own template takes the schema and its grammar holds
    // the whole answer to it, so the prompt carries no statement of it --
    // the grammar is the one place it is stated.
    TemplateFixture fixture{{R"({"a":"x"})"}};
    Captured seen;
    const auto response = fixture.provider->stream_chat(
        structured({ChatMessage::user("alpha beta gamma")}), seen.options);

    const auto& rendered = fixture.runtime->model->chat_renders.back();
    CHECK(rendered.response_schema == kAnswerSchema);
    const std::string& prompt = fixture.runtime->model->tokenized.back();
    CHECK(prompt.find("OUTPUT FORMAT") == std::string::npos);
    CHECK(prompt.find("properties") == std::string::npos);

    const auto& grammars = fixture.runtime->model->contexts.front()->grammars;
    REQUIRE_FALSE(grammars.empty());
    CHECK(grammars.back().gbnf == std::string{"root ::= fake-answer "} + kAnswerSchema);
    CHECK_FALSE(grammars.back().lazy);
    // Advanced past the reply's opening, which the prompt already holds.
    CHECK(grammars.back().prefill == " assistant:");
    CHECK(prompt.ends_with(grammars.back().prefill));

    CHECK(response.message.content.plain_text() == R"({"a":"x"})");
    CHECK(seen.notices.empty());
}

TEST_CASE("a clerk's schema is held on its own context too", "[backends][llamacpp][structured]") {
    // Every structured caller runs as a side request: the grammar rides the
    // throwaway context, not the session's.
    TemplateFixture fixture{{R"({"a":"x"})"}};
    ChatRequest request = structured(
        {ChatMessage::system("You are a clerk."), ChatMessage::user("alpha beta gamma")});
    request.transient.side_request = true;
    (void)fixture.provider->chat(request, {});
    const auto& grammars = fixture.runtime->model->contexts.back()->grammars;
    REQUIRE_FALSE(grammars.empty());
    CHECK_FALSE(grammars.back().gbnf.empty());
    CHECK_FALSE(grammars.back().lazy);
}

TEST_CASE("a thinking model still thinks before its structured answer",
          "[backends][llamacpp][structured]") {
    // The grammar admits the reasoning ahead of the answer, so the template's
    // thinking switch stays on and the reasoning reaches its own sink.
    TemplateFixture fixture{{"<think>", "pondering", "</think>", R"({"a":"x"})"}};
    Captured seen;
    const auto response =
        fixture.provider->stream_chat(structured({ChatMessage::user("think first")}), seen.options);
    CHECK(fixture.runtime->model->chat_renders.back().enable_thinking);
    CHECK(seen.thinking == "pondering");
    CHECK(response.message.content.plain_text() == R"({"a":"x"})");
}

TEST_CASE("a turn with tools keeps its tool grammar, and states the schema",
          "[backends][llamacpp][structured]") {
    // A grammar over the whole answer leaves no room for a call, so the loop's
    // rule for every provider holds here too: the schema is held on the
    // tools-less pass, and a turn that may still call a tool states it.
    TemplateFixture fixture{kReadFileCall};
    ChatRequest request = with_tools({ChatMessage::user("read it")});
    request.transient.response_schema = kAnswerSchema;
    const auto response = fixture.provider->chat(request, {});

    for (const auto& render : fixture.runtime->model->chat_renders) {
        CHECK(render.response_schema.empty());
    }
    CHECK(fixture.runtime->model->tokenized.back().find("OUTPUT FORMAT") != std::string::npos);
    const auto& grammars = fixture.runtime->model->contexts.front()->grammars;
    REQUIRE_FALSE(grammars.empty());
    CHECK(grammars.back().gbnf == "root ::= fake-call");
    CHECK(grammars.back().lazy);
    CHECK(response.message.tool_calls.size() == 1);
}

TEST_CASE("a schema no grammar can hold is stated in the prompt, and said once",
          "[backends][llamacpp][structured]") {
    // A format with no place for a schema, or one the converter cannot
    // express: the answer is held by the prompt and the validator, as before
    // 26f, and the user is told once rather than every call.
    TemplateFixture fixture{{R"({"a":"x"})"}};
    fixture.runtime->schema_error = "Unresolved $ref #/definitions/missing";
    Captured seen;
    const auto first = fixture.provider->stream_chat(
        structured({ChatMessage::user("alpha beta gamma")}), seen.options);
    CHECK(fixture.runtime->model->tokenized.back().find("OUTPUT FORMAT") != std::string::npos);
    const auto& grammars = fixture.runtime->model->contexts.front()->grammars;
    REQUIRE_FALSE(grammars.empty());
    CHECK(grammars.back().gbnf.empty());
    CHECK(first.message.content.plain_text() == R"({"a":"x"})");
    REQUIRE(seen.notices.size() == 1);
    CHECK(seen.notices.front().find("Unresolved $ref") != std::string::npos);
    CHECK(seen.notices.front().find("qwen3-vl-8b") != std::string::npos);

    fixture.runtime->model->contexts.front()->sampled = 0;
    (void)fixture.provider->stream_chat(structured({ChatMessage::user("again")}), seen.options);
    CHECK(seen.notices.size() == 1);
}

TEST_CASE("a schema's grammar that does not compile falls back before the prompt is sent",
          "[backends][llamacpp][structured]") {
    // Compiled as the prompt is rendered, while the schema can still be
    // stated: one that failed only at the first sample would leave the
    // answer held by nothing.
    TemplateFixture fixture{{R"({"a":"x"})"}};
    fixture.runtime->grammar_error = "parse error at line 3";
    Captured seen;
    (void)fixture.provider->stream_chat(structured({ChatMessage::user("alpha beta gamma")}),
                                        seen.options);
    CHECK(fixture.runtime->model->tokenized.back().find("OUTPUT FORMAT") != std::string::npos);
    CHECK(fixture.runtime->model->contexts.front()->grammars.back().gbnf.empty());
    REQUIRE(seen.notices.size() == 1);
    CHECK(seen.notices.front().find("parse error at line 3") != std::string::npos);
}

TEST_CASE("a held schema's prompt keeps its last-user checkpoint",
          "[backends][llamacpp][structured][checkpoint]") {
    // The conversation before the last user message is rendered with the same
    // inputs as the full prompt -- no statement of the schema in either -- or
    // its length is no position in the prompt and the checkpoint is lost.
    TemplateFixture fixture{{R"({"a":"x"})"}};
    fixture.runtime->rewindable = false;
    const auto response = fixture.provider->chat(
        structured({ChatMessage::system("be brief"), ChatMessage::user("one two three four")}), {});
    auto session = fixture.runtime->model->contexts.front();
    CHECK(session->checkpoints() == std::vector<std::int64_t>{4, response.usage.prompt_tokens - 4});
}

TEST_CASE("thinking reaches only the thinking sink", "[backends][llamacpp][tools]") {
    TemplateFixture fixture{{"<think>", "pondering", "</think>", "The", " answer"}};
    Captured seen;
    const auto response =
        fixture.provider->stream_chat(turn({ChatMessage::user("think first")}), seen.options);
    CHECK(seen.thinking == "pondering");
    CHECK(seen.streamed == "The answer");
    CHECK(response.message.content.plain_text() == "The answer");
    CHECK(seen.streamed.find("pondering") == std::string::npos);
}

TEST_CASE("skipping reasoning is the template's own switch", "[backends][llamacpp][tools]") {
    TemplateFixture fixture{{"Title"}};
    ChatRequest request = turn({ChatMessage::user("name this chat")});
    request.thinking.mode = apogee::harness::ThinkingMode::Off;
    (void)fixture.provider->chat(request, {});
    REQUIRE_FALSE(fixture.runtime->model->chat_renders.empty());
    CHECK_FALSE(fixture.runtime->model->chat_renders.back().enable_thinking);
    // The fallback's closed think block is not appended on top of it.
    const std::string& prompt = fixture.runtime->model->tokenized.back();
    CHECK(prompt.ends_with("assistant(no-think):"));
}

TEST_CASE("thinking off also asks gpt-oss's switch for its lowest effort",
          "[backends][llamacpp][thinking]") {
    // gpt-oss has no off: its template reads `reasoning_effort`, and `low` is
    // the least it does. Templates that do not read it ignore it.
    TemplateFixture fixture{{"answer"}};
    ChatRequest request = turn({ChatMessage::user("x")});
    request.thinking.mode = apogee::harness::ThinkingMode::Off;
    (void)fixture.provider->chat(request, {});
    CHECK_FALSE(fixture.runtime->model->chat_renders.back().enable_thinking);
    CHECK(fixture.runtime->model->chat_renders.back().reasoning_effort == "low");

    // On leaves every template at its own default.
    request.thinking.mode = apogee::harness::ThinkingMode::On;
    (void)fixture.provider->chat(request, {});
    CHECK(fixture.runtime->model->chat_renders.back().enable_thinking);
    CHECK(fixture.runtime->model->chat_renders.back().reasoning_effort.empty());
}

TEST_CASE("a thinking budget is counted between the format's own reasoning tags",
          "[backends][llamacpp][thinking]") {
    TemplateFixture fixture{{"<think>", "pondering", "</think>", "The", " answer"}};
    fixture.runtime->thinking_tags = true;
    ChatRequest request = turn({ChatMessage::user("think first")});
    request.thinking.budget = 64;
    (void)fixture.provider->chat(request, {});
    const auto& budgeted = fixture.runtime->model->contexts.front()->samplings.back();
    REQUIRE(budgeted.reasoning_budget.has_value());
    CHECK(budgeted.reasoning_budget->tokens == 64);
    CHECK(budgeted.reasoning_budget->start == "<think>");
    CHECK(budgeted.reasoning_budget->ends == std::vector<std::string>{"</think>"});
    // The prompt's own opening, so a template that opens the block itself
    // starts the count already inside it.
    CHECK(budgeted.reasoning_budget->prefill == " assistant:");

    // Off has nothing to count, and no budget asked is no limit.
    request.thinking.mode = apogee::harness::ThinkingMode::Off;
    (void)fixture.provider->chat(request, {});
    CHECK_FALSE(fixture.runtime->model->contexts.front()->samplings.back().reasoning_budget);
    request.thinking = {};
    (void)fixture.provider->chat(request, {});
    CHECK_FALSE(fixture.runtime->model->contexts.front()->samplings.back().reasoning_budget);
}

TEST_CASE("a budget a format cannot count is said, once a conversation",
          "[backends][llamacpp][thinking]") {
    TemplateFixture fixture{{"answer"}};
    Captured seen;
    ChatRequest request = turn({ChatMessage::user("x")});
    request.thinking.budget = 64;
    (void)fixture.provider->stream_chat(request, seen.options);
    (void)fixture.provider->stream_chat(request, seen.options);
    const auto said = std::ranges::count_if(seen.notices, [](const std::string& notice) {
        return notice.find("thinking budget is not applied") != std::string::npos;
    });
    CHECK(said == 1);
    CHECK_FALSE(fixture.runtime->model->contexts.front()->samplings.back().reasoning_budget);
}

TEST_CASE("a spent budget is said once, while the reasoning is still on screen",
          "[backends][llamacpp][thinking]") {
    TemplateFixture fixture{{"<think>", "a", "b", "c", "</think>", "done"}};
    fixture.runtime->thinking_tags = true;
    fixture.runtime->budget_spent_after = 2;
    std::vector<apogee::harness::StatusEvent> spent;
    apogee::harness::StreamOptions options;
    options.on_status = [&spent](const apogee::harness::StatusEvent& event) {
        if (event.type == apogee::harness::StatusEvent::Type::ThinkingBudget) {
            spent.push_back(event);
        }
    };
    ChatRequest request = turn({ChatMessage::user("think first")});
    request.thinking.budget = 2;
    (void)fixture.provider->stream_chat(request, options);
    REQUIRE(spent.size() == 1);
    CHECK(spent.front().tokens == 2);
    CHECK(spent.front().detail.find("2 tokens") != std::string::npos);
}

TEST_CASE("a template that cannot render falls back, and says so once when tools were asked for",
          "[backends][llamacpp][tools]") {
    TemplateFixture fixture{{"fine"}};
    fixture.runtime->chat_template_error = "the template refused tools";
    Captured seen;
    const auto response =
        fixture.provider->stream_chat(with_tools({ChatMessage::user("read it")}), seen.options);
    CHECK(response.message.content.plain_text() == "fine");
    REQUIRE(seen.notices.size() == 1);
    CHECK(seen.notices.front().find("answering without tools") != std::string::npos);
    CHECK(seen.notices.front().find("the template refused tools") != std::string::npos);
    // The prompt came from the fallback, not the template.
    CHECK(fixture.runtime->model->tokenized.back().find("[template]") == std::string::npos);

    // With no tools asked for, the fallback is silent: nothing was withheld.
    Captured quiet;
    fixture.runtime->model->contexts.front()->sampled = 0;
    (void)fixture.provider->stream_chat(turn({ChatMessage::user("hello")}), quiet.options);
    CHECK(quiet.notices.empty());
}

TEST_CASE("a reply that does not match the format is kept as text and runs nothing",
          "[backends][llamacpp][tools]") {
    TemplateFixture fixture{{"Let me look.", "<tool_call>", R"({"name":)"}};
    Captured seen;
    const auto response =
        fixture.provider->stream_chat(with_tools({ChatMessage::user("read it")}), seen.options);
    CHECK(response.message.tool_calls.empty());
    CHECK(response.message.content.plain_text() == "Let me look.");
    CHECK(seen.streamed == "Let me look.");
    REQUIRE(seen.notices.size() == 1);
    CHECK(seen.notices.front().find("did not match") != std::string::npos);
}

TEST_CASE("a reply that matches nothing and showed nothing comes out as its text",
          "[backends][llamacpp][tools]") {
    // Never a turn with no answer, no tool and only a notice.
    TemplateFixture fixture{{"<tool_call>", R"({"name":"read_file")"}};
    Captured seen;
    const auto response =
        fixture.provider->stream_chat(with_tools({ChatMessage::user("read it")}), seen.options);
    CHECK(response.message.tool_calls.empty());
    CHECK(response.message.content.plain_text() == R"(<tool_call>{"name":"read_file")");
    CHECK(seen.streamed == response.message.content.plain_text());
    CHECK(seen.notices.size() == 1);

    // Unless the model was thinking when it stopped: reasoning stays out.
    TemplateFixture thinking{{"<think>", "still", "<tool_call>"}};
    Captured quiet;
    const auto cut =
        thinking.provider->stream_chat(with_tools({ChatMessage::user("x")}), quiet.options);
    CHECK(cut.message.content.plain_text().empty());
    CHECK(quiet.thinking.find("still") != std::string::npos);
}

TEST_CASE("a stop string ends the reply and is not part of it", "[backends][llamacpp][tools]") {
    TemplateFixture fixture{{"Hello", "<end>", "never"}};
    fixture.runtime->stops = {"<end>"};
    const auto response = fixture.provider->chat(turn({ChatMessage::user("hi")}), {});
    CHECK(response.message.content.plain_text() == "Hello");
    CHECK(response.usage.completion_tokens == 2);
}

TEST_CASE("a grammar that does not compile runs unconstrained, and says so",
          "[backends][llamacpp][tools]") {
    TemplateFixture fixture{kReadFileCall};
    fixture.runtime->grammar_error = "bad grammar";
    Captured seen;
    const auto response =
        fixture.provider->stream_chat(with_tools({ChatMessage::user("read it")}), seen.options);
    // The reader still found the call.
    REQUIRE(response.message.tool_calls.size() == 1);
    REQUIRE(seen.notices.size() == 1);
    CHECK(seen.notices.front().find("bad grammar") != std::string::npos);
    const auto& grammars = fixture.runtime->model->contexts.front()->grammars;
    REQUIRE(grammars.size() == 2);
    CHECK(grammars.back().gbnf.empty());
}

TEST_CASE("a warm count includes the tool definitions", "[backends][llamacpp][tools]") {
    TemplateFixture fixture{{"ok"}};
    (void)fixture.provider->chat(turn({ChatMessage::user("warm up")}), {});
    const std::int64_t bare = fixture.provider->count_prompt_tokens(turn({ChatMessage::user("x")}));
    const std::int64_t tooled =
        fixture.provider->count_prompt_tokens(with_tools({ChatMessage::user("x")}));
    CHECK(tooled > bare);
}

// ---------------------------------------------------------------------------
// Checkpoints for a model whose memory cannot be rewound (25c)
// ---------------------------------------------------------------------------

namespace {

/// Prompt tokens the context decoded since record `from`, and where the first
/// of those decodes started -- the prompt phase only, which ends where
/// generation begins (`prompt_end`).
struct PromptDecode {
    std::int64_t start = -1;
    std::int64_t count = 0;
};

PromptDecode prompt_decode(const apogee::testing::FakeLlamaContext& context, std::size_t from,
                           std::int64_t prompt_end) {
    PromptDecode out;
    for (std::size_t i = from; i < context.decodes.size(); ++i) {
        const apogee::backends::DecodeRecord& record = context.decodes[i];
        if (record.position >= prompt_end) {
            break;
        }
        if (out.start < 0) {
            out.start = record.position;
        }
        out.count += record.count;
    }
    return out;
}

}  // namespace

TEST_CASE("a hybrid model's second turn reads only what is new",
          "[backends][llamacpp][checkpoint]") {
    // The re-read 25c removes: the template re-renders turn one's answer
    // without its reasoning, so turn two parts from the cache at the answer,
    // the running state cannot be cut there, and the whole conversation was
    // read again. Now the checkpoint just short of turn one's prompt end is
    // restored and only the rest is read.
    TemplateFixture fixture{{"<think>", "pondering", "</think>", "first", " answer"}};
    fixture.runtime->rewindable = false;
    const std::vector<ChatMessage> opening{ChatMessage::system("be brief"),
                                           ChatMessage::user("one two three four five")};
    const auto first = fixture.provider->chat(turn(opening), {});
    auto session = fixture.runtime->model->contexts.front();
    const std::int64_t first_prompt = first.usage.prompt_tokens;
    // Where the last user message starts ("[template] system: be brief"), and
    // four tokens short of the prompt's end.
    CHECK(session->checkpoints() == std::vector<std::int64_t>{4, first_prompt - 4});

    const std::size_t mark = session->decodes.size();
    session->sampled = 0;
    std::vector<ChatMessage> next = opening;
    next.push_back(ChatMessage::assistant(first.message.content));
    next.push_back(ChatMessage::user("six seven"));
    const auto second = fixture.provider->chat(turn(next), {});

    CHECK(session->restores == std::vector<std::int64_t>{first_prompt - 4});
    const PromptDecode read = prompt_decode(*session, mark, second.usage.prompt_tokens);
    CHECK(read.start == first_prompt - 4);
    CHECK(read.count == second.usage.prompt_tokens - (first_prompt - 4));
}

TEST_CASE("a hybrid model's tool step reads only the call and its result",
          "[backends][llamacpp][checkpoint]") {
    TemplateFixture fixture{kReadFileCall};
    fixture.runtime->rewindable = false;
    const std::vector<ChatMessage> asked{ChatMessage::user("what does notes.txt say, please")};
    const auto step = fixture.provider->chat(with_tools(asked), {});
    REQUIRE(step.message.tool_calls.size() == 1);
    auto session = fixture.runtime->model->contexts.front();
    const std::int64_t first_prompt = step.usage.prompt_tokens;

    const std::size_t mark = session->decodes.size();
    session->sampled = 0;
    ChatMessage call = ChatMessage::assistant("");
    call.tool_calls = step.message.tool_calls;
    apogee::harness::ToolResult result;
    result.tool_call_id = step.message.tool_calls.front().id;
    result.name = "read_file";
    result.content = "hi there";
    std::vector<ChatMessage> next = asked;
    next.push_back(call);
    next.push_back(ChatMessage::from_tool_result(result));
    const auto answer = fixture.provider->chat(with_tools(next), {});

    CHECK(session->restores == std::vector<std::int64_t>{first_prompt - 4});
    const PromptDecode read = prompt_decode(*session, mark, answer.usage.prompt_tokens);
    CHECK(read.start == first_prompt - 4);
    CHECK(read.count == answer.usage.prompt_tokens - (first_prompt - 4));
}

TEST_CASE("a checkpoint past the divergence is never restored",
          "[backends][llamacpp][checkpoint]") {
    // The last user message itself changed: turn two parts from the cache
    // inside it, before the checkpoint near the prompt's end. That one holds
    // a state the new prompt does not share; the one at the message's start
    // is the newest that is safe.
    TemplateFixture fixture{{"ok"}};
    fixture.runtime->rewindable = false;
    (void)fixture.provider->chat(
        turn({ChatMessage::system("S"), ChatMessage::user("q one two three four five six")}), {});
    auto session = fixture.runtime->model->contexts.front();
    // "[template] system: S" is three tokens; twelve in all, so 3 and 8.
    REQUIRE(session->checkpoints() == std::vector<std::int64_t>{3, 8});

    session->sampled = 0;
    (void)fixture.provider->chat(
        turn({ChatMessage::system("S"), ChatMessage::user("q ONE two three four five six")}), {});
    CHECK(session->restores == std::vector<std::int64_t>{3});
    // Nothing past the restored point survives it.
    for (const std::int64_t held : session->checkpoints()) {
        CHECK(held >= 3);
    }
}

TEST_CASE("the last user message's checkpoint is taken only at a real token boundary",
          "[backends][llamacpp][checkpoint]") {
    // A template that renders the conversation before the last user message
    // differently on its own gives a length that is no position in the
    // prompt: a checkpoint there would hold a state no prompt ever reached.
    TemplateFixture fixture{{"ok"}};
    fixture.runtime->rewindable = false;
    fixture.runtime->unstable_prefix = true;
    const auto response = fixture.provider->chat(
        turn({ChatMessage::system("S"), ChatMessage::user("q one two three four five six")}), {});
    auto session = fixture.runtime->model->contexts.front();
    // Only the one near the prompt's end.
    CHECK(session->checkpoints() == std::vector<std::int64_t>{response.usage.prompt_tokens - 4});
}

TEST_CASE("with no checkpoint before the divergence the prompt is read again from 0",
          "[backends][llamacpp][checkpoint]") {
    TemplateFixture fixture{{"ok"}};
    fixture.runtime->rewindable = false;
    (void)fixture.provider->chat(turn({ChatMessage::user("alpha beta gamma delta epsilon")}), {});
    auto session = fixture.runtime->model->contexts.front();
    const std::size_t mark = session->decodes.size();
    session->sampled = 0;
    // Parts at the second token, before any checkpoint.
    const auto second = fixture.provider->chat(turn({ChatMessage::system("new")}), {});
    CHECK(session->restores.empty());
    CHECK(prompt_decode(*session, mark, second.usage.prompt_tokens).start == 0);
}

TEST_CASE("a side request leaves the session's checkpoints untouched",
          "[backends][llamacpp][checkpoint]") {
    TemplateFixture fixture{{"ok"}};
    fixture.runtime->rewindable = false;
    (void)fixture.provider->chat(
        turn({ChatMessage::system("S"), ChatMessage::user("q one two three four five six")}), {});
    auto session = fixture.runtime->model->contexts.front();
    const std::vector<std::int64_t> before = session->checkpoints();
    REQUIRE_FALSE(before.empty());

    ChatRequest title = turn({ChatMessage::user("name this conversation in three words")});
    title.transient.side_request = true;
    (void)fixture.provider->chat(title, {});
    REQUIRE(fixture.runtime->model->contexts.size() == 2);
    CHECK(session->checkpoints() == before);
    CHECK(session->restores.empty());
    // The side request's own throwaway context takes none either: nothing
    // would ever go back to it.
    CHECK(fixture.runtime->model->contexts.back()->checkpoints().empty());
}

TEST_CASE("a pure-attention model takes no checkpoints, and pays nothing to find them",
          "[backends][llamacpp][checkpoint]") {
    TemplateFixture fixture{{"ok"}};
    (void)fixture.provider->chat(
        turn({ChatMessage::system("S"), ChatMessage::user("q one two three four five six")}), {});
    auto session = fixture.runtime->model->contexts.front();
    CHECK(session->checkpoints().empty());
    // No second render to find the last user message's start.
    for (const auto& render : fixture.runtime->model->chat_renders) {
        CHECK(render.add_generation_prompt);
    }
}

TEST_CASE("each turn reports what the cache kept and the checkpoints it holds",
          "[backends][llamacpp][checkpoint]") {
    TemplateFixture fixture{{"ok"}};
    fixture.runtime->rewindable = false;
    std::vector<apogee::harness::StatusEvent> reports;
    apogee::harness::StreamOptions options;
    options.on_status = [&reports](const apogee::harness::StatusEvent& event) {
        if (event.type == apogee::harness::StatusEvent::Type::PromptCache) {
            reports.push_back(event);
        }
    };
    const auto response = fixture.provider->stream_chat(
        turn({ChatMessage::system("S"), ChatMessage::user("q one two three four five six")}),
        options);
    REQUIRE(reports.size() == 1);
    CHECK(reports.front().tokens == response.usage.prompt_tokens);
    CHECK(reports.front().used_tokens == 0);
    CHECK(reports.front().detail.find("0 from the cache") != std::string::npos);
    CHECK(reports.front().detail.find("2 of 8 checkpoints, 0.0 MiB") != std::string::npos);

    // A pure-attention model has no checkpoints to report.
    TemplateFixture plain{{"ok"}};
    std::vector<std::string> lines;
    apogee::harness::StreamOptions quiet;
    quiet.on_status = [&lines](const apogee::harness::StatusEvent& event) {
        if (event.type == apogee::harness::StatusEvent::Type::PromptCache) {
            lines.push_back(event.detail);
        }
    };
    (void)plain.provider->stream_chat(turn({ChatMessage::user("hello")}), quiet);
    REQUIRE(lines.size() == 1);
    CHECK(lines.front().find("checkpoints") == std::string::npos);
}

// ---------------------------------------------------------------------------
// A window-sized cache for sliding-window models (26m)
// ---------------------------------------------------------------------------

TEST_CASE("the window rule: intact from 0, or when the oldest kept is before the window",
          "[backends][llamacpp][sliding]") {
    using apogee::backends::window_intact;

    // No window, or nothing kept before the cut: nothing can be missing.
    CHECK(window_intact(500, 600, 0));
    CHECK(window_intact(-1, 0, 1024));
    // Everything from 0 is held.
    CHECK(window_intact(0, 300, 1024));
    CHECK(window_intact(0, 5000, 1024));
    // Held from before the window: the positions the next token looks back
    // over are all there.
    CHECK(window_intact(3975, 5000, 1024));
    // llama-server's margin, which errs safe: exactly at the window is short.
    CHECK_FALSE(window_intact(3976, 5000, 1024));
    CHECK_FALSE(window_intact(4999, 5000, 1024));
    // A cache emptied under a cut that was supposed to keep something.
    CHECK_FALSE(window_intact(-1, 10, 1024));
    // A cut early in a conversation whose first positions are gone.
    CHECK_FALSE(window_intact(10, 500, 1024));
}

TEST_CASE("a sliding model's cut within its window reads only what is new, with no restore",
          "[backends][llamacpp][sliding]") {
    TemplateFixture fixture{{"<think>", "pondering", "</think>", "first", " answer"}};
    fixture.runtime->sliding_window = 2;
    fixture.runtime->sliding_keep = 1000;  // the conversation fits its window
    const std::vector<ChatMessage> opening{ChatMessage::system("be brief"),
                                           ChatMessage::user("one two three four five")};
    const auto first = fixture.provider->chat(turn(opening), {});
    auto session = fixture.runtime->model->contexts.front();
    const std::int64_t first_prompt = first.usage.prompt_tokens;
    // Taken as a hybrid's are: a sliding cache may need them.
    CHECK(session->checkpoints() == std::vector<std::int64_t>{4, first_prompt - 4});

    const std::size_t mark = session->decodes.size();
    session->sampled = 0;
    std::vector<ChatMessage> next = opening;
    next.push_back(ChatMessage::assistant(first.message.content));
    next.push_back(ChatMessage::user("six seven"));
    const auto second = fixture.provider->chat(turn(next), {});

    // The cut kept its window, so it stood: nothing restored, and turn two
    // read from where the prompts part, past the checkpoint near the end.
    CHECK(session->restores.empty());
    const PromptDecode read = prompt_decode(*session, mark, second.usage.prompt_tokens);
    CHECK(read.start >= first_prompt - 4);
    CHECK(read.count == second.usage.prompt_tokens - read.start);
}

TEST_CASE("a cut past a sliding window restores a checkpoint, never the short window",
          "[backends][llamacpp][sliding]") {
    // The case a naive cut gets wrong: the cache holds only its last three
    // positions, turn two parts from it further back than that, and decoding
    // on from there would look back over positions that are gone. The fake
    // fails any decode that does (`window_intact` on every decode).
    TemplateFixture fixture{{"<think>", "pondering", "</think>", "first", " answer"}};
    fixture.runtime->sliding_window = 2;
    fixture.runtime->sliding_keep = 3;
    const std::vector<ChatMessage> opening{ChatMessage::system("be brief"),
                                           ChatMessage::user("one two three four five")};
    const auto first = fixture.provider->chat(turn(opening), {});
    auto session = fixture.runtime->model->contexts.front();
    const std::int64_t first_prompt = first.usage.prompt_tokens;

    const std::size_t mark = session->decodes.size();
    session->sampled = 0;
    std::vector<ChatMessage> next = opening;
    next.push_back(ChatMessage::assistant(first.message.content));
    next.push_back(ChatMessage::user("six seven"));
    const auto second = fixture.provider->chat(turn(next), {});

    CHECK(session->restores == std::vector<std::int64_t>{first_prompt - 4});
    const PromptDecode read = prompt_decode(*session, mark, second.usage.prompt_tokens);
    CHECK(read.start == first_prompt - 4);
    CHECK(read.count == second.usage.prompt_tokens - (first_prompt - 4));
}

TEST_CASE("a sliding model's tool step reads only the call and its result",
          "[backends][llamacpp][sliding]") {
    TemplateFixture fixture{kReadFileCall};
    fixture.runtime->sliding_window = 2;
    fixture.runtime->sliding_keep = 3;
    const std::vector<ChatMessage> asked{ChatMessage::user("what does notes.txt say, please")};
    const auto step = fixture.provider->chat(with_tools(asked), {});
    REQUIRE(step.message.tool_calls.size() == 1);
    auto session = fixture.runtime->model->contexts.front();
    const std::int64_t first_prompt = step.usage.prompt_tokens;

    const std::size_t mark = session->decodes.size();
    session->sampled = 0;
    ChatMessage call = ChatMessage::assistant("");
    call.tool_calls = step.message.tool_calls;
    apogee::harness::ToolResult result;
    result.tool_call_id = step.message.tool_calls.front().id;
    result.name = "read_file";
    result.content = "hi there";
    std::vector<ChatMessage> next = asked;
    next.push_back(call);
    next.push_back(ChatMessage::from_tool_result(result));
    const auto answer = fixture.provider->chat(with_tools(next), {});

    CHECK(session->restores == std::vector<std::int64_t>{first_prompt - 4});
    const PromptDecode read = prompt_decode(*session, mark, answer.usage.prompt_tokens);
    CHECK(read.start == first_prompt - 4);
}

TEST_CASE("with the window short and no checkpoint before the cut, a sliding model reads from 0",
          "[backends][llamacpp][sliding]") {
    TemplateFixture fixture{{"ok"}};
    fixture.runtime->sliding_window = 2;
    fixture.runtime->sliding_keep = 3;
    (void)fixture.provider->chat(turn({ChatMessage::user("alpha beta gamma delta epsilon")}), {});
    auto session = fixture.runtime->model->contexts.front();
    const std::size_t mark = session->decodes.size();
    session->sampled = 0;
    // Parts at the second token, before any checkpoint.
    const auto second = fixture.provider->chat(turn({ChatMessage::system("new")}), {});
    CHECK(session->restores.empty());
    CHECK(prompt_decode(*session, mark, second.usage.prompt_tokens).start == 0);
}

TEST_CASE("the checkpoint policy: replaced at a position, capped, never restored past",
          "[backends][llamacpp][checkpoint]") {
    struct Held {
        std::int64_t position = 0;
        int tag = 0;
    };

    std::vector<Held> held;
    for (std::int64_t position = 1; position <= 10; ++position) {
        apogee::backends::keep_checkpoint(held, Held{position, 0});
    }
    // The cap keeps the newest eight: the two oldest went first.
    REQUIRE(held.size() == apogee::backends::kMaxCheckpoints);
    CHECK(held.front().position == 3);
    CHECK(held.back().position == 10);
    // One at a position already held replaces it rather than doubling it.
    apogee::backends::keep_checkpoint(held, Held{10, 7});
    CHECK(held.size() == apogee::backends::kMaxCheckpoints);
    CHECK(held.back().tag == 7);

    // The newest at or before the divergence -- never one past it.
    CHECK(apogee::backends::checkpoint_for(held, 6)->position == 6);
    CHECK(apogee::backends::checkpoint_for(held, 100)->position == 10);
    CHECK(apogee::backends::checkpoint_for(held, 2) == held.end());
    apogee::backends::forget_checkpoints_after(held, 5);
    CHECK(held.back().position == 5);
    CHECK(held.size() == 3);
}

TEST_CASE("the system messages opening a conversation reach a local template as one",
          "[backends][llamacpp][tools][system]") {
    // Qwen3.5 and 3.8's templates raise on a second system message, and a
    // failed render drops the model to the fallback template -- and its tools
    // with it. The environment note (25d) ahead of a chat's own system prompt
    // is the ordinary case, so the leading run is joined into one.
    TemplateFixture fixture{{"fine"}};
    fixture.runtime->system_first_only = true;
    Captured seen;
    (void)fixture.provider->stream_chat(
        with_tools({ChatMessage::system("Environment: today"), ChatMessage::system(""),
                    ChatMessage::system("You are terse."), ChatMessage::user("hi")}),
        seen.options);

    REQUIRE(fixture.runtime->model != nullptr);
    const auto& rendered = fixture.runtime->model->chat_renders.back();
    REQUIRE(rendered.messages.size() == 2);
    CHECK(rendered.messages[0].role == apogee::harness::Role::System);
    CHECK(rendered.messages[0].content.plain_text() == "Environment: today\n\nYou are terse.");
    CHECK(rendered.messages[1].content.plain_text() == "hi");
    // The template rendered, tools and all: nothing fell back.
    CHECK(fixture.runtime->model->tokenized.back().find("tools: read_file") != std::string::npos);
    CHECK(seen.notices.empty());

    // One system message, or none, is passed through as it came.
    (void)fixture.provider->stream_chat(
        with_tools({ChatMessage::system("You are terse."), ChatMessage::user("again")}),
        seen.options);
    CHECK(fixture.runtime->model->chat_renders.back().messages.size() == 2);
    CHECK(fixture.runtime->model->chat_renders.back().messages[0].content.plain_text() ==
          "You are terse.");
}

// --- The window and its cache (26a) -------------------------------------------

namespace {

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

/// A GGUF header naming `trained` as its context length, in a scratch file.
[[nodiscard]] std::filesystem::path header_trained_for(std::uint32_t trained,
                                                       const std::string& name) {
    apogee::testing::GgufBuilder builder;
    builder.magic().u32(3).u64(1).u64(2);
    builder.string_kv("general.architecture", "qwen35");
    builder.u32_kv("qwen35.context_length", trained);
    builder.tensor("token_embd.weight");
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("apogee-window-" + name + ".gguf");
    REQUIRE(builder.write_to(path));
    return path;
}

}  // namespace

TEST_CASE("an unset window is 32K on a large model and the trained window on a small one",
          "[backends][llamacpp][window]") {
    // Left at the trained window, Qwen3.8-27B's cache was 16 GiB before the
    // first word.
    Sized large{262144};
    CHECK(large.session_window() == 32768);
    CHECK(large.provider->context_window() == 32768);
    CHECK(large.runtime->last_load.fit_window);

    Sized small{8192};
    CHECK(small.session_window() == 8192);
    CHECK(small.provider->context_window() == 8192);

    Sized unknown{0};
    CHECK(unknown.session_window() == 32768);
}

TEST_CASE("an explicit context_size is used exactly as written, and never fitted",
          "[backends][llamacpp][window]") {
    Sized set{262144, 200000, /*fitted=*/20000};
    CHECK(set.session_window() == 200000);
    CHECK(set.provider->context_window() == 200000);
    CHECK_FALSE(set.runtime->last_load.fit_window);

    // Past the trained window too: the user knows something Apogee does not.
    Sized past{4096, 5000};
    CHECK(past.session_window() == 5000);
}

TEST_CASE("free memory lowers the default window and never raises it",
          "[backends][llamacpp][window]") {
    Sized short_of_memory{262144, 0, /*fitted=*/20000};
    CHECK(short_of_memory.session_window() == 20000);
    CHECK(short_of_memory.provider->context_window() == 20000);

    Sized roomy{262144, 0, /*fitted=*/100000};
    CHECK(roomy.session_window() == 32768);
}

TEST_CASE("every context over the model stays inside the session's window",
          "[backends][llamacpp][window]") {
    Sized large{262144};
    (void)large.session_window();

    ChatRequest side = turn({ChatMessage::user("name this")});
    side.transient.side_request = true;
    side.max_tokens = 100000;
    (void)large.provider->chat(side, {});
    // Not the trained 262,144: a side request asking for more than the
    // window gets the window.
    CHECK(large.runtime->model->context_sizes.back() == 32768);

    side.max_tokens = 32;
    (void)large.provider->chat(side, {});
    CHECK(large.runtime->model->context_sizes.back() == 4096);
}

TEST_CASE("the cache type reaches the load: q8_0 unnamed by default, a named one as named",
          "[backends][llamacpp][window]") {
    Fixture fixture;
    (void)fixture.provider->chat(turn({ChatMessage::user("alpha")}), {});
    CHECK(fixture.runtime->last_load.cache_type == apogee::harness::KvCacheType::Q8_0);
    CHECK_FALSE(fixture.runtime->last_load.cache_type_named);
    CHECK(fixture.runtime->model->contexts.front()->cache_type() ==
          apogee::harness::KvCacheType::Q8_0);

    auto owned = std::make_unique<FakeLlamaRuntime>();
    FakeLlamaRuntime* runtime = owned.get();
    LlamaCppProvider::Options options;
    options.backend_name = "local";
    options.model_path = "/models/test.gguf";
    options.cache_type = apogee::harness::KvCacheType::F16;
    options.cache_type_named = true;
    LlamaCppProvider provider{std::move(options), std::move(owned)};
    (void)provider.chat(turn({ChatMessage::user("alpha")}), {});
    CHECK(runtime->last_load.cache_type == apogee::harness::KvCacheType::F16);
    CHECK(runtime->last_load.cache_type_named);
    CHECK(runtime->last_load.path == "/models/test.gguf");
}

TEST_CASE("before a load, the window comes from the model file's header",
          "[backends][llamacpp][window]") {
    // Asked before every turn, so it must never load 16 GB of weights.
    const std::filesystem::path large_file = header_trained_for(262144, "large");
    Sized large{262144, 0, 0, large_file.string()};
    CHECK(large.provider->context_window() == 32768);
    CHECK(large.runtime->loads == 0);

    const std::filesystem::path small_file = header_trained_for(2048, "small");
    Sized small{2048, 0, 0, small_file.string()};
    CHECK(small.provider->context_window() == 2048);

    // No readable header: unknown, never a guess.
    Sized missing{262144};
    CHECK(missing.provider->context_window() == 0);

    std::error_code code;
    std::filesystem::remove(large_file, code);
    std::filesystem::remove(small_file, code);
}

TEST_CASE("the verbose cache line states the window and how the cache is kept",
          "[backends][llamacpp][window]") {
    Sized large{262144};
    std::vector<std::string> lines;
    apogee::harness::StreamOptions options;
    options.on_status = [&lines](const apogee::harness::StatusEvent& event) {
        if (event.type == apogee::harness::StatusEvent::Type::PromptCache) {
            lines.push_back(event.detail);
        }
    };
    (void)large.provider->stream_chat(turn({ChatMessage::user("alpha beta")}), options);
    REQUIRE(lines.size() == 1);
    // The fake's context holds what the test pinned; the type is the model's.
    CHECK(lines.front().find(" · window 1000000, q8_0 cache") != std::string::npos);
}

TEST_CASE("a backend entry's window and cache reach the provider as written",
          "[backends][llamacpp][window]") {
    apogee::harness::BackendConfig entry;
    entry.type = apogee::harness::BackendType::LlamaCpp;
    entry.model_path = "/models/test.gguf";

    const LlamaCppProvider::Options unset = LlamaCppProvider::options_from("local", entry);
    CHECK(unset.context_size == 0);
    CHECK(unset.cache_type == apogee::harness::KvCacheType::Q8_0);
    CHECK_FALSE(unset.cache_type_named);

    entry.context_size = 65536;
    entry.cache_type = apogee::harness::KvCacheType::F16;
    const LlamaCppProvider::Options named = LlamaCppProvider::options_from("local", entry);
    CHECK(named.context_size == 65536);
    CHECK(named.cache_type == apogee::harness::KvCacheType::F16);
    CHECK(named.cache_type_named);
    CHECK(named.backend_name == "local");
    CHECK(named.model_path == "/models/test.gguf");
}

TEST_CASE("an image turn's context is the session's window, not the trained one",
          "[backends][llamacpp][window]") {
    Sized large{262144};
    large.runtime->vision = true;
    ChatMessage asked = ChatMessage::user("what is this?");
    asked.content = apogee::harness::MessageContent::from_parts(
        {apogee::harness::ContentPart::from_text("what is this?"),
         apogee::harness::ContentPart::from_image_url("data:image/png;base64,UE5H")});
    const ChatRequest image = turn({asked});
    // The fake's contexts decode no image, so the turn fails -- after its
    // context was made, which is what is asked here.
    CHECK_THROWS(large.provider->chat(image, {}));
    REQUIRE_FALSE(large.runtime->model->context_sizes.empty());
    CHECK(large.runtime->model->context_sizes.back() == 32768);
}

// --- A model file with no chat template ----------------------------------------

namespace {

/// A GGUF header in a scratch file, with or without a chat template.
[[nodiscard]] std::filesystem::path header_with_template(bool with_template,
                                                         const std::string& name) {
    apogee::testing::GgufBuilder builder;
    builder.magic().u32(3).u64(1).u64(with_template ? 2 : 1);
    builder.string_kv("general.architecture", "gemma4");
    if (with_template) {
        builder.string_kv("tokenizer.chat_template", "{{ messages }}");
    }
    builder.tensor("token_embd.weight");
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("apogee-template-" + name + ".gguf");
    REQUIRE(builder.write_to(path));
    return path;
}

/// The notices a run gave.
struct Notices {
    std::vector<std::string> lines;
    apogee::harness::StreamOptions options;

    Notices() {
        options.on_status = [this](const apogee::harness::StatusEvent& event) {
            if (event.type == apogee::harness::StatusEvent::Type::Notice) {
                lines.push_back(event.detail);
            }
        };
    }
};

}  // namespace

TEST_CASE("a guessed framing's turn marker ends the reply, and never reaches the screen",
          "[backends][llamacpp][template]") {
    // A base model given the ChatML fallback writes `<|im_end|>` out as text,
    // a piece at a time, and then invents the rest of the transcript.
    auto owned = std::make_unique<FakeLlamaRuntime>();
    owned->script_text = {"Red", "<|im_", "end|>", "user", "more"};
    owned->eog_token = -1;
    FakeLlamaRuntime* runtime = owned.get();
    LlamaCppProvider::Options options;
    options.backend_name = "local";
    options.model = "test-model";
    options.model_path = "/models/test.gguf";
    LlamaCppProvider provider{std::move(options), std::move(owned)};

    std::string streamed;
    apogee::harness::StreamOptions stream;
    stream.on_token = [&streamed](std::string_view piece) { streamed += piece; };
    const auto response = provider.stream_chat(turn({ChatMessage::user("colors")}), stream);

    CHECK(response.message.content.plain_text() == "Red");
    CHECK(streamed == "Red");
    // Stopped at the marker: "user" and "more" were never sampled.
    CHECK(response.usage.completion_tokens == 3);
    CHECK(runtime->model->contexts.front()->sampled == 3);
}

TEST_CASE("text that only looked like the start of a marker still reaches the screen",
          "[backends][llamacpp][template]") {
    auto owned = std::make_unique<FakeLlamaRuntime>();
    owned->script_text = {"a", "<|im", "possible"};
    owned->eog_token = -1;
    LlamaCppProvider::Options options;
    options.backend_name = "local";
    options.model = "test-model";
    options.model_path = "/models/test.gguf";
    LlamaCppProvider provider{std::move(options), std::move(owned)};
    const auto response = provider.chat(turn({ChatMessage::user("x")}), {});
    CHECK(response.message.content.plain_text() == "a<|impossible");

    // And one held when the reply ends is flushed rather than lost.
    auto ending = std::make_unique<FakeLlamaRuntime>();
    ending->script_text = {"b", "<|im_"};
    ending->eog_token = -1;
    LlamaCppProvider::Options more;
    more.backend_name = "local";
    more.model = "test-model";
    more.model_path = "/models/test.gguf";
    LlamaCppProvider cut{std::move(more), std::move(ending)};
    CHECK(cut.chat(turn({ChatMessage::user("x")}), {}).message.content.plain_text() == "b<|im_");
}

TEST_CASE("a stop string's token is not claimed by the cache, so the next turn has no gap",
          "[backends][llamacpp][kv]") {
    // The token that completes a stop string is never fed back. Claiming it
    // in the conversation's cached tokens let a next prompt that shares it
    // decode on one position past the cache's end -- the scripted context
    // refuses any decode past its end.
    TemplateFixture fixture{{"Hello", "<end>", "never"}};
    fixture.runtime->stops = {"<end>"};
    const auto first = fixture.provider->chat(turn({ChatMessage::user("hi")}), {});
    REQUIRE(first.message.content.plain_text() == "Hello");

    auto session = fixture.runtime->model->contexts.front();
    session->sampled = 0;
    const auto second =
        fixture.provider->chat(turn({ChatMessage::user("hi"), ChatMessage::assistant("Hello <end>"),
                                     ChatMessage::user("more")}),
                               {});
    CHECK(second.usage.prompt_tokens > first.usage.prompt_tokens);
}

TEST_CASE("a model file with no chat template is said to be a base model, once a conversation",
          "[backends][llamacpp][template]") {
    const std::filesystem::path bare = header_with_template(false, "bare");
    auto owned = std::make_unique<FakeLlamaRuntime>();
    owned->script_text = {"ok"};
    LlamaCppProvider::Options options;
    options.backend_name = "local";
    options.model = bare.string();
    options.model_path = bare.string();
    LlamaCppProvider provider{std::move(options), std::move(owned)};

    Notices first;
    (void)provider.stream_chat(with_tools({ChatMessage::user("what is the weather")}),
                               first.options);
    REQUIRE(first.lines.size() == 1);
    // Named by its file, not its whole path; what it is, and what to do.
    CHECK(first.lines.front().starts_with("apogee-template-bare.gguf ships no chat template"));
    CHECK(first.lines.front().find("base (pretrained) model") != std::string::npos);
    CHECK(first.lines.front().find("cannot use tools") != std::string::npos);
    CHECK(first.lines.front().find("'-it' or '-Instruct'") != std::string::npos);
    // The one wording `models info` uses too (26r).
    CHECK(first.lines.front() == "apogee-template-bare.gguf ships no chat template, so it is " +
                                     apogee::models::base_model_note());
    // And the fact itself crosses as plain data, for the surfaces to act on.
    CHECK(provider.model_behavior().base_model);

    // Once: a second turn, tools or not, is not told again.
    Notices second;
    (void)provider.stream_chat(turn({ChatMessage::user("and tomorrow")}), second.options);
    CHECK(second.lines.empty());

    // A file with a template is not a base model by this measure; one that
    // still cannot take tools is told only that.
    const std::filesystem::path templated = header_with_template(true, "templated");
    auto again = std::make_unique<FakeLlamaRuntime>();
    again->script_text = {"ok"};
    LlamaCppProvider::Options with;
    with.backend_name = "local";
    with.model = "templated";
    with.model_path = templated.string();
    LlamaCppProvider other{std::move(with), std::move(again)};
    Notices third;
    (void)other.stream_chat(with_tools({ChatMessage::user("x")}), third.options);
    REQUIRE(third.lines.size() == 1);
    CHECK(third.lines.front().find("is answering without tools") != std::string::npos);
    CHECK_FALSE(other.model_behavior().base_model);

    std::error_code code;
    std::filesystem::remove(bare, code);
    std::filesystem::remove(templated, code);
}

TEST_CASE(
    "a base model's spilled markers never reach the answer, and what is shown is what is kept",
    "[backends][llamacpp][template][base]") {
    const auto run = [](const std::filesystem::path& file, std::vector<std::string> pieces,
                        std::string& streamed) {
        auto owned = std::make_unique<FakeLlamaRuntime>();
        owned->script_text = std::move(pieces);
        owned->eog_token = -1;
        LlamaCppProvider::Options options;
        options.backend_name = "local";
        options.model = file.string();
        options.model_path = file.string();
        LlamaCppProvider provider{std::move(options), std::move(owned)};
        apogee::harness::StreamOptions stream;
        stream.on_token = [&streamed](std::string_view piece) { streamed += piece; };
        return provider.stream_chat(turn({ChatMessage::user("how warm is it?")}), stream)
            .message.content.plain_text();
    };
    // No template (26r): the motivating session's spill, a piece at a time.
    const std::filesystem::path bare = header_with_template(false, "spill");
    std::string streamed;
    CHECK(run(bare, {"It is 67", "°F.", "<|end", "|><|im", "|", "user"}, streamed) ==
          "It is 67°F.");
    CHECK(streamed == "It is 67°F.");
    // A fragment the stream ends inside: never emitted, never kept.
    streamed.clear();
    CHECK(run(bare, {"Done", "<|"}, streamed) == "Done");
    CHECK(streamed == "Done");

    // A model with a template may be quoting one: its text is left alone.
    const std::filesystem::path templated = header_with_template(true, "quoting");
    streamed.clear();
    const std::string quoted = run(templated, {"Write ", "<|end|>", " to end a turn."}, streamed);
    CHECK(quoted == "Write <|end|> to end a turn.");
    CHECK(streamed == quoted);

    std::error_code code;
    std::filesystem::remove(bare, code);
    std::filesystem::remove(templated, code);
}

// --- Audio, for the transcription role (26b) -------------------------------

// --- Images, audio and video (26e) ----------------------------------------

namespace {

/// A provider on a model whose projector sees, and maybe hears, and whose
/// contexts decode media rather than refusing it.
struct MediaFixture {
    FakeLlamaRuntime* runtime = nullptr;
    std::unique_ptr<LlamaCppProvider> provider;

    explicit MediaFixture(bool audio = false, bool vision = true) {
        auto owned = std::make_unique<FakeLlamaRuntime>();
        owned->eog_token = -1;
        owned->vision = vision;
        owned->audio = audio;
        owned->sample_rate = 16000;
        owned->decodes_media = true;
        runtime = owned.get();
        LlamaCppProvider::Options options;
        options.backend_name = "local";
        options.model = "test-model";
        options.model_path = "/models/test.gguf";
        provider = std::make_unique<LlamaCppProvider>(std::move(options), std::move(owned));
    }
};

[[nodiscard]] apogee::harness::ContentPart frame_part() {
    apogee::harness::ContentPart part =
        apogee::harness::ContentPart::from_image_url("data:image/jpeg;base64,RlJBTUU=");
    part.video_frame = true;
    return part;
}

}  // namespace

TEST_CASE("each picture and sound is marked where it sits in its message",
          "[backends][llamacpp][media]") {
    MediaFixture fixture{true};
    ChatMessage clip = ChatMessage::user("");
    clip.content = apogee::harness::MessageContent::from_parts(
        {apogee::harness::ContentPart::from_text("[0:00]"), frame_part(), frame_part(),
         apogee::harness::ContentPart::from_text("[0:05]"), frame_part(),
         apogee::harness::ContentPart::from_audio("UklGRgAA", "wav"),
         apogee::harness::ContentPart::from_text("what happens?")});
    const ChatRequest request = turn({ChatMessage::system("be brief"), ChatMessage::user("earlier"),
                                      ChatMessage::assistant("noted"), clip});
    (void)fixture.provider->chat(request, {});

    const auto& model = *fixture.runtime->model;
    REQUIRE(model.contexts.size() == 1);
    const auto& context = *model.contexts.back();
    REQUIRE(context.media_decodes.size() == 1);
    const std::vector<apogee::backends::MediaInput>& media = context.media_decodes.front();
    REQUIRE(media.size() == 4);
    CHECK(media[0].frame);
    CHECK(media[0].bytes == "FRAME");
    CHECK(media[2].frame);
    CHECK_FALSE(media[3].frame);
    CHECK(media[3].bytes.starts_with("RIFF"));

    // In the message that carried them, after the earlier turns -- not
    // stacked at the top of the prompt -- frames against each other, their
    // times between them.
    const std::string& text = context.media_texts.front();
    CHECK(text.find("earlier") < text.find("<image>"));
    CHECK(text.find("[0:00]<image><image>[0:05]<image><image>\nwhat happens?") !=
          std::string::npos);
}

TEST_CASE("a turn with media says what it encoded under --verbose", "[backends][llamacpp][media]") {
    MediaFixture fixture{true};
    ChatMessage asked = ChatMessage::user("");
    asked.content = apogee::harness::MessageContent::from_parts(
        {apogee::harness::ContentPart::from_image_url("data:image/png;base64,UE5H"), frame_part(),
         apogee::harness::ContentPart::from_audio("UklGRgAA", "wav"),
         apogee::harness::ContentPart::from_text("and this?")});
    std::vector<std::string> said;
    apogee::harness::StreamOptions options;
    options.on_status = [&said](const apogee::harness::StatusEvent& event) {
        if (event.type == apogee::harness::StatusEvent::Type::PromptCache) {
            said.push_back(event.detail);
        }
    };
    (void)fixture.provider->stream_chat(turn({asked}), options);
    REQUIRE(said.size() == 1);
    CHECK(said.front().starts_with("prompt "));
    CHECK(said.front().find("with 1 image, 1 frame, 1 sound encoded: read whole on a fresh "
                            "context in ") != std::string::npos);
}

TEST_CASE("audio a projector cannot hear is refused, not ignored", "[backends][llamacpp][media]") {
    MediaFixture fixture{false};
    ChatMessage heard = ChatMessage::user("");
    heard.content = apogee::harness::MessageContent::from_parts(
        {apogee::harness::ContentPart::from_audio("UklGRgAA", "wav"),
         apogee::harness::ContentPart::from_text("what was said?")});
    try {
        (void)fixture.provider->chat(turn({heard}), {});
        FAIL("an audio turn on a projector without audio ran");
    } catch (const apogee::harness::ProviderError& e) {
        CHECK(std::string{e.what()}.find("no audio encoder") != std::string::npos);
    }
}

TEST_CASE("a remote image is never fetched: the turn stays on the text path",
          "[backends][llamacpp][media]") {
    MediaFixture fixture;
    ChatMessage asked = ChatMessage::user("");
    asked.content = apogee::harness::MessageContent::from_parts(
        {apogee::harness::ContentPart::from_image_url("https://example.com/a.png"),
         apogee::harness::ContentPart::from_text("what is it?")});
    (void)fixture.provider->chat(turn({asked}), {});
    REQUIRE_FALSE(fixture.runtime->model->contexts.empty());
    CHECK(fixture.runtime->model->contexts.back()->media_decodes.empty());
}

TEST_CASE("the projector's rate is the model's own, once it is loaded",
          "[backends][llamacpp][media]") {
    MediaFixture fixture{true};
    fixture.runtime->sample_rate = 24000;
    // Before a load nothing can say.
    CHECK(fixture.provider->audio_sample_rate() == 0);
    (void)fixture.provider->chat(turn({ChatMessage::user("hello")}), {});
    CHECK(fixture.provider->audio_sample_rate() == 24000);
}

// --- Sampling (26h) ------------------------------------------------------------

namespace {

/// A provider over a model whose header names `architecture` and, where the
/// file recommends any, its `general.sampling.*` values -- written to a
/// scratch file the provider reads as it would a real model's.
struct Sampled {
    std::filesystem::path path;
    FakeLlamaRuntime* runtime = nullptr;
    std::unique_ptr<LlamaCppProvider> provider;

    Sampled(const std::string& architecture, std::optional<float> file_temperature,
            LlamaCppProvider::Options options = {})
        : path{std::filesystem::temp_directory_path() /
               ("apogee-sampling-" + std::to_string(std::random_device{}()) + ".gguf")} {
        apogee::testing::GgufBuilder builder;
        builder.magic().u32(3).u64(1).u64(file_temperature.has_value() ? 3 : 2);
        builder.string_kv("general.architecture", architecture);
        builder.string_kv("tokenizer.chat_template", "{{ messages }}");
        if (file_temperature.has_value()) {
            builder.f32_kv("general.sampling.temp", *file_temperature);
        }
        builder.tensor("token_embd.weight");
        REQUIRE(builder.write_to(path));
        auto owned = std::make_unique<FakeLlamaRuntime>();
        runtime = owned.get();
        options.backend_name = "local";
        options.model = "test-model";
        options.model_path = path.string();
        provider = std::make_unique<LlamaCppProvider>(std::move(options), std::move(owned));
    }

    Sampled(const Sampled&) = delete;
    Sampled& operator=(const Sampled&) = delete;
    Sampled(Sampled&&) = delete;
    Sampled& operator=(Sampled&&) = delete;

    ~Sampled() {
        std::error_code code;
        std::filesystem::remove(path, code);
    }

    /// What the last generation sampled with.
    [[nodiscard]] apogee::backends::SamplingSettings last(const ChatRequest& request) {
        (void)provider->chat(request, {});
        REQUIRE(runtime->model != nullptr);
        REQUIRE_FALSE(runtime->model->contexts.front()->samplings.empty());
        return runtime->model->contexts.front()->samplings.back();
    }
};

}  // namespace

TEST_CASE("the request's temperature reaches the sampler", "[backends][llamacpp][sampling]") {
    // The regression this item fixes: `-t 0.9` was accepted and the model
    // sampled greedily anyway, on every local model.
    Sampled sampled{"llama", std::nullopt};
    ChatRequest request = turn({ChatMessage::user("alpha beta")});
    request.temperature = 0.9;
    const apogee::backends::SamplingSettings settings = sampled.last(request);
    CHECK(settings.temperature == 0.9);
    CHECK_FALSE(settings.greedy());
}

TEST_CASE("temperature 0 is greedy whatever the rungs below suggest",
          "[backends][sampling][llamacpp]") {
    // The file recommends sampling; the request asks for greedy, and gets it.
    Sampled sampled{"qwen35", 1.0F};
    ChatRequest request = turn({ChatMessage::user("alpha beta")});
    request.temperature = 0.0;
    CHECK(sampled.last(request).greedy());
}

TEST_CASE("with no temperature asked, the model file's recommendation is used",
          "[backends][llamacpp][sampling]") {
    Sampled sampled{"llama", 0.6F};
    const apogee::backends::SamplingSettings settings =
        sampled.last(turn({ChatMessage::user("alpha beta")}));
    CHECK(settings.temperature == static_cast<double>(0.6F));
    CHECK(sampled.provider->sampling_for(turn({})).temperature.source ==
          apogee::backends::SamplingSource::ModelFile);
}

TEST_CASE("a file that says nothing gets its family's card, thinking or not",
          "[backends][llamacpp][sampling]") {
    // Qwen's card: 0.6 with thinking, 0.7 without -- greedy loops on its
    // thinking models, which is why "nothing configured" must not mean greedy.
    Sampled sampled{"qwen35", std::nullopt};
    const apogee::backends::SamplingSettings thinking =
        sampled.last(turn({ChatMessage::user("alpha beta")}));
    CHECK(thinking.temperature == 0.6);
    CHECK(thinking.top_k == 20);

    ChatRequest answering = turn({ChatMessage::user("alpha beta")});
    answering.thinking.mode = apogee::harness::ThinkingMode::Off;
    CHECK(sampled.provider->sampling_for(answering).temperature.value == 0.7);
}

TEST_CASE("an unprofiled model that recommends nothing samples greedily",
          "[backends][llamacpp][sampling]") {
    Sampled sampled{"mystery-arch", std::nullopt};
    CHECK(sampled.last(turn({ChatMessage::user("alpha beta")})).greedy());
}

TEST_CASE("the config's sampling and seed reach the sampler", "[backends][llamacpp][sampling]") {
    apogee::harness::BackendConfig entry;
    entry.type = apogee::harness::BackendType::LlamaCpp;
    entry.model_path = "/models/test.gguf";
    entry.temperature = 0.8;
    entry.top_p = 0.9;
    entry.top_k = 40;
    entry.min_p = 0.05;
    entry.repeat_penalty = 1.1;
    entry.presence_penalty = 0.4;
    entry.seed = 7;
    const LlamaCppProvider::Options options = LlamaCppProvider::options_from("local", entry);
    CHECK(options.sampling.temperature == 0.8);
    CHECK(options.sampling.top_k == 40);
    REQUIRE(options.seed.has_value());
    CHECK(*options.seed == 7U);

    // The config outranks the file, and the request outranks the config.
    Sampled sampled{"qwen35", 1.0F, options};
    const apogee::backends::SamplingSettings settings =
        sampled.last(turn({ChatMessage::user("alpha beta")}));
    CHECK(settings.temperature == 0.8);
    CHECK(settings.top_p == 0.9);
    CHECK(settings.top_k == 40);
    CHECK(settings.min_p == 0.05);
    CHECK(settings.repeat_penalty == 1.1);
    CHECK(settings.presence_penalty == 0.4);
    REQUIRE(settings.seed.has_value());
    CHECK(*settings.seed == 7U);

    ChatRequest asked = turn({ChatMessage::user("alpha beta")});
    asked.temperature = 0.3;
    CHECK(sampled.last(asked).temperature == 0.3);
}

TEST_CASE("a side request samples by its own temperature, not the turn's",
          "[backends][llamacpp][sampling]") {
    // The title, the clerk, the judge: each asks for 0, and each gets greedy
    // on its own context while the conversation keeps its settings.
    Sampled sampled{"qwen35", std::nullopt};
    (void)sampled.provider->chat(turn({ChatMessage::user("alpha beta")}), {});
    ChatRequest side = turn({ChatMessage::user("title this")});
    side.temperature = 0.0;
    side.transient.side_request = true;
    (void)sampled.provider->chat(side, {});
    REQUIRE(sampled.runtime->model->contexts.size() >= 2);
    CHECK(sampled.runtime->model->contexts.back()->samplings.back().greedy());
    CHECK_FALSE(sampled.runtime->model->contexts.front()->samplings.back().greedy());
}
