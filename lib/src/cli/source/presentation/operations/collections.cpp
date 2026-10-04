#include "operations/collections.h"

#include <algorithm>
#include <system_error>

#include "contracts/layout.h"
#include "contracts/paths.h"

namespace apogee::commands {

std::filesystem::path collection_path(std::string_view name) {
    return harness::embeddings_dir() / (std::string{name} + ".db");
}

std::vector<std::string> collection_names() {
    std::vector<std::string> names;
    std::error_code code;
    for (const auto& entry : std::filesystem::directory_iterator(harness::embeddings_dir(), code)) {
        if (code) {
            break;
        }
        if (entry.is_regular_file(code) && entry.path().extension() == ".db") {
            names.push_back(entry.path().stem().string());
        }
    }
    std::ranges::sort(names);
    return names;
}

}  // namespace apogee::commands
