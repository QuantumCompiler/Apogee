#pragma once

#include "ansi/ansi.h"

/// How the shell colours what it draws (32b).
///
/// `ansi/`'s mode matrix decides, once, as it does for every painted view:
/// `NO_COLOR` set, or `--no-color`, and the shell draws in the terminal's own
/// colours -- structure still shown by bold, dim and inverse, which are not
/// colour.
namespace apogee::tui {

struct Theme {
    bool color = true;
};

/// The theme for this process under `mode`.
[[nodiscard]] Theme detect_theme(ansi::ColorMode mode = ansi::ColorMode::Auto);

}  // namespace apogee::tui
