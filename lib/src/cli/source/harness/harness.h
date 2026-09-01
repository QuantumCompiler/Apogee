#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "harness/behavior.h"
#include "harness/config.h"
#include "harness/provider.h"
#include "harness/types.h"

/// The registry and router: the harness proper.
///
/// Surfaces hold a Harness, not a provider. They name a model; the router
/// decides which backend serves it. That indirection is what makes
/// mid-conversation model switching a lookup rather than a reconstruction, and
/// what lets `apogee complete -m anything` work without the command knowing a
/// single backend type.
namespace apogee::harness {

/// Decides which provider serves a model name.
class ModelRouter {
public:
    ModelRouter() = default;
    virtual ~ModelRouter() = default;
    ModelRouter(const ModelRouter&) = delete;
    ModelRouter& operator=(const ModelRouter&) = delete;
    ModelRouter(ModelRouter&&) = delete;
    ModelRouter& operator=(ModelRouter&&) = delete;

    /// The provider for `model`, or throws NoAvailableBackendError.
    /// An empty name means "the configured default".
    [[nodiscard]] virtual LLMProvider& route(std::string_view model) const = 0;
};

/// Normalizes a routing key: lowercased, with `.` and `:` folded to `-`.
///
/// Exists because the same model gets written three ways across a config file
/// and a command line — `Qwen3.5`, `qwen3:5`, `qwen3-5` — and a user who typed
/// one should not get "no such backend" because the file spells it another.
[[nodiscard]] std::string normalize_route_key(std::string_view value);

/// The three-rung router, carrying Ommi's precedence exactly.
///
///   1. exact backend key         — `backends:` map key
///   2. a backend entry's `model:` field
///   3. the configured default    — `models.default`
///
/// Each rung tries the literal name first, then the normalized form, before
/// falling to the next. Rung order is the whole design: a backend KEY must beat
/// another entry's model field, or two entries pointing at the same model make
/// `-m <key>` ambiguous.
///
/// Ommi has a fourth rung — "if exactly one backend is registered, use it" —
/// deliberately not ported (2026-08-25). It papers over an unset
/// `models.default` in a way that stops working the moment a second backend is
/// added, which is precisely when a user has the least idea why routing
/// changed. A clear "set models.default" is the better failure.
class SimpleRouter final : public ModelRouter {
public:
    /// `providers` maps a backend key to its provider. Entries in `config`
    /// with no registered provider are skipped: a config may name a backend
    /// this process did not construct.
    SimpleRouter(const Config& config,
                 const std::map<std::string, std::shared_ptr<LLMProvider>>& providers);

    [[nodiscard]] LLMProvider& route(std::string_view model) const override;

private:
    [[nodiscard]] LLMProvider* lookup(std::string_view key) const;

    /// Both indexes are built once at construction: routing happens per turn,
    /// and rebuilding a map per turn to save a few hundred bytes is the wrong
    /// trade.
    std::map<std::string, LLMProvider*, std::less<>> by_key_;
    std::map<std::string, LLMProvider*, std::less<>> by_normalized_key_;
    std::map<std::string, LLMProvider*, std::less<>> by_model_;
    std::map<std::string, LLMProvider*, std::less<>> by_normalized_model_;
    std::string default_model_;
    /// Names of registered backends, for the error message. A routing failure
    /// that does not say what WOULD have worked just sends the user to the
    /// config file to guess.
    std::vector<std::string> known_names_;
};

/// Owns the providers and routes requests to them.
class Harness {
public:
    explicit Harness(Config config);
    ~Harness();

    Harness(const Harness&) = delete;
    Harness& operator=(const Harness&) = delete;
    Harness(Harness&&) noexcept;
    Harness& operator=(Harness&&) noexcept;

    /// Registers `provider` under `name`. Replaces any existing registration —
    /// a caller rebuilding one backend should not have to tear down the rest.
    void register_provider(std::string name, std::shared_ptr<LLMProvider> provider);

    /// Builds a SimpleRouter over the currently registered providers.
    /// Call after registering; re-call after registering more.
    void use_default_router();

    void set_router(std::unique_ptr<ModelRouter> router);

    [[nodiscard]] const Config& config() const noexcept {
        return config_;
    }

    /// Registered backend keys, sorted.
    [[nodiscard]] std::vector<std::string> provider_names() const;

    /// The provider registered under `name`.
    /// Throws ProviderNotRegisteredError when there is none.
    [[nodiscard]] LLMProvider& provider(std::string_view name) const;

    /// The provider that serves `model`. Throws NoAvailableBackendError.
    [[nodiscard]] LLMProvider& route(std::string_view model) const;

    /// `models.default` from config.
    [[nodiscard]] const std::string& default_model() const noexcept;

    // --- Request paths -----------------------------------------------------
    //
    // Thin: route, then delegate. They exist so a surface holds one object
    // rather than a provider plus a router, and so routing failures are
    // reported in one place.

    [[nodiscard]] ChatResponse chat(const ChatRequest& request,
                                    const CancellationToken& cancellation = {}) const;

    [[nodiscard]] ChatResponse stream_chat(const ChatRequest& request,
                                           const StreamOptions& options) const;

    [[nodiscard]] ChatResponse complete(const ChatRequest& request,
                                        const CancellationToken& cancellation = {}) const;

    /// Every model from every registered provider.
    ///
    /// A provider that fails to answer is SKIPPED, not fatal: one
    /// misconfigured backend must not stop `apogee models` from listing the
    /// others, which is exactly when a user needs the list most.
    [[nodiscard]] std::vector<ModelInfo> list_all_models(
        const CancellationToken& cancellation = {}) const;

    // --- Capability probes -------------------------------------------------
    //
    // The Harness performs the discovery so callers ask a plain typed question.
    // No `dynamic_cast` at a call site: an acceptance criterion of this item,
    // and what stops "can this embed?" from becoming a switch over backend
    // types the way it did in Ommi.

    /// Whether the backend serving `model` can produce embeddings.
    /// False for an unroutable model — an unknown backend cannot embed either.
    [[nodiscard]] bool can_embed(std::string_view model) const noexcept;

    /// The embedding interface for `model`, or nullptr when it cannot embed.
    /// Non-owning; valid as long as the provider stays registered.
    [[nodiscard]] EmbeddingCapable* embedder_for(std::string_view model) const noexcept;

    /// Whether the backend serving `model` puts tool calls in the text stream.
    [[nodiscard]] bool uses_in_text_tool_calls(std::string_view model) const noexcept;

    /// Exact prompt-token count for `request` on `model`, when the backend
    /// owns a tokenizer and can answer cheaply. `std::nullopt` means "use the
    /// estimate" -- an unroutable model, a backend with no tokenizer, or a
    /// provider that declined this particular request.
    [[nodiscard]] std::optional<std::int64_t> count_prompt_tokens(
        std::string_view model, const ChatRequest& request) const noexcept;

    /// Whether the backend serving `model` accepts image parts.
    ///
    /// **True for an unknown or unroutable model.** A provider that does not
    /// declare the capability is assumed capable: every cloud vendor accepts
    /// images, so pre-refusing an attachment on a backend we cannot ask would
    /// be the expensive direction of a wrong guess -- the provider itself will
    /// give a better error than we can invent.
    [[nodiscard]] bool accepts_images(std::string_view model) const noexcept;

    /// The behavior profile for `model`, or the permissive zero value.
    [[nodiscard]] ModelBehavior model_behavior_for(std::string_view model) const;

    /// Status for one backend, when it reports any.
    [[nodiscard]] std::optional<StatusEvent> model_status(std::string_view backend_name) const;

    /// The effective context window for `model`: the backend entry\'s
    /// `context_size` when set, else the compiled fallback table.
    /// 0 means unknown — never unlimited.
    [[nodiscard]] std::int64_t context_window_for_model(std::string_view model) const;

private:
    Config config_;
    std::map<std::string, std::shared_ptr<LLMProvider>> providers_;
    std::unique_ptr<ModelRouter> router_;
};

}  // namespace apogee::harness
