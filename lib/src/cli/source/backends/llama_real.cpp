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

class RealContext final : public LlamaContext {
public:
    RealContext(std::unique_ptr<llama_context, ContextDeleter> context,
                std::unique_ptr<llama_sampler, SamplerDeleter> sampler)
        : context_{std::move(context)}, sampler_{std::move(sampler)} {}

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
    std::unique_ptr<llama_context, ContextDeleter> context_;
    std::unique_ptr<llama_sampler, SamplerDeleter> sampler_;
    std::int64_t evaluated_ = 0;
};

class RealModel final : public LlamaModel {
public:
    explicit RealModel(std::unique_ptr<llama_model, ModelDeleter> model)
        : model_{std::move(model)}, vocab_{llama_model_get_vocab(model_.get())} {}

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

        return std::make_unique<RealContext>(std::move(context), std::move(sampler));
    }

private:
    std::unique_ptr<llama_model, ModelDeleter> model_;
    const llama_vocab* vocab_ = nullptr;
};

class RealRuntime final : public LlamaRuntime {
public:
    [[nodiscard]] std::unique_ptr<LlamaModel> load(const std::string& path, std::int64_t gpu_layers,
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
        return std::make_unique<RealModel>(std::move(model));
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
