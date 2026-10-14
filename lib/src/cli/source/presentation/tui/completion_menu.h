#pragma once

#include <cstddef>
#include <ftxui/dom/elements.hpp>
#include <vector>

#include "tui/menu_item.h"
#include "tui/theme.h"

/// The completion menu's rows (45b), drawn the one way on every surface the
/// shell owns -- the exec line's and the session input's: a window of items,
/// the chosen one marked `›`, each group's heading before its first item in
/// the window, the descriptions in one column, and how many more there are
/// above and below. Internal to `tui/`: it names FTXUI's types.
namespace apogee::tui {

/// The first item of a `rows`-item window that keeps `chosen` in sight,
/// moved no further than it must from `top`.
[[nodiscard]] std::size_t menu_window(std::size_t chosen, std::size_t top, std::size_t rows,
                                      std::size_t count);

/// The window from `top`, `rows` items at most, `chosen` marked.
[[nodiscard]] ftxui::Elements menu_rows(const std::vector<MenuItem>& items, std::size_t chosen,
                                        std::size_t top, std::size_t rows, const Theme& theme);

}  // namespace apogee::tui
