#include "backends/factory.h"

#include <utility>

#include "backends/anthropic.h"
#include "backends/mock.h"
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
                                                    std::string& reason) {
    switch (config.type) {
        case harness::BackendType::Anthropic: {
            try {
                return AnthropicProvider::from_config(name, config);
            } catch (const harness::ProviderError& e) {
                reason = e.what();
                return nullptr;
            }
        }
        case harness::BackendType::Mock: {
            MockProvider::Options options;
            options.backend_name = name;
            if (!config.model.empty()) {
                options.model = config.model;
            }
            return std::make_shared<MockProvider>(std::move(options));
        }
        case harness::BackendType::OpenAI:
            reason = "the OpenAI backend has not landed yet";
            return nullptr;
        case harness::BackendType::Google:
            reason = "the Google backend has not landed yet";
            return nullptr;
        case harness::BackendType::LlamaCpp:
            reason = "the local llama.cpp backend has not landed yet";
            return nullptr;
    }
    reason = "unknown backend type";
    return nullptr;
}

BuildResult build_providers(harness::Harness& harness) {
    BuildResult result;

    for (const auto& [name, config] : harness.config().backends) {
        BackendStatus status;
        status.name = name;

        std::string reason;
        std::shared_ptr<harness::LLMProvider> provider = make_provider(name, config, reason);
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
