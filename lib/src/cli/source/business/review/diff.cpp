#include "review/diff.h"

#include <algorithm>
#include <charconv>
#include <cstdio>

#include "contracts/review_skill.h"

namespace apogee::review {
namespace {

[[nodiscard]] std::vector<std::string_view> lines_of(std::string_view text) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string_view line = text.substr(start, end - start);
        if (line.ends_with('\r')) {
            line.remove_suffix(1);
        }
        out.push_back(line);
        start = end + 1;
    }
    return out;
}

/// `+c` or `+c,d` after the `@@ -a,b ` of a hunk header: the first new line.
[[nodiscard]] std::uint32_t hunk_start(std::string_view header) {
    const std::size_t plus = header.find(" +");
    if (plus == std::string_view::npos) {
        return 1;
    }
    std::uint32_t value = 0;
    const char* first = header.data() + plus + 2;
    const char* last = header.data() + header.size();
    std::from_chars(first, last, value);
    return value;
}

/// The path after `a/`/`b/` in a `---`/`+++` line; empty for `/dev/null`.
[[nodiscard]] std::string side_path(std::string_view line) {
    std::string_view path = line.substr(4);
    if (const std::size_t tab = path.find('\t'); tab != std::string_view::npos) {
        path = path.substr(0, tab);
    }
    if (path == "/dev/null") {
        return {};
    }
    if (path.starts_with("a/") || path.starts_with("b/")) {
        path.remove_prefix(2);
    }
    return std::string{path};
}

void finish(DiffFile& file, std::string& hunk, std::vector<DiffFile>& files) {
    if (!hunk.empty()) {
        file.hunks.push_back(hunk);
        file.numbered += hunk;
        hunk.clear();
    }
    if (!file.path.empty() || !file.old_path.empty()) {
        if (file.path.empty()) {
            file.path = file.old_path;
        }
        files.push_back(std::move(file));
    }
    file = DiffFile{};
}

[[nodiscard]] std::string header_of(const DiffFile& file) {
    return "diff --git a/" + (file.old_path.empty() ? file.path : file.old_path) + " b/" +
           file.path + "\n";
}

}  // namespace

const DiffFile* ParsedDiff::find(std::string_view path) const noexcept {
    for (const DiffFile& file : files) {
        if (file.path == path) {
            return &file;
        }
    }
    return nullptr;
}

bool ParsedDiff::is_added(std::string_view path, std::uint32_t line) const noexcept {
    const DiffFile* file = find(path);
    return file != nullptr && file->added.contains(line);
}

std::vector<std::string> ParsedDiff::paths() const {
    std::vector<std::string> out;
    for (const DiffFile& file : files) {
        out.push_back(file.path);
    }
    return out;
}

ParsedDiff parse_unified_diff(std::string_view diff) {
    ParsedDiff parsed;
    DiffFile file;
    std::string hunk;
    bool in_hunk = false;
    std::uint32_t next = 0;
    for (const std::string_view line : lines_of(diff)) {
        if (line.starts_with("diff --git ")) {
            finish(file, hunk, parsed.files);
            in_hunk = false;
            // `diff --git a/x b/y`: the paths, until `---`/`+++` say better.
            const std::string_view rest = line.substr(11);
            const std::size_t split = rest.find(" b/");
            if (split != std::string_view::npos && rest.starts_with("a/")) {
                file.old_path = std::string{rest.substr(2, split - 2)};
                file.path = std::string{rest.substr(split + 3)};
            }
            file.numbered = std::string{line} + "\n";
            continue;
        }
        if (!in_hunk && line.starts_with("--- ")) {
            const std::string path = side_path(line);
            file.added_file = path.empty();
            if (!path.empty()) {
                file.old_path = path;
            }
            file.numbered += std::string{line} + "\n";
            continue;
        }
        if (!in_hunk && line.starts_with("+++ ")) {
            const std::string path = side_path(line);
            file.deleted_file = path.empty();
            if (!path.empty()) {
                file.path = path;
            } else {
                file.path = file.old_path;
            }
            file.numbered += std::string{line} + "\n";
            continue;
        }
        if (line.starts_with("Binary files ")) {
            file.binary = true;
            continue;
        }
        if (line.starts_with("@@ ")) {
            if (!hunk.empty()) {
                file.hunks.push_back(hunk);
                file.numbered += hunk;
                hunk.clear();
            }
            in_hunk = true;
            next = hunk_start(line);
            hunk = std::string{line} + "\n";
            continue;
        }
        if (!in_hunk) {
            continue;  // `index`, modes, renames: the header has said enough
        }
        if (line.starts_with("\\")) {
            continue;  // `\ No newline at end of file`
        }
        char number[16];
        if (line.starts_with("+")) {
            file.added.emplace(next, std::string{line.substr(1)});
            std::snprintf(number, sizeof number, "%4u ", static_cast<unsigned>(next));
            hunk += std::string{number} + std::string{line} + "\n";
            ++next;
        } else if (line.starts_with("-")) {
            hunk += "     " + std::string{line} + "\n";
        } else {
            std::snprintf(number, sizeof number, "%4u ", static_cast<unsigned>(next));
            hunk += std::string{number} + std::string{line} + "\n";
            ++next;
        }
    }
    finish(file, hunk, parsed.files);
    return parsed;
}

ParsedDiff exclude_files(const ParsedDiff& diff, const std::vector<std::string>& globs,
                         std::vector<std::string>& excluded) {
    ParsedDiff kept;
    for (const DiffFile& file : diff.files) {
        const bool listed = std::ranges::any_of(globs, [&file](const std::string& glob) {
            return harness::review_glob_match(glob, file.path);
        });
        if (listed || file.binary) {
            excluded.push_back(file.path);
        } else {
            kept.files.push_back(file);
        }
    }
    return kept;
}

std::size_t estimate_tokens(std::string_view text) noexcept {
    return (text.size() + 3) / 4;
}

std::vector<DiffChunk> chunk_diff(const ParsedDiff& diff, std::size_t max_tokens) {
    std::vector<DiffChunk> chunks;
    DiffChunk current;
    const auto flush = [&]() {
        if (!current.text.empty()) {
            chunks.push_back(std::move(current));
            current = DiffChunk{};
        }
    };
    for (const DiffFile& file : diff.files) {
        if (file.added.empty()) {
            continue;  // nothing added: nothing a finding could anchor to
        }
        if (estimate_tokens(current.text) + estimate_tokens(file.numbered) <= max_tokens) {
            current.paths.push_back(file.path);
            current.text += file.numbered;
            continue;
        }
        flush();
        if (estimate_tokens(file.numbered) <= max_tokens) {
            current.paths.push_back(file.path);
            current.text = file.numbered;
            continue;
        }
        // Too large for one chunk: its hunks, each under the file's header.
        const std::string header = header_of(file);
        for (const std::string& hunk : file.hunks) {
            if (!current.text.empty() &&
                estimate_tokens(current.text) + estimate_tokens(hunk) > max_tokens) {
                flush();
            }
            if (current.text.empty()) {
                current.paths.push_back(file.path);
                current.text = header;
            }
            current.text += hunk;
        }
        flush();
    }
    flush();
    return chunks;
}

}  // namespace apogee::review
