#include "contracts/captions.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

#include "contracts/utf8.h"

namespace apogee::harness {
namespace {

/// `c` as caption text: a markup character as its fullwidth form.
[[nodiscard]] std::string fullwidth(char c) {
    switch (c) {
        case '<':
            return "\xEF\xBC\x9C";  // U+FF1C
        case '>':
            return "\xEF\xBC\x9E";  // U+FF1E
        case '{':
            return "\xEF\xBD\x9B";  // U+FF5B
        case '}':
            return "\xEF\xBD\x9D";  // U+FF5D
        case '\\':
            return "\xEF\xBC\xBC";  // U+FF3C
        default:
            return std::string(1, c);
    }
}

}  // namespace

std::string srt_time(double seconds) {
    const auto total = static_cast<long long>(std::llround(std::max(seconds, 0.0) * 1000.0));
    std::array<char, 32> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%02lld:%02lld:%02lld,%03lld", total / 3600000,
                  (total / 60000) % 60, (total / 1000) % 60, total % 1000);
    return buffer.data();
}

std::string write_srt(const std::vector<CaptionCue>& cues) {
    std::string out;
    int number = 0;
    for (const CaptionCue& cue : cues) {
        // Valid text, no carriage returns, no blank line inside the cue, no
        // markup.
        std::string text;
        for (const char c : valid_utf8(cue.text)) {
            if (c == '\r') {
                continue;
            }
            if (c == '\n' && (text.empty() || text.back() == '\n')) {
                continue;
            }
            text += fullwidth(c);
        }
        while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) {
            text.pop_back();
        }
        if (text.empty() || !(cue.end > cue.start)) {
            continue;
        }
        out += std::to_string(++number) + "\n" + srt_time(cue.start) + " --> " + srt_time(cue.end) +
               "\n" + text + "\n\n";
    }
    return out;
}

}  // namespace apogee::harness
