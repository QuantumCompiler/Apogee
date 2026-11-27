#include "support/printed_command.h"

#include <cstddef>

namespace apogee::testing {
namespace {

/// Where `needle` first starts in `text` at or after `from`, outside single
/// and double quotes as a POSIX shell reads them, or npos.
[[nodiscard]] std::size_t find_outside_quotes(const std::string& text, const std::string& needle,
                                              std::size_t from) {
    char quote = '\0';
    for (std::size_t at = from; at < text.size(); ++at) {
        const char c = text[at];
        if (quote == '\0' && text.compare(at, needle.size(), needle) == 0) {
            return at;
        }
        if (quote == '\0' && (c == '\'' || c == '"')) {
            quote = c;
        } else if (quote != '\'' && c == '\\' && at + 1 < text.size()) {
            ++at;  // escaped outside quotes, or inside double quotes: `'\''`
        } else if (c == quote) {
            quote = '\0';
        }
    }
    return std::string::npos;
}

}  // namespace

std::vector<std::string> remedy_doors(const std::string& remedy) {
    std::vector<std::string> doors;
    std::size_t start = 0;
    while (start <= remedy.size()) {
        const std::size_t end = find_outside_quotes(remedy, ", or ", start);
        std::string door =
            remedy.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (const std::size_t to = find_outside_quotes(door, " to ", 0); to != std::string::npos) {
            door.resize(to);
        }
        doors.push_back(door);
        if (end == std::string::npos) {
            break;
        }
        start = end + 5;
    }
    return doors;
}

}  // namespace apogee::testing
