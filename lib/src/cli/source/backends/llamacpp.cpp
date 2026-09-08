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

/// Decodes the base64 payload of a `data:` URI. Empty for anything else.
///
/// The IR carries images as data URIs because that is what the cloud vendors
/// take; mtmd wants the raw bytes, so this is where the two meet. A remote
/// `https://` image is NOT fetched here -- a local backend silently reaching
/// out to the network to answer a prompt is a surprise nobody asked for, and
/// the caller reports it as unsupported instead.
[[nodiscard]] std::string decode_data_uri(std::string_view url) {
    constexpr std::string_view marker_text = ";base64,";
    if (!url.starts_with("data:")) {
        return {};
    }
    const std::size_t marker = url.find(marker_text);
    if (marker == std::string_view::npos) {
        return {};
    }
    const std::string_view encoded = url.substr(marker + marker_text.size());

    static constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(encoded.size() / 4 * 3);

    std::uint32_t accumulator = 0;
    int bits = 0;
    for (const char c : encoded) {
        if (c == '=') {
            break;
        }
        const std::size_t value = alphabet.find(c);
        if (value == std::string_view::npos) {
            continue;  // whitespace and newlines are legal in a data URI
        }
        accumulator = (accumulator << 6U) | static_cast<std::uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((accumulator >> static_cast<unsigned>(bits)) & 0xFFU));
        }
    }
    return out;
}

/// Every image in `messages`, as raw encoded bytes.
[[nodiscard]] std::vector<std::string> collect_images(
    const std::vector<harness::ChatMessage>& messages) {
    std::vector<std::string> images;
    for (const harness::ChatMessage& message : messages) {
        for (const harness::ContentPart& part : message.content.parts()) {
            if (part.kind != harness::ContentPart::Kind::ImageUrl) {
                continue;
            }
            if (std::string bytes = decode_data_uri(part.image_url); !bytes.empty()) {
                images.push_back(std::move(bytes));
            }
        }
    }
    return images;
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
    options.mmproj_path = harness::expand_env(config.mmproj_path);
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
    // Answers from CONFIGURED state, not from a loaded model: this is asked
    // before a turn begins, and loading 16GB of weights to answer a yes/no
    // question would make every `--image` check cost a model load.
    //
    // Both conditions are necessary. Without llama.cpp there is no mtmd at all;
    // without an mmproj_path there is no projector to use. Whether the
    // projector actually does images (rather than audio) is checked when it
    // loads, and a mismatch surfaces there with a message naming the file.
    return llama_available() && !options_.mmproj_path.empty();
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
    model_ = runtime_->load(options_.model_path, options_.gpu_layers, options_.mmproj_path, error);
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

LlamaCppProvider::Generation LlamaCppProvider::generate(LlamaContext& context,
                                                        std::int64_t prompt_end,
                                                        const harness::ChatRequest& request,
                                                        const harness::StreamOptions& options) {
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
    const std::int64_t wall = context.capacity();

    std::int64_t produced = 0;
    for (; produced < limit; ++produced) {
        if (wall > 0 && prompt_end + produced >= wall) {
            finish = harness::FinishReason::Length;
            break;
        }

        // Between tokens, not merely at entry: a local model generating into a
        // long answer is exactly when a user reaches for Ctrl-C.
        options.cancellation.throw_if_cancelled();

        const std::int32_t token = context.sample();
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
        context.decode({token}, prompt_end + produced);
    }
    // Reaching the cap without an end-of-generation token is a truncated
    // answer, and a surface that shows it as complete is lying to the user.
    if (produced >= limit) {
        finish = harness::FinishReason::Length;
    }

    Generation result;
    result.text = std::move(answer);
    result.tokens = std::move(generated);
    result.finish = finish;
    return result;
}

harness::ChatResponse LlamaCppProvider::run_multimodal(const harness::ChatRequest& request,
                                                       const harness::StreamOptions& options,
                                                       const std::vector<std::string>& images) {
    if (!model_->supports_vision()) {
        // Reached when a model loaded but its projector does not do images --
        // an audio-only mmproj, say. The capability probe answered from config,
        // which cannot know that; this is where the truth arrives.
        throw harness::ProviderError(
            options_.backend_name,
            "this model has no usable image support. Check that mmproj_path points at the "
            "projector matching this model");
    }

    // The prompt is rendered as text with one marker per image, which is the
    // contract mtmd's tokenizer expects. Markers go at the FRONT of the user's
    // text: every vision model in this family was trained with the picture
    // before the question about it.
    const std::string marker = model_->image_marker();
    std::string prompt;
    for (std::size_t i = 0; i < images.size(); ++i) {
        prompt += marker;
        prompt += "\n";
    }
    prompt += llama_tokens::render_prompt(*model_, options_.model, request.messages, true);

    // A fresh context every time. There is no prefix to reuse -- an image
    // occupies embedding positions that no token comparison can match -- so
    // pretending otherwise would corrupt the cache rather than save work.
    std::unique_ptr<LlamaContext> scratch = model_->make_context(options_.context_size);
    LlamaContext& context = *scratch;

    std::string error;
    const std::int64_t prompt_end = context.decode_multimodal(images, prompt, 0, error);
    if (prompt_end < 0) {
        throw harness::ProviderError(options_.backend_name, error);
    }

    const Generation generation = generate(context, prompt_end, request, options);

    // The session's own KV is deliberately untouched: this turn ran on a
    // throwaway context, so `session_tokens_` still describes what the text
    // path cached and the next text turn can still reuse it.
    last_use_ = options_.clock();
    used_ = true;

    harness::ChatResponse response;
    response.message = harness::ChatMessage::assistant(generation.text);
    response.model = options_.model;
    response.finish_reason = generation.finish;
    response.usage.completion_tokens = static_cast<std::int64_t>(generation.tokens.size());
    return response;
}

harness::ChatResponse LlamaCppProvider::run(const harness::ChatRequest& request,
                                            const harness::StreamOptions& options) {
    expire_if_idle();
    ensure_model(options.on_status);

    // Images take a different route entirely. mtmd turns text-with-markers plus
    // decoded pictures into interleaved text and embedding chunks, so there is
    // no flat token vector to prefix-match against -- which is why the image
    // path below decodes from scratch and skips the KV reuse the text path
    // depends on. Paying that on a turn with a picture in it is the honest
    // trade; pretending an image is a token sequence is not.
    const std::vector<std::string> images = collect_images(request.messages);
    if (!images.empty()) {
        return run_multimodal(request, options, images);
    }

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

    // The sampling loop is shared with the image path: extracted when vision
    // landed, because the alternative was a second copy that would drift the
    // first time a stop condition changed.
    const Generation generation = generate(*context, prompt_end, request, options);
    const std::string& answer = generation.text;

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
        session_tokens_.insert(session_tokens_.end(), generation.tokens.begin(),
                               generation.tokens.end());
    }

    last_use_ = options_.clock();
    used_ = true;

    harness::ChatResponse response;
    response.message = harness::ChatMessage::assistant(answer);
    response.finish_reason = generation.finish;
    response.model = options_.model;
    // Exact on both sides: this is our own tokenizer, not an estimate and not a
    // vendor's report.
    response.usage.prompt_tokens = static_cast<std::int64_t>(prompt.size());
    response.usage.completion_tokens = static_cast<std::int64_t>(generation.tokens.size());
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
