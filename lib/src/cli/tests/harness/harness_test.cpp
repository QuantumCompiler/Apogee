#include "harness/harness.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

#include "backends/mock.h"
#include "harness/config.h"
#include "harness/context_windows.h"
#include "harness/errors.h"

using apogee::backends::MockEmbeddingProvider;
using apogee::backends::MockProvider;
using apogee::backends::MockTurn;
using apogee::harness::CancellationToken;
using apogee::harness::CancelledError;
using apogee::harness::ChatMessage;
using apogee::harness::ChatRequest;
using apogee::harness::Config;
using apogee::harness::Harness;
using apogee::harness::LLMProvider;
using apogee::harness::NoAvailableBackendError;
using apogee::harness::ProviderNotRegisteredError;
using apogee::harness::StreamOptions;

namespace {

Config config_from(std::string_view yaml) {
    return apogee::harness::parse_config(yaml, "<test>");
}

std::shared_ptr<MockProvider> mock(std::string backend, std::string model,
                                   std::vector<MockTurn> turns = {}) {
    MockProvider::Options options;
    options.backend_name = std::move(backend);
    options.model = std::move(model);
    options.turns = std::move(turns);
    return std::make_shared<MockProvider>(std::move(options));
}

ChatRequest request_for(std::string model) {
    ChatRequest request;
    request.model = std::move(model);
    request.messages = {ChatMessage::user("hello")};
    return request;
}

}  // namespace

// ---------------------------------------------------------------------------
// Router precedence -- the three rungs, table-tested
// ---------------------------------------------------------------------------

TEST_CASE("the router resolves through all three rungs in order", "[harness][router]") {
    // Rung order is the whole design. If a `model:` field could beat a backend
    // KEY, then two entries naming the same model would make `-m <key>`
    // ambiguous — the user's most explicit possible request, ignored.
    const Config config = config_from(R"(
models:
  default: fallback
backends:
  primary:
    type: mock
    model: shared-model
  secondary:
    type: mock
    model: other-model
  fallback:
    type: mock
    model: fallback-model
)");

    Harness harness{config};
    harness.register_provider("primary", mock("primary", "shared-model"));
    harness.register_provider("secondary", mock("secondary", "other-model"));
    harness.register_provider("fallback", mock("fallback", "fallback-model"));
    harness.use_default_router();

    struct Case {
        std::string requested;
        std::string expected_backend;
        std::string rung;
    };

    const std::vector<Case> cases{
        {"primary", "primary", "1: exact backend key"},
        {"secondary", "secondary", "1: exact backend key"},
        {"other-model", "secondary", "2: a backend entry's model: field"},
        {"shared-model", "primary", "2: a backend entry's model: field"},
        {"", "fallback", "3: models.default"},
        {"totally-unknown", "fallback", "3: models.default catches the miss"},
    };

    for (const Case& c : cases) {
        INFO("rung " << c.rung << " -- requested '" << c.requested << "'");
        CHECK(harness.route(c.requested).backend_name() == c.expected_backend);
    }
}

TEST_CASE("a backend key beats another entry's model field", "[harness][router]") {
    // The precedence collision, isolated: "shared" is BOTH a backend key and
    // another entry's model name.
    const Config config = config_from(R"(
models:
  default: other
backends:
  shared:
    type: mock
    model: a-model
  other:
    type: mock
    model: shared
)");

    Harness harness{config};
    harness.register_provider("shared", mock("shared", "a-model"));
    harness.register_provider("other", mock("other", "shared"));
    harness.use_default_router();

    CHECK(harness.route("shared").backend_name() == "shared");
}

TEST_CASE("routing normalizes case, dots, and colons", "[harness][router]") {
    // The same model gets written three ways across a config file and a command
    // line. A user who typed one should not get "no such backend" because the
    // file spells it another.
    const Config config = config_from(R"(
models:
  default: Qwen3.5
backends:
  Qwen3.5:
    type: mock
    model: "qwen3:5b"
)");

    Harness harness{config};
    harness.register_provider("Qwen3.5", mock("Qwen3.5", "qwen3:5b"));
    harness.use_default_router();

    for (const std::string& spelling :
         {"Qwen3.5", "qwen3.5", "qwen3-5", "QWEN3-5", "qwen3:5b", "qwen3-5b"}) {
        INFO("spelling: " << spelling);
        CHECK(harness.route(spelling).backend_name() == "Qwen3.5");
    }
}

TEST_CASE("an unroutable model is a typed error naming what would have worked",
          "[harness][router]") {
    // A routing failure that does not say what IS configured just sends the
    // user to the config file to guess.
    const Config config = config_from("backends:\n  a:\n    type: mock\n");
    Harness harness{config};
    harness.register_provider("a", mock("a", "model-a"));
    harness.use_default_router();

    try {
        (void)harness.route("nope");
        FAIL("expected NoAvailableBackendError");
    } catch (const NoAvailableBackendError& e) {
        CHECK(e.model() == "nope");
        const std::string message = e.what();
        CHECK(message.find("nope") != std::string::npos);
        CHECK(message.find("a") != std::string::npos);
        CHECK(message.find("models.default") != std::string::npos);
    }
}

TEST_CASE("with no backends at all the error names the fix", "[harness][router]") {
    Harness harness{Config{}};
    harness.use_default_router();
    try {
        (void)harness.route("anything");
        FAIL("expected NoAvailableBackendError");
    } catch (const NoAvailableBackendError& e) {
        CHECK(std::string{e.what()}.find("config add-backend") != std::string::npos);
    }
}

TEST_CASE("a single registered backend is NOT routed to implicitly", "[harness][router]") {
    // Ommi's fourth rung, deliberately not ported: it papers over an unset
    // models.default in a way that stops working the moment a second backend
    // is added -- exactly when the user has least idea why routing changed.
    const Config config = config_from("backends:\n  only:\n    type: mock\n");
    Harness harness{config};
    harness.register_provider("only", mock("only", "m"));
    harness.use_default_router();

    CHECK(harness.route("only").backend_name() == "only");
    CHECK_THROWS_AS((void)harness.route(""), NoAvailableBackendError);
}

TEST_CASE("a provider registered without a config entry is still routable by key",
          "[harness][router]") {
    // Downstream items' tests register a mock with no config file at all.
    Harness harness{Config{}};
    harness.register_provider("ad-hoc", mock("ad-hoc", "m"));
    harness.use_default_router();

    CHECK(harness.route("ad-hoc").backend_name() == "ad-hoc");
}

TEST_CASE("routing before a router exists is a typed error", "[harness][router]") {
    Harness harness{Config{}};
    harness.register_provider("a", mock("a", "m"));
    CHECK_THROWS_AS((void)harness.route("a"), NoAvailableBackendError);
}

TEST_CASE("an unregistered provider name is a typed error", "[harness]") {
    Harness harness{Config{}};
    try {
        (void)harness.provider("ghost");
        FAIL("expected ProviderNotRegisteredError");
    } catch (const ProviderNotRegisteredError& e) {
        CHECK(e.name() == "ghost");
    }
}

// ---------------------------------------------------------------------------
// Streaming and cancellation
// ---------------------------------------------------------------------------

TEST_CASE("a streamed chat round-trips through the harness", "[harness][stream]") {
    Harness harness{Config{}};
    harness.register_provider("m", mock("m", "mock-1", {MockTurn{"hello world", {}, {}, {}}}));
    harness.use_default_router();

    std::string streamed;
    StreamOptions options;
    options.on_token = [&streamed](std::string_view chunk) { streamed += chunk; };

    const auto response = harness.stream_chat(request_for("m"), options);

    CHECK(streamed == "hello world");
    // The full response comes back too, so a caller that both renders live and
    // persists history never has to reassemble it from chunks -- which is
    // precisely where whitespace and tool-call fragments get lost.
    CHECK(response.message.content.plain_text() == "hello world");
    CHECK(response.model == "mock-1");
}

TEST_CASE("cancellation mid-stream stops promptly and throws", "[harness][stream][cancel]") {
    MockProvider::Options options;
    options.backend_name = "m";
    options.turns = {MockTurn{"aaaabbbbccccdddd", {}, {}, {}}};
    options.chunk_size = 4;
    auto provider = std::make_shared<MockProvider>(std::move(options));

    Harness harness{Config{}};
    harness.register_provider("m", provider);
    harness.use_default_router();

    const CancellationToken token = CancellationToken::create();
    std::string streamed;
    StreamOptions stream;
    stream.cancellation = token;
    stream.on_token = [&streamed, &token](std::string_view chunk) {
        streamed += chunk;
        if (streamed.size() >= 8) {
            token.cancel();
        }
    };

    CHECK_THROWS_AS((void)harness.stream_chat(request_for("m"), stream), CancelledError);
    // Stopped where it was told to, not after draining the rest.
    CHECK(streamed.size() == 8);
}

TEST_CASE("an already-cancelled token yields nothing at all", "[harness][cancel]") {
    Harness harness{Config{}};
    harness.register_provider("m", mock("m", "mock-1", {MockTurn{"text", {}, {}, {}}}));
    harness.use_default_router();

    const CancellationToken token = CancellationToken::create();
    token.cancel();

    bool emitted = false;
    StreamOptions options;
    options.cancellation = token;
    options.on_token = [&emitted](std::string_view) { emitted = true; };

    CHECK_THROWS_AS((void)harness.stream_chat(request_for("m"), options), CancelledError);
    CHECK_FALSE(emitted);
    CHECK_THROWS_AS((void)harness.chat(request_for("m"), token), CancelledError);
}

TEST_CASE("a default-constructed token never cancels", "[harness][cancel]") {
    const CancellationToken token;
    CHECK_FALSE(token.stop_requested());
    CHECK_NOTHROW(token.throw_if_cancelled());
    token.cancel();  // no-op, and must not crash
    CHECK_FALSE(token.stop_requested());
}

TEST_CASE("token copies share one cancellation flag", "[harness][cancel]") {
    const CancellationToken original = CancellationToken::create();
    const CancellationToken copy = original;  // NOLINT(performance-unnecessary-copy-initialization)
    original.cancel();
    CHECK(copy.stop_requested());
}

// ---------------------------------------------------------------------------
// Capability probes
// ---------------------------------------------------------------------------

TEST_CASE("can_embed is answerable without a cast at the call site", "[harness][capability]") {
    // The acceptance criterion: discovery happens inside the Harness, so no
    // caller ever writes dynamic_cast -- which is what stops "can this embed?"
    // from becoming a switch over backend types.
    Harness harness{Config{}};
    harness.register_provider("chat-only", mock("chat-only", "m"));
    harness.register_provider("embedder", std::make_shared<MockEmbeddingProvider>("embedder", 8));
    harness.use_default_router();

    CHECK_FALSE(harness.can_embed("chat-only"));
    CHECK(harness.can_embed("embedder"));

    auto* embedder = harness.embedder_for("embedder");
    REQUIRE(embedder != nullptr);
    CHECK(embedder->embedding_dimensions() == 8);
    CHECK(harness.embedder_for("chat-only") == nullptr);
}

TEST_CASE("capability probes on an unroutable model answer no, not throw",
          "[harness][capability]") {
    // A caller asking about a capability should not have to handle a routing
    // failure as well -- an unknown backend cannot embed either way.
    Harness harness{Config{}};
    harness.use_default_router();

    CHECK_FALSE(harness.can_embed("ghost"));
    CHECK(harness.embedder_for("ghost") == nullptr);
    CHECK_FALSE(harness.uses_in_text_tool_calls("ghost"));
    CHECK_FALSE(harness.model_behavior_for("ghost").known());
    CHECK_FALSE(harness.model_status("ghost").has_value());
}

TEST_CASE("embedding produces stable, input-dependent vectors", "[harness][capability]") {
    MockEmbeddingProvider provider{"e", 8};
    const auto first = provider.embed({"alpha", "beta", "alpha"}, {});

    REQUIRE(first.size() == 3);
    CHECK(first[0].size() == 8);
    CHECK(first[0] == first[2]);  // identical text embeds identically
    CHECK(first[0] != first[1]);  // different text does not
}

// ---------------------------------------------------------------------------
// ModelBehavior
// ---------------------------------------------------------------------------

TEST_CASE("the zero-value ModelBehavior means unknown, and unknown is permissive",
          "[harness][behavior]") {
    // Documented contract, pinned here because the consequence of getting it
    // backwards is invisible in review: a tool call goes unrecognised, nothing
    // dispatches, AND the raw markup is printed to the user as if it were the
    // answer. Being too eager merely suppresses a line of text.
    const apogee::harness::ModelBehavior unknown;

    CHECK_FALSE(unknown.known());
    CHECK(unknown.profile.empty());
    CHECK(unknown.tool_call_openers.empty());
    CHECK_FALSE(unknown.native_tool_calls);
    CHECK(unknown.reasoning_tags.empty());
    CHECK_FALSE(unknown.has_opener("TOOL_CALL:"));
}

TEST_CASE("a provider that reports behavior is discovered through the harness",
          "[harness][behavior]") {
    apogee::harness::ModelBehavior behavior;
    behavior.profile = "llama3";
    behavior.tool_call_openers = {"TOOL_CALL:", "<|python_tag|>"};
    behavior.native_tool_calls = true;
    behavior.reasoning_tags = {{"<think>", "</think>"}};

    MockProvider::Options options;
    options.backend_name = "local";
    options.behavior = behavior;

    Harness harness{Config{}};
    harness.register_provider("local", std::make_shared<MockProvider>(std::move(options)));
    harness.use_default_router();

    const auto resolved = harness.model_behavior_for("local");
    CHECK(resolved.known());
    CHECK(resolved.profile == "llama3");
    CHECK(resolved.has_opener("TOOL_CALL:"));
    CHECK_FALSE(resolved.has_opener("NOPE:"));
    REQUIRE(resolved.reasoning_tags.size() == 1);
    CHECK(resolved.reasoning_tags[0].first == "<think>");
}

// ---------------------------------------------------------------------------
// Listing and context windows
// ---------------------------------------------------------------------------

TEST_CASE("one failing provider does not empty the model list", "[harness]") {
    // A user running `apogee models` with one bad API key still needs to see
    // the rest -- that is exactly when they need the list most.
    class FailingProvider final : public LLMProvider {
    public:
        [[nodiscard]] std::string_view backend_name() const noexcept override {
            return "bad";
        }

        [[nodiscard]] apogee::harness::ChatResponse chat(const ChatRequest&,
                                                         const CancellationToken&) override {
            throw apogee::harness::ProviderError("bad", "unreachable");
        }

        [[nodiscard]] apogee::harness::ChatResponse stream_chat(const ChatRequest&,
                                                                const StreamOptions&) override {
            throw apogee::harness::ProviderError("bad", "unreachable");
        }

        [[nodiscard]] std::vector<apogee::harness::ModelInfo> list_models(
            const CancellationToken&) override {
            throw apogee::harness::ProviderError("bad", "unreachable");
        }
    };

    Harness harness{Config{}};
    harness.register_provider("good", mock("good", "good-model"));
    harness.register_provider("bad", std::make_shared<FailingProvider>());
    harness.use_default_router();

    const auto models = harness.list_all_models();
    REQUIRE(models.size() == 1);
    CHECK(models[0].id == "good-model");
}

TEST_CASE("a configured context_size beats the fallback table", "[harness][context]") {
    // The user knows something we do not -- a model served with a deliberately
    // shortened window, for instance.
    const Config config = config_from(R"(
backends:
  claude:
    type: mock
    model: claude-sonnet-5
    context_size: 4096
  default-window:
    type: mock
    model: claude-sonnet-5
)");
    Harness harness{config};

    CHECK(harness.context_window_for_model("claude") == 4096);
    CHECK(harness.context_window_for_model("default-window") == 200000);
}

TEST_CASE("an unknown model reports an unknown window, not a guess", "[harness][context]") {
    using apogee::harness::context_window_for;

    CHECK(context_window_for("claude-sonnet-5") == 200000);
    CHECK(context_window_for("CLAUDE-SONNET-5-20260101") == 200000);
    CHECK(context_window_for("gpt-5") == 400000);
    CHECK(context_window_for("gemini-2.5-pro") == 1048576);

    // 0 means unknown, never unlimited. A caller must skip context warnings
    // rather than invent a threshold that either nags or never fires.
    CHECK(context_window_for("some-local-gguf") == 0);
    CHECK(context_window_for("") == 0);

    CHECK(apogee::harness::resolve_context_window(1024, "claude-sonnet-5") == 1024);
    CHECK(apogee::harness::resolve_context_window(0, "claude-sonnet-5") == 200000);
}

TEST_CASE("the longest matching prefix wins in the window table", "[harness][context]") {
    // "claude-sonnet" must beat the generic "claude-" row.
    CHECK(apogee::harness::context_window_for("gpt-4o") == 128000);
    CHECK(apogee::harness::context_window_for("gpt-4") == 8192);
    CHECK(apogee::harness::context_window_for("gpt-4.1") == 1047576);
}

// ---------------------------------------------------------------------------
// Preload
// ---------------------------------------------------------------------------

namespace {

/// A provider whose model loads lazily and says so.
class LazyProvider final : public apogee::harness::LLMProvider,
                           public apogee::harness::StatusReporting {
public:
    bool loaded = false;

    [[nodiscard]] std::string_view backend_name() const noexcept override {
        return "lazy";
    }

    [[nodiscard]] apogee::harness::ChatResponse chat(
        const apogee::harness::ChatRequest& /*request*/,
        const apogee::harness::CancellationToken& /*cancellation*/) override {
        return {};
    }

    [[nodiscard]] apogee::harness::ChatResponse stream_chat(
        const apogee::harness::ChatRequest& /*request*/,
        const apogee::harness::StreamOptions& /*options*/) override {
        return {};
    }

    [[nodiscard]] std::vector<apogee::harness::ModelInfo> list_models(
        const apogee::harness::CancellationToken& /*cancellation*/) override {
        return {};
    }

    [[nodiscard]] apogee::harness::StatusEvent model_status() const override {
        apogee::harness::StatusEvent event;
        event.type = loaded ? apogee::harness::StatusEvent::Type::ModelReady
                            : apogee::harness::StatusEvent::Type::ModelLoading;
        return event;
    }

    void preload(const apogee::harness::StatusSink& on_status) override {
        loaded = true;
        if (on_status) {
            on_status(model_status());
        }
    }
};

}  // namespace

TEST_CASE("preload_model loads a lazily loading backend and answers no for the rest",
          "[harness][capability]") {
    // Asked through the harness, never through a cast at the call site -- and
    // only a backend that REPORTS a load state is asked, so a cloud provider is
    // never sent a warm-up request in the name of preloading.
    auto lazy = std::make_shared<LazyProvider>();
    Harness harness{Config{}};
    harness.register_provider("lazy", lazy);
    harness.register_provider("mock", std::make_shared<MockProvider>(MockProvider::Options{}));
    harness.use_default_router();

    REQUIRE(harness.model_status("lazy")->type == apogee::harness::StatusEvent::Type::ModelLoading);
    int reported = 0;
    CHECK(harness.preload_model("lazy",
                                [&reported](const apogee::harness::StatusEvent&) { ++reported; }));
    CHECK(lazy->loaded);
    CHECK(reported == 1);
    CHECK(harness.model_status("lazy")->type == apogee::harness::StatusEvent::Type::ModelReady);

    CHECK_FALSE(harness.preload_model("mock", {}));
    CHECK_FALSE(harness.preload_model("ghost", {}));
}
