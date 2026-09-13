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

/// It also embeds. OpenAI serves chat and embeddings from different models
/// behind one key, so one entry does both: `model` answers, `embedding_model`
/// vectorises. That is the per-provider capability the harness discovers; a
/// type allowlist would have had to be edited to learn this, which is the
/// Ommi gate that did not transfer.
class OpenAIProvider final : public harness::LLMProvider, public harness::EmbeddingCapable {
public:
    struct Options {
        std::string backend_name = "openai";
        std::string model = "gpt-5";
        /// Empty means the vendor's documented default.
        std::string embedding_model;
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

    // --- EmbeddingCapable ---------------------------------------------------

    /// Batches at the documented maximum, retries like the chat path, and
    /// never lets the key into an error message.
    [[nodiscard]] std::vector<std::vector<float>> embed(
        const std::vector<std::string>& inputs,
        const harness::CancellationToken& cancellation) override;

    /// The model's documented width, or 0 for a model this build does not
    /// know -- which becomes the real width after the first call.
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
    [[nodiscard]] HttpRequest build_http_request(const nlohmann::json& body) const;
    [[nodiscard]] HttpRequest build_embed_request(const nlohmann::json& body) const;
    [[nodiscard]] openai::RequestOptions request_options(const harness::ChatRequest& request,
                                                         bool stream) const;
    [[noreturn]] void fail(long status, std::string_view body) const;

    Options options_;
    std::unique_ptr<HttpClient> client_;
    /// Learned from the first vector when the model's width was not known.
    std::size_t observed_dimensions_ = 0;
};

}  // namespace apogee::backends
