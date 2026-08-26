#include "harness/paths.h"

#include <cstdlib>
#include <stdexcept>

#include "platform/platform.h"

namespace apogee::harness {

std::filesystem::path apogee_home() {
    if (const char* override_root = std::getenv(kHomeEnvVar);
        override_root != nullptr && *override_root != '\0') {
        return std::filesystem::path{override_root};
    }

    const std::optional<std::string> home = platform::home_directory();
    if (!home.has_value()) {
        throw std::runtime_error(
            "cannot determine your home directory; set the APOGEE_HOME "
            "environment variable to choose where Apogee keeps its files");
    }
    return std::filesystem::path{*home} / ".apogee";
}

std::filesystem::path config_dir() {
    return apogee_home() / "config";
}

std::filesystem::path default_config_path() {
    return config_dir() / "config.yaml";
}

std::filesystem::path resolve_config_path(const std::string& flag_value) {
    if (!flag_value.empty()) {
        return std::filesystem::path{flag_value};
    }
    return default_config_path();
}

}  // namespace apogee::harness
