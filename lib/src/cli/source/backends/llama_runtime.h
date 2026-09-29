#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "harness/config.h"
#include "harness/types.h"

/// The slice of llama.cpp the local backend needs, behind an interface.
///
/// **Why an interface at all**, when llama.cpp is already a C API we could call
/// directly: the merge-blocking build does not have llama.cpp in it. Its
/// kernels are expensive to compile, so it sits behind `APOGEE_ENABLE_LLAMA`
/// and gets its own non-blocking CI job. A provider that called `llama_*`
/// directly would therefore be untestable on the only target that gates merges
/// -- which is the same as untested.
///
/// So the seam is the same one every other backend already uses (`HttpTransport`
/// for the cloud providers, `LineReader` for the REPL): the provider owns
/// behaviour, the runtime owns the vendor call, and tests drive a scripted
/// implementation. The KV-reuse and side-request-isolation guarantees this item
/// exists to make are *counting* assertions -- how many tokens got decoded, on
/// which context -- and a fake counts them exactly, deterministically, with no
/// model file and no GPU.
namespace apogee::backends {

/// A single decode position range, recorded per call. Tests assert on these;
/// the real runtime uses them to drive `llama_decode`.
struct DecodeRecord {
    std::int64_t position = 0;  ///< n_past at the start of this decode
    std::int64_t count = 0;     ///< how many tokens were decoded
};

/// The grammar one generation obeys. An empty `gbnf` constrains nothing.
struct SamplingGrammar {
    std::string gbnf;
    /// Applied only once a trigger fires -- a tool call's opener -- so the
    /// model's prose stays free and only the call is held to the format.
    bool lazy = false;
    std::vector<std::string> trigger_patterns;
    std::vector<std::int32_t> trigger_tokens;
};

/// A reply, read back through the model's own template format.
struct ParsedReply {
    std::string content;
    /// Shown live, never kept: the IR has no field for it.
    std::string reasoning;
    std::vector<harness::ToolCall> tool_calls;
};

/// Reads the replies to one rendered request.
class ReplyReader {
public:
    ReplyReader() = default;
    virtual ~ReplyReader() = default;
    ReplyReader(const ReplyReader&) = delete;
    ReplyReader& operator=(const ReplyReader&) = delete;
    ReplyReader(ReplyReader&&) = delete;
    ReplyReader& operator=(ReplyReader&&) = delete;

    /// Reads `text`, everything generated so far. `partial` while it is still
    /// streaming: an unfinished call is held back rather than refused. False
    /// with `error` when a finished reply does not match the format.
    [[nodiscard]] virtual bool read(std::string_view text, bool partial, ParsedReply& out,
                                    std::string& error) const = 0;
};

/// A request rendered through the model's own chat template, tools and all.
struct ChatRendering {
    std::string prompt;
    SamplingGrammar grammar;
    /// Special tokens the reader must see as text (`<tool_call>` on Qwen). A
    /// special token is otherwise rendered as nothing, and the call it opens
    /// would reach the reader as bare JSON.
    std::vector<std::int32_t> preserved_tokens;
    /// Strings that end generation, besides end-of-generation itself.
    std::vector<std::string> stops;
    /// The template format's name, for a diagnostic.
    std::string format;
    std::unique_ptr<ReplyReader> reader;
};

/// The most checkpoints one context keeps; the oldest goes first (25c,
/// default taken). A chat needs the one before its latest answer; llama-server
/// keeps 32 because a server shares a context across many slots.
inline constexpr std::size_t kMaxCheckpoints = 8;

/// The checkpoint policy every runtime shares (25c), over any `Checkpoint`
/// with a `position`: the state after exactly that many tokens. One place, so
/// the real runtime and the scripted one cannot keep different rules.
///
/// Adding: one at the same position is replaced, and past kMaxCheckpoints
/// the oldest goes. Positions only grow along the list -- a trim drops every
/// checkpoint past where the cache ends, and new ones are taken beyond it.
template <typename Checkpoint>
void keep_checkpoint(std::vector<Checkpoint>& held, Checkpoint added) {
    std::erase_if(held, [&added](const Checkpoint& old) { return old.position == added.position; });
    while (held.size() >= kMaxCheckpoints) {
        held.erase(held.begin());
    }
    held.push_back(std::move(added));
}

/// The checkpoint a trim to `position` restores: the newest at or before it,
/// never one past it -- that one holds a state the new prompt does not share
/// -- or `held.end()` when there is none.
template <typename Checkpoint>
[[nodiscard]] typename std::vector<Checkpoint>::iterator checkpoint_for(
    std::vector<Checkpoint>& held, std::int64_t position) {
    for (auto it = held.end(); it != held.begin();) {
        --it;
        if (it->position <= position && it->position > 0) {
            return it;
        }
    }
    return held.end();
}

/// Drops every checkpoint past `position`: they describe a cache that no
/// longer exists.
template <typename Checkpoint>
void forget_checkpoints_after(std::vector<Checkpoint>& held, std::int64_t position) {
    std::erase_if(held, [position](const Checkpoint& old) { return old.position > position; });
}

/// Whether a sliding-window cache still holds the window a token decoded at
/// `position` looks back over (26m).
///
/// A window-sized cache keeps only its last window and a batch. Cut back to
/// `position`, it may no longer hold the positions just before that, and
/// decoding on regardless answers from a context with a hole in it -- fluently,
/// wrongly, and with no error. `oldest` is the smallest position the cache
/// still holds (-1 when it holds none); `window` is the model's, 0 for a model
/// with none. The test is llama-server's (`pos_min_thold`), which errs safe:
/// intact when the oldest position is 0 or lies before `position` minus the
/// window.
[[nodiscard]] inline bool window_intact(std::int64_t oldest, std::int64_t position,
                                        std::int64_t window) noexcept {
    if (window <= 0 || position <= 0) {
        return true;
    }
    if (oldest < 0) {
        return false;
    }
    return oldest == 0 || oldest < position - window;
}

/// One KV cache -- llama.cpp's `llama_context`.
///
/// A context IS the conversation's warm state. Keeping one alive across turns
/// is the whole architectural payoff over Ommi, which re-ingested through an
/// on-disk prompt cache and paid ~1s of weight re-mapping per spawned turn.
class LlamaContext {
public:
    LlamaContext() = default;
    virtual ~LlamaContext() = default;
    LlamaContext(const LlamaContext&) = delete;
    LlamaContext& operator=(const LlamaContext&) = delete;
    LlamaContext(LlamaContext&&) = delete;
    LlamaContext& operator=(LlamaContext&&) = delete;

    /// Decodes `tokens` starting at `position`, extending the KV cache.
    /// Throws on a decode failure.
    virtual void decode(const std::vector<std::int32_t>& tokens, std::int64_t position) = 0;

    /// Samples the next token given the current state.
    [[nodiscard]] virtual std::int32_t sample() = 0;

    /// The grammar every sample obeys from now on; an empty one removes it.
    /// Set before each generation, since a grammar holds state across the
    /// tokens of one reply. False with `error` when it does not compile.
    ///
    /// The default accepts only "none": a runtime that cannot constrain must
    /// say so rather than sample freely under a grammar it ignored.
    [[nodiscard]] virtual bool set_grammar(const SamplingGrammar& grammar, std::string& error) {
        if (grammar.gbnf.empty()) {
            return true;
        }
        error = "this context cannot apply a grammar";
        return false;
    }

    /// Drops every cached position at or after `position`, so the next decode
    /// re-establishes from there. `position == 0` clears the cache entirely.
    ///
    /// Returns where the cache now ends, which the caller decodes on from. A
    /// recurrent or hybrid model (Qwen3.5's linear-attention layers, Mamba,
    /// RWKV) keeps a running state rather than one entry per token, and
    /// llama.cpp can rewind it only a few tokens; asked for more it refuses.
    /// Then the newest `checkpoint` at or before `position` is restored and
    /// its position returned; with none, the cache is cleared and 0 returned
    /// -- slower, where decoding on from `position` would be wrong.
    /// A sliding-window model's cache (Gemma, gpt-oss) keeps only its last
    /// window, so a cut that leaves the window short (`window_intact`) is
    /// refused the same way (26m).
    [[nodiscard]] virtual std::int64_t trim_to(std::int64_t position) = 0;

    /// Saves the part of the cache `trim_to` cannot rewind, as it stands --
    /// the state after decoding exactly positions `[0, position)` -- so a
    /// later prompt that diverges at or after `position` decodes on from here
    /// instead of from 0. llama-server's context checkpoints (25c).
    ///
    /// A no-op answering false on a context whose memory rewinds anyway: a
    /// pure-attention model never pays for one. At most kMaxCheckpoints are
    /// kept, the oldest evicted; one at the same position is replaced.
    [[nodiscard]] virtual bool checkpoint(std::int64_t position) {
        (void)position;
        return false;
    }

    /// Whether `checkpoint` does anything here -- the memory cannot be
    /// rewound, or keeps only a sliding window (26m). A caller skips the work
    /// of finding where to take them when it would be thrown away.
    [[nodiscard]] virtual bool needs_checkpoints() const noexcept {
        return false;
    }

    /// How many checkpoints are held, and the host memory they take.
    [[nodiscard]] virtual std::size_t checkpoint_count() const noexcept {
        return 0;
    }

    [[nodiscard]] virtual std::size_t checkpoint_bytes() const noexcept {
        return 0;
    }

    /// Total tokens this context has ever decoded.
    ///
    /// The KV-reuse acceptance criterion is stated against exactly this number:
    /// if turn two decodes the whole conversation again, the cache did nothing.
    [[nodiscard]] virtual std::int64_t eval_count() const noexcept = 0;

    /// Total positions this context can hold, prompt plus generation.
    ///
    /// Generation must STOP at this line rather than walk into it: llama.cpp
    /// answers an over-full cache by failing the decode, and an exception in
    /// the middle of a turn is a much worse outcome than a truncated answer --
    /// especially in-process, where there is no child to lose instead.
    [[nodiscard]] virtual std::int64_t capacity() const noexcept = 0;

    /// The cache this context keeps its keys and values in: the model's
    /// (26a), or `f16` where a quantized one could not be made.
    [[nodiscard]] virtual harness::KvCacheType cache_type() const noexcept {
        return harness::KvCacheType::F16;
    }

    /// Decodes an interleaved text-and-image prompt, extending the KV cache
    /// from `position`. Returns the new position, or -1 with `error` filled.
    ///
    /// **Separate from `decode` because a multimodal prompt is not a token
    /// vector.** llama.cpp's mtmd turns text-with-markers plus decoded images
    /// into a mixture of text chunks and image-embedding chunks, and only it
    /// knows how to feed them. Squeezing that through the token-vector
    /// interface would mean either lying about the type or exposing the raw
    /// `llama_context` — and exposing it is what makes every later caller free
    /// to bypass this seam.
    ///
    /// `images` are raw encoded bytes (PNG, JPEG, …), decoded by mtmd rather
    /// than by us. `text` carries one marker per image, in order.
    ///
    /// The default refuses: a runtime without vision must say so rather than
    /// silently ignore the pictures it was handed.
    [[nodiscard]] virtual std::int64_t decode_multimodal(const std::vector<std::string>& images,
                                                         std::string_view text,
                                                         std::int64_t position,
                                                         std::string& error) {
        (void)images;
        (void)text;
        (void)position;
        error = "this context has no multimodal projector loaded";
        return -1;
    }

    /// The most tokens one `decode` call may carry.
    ///
    /// Not a detail the caller can ignore: llama.cpp rejects an over-long batch
    /// outright, so a prompt bigger than this must be fed in pieces. Submitting
    /// a whole prompt in one call works right up until someone pastes a long
    /// file, and then fails with a message about KV slots that points nowhere
    /// near the cause.
    [[nodiscard]] virtual std::int64_t max_batch_tokens() const noexcept = 0;
};

/// A loaded model -- llama.cpp's `llama_model`. Contexts are made from it, and
/// several contexts can share one model without reloading the weights, which is
/// what makes an isolated side-request context cheap.
class LlamaModel {
public:
    LlamaModel() = default;
    virtual ~LlamaModel() = default;
    LlamaModel(const LlamaModel&) = delete;
    LlamaModel& operator=(const LlamaModel&) = delete;
    LlamaModel(LlamaModel&&) = delete;
    LlamaModel& operator=(LlamaModel&&) = delete;

    [[nodiscard]] virtual std::vector<std::int32_t> tokenize(std::string_view text,
                                                             bool add_special) const = 0;

    /// Renders one token as text. Returns empty for a control token that has
    /// no printable form.
    [[nodiscard]] virtual std::string token_text(std::int32_t token) const = 0;

    /// Renders one token as text, a special token included -- for the ones a
    /// rendering preserves.
    [[nodiscard]] virtual std::string special_token_text(std::int32_t token) const {
        return token_text(token);
    }

    /// Whether `token` ends generation.
    [[nodiscard]] virtual bool is_eog(std::int32_t token) const noexcept = 0;

    /// Renders `messages` with the template baked into the GGUF.
    ///
    /// Empty when the model ships none, which is the caller's signal to fall
    /// back to the name-matched registry. This is a method rather than a
    /// "give me the template string" accessor because applying it is a
    /// llama.cpp call (`llama_chat_apply_template`) -- handing the raw Jinja
    /// text across the seam would oblige us to implement a Jinja engine to use
    /// it, which is exactly the work linking llama.cpp avoids.
    ///
    /// **A model's own template always wins over our registry.** It is the
    /// model's statement about itself; our registry is a guess from its name,
    /// and a wrong guess produces fluent nonsense rather than an error.
    [[nodiscard]] virtual std::string apply_builtin_template(
        const std::vector<harness::ChatMessage>& messages, bool add_generation_prompt) const = 0;

    /// Renders `messages` and `tools` through the model's own chat template,
    /// with llama.cpp's chat layer (`common/chat.h`, the one llama-server
    /// runs): the prompt, the grammar a tool call must follow, and a reader
    /// for the reply. `enable_thinking` is the template's own switch.
    ///
    /// False with `error` when it cannot -- the GGUF ships no template, or
    /// its template cannot render this request. The caller then renders
    /// through `apply_builtin_template` and the registry, as before, and says
    /// so when the request carried tools. The default cannot.
    /// `add_generation_prompt` off renders the messages alone -- a prefix of
    /// the full prompt, whose length is where its next message starts.
    [[nodiscard]] virtual bool render_chat(const std::vector<harness::ChatMessage>& messages,
                                           const std::vector<harness::Tool>& tools,
                                           bool enable_thinking, bool add_generation_prompt,
                                           ChatRendering& out, std::string& error) const {
        (void)messages;
        (void)tools;
        (void)enable_thinking;
        (void)add_generation_prompt;
        (void)out;
        error = "this runtime has no chat-template layer";
        return false;
    }

    /// Training context length, or 0 when unknown.
    [[nodiscard]] virtual std::int64_t context_length() const noexcept = 0;

    /// The most positions free memory held when this model was loaded, where
    /// the load was asked to find out (`ModelLoad::fit_window`) and the
    /// default window would not fit; 0 when it fits, or nobody asked (26a).
    [[nodiscard]] virtual std::int64_t fitted_window() const noexcept {
        return 0;
    }

    /// A fresh context of `context_size` positions over this model, with its
    /// own empty KV cache, kept in the model's cache type.
    [[nodiscard]] virtual std::unique_ptr<LlamaContext> make_context(std::int64_t context_size) = 0;

    /// Whether a multimodal projector was loaded alongside this model AND it
    /// actually supports images.
    ///
    /// Both halves matter: a projector file can load and still be audio-only,
    /// and answering yes on the strength of "an mmproj was configured" is how a
    /// surface ends up accepting a picture it cannot use.
    [[nodiscard]] virtual bool supports_vision() const noexcept {
        return false;
    }

    /// The literal a prompt uses to stand for an image, e.g. `<__media__>`.
    /// Empty when this model has no projector.
    [[nodiscard]] virtual std::string image_marker() const {
        return {};
    }

    /// Width of the vectors `embed_batch` produces -- the model's hidden size.
    [[nodiscard]] virtual std::size_t embedding_dimensions() const noexcept = 0;

    /// Embeds `texts`, one L2-normalised vector each, in order.
    ///
    /// **A separate context from the conversation's.** Embedding runs with
    /// pooling on and every token's output kept, which is a different context
    /// configuration from generation -- and decoding a document into the chat
    /// KV cache would corrupt the warm state the whole in-process design
    /// exists to keep. The real runtime holds one embedding context per model
    /// and clears it between batches; nothing here touches `make_context`'s.
    ///
    /// A text longer than the context's batch is **truncated to fit**, the
    /// same choice llama.cpp's own embedding tool makes: the chunker bounds
    /// chunk sizes far below that line, so this is the defence against a
    /// pathological input rather than a policy. Returns an empty vector and
    /// fills `error` when the model cannot embed at all.
    [[nodiscard]] virtual std::vector<std::vector<float>> embed_batch(
        const std::vector<std::string>& texts, std::string& error) = 0;
};

/// What a model is loaded from and with.
struct ModelLoad {
    /// The GGUF.
    std::string path;
    /// Layers to offload to the GPU; 0 forces CPU.
    std::int64_t gpu_layers = 999;
    /// The multimodal projector; empty loads a text-only model, which is the
    /// common case.
    std::string mmproj_path;
    /// The cache every generation context of this model keeps (26a).
    harness::KvCacheType cache_type = harness::KvCacheType::Q8_0;
    /// Whether the backend's config named the cache type. A named type is used
    /// as written or refused; the default gives way to `f16` on a model that
    /// cannot take it.
    bool cache_type_named = false;
    /// Whether to find what free memory holds, for a backend whose window is
    /// the default: `LlamaModel::fitted_window` reports it.
    bool fit_window = false;
};

/// Loads models. One per process in practice.
class LlamaRuntime {
public:
    LlamaRuntime() = default;
    virtual ~LlamaRuntime() = default;
    LlamaRuntime(const LlamaRuntime&) = delete;
    LlamaRuntime& operator=(const LlamaRuntime&) = delete;
    LlamaRuntime(LlamaRuntime&&) = delete;
    LlamaRuntime& operator=(LlamaRuntime&&) = delete;

    /// Loads `request.path`.
    ///
    /// Returns nullptr and fills `error` on failure rather than throwing: a
    /// missing or corrupt model file is an ordinary user mistake with an
    /// obvious fix, and the acceptance criterion for it is a clear message
    /// naming the file -- never a crash.
    /// A projector that fails to load is an error rather than a downgrade to
    /// text: the user asked for vision, and silently answering without looking
    /// at their picture is worse than saying why.
    [[nodiscard]] virtual std::unique_ptr<LlamaModel> load(const ModelLoad& request,
                                                           std::string& error) = 0;
};

/// The runtime this build has, or nullptr when llama.cpp was not compiled in.
///
/// `reason` is filled on nullptr with a message a user can act on. Building
/// without llama.cpp is the DEFAULT, not an error state -- most users of a
/// cloud backend should not pay for Metal kernel compilation -- so the message
/// says how to turn it on rather than reporting a fault.
[[nodiscard]] std::unique_ptr<LlamaRuntime> make_llama_runtime(std::string& reason);

/// Whether this build has llama.cpp compiled in.
[[nodiscard]] bool llama_available() noexcept;

}  // namespace apogee::backends
