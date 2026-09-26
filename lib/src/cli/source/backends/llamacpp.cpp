#include "backends/llamacpp.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <optional>
#include <random>
#include <utility>

#include "backends/llamacpp_embed.h"
#include "backends/llamacpp_tokens.h"
#include "backends/markup_filter.h"
#include "backends/native_tool_calls.h"
#include "events/bus.h"
#include "harness/errors.h"
#include "models/gguf_inspect.h"

namespace apogee::backends {
namespace {

/// The prompt-level form of structured output: a local model has no JSON
/// mode here (the pinned subtree carries no schema-to-grammar converter),
/// so the schema is stated in the system block and the caller validates.
/// Skipped when a system message already carries the schema text -- the
/// agent runner states it once itself -- so the model never reads it twice.
std::vector<harness::ChatMessage> messages_with_schema(const harness::ChatRequest& request) {
    const std::string& schema = request.transient.response_schema;
    if (schema.empty()) {
        return request.messages;
    }
    const nlohmann::json parsed = nlohmann::json::parse(schema, nullptr, false);
    const std::string text = parsed.is_discarded() ? schema : parsed.dump(2);
    for (const harness::ChatMessage& message : request.messages) {
        if (message.role == harness::Role::System &&
            message.content.plain_text().find("OUTPUT FORMAT") != std::string::npos) {
            return request.messages;
        }
    }
    std::vector<harness::ChatMessage> out = request.messages;
    const std::string instruction =
        "OUTPUT FORMAT\nYour response MUST be valid JSON conforming to the following JSON "
        "Schema. Output only the JSON object -- no surrounding text or markdown code "
        "blocks.\n\n" +
        text;
    // Beside an existing system message when there is one, else first.
    std::size_t at = 0;
    for (std::size_t i = 0; i < out.size(); ++i) {
        if (out[i].role == harness::Role::System) {
            at = i + 1;
        }
    }
    out.insert(out.begin() + static_cast<std::ptrdiff_t>(at),
               harness::ChatMessage::system(instruction));
    return out;
}

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

/// Sends `text` as a notice, when anyone is listening.
void notice(const harness::StreamOptions& options, std::string text) {
    if (!options.on_status) {
        return;
    }
    harness::StatusEvent event;
    event.type = harness::StatusEvent::Type::Notice;
    event.phase = harness::StatusEvent::Phase::Done;
    event.detail = std::move(text);
    options.on_status(event);
}

/// An id for a call the model's format left without one: nine letters and
/// digits, the shape the strictest template in use (Mistral's) insists on, so
/// the call and its result can be matched when the transcript renders again.
[[nodiscard]] std::string make_call_id() {
    static constexpr std::string_view kAlphabet =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    thread_local std::mt19937 generator{std::random_device{}()};
    std::uniform_int_distribution<std::size_t> pick{0, kAlphabet.size() - 1};
    std::string id(9, '0');
    for (char& c : id) {
        c = kAlphabet[pick(generator)];
    }
    return id;
}

/// A reply read through the model's own template format as it streams.
///
/// llama-server's method: the whole reply so far is re-read after every
/// token and only the difference is emitted -- reasoning to the thinking
/// sink, content to the answer, and a tool call held back entirely, so its
/// markup never reaches a screen or a transcript. Quadratic in the reply's
/// length, which is nothing at chat lengths (25b, default taken).
class TemplateReply {
public:
    TemplateReply(const ChatRendering& chat, const harness::StreamOptions& options)
        : chat_{chat}, options_{options} {}

    /// Takes the next piece. True when a stop string ended the reply; the
    /// stop string itself is not part of it.
    [[nodiscard]] bool write(std::string_view piece) {
        raw_ += piece;
        bool stopped = false;
        for (const std::string& stop : chat_.stops) {
            if (!stop.empty() && raw_.size() >= stop.size() &&
                raw_.compare(raw_.size() - stop.size(), stop.size(), stop) == 0) {
                raw_.resize(raw_.size() - stop.size());
                stopped = true;
                break;
            }
        }
        ParsedReply now;
        std::string ignored;
        if (chat_.reader->read(raw_, true, now, ignored)) {
            show(now);
        }
        return stopped;
    }

    /// Reads the finished reply. False, with `error`, when it does not match
    /// the format: what was already shown stands as the answer, and no call
    /// is taken from it -- a malformed call is never run on a guess.
    [[nodiscard]] bool finish(std::string& error) {
        ParsedReply parsed;
        if (!chat_.reader->read(raw_, false, parsed, error)) {
            final_.content = shown_content_;
            final_.tool_calls.clear();
            // Nothing shown at all: the reply comes out as the text it was,
            // rather than a turn with no answer, no tool and only a notice --
            // the same safety net the fallback's gate keeps (found on
            // Llama 3.2 3B, 2026-09-25). Not when the model was still
            // thinking: reasoning never becomes the answer.
            if (shown_content_.empty() && shown_reasoning_.empty() && !raw_.empty()) {
                final_.content = raw_;
                if (options_.on_token) {
                    options_.on_token(raw_);
                }
            }
            return false;
        }
        show(parsed);
        final_ = std::move(parsed);
        return true;
    }

    [[nodiscard]] const std::string& content() const noexcept {
        return final_.content;
    }

    [[nodiscard]] std::vector<harness::ToolCall> calls() const {
        return final_.tool_calls;
    }

private:
    /// Emits what `now` adds to what was already shown. A re-read that
    /// revises earlier text cannot take back what a screen already has, so
    /// only a strict extension is emitted.
    void show(const ParsedReply& now) {
        const auto extend = [](std::string& shown, const std::string& next,
                               const std::function<void(std::string_view)>& sink) {
            if (next.size() <= shown.size() || next.compare(0, shown.size(), shown) != 0) {
                return;
            }
            if (sink) {
                sink(std::string_view{next}.substr(shown.size()));
            }
            shown = next;
        };
        // Reasoning never reaches the answer: it goes to its own sink, and
        // the IR has no field to keep it in.
        extend(shown_reasoning_, now.reasoning, options_.on_thinking);
        extend(shown_content_, now.content, options_.on_token);
    }

    const ChatRendering& chat_;
    const harness::StreamOptions& options_;
    std::string raw_;
    std::string shown_reasoning_;
    std::string shown_content_;
    ParsedReply final_;
};

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

const ModelProfile* LlamaCppProvider::profile() const {
    if (profile_resolved_) {
        return profile_;
    }
    // The architecture comes from the GGUF's own header -- a fact recorded in
    // the file, which is why the ladder ranks it above guessing from a name.
    // Read here rather than through the runtime seam because it is a few
    // kilobytes and needs no model load: `model_behavior()` is asked before a
    // turn, and loading weights to answer it would be absurd.
    if (!options_.model_path.empty()) {
        architecture_ =
            models::inspect_gguf(std::filesystem::path{options_.model_path}).architecture;
    }
    profile_ = resolve_profile({}, architecture_, options_.model);
    profile_resolved_ = true;
    return profile_;
}

harness::ModelBehavior LlamaCppProvider::model_behavior() const {
    return behavior_for(profile());
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

std::vector<std::vector<float>> LlamaCppProvider::embed(
    const std::vector<std::string>& inputs, const harness::CancellationToken& cancellation) {
    cancellation.throw_if_cancelled();
    // The same idle policy as a chat turn, on the way in as well as the way
    // out: a model left resident by an embed-only workload is the 16GB this
    // setting exists to give back. The test for this found it missing.
    expire_if_idle();
    ensure_model({});
    try {
        std::vector<std::vector<float>> vectors = embed_with_llama(*model_, inputs, cancellation);
        // An embedding is a use of the model like any other, so the idle
        // timer restarts from here -- otherwise a long ingest could unload the
        // weights under itself.
        last_use_ = options_.clock();
        used_ = true;
        return vectors;
    } catch (const std::runtime_error& e) {
        throw harness::ProviderError(options_.backend_name, e.what());
    }
}

std::string LlamaCppProvider::embedding_model_name() const {
    if (!options_.model.empty()) {
        return options_.model;
    }
    return std::filesystem::path{options_.model_path}.filename().string();
}

std::size_t LlamaCppProvider::embedding_dimensions() const noexcept {
    return model_ == nullptr ? 0 : model_->embedding_dimensions();
}

bool LlamaCppProvider::uses_in_text_tool_calls() const noexcept {
    return model_behavior().native_tool_calls;
}

harness::StatusEvent LlamaCppProvider::model_status() const {
    harness::StatusEvent event;
    event.type = model_ == nullptr ? harness::StatusEvent::Type::ModelLoading
                                   : harness::StatusEvent::Type::ModelReady;
    event.phase = harness::StatusEvent::Phase::Done;
    event.name = options_.model;
    return event;
}

void LlamaCppProvider::preload(const harness::StatusSink& on_status) {
    ensure_model(on_status);
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
    // The lifecycle bus hears about a load whether or not anyone streams
    // status: a served client subscribed to /v1/admin/events sees the model
    // come up.
    events::emit(events::kModelLoadStarted,
                 nlohmann::json{{"backend", options_.backend_name}, {"model", options_.model}});

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
    events::emit(events::kModelLoadCompleted,
                 nlohmann::json{{"backend", options_.backend_name}, {"model", options_.model}});
}

std::int64_t LlamaCppProvider::count_prompt_tokens(const harness::ChatRequest& request) {
    if (model_ == nullptr) {
        return -1;  // see the header: never load a model to answer a measurement
    }
    // The prompt a turn would actually send, tool definitions included -- on
    // a local window they are not a rounding error.
    return static_cast<std::int64_t>(model_->tokenize(render_request(request).text, true).size());
}

LlamaCppProvider::RenderedRequest LlamaCppProvider::render_request(
    const harness::ChatRequest& request) const {
    RenderedRequest rendered;
    const std::vector<harness::ChatMessage> messages = messages_with_schema(request);
    // The template's own switch, where it has one; a family without one
    // ignores it (Qwen's closed think block is exactly this switch).
    const bool enable_thinking = !request.transient.skip_reasoning;
    auto chat = std::make_unique<ChatRendering>();
    if (model_->render_chat(messages, request.tools, enable_thinking, *chat,
                            rendered.fallback_reason)) {
        rendered.text = chat->prompt;
        rendered.chat = std::move(chat);
        return rendered;
    }
    // The fallback, as it was before 25b: llama.cpp's fixed template set or
    // the name-matched registry, and the profile's filters on the reply.
    rendered.text = llama_tokens::render_prompt(*model_, options_.model, messages, true);
    if (request.transient.skip_reasoning) {
        // After the generation prompt, so the model's first token is already
        // the answer's.
        rendered.text += reasoning_skip_for(profile());
    }
    return rendered;
}

void LlamaCppProvider::notice_if_toolless(const harness::ChatRequest& request,
                                          const RenderedRequest& rendered,
                                          const harness::StreamOptions& options) const {
    if (request.tools.empty() || rendered.chat != nullptr) {
        return;
    }
    // Unknown is permissive, and nothing is dropped silently: the turn still
    // runs, and the user is told why the model cannot see its tools.
    notice(options,
           options_.model + " is answering without tools: " +
               (rendered.fallback_reason.empty() ? std::string{"its template cannot take them"}
                                                 : rendered.fallback_reason));
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

std::int64_t LlamaCppProvider::generation_limit(const harness::ChatRequest& request) const {
    const std::int64_t own = request.max_tokens.value_or(0);
    return own > 0 ? own : options_.max_tokens;
}

std::int64_t LlamaCppProvider::side_context_size(const harness::ChatRequest& request,
                                                 std::size_t prompt_tokens) const {
    // Floored so llama.cpp's default batch (2048) stays below the window
    // rather than equal to it -- the edge the session context's sizing found
    // (llama_real.cpp, make_context) -- and a small window costs nothing.
    constexpr std::int64_t kFloor = 4096;
    constexpr std::int64_t kSlack = 256;
    const std::int64_t window =
        options_.context_size > 0 ? options_.context_size : model_->context_length();
    const std::int64_t needed =
        static_cast<std::int64_t>(prompt_tokens) + generation_limit(request) + kSlack;
    return std::min(window, std::max(needed, kFloor));
}

LlamaCppProvider::Generation LlamaCppProvider::generate(LlamaContext& context,
                                                        std::int64_t prompt_end,
                                                        const harness::ChatRequest& request,
                                                        const harness::StreamOptions& options,
                                                        const ChatRendering* chat) {
    options.cancellation.throw_if_cancelled();

    const std::int64_t limit = generation_limit(request);

    // The grammar is set every generation -- the session's context outlives
    // any one request, and a call's grammar from the last turn must not
    // constrain this one. None on the fallback path.
    if (std::string error;
        !context.set_grammar(chat != nullptr ? chat->grammar : SamplingGrammar{}, error)) {
        // The reader still parses a call without it; unconstrained is the
        // permissive reading, and the user is told.
        (void)context.set_grammar(SamplingGrammar{}, error);
        notice(options, "tool calls on " + options_.model + " run without their grammar: " + error);
    }
    std::optional<TemplateReply> reply;
    if (chat != nullptr) {
        reply.emplace(*chat, options);
    }

    std::string answer;
    std::vector<std::int32_t> generated;
    harness::FinishReason finish = harness::FinishReason::Stop;

    // Reasoning is separated HERE, at the source, so display, the returned
    // text, persisted history, and any later tool parsing all see the same
    // thing. Filtering at one surface and not another is how a <think> block
    // ends up in a saved transcript after being hidden on screen.
    //
    // A KNOWN profile's empty pair list is honoured as "this family emits
    // none"; an unprofiled model gets the permissive default set.
    ThinkFilter think{reasoning_pairs_for(profile())};
    if (options.on_thinking) {
        think.on_thinking(options.on_thinking);
    }

    // Three filters, in this order, and the order is load-bearing.
    //
    // ThinkFilter first, because for gpt-oss the reasoning block's OPENER is
    // itself a header (`<|channel|>analysis<|message|>`). Strip headers first
    // and the block loses its boundary, so the model's working lands in the
    // answer -- the exact bug Milestone P fixed for Qwen, reintroduced by a
    // different route.
    //
    // The gate second, because it keys on `<|channel|>commentary to=` and the
    // markup filter would have eaten the `<|channel|>` half of that.
    //
    // The markup filter last, on what is left: the pure framing between the
    // channels.
    ToolCallGate gate{model_behavior().native_tool_calls};
    MarkupFilter markup{header_markers_for(profile())};

    // One funnel, so every path -- token callback, returned text, saved history
    // -- sees the same bytes. A filter applied on one and not another is how a
    // hidden marker reappears in a transcript.
    const auto pump = [&](std::string_view piece) { return markup.write(gate.write(piece)); };

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
        generated.push_back(token);

        if (reply.has_value()) {
            // A preserved token is rendered as its text -- the call's opener
            // among them -- or the reader would see a call as bare JSON.
            const bool preserved =
                std::find(chat->preserved_tokens.begin(), chat->preserved_tokens.end(), token) !=
                chat->preserved_tokens.end();
            const std::string piece =
                preserved ? model_->special_token_text(token) : model_->token_text(token);
            if (reply->write(piece)) {
                break;  // a stop string: not fed back, the reply is over
            }
        } else {
            const std::string piece = model_->token_text(token);
            const std::string visible = pump(think.write(piece));
            answer += visible;
            if (options.on_token && !visible.empty()) {
                options.on_token(visible);
            }
        }

        // Feed the token back so the next sample sees it. Its position is the
        // end of the prompt plus however many we have already produced.
        context.decode({token}, prompt_end + produced);
    }
    // Whatever the filter still holds: a partial marker at end of stream was
    // never a marker, and an unterminated reasoning block's residue goes to the
    // thinking sink rather than into the answer.
    // Flushed in the same order they are written through. `gate.flush()` is
    // where the safety net lives: a span that opened like a tool call and
    // parsed as nothing comes back out as text here, rather than leaving a turn
    // with no answer, no tool, and no error -- the least debuggable outcome
    // there is, and exactly how an unrecognised grammar variant presents.
    std::vector<harness::ToolCall> calls;
    if (reply.has_value()) {
        if (std::string error; !reply->finish(error)) {
            notice(options, options_.model + "'s reply did not match its template's format (" +
                                chat->format + "): it is kept as text, and no tool call in it ran");
        }
        answer = reply->content();
        calls = reply->calls();
        for (harness::ToolCall& call : calls) {
            if (call.id.empty()) {
                call.id = make_call_id();
            }
        }
    } else {
        std::string tail = markup.write(gate.write(think.flush()));
        tail += markup.write(gate.flush());
        tail += markup.flush();
        if (!tail.empty()) {
            answer += tail;
            if (options.on_token) {
                options.on_token(tail);
            }
        }
        calls = gate.calls();
    }

    // Reaching the cap without an end-of-generation token is a truncated
    // answer, and a surface that shows it as complete is lying to the user.
    if (produced >= limit) {
        finish = harness::FinishReason::Length;
    }

    Generation result;
    result.text = std::move(answer);
    result.tokens = std::move(generated);
    result.tool_calls = std::move(calls);
    result.finish = finish;
    if (!result.tool_calls.empty()) {
        // A native call ends the turn on the model's side (`<|call|>` is
        // end-of-generation), so the honest finish reason is the tool call,
        // not the stop token that carried it.
        result.finish = harness::FinishReason::ToolCalls;
    }
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
    // Through the same renderer as a text turn, tools included: a model asked
    // about a picture can act on it (the Milestone O rule, 25b default).
    const RenderedRequest rendered = render_request(request);
    notice_if_toolless(request, rendered, options);
    prompt += rendered.text;

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

    const Generation generation =
        generate(context, prompt_end, request, options, rendered.chat.get());

    // The session's own KV is deliberately untouched: this turn ran on a
    // throwaway context, so `session_tokens_` still describes what the text
    // path cached and the next text turn can still reuse it.
    last_use_ = options_.clock();
    used_ = true;

    harness::ChatResponse response;
    response.message = harness::ChatMessage::assistant(generation.text);
    // The image path shares `generate()`, so it gets tool calls for free. A
    // model asked to look at a picture and then act on it is the ordinary case,
    // not an exotic one -- and forgetting this here is how a capability comes
    // out working on one surface and silently missing on another.
    response.message.tool_calls = generation.tool_calls;
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

    const RenderedRequest rendered = render_request(request);
    notice_if_toolless(request, rendered, options);
    // add_special: see llama_tokens::tokenize_prompt.
    const std::vector<std::int32_t> prompt = model_->tokenize(rendered.text, true);

    // A side request -- a background title summary, a one-off clerk call -- is
    // not a turn of this conversation. It runs on its own throwaway context so
    // the session's KV is untouched: Ommi's SideRequest lesson, where an async
    // titler's cache write clobbered the session it was titling.
    const bool side_request = request.transient.side_request;

    LlamaContext* context = nullptr;
    std::unique_ptr<LlamaContext> scratch;

    if (side_request) {
        scratch = model_->make_context(side_context_size(request, prompt.size()));
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
        // the new suffix decodes into the right positions. A model whose
        // memory cannot be cut there is cleared instead, and the whole prompt
        // decodes again from wherever the cache really ends.
        const std::int64_t kept = context->trim_to(static_cast<std::int64_t>(shared));

        const std::vector<std::int32_t> suffix{prompt.begin() + static_cast<std::ptrdiff_t>(kept),
                                               prompt.end()};
        decode_in_batches(*context, suffix, kept);
        session_tokens_ = prompt;
    }

    // Either path leaves the KV holding exactly positions [0, prompt.size()),
    // so generation continues from there regardless of how much was reused.
    const std::int64_t prompt_end = static_cast<std::int64_t>(prompt.size());

    // The sampling loop is shared with the image path: extracted when vision
    // landed, because the alternative was a second copy that would drift the
    // first time a stop condition changed.
    const Generation generation =
        generate(*context, prompt_end, request, options, rendered.chat.get());
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
    response.message.tool_calls = generation.tool_calls;
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
