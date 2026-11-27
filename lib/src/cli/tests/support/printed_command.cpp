#include "support/printed_command.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <system_error>

#include "platform/child_process.h"

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

std::vector<std::string> door_steps(const std::string& door) {
    std::vector<std::string> steps;
    std::size_t start = 0;
    for (std::size_t end = find_outside_quotes(door, " && ", start); end != std::string::npos;
         end = find_outside_quotes(door, " && ", start)) {
        steps.push_back(door.substr(start, end - start));
        start = end + 4;
    }
    steps.push_back(door.substr(start));
    return steps;
}

std::optional<std::vector<std::string>> sh_words(const std::string& command) {
    std::error_code code;
    if (!platform::supports_child_processes() || !std::filesystem::exists("/bin/sh", code) ||
        !std::filesystem::exists("/usr/bin/env", code)) {
        return std::nullopt;
    }
    platform::ChildCommand child;
    child.program = "/usr/bin/env";
    child.arguments = {"-i", "PATH=/usr/bin:/bin", "/bin/sh", "-c", "printf '%s\\0' " + command};
    const platform::CapturedRun ran = platform::run_captured(
        child, platform::CapturedLimits{.timeout = std::chrono::seconds{30}});
    if (ran.status != 0) {
        return std::vector<std::string>{};
    }
    std::vector<std::string> words;
    std::string word;
    for (const char c : ran.out) {
        if (c == '\0') {
            words.push_back(word);
            word.clear();
        } else {
            word += c;
        }
    }
    return words;
}

}  // namespace apogee::testing
