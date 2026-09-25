#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace apogee::testing {

/// Enough of a terminal to replay what the views write, and to say what the
/// screen ends up showing.
///
/// Printable cells (a wide codepoint takes two), `\r`, `\n` (with the tty's
/// newline-to-CR-LF translation), `ESC[nA`, `ESC[2K`, `ESC[K`, SGR sequences
/// (ignored: this models text, not colour), OSC 8 hyperlinks (ignored), and
/// **deferred wrap**: a character written in the last column leaves the
/// cursor there, and the wrap happens only when the next character arrives.
/// That last rule is where terminals and naive views disagree, and it is how
/// the thinking view's leftover lines were reproduced on 2026-09-23.
///
/// Anything else is recorded as unhandled, so a view that starts emitting a
/// sequence this model does not understand fails its test rather than
/// passing on a misread screen.
class TerminalModel {
public:
    explicit TerminalModel(std::size_t width, std::size_t height = 1000);

    void feed(std::string_view bytes);

    /// Every row, scrollback included, trailing spaces trimmed and trailing
    /// empty rows dropped.
    [[nodiscard]] std::vector<std::string> lines() const;

    /// `lines()` joined with newlines.
    [[nodiscard]] std::string text() const;

    /// The rightmost column any character was written in (zero-based).
    [[nodiscard]] std::size_t widest_column() const noexcept {
        return widest_;
    }

    /// Whether the cursor was ever asked to move above the screen's top row:
    /// an erase that reaches into scrollback, which no terminal can do.
    [[nodiscard]] bool climbed_past_top() const noexcept {
        return climbed_;
    }

    /// Escape sequences this model does not understand.
    [[nodiscard]] const std::vector<std::string>& unhandled() const noexcept {
        return unhandled_;
    }

private:
    void put(std::string_view cell, std::size_t cells);
    void ensure_row();

    std::size_t width_;
    std::size_t height_;
    /// Each row's cells; a wide character's second cell holds "".
    std::vector<std::vector<std::string>> rows_;
    std::size_t row_ = 0;
    std::size_t column_ = 0;
    bool pending_wrap_ = false;
    std::size_t widest_ = 0;
    bool climbed_ = false;
    std::vector<std::string> unhandled_;
};

}  // namespace apogee::testing
