#pragma once

#include <cstddef>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <optional>
#include <string>
#include <vector>

#include "tui/progress.h"
#include "tui/runner_view.h"
#include "tui/theme.h"

/// The exec line's state and its keys and drawing, behind `runner_view.h` --
/// `tui/`'s sources alone include it (the shell holds one).
namespace apogee::tui {

struct ExecLineState {
    ExecLineOptions options;
    bool open = false;
    /// The words prefixed to what is typed (the view's group), or empty.
    std::string scope;
    std::string text;
    /// What Tab offered for the word under the cursor, shown under the line.
    std::vector<std::string> candidates;
    /// What the last Enter said: that it runs, or why not.
    std::string said;
    std::vector<std::string> history;
    std::optional<std::size_t> history_at;
};

/// Opens the line under `scope`, what was typed before cleared.
void open_exec_line(ExecLineState& line, std::string scope);

/// A key while the line is open: true when the line took it. A key it does
/// not use -- an F-key naming a view -- is left to the shell.
[[nodiscard]] bool exec_line_key(ExecLineState& line, const ftxui::Event& event);

/// The line and the output above it, drawn while it is open.
[[nodiscard]] ftxui::Element draw_exec_line(const ExecLineState& line, const Theme& theme);

}  // namespace apogee::tui
