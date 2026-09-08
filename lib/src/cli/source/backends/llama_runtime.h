#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

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

    /// Drops every cached position at or after `position`, so the next decode
    /// re-establishes from there. `position == 0` clears the cache entirely.
    virtual void trim_to(std::int64_t position) = 0;

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

    /// Training context length, or 0 when unknown.
    [[nodiscard]] virtual std::int64_t context_length() const noexcept = 0;

    /// A fresh context over this model, with its own empty KV cache.
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

    /// Loads the GGUF at `path`.
    ///
    /// Returns nullptr and fills `error` on failure rather than throwing: a
    /// missing or corrupt model file is an ordinary user mistake with an
    /// obvious fix, and the acceptance criterion for it is a clear message
    /// naming the file -- never a crash.
    /// `mmproj_path` empty loads a text-only model, which is the common case.
    /// A projector that fails to load is an error rather than a downgrade to
    /// text: the user asked for vision, and silently answering without looking
    /// at their picture is worse than saying why.
    [[nodiscard]] virtual std::unique_ptr<LlamaModel> load(const std::string& path,
                                                           std::int64_t gpu_layers,
                                                           const std::string& mmproj_path,
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
