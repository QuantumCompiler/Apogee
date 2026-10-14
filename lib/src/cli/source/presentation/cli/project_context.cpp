#include "cli/project_context.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

#include "contracts/utf8.h"

namespace apogee::commands {
namespace {

[[nodiscard]] bool is_space(char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

/// The words in `text`, counted as runs of non-space.
[[nodiscard]] std::size_t count_words(std::string_view text) {
    std::size_t words = 0;
    bool in_word = false;
    for (const char c : text) {
        if (is_space(c)) {
            in_word = false;
        } else if (!in_word) {
            in_word = true;
            ++words;
        }
    }
    return words;
}

/// `text` up to the end of its `limit`-th word, as written.
[[nodiscard]] std::string head_words(std::string_view text, std::size_t limit) {
    std::size_t words = 0;
    bool in_word = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (is_space(text[i])) {
            if (in_word && words == limit) {
                return std::string{text.substr(0, i)};
            }
            in_word = false;
        } else if (!in_word) {
            in_word = true;
            ++words;
        }
    }
    return std::string{text};
}

/// `value` with its thousands grouped: 4,000.
[[nodiscard]] std::string grouped(std::size_t value) {
    std::string digits = std::to_string(value);
    for (int at = static_cast<int>(digits.size()) - 3; at > 0; at -= 3) {
        digits.insert(static_cast<std::size_t>(at), ",");
    }
    return digits;
}

}  // namespace

ProjectOpening read_project_opening(const std::filesystem::path& dir) {
    ProjectOpening opening;
    std::error_code code;
    opening.project = std::filesystem::exists(dir / ".git", code);
    for (const std::string_view name : kProjectContextFiles) {
        const std::filesystem::path file = dir / name;
        if (!std::filesystem::is_regular_file(file, code)) {
            continue;
        }
        opening.project = true;
        std::ifstream in{file, std::ios::binary};
        if (!in) {
            opening.unreadable = file.string() + " could not be read";
            break;
        }
        const std::string bytes{std::istreambuf_iterator<char>{in},
                                std::istreambuf_iterator<char>{}};
        // Text where it enters: a Latin-1 file is mended here, so no request
        // that carries it can fail a strict dump.
        const std::string text = harness::valid_utf8(bytes);
        ProjectContext context;
        context.file = std::filesystem::absolute(file, code);
        context.total_words = count_words(text);
        context.text = head_words(text, kProjectContextWords);
        context.words = std::min(context.total_words, kProjectContextWords);
        opening.context = std::move(context);
        break;
    }
    return opening;
}

std::string project_context_note(const ProjectContext& context) {
    std::string note = "Project context, from " + context.file.filename().string() +
                       " in the folder this session opened in (" +
                       context.file.parent_path().string() +
                       "). It is this project's own instructions: follow them where they "
                       "apply.\n\n" +
                       context.text;
    if (context.truncated()) {
        note += "\n\n[" + context.file.filename().string() +
                " goes on: " + grouped(context.total_words - context.words) +
                " more words were left out, past the " + grouped(kProjectContextWords) +
                "-word cap. Read the file itself for the rest.]";
    }
    return note;
}

std::string project_context_line(const ProjectContext& context) {
    const std::string name = context.file.filename().string();
    if (context.truncated()) {
        return "project context: " + name + ", the first " + grouped(context.words) + " of " +
               grouped(context.total_words) + " words, in every request -- the rest left out";
    }
    return "project context: " + name + ", " + grouped(context.words) + " words, in every request";
}

std::vector<std::string> opening_attaches(const std::vector<std::string>& given, bool project) {
    std::vector<std::string> out;
    bool skipped = false;
    for (const std::string& spec : given) {
        if (spec.empty()) {
            skipped = true;
        } else {
            out.push_back(spec);
        }
    }
    if (project && out.empty() && !skipped) {
        out.emplace_back(".");
    }
    return out;
}

}  // namespace apogee::commands
