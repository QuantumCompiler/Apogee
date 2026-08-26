#include "harness/harness.h"

#include <algorithm>
#include <utility>

#include "harness/context_windows.h"
#include "harness/errors.h"

namespace apogee::harness {
namespace {

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
    providers_[std::move(name)] = std::move(provider);
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

ChatResponse Harness::chat(const ChatRequest& request,
                           const CancellationToken& cancellation) const {
    return route(request.model).chat(request, cancellation);
}

ChatResponse Harness::stream_chat(const ChatRequest& request, const StreamOptions& options) const {
    return route(request.model).stream_chat(request, options);
}

ChatResponse Harness::complete(const ChatRequest& request,
                               const CancellationToken& cancellation) const {
    return route(request.model).complete(request, cancellation);
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

std::int64_t Harness::context_window_for_model(std::string_view model) const {
    const std::string_view name =
        model.empty() ? std::string_view{config_.models.default_backend} : model;

    // An explicit context_size on the backend entry always wins over the
    // table: the user knows something we do not, such as a model served with a
    // deliberately shortened window.
    if (const BackendConfig* backend = config_.find_backend(name); backend != nullptr) {
        const std::int64_t configured = backend->context_size.value_or(0);
        // Fall back on the entry's model name, not the routing key -- the table
        // is keyed by model family, and the backend key is often a nickname.
        const std::string_view model_name =
            backend->model.empty() ? name : std::string_view{backend->model};
        return resolve_context_window(configured, model_name);
    }
    return context_window_for(name);
}

}  // namespace apogee::harness
