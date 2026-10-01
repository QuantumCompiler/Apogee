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

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "backends/llama_chat.h"
#include "models/gguf_inspect.h"
#include "models/kv_cache.h"

namespace apogee::backends {
namespace {

/// What llama.cpp and mtmd said at WARN and above, kept rather than printed.
///
/// It used to go straight to stderr, and inside a chat turn that lands on top
/// of the live spinner -- "✻ Thinking…init: the tokens of sequence 0 in the
/// input batch have inconsistent sequence positions:", then our own error
/// below it (2026-09-23). A warning that is not a failure is only noise
/// there. A failure's reason is worth having, so it rides on the error it
/// explains instead: `with_llama_reason` appends what was said since the
/// last `forget`.
class LlamaLog {
public:
    void add(ggml_log_level level, std::string_view text) {
        const std::lock_guard<std::mutex> lock{mutex_};
        // A continuation belongs to whatever it continues: kept only when
        // that was.
        if (level != GGML_LOG_LEVEL_CONT) {
            keeping_ = level >= GGML_LOG_LEVEL_WARN;
        }
        if (!keeping_) {
            return;
        }
        text_ += text;
        if (text_.size() > kCap) {
            text_.erase(0, text_.size() - kCap);
        }
    }

    void forget() {
        const std::lock_guard<std::mutex> lock{mutex_};
        text_.clear();
    }

    [[nodiscard]] std::string take() {
        const std::lock_guard<std::mutex> lock{mutex_};
        std::string out = std::move(text_);
        text_.clear();
        while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) {
            out.pop_back();
        }
        return out;
    }

private:
    static constexpr std::size_t kCap = 2048;
    std::mutex mutex_;
    std::string text_;
    bool keeping_ = false;
};

LlamaLog& llama_log() {
    static LlamaLog log;
    return log;
}

void keep_llama_log(ggml_log_level level, const char* text, void* /*user_data*/) {
    if (text != nullptr) {
        llama_log().add(level, text);
    }
}

/// `message`, with llama.cpp's own words on it when it said any.
[[nodiscard]] std::string with_llama_reason(std::string message) {
    if (const std::string said = llama_log().take(); !said.empty()) {
        message += "\nllama.cpp said: " + said;
    }
    return message;
}

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
        // llama.cpp's own logging, kept rather than printed (`LlamaLog`),
        // before anything can emit.
        //
        // Loading a model prints ~20 lines of Metal/ggml device capabilities at
        // INFO. On a CLI whose contract is "the answer, and nothing else", that
        // buries our error message in kernel chatter -- the acceptance criterion
        // here asks for a CLEAR error naming the file, and three useful lines
        // preceded by twenty irrelevant ones does not qualify. WARN and ERROR
        // are kept: when a model genuinely fails, llama.cpp's reason is more
        // specific than anything we can infer, and it is appended to ours.
        //
        // mtmd logs through its OWN channels, which `llama_log_set` does not
        // reach -- and it is chattier: "encoding image slice...", "image
        // decoded (batch 1/1) in 55 ms". Found by running it: those lines
        // landed on STDOUT, interleaved with the answer. On a terminal that is
        // noise; in machine mode it is non-JSON in the middle of the event
        // stream, which breaks the driver's parser at the worst moment. Same
        // rule for both of mtmd's loggers.
        llama_log_set(keep_llama_log, nullptr);
        mtmd_log_set(keep_llama_log, nullptr);
        mtmd_helper_log_set(keep_llama_log, nullptr);

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
using SamplerPtr = std::unique_ptr<llama_sampler, SamplerDeleter>;

/// The sampler chain for one generation: the grammar when there is one, then
/// greedy selection. Greedy: deterministic, and per-family sampling belongs to
/// its own item (sampling-profiles). Null with `error` when the grammar does
/// not compile.
SamplerPtr make_sampler(const llama_vocab* vocab, const SamplingGrammar& grammar,
                        std::string& error) {
    SamplerPtr chain{llama_sampler_chain_init(llama_sampler_chain_default_params())};
    if (!grammar.gbnf.empty()) {
        llama_log().forget();
        llama_sampler* constrained = nullptr;
        if (grammar.lazy) {
            std::vector<const char*> patterns;
            patterns.reserve(grammar.trigger_patterns.size());
            for (const std::string& pattern : grammar.trigger_patterns) {
                patterns.push_back(pattern.c_str());
            }
            std::vector<llama_token> tokens{grammar.trigger_tokens.begin(),
                                            grammar.trigger_tokens.end()};
            constrained = llama_sampler_init_grammar_lazy_patterns(
                vocab, grammar.gbnf.c_str(), "root", patterns.data(), patterns.size(),
                tokens.data(), tokens.size());
        } else {
            constrained = llama_sampler_init_grammar(vocab, grammar.gbnf.c_str(), "root");
        }
        if (constrained == nullptr) {
            error = with_llama_reason("llama.cpp: the tool-call grammar did not compile");
            return nullptr;
        }
        // The chain owns what is added to it.
        llama_sampler_chain_add(chain.get(), constrained);
    }
    llama_sampler_chain_add(chain.get(), llama_sampler_init_greedy());
    return chain;
}

/// A reply reader over llama.cpp's own parser for the rendered template.
class RealReplyReader final : public ReplyReader {
public:
    explicit RealReplyReader(std::unique_ptr<llama_chat::ReplyParser> parser)
        : parser_{std::move(parser)} {}

    [[nodiscard]] bool read(std::string_view text, bool partial, ParsedReply& out,
                            std::string& error) const override {
        llama_chat::Reply reply;
        if (!parser_->parse(std::string{text}, partial, reply, error)) {
            return false;
        }
        out.content = std::move(reply.content);
        out.reasoning = std::move(reply.reasoning);
        out.tool_calls.clear();
        for (llama_chat::ToolCall& call : reply.tool_calls) {
            harness::ToolCall converted;
            converted.id = std::move(call.id);
            converted.name = std::move(call.name);
            converted.arguments = call.arguments.empty() ? "{}" : std::move(call.arguments);
            out.tool_calls.push_back(std::move(converted));
        }
        return true;
    }

private:
    std::unique_ptr<llama_chat::ReplyParser> parser_;
};

class RealContext final : public LlamaContext {
public:
    /// `checkpoints` is whether this context's memory needs them: a
    /// recurrent or hybrid model's running state cannot be rewound (25c), and
    /// a sliding-window model's cache keeps only its window (26m), whose
    /// length is `sliding_window` (0 for none).
    RealContext(std::unique_ptr<llama_context, ContextDeleter> context, SamplerPtr sampler,
                mtmd_context* vision, bool checkpoints, harness::KvCacheType cache_type,
                std::int64_t sliding_window)
        : context_{std::move(context)},
          sampler_{std::move(sampler)},
          checkpoints_needed_{checkpoints},
          vision_{vision},
          cache_type_{cache_type},
          sliding_window_{sliding_window} {}

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

        llama_log().forget();
        const std::int32_t status = llama_decode(context_.get(), batch);
        if (status != 0) {
            throw std::runtime_error(with_llama_reason(
                status == 1 ? "llama.cpp: no KV slot for the batch -- the prompt "
                              "exceeds this backend's context_size"
                            : "llama.cpp: decode failed (" + std::to_string(status) + ")"));
        }
        evaluated_ += static_cast<std::int64_t>(tokens.size());
    }

    [[nodiscard]] std::int32_t sample() override {
        // `llama_sampler_sample` accepts the token itself. A second accept
        // here was harmless while the chain was greedy alone, and would
        // advance a grammar twice per token (found by 25b, 2026-09-25).
        return llama_sampler_sample(sampler_.get(), context_.get(), -1);
    }

    [[nodiscard]] bool set_grammar(const SamplingGrammar& grammar, std::string& error) override {
        // A fresh chain every generation: a grammar's state is one reply's.
        SamplerPtr chain =
            make_sampler(llama_model_get_vocab(llama_get_model(context_.get())), grammar, error);
        if (chain == nullptr) {
            return false;
        }
        sampler_ = std::move(chain);
        return true;
    }

    [[nodiscard]] std::int64_t trim_to(std::int64_t position) override {
        // p1 < 0 means "to infinity": drop everything from `position` on.
        llama_memory_t memory = llama_get_memory(context_.get());
        if (llama_memory_seq_rm(memory, 0, static_cast<llama_pos>(position), -1) &&
            window_intact(llama_memory_seq_pos_min(memory, 0), position, sliding_window_)) {
            forget_checkpoints_after(checkpoints_, position);
            return position;
        }
        // A running state that cannot be rewound this far (see the
        // interface): refused, and untouched. Or a sliding window the cut left
        // short: llama.cpp removes the positions, and the ones before them the
        // next token looks back over are already gone (26m). Either way the
        // newest checkpoint at or before `position` holds the state as it was
        // there -- llama-server's restore (tools/server/server-context.cpp).
        // Once it is back, the attention half trims to it like any other
        // cache, and the restored part already ends there, so the cut
        // succeeds. A sliding checkpoint holds its window and a batch, so its
        // window is whole; it is checked on the state that came back anyway,
        // since the alternative to checking is a wrong answer.
        if (const auto it = checkpoint_for(checkpoints_, position); it != checkpoints_.end()) {
            llama_log().forget();
            const std::size_t loaded =
                llama_state_seq_set_data_ext(context_.get(), it->data.data(), it->data.size(), 0,
                                             LLAMA_STATE_SEQ_FLAGS_PARTIAL_ONLY);
            // A restore that did not take falls through: start again below.
            if (loaded != 0 &&
                llama_memory_seq_rm(memory, 0, static_cast<llama_pos>(it->position), -1) &&
                window_intact(llama_memory_seq_pos_min(memory, 0), it->position, sliding_window_)) {
                const std::int64_t restored = it->position;
                forget_checkpoints_after(checkpoints_, restored);
                return restored;
            }
        }
        // No checkpoint to go back to -- start again from nothing.
        llama_memory_clear(memory, true);
        checkpoints_.clear();
        return 0;
    }

    [[nodiscard]] bool checkpoint(std::int64_t position) override {
        if (!checkpoints_needed_ || position <= 0) {
            return false;
        }
        // Only the part that cannot be rewound: the attention half stays in
        // the cache and is trimmed like any other.
        const std::size_t size =
            llama_state_seq_get_size_ext(context_.get(), 0, LLAMA_STATE_SEQ_FLAGS_PARTIAL_ONLY);
        if (size == 0) {
            return false;
        }
        Checkpoint saved;
        saved.position = position;
        saved.data.resize(size);
        if (llama_state_seq_get_data_ext(context_.get(), saved.data.data(), size, 0,
                                         LLAMA_STATE_SEQ_FLAGS_PARTIAL_ONLY) == 0) {
            return false;
        }
        keep_checkpoint(checkpoints_, std::move(saved));
        return true;
    }

    [[nodiscard]] bool needs_checkpoints() const noexcept override {
        return checkpoints_needed_;
    }

    [[nodiscard]] std::size_t checkpoint_count() const noexcept override {
        return checkpoints_.size();
    }

    [[nodiscard]] std::size_t checkpoint_bytes() const noexcept override {
        std::size_t total = 0;
        for (const Checkpoint& held : checkpoints_) {
            total += held.data.size();
        }
        return total;
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

    [[nodiscard]] harness::KvCacheType cache_type() const noexcept override {
        return cache_type_;
    }

private:
    std::int64_t decode_multimodal(const std::vector<MediaInput>& media, std::string_view text,
                                   std::int64_t position, std::string& error) override {
        if (vision_ == nullptr) {
            error = "this backend has no mmproj_path configured, so it cannot read images";
            return -1;
        }

        // mtmd decodes the bytes itself -- PNG, JPEG and the rest, and WAV
        // audio at the projector's own rate (26e) -- which is why the seam
        // carries raw bytes rather than pixels or samples. Doing our own
        // decoding would mean a second image library and a second set of
        // format bugs.
        std::vector<BitmapPtr> owned;
        std::vector<const mtmd_bitmap*> borrowed;
        owned.reserve(media.size());
        borrowed.reserve(media.size());
        llama_log().forget();
        for (const MediaInput& item : media) {
            // Since b11151 the helper also decodes video, handing back a
            // video context beside the bitmap -- by spawning ffmpeg from code
            // whose output Apogee does not own. A clip arrives here as its
            // frames, extracted by Apogee's own runner (26e); a video context
            // is freed and the input refused.
            const mtmd_helper_bitmap_wrapper decoded = mtmd_helper_bitmap_init_from_buf(
                vision_, reinterpret_cast<const unsigned char*>(item.bytes.data()),
                item.bytes.size(),
                /*placeholder=*/false, mtmd_helper_init_opt_default());
            BitmapPtr bitmap{decoded.bitmap};
            if (decoded.video_ctx != nullptr) {
                mtmd_helper_video_free(decoded.video_ctx);
                error =
                    "an attachment decoded as video, which this backend takes only as its "
                    "frames -- attach it with /attach";
                return -1;
            }
            if (bitmap == nullptr) {
                error = with_llama_reason(
                    "an attached image or sound could not be decoded -- it may be a format this "
                    "projector does not handle, or the file may be damaged");
                return -1;
            }
            if (item.frame) {
                // Consecutive frames of a clip: a video model merges them.
                mtmd_bitmap_set_mergeable(bitmap.get(), true);
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
        // Since b11151 the text is read by length, not to its terminator:
        // left at zero, mtmd saw an empty prompt with no image markers in it,
        // and refused every image (found live, 2026-09-23 -- the compiler
        // cannot, since the field value-initialises).
        input.text_len = prompt.size();
        input.add_special = true;
        input.parse_special = true;

        llama_log().forget();
        const std::int32_t tokenized =
            mtmd_tokenize(vision_, chunks.get(), &input, borrowed.data(), borrowed.size());
        if (tokenized == 1) {
            // A marker count that does not match the number of images: our
            // bug rather than the user's, so it says what went wrong rather
            // than blaming the picture.
            error = "the multimodal prompt could not be tokenized (marker/image mismatch)";
            return -1;
        }
        if (tokenized != 0) {
            // 2: the projector could not prepare an image -- mtmd says why.
            error = with_llama_reason("an attached image could not be prepared for this projector");
            return -1;
        }

        llama_pos new_position = 0;
        llama_log().forget();
        const std::int32_t status = mtmd_helper_eval_chunks(
            vision_, context_.get(), chunks.get(), static_cast<llama_pos>(position),
            /*seq_id=*/0, static_cast<std::int32_t>(llama_n_batch(context_.get())),
            /*logits_last=*/true, &new_position);
        if (status != 0) {
            error = with_llama_reason(
                "evaluating the image failed -- the context may be too small to hold it");
            return -1;
        }

        evaluated_ += static_cast<std::int64_t>(new_position) - position;
        return static_cast<std::int64_t>(new_position);
    }

    /// The saved running state after exactly `position` tokens.
    struct Checkpoint {
        std::int64_t position = 0;
        std::vector<std::uint8_t> data;
    };

    std::unique_ptr<llama_context, ContextDeleter> context_;
    SamplerPtr sampler_;
    std::int64_t evaluated_ = 0;
    bool checkpoints_needed_ = false;
    /// Oldest first; at most kMaxCheckpoints.
    std::vector<Checkpoint> checkpoints_;
    /// Borrowed from the model, which outlives every context made from it.
    mtmd_context* vision_ = nullptr;
    harness::KvCacheType cache_type_ = harness::KvCacheType::F16;
    std::int64_t sliding_window_ = 0;
};

/// How a model's generation contexts are made (26a).
struct ContextSettings {
    harness::KvCacheType cache_type = harness::KvCacheType::Q8_0;
    /// Named in the config: used as written, or the context is refused.
    bool cache_type_named = false;
    /// What free memory held at load, or 0; see `fitted_window`.
    std::int64_t fitted_window = 0;
};

[[nodiscard]] ggml_type ggml_type_of(harness::KvCacheType type) noexcept {
    switch (type) {
        case harness::KvCacheType::F16:
            return GGML_TYPE_F16;
        case harness::KvCacheType::Q8_0:
            return GGML_TYPE_Q8_0;
        case harness::KvCacheType::Q4_0:
            return GGML_TYPE_Q4_0;
    }
    return GGML_TYPE_F16;
}

class RealModel final : public LlamaModel {
public:
    /// `vision` may be null: a text-only model.
    RealModel(std::unique_ptr<llama_model, ModelDeleter> model, MtmdPtr vision,
              ContextSettings settings)
        : model_{std::move(model)},
          vocab_{llama_model_get_vocab(model_.get())},
          vision_{std::move(vision)},
          settings_{settings} {}

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

    [[nodiscard]] std::string special_token_text(std::int32_t token) const override {
        std::string piece(64, '\0');
        std::int32_t written = llama_token_to_piece(
            vocab_, token, piece.data(), static_cast<std::int32_t>(piece.size()), 0, true);
        if (written < 0) {
            piece.resize(static_cast<std::size_t>(-written));
            written = llama_token_to_piece(vocab_, token, piece.data(),
                                           static_cast<std::int32_t>(piece.size()), 0, true);
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

    [[nodiscard]] bool render_chat(const std::vector<harness::ChatMessage>& messages,
                                   const std::vector<harness::Tool>& tools, bool enable_thinking,
                                   bool add_generation_prompt, ChatRendering& out,
                                   std::string& error) const override {
        if (!templates_loaded_) {
            // Once per load: parsing a Jinja template is not free, and its
            // answer cannot change while the weights are resident.
            templates_loaded_ = true;
            templates_ = llama_chat::Templates::load(model_.get(), templates_error_);
        }
        if (templates_ == nullptr) {
            error = templates_error_;
            return false;
        }

        llama_chat::Inputs inputs;
        inputs.enable_thinking = enable_thinking;
        inputs.add_generation_prompt = add_generation_prompt;
        inputs.messages.reserve(messages.size());
        for (const harness::ChatMessage& message : messages) {
            llama_chat::Message converted;
            converted.role = std::string{harness::to_string(message.role)};
            converted.content = message.content.plain_text();
            converted.tool_call_id = message.tool_call_id;
            converted.tool_name = message.name;
            for (const harness::ToolCall& call : message.tool_calls) {
                converted.tool_calls.push_back({call.id, call.name, call.arguments});
            }
            inputs.messages.push_back(std::move(converted));
        }
        inputs.tools.reserve(tools.size());
        for (const harness::Tool& tool : tools) {
            inputs.tools.push_back({tool.name, tool.description, tool.parameters_schema});
        }

        llama_chat::Rendered rendered;
        if (!templates_->render(inputs, rendered, error)) {
            return false;
        }
        out.prompt = std::move(rendered.prompt);
        out.grammar.gbnf = std::move(rendered.grammar);
        out.grammar.lazy = rendered.grammar_lazy;
        out.grammar.trigger_patterns = std::move(rendered.trigger_patterns);
        out.grammar.trigger_tokens = std::move(rendered.trigger_tokens);
        out.preserved_tokens = std::move(rendered.preserved_tokens);
        out.stops = std::move(rendered.stops);
        out.format = std::move(rendered.format);
        out.reader = std::make_unique<RealReplyReader>(std::move(rendered.parser));
        return true;
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

    [[nodiscard]] std::int64_t fitted_window() const noexcept override {
        return settings_.fitted_window;
    }

    [[nodiscard]] std::unique_ptr<LlamaContext> make_context(std::int64_t context_size) override {
        // The provider always names the window (26a); 0 is kept meaning the
        // trained one, resolved here because n_batch needs a real number.
        const std::int64_t resolved =
            context_size > 0 ? context_size : llama_model_n_ctx_train(model_.get());

        harness::KvCacheType cache_type = settings_.cache_type;
        std::unique_ptr<llama_context, ContextDeleter> context = create(resolved, cache_type);
        if (context == nullptr && cache_type != harness::KvCacheType::F16 &&
            !settings_.cache_type_named) {
            // The default gives way rather than failing: a quantized cache
            // needs every head width to divide into its blocks of 32, and
            // flash attention for them. A cache_type the user named is theirs
            // and is refused below instead.
            cache_type = harness::KvCacheType::F16;
            context = create(resolved, cache_type);
        }
        if (context == nullptr) {
            std::string message = "llama.cpp: could not create a context for this model";
            if (settings_.cache_type_named && cache_type != harness::KvCacheType::F16) {
                message += " with cache_type " + std::string{harness::to_string(cache_type)} +
                           " -- try cache_type: f16";
            }
            throw std::runtime_error(with_llama_reason(message));
        }

        std::string error;
        SamplerPtr sampler = make_sampler(vocab_, SamplingGrammar{}, error);
        // Checkpoints where the memory cannot be rewound -- a recurrent or
        // hybrid model -- and where it keeps only a sliding window (26m).
        const std::int64_t sliding_window = llama_model_n_swa(model_.get());
        const bool checkpoints = llama_model_is_recurrent(model_.get()) ||
                                 llama_model_is_hybrid(model_.get()) || sliding_window > 0;
        return std::make_unique<RealContext>(std::move(context), std::move(sampler), vision_.get(),
                                             checkpoints, cache_type, sliding_window);
    }

    [[nodiscard]] bool supports_vision() const noexcept override {
        // Both halves: a projector was loaded AND it does images. A projector
        // can load and be audio-only, and answering yes on the strength of
        // "an mmproj was configured" is how a surface accepts a picture it
        // cannot use.
        return vision_ != nullptr && mtmd_support_vision(vision_.get());
    }

    [[nodiscard]] bool supports_audio() const noexcept override {
        return vision_ != nullptr && mtmd_support_audio(vision_.get());
    }

    [[nodiscard]] int audio_sample_rate() const noexcept override {
        if (vision_ == nullptr) {
            return 0;
        }
        const int rate = mtmd_get_audio_sample_rate(vision_.get());
        return rate > 0 ? rate : 0;
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
            llama_log().forget();
            const std::int32_t status =
                llama_model_has_encoder(model_.get()) && !llama_model_has_decoder(model_.get())
                    ? llama_encode(ctx, batch)
                    : llama_decode(ctx, batch);
            if (status != 0) {
                llama_batch_free(batch);
                error = with_llama_reason("llama.cpp: embedding decode failed (" +
                                          std::to_string(status) + ")");
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
    /// A generation context of `window` positions whose keys and values are
    /// kept as `cache_type`; null when llama.cpp will not make one.
    [[nodiscard]] std::unique_ptr<llama_context, ContextDeleter> create(
        std::int64_t window, harness::KvCacheType cache_type) const {
        llama_context_params params = llama_context_default_params();
        params.n_ctx = static_cast<std::uint32_t>(window);
        // n_batch is left at llama.cpp's own default, and the provider chunks
        // its prompt to `max_batch_tokens()` instead.
        //
        // Setting `n_batch = n_ctx` was tried first and is WRONG: llama.cpp
        // reserves batch-sized headroom inside the KV cache, so making the two
        // equal leaves no slot for the first generated token. It fails as
        // `decode: failed to find a memory slot for batch of size 1` on turn
        // two -- a message that names neither the cause nor the setting. Real
        // hardware found this; the scripted runtime cannot model an allocator.
        params.type_k = ggml_type_of(cache_type);
        params.type_v = ggml_type_of(cache_type);
        // A sliding-window layer keeps its window and a batch, not the whole
        // conversation (26m): llama-server's default, and on Gemma 4 31B at
        // 32K the difference between 2.0 GiB of cache and 14.6. Such a cache
        // cannot be cut back past its window, which `trim_to` checks, with
        // checkpoints to go back to. A model with no sliding window ignores it.
        params.swa_full = false;
        // A quantized cache needs flash attention, so it is turned on rather
        // than left to detection -- which, where a device lacked the kernel,
        // would turn it off and fail the context. An f16 cache leaves it to
        // llama.cpp, which turns it on where the device has it (every model
        // measured on Metal) and off rather than running attention on the CPU.
        params.flash_attn_type = cache_type == harness::KvCacheType::F16
                                     ? LLAMA_FLASH_ATTN_TYPE_AUTO
                                     : LLAMA_FLASH_ATTN_TYPE_ENABLED;
        llama_log().forget();
        return std::unique_ptr<llama_context, ContextDeleter>{
            llama_init_from_model(model_.get(), params)};
    }

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
        llama_log().forget();
        embedding_context_.reset(llama_init_from_model(model_.get(), params));
        if (embedding_context_ == nullptr) {
            error = with_llama_reason(
                "llama.cpp: could not create an embedding context for this model");
            return false;
        }
        return true;
    }

    std::unique_ptr<llama_model, ModelDeleter> model_;
    const llama_vocab* vocab_ = nullptr;
    /// The projector, when one was configured. Outlives every context made
    /// from this model, which is why contexts may borrow it raw.
    MtmdPtr vision_;
    ContextSettings settings_;
    /// Lazily created; see ensure_embedding_context.
    std::unique_ptr<llama_context, ContextDeleter> embedding_context_;
    /// The model's chat templates, parsed on first render; see render_chat.
    mutable std::unique_ptr<llama_chat::Templates> templates_;
    mutable std::string templates_error_;
    mutable bool templates_loaded_ = false;
};

/// Free memory across the devices a model offloads to; 0 when it runs on
/// the CPU, or no device says.
[[nodiscard]] std::int64_t free_device_memory(std::int64_t gpu_layers) {
    if (gpu_layers <= 0) {
        return 0;
    }
    std::int64_t total = 0;
    for (std::size_t index = 0; index < ggml_backend_dev_count(); ++index) {
        ggml_backend_dev_t device = ggml_backend_dev_get(index);
        const enum ggml_backend_dev_type type = ggml_backend_dev_type(device);
        if (type != GGML_BACKEND_DEVICE_TYPE_GPU && type != GGML_BACKEND_DEVICE_TYPE_IGPU) {
            continue;
        }
        std::size_t free = 0;
        std::size_t size = 0;
        ggml_backend_dev_memory(device, &free, &size);
        total += static_cast<std::int64_t>(free);
    }
    return total;
}

/// The smallest window the fitter may lower one to: the side contexts'
/// floor, below which a conversation is hardly one.
constexpr std::uint32_t kMinimumFittedWindow = 4096;

/// What free memory holds, for a backend with no `context_size` (26a): 0
/// when the default window fits, else the fitter's answer -- the positions
/// that fit, up to the trained window, which the provider caps at the default.
///
/// llama.cpp's fitter is the one that answers, but it reads the model over to
/// project it -- 0.13 to 0.72 s on the models measured -- and on a
/// machine that holds the default it only ever says so. So a check that costs
/// nothing goes first: the weights, the default window's cache from the
/// header, and llama-server's 1 GiB margin, against what the devices have
/// free. Only a model that fails it, or whose cache the header cannot size,
/// is handed to the fitter.
[[nodiscard]] std::int64_t fit_default_window(const ModelLoad& request) {
    constexpr std::int64_t kMargin = std::int64_t{1024} * 1024 * 1024;
    const std::int64_t free = free_device_memory(request.gpu_layers);
    if (free <= 0) {
        // On the CPU: nothing to fit to, and system memory pages.
        return 0;
    }
    const models::GgufInfo info = models::inspect_gguf(request.path);
    if (const std::optional<models::CacheShape> shape =
            info.parsed ? models::cache_shape(info.architecture, info.attention) : std::nullopt;
        shape.has_value()) {
        const std::int64_t window = models::default_local_window(info.attention.context_length, 0);
        std::int64_t needed =
            info.file_size +
            models::cache_bytes(models::cache_values(*shape, window), request.cache_type) + kMargin;
        if (!request.mmproj_path.empty()) {
            std::error_code code;
            const std::uintmax_t projector = std::filesystem::file_size(request.mmproj_path, code);
            needed += code ? 0 : static_cast<std::int64_t>(projector);
        }
        if (needed <= free) {
            return 0;
        }
    }
    const std::int64_t fitted = llama_chat::fit_window(
        request.path, static_cast<std::int32_t>(request.gpu_layers),
        static_cast<std::int32_t>(ggml_type_of(request.cache_type)), kMinimumFittedWindow);
    return std::max<std::int64_t>(fitted, 0);
}

class RealRuntime final : public LlamaRuntime {
public:
    [[nodiscard]] std::unique_ptr<LlamaModel> load(const ModelLoad& request,
                                                   std::string& error) override {
        ensure_backend_init();
        const std::string& path = request.path;
        const std::int64_t gpu_layers = request.gpu_layers;
        const std::string& mmproj_path = request.mmproj_path;

        ContextSettings settings;
        settings.cache_type = request.cache_type;
        settings.cache_type_named = request.cache_type_named;
        if (request.fit_window) {
            settings.fitted_window = fit_default_window(request);
        }

        llama_model_params params = llama_model_default_params();
        params.n_gpu_layers = static_cast<std::int32_t>(gpu_layers);

        llama_log().forget();
        std::unique_ptr<llama_model, ModelDeleter> model{
            llama_model_load_from_file(path.c_str(), params)};
        if (model == nullptr) {
            // Naming the file is the acceptance criterion: "no such model" with
            // no path sends the user to check their config for the wrong key.
            error = with_llama_reason("could not load the model at '" + path +
                                      "' -- check that the file exists and is a valid GGUF");
            return nullptr;
        }
        if (mmproj_path.empty()) {
            return std::make_unique<RealModel>(std::move(model), nullptr, settings);
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
            error = with_llama_reason("could not load the multimodal projector at '" + mmproj_path +
                                      "' -- check that it is the mmproj file matching this model");
            return nullptr;
        }
        return std::make_unique<RealModel>(std::move(model), std::move(vision), settings);
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
