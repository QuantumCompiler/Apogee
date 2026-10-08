#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "contracts/behavior.h"
#include "contracts/cancellation.h"
#include "contracts/types.h"

/// The interface every model backend implements.
///
/// Streaming, cancellation, multimodal content, and tool structures are in the
/// interface from day one, because that is what keeps several backends and
/// surfaces consistent. Adding any of them later means touching every
/// implementation and every caller at once.
///
/// Adding a backend is: implement LLMProvider, add a `type:` row to the config
/// enum, register it. Nothing above the backends layer changes.
namespace apogee::harness {

/// Receives streamed text as it arrives.
///
/// Callbacks rather than coroutine generators (decided 2026-08-25): the shape
/// is simple to implement in every backend, and it does not force the whole
/// call stack to become coroutines. A provider calls this from whatever thread
/// it reads on, so a sink that touches shared state must do its own locking.
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
/// A per-provider capability, not a hardcoded allowlist of backend types:
/// cloud vendors embed too -- OpenAI and Google both do over their APIs -- so
/// whether a backend embeds is a question for the provider, not a type test.
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

    /// Hears every load this provider makes from now on -- its `ModelLoading`
    /// start, its `ModelReady` or its error -- whichever request caused it: an
    /// embedding, a helper's side call, a preload, a turn (27e). A load is
    /// otherwise said only to a request that streams status, and most of a
    /// helper's do not, so a first use paid its load in silence. Set before
    /// the provider is first used; the default hears nothing, for a provider
    /// with nothing to load.
    virtual void set_load_listener(const StatusSink& listener) {
        (void)listener;
    }
};

/// Implemented by a provider that gives its model back after an idle spell
/// -- `idle_unload_seconds`, a local model -- and that a session can hold
/// against that clock (27e).
///
/// The idle clock is per backend and stays so: a hold only says "a session is
/// using this", so a model the session has used is not unloaded between two
/// of its turns, however far apart -- an embedder idling out mid-conversation
/// pays its whole load back on the next question. Let go, the clock rules
/// again from the model's last use. A hold is this process's memory and dies
/// with it: nothing is persisted, and nothing runs in the background.
class ResidencyHolding {
public:
    ResidencyHolding() = default;
    virtual ~ResidencyHolding() = default;
    ResidencyHolding(const ResidencyHolding&) = delete;
    ResidencyHolding& operator=(const ResidencyHolding&) = delete;
    ResidencyHolding(ResidencyHolding&&) = delete;
    ResidencyHolding& operator=(ResidencyHolding&&) = delete;

    /// Holds the model against its idle clock, or lets it go. Safe from any
    /// thread; a request already deciding sees one value or the other.
    virtual void hold_resident(bool held) noexcept = 0;

    /// Whether it is held now.
    [[nodiscard]] virtual bool held_resident() const noexcept = 0;
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

/// Implemented by a provider that can take audio (26b): a local model whose
/// projector has an audio encoder, read through mtmd.
///
/// Unlike images, a provider that does not declare it is taken to have none:
/// no cloud backend here is sent audio, so the only way to transcribe is a
/// model that says it can, and a `transcription` role pointed anywhere else
/// is a mistake worth naming.
class AudioCapable {
public:
    AudioCapable() = default;
    virtual ~AudioCapable() = default;
    AudioCapable(const AudioCapable&) = delete;
    AudioCapable& operator=(const AudioCapable&) = delete;
    AudioCapable(AudioCapable&&) = delete;
    AudioCapable& operator=(AudioCapable&&) = delete;

    /// Whether this provider accepts audio right now.
    [[nodiscard]] virtual bool accepts_audio() const noexcept = 0;

    /// The rate its model hears audio at, in Hz, or 0 while it cannot say --
    /// a local model's is its projector's, known once that is loaded (26e).
    /// Audio is decoded to this rate before it is sent; at 0 it is decoded
    /// to 16 kHz, which the backend converts to its own.
    [[nodiscard]] virtual int audio_sample_rate() const noexcept {
        return 0;
    }
};

/// Implemented by a provider that reads a video clip as its frames (26e): a
/// local model with a vision projector, in a build whose mtmd has video, whose
/// consecutive frames it may merge.
///
/// A provider that does not declare it is taken to have none, as with audio:
/// no cloud backend here is sent frames as a clip, and a clip on a model that
/// cannot take one becomes a timeline of described frames instead.
class VideoCapable {
public:
    VideoCapable() = default;
    virtual ~VideoCapable() = default;
    VideoCapable(const VideoCapable&) = delete;
    VideoCapable& operator=(const VideoCapable&) = delete;
    VideoCapable(VideoCapable&&) = delete;
    VideoCapable& operator=(VideoCapable&&) = delete;

    /// Whether this provider accepts a clip's frames right now.
    [[nodiscard]] virtual bool accepts_video() const noexcept = 0;
};

/// Implemented by a provider that sizes its own conversation window.
///
/// A cloud window is a fact about a model name, which the fallback table in
/// `context_windows.h` knows; a local one is not -- it is the backend's
/// default fitted to the model and the machine at load (26a), which no table
/// of names could hold. Context monitoring warns and compacts against the
/// window a backend actually allocated, so the backend is asked.
class ContextWindowReporting {
public:
    ContextWindowReporting() = default;
    virtual ~ContextWindowReporting() = default;
    ContextWindowReporting(const ContextWindowReporting&) = delete;
    ContextWindowReporting& operator=(const ContextWindowReporting&) = delete;
    ContextWindowReporting(ContextWindowReporting&&) = delete;
    ContextWindowReporting& operator=(ContextWindowReporting&&) = delete;

    /// The window in tokens, or 0 when this provider cannot say.
    [[nodiscard]] virtual std::int64_t context_window() const = 0;
};

/// Implemented by a provider whose conversation can outlive the process
/// (26j): a local model's attention cache, saved to a file and restored, so
/// a resumed chat reads only what follows what it had read.
///
/// A provider without it simply reads a resumed conversation again -- every
/// cloud backend does, the vendor keeping whatever cache it keeps.
class ConversationCaching {
public:
    ConversationCaching() = default;
    virtual ~ConversationCaching() = default;
    ConversationCaching(const ConversationCaching&) = delete;
    ConversationCaching& operator=(const ConversationCaching&) = delete;
    ConversationCaching(ConversationCaching&&) = delete;
    ConversationCaching& operator=(ConversationCaching&&) = delete;

    /// Names the conversation the turns that follow belong to -- a chat's
    /// id -- so the first of them can restore the state it was saved with.
    virtual void resume_conversation(std::string_view conversation_id) = 0;

    /// Saves the conversation's state under `conversation_id`, as far as
    /// its next prompt is sure to share it, saying what it did through
    /// `on_status`. Never throws: a state not saved is read again next time.
    virtual void save_conversation(std::string_view conversation_id,
                                   const StatusSink& on_status) = 0;
};

/// Implemented by a provider that can accept image content parts.
///
/// Exists so no surface has to ask "what type is this backend?" before
/// attaching an image. `cli/complete.cpp` did exactly that as a recorded
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

/// Four methods. One-shot streaming has no method of its own: it is served by
/// `stream_chat` with a single user message, which is a deliberate
/// simplification -- two streaming paths would mean two places for a
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

    /// Whether each generation call costs money.
    ///
    /// A fact the provider states about itself, never a list of types: the
    /// graph build's cost policy (a whole collection is never extracted
    /// through a metered backend on Apogee's initiative) reads this through
    /// the Harness. **Unknown is metered** -- the default answers true, so a
    /// new backend is assumed to cost until it says otherwise. Local weights
    /// and the mock answer false.
    [[nodiscard]] virtual bool generation_is_metered() const noexcept {
        return true;
    }
};

struct Config;

/// What the provider factory fills, seen from below (A1): the config it reads
/// the backends from, the registration of each provider it builds, and the
/// default router installed over them. The Harness implements it, so
/// `backends/factory` builds providers into a Harness without including one --
/// the Data layer never reaches up for the Business object it fills. The
/// capability-interface pattern, pointed the other way.
class ProviderRegistry {
public:
    virtual ~ProviderRegistry() = default;

    [[nodiscard]] virtual const Config& config() const noexcept = 0;
    virtual void register_provider(std::string name, std::shared_ptr<LLMProvider> provider) = 0;
    virtual void use_default_router() = 0;

    /// Hears each turn a registered backend answered without throwing --
    /// a chat, a stream or a completion, the backend's name (28c: the
    /// provider cache's verified record). Plain data crossing up, so the
    /// layer that installs it is never known to the one that calls it. One
    /// observer; an empty one clears it. A registry that keeps no record
    /// ignores it.
    using TurnObserver = std::function<void(const std::string& backend)>;

    virtual void observe_turns(TurnObserver observer) {
        (void)observer;
    }

protected:
    ProviderRegistry() = default;
    ProviderRegistry(const ProviderRegistry&) = default;
    ProviderRegistry& operator=(const ProviderRegistry&) = default;
    ProviderRegistry(ProviderRegistry&&) = default;
    ProviderRegistry& operator=(ProviderRegistry&&) = default;
};

}  // namespace apogee::harness
