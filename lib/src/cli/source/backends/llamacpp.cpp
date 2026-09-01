#include "backends/llamacpp.h"

#include <algorithm>
#include <utility>

#include "backends/llamacpp_tokens.h"
#include "harness/errors.h"

namespace apogee::backends {
namespace {

/// A context has to hold at least one token whose logits we can sample from.
///
/// The edge case this exists for: a request whose token sequence is an exact
/// prefix match for what is already decoded (resending an identical prompt).
/// Reusing all of it would leave nothing to decode, and llama.cpp has no logits
/// to sample -- so one token is always re-decoded.
[[nodiscard]] std::size_t reusable_prefix(std::size_t shared, std::size_t total) {
    if (total == 0) {
        return 0;
    }
    return std::min(shared, total - 1);
}

/// Feeds `tokens` to `context` in batches it will accept.
///
/// llama.cpp caps one decode call, and a prompt longer than that cap is
/// rejected rather than split for us. Without this, the backend works on short
/// prompts and fails the first time someone pastes a file.
/// Refuses a prompt the context cannot hold, in our words rather than
/// llama.cpp's.
///
/// Its own failure is `decode: failed to find a memory slot`, which names
/// neither the prompt nor the setting that governs it. The fix is always the
/// same -- raise `context_size` or send less -- so the message should say so.
void reject_if_too_long(const LlamaContext& context, std::size_t prompt_tokens,
                        const std::string& backend_name) {
    const std::int64_t capacity = context.capacity();
    if (capacity > 0 && static_cast<std::int64_t>(prompt_tokens) >= capacity) {
        throw harness::ProviderError(
            backend_name, "the prompt is " + std::to_string(prompt_tokens) +
                              " tokens but this backend's context holds " +
                              std::to_string(capacity) +
                              ". Raise context_size on the backend, or start a new conversation");
    }
}

void decode_in_batches(LlamaContext& context, const std::vector<std::int32_t>& tokens,
                       std::int64_t position) {
    const auto limit =
        static_cast<std::size_t>(std::max<std::int64_t>(context.max_batch_tokens(), 1));
    for (std::size_t offset = 0; offset < tokens.size(); offset += limit) {
        const std::size_t count = std::min(limit, tokens.size() - offset);
        const std::vector<std::int32_t> slice{
            tokens.begin() + static_cast<std::ptrdiff_t>(offset),
            tokens.begin() + static_cast<std::ptrdiff_t>(offset + count)};
        context.decode(slice, position + static_cast<std::int64_t>(offset));
    }
}

}  // namespace

LlamaCppProvider::LlamaCppProvider(Options options, std::unique_ptr<LlamaRuntime> runtime)
    : options_{std::move(options)}, runtime_{std::move(runtime)} {
    if (!options_.clock) {
        options_.clock = [] { return std::chrono::steady_clock::now(); };
    }
}

std::unique_ptr<LlamaCppProvider> LlamaCppProvider::from_config(
    const std::string& backend_name, const harness::BackendConfig& config) {
    if (config.model_path.empty()) {
        throw harness::ProviderError(
            backend_name,
            "no model_path configured. Set model_path on this backend to the GGUF file "
            "you want to run locally");
    }

    std::string reason;
    std::unique_ptr<LlamaRuntime> runtime = make_llama_runtime(reason);
    if (runtime == nullptr) {
        throw harness::ProviderError(backend_name, reason);
    }

    Options options;
    options.backend_name = backend_name;
    options.model = config.model.empty() ? config.model_path : config.model;
    options.model_path = config.model_path;
    if (config.context_size.has_value()) {
        options.context_size = *config.context_size;
    }
    if (config.max_tokens.has_value()) {
        options.max_tokens = *config.max_tokens;
    }
    if (config.idle_unload_seconds.has_value() && *config.idle_unload_seconds > 0) {
        options.idle_unload = std::chrono::seconds{*config.idle_unload_seconds};
    }
    return std::make_unique<LlamaCppProvider>(std::move(options), std::move(runtime));
}

std::string_view LlamaCppProvider::backend_name() const noexcept {
    return options_.backend_name;
}

bool LlamaCppProvider::accepts_images() const noexcept {
    return false;
}

bool LlamaCppProvider::model_loaded() const noexcept {
    return model_ != nullptr;
}

void LlamaCppProvider::unload() {
    // Order matters: a context borrows its model, so it must go first.
    session_.reset();
    session_tokens_.clear();
    model_.reset();
}

harness::StatusEvent LlamaCppProvider::model_status() const {
    harness::StatusEvent event;
    event.type = model_ == nullptr ? harness::StatusEvent::Type::ModelLoading
                                   : harness::StatusEvent::Type::ModelReady;
    event.phase = harness::StatusEvent::Phase::Done;
    event.name = options_.model;
    return event;
}

void LlamaCppProvider::expire_if_idle() {
    if (options_.idle_unload.count() <= 0 || model_ == nullptr || !used_) {
        return;
    }
    if (options_.clock() - last_use_ >= options_.idle_unload) {
        unload();
    }
}

void LlamaCppProvider::ensure_model(const harness::StatusSink& on_status) {
    if (model_ != nullptr) {
        return;
    }

    if (on_status) {
        harness::StatusEvent event;
        event.type = harness::StatusEvent::Type::ModelLoading;
        event.phase = harness::StatusEvent::Phase::Start;
        event.name = options_.model;
        on_status(event);
    }

    std::string error;
    model_ = runtime_->load(options_.model_path, options_.gpu_layers, error);
    if (model_ == nullptr) {
        if (on_status) {
            harness::StatusEvent failed;
            failed.type = harness::StatusEvent::Type::ModelLoading;
            failed.phase = harness::StatusEvent::Phase::Error;
            failed.name = options_.model;
            failed.detail = error;
            on_status(failed);
        }
        // A clear message naming the file, never a crash -- the acceptance
        // criterion for this path.
        throw harness::ProviderError(options_.backend_name, error);
    }

    if (on_status) {
        harness::StatusEvent ready;
        ready.type = harness::StatusEvent::Type::ModelReady;
        ready.phase = harness::StatusEvent::Phase::Done;
        ready.name = options_.model;
        on_status(ready);
    }
}

std::int64_t LlamaCppProvider::count_prompt_tokens(const harness::ChatRequest& request) {
    if (model_ == nullptr) {
        return -1;  // see the header: never load a model to answer a measurement
    }
    return llama_tokens::count_prompt_tokens(*model_, options_.model, request.messages);
}

harness::ChatResponse LlamaCppProvider::chat(const harness::ChatRequest& request,
                                             const harness::CancellationToken& cancellation) {
    harness::StreamOptions options;
    options.cancellation = cancellation;
    return run(request, options);
}

harness::ChatResponse LlamaCppProvider::stream_chat(const harness::ChatRequest& request,
                                                    const harness::StreamOptions& options) {
    return run(request, options);
}

harness::ChatResponse LlamaCppProvider::run(const harness::ChatRequest& request,
                                            const harness::StreamOptions& options) {
    expire_if_idle();
    ensure_model(options.on_status);

    const std::vector<std::int32_t> prompt =
        llama_tokens::tokenize_prompt(*model_, options_.model, request.messages, true);

    // A side request -- a background title summary, a one-off clerk call -- is
    // not a turn of this conversation. It runs on its own throwaway context so
    // the session's KV is untouched: Ommi's SideRequest lesson, where an async
    // titler's cache write clobbered the session it was titling.
    const bool side_request = request.transient.side_request;

    LlamaContext* context = nullptr;
    std::unique_ptr<LlamaContext> scratch;

    if (side_request) {
        scratch = model_->make_context(options_.context_size);
        context = scratch.get();
        reject_if_too_long(*context, prompt.size(), options_.backend_name);
        decode_in_batches(*context, prompt, 0);
    } else {
        if (session_ == nullptr) {
            session_ = model_->make_context(options_.context_size);
            session_tokens_.clear();
        }
        context = session_.get();
        reject_if_too_long(*context, prompt.size(), options_.backend_name);

        const std::size_t shared = reusable_prefix(
            llama_tokens::common_prefix_length(session_tokens_, prompt), prompt.size());

        // Everything past the shared prefix is stale -- drop it from the KV so
        // the new suffix decodes into the right positions.
        context->trim_to(static_cast<std::int64_t>(shared));

        const std::vector<std::int32_t> suffix{prompt.begin() + static_cast<std::ptrdiff_t>(shared),
                                               prompt.end()};
        decode_in_batches(*context, suffix, static_cast<std::int64_t>(shared));
        session_tokens_ = prompt;
    }

    // Either path leaves the KV holding exactly positions [0, prompt.size()),
    // so generation continues from there regardless of how much was reused.
    const std::int64_t prompt_end = static_cast<std::int64_t>(prompt.size());

    options.cancellation.throw_if_cancelled();

    const std::int64_t limit =
        request.max_tokens.value_or(0) > 0 ? *request.max_tokens : options_.max_tokens;

    std::string answer;
    std::vector<std::int32_t> generated;
    harness::FinishReason finish = harness::FinishReason::Stop;

    // Where generation must stop even if the model would keep going. Found on
    // real hardware, not by the scripted runtime: `apogee chat`'s background
    // title request has no max_tokens of its own, so it ran to the provider
    // default -- and a model that never emits end-of-generation filled the KV
    // cache and threw, taking the whole turn down with it. A truncated title is
    // a non-event; an exception mid-conversation is not.
    const std::int64_t wall = context->capacity();

    std::int64_t produced = 0;
    for (; produced < limit; ++produced) {
        if (wall > 0 && prompt_end + produced >= wall) {
            finish = harness::FinishReason::Length;
            break;
        }

        // Between tokens, not merely at entry: a local model generating into a
        // long answer is exactly when a user reaches for Ctrl-C.
        options.cancellation.throw_if_cancelled();

        const std::int32_t token = context->sample();
        if (model_->is_eog(token)) {
            break;
        }

        const std::string piece = model_->token_text(token);
        answer += piece;
        generated.push_back(token);
        if (options.on_token && !piece.empty()) {
            options.on_token(piece);
        }

        // Feed the token back so the next sample sees it. Its position is the
        // end of the prompt plus however many we have already produced.
        context->decode({token}, prompt_end + produced);
    }
    // Reaching the cap without an end-of-generation token is a truncated
    // answer, and a surface that shows it as complete is lying to the user.
    if (produced >= limit) {
        finish = harness::FinishReason::Length;
    }

    if (!side_request) {
        // What the KV now holds is the prompt plus everything generated, and
        // that is what the next turn's prefix match must be made against.
        //
        // **Transient (RAG) content needs no special handling here**, which is
        // worth stating because the constraint is explicit on this item. The
        // match is token-for-token, so a turn can only reuse a cached token
        // that its own prompt actually contains: the moment the next request
        // stops carrying the injected block, the prefix ends there and every
        // transient token is trimmed. An extra "forget the transient region"
        // step was written first and then removed -- it could only ever make
        // the remembered prefix SHORTER than the truth, never protect
        // correctness, and it could not be made to fail a test.
        session_tokens_.insert(session_tokens_.end(), generated.begin(), generated.end());
    }

    last_use_ = options_.clock();
    used_ = true;

    harness::ChatResponse response;
    response.message = harness::ChatMessage::assistant(answer);
    response.finish_reason = finish;
    response.model = options_.model;
    // Exact on both sides: this is our own tokenizer, not an estimate and not a
    // vendor's report.
    response.usage.prompt_tokens = static_cast<std::int64_t>(prompt.size());
    response.usage.completion_tokens = static_cast<std::int64_t>(generated.size());
    return response;
}

std::vector<harness::ModelInfo> LlamaCppProvider::list_models(
    const harness::CancellationToken& cancellation) {
    cancellation.throw_if_cancelled();
    // A local backend serves exactly the one GGUF it was pointed at. Listing
    // what is on disk is model management -- a different item's concern.
    harness::ModelInfo info;
    info.id = options_.model;
    info.name = options_.model;
    info.provider = "llamacpp";
    info.backend = options_.backend_name;
    return {info};
}

}  // namespace apogee::backends
