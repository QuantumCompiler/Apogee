#include "backends/factory.h"

#include <utility>

#include "backends/anthropic.h"
#include "backends/claude_cli.h"
#include "backends/codex_cli.h"
#include "backends/gemini_cli.h"
#include "backends/google.h"
#include "backends/llamacpp.h"
#include "backends/mock.h"
#include "backends/ollama_cli.h"
#include "backends/openai.h"
#include "harness/errors.h"

namespace apogee::backends {

std::size_t BuildResult::constructed_count() const noexcept {
    std::size_t count = 0;
    for (const BackendStatus& status : statuses) {
        if (status.constructed) {
            ++count;
        }
    }
    return count;
}

std::string BuildResult::skipped_summary() const {
    std::string summary;
    for (const BackendStatus& status : statuses) {
        if (status.constructed) {
            continue;
        }
        if (!summary.empty()) {
            summary += "; ";
        }
        summary += status.name + " (" + status.reason + ")";
    }
    return summary;
}

std::shared_ptr<harness::LLMProvider> make_provider(const std::string& name,
                                                    const harness::BackendConfig& config,
                                                    std::string& reason,
                                                    const BuildOptions& options) {
    switch (config.type) {
        case harness::BackendType::Anthropic: {
            try {
                std::unique_ptr<AnthropicProvider> provider =
                    AnthropicProvider::from_config(name, config, options.web_search);
                return provider;
            } catch (const harness::ProviderError& e) {
                reason = e.what();
                return nullptr;
            }
        }
        case harness::BackendType::Mock: {
            if (!config.embedding_model.empty()) {
                // A mock that embeds, so the vector path can be driven end to
                // end with nothing installed -- the mock's stated purpose. The
                // embedding_model becomes the space its vectors are recorded
                // in, so two mock embedders with different names mismatch
                // exactly as two real models would.
                auto provider = std::make_shared<MockEmbeddingProvider>(name);
                provider->set_model_name(config.embedding_model);
                return provider;
            }
            MockProvider::Options options;
            options.backend_name = name;
            if (!config.model.empty()) {
                options.model = config.model;
            }
            return std::make_shared<MockProvider>(std::move(options));
        }
        case harness::BackendType::OpenAI: {
            try {
                return OpenAIProvider::from_config(name, config, options.web_search);
            } catch (const harness::ProviderError& e) {
                reason = e.what();
                return nullptr;
            }
        }
        case harness::BackendType::Google: {
            try {
                return GoogleProvider::from_config(name, config, options.web_search);
            } catch (const harness::ProviderError& e) {
                reason = e.what();
                return nullptr;
            }
        }
        case harness::BackendType::ClaudeCli: {
            try {
                return ClaudeCliProvider::from_config(name, config);
            } catch (const harness::ProviderError& e) {
                reason = e.what();
                return nullptr;
            }
        }
        case harness::BackendType::CodexCli: {
            try {
                return CodexCliProvider::from_config(name, config);
            } catch (const harness::ProviderError& e) {
                reason = e.what();
                return nullptr;
            }
        }
        case harness::BackendType::GeminiCli: {
            try {
                return GeminiCliProvider::from_config(name, config);
            } catch (const harness::ProviderError& e) {
                reason = e.what();
                return nullptr;
            }
        }
        case harness::BackendType::OllamaCli: {
            try {
                return OllamaCliProvider::from_config(name, config);
            } catch (const harness::ProviderError& e) {
                reason = e.what();
                return nullptr;
            }
        }
        case harness::BackendType::LlamaCpp: {
            try {
                return LlamaCppProvider::from_config(name, config);
            } catch (const harness::ProviderError& e) {
                reason = e.what();
                return nullptr;
            }
        }
    }
    reason = "unknown backend type";
    return nullptr;
}

BuildResult build_providers(harness::Harness& harness, const BuildOptions& options) {
    BuildResult result;

    for (const auto& [name, config] : harness.config().backends) {
        BackendStatus status;
        status.name = name;

        std::string reason;
        std::shared_ptr<harness::LLMProvider> provider =
            make_provider(name, config, reason, options);
        if (provider == nullptr) {
            status.reason = reason.empty() ? "could not be constructed" : reason;
            result.statuses.push_back(std::move(status));
            continue;
        }

        harness.register_provider(name, std::move(provider));
        status.constructed = true;
        result.statuses.push_back(std::move(status));
    }

    harness.use_default_router();
    return result;
}

}  // namespace apogee::backends
