#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

/// A unified diff as a review reads it (48c): its files, each file's lines
/// numbered on the new side, and the map of the lines the change added --
/// the ground truth every post-model filter holds a finding to. Diff text is
/// data: it is parsed, numbered and quoted, never followed.
namespace apogee::review {

struct DiffFile {
    /// The path on the new side (the old one for a deleted file).
    std::string path;
    std::string old_path;
    bool added_file = false;
    bool deleted_file = false;
    /// `Binary files ... differ`: no lines to review.
    bool binary = false;
    /// The file's lines as the reviewer reads them: its headers, then each
    /// hunk with every new-side line numbered (`  12 +    total = 0`) and
    /// removed lines unnumbered.
    std::string numbered;
    /// Each hunk's numbered text, in order -- what a chunk splits a large
    /// file at.
    std::vector<std::string> hunks;
    /// New-side line -> its text, for every line the change added.
    std::map<std::uint32_t, std::string> added;
};

struct ParsedDiff {
    std::vector<DiffFile> files;

    /// The file at `path`, or nullptr.
    [[nodiscard]] const DiffFile* find(std::string_view path) const noexcept;
    /// Whether the change added line `line` of `path`.
    [[nodiscard]] bool is_added(std::string_view path, std::uint32_t line) const noexcept;
    /// Every file's path, in order.
    [[nodiscard]] std::vector<std::string> paths() const;
};

/// Reads `diff`: `diff --git` headers, `---`/`+++` (`/dev/null` for an added
/// or deleted side), `@@ -a,b +c,d @@` hunks, `Binary files … differ`, and
/// `\ No newline at end of file`. Anything else between files is skipped.
[[nodiscard]] ParsedDiff parse_unified_diff(std::string_view diff);

/// `diff` without the files a glob in `globs` matches (the exclusion list:
/// lockfiles, generated, vendored) or that are binary; the paths left out go
/// to `excluded`.
[[nodiscard]] ParsedDiff exclude_files(const ParsedDiff& diff,
                                       const std::vector<std::string>& globs,
                                       std::vector<std::string>& excluded);

/// A rough token count for budgeting: four bytes a token, rounded up.
[[nodiscard]] std::size_t estimate_tokens(std::string_view text) noexcept;

/// A part of the diff one pass reads.
struct DiffChunk {
    std::vector<std::string> paths;
    std::string text;
};

/// `diff` packed into chunks of at most `max_tokens` each: whole files where
/// they fit, a file too large for one split at its hunks (its header
/// repeated), a hunk too large for one taken alone.
[[nodiscard]] std::vector<DiffChunk> chunk_diff(const ParsedDiff& diff, std::size_t max_tokens);

}  // namespace apogee::review
