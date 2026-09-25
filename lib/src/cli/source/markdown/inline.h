#pragma once

#include <string_view>
#include <vector>

#include "ansi/ansi.h"
#include "markdown/types.h"

/// One line's inline Markdown -- emphasis, code, links -- as styled spans.
///
/// **The subset models write, by CommonMark's rules where it matters.**
/// Emphasis follows the delimiter-run algorithm (left- and right-flanking
/// runs, the rule of three, `_` never inside a word), because the cheap
/// version -- pair up asterisks -- italicises `2 * 3 * 4` and the middle of
/// every `snake_case_name`, and those appear in answers constantly.
///
/// Anything unmatched is shown as written: a line still streaming renders
/// `**bo` as the three characters it is, and becomes bold when `ld**` arrives.
namespace apogee::markdown {

/// The colour inline code is painted in, without its backticks.
inline constexpr ansi::Color kCodeColor = ansi::Color::Magenta;

struct InlineOptions {
    /// What every span starts from: a heading's bold and colour, a table
    /// header's bold.
    ansi::TextAttributes base;
    /// Links as OSC 8 hyperlinks (the text alone, underlined), or as the text
    /// followed by the address, dimmed, where the terminal has no links.
    bool hyperlinks = false;
};

/// Renders `text` -- one line, no newlines -- into spans with adjacent spans
/// of the same look merged.
[[nodiscard]] std::vector<Span> render_inline(std::string_view text, const InlineOptions& options);

}  // namespace apogee::markdown
