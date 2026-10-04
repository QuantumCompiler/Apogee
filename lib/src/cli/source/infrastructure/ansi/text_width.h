#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

/// How many terminal cells text takes, and how to cut it into rows that fit.
///
/// Shared by every view that paints wrapped text and later erases it by
/// counted rows -- the thinking view, the answer view, the download progress
/// line. Moved here from the thinking view when the answer view needed the
/// same arithmetic (2026-09-25): two copies of a width table are two tables
/// that disagree, and a disagreement is a row the erase misses.
namespace apogee::ansi {

/// Length in bytes of the UTF-8 sequence that starts with `lead`. A stray
/// continuation byte counts as one, so a scan never advances by zero.
[[nodiscard]] std::size_t utf8_sequence_length(unsigned char lead) noexcept;

/// The codepoint starting at `text[at]`, `length` bytes long. A malformed
/// sequence decodes as its lead byte.
[[nodiscard]] char32_t decode_utf8(std::string_view text, std::size_t at,
                                   std::size_t length) noexcept;

/// Cells a terminal gives `point`: two for a wide codepoint (CJK, Hangul,
/// fullwidth forms, most emoji), none for a combining mark or a zero-width
/// one, one otherwise.
[[nodiscard]] std::size_t codepoint_cells(char32_t point) noexcept;

/// Number of display cells in a UTF-8 string.
///
/// Still an approximation of what a terminal does -- a table of ranges, not
/// the Unicode width data -- but no longer one that undercounts Chinese
/// reasoning by half. Undercounting is not cosmetic here: a row wider than
/// the terminal wraps, the view then erases one row too few, and every
/// repaint leaves a line behind in the scrollback.
[[nodiscard]] std::size_t display_width(std::string_view text);

/// Wraps `text` and returns the last `max_lines` non-blank rows.
///
/// Wrapping is **by codepoint, not byte** (a multi-byte character split
/// across rows corrupts the output), and blank lines are dropped because a
/// paragraph break inside the reasoning would otherwise spend one of only two
/// rows painting nothing.
[[nodiscard]] std::vector<std::string> wrap_tail(std::string_view text, std::size_t width,
                                                 std::size_t max_lines);

}  // namespace apogee::ansi
