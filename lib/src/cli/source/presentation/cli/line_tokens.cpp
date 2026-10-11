#include "cli/line_tokens.h"

#include <optional>

namespace apogee::commands {

namespace {

[[nodiscard]] bool blank(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

}  // namespace

LineTokens line_tokens(std::string_view line) {
    LineTokens out;
    std::optional<std::string> word;  // engaged once a word has begun
    std::size_t at = 0;
    while (at < line.size()) {
        const char c = line[at];
        if (blank(c)) {
            if (word.has_value()) {
                out.words.push_back(std::move(*word));
                word.reset();
            }
            ++at;
            continue;
        }
        if (!word.has_value()) {
            word.emplace();
        }
        if (c == '\'') {
            const std::size_t end = line.find('\'', at + 1);
            if (end == std::string_view::npos) {
                out.error = "a single quote is left open";
                return out;
            }
            word->append(line.substr(at + 1, end - at - 1));
            at = end + 1;
            continue;
        }
        if (c == '"') {
            ++at;
            bool closed = false;
            while (at < line.size()) {
                const char inner = line[at];
                if (inner == '"') {
                    closed = true;
                    ++at;
                    break;
                }
                if (inner == '\\' && at + 1 < line.size()) {
                    const char next = line[at + 1];
                    if (next == '"' || next == '\\' || next == '$' || next == '`') {
                        word->push_back(next);
                        at += 2;
                        continue;
                    }
                }
                word->push_back(inner);
                ++at;
            }
            if (!closed) {
                out.error = "a double quote is left open";
                return out;
            }
            continue;
        }
        if (c == '\\') {
            if (at + 1 >= line.size()) {
                out.error = "a backslash ends the line with nothing to escape";
                return out;
            }
            word->push_back(line[at + 1]);
            at += 2;
            continue;
        }
        word->push_back(c);
        ++at;
    }
    if (word.has_value()) {
        out.words.push_back(std::move(*word));
    }
    out.trailing_space = !line.empty() && blank(line.back());
    return out;
}

}  // namespace apogee::commands
