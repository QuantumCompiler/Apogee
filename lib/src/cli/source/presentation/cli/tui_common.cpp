#include "cli/tui_common.h"

#include <nlohmann/json.hpp>

#include "contracts/paths.h"

namespace apogee::commands {

std::filesystem::path config_file(const RootContext& context) {
    return harness::resolve_config_path(context.config_path);
}

std::string field(const nlohmann::json& object, const char* key) {
    const auto found = object.find(key);
    if (found == object.end() || found->is_null()) {
        return {};
    }
    return found->is_string() ? found->get<std::string>() : found->dump();
}

std::vector<std::string> lines_of(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t end = text.find('\n', start);
        lines.push_back(
            text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return lines;
}

}  // namespace apogee::commands
