#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "backends/google_wire.h"
#include "backends/http_client.h"
#include "harness/config.h"
#include "harness/provider.h"

/// The Google Gemini backend — `generateContent`, direct HTTPS, API-key path.
///
/// API key only for v0.1.0 (decided 2026-08-26); Vertex-style credentials are a
/// separate auth story nothing here needs. The subscription path (the `gemini`
/// CLI) is a separate backend in the vendor-CLI family.
namespace apogee::backends {

/// It also embeds, through `batchEmbedContents` -- the same key, a different
/// model. See `openai.h` for why this is a per-provider capability rather than
/// an entry in a type list.
class GoogleProvider final : public harness::LLMProvider, public harness::EmbeddingCapable {
public:
    struct Options {
        std::string backend_name = "google";
        std::string model = "gemini-2.5-pro";
        /// Empty means the vendor's documented default.
        std::string embedding_model;
        std::string api_key;
        std::string base_url = "https://generativelanguage.googleapis.com";
        std::string api_version = "v1beta";
        std::int64_t max_tokens = 4096;
        /// Passes through to `thinkingConfig.thinkingBudget` -- Gemini takes a
        /// real token budget, unlike OpenAI's effort band.
        std::int64_t thinking_budget_tokens = 0;
        bool web_search = false;
    };

    GoogleProvider(Options options, std::unique_ptr<HttpClient> client);

    [[nodiscard]] static std::unique_ptr<GoogleProvider> from_config(
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

    // --- EmbeddingCapable ---------------------------------------------------

    [[nodiscard]] std::vector<std::vector<float>> embed(
        const std::vector<std::string>& inputs,
        const harness::CancellationToken& cancellation) override;

    [[nodiscard]] std::size_t embedding_dimensions() const noexcept override;

    /// The model `embed` will use.
    [[nodiscard]] std::string_view embedding_model() const noexcept;

    [[nodiscard]] std::string embedding_model_name() const override {
        return std::string{embedding_model()};
    }

    /// Every call is billed. Said explicitly rather than inherited.
    [[nodiscard]] bool embedding_is_metered() const noexcept override {
        return true;
    }

private:
    [[nodiscard]] HttpRequest build_http_request(const nlohmann::json& body, bool stream) const;
    [[nodiscard]] HttpRequest build_embed_request(const nlohmann::json& body) const;
    [[nodiscard]] google::RequestOptions request_options(const harness::ChatRequest& request) const;
    [[noreturn]] void fail(long status, std::string_view body) const;

    Options options_;
    std::unique_ptr<HttpClient> client_;
    std::size_t observed_dimensions_ = 0;
};

}  // namespace apogee::backends
