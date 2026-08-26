#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "backends/anthropic_wire.h"
#include "backends/http_client.h"
#include "harness/config.h"
#include "harness/provider.h"

/// The Anthropic Messages API backend — direct HTTPS, API-key path.
///
/// This is the API-billing path. The subscription path (driving the `claude`
/// CLI the user is already logged into) is a separate backend arriving after
/// v0.1.0; the two coexist and are chosen per config entry.
///
/// Everything Ommi rented from the `claude` CLI — the thinking stream, native
/// tool use, web search — is re-sourced here from native API features:
/// `thinking_delta` events instead of demuxing `<thinking>` out of text,
/// structured `tool_use` blocks instead of a text protocol, and Anthropic's
/// server-side `web_search` tool instead of a shelled-out search.
namespace apogee::backends {

class AnthropicProvider final : public harness::LLMProvider {
public:
    struct Options {
        std::string backend_name = "anthropic";
        std::string model = "claude-sonnet-5";
        std::string api_key;

        /// Overridable for tests and for gateway deployments.
        std::string base_url = "https://api.anthropic.com";
        std::string api_version = "2023-06-01";

        /// Anthropic requires max_tokens on every request; there is no
        /// "as many as you need". This is the default when the request does
        /// not carry one.
        std::int64_t max_tokens = 4096;

        /// Extended thinking budget. 0 leaves thinking off.
        std::int64_t thinking_budget_tokens = 0;

        /// Anthropic's server-side web_search tool.
        bool web_search = false;
        std::int64_t web_search_max_uses = 0;
    };

    /// `client` is injected so tests run against a scripted transport: no
    /// network, no key, no charges, and failure modes a live endpoint will not
    /// produce on demand.
    AnthropicProvider(Options options, std::unique_ptr<HttpClient> client);

    /// Builds one over a real libcurl transport.
    [[nodiscard]] static std::unique_ptr<AnthropicProvider> create(Options options);

    /// Builds one from a config entry, resolving the key and model.
    /// Throws harness::ProviderError when the entry has no usable API key.
    [[nodiscard]] static std::unique_ptr<AnthropicProvider> from_config(
        const std::string& backend_name, const harness::BackendConfig& config);

    [[nodiscard]] std::string_view backend_name() const noexcept override;

    [[nodiscard]] harness::ChatResponse chat(
        const harness::ChatRequest& request,
        const harness::CancellationToken& cancellation) override;

    [[nodiscard]] harness::ChatResponse stream_chat(const harness::ChatRequest& request,
                                                    const harness::StreamOptions& options) override;

    [[nodiscard]] std::vector<harness::ModelInfo> list_models(
        const harness::CancellationToken& cancellation) override;

    /// Exact input-token count for `request`, via /v1/messages/count_tokens.
    ///
    /// Exact rather than estimated: context warnings that fire at the wrong
    /// point are worse than none, and a tokenizer guess for a vendor model is
    /// always wrong by an unknown margin.
    [[nodiscard]] std::int64_t count_tokens(const harness::ChatRequest& request,
                                            const harness::CancellationToken& cancellation);

    /// Drops the replay cache. Call when a conversation restarts.
    void reset_conversation();

private:
    [[nodiscard]] HttpRequest build_http_request(const nlohmann::json& body,
                                                 std::string_view path) const;
    [[nodiscard]] anthropic::RequestOptions request_options(const harness::ChatRequest& request,
                                                            bool stream) const;
    [[noreturn]] void fail(long status, std::string_view body) const;

    Options options_;
    std::unique_ptr<HttpClient> client_;
    /// Raw assistant blocks kept so extended-thinking turns can be replayed on
    /// the next request. Provider-local; never serialized, never in the IR.
    anthropic::ThinkingCache thinking_cache_;
};

}  // namespace apogee::backends
