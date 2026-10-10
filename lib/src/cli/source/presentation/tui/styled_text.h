#pragma once

#include <string_view>

#include "markdown/types.h"

/// A line the session core styled for a terminal, read back into spans
/// (32c): the same `ansi::Style` codes the terminal shows -- `[apogee]` in
/// its colour, a note dimmed -- drawn by the TUI as the same looks, so the
/// core says each line once and both painters show it.
namespace apogee::tui {

/// `text` with its SGR sequences (bold, dim, italic, underline, strike, the
/// eight colours, resets) turned into span attributes and its OSC 8 links
/// into span links; any other escape sequence and any other control
/// character dropped, never drawn.
[[nodiscard]] markdown::Row spans_from_sgr(std::string_view text);

}  // namespace apogee::tui
