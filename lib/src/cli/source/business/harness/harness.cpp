#include "harness/harness.h"

#include <algorithm>
#include <exception>
#include <optional>
#include <utility>

#include "contracts/errors.h"
#include "contracts/utf8.h"
#include "harness/context_windows.h"
#include "harness/roles.h"

namespace apogee::harness {
namespace {

/// Makes `text` UTF-8 in place. Whether it had to.
bool repair(std::string& text) {
    if (is_valid_utf8(text)) {
        return false;
    }
    text = valid_utf8(text);
    return true;
}

/// Whether everything `message` says is UTF-8 already.
[[nodiscard]] bool is_text(const ChatMessage& message) {
    const std::vector<ContentPart>& parts = message.content.parts();
    const bool content =
        parts.empty() ? is_valid_utf8(message.content.plain_text())
                      : std::ranges::all_of(parts, [](const ContentPart& part) {
                            return part.kind != ContentPart::Kind::Text || is_valid_utf8(part.text);
                        });
    return content && is_valid_utf8(message.tool_call_id) && is_valid_utf8(message.name) &&
           std::ranges::all_of(message.tool_calls, [](const ToolCall& call) {
               return is_valid_utf8(call.id) && is_valid_utf8(call.name) &&
                      is_valid_utf8(call.arguments);
           });
}

/// Makes everything `message` says UTF-8: its text, each text part of a
/// multi-part one, its tool calls. Nothing is rebuilt that did not need
/// mending, so a well-formed message is left exactly as it was.
void mend(ChatMessage& message) {
    if (message.content.parts().empty()) {
        if (std::string text = message.content.plain_text(); repair(text)) {
            message.content = std::move(text);
        }
    } else {
        std::vector<ContentPart> parts = message.content.parts();
        bool mended = false;
        for (ContentPart& part : parts) {
            if (part.kind == ContentPart::Kind::Text) {
                mended = repair(part.text) || mended;
            }
        }
        if (mended) {
            message.content = MessageContent::from_parts(std::move(parts));
        }
    }
    (void)repair(message.tool_call_id);
    (void)repair(message.name);
    for (ToolCall& call : message.tool_calls) {
        (void)repair(call.id);
        (void)repair(call.name);
        (void)repair(call.arguments);
    }
}

/// `request` mended, when any of its messages holds anything that is not
/// UTF-8; nothing -- and nothing copied -- in the ordinary case, when every
/// one already is. Every backend serializes a request with a strict dump, and
/// its text comes from everywhere: the user, a file, an attachment,
/// retrieved excerpts.
[[nodiscard]] std::optional<ChatRequest> mended(const ChatRequest& request) {
    if (std::ranges::all_of(request.messages, is_text)) {
        return std::nullopt;
    }
    ChatRequest copy = request;
    for (ChatMessage& message : copy.messages) {
        mend(message);
    }
    return copy;
}

/// `response` with everything the model wrote in it UTF-8.
[[nodiscard]] ChatResponse as_text(ChatResponse response) {
    mend(response.message);
    return response;
}

std::string join_names(const std::vector<std::string>& names) {
    std::string out;
    for (const std::string& name : names) {
        if (!out.empty()) {
            out += ", ";
        }
        out += name;
    }
    return out;
}

/// Inserts without overwriting: the FIRST registration of a key wins.
///
/// Matters for the model index. Two entries may declare the same `model:`, and
/// silently letting the later one win makes routing depend on map iteration
/// order — a bug that reproduces on one machine and not another.
void index_first_wins(std::map<std::string, LLMProvider*, std::less<>>& index,
                      const std::string& key, LLMProvider* provider) {
    if (key.empty()) {
        return;
    }
    index.emplace(key, provider);
}

}  // namespace

std::string normalize_route_key(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (const char c : value) {
        if (c == '.' || c == ':') {
            out.push_back('-');
            continue;
        }
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

// ---------------------------------------------------------------------------
// SimpleRouter
// ---------------------------------------------------------------------------

SimpleRouter::SimpleRouter(const Config& config,
                           const std::map<std::string, std::shared_ptr<LLMProvider>>& providers)
    : default_model_{config.models.default_backend} {
    for (const auto& [backend_name, backend_config] : config.backends) {
        const auto registered = providers.find(backend_name);
        if (registered == providers.end() || registered->second == nullptr) {
            // A config may name backends this process did not construct.
            continue;
        }
        LLMProvider* provider = registered->second.get();
        known_names_.push_back(backend_name);

        index_first_wins(by_key_, backend_name, provider);
        index_first_wins(by_normalized_key_, normalize_route_key(backend_name), provider);
        index_first_wins(by_model_, backend_config.model, provider);
        index_first_wins(by_normalized_model_, normalize_route_key(backend_config.model), provider);
    }

    // Providers registered without a matching config entry are still routable
    // by their key. Tests register a mock with no config entry, and refusing
    // to route it would make every downstream item's tests need a config file.
    for (const auto& [name, provider] : providers) {
        if (provider == nullptr || by_key_.contains(name)) {
            continue;
        }
        known_names_.push_back(name);
        index_first_wins(by_key_, name, provider.get());
        index_first_wins(by_normalized_key_, normalize_route_key(name), provider.get());
    }

    std::ranges::sort(known_names_);
    known_names_.erase(std::ranges::unique(known_names_).begin(), known_names_.end());
}

LLMProvider* SimpleRouter::lookup(std::string_view key) const {
    if (key.empty()) {
        return nullptr;
    }
    // Rung 1: the backend key, literal then normalized.
    if (const auto it = by_key_.find(key); it != by_key_.end()) {
        return it->second;
    }
    if (const auto it = by_normalized_key_.find(normalize_route_key(key));
        it != by_normalized_key_.end()) {
        return it->second;
    }
    // Rung 2: a backend entry's `model:` field, literal then normalized.
    if (const auto it = by_model_.find(key); it != by_model_.end()) {
        return it->second;
    }
    if (const auto it = by_normalized_model_.find(normalize_route_key(key));
        it != by_normalized_model_.end()) {
        return it->second;
    }
    return nullptr;
}

LLMProvider& SimpleRouter::route(std::string_view model) const {
    if (!model.empty()) {
        if (LLMProvider* found = lookup(model); found != nullptr) {
            return *found;
        }
    }

    // Rung 3: the configured default.
    if (LLMProvider* found = lookup(default_model_); found != nullptr) {
        return *found;
    }

    const std::string requested{model.empty() ? std::string_view{"<default>"} : model};
    std::string message = "no backend serves '" + requested + "'";
    if (known_names_.empty()) {
        message += " -- no backends are configured; add one with 'apogee config add-backend'";
    } else {
        message += " (configured backends: " + join_names(known_names_) + ")";
        if (default_model_.empty()) {
            message += "; no models.default is set -- 'apogee config set-default <name>'";
        }
    }
    throw NoAvailableBackendError(requested, message);
}

// ---------------------------------------------------------------------------
// Harness
// ---------------------------------------------------------------------------

Harness::Harness(Config config) : config_{std::move(config)} {}

Harness::~Harness() = default;
Harness::Harness(Harness&&) noexcept = default;
Harness& Harness::operator=(Harness&&) noexcept = default;

void Harness::register_provider(std::string name, std::shared_ptr<LLMProvider> provider) {
    if (provider != nullptr) {
        // A rebuilt backend keeps the session's hold and its listener (27e).
        attach_residency(name, *provider);
    }
    providers_[std::move(name)] = std::move(provider);
}

void Harness::attach_residency(const std::string& name, LLMProvider& provider) const {
    if (auto* holding = dynamic_cast<ResidencyHolding*>(&provider); holding != nullptr) {
        holding->hold_resident(held_.contains(name));
    }
    if (auto* reporting = dynamic_cast<StatusReporting*>(&provider); reporting != nullptr) {
        if (load_listener_) {
            // The name shared, not copied: a listener is copied wherever a
            // provider keeps it.
            reporting->set_load_listener(
                [listener = load_listener_, backend = std::make_shared<const std::string>(name)](
                    const StatusEvent& event) { listener(*backend, event); });
        } else {
            reporting->set_load_listener({});
        }
    }
}

void Harness::use_default_router() {
    router_ = std::make_unique<SimpleRouter>(config_, providers_);
}

void Harness::set_router(std::unique_ptr<ModelRouter> router) {
    router_ = std::move(router);
}

std::vector<std::string> Harness::provider_names() const {
    std::vector<std::string> names;
    names.reserve(providers_.size());
    for (const auto& [name, unused] : providers_) {
        names.push_back(name);
    }
    return names;
}

LLMProvider& Harness::provider(std::string_view name) const {
    const auto it = providers_.find(std::string{name});
    if (it == providers_.end() || it->second == nullptr) {
        throw ProviderNotRegisteredError(std::string{name});
    }
    return *it->second;
}

LLMProvider& Harness::route(std::string_view model) const {
    if (router_ == nullptr) {
        throw NoAvailableBackendError(
            std::string{model},
            "the harness has no router -- call use_default_router() after registering providers");
    }
    return router_->route(model);
}

const std::string& Harness::default_model() const noexcept {
    return config_.models.default_backend;
}

void Harness::set_active_suite(std::string suite) {
    config_.models.default_suite = std::move(suite);
    if (holding_suite_) {
        hold_suite_members();
    }
}

void Harness::observe_turns(TurnObserver observer) {
    turn_observer_ = std::move(observer);
}

void Harness::heard(const LLMProvider& provider) const noexcept {
    if (!turn_observer_) {
        return;
    }
    try {
        for (const auto& [name, registered] : providers_) {
            if (registered.get() == &provider) {
                turn_observer_(name);
                return;
            }
        }
    } catch (...) {
        // An observer's failure costs its record, never the turn.
    }
}

ChatResponse Harness::chat(const ChatRequest& request,
                           const CancellationToken& cancellation) const {
    const std::optional<ChatRequest> sent = mended(request);
    LLMProvider& provider = route(request.model);
    ChatResponse response =
        as_text(provider.chat(sent.has_value() ? *sent : request, cancellation));
    heard(provider);
    return response;
}

ChatResponse Harness::stream_chat(const ChatRequest& request, const StreamOptions& options) const {
    // One stream each for the answer and the reasoning, so each sink is
    // handed whole characters. A sink the caller left unset stays unset: a
    // provider may stream only when asked to.
    Utf8Stream answer;
    Utf8Stream thinking;
    const auto say = [](const auto& sink, const std::string& text) {
        if (!text.empty()) {
            sink(text);
        }
    };
    StreamOptions whole = options;
    if (options.on_thinking) {
        whole.on_thinking = [&](std::string_view piece) {
            say(options.on_thinking, thinking.feed(piece));
        };
    }
    if (options.on_token) {
        whole.on_token = [&](std::string_view piece) {
            // The answer begun, the reasoning is over: its unfinished
            // character is said now, so a surface still shows reasoning
            // before answer. An empty piece begins nothing.
            if (!piece.empty() && options.on_thinking) {
                say(options.on_thinking, thinking.flush());
            }
            say(options.on_token, answer.feed(piece));
        };
    }
    const std::optional<ChatRequest> sent = mended(request);
    LLMProvider& provider = route(request.model);
    ChatResponse response = provider.stream_chat(sent.has_value() ? *sent : request, whole);
    // A stream that ended inside a character ends in U+FFFD, said as its
    // last piece -- the same text the response and history keep.
    if (options.on_thinking) {
        say(options.on_thinking, thinking.flush());
    }
    if (options.on_token) {
        say(options.on_token, answer.flush());
    }
    heard(provider);
    return as_text(std::move(response));
}

ChatResponse Harness::complete(const ChatRequest& request,
                               const CancellationToken& cancellation) const {
    const std::optional<ChatRequest> sent = mended(request);
    LLMProvider& provider = route(request.model);
    ChatResponse response =
        as_text(provider.complete(sent.has_value() ? *sent : request, cancellation));
    heard(provider);
    return response;
}

std::vector<ModelInfo> Harness::list_all_models(const CancellationToken& cancellation) const {
    std::vector<ModelInfo> all;
    for (const auto& [name, provider] : providers_) {
        if (provider == nullptr) {
            continue;
        }
        try {
            std::vector<ModelInfo> models = provider->list_models(cancellation);
            all.insert(all.end(), models.begin(), models.end());
        } catch (const HarnessError&) {
            // One unreachable backend must not empty the list. A user running
            // `apogee models` with a bad API key still needs to see the rest.
            continue;
        }
    }
    return all;
}

bool Harness::can_embed(std::string_view model) const noexcept {
    return embedder_for(model) != nullptr;
}

EmbeddingCapable* Harness::embedder_for(std::string_view model) const noexcept {
    try {
        // The one place a capability cast lives. Callers ask can_embed() /
        // embedder_for() and never learn that a cast was involved.
        return dynamic_cast<EmbeddingCapable*>(&route(model));
    } catch (const HarnessError&) {
        return nullptr;
    }
}

bool Harness::uses_in_text_tool_calls(std::string_view model) const noexcept {
    try {
        const auto* caller = dynamic_cast<const InTextToolCalling*>(&route(model));
        return caller != nullptr && caller->uses_in_text_tool_calls();
    } catch (const HarnessError&) {
        return false;
    }
}

std::optional<std::int64_t> Harness::count_prompt_tokens(
    std::string_view model, const ChatRequest& request) const noexcept {
    try {
        auto* counter = dynamic_cast<TokenCounting*>(&route(model));
        if (counter == nullptr) {
            return std::nullopt;
        }
        const std::int64_t tokens = counter->count_prompt_tokens(request);
        // A provider signals "not right now" with a negative count rather than
        // by throwing: an unloaded model is an ordinary state, not an error,
        // and the caller's fallback is an estimate either way.
        if (tokens < 0) {
            return std::nullopt;
        }
        return tokens;
    } catch (const HarnessError&) {
        return std::nullopt;
    } catch (const std::exception&) {
        // Counting is a measurement, never the point of the call. A tokenizer
        // that throws must not take down the turn it was measuring.
        return std::nullopt;
    }
}

bool Harness::generation_is_metered(std::string_view model) const noexcept {
    try {
        return route(model).generation_is_metered();
    } catch (const HarnessError&) {
        return true;
    }
}

bool Harness::accepts_images(std::string_view model) const noexcept {
    try {
        const auto* vision = dynamic_cast<const VisionCapable*>(&route(model));
        // Not declaring the capability means "yes" -- see the header.
        return vision == nullptr || vision->accepts_images();
    } catch (const HarnessError&) {
        return true;
    }
}

void Harness::resume_conversation(std::string_view model,
                                  std::string_view conversation_id) const noexcept {
    try {
        if (auto* caching = dynamic_cast<ConversationCaching*>(&route(model)); caching != nullptr) {
            caching->resume_conversation(conversation_id);
        }
    } catch (const std::exception&) {
        // A conversation not restored is read again: never worth a failure.
        return;
    }
}

void Harness::save_conversation(std::string_view model, std::string_view conversation_id,
                                const StatusSink& on_status) const noexcept {
    try {
        if (auto* caching = dynamic_cast<ConversationCaching*>(&route(model)); caching != nullptr) {
            caching->save_conversation(conversation_id, on_status);
        }
    } catch (const std::exception&) {
        // The same: a state not saved is read again next time.
        return;
    }
}

bool Harness::accepts_audio(std::string_view model) const noexcept {
    try {
        const auto* audio = dynamic_cast<const AudioCapable*>(&route(model));
        // Not declaring it means "no" -- see AudioCapable.
        return audio != nullptr && audio->accepts_audio();
    } catch (const HarnessError&) {
        return false;
    }
}

std::string_view to_string(Medium medium) noexcept {
    switch (medium) {
        case Medium::Image:
            return "an image";
        case Medium::Audio:
            return "audio";
        case Medium::Video:
            return "a video";
    }
    return "media";
}

bool Harness::accepts_video(std::string_view model) const noexcept {
    try {
        const auto* video = dynamic_cast<const VideoCapable*>(&route(model));
        // Not declaring it means "no" -- see VideoCapable.
        return video != nullptr && video->accepts_video();
    } catch (const HarnessError&) {
        return false;
    }
}

bool Harness::can_read(std::string_view model, Medium medium) const noexcept {
    switch (medium) {
        case Medium::Image:
            return accepts_images(model);
        case Medium::Audio:
            return accepts_audio(model);
        case Medium::Video:
            return accepts_video(model);
    }
    return false;
}

int Harness::audio_sample_rate(std::string_view model) const noexcept {
    try {
        const auto* audio = dynamic_cast<const AudioCapable*>(&route(model));
        return audio != nullptr && audio->accepts_audio() ? audio->audio_sample_rate() : 0;
    } catch (const HarnessError&) {
        return 0;
    }
}

ModelBehavior Harness::model_behavior_for(std::string_view model) const {
    try {
        const auto* reporter = dynamic_cast<const ModelBehaviorReporting*>(&route(model));
        // The zero value means unknown, and unknown means permissive. Returning
        // it for an unroutable model is deliberate: a caller asking about
        // behavior should not have to handle a routing failure as well.
        return reporter == nullptr ? ModelBehavior{} : reporter->model_behavior();
    } catch (const HarnessError&) {
        return ModelBehavior{};
    }
}

std::optional<StatusEvent> Harness::model_status(std::string_view backend_name) const {
    const auto it = providers_.find(std::string{backend_name});
    if (it == providers_.end() || it->second == nullptr) {
        return std::nullopt;
    }
    const auto* reporter = dynamic_cast<const StatusReporting*>(it->second.get());
    if (reporter == nullptr) {
        return std::nullopt;
    }
    return reporter->model_status();
}

bool Harness::preload_model(std::string_view backend_name, const StatusSink& on_status) const {
    const auto it = providers_.find(std::string{backend_name});
    if (it == providers_.end() || it->second == nullptr) {
        return false;
    }
    auto* reporter = dynamic_cast<StatusReporting*>(it->second.get());
    if (reporter == nullptr) {
        return false;
    }
    reporter->preload(on_status);
    return true;
}

void Harness::hold_in_use(const std::vector<std::string>& backends) {
    held_.clear();
    for (const std::string& name : backends) {
        // A provider is registered under the config's key, which a member
        // names as backend names are matched: case aside.
        const auto entry = config_.backends.find(name);
        held_.insert(entry != config_.backends.end() ? entry->first : name);
    }
    for (const auto& [name, provider] : providers_) {
        if (auto* holding = dynamic_cast<ResidencyHolding*>(provider.get()); holding != nullptr) {
            holding->hold_resident(held_.contains(name));
        }
    }
}

std::vector<std::string> Harness::held() const {
    return {held_.begin(), held_.end()};
}

void Harness::hold_active_suite(bool on) {
    if (!on) {
        release_holds();
        return;
    }
    holding_suite_ = true;
    hold_suite_members();
}

void Harness::release_holds() noexcept {
    holding_suite_ = false;
    held_.clear();
    for (const auto& [name, provider] : providers_) {
        if (auto* holding = dynamic_cast<ResidencyHolding*>(provider.get()); holding != nullptr) {
            holding->hold_resident(false);
        }
    }
}

void Harness::hold_suite_members() {
    std::vector<std::string> backends;
    if (const SuiteConfig* suite = active_suite(config_); suite != nullptr) {
        for (const SuiteBackend& member : suite_backends(*suite)) {
            backends.push_back(member.backend);
        }
    }
    hold_in_use(backends);
}

void Harness::listen_for_loads(LoadListener listener) {
    load_listener_ = std::move(listener);
    for (const auto& [name, provider] : providers_) {
        if (provider != nullptr) {
            attach_residency(name, *provider);
        }
    }
}

std::optional<bool> Harness::resident(std::string_view backend) const {
    const auto it = providers_.find(std::string{backend});
    if (it == providers_.end() || it->second == nullptr) {
        return std::nullopt;
    }
    // Only a model this process holds has a residency to report: a vendor
    // CLI says "ready" with nothing of its own in memory.
    if (dynamic_cast<const ResidencyHolding*>(it->second.get()) == nullptr) {
        return std::nullopt;
    }
    const auto* reporter = dynamic_cast<const StatusReporting*>(it->second.get());
    if (reporter == nullptr) {
        return std::nullopt;
    }
    return reporter->model_status().type == StatusEvent::Type::ModelReady;
}

WarmResult Harness::warm(const std::vector<std::string>& backends,
                         const WarmProgress& progress) const {
    WarmResult result;
    std::vector<std::string> to_load;
    for (const std::string& name : backends) {
        const std::optional<bool> loaded = resident(name);
        if (!loaded.has_value()) {
            continue;  // nothing of its own to load: never a request to warm it
        }
        if (*loaded) {
            result.resident.push_back(name);
        } else {
            to_load.push_back(name);
        }
    }
    for (std::size_t i = 0; i < to_load.size(); ++i) {
        const std::string& name = to_load[i];
        if (progress) {
            progress(name, i + 1, to_load.size());
        }
        try {
            (void)preload_model(name, {});
            result.loaded.push_back(name);
        } catch (const std::exception& e) {
            result.failed.emplace_back(name, e.what());
        }
    }
    return result;
}

std::int64_t Harness::context_window_for_model(std::string_view model) const {
    const std::string resolved = resolve_chat_backend(config_, model);
    const std::string_view name{resolved};
    const BackendConfig* backend = config_.find_backend(name);

    // An explicit context_size on the backend entry always wins: the user
    // knows something we do not, such as a model served with a deliberately
    // shortened window. The active suite's pin is that entry's, while the
    // suite is active (27d) -- the window the factory built the backend at.
    const std::int64_t configured =
        backend != nullptr ? backend_as_run(config_, name).context_size.value_or(0) : 0;
    if (configured > 0) {
        return configured;
    }

    // Then a backend that sizes its own window: a local model's is fitted to
    // the model and the machine at load (26a), which no table of names knows.
    if (const auto it = providers_.find(resolved);
        it != providers_.end() && it->second != nullptr) {
        if (const auto* reporter = dynamic_cast<const ContextWindowReporting*>(it->second.get());
            reporter != nullptr) {
            try {
                if (const std::int64_t window = reporter->context_window(); window > 0) {
                    return window;
                }
            } catch (const std::exception&) {
                // Unknown, as the table below says for a model it lacks.
            }
        }
    }

    // Fall back on the entry's model name, not the routing key -- the table
    // is keyed by model family, and the backend key is often a nickname.
    const std::string_view model_name =
        backend != nullptr && !backend->model.empty() ? std::string_view{backend->model} : name;
    return resolve_context_window(configured, model_name);
}

// ---------------------------------------------------------------------------
// SessionHold
// ---------------------------------------------------------------------------

SessionHold::SessionHold(Harness& harness) : harness_{harness} {
    harness_.hold_active_suite(true);
}

SessionHold::~SessionHold() {
    harness_.release_holds();
}

}  // namespace apogee::harness
