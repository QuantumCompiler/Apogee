#pragma once

#include <cstddef>
#include <string>
#include <string_view>

/// A change to one file as a unified diff (52a): what a file-writing tool's
/// permission question carries beside its target, so the person asked -- or
/// the app drawing the card -- sees the change itself, not only its path.
///
/// One computation per change: the diff is made from the two texts the tool
/// would leave, and its counts are read back off the diff's own text, so a
/// prompt's `+12 -3` and the diff a driver shows can never disagree.
namespace apogee::harness {

/// The most diff text a question carries, in bytes (52a): past it the hunks
/// stop at a line and a last line says the cap and the whole change's
/// counts. Room for a screenful of hunks; one line of the machine protocol
/// stays modest.
inline constexpr std::size_t kDiffCap = 32 * 1024;

/// How much a change adds and removes, in lines.
struct DiffCounts {
    std::size_t added = 0;
    std::size_t removed = 0;
    /// The text stopped at its cap; the counts are still the whole change's.
    bool cut = false;
    /// Either side held a NUL byte: no lines to count.
    bool binary = false;
};

/// `before` changed into `after`, as a unified diff of `path` with three
/// lines of context: `--- a/<path>` and `+++ b/<path>` (`--- /dev/null` when
/// `before_exists` is false -- a new file), then `@@ -a,b +c,d @@` hunks,
/// with `\ No newline at end of file` after a side's unterminated last line.
/// Empty when nothing changes. A side holding a NUL byte is binary: `Binary
/// files a/<path> and b/<path> differ`, as git says it. Past `cap` bytes the
/// last hunk is cut at a line, its header counting the lines it kept, and a
/// last line says so: `[diff cut at 32 KB: +120 -45 lines in all]`.
[[nodiscard]] std::string unified_diff(std::string_view path, std::string_view before,
                                       std::string_view after, bool before_exists = true,
                                       std::size_t cap = kDiffCap);

/// What `diff` adds and removes, read off its text: the `+` and `-` lines of
/// its hunks, or a cut diff's own totals.
[[nodiscard]] DiffCounts diff_counts(std::string_view diff);

/// `+12 -3`, as a terminal's permission prompt says a change -- `(cut)` after
/// it when the diff stopped at its cap; `binary` for a binary one.
[[nodiscard]] std::string describe_diff_counts(const DiffCounts& counts);

}  // namespace apogee::harness
