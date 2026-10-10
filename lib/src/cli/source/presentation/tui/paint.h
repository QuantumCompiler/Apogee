#pragma once

#include <ftxui/dom/elements.hpp>

#include "markdown/types.h"
#include "tui/theme.h"

/// Painting `markdown/`'s rows with FTXUI, and the width a frame is drawn at.
/// `tui/`'s sources alone include it.
namespace apogee::tui {

/// One row of styled spans: each span its own look -- bold, dim, italic,
/// underline, strike, a colour where the theme has colour, a link.
[[nodiscard]] ftxui::Element paint_row(const markdown::Row& row, const Theme& theme);

/// `child` held to the box it is given: laid out at exactly that size, drawn
/// clipped to it, and never scrolled. The shell's stage is one: a view lays
/// itself out in the space there is -- the session view's transcript region
/// shrinking and scrolling inside it, its input always on screen -- where a
/// frame would hand the view its whole natural height and scroll the stage
/// to whatever the view had focused (found 2026-10-10: the input pushed off
/// the bottom by a long conversation).
[[nodiscard]] ftxui::Element clip(ftxui::Element child);

/// The width the shell draws its current frame at: what a view wraps its
/// rows to. Set by the shell before each frame, on the shell's thread.
[[nodiscard]] int frame_width() noexcept;
void set_frame_width(int width) noexcept;

}  // namespace apogee::tui
