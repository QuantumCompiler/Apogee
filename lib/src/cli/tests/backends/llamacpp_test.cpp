#include "backends/llamacpp.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "backends/llamacpp_tokens.h"
#include "harness/errors.h"
#include "harness/harness.h"
#include "support/fake_llama.h"

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
    // over Ommi's spawn-per-turn model collapses.
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
    // Ommi's SideRequest lesson, ported: its async titler ran with the session's
    // prompt-cache flags and could clobber the KV state of the very conversation
    // it was summarising -- and write the same file concurrently with the next
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
        events.push_back(event);
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
