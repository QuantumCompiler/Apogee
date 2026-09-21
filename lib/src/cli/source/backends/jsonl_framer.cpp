#include "backends/jsonl_framer.h"

#include <cctype>

namespace apogee::backends {
namespace {

/// Strips one trailing `\r`, so a CRLF stream yields the same line a LF stream
/// does. Without this the `\r` reaches the JSON parser and a valid object
/// fails to parse -- on Windows only, which is the worst place to find it.
[[nodiscard]] std::string_view strip_cr(std::string_view line) noexcept {
    if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
    }
    return line;
}

}  // namespace

void JsonlFramer::feed(std::string_view bytes, const LineSink& on_line) {
    // Appending first, then scanning the joined buffer, is what makes a line
    // split across reads work. Scanning `bytes` alone and remembering "there
    // was a partial" is the version that loses the seam.
    carry_.append(bytes);

    std::size_t start = 0;
    for (;;) {
        const std::size_t newline = carry_.find('\n', start);
        if (newline == std::string::npos) {
            break;
        }
        const std::string_view line =
            strip_cr(std::string_view{carry_}.substr(start, newline - start));
        if (!line.empty() && on_line) {
            on_line(line);
        }
        start = newline + 1;
    }

    // Keep only the unterminated tail. Erasing from the front once per feed
    // beats erasing per line, and it keeps `carry_` from growing without bound
    // across a long stream.
    if (start > 0) {
        carry_.erase(0, start);
    }
}

void JsonlFramer::flush(const LineSink& on_line) {
    const std::string_view line = strip_cr(carry_);
    if (!line.empty() && on_line) {
        on_line(line);
    }
    carry_.clear();
}

bool looks_like_json_object(std::string_view line) noexcept {
    for (const char character : line) {
        if (std::isspace(static_cast<unsigned char>(character)) != 0) {
            continue;
        }
        return character == '{';
    }
    return false;
}

std::vector<std::string> split_lines(std::string_view bytes) {
    std::vector<std::string> lines;
    JsonlFramer framer;
    const auto collect = [&lines](std::string_view line) { lines.emplace_back(line); };
    framer.feed(bytes, collect);
    framer.flush(collect);
    return lines;
}

}  // namespace apogee::backends
