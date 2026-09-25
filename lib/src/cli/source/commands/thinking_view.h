#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ansi/ansi.h"
#include "ansi/text_width.h"
#include "commands/terminal.h"

/// Live reasoning, rendered without letting it accumulate in scrollback.
///
/// **Read the appendix on the terminal-ux-layer backlog item before changing
/// this.** The obvious implementation — print a header, stream the reasoning
/// under it, then print the answer — is what Ommi shipped first. It reads fine
/// for one turn and is unusable by the third: every turn leaves a ten-line
/// internal monologue above a two-line answer, and scrolling back to find what
/// was actually *said* is miserable. A real user report triggered the rewrite.
///
/// What this does instead: while a block streams, a header plus a rolling
/// window of the last two wrapped lines, repainted in place. When the block
/// ends, the whole region is erased and one dimmed `✻ Thought for Ns` line is
/// all that survives.
namespace apogee::commands {

class ThinkingView {
public:
    struct Options {
        /// Terminal width. Painted rows are wrapped short of it (see
        /// `content_width`).
        std::size_t width = 80;
        /// When set, asked for the width at every repaint instead: a terminal
        /// resized mid-turn otherwise wraps rows painted for the old width,
        /// and the erase arithmetic goes wrong with them.
        std::function<std::size_t()> measure;
        /// Whether to render at all. False on a pipe -- a non-TTY run emits no
        /// thinking, no escape codes, and no summary line.
        bool active = true;
        /// Keep the full reasoning as a permanent transcript with no cursor
        /// movement and no collapse. Someone debugging a model wants every
        /// word, and the collapsed view is actively hostile to that.
        bool verbose = false;
        ansi::Style style;
    };

    ThinkingView(TerminalWriter& writer, Options options);

    /// Feeds a chunk of reasoning.
    ///
    /// **An empty chunk opens nothing.** Redacted-thinking models emit thinking
    /// events with an empty payload; opening the view on one renders an empty
    /// reasoning block on every such turn.
    void write(std::string_view chunk);

    /// Closes an open block: erases the region and leaves the dimmed summary.
    ///
    /// **Idempotent** -- a turn can go thinking → text → thinking, and the
    /// end-of-turn path calls this unconditionally.
    void finish();

    /// Erases the region without leaving a summary. For the path where an
    /// answer or an error takes over and no reasoning happened.
    void abandon();

    [[nodiscard]] bool open() const noexcept {
        return open_;
    }

    /// Rows currently painted. Exposed for tests: the erase arithmetic depends
    /// on this being exactly right.
    [[nodiscard]] std::size_t painted_rows() const noexcept {
        return painted_;
    }

    /// Seconds the current block has been open. Injectable for tests.
    using Clock = std::function<std::int64_t()>;

    void set_clock(Clock clock) {
        clock_ = std::move(clock);
    }

private:
    void repaint_locked(std::ostream& out);
    void erase_locked(std::ostream& out);
    [[nodiscard]] std::size_t content_width() const;

    TerminalWriter& writer_;
    Options options_;
    std::string tail_;
    std::size_t painted_ = 0;
    bool open_ = false;
    std::int64_t started_ = 0;
    Clock clock_;
};

/// Retained tail cap. Only the last few rows are ever shown, so keeping the
/// whole block is pointless; a few KB is generous.
inline constexpr std::size_t kMaxRetainedTail = 4096;

/// Rows in the rolling window.
///
/// **Fixed, not configurable** (decided 2026-08-26). Two is the Claude CLI's
/// shape and needs no explanation; a setting invites someone to set it to 40
/// and recreate the scrollback problem this view exists to solve.
inline constexpr std::size_t kTailLines = 2;

}  // namespace apogee::commands
