#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "markdown/types.h"

/// Styled spans cut into rows that fit a width.
namespace apogee::markdown {

/// Display cells a row takes (`ansi::display_width` over its text).
[[nodiscard]] std::size_t row_width(const Row& row);

/// A row's text with its styling dropped. For tests and for measuring.
[[nodiscard]] std::string plain_text(const Row& row);

/// Wraps `content` into rows of at most `width` cells: the first row begins
/// with `first` (a bullet, a gutter), every later row with `rest` (a hanging
/// indent, the same gutter). Rows break between words; a word longer than a
/// whole row breaks between codepoints. Spaces at a break are dropped, and so
/// are spaces at the very end. Always at least one row.
[[nodiscard]] std::vector<Row> wrap(const std::vector<Span>& content, std::size_t width,
                                    const Row& first, const Row& rest);

/// As `wrap`, but for code: rows are cut at the width wherever it falls and
/// no space is dropped, because indentation in code is meaning.
[[nodiscard]] std::vector<Row> hard_wrap(const std::vector<Span>& content, std::size_t width,
                                         const Row& first, const Row& rest);

}  // namespace apogee::markdown
