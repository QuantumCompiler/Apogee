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
