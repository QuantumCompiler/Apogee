#include "modelstore/hf_ref.h"

namespace apogee::models {

std::optional<HfRef> parse_hf_ref(std::string_view ref) {
    HfRef parsed;

    // The file, if named: everything after the last ':' that follows the '/'.
    if (const std::size_t colon = ref.rfind(':'); colon != std::string_view::npos) {
        const std::size_t slash = ref.find('/');
        if (slash != std::string_view::npos && colon > slash) {
            parsed.file = std::string{ref.substr(colon + 1)};
            ref = ref.substr(0, colon);
        }
    }

    // The revision, if named.
    if (const std::size_t at = ref.rfind('@'); at != std::string_view::npos) {
        parsed.revision = std::string{ref.substr(at + 1)};
        ref = ref.substr(0, at);
    }

    const std::size_t slash = ref.find('/');
    if (slash == std::string_view::npos || slash == 0 || slash + 1 >= ref.size()) {
        return std::nullopt;
    }
    parsed.owner = std::string{ref.substr(0, slash)};
    parsed.repo = std::string{ref.substr(slash + 1)};
    if (parsed.repo.find('/') != std::string::npos) {
        return std::nullopt;
    }
    return parsed;
}

std::string repo_directory_name(const HfRef& ref) {
    std::string name = ref.owner + "--" + ref.repo;
    for (char& c : name) {
        if (c == '/' || c == ':' || c == '@' || c == ' ' || c == '\\') {
            c = '-';
        }
    }
    return name;
}

}  // namespace apogee::models
