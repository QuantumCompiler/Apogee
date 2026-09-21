/// The real `LlamaRuntime`, over llama.cpp's C API.
///
/// Compiled only when `APOGEE_ENABLE_LLAMA=ON`; otherwise this file provides
/// the "not built in" answer and nothing here includes `llama.h`. That is why
/// the whole file is one `#if` rather than a per-function guard: the two halves
/// share no code, and a half-guarded file invites an accidental reference to a
/// llama type from the disabled build.
///
/// Every raw handle llama.cpp hands out is wrapped in a `unique_ptr` with a
/// custom deleter at this boundary and never escapes it -- the Code Style rule
/// for C APIs, and the reason a `throw` from a decode cannot leak a context.

#include <string>

#include "backends/llama_runtime.h"

#if defined(APOGEE_ENABLE_LLAMA)

#include <llama.h>
#include <mtmd-helper.h>
#include <mtmd.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace apogee::backends {
namespace {

/// llama.cpp's global backend init, done once and never torn down.
///
/// `llama_backend_free()` is deliberately not called: it would have to run
/// after every model and context is gone, and tying that to static destruction
/// order across translation units is exactly the kind of shutdown crash that is
/// impossible to reproduce. Leaking a process-lifetime registry at exit costs
/// nothing.
void ensure_backend_init() {
    static std::once_flag once;
    std::call_once(once, [] {
        // Silence llama.cpp's own logging below WARN before anything can emit.
        //
        // Loading a model prints ~20 lines of Metal/ggml device capabilities at
        // INFO. On a CLI whose contract is "the answer, and nothing else", that
        // buries our error message in kernel chatter -- the acceptance criterion
        // here asks for a CLEAR error naming the file, and three useful lines
        // preceded by twenty irrelevant ones does not qualify.
        //
        // WARN and ERROR still pass through: when a model genuinely fails,
        // llama.cpp's reason is more specific than anything we can infer, and
        // it belongs on stderr next to ours.
        llama_log_set(
            [](ggml_log_level level, const char* text, void* /*user_data*/) {
                if (level >= GGML_LOG_LEVEL_WARN && text != nullptr) {
                    std::fputs(text, stderr);
                }
            },
            nullptr);

        // mtmd logs through its OWN channels, which `llama_log_set` does not
        // reach -- and it is chattier: "encoding image slice...", "image
        // decoded (batch 1/1) in 55 ms". Found by running it: those lines
        // landed on STDOUT, interleaved with the answer. On a terminal that is
        // noise; in machine mode it is non-JSON in the middle of the event
        // stream, which breaks the driver's parser at the worst moment.
        //
        // Same rule as above, applied to both of mtmd's loggers: WARN and above
        // to stderr, everything below dropped.
        const auto quiet = [](ggml_log_level level, const char* text, void* /*user_data*/) {
            if (level >= GGML_LOG_LEVEL_WARN && text != nullptr) {
                std::fputs(text, stderr);
            }
        };
        mtmd_log_set(quiet, nullptr);
        mtmd_helper_log_set(quiet, nullptr);

        llama_backend_init();
    });
}

struct ModelDeleter {
    void operator()(llama_model* model) const noexcept {
        llama_model_free(model);
    }
};

struct ContextDeleter {
    void operator()(llama_context* context) const noexcept {
        llama_free(context);
    }
};

struct SamplerDeleter {
    void operator()(llama_sampler* sampler) const noexcept {
        llama_sampler_free(sampler);
    }
};

/// llama.cpp's multimodal projector. Owned by the model; contexts borrow it.
struct MtmdDeleter {
    void operator()(mtmd_context* context) const noexcept {
        mtmd_free(context);
    }
};

using MtmdPtr = std::unique_ptr<mtmd_context, MtmdDeleter>;

struct BitmapDeleter {
    void operator()(mtmd_bitmap* bitmap) const noexcept {
        mtmd_bitmap_free(bitmap);
    }
};

struct ChunksDeleter {
    void operator()(mtmd_input_chunks* chunks) const noexcept {
        mtmd_input_chunks_free(chunks);
    }
};

using BitmapPtr = std::unique_ptr<mtmd_bitmap, BitmapDeleter>;
using ChunksPtr = std::unique_ptr<mtmd_input_chunks, ChunksDeleter>;

class RealContext final : public LlamaContext {
public:
    RealContext(std::unique_ptr<llama_context, ContextDeleter> context,
                std::unique_ptr<llama_sampler, SamplerDeleter> sampler, mtmd_context* vision)
        : context_{std::move(context)}, sampler_{std::move(sampler)}, vision_{vision} {}

    void decode(const std::vector<std::int32_t>& tokens, std::int64_t position) override {
        if (tokens.empty()) {
            return;
        }

        // A batch is built by hand rather than with llama_batch_get_one because
        // positions matter here: a reused KV prefix means the first new token
        // is at `position`, not at zero.
        llama_batch batch = llama_batch_init(static_cast<std::int32_t>(tokens.size()), 0, 1);
        const std::unique_ptr<llama_batch, void (*)(llama_batch*)> guard{
            &batch, [](llama_batch* owned) { llama_batch_free(*owned); }};

        batch.n_tokens = static_cast<std::int32_t>(tokens.size());
        for (std::size_t index = 0; index < tokens.size(); ++index) {
            batch.token[index] = tokens[index];
            batch.pos[index] = static_cast<llama_pos>(position + static_cast<std::int64_t>(index));
            batch.n_seq_id[index] = 1;
            batch.seq_id[index][0] = 0;
            batch.logits[index] = 0;
        }
        // Only the final token's logits are needed -- that is what we sample
        // from. Asking for all of them allocates the full vocab per token.
        batch.logits[tokens.size() - 1] = 1;

        const std::int32_t status = llama_decode(context_.get(), batch);
        if (status != 0) {
            throw std::runtime_error(
                status == 1 ? "llama.cpp: no KV slot for the batch -- the prompt "
                              "exceeds this backend's context_size"
                            : "llama.cpp: decode failed (" + std::to_string(status) + ")");
        }
        evaluated_ += static_cast<std::int64_t>(tokens.size());
    }

    [[nodiscard]] std::int32_t sample() override {
        const llama_token token = llama_sampler_sample(sampler_.get(), context_.get(), -1);
        llama_sampler_accept(sampler_.get(), token);
        return token;
    }

    void trim_to(std::int64_t position) override {
        // p1 < 0 means "to infinity": drop everything from `position` on.
        llama_memory_seq_rm(llama_get_memory(context_.get()), 0, static_cast<llama_pos>(position),
                            -1);
    }

    [[nodiscard]] std::int64_t eval_count() const noexcept override {
        return evaluated_;
    }

    [[nodiscard]] std::int64_t max_batch_tokens() const noexcept override {
        return static_cast<std::int64_t>(llama_n_batch(context_.get()));
    }

    [[nodiscard]] std::int64_t capacity() const noexcept override {
        return static_cast<std::int64_t>(llama_n_ctx(context_.get()));
    }

private:
    std::int64_t decode_multimodal(const std::vector<std::string>& images, std::string_view text,
                                   std::int64_t position, std::string& error) override {
        if (vision_ == nullptr) {
            error = "this backend has no mmproj_path configured, so it cannot read images";
            return -1;
        }

        // mtmd decodes the image bytes itself -- PNG, JPEG, and the rest --
        // which is why the seam carries raw bytes rather than pixels. Doing our
        // own decoding would mean a second image library and a second set of
        // format bugs.
        std::vector<BitmapPtr> owned;
        std::vector<const mtmd_bitmap*> borrowed;
        owned.reserve(images.size());
        borrowed.reserve(images.size());
        for (const std::string& bytes : images) {
            BitmapPtr bitmap{mtmd_helper_bitmap_init_from_buf(
                vision_, reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size())};
            if (bitmap == nullptr) {
                error =
                    "an attached image could not be decoded -- it may be a format this "
                    "projector does not handle, or the file may be damaged";
                return -1;
            }
            borrowed.push_back(bitmap.get());
            owned.push_back(std::move(bitmap));
        }

        ChunksPtr chunks{mtmd_input_chunks_init()};
        if (chunks == nullptr) {
            error = "could not allocate multimodal input";
            return -1;
        }

        mtmd_input_text input{};
        const std::string prompt{text};
        input.text = prompt.c_str();
        input.add_special = true;
        input.parse_special = true;

        if (mtmd_tokenize(vision_, chunks.get(), &input, borrowed.data(), borrowed.size()) != 0) {
            // The usual cause is a marker count that does not match the number
            // of images, which is our bug rather than the user's -- so it says
            // what went wrong rather than blaming the picture.
            error = "the multimodal prompt could not be tokenized (marker/image mismatch)";
            return -1;
        }

        llama_pos new_position = 0;
        const std::int32_t status = mtmd_helper_eval_chunks(
            vision_, context_.get(), chunks.get(), static_cast<llama_pos>(position),
            /*seq_id=*/0, static_cast<std::int32_t>(llama_n_batch(context_.get())),
            /*logits_last=*/true, &new_position);
        if (status != 0) {
            error = "evaluating the image failed -- the context may be too small to hold it";
            return -1;
        }

        evaluated_ += static_cast<std::int64_t>(new_position) - position;
        return static_cast<std::int64_t>(new_position);
    }

    std::unique_ptr<llama_context, ContextDeleter> context_;
    std::unique_ptr<llama_sampler, SamplerDeleter> sampler_;
    std::int64_t evaluated_ = 0;
    /// Borrowed from the model, which outlives every context made from it.
    mtmd_context* vision_ = nullptr;
};

class RealModel final : public LlamaModel {
public:
    explicit RealModel(std::unique_ptr<llama_model, ModelDeleter> model)
        : model_{std::move(model)}, vocab_{llama_model_get_vocab(model_.get())} {}

    RealModel(std::unique_ptr<llama_model, ModelDeleter> model, MtmdPtr vision)
        : model_{std::move(model)},
          vocab_{llama_model_get_vocab(model_.get())},
          vision_{std::move(vision)} {}

    [[nodiscard]] std::vector<std::int32_t> tokenize(std::string_view text,
                                                     bool add_special) const override {
        if (text.empty()) {
            return {};
        }
        // Negative return = "the buffer was this many short". Ask once with a
        // zero-length buffer to learn the count, then fill exactly.
        const std::int32_t needed =
            -llama_tokenize(vocab_, text.data(), static_cast<std::int32_t>(text.size()), nullptr, 0,
                            add_special, true);
        if (needed <= 0) {
            return {};
        }

        std::vector<std::int32_t> tokens(static_cast<std::size_t>(needed));
        const std::int32_t written =
            llama_tokenize(vocab_, text.data(), static_cast<std::int32_t>(text.size()),
                           tokens.data(), needed, add_special, true);
        if (written < 0) {
            return {};
        }
        tokens.resize(static_cast<std::size_t>(written));
        return tokens;
    }

    [[nodiscard]] std::string token_text(std::int32_t token) const override {
        std::string piece(64, '\0');
        std::int32_t written = llama_token_to_piece(
            vocab_, token, piece.data(), static_cast<std::int32_t>(piece.size()), 0, false);
        if (written < 0) {
            piece.resize(static_cast<std::size_t>(-written));
            written = llama_token_to_piece(vocab_, token, piece.data(),
                                           static_cast<std::int32_t>(piece.size()), 0, false);
            if (written < 0) {
                return {};
            }
        }
        piece.resize(static_cast<std::size_t>(written));
        return piece;
    }

    [[nodiscard]] bool is_eog(std::int32_t token) const noexcept override {
        return llama_vocab_is_eog(vocab_, token);
    }

    [[nodiscard]] std::string apply_builtin_template(
        const std::vector<harness::ChatMessage>& messages,
        bool add_generation_prompt) const override {
        const char* tmpl = llama_model_chat_template(model_.get(), nullptr);
        if (tmpl == nullptr) {
            return {};
        }

        // The strings must outlive the call: llama_chat_message holds borrowed
        // pointers, so rendering into a temporary here would dangle.
        std::vector<std::string> roles;
        std::vector<std::string> contents;
        roles.reserve(messages.size());
        contents.reserve(messages.size());
        for (const harness::ChatMessage& message : messages) {
            roles.emplace_back(harness::to_string(message.role));
            contents.push_back(message.content.plain_text());
        }

        std::vector<llama_chat_message> chat;
        chat.reserve(messages.size());
        for (std::size_t index = 0; index < messages.size(); ++index) {
            chat.push_back({roles[index].c_str(), contents[index].c_str()});
        }

        std::size_t budget = 0;
        for (const std::string& content : contents) {
            budget += content.size();
        }
        // The header's own recommendation, floored so a short conversation
        // still has room for its framing.
        std::string out(std::max<std::size_t>(2 * budget, 1024), '\0');
        std::int32_t written =
            llama_chat_apply_template(tmpl, chat.data(), chat.size(), add_generation_prompt,
                                      out.data(), static_cast<std::int32_t>(out.size()));
        if (written > static_cast<std::int32_t>(out.size())) {
            out.resize(static_cast<std::size_t>(written));
            written =
                llama_chat_apply_template(tmpl, chat.data(), chat.size(), add_generation_prompt,
                                          out.data(), static_cast<std::int32_t>(out.size()));
        }
        if (written < 0) {
            // llama.cpp does not parse Jinja -- it recognises a fixed set of
            // templates. A model shipping something outside that set lands
            // here, and the caller falls back to the name-matched registry.
            return {};
        }
        out.resize(static_cast<std::size_t>(written));
        return out;
    }

    [[nodiscard]] std::int64_t context_length() const noexcept override {
        return llama_model_n_ctx_train(model_.get());
    }

    [[nodiscard]] std::unique_ptr<LlamaContext> make_context(std::int64_t context_size) override {
        // Unset means "whatever this model was trained for", resolved HERE
        // rather than left as llama.cpp's 0 sentinel because n_batch below
        // needs the real number. Defaulting to a fixed 4096 instead was wrong
        // in both directions: it warns on a 2048-context model and silently
        // truncates one trained for 128K.
        const std::int64_t resolved =
            context_size > 0 ? context_size : llama_model_n_ctx_train(model_.get());

        llama_context_params params = llama_context_default_params();
        params.n_ctx = static_cast<std::uint32_t>(resolved);
        // n_batch is left at llama.cpp's own default, and the provider chunks
        // its prompt to `max_batch_tokens()` instead.
        //
        // Setting `n_batch = n_ctx` was tried first and is WRONG: llama.cpp
        // reserves batch-sized headroom inside the KV cache, so making the two
        // equal leaves no slot for the first generated token. It fails as
        // `decode: failed to find a memory slot for batch of size 1` on turn
        // two -- a message that names neither the cause nor the setting. Real
        // hardware found this; the scripted runtime cannot model an allocator.

        std::unique_ptr<llama_context, ContextDeleter> context{
            llama_init_from_model(model_.get(), params)};
        if (context == nullptr) {
            throw std::runtime_error("llama.cpp: could not create a context for this model");
        }

        std::unique_ptr<llama_sampler, SamplerDeleter> sampler{
            llama_sampler_chain_init(llama_sampler_chain_default_params())};
        // Greedy: deterministic, and the temperature/top-p knobs belong with
        // the per-family sampling profiles in model-profiles-and-management.
        llama_sampler_chain_add(sampler.get(), llama_sampler_init_greedy());

        return std::make_unique<RealContext>(std::move(context), std::move(sampler), vision_.get());
    }

    [[nodiscard]] bool supports_vision() const noexcept override {
        // Both halves: a projector was loaded AND it does images. A projector
        // can load and be audio-only, and answering yes on the strength of
        // "an mmproj was configured" is how a surface accepts a picture it
        // cannot use.
        return vision_ != nullptr && mtmd_support_vision(vision_.get());
    }

    [[nodiscard]] std::string image_marker() const override {
        const char* marker = mtmd_default_marker();
        return marker == nullptr ? std::string{} : std::string{marker};
    }

    /// Sequences one embedding batch may carry -- the context's `n_seq_max`.
    static constexpr std::size_t kEmbeddingSequences = 64;

    [[nodiscard]] std::size_t embedding_dimensions() const noexcept override {
        const std::int32_t width = llama_model_n_embd(model_.get());
        return width > 0 ? static_cast<std::size_t>(width) : 0;
    }

    [[nodiscard]] std::vector<std::vector<float>> embed_batch(const std::vector<std::string>& texts,
                                                              std::string& error) override {
        if (texts.empty()) {
            return {};
        }
        if (!ensure_embedding_context(error)) {
            return {};
        }
        llama_context* ctx = embedding_context_.get();
        const auto n_batch = static_cast<std::size_t>(llama_n_batch(ctx));
        const std::size_t width = embedding_dimensions();

        // Tokenize every text first, truncating to the batch so one
        // pathological input cannot fail the whole call (the chunker keeps
        // real inputs far below this line).
        std::vector<std::vector<llama_token>> token_lists;
        token_lists.reserve(texts.size());
        for (const std::string& text : texts) {
            std::vector<std::int32_t> tokens = tokenize(text.empty() ? " " : text, true);
            if (tokens.size() > n_batch) {
                tokens.resize(n_batch);
            }
            token_lists.emplace_back(tokens.begin(), tokens.end());
        }

        std::vector<std::vector<float>> out(texts.size());

        // Pack as many sequences as fit one batch, decode, read each sequence's
        // pooled vector, clear, repeat. One sequence id per text is what lets
        // the pooling be per-text rather than over the whole batch -- bounded
        // by the sequences the context was created to hold.
        std::size_t next = 0;
        while (next < token_lists.size()) {
            const std::size_t first = next;
            std::size_t packed = 0;
            while (next < token_lists.size() && next - first < kEmbeddingSequences &&
                   packed + token_lists[next].size() <= n_batch) {
                packed += token_lists[next].size();
                ++next;
            }
            if (next == first) {
                // Cannot happen after truncation, but a loop that could spin
                // forever on a bug is worse than a clear failure.
                error = "an input could not be fitted into an embedding batch";
                return {};
            }

            llama_batch batch = llama_batch_init(static_cast<std::int32_t>(packed), 0,
                                                 static_cast<std::int32_t>(next - first));
            for (std::size_t seq = first; seq < next; ++seq) {
                const auto seq_id = static_cast<llama_seq_id>(seq - first);
                for (std::size_t pos = 0; pos < token_lists[seq].size(); ++pos) {
                    const std::int32_t at = batch.n_tokens;
                    batch.token[at] = token_lists[seq][pos];
                    batch.pos[at] = static_cast<llama_pos>(pos);
                    batch.n_seq_id[at] = 1;
                    batch.seq_id[at][0] = seq_id;
                    // Every token's output is kept: pooling reads them all.
                    batch.logits[at] = 1;
                    ++batch.n_tokens;
                }
            }

            llama_memory_clear(llama_get_memory(ctx), true);
            const std::int32_t status =
                llama_model_has_encoder(model_.get()) && !llama_model_has_decoder(model_.get())
                    ? llama_encode(ctx, batch)
                    : llama_decode(ctx, batch);
            if (status != 0) {
                llama_batch_free(batch);
                error = "llama.cpp: embedding decode failed (" + std::to_string(status) + ")";
                return {};
            }

            for (std::size_t seq = first; seq < next; ++seq) {
                const float* pooled =
                    llama_get_embeddings_seq(ctx, static_cast<llama_seq_id>(seq - first));
                if (pooled == nullptr) {
                    llama_batch_free(batch);
                    error = "llama.cpp: no pooled embedding came back for an input";
                    return {};
                }
                std::vector<float> vector(pooled, pooled + width);
                // L2-normalised, so cosine similarity downstream is a dot
                // product -- the same output llama.cpp's own embedding tool
                // produces by default.
                double norm = 0.0;
                for (const float component : vector) {
                    norm += static_cast<double>(component) * component;
                }
                if (norm > 0.0) {
                    const auto scale = static_cast<float>(1.0 / std::sqrt(norm));
                    for (float& component : vector) {
                        component *= scale;
                    }
                }
                out[seq] = std::move(vector);
            }
            llama_batch_free(batch);
        }
        return out;
    }

private:
    /// Creates the embedding context on first use.
    ///
    /// Separate from `make_context`'s on purpose: embeddings need pooling on
    /// and all outputs kept, which generation does not want, and decoding a
    /// document into the conversation's KV cache would corrupt the warm state
    /// this backend exists to keep. Sized to its batch, because a pooled
    /// sequence never needs more positions than one batch carries.
    [[nodiscard]] bool ensure_embedding_context(std::string& error) {
        if (embedding_context_ != nullptr) {
            return true;
        }
        llama_context_params params = llama_context_default_params();
        params.embeddings = true;
        params.pooling_type = LLAMA_POOLING_TYPE_MEAN;
        // Batch and micro-batch equal: pooling over a sequence needs the whole
        // sequence in one micro-batch.
        params.n_batch = 2048;
        params.n_ubatch = 2048;
        params.n_ctx = 2048;
        // One sequence per text in a batch, so the cache must be told to hold
        // that many. llama.cpp's default is ONE, and a batch using sequence id
        // 1 against it is an assertion failure, not an error return -- found
        // live on the first real run, after every scripted test had passed.
        params.n_seq_max = static_cast<std::uint32_t>(kEmbeddingSequences);
        // ONE shared cache for all sequences, distinguished by sequence id.
        // Without this llama.cpp gives each sequence its own stream and splits
        // a mixed-length batch into EQUAL-LENGTH micro-batches -- and pooling
        // happens per micro-batch, so a sequence longer than its batch-mates
        // is pooled over a fragment of itself. Found live: a text's vector
        // moved to cosine 0.56 of itself when a shorter text shared its batch,
        // and was exact whenever every batch-mate was at least as long.
        params.kv_unified = true;
        embedding_context_.reset(llama_init_from_model(model_.get(), params));
        if (embedding_context_ == nullptr) {
            error = "llama.cpp: could not create an embedding context for this model";
            return false;
        }
        return true;
    }

    std::unique_ptr<llama_model, ModelDeleter> model_;
    const llama_vocab* vocab_ = nullptr;
    /// The projector, when one was configured. Outlives every context made
    /// from this model, which is why contexts may borrow it raw.
    MtmdPtr vision_;
    /// Lazily created; see ensure_embedding_context.
    std::unique_ptr<llama_context, ContextDeleter> embedding_context_;
};

class RealRuntime final : public LlamaRuntime {
public:
    [[nodiscard]] std::unique_ptr<LlamaModel> load(const std::string& path, std::int64_t gpu_layers,
                                                   const std::string& mmproj_path,
                                                   std::string& error) override {
        ensure_backend_init();

        llama_model_params params = llama_model_default_params();
        params.n_gpu_layers = static_cast<std::int32_t>(gpu_layers);

        std::unique_ptr<llama_model, ModelDeleter> model{
            llama_model_load_from_file(path.c_str(), params)};
        if (model == nullptr) {
            // Naming the file is the acceptance criterion: "no such model" with
            // no path sends the user to check their config for the wrong key.
            error = "could not load the model at '" + path +
                    "' -- check that the file exists and is a valid GGUF";
            return nullptr;
        }
        if (mmproj_path.empty()) {
            return std::make_unique<RealModel>(std::move(model));
        }

        mtmd_context_params vision_params = mtmd_context_params_default();
        vision_params.use_gpu = gpu_layers > 0;
        // mtmd is chatty on stderr at info level, and this runs inside a turn.
        vision_params.print_timings = false;

        MtmdPtr vision{mtmd_init_from_file(mmproj_path.c_str(), model.get(), vision_params)};
        if (vision == nullptr) {
            // A projector that will not load is an ERROR, not a downgrade to
            // text: the user configured vision, and quietly answering without
            // looking at their picture is worse than saying why.
            error = "could not load the multimodal projector at '" + mmproj_path +
                    "' -- check that it is the mmproj file matching this model";
            return nullptr;
        }
        return std::make_unique<RealModel>(std::move(model), std::move(vision));
    }
};

}  // namespace

std::unique_ptr<LlamaRuntime> make_llama_runtime(std::string& reason) {
    reason.clear();
    return std::make_unique<RealRuntime>();
}

bool llama_available() noexcept {
    return true;
}

}  // namespace apogee::backends

#else  // APOGEE_ENABLE_LLAMA

namespace apogee::backends {

std::unique_ptr<LlamaRuntime> make_llama_runtime(std::string& reason) {
    // Not an error state: the default build deliberately excludes llama.cpp so
    // a cloud-only user does not pay for GPU kernel compilation. The message
    // says how to turn it on rather than reporting a fault.
    reason =
        "this build has no local inference: llama.cpp was not compiled in. Rebuild with "
        "-DAPOGEE_ENABLE_LLAMA=ON to enable the llamacpp backend";
    return nullptr;
}

bool llama_available() noexcept {
    return false;
}

}  // namespace apogee::backends

#endif  // APOGEE_ENABLE_LLAMA
