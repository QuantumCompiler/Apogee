#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "harness/behavior.h"
#include "harness/cancellation.h"
#include "harness/types.h"

/// The interface every model backend implements.
///
/// Ommi's recorded lesson: getting streaming, cancellation, multimodal content,
/// and tool structures into the interface on day one is what let it keep four
/// backends and several surfaces consistent. Adding any of them later means
/// touching every implementation and every caller at once.
///
/// Adding a backend is: implement LLMProvider, add a `type:` row to the config
/// enum, register it. Nothing above the backends layer changes.
namespace apogee::harness {

/// Receives streamed text as it arrives.
///
/// Callbacks rather than coroutine generators (decided 2026-08-25): this is the
/// Ommi-equivalent shape, it is simple to implement in every backend, and it
/// does not force the whole call stack to become coroutines. A provider calls
/// this from whatever thread it reads on, so a sink that touches shared state
/// must do its own locking.
using TokenSink = std::function<void(std::string_view)>;

/// Receives progress events. Optional -- an empty sink is valid and common.
using StatusSink = std::function<void(const StatusEvent&)>;

/// Receives extended-thinking text as it arrives.
///
/// **A separate sink from TokenSink, deliberately.** The alternative -- wrapping
/// reasoning in in-band markers like `<thinking>…</thinking>` and demuxing
/// downstream -- is what a channel-typed harness is forced into, and it costs a
/// filter that must tolerate markers split across reads plus a permanent
/// ambiguity: a *literal* `<thinking>` in the model's answer is now
/// indistinguishable from the real thing. A typed sink gets this for free.
///
/// Whatever a surface does with thinking, it is **display and loop metadata
/// only**: never persisted to history, never in the text returned to a
/// programmatic caller, never in a served API response. It bloats every later
/// prompt if it re-enters history, and an OpenAI-format client does not expect
/// reasoning in its content field.
using ThinkingSink = std::function<void(std::string_view)>;

/// Options common to a streamed call.
struct StreamOptions {
    TokenSink on_token;
    ThinkingSink on_thinking;
    StatusSink on_status;
    CancellationToken cancellation;
};

// ---------------------------------------------------------------------------
// Capability interfaces
// ---------------------------------------------------------------------------
//
// Discovered, not required: a provider inherits one only if it has that
// capability, and the HARNESS does the discovery. Callers ask the Harness a
// plain typed question (`can_embed(model)`), so no `dynamic_cast` appears at a
// call site -- that is an acceptance criterion of this item, and it is what
// keeps a capability check from turning into a type-switch over backends.

/// Implemented by a provider that can turn text into vectors.
///
/// Ommi gated embedding behind a hardcoded allowlist of backend types, on the
/// reasoning that its only cloud vendor could not embed. That rationale does
/// not transfer: OpenAI and Google both embed over their APIs. So this is a
/// per-provider capability, not a type test.
class EmbeddingCapable {
public:
    EmbeddingCapable() = default;
    virtual ~EmbeddingCapable() = default;
    EmbeddingCapable(const EmbeddingCapable&) = delete;
    EmbeddingCapable& operator=(const EmbeddingCapable&) = delete;
    EmbeddingCapable(EmbeddingCapable&&) = delete;
    EmbeddingCapable& operator=(EmbeddingCapable&&) = delete;

    /// Embeds each input, returning one vector per input, in order.
    [[nodiscard]] virtual std::vector<std::vector<float>> embed(
        const std::vector<std::string>& inputs, const CancellationToken& cancellation) = 0;

    /// Dimensionality of the vectors this provider produces, or 0 if it only
    /// becomes known after the first call.
    [[nodiscard]] virtual std::size_t embedding_dimensions() const noexcept = 0;

    /// The model these vectors come from, as a store should record it.
    ///
    /// Vectors from two models are two vector spaces; a collection records
    /// which one it was built in so a query is never scored across spaces.
    [[nodiscard]] virtual std::string embedding_model_name() const = 0;

    /// Whether each call costs money.
    ///
    /// A fact the provider states about itself, never a list of types: the
    /// spend policy (a whole corpus is never vectorised through a metered
    /// embedder without being asked) reads this. **Unknown is metered** -- the
    /// default answers true, so a new embedder is assumed to cost until it
    /// says otherwise.
    [[nodiscard]] virtual bool embedding_is_metered() const noexcept {
        return true;
    }
};

/// Implemented by a provider whose model loads lazily and can report progress.
class StatusReporting {
public:
    StatusReporting() = default;
    virtual ~StatusReporting() = default;
    StatusReporting(const StatusReporting&) = delete;
    StatusReporting& operator=(const StatusReporting&) = delete;
    StatusReporting(StatusReporting&&) = delete;
    StatusReporting& operator=(StatusReporting&&) = delete;

    [[nodiscard]] virtual StatusEvent model_status() const = 0;

    /// Loads now rather than on the first request, reporting progress through
    /// `on_status`. The default does nothing: a provider whose status is
    /// always "ready" has nothing to load. `apogee serve --preload` calls this
    /// through `Harness::preload_model`, so the first remote client does not
    /// pay the load. Throws ProviderError when the load fails.
    virtual void preload(const StatusSink& on_status) {
        (void)on_status;
    }
};

/// Implemented by a provider that embeds tool calls inside the text stream
/// rather than in a structured field (llama.cpp system-prompt injection).
///
/// The agent loop uses this to choose between streaming with a lookahead
/// buffer and waiting for a complete structured response.
class InTextToolCalling {
public:
    InTextToolCalling() = default;
    virtual ~InTextToolCalling() = default;
    InTextToolCalling(const InTextToolCalling&) = delete;
    InTextToolCalling& operator=(const InTextToolCalling&) = delete;
    InTextToolCalling(InTextToolCalling&&) = delete;
    InTextToolCalling& operator=(InTextToolCalling&&) = delete;

    [[nodiscard]] virtual bool uses_in_text_tool_calls() const noexcept = 0;
};

/// Implemented by a provider that can count tokens exactly, with its model's
/// own tokenizer.
///
/// The estimate every caller falls back to is characters/4 -- honest, and wrong
/// by a model-dependent margin. That margin is the whole problem for context
/// monitoring: a warning that fires at the wrong point is worse than no warning,
/// because the user learns to ignore it. A provider that owns a real tokenizer
/// (a local model does; a cloud vendor may expose a counting endpoint) says so
/// here, and `agentloop::TokenCount::estimated` stops being a hardcoded true.
///
/// **Counting must be cheap enough for the hot path.** A local tokenizer is;
/// an HTTP round-trip per keystroke-adjacent measurement is not, which is why
/// this is a capability a provider opts into rather than a method on
/// LLMProvider that every backend would have to answer somehow.
class TokenCounting {
public:
    TokenCounting() = default;
    virtual ~TokenCounting() = default;
    TokenCounting(const TokenCounting&) = delete;
    TokenCounting& operator=(const TokenCounting&) = delete;
    TokenCounting(TokenCounting&&) = delete;
    TokenCounting& operator=(TokenCounting&&) = delete;

    /// Exact prompt-token count for `request`, or a negative value if this
    /// provider cannot count it after all (an unloaded model, say). A caller
    /// that gets a negative value falls back to the estimate.
    [[nodiscard]] virtual std::int64_t count_prompt_tokens(const ChatRequest& request) = 0;
};

/// Implemented by a provider that can accept image content parts.
///
/// Exists so no surface has to ask "what type is this backend?" before
/// attaching an image. `commands/complete.cpp` did exactly that as a recorded
/// stopgap while llamacpp had no implementation to ask; a type switch is the
/// shape the capability rule exists to prevent, and this retires it.
///
/// A provider that does not inherit this is assumed to accept images: the
/// cloud vendors all do, and pre-refusing on an unknown backend would be the
/// expensive direction of a wrong guess.
class VisionCapable {
public:
    VisionCapable() = default;
    virtual ~VisionCapable() = default;
    VisionCapable(const VisionCapable&) = delete;
    VisionCapable& operator=(const VisionCapable&) = delete;
    VisionCapable(VisionCapable&&) = delete;
    VisionCapable& operator=(VisionCapable&&) = delete;

    /// Whether this provider accepts image parts right now. A local backend
    /// answers false until an mmproj model is configured for it.
    [[nodiscard]] virtual bool accepts_images() const noexcept = 0;
};

/// Implemented by a provider that knows its model family's quirks.
/// A provider that does not leaves callers with the permissive zero value.
class ModelBehaviorReporting {
public:
    ModelBehaviorReporting() = default;
    virtual ~ModelBehaviorReporting() = default;
    ModelBehaviorReporting(const ModelBehaviorReporting&) = delete;
    ModelBehaviorReporting& operator=(const ModelBehaviorReporting&) = delete;
    ModelBehaviorReporting(ModelBehaviorReporting&&) = delete;
    ModelBehaviorReporting& operator=(ModelBehaviorReporting&&) = delete;

    [[nodiscard]] virtual ModelBehavior model_behavior() const = 0;
};

// ---------------------------------------------------------------------------
// The provider interface
// ---------------------------------------------------------------------------

/// Four methods, down from Ommi's five: its `StreamTokens` one-shot streaming
/// role is served by `stream_chat` with a single user message, which is a
/// deliberate simplification -- two streaming paths meant two places for a
/// cancellation or framing bug to hide.
class LLMProvider {
public:
    LLMProvider() = default;
    virtual ~LLMProvider() = default;
    LLMProvider(const LLMProvider&) = delete;
    LLMProvider& operator=(const LLMProvider&) = delete;
    LLMProvider(LLMProvider&&) = delete;
    LLMProvider& operator=(LLMProvider&&) = delete;

    /// The config entry this provider was built from.
    [[nodiscard]] virtual std::string_view backend_name() const noexcept = 0;

    /// A multi-turn request, answered in full.
    [[nodiscard]] virtual ChatResponse chat(const ChatRequest& request,
                                            const CancellationToken& cancellation) = 0;

    /// A multi-turn request, streamed. The complete response is returned as
    /// well, so a caller that both renders live and persists history does not
    /// have to reassemble it from chunks -- the reassembly being exactly where
    /// whitespace and tool-call fragments get lost.
    ///
    /// Must throw CancelledError promptly once the token is cancelled.
    [[nodiscard]] virtual ChatResponse stream_chat(const ChatRequest& request,
                                                   const StreamOptions& options) = 0;

    /// A single-turn completion. Convenience over chat with one user message,
    /// kept because it is the shape `apogee complete` wants.
    [[nodiscard]] virtual ChatResponse complete(const ChatRequest& request,
                                                const CancellationToken& cancellation);

    /// Models this provider can serve.
    [[nodiscard]] virtual std::vector<ModelInfo> list_models(
        const CancellationToken& cancellation) = 0;
};

}  // namespace apogee::harness
