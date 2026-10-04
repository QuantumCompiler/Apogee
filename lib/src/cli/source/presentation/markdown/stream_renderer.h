#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "markdown/types.h"

/// Markdown rendered as it streams -- the logic, with no terminal in it.
///
/// **Line-at-a-time commits, the open line redrawn in place** (decided
/// 2026-09-23). A line is rendered for good when its newline arrives, and is
/// never touched again: anything that may have scrolled away is never
/// repainted. The line still arriving is the **open area**, re-rendered from
/// scratch on every chunk, with whatever is unfinished in it (an unclosed
/// `**`) shown as written.
///
/// **Tables are held until they end.** Column widths need every row, so a
/// streaming table shows one dim `table · N rows…` line in the open area, and
/// is laid out and committed at its first non-table line. A table wider than
/// the screen wraps its cells within narrower columns; only a table whose
/// columns cannot fit even narrow falls back to its rows as written.
///
/// **Chunking cannot change the result.** Commits happen only at newlines, a
/// line's rendering depends only on the lines before it, and the open area is
/// recomputed whole on every feed -- so any split of the same answer ends in
/// the same committed rows.
///
/// A view, never a transform: what reaches a pipe, machine mode and the saved
/// transcript is the model's text, untouched. This renders only for a
/// terminal.
namespace apogee::markdown {

/// How a table column is aligned, from its delimiter row.
enum class Align : std::uint8_t { Left, Center, Right };

class StreamRenderer {
public:
    struct Options {
        /// Links as OSC 8 hyperlinks rather than `text (url)`.
        bool hyperlinks = false;
    };

    StreamRenderer() = default;

    explicit StreamRenderer(Options options) : options_{options} {}

    /// Takes the next piece of the answer, wherever it was cut. Rows are
    /// wrapped to `width` cells.
    [[nodiscard]] RenderOps feed(std::string_view chunk, std::size_t width);

    /// Ends the answer: the open line, and a table still streaming, are
    /// committed, and the open area empties. Blank lines at the very end are
    /// dropped.
    [[nodiscard]] RenderOps finish(std::size_t width);

    /// Forgets everything, ready for the next answer.
    void reset();

    /// Whether the answer has shown any text yet -- blank lines alone do not
    /// count.
    [[nodiscard]] bool began() const noexcept;

private:
    struct Fence {
        char marker = '`';
        std::size_t length = 3;
        /// Spaces before the opening fence, stripped from each code line.
        std::size_t indent = 0;
        /// Where the block sits: under a list item's text, or at the margin.
        std::size_t block_indent = 0;
    };

    struct ListLevel {
        std::size_t marker_indent = 0;
        /// The source column the item's text starts at; a later line indented
        /// this far continues the item.
        std::size_t content_indent = 0;
        /// The rendered hanging indent: the width of the bullet and its gap.
        std::size_t hang = 0;
    };

    /// Where the renderer stands between lines. Copied to render the open area
    /// without disturbing it.
    struct State {
        std::optional<Fence> fence;
        /// A table being collected: its raw rows, the header first.
        std::vector<std::string> table;
        std::string table_delimiter;
        std::vector<Align> aligns;
        /// A line that may be a table's header, decided by the line after it.
        std::optional<std::string> table_candidate;
        std::vector<ListLevel> lists;
        /// Whether anything has been committed: blank lines before the first
        /// text are dropped.
        bool began = false;
        /// A blank line seen and not yet shown: shown once, before the next
        /// block, so runs of blank lines collapse and trailing ones vanish.
        bool blank_pending = false;
        /// The previous line was a list item or its continuation, with no
        /// blank line between -- so an unindented line continues the item.
        bool in_item = false;
    };

    void process(State& state, std::string_view raw, std::size_t width,
                 std::vector<Row>& out) const;
    void ordinary(State& state, const std::string& text, std::size_t width,
                  std::vector<Row>& out) const;
    void flush_table(State& state, std::size_t width, std::vector<Row>& out) const;
    [[nodiscard]] std::vector<Row> open_area(std::size_t width) const;

    Options options_;
    State state_;
    /// The line still arriving.
    std::string pending_;
};

}  // namespace apogee::markdown
