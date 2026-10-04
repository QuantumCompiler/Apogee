#include "operations/run_settings.h"

namespace apogee::commands {

namespace {

const harness::BackendConfig* entry_for(const harness::Config& config,
                                        std::string_view backend_name) {
    return config.find_backend(backend_name);
}

}  // namespace

std::optional<double> resolve_temperature(const std::optional<double>& flag_value,
                                          const harness::Config& config,
                                          std::string_view backend_name) {
    if (flag_value.has_value()) {
        return flag_value;
    }
    const harness::BackendConfig* entry = entry_for(config, backend_name);
    return entry == nullptr ? std::nullopt : entry->temperature;
}

std::optional<std::int64_t> resolve_max_tokens(const std::optional<std::int64_t>& flag_value,
                                               const harness::Config& config,
                                               std::string_view backend_name) {
    if (flag_value.has_value()) {
        return flag_value;
    }
    const harness::BackendConfig* entry = entry_for(config, backend_name);
    return entry == nullptr ? std::nullopt : entry->max_tokens;
}

std::string resolve_system_prompt(const std::string& flag_value, const harness::Config& config,
                                  std::string_view backend_name) {
    if (!flag_value.empty()) {
        return flag_value;
    }
    const harness::BackendConfig* entry = entry_for(config, backend_name);
    return entry == nullptr ? std::string{} : entry->system_prompt;
}

}  // namespace apogee::commands
