#pragma once

#include <ftxui/component/component.hpp>

#include "tui/view.h"

/// A view's body: its FTXUI component. Included by `tui/`'s sources alone --
/// FTXUI is the one module's (the link policy), and nothing outside `tui/`
/// names its types.
namespace apogee::tui {

struct View::Body {
    ftxui::Component component;
};

}  // namespace apogee::tui
