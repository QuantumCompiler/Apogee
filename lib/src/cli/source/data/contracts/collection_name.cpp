#include "contracts/collection_name.h"

#include <filesystem>

namespace apogee::harness {

bool name_leaves_folder(std::string_view name) {
    if (name.empty() || name == "..") {
        return true;
    }
    if (name.find_first_of(std::string_view{"/\\\0", 3}) != std::string_view::npos) {
        return true;
    }
    return std::filesystem::path{std::string{name}}.has_root_name();
}

std::string collection_read_error(std::string_view name) {
    if (name.empty()) {
        return "a collection needs a name";
    }
    if (name_leaves_folder(name)) {
        return "'" + std::string{name} + "' is not a plain collection name";
    }
    return {};
}

}  // namespace apogee::harness
