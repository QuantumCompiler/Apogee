#pragma once

#include "cli/command.h"
#include "cli/tui_workbench.h"
#include "tui/list_view.h"

/// The shell's Symphonies view (37d): the definitions `symphonies list
/// --output-format json` states -- name, the stages' roles in order, where
/// each comes from, its description or that it cannot be played -- Enter
/// showing `symphonies show`'s card, and `p` playing the selected one
/// through the session's own `/play` (`WorkbenchHooks::play_symphony`): typed
/// input or none, entered in the execute session open, or the first line of
/// a new one under the default suite. The walk, its caps and its refusals are
/// the session's to say; the view never checks a play itself (ADR 0010).
namespace apogee::commands {

[[nodiscard]] tui::ListOptions symphonies_view_options(const RootContext& context,
                                                       const WorkbenchHooks& hooks);

}  // namespace apogee::commands
