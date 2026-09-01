#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "backends/http_client.h"
#include "backends/openai_wire.h"
#include "harness/config.h"
#include "harness/provider.h"

/// The OpenAI backend — Responses API, direct HTTPS, API-key path.
///
/// The subscription path (the `codex` CLI) is a separate backend in the vendor-
/// CLI family; both are selectable per config entry.
namespace apogee::backends {

class OpenAIProvider final : public harness::LLMProvider {
public:
    struct Options {
        std::string backend_name = "openai";
        std::string model = "gpt-5";
        std::string api_key;
        std::string base_url = "https://api.openai.com";
        std::int64_t max_tokens = 4096;
        /// Translated to `reasoning.effort` at the boundary.
        std::int64_t thinking_budget_tokens = 0;
        bool web_search = false;
    };

    OpenAIProvider(Options options, std::unique_ptr<HttpClient> client);

    [[nodiscard]] static std::unique_ptr<OpenAIProvider> from_config(
        const std::string& backend_name, const harness::BackendConfig& config,
        bool web_search = false);

    [[nodiscard]] std::string_view backend_name() const noexcept override;

    [[nodiscard]] harness::ChatResponse chat(
        const harness::ChatRequest& request,
        const harness::CancellationToken& cancellation) override;

    [[nodiscard]] harness::ChatResponse stream_chat(const harness::ChatRequest& request,
                                                    const harness::StreamOptions& options) override;

    [[nodiscard]] std::vector<harness::ModelInfo> list_models(
        const harness::CancellationToken& cancellation) override;

private:
    [[nodiscard]] HttpRequest build_http_request(const nlohmann::json& body) const;
    [[nodiscard]] openai::RequestOptions request_options(const harness::ChatRequest& request,
                                                         bool stream) const;
    [[noreturn]] void fail(long status, std::string_view body) const;

    Options options_;
    std::unique_ptr<HttpClient> client_;
};

}  // namespace apogee::backends
