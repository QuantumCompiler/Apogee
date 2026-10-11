#pragma once

#include <ftxui/component/component.hpp>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "tui/runner_view_state.h"
#include "tui/shell.h"

/// The shell's state behind its header: the views, the FTXUI tree, the
/// notice row. `tui/`'s sources alone include it -- the pump reaches the root
/// through it.
namespace apogee::tui {

struct Shell::State {
    ShellOptions options;
    std::vector<View> views;
    /// The stage's selector: the active view's index.
    int selected = 0;
    /// A tab container over the views' bodies, one shown at a time.
    ftxui::Component stage;
    /// The frame, with the shell's keys caught before the stage's.
    ftxui::Component root;
    std::optional<View> bottom;
    /// The exec line, when the shell has one (37h).
    std::optional<ExecLineState> exec_line;
    std::string notice;
    bool quit = false;
    /// The width `render_text` draws at; 0 in the terminal, which is asked.
    int fixed_width = 0;
    std::function<void()> exit;
};

}  // namespace apogee::tui
