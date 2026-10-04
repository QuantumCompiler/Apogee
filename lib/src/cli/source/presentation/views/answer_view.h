#pragma once

#include <cstddef>
#include <functional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "ansi/ansi.h"
#include "markdown/stream_renderer.h"
#include "markdown/types.h"

/// An answer's Markdown, painted as it streams on a terminal.
///
/// The painter half of the renderer in `markdown/`: that decides what the
/// rows are, this owns the bytes. Committed rows are written once, each with
/// its newline. The open area -- the line still arriving, a table's
/// placeholder -- is written without a trailing newline, and on the next
/// chunk erased by counted rows (`\r ESC[2K`, then `ESC[A ESC[2K` for each row
/// above) and painted again. The same arithmetic the thinking view uses, and
/// for the same reason it keeps to two rules:
///
/// - **Never the terminal's last column.** Rows are wrapped one short of the
///   width, measured at every paint, so no terminal's wrap policy can turn one
///   painted row into two and leave the erase a row short (2026-09-23).
/// - **Never more open rows than the screen holds.** An erase cannot reach a
///   row that has scrolled away, so an open line taller than the screen shows
///   only its last rows until its newline commits it whole.
///
/// Rendering is a view, never a transform: this is used only where stdout is
/// a terminal, and what a pipe or the transcript receives is the model's text.
namespace apogee::commands {

class AnswerView {
public:
    struct Options {
        /// Where the answer goes: stdout.
        std::ostream* out = nullptr;
        ansi::Style style;
        /// Links as OSC 8 hyperlinks, where the terminal is known to support
        /// them; `text (url)` otherwise.
        bool hyperlinks = false;
        /// Terminal width when `measure_width` is unset or cannot say.
        std::size_t width = 80;
        /// Asked at every paint: a terminal resized mid-answer changes the
        /// width of what is painted next.
        std::function<std::size_t()> measure_width;
        /// Terminal height, asked at every paint; 0 when unknown.
        std::function<std::size_t()> measure_height;
    };

    explicit AnswerView(Options options);

    /// Starts a new answer.
    void begin();

    /// Feeds the next chunk of the answer.
    void write(std::string_view chunk);

    /// Ends the answer: whatever is open is committed, and the cursor is left
    /// at the start of the line below it. Idempotent.
    void finish();

    /// Whether an answer is in progress.
    [[nodiscard]] bool open() const noexcept {
        return active_;
    }

    /// Whether the current (or last) answer has shown any text.
    [[nodiscard]] bool began() const noexcept {
        return renderer_.began();
    }

    /// Rows the open area currently occupies. Exposed for tests: the erase
    /// arithmetic depends on this being exactly right.
    [[nodiscard]] std::size_t painted_rows() const noexcept {
        return painted_;
    }

private:
    void paint(const markdown::RenderOps& ops);
    [[nodiscard]] std::string render_row(const markdown::Row& row) const;
    [[nodiscard]] std::size_t content_width() const;
    [[nodiscard]] std::size_t max_open_rows() const;

    Options options_;
    markdown::StreamRenderer renderer_;
    /// The open area as last painted, so a chunk that changes nothing visible
    /// (most of them extend a word) costs no repaint.
    std::vector<markdown::Row> shown_;
    std::size_t painted_ = 0;
    bool active_ = false;
};

}  // namespace apogee::commands
