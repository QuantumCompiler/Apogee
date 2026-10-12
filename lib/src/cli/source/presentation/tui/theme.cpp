#include "tui/theme.h"

namespace apogee::tui {

Theme detect_theme(ansi::ColorMode mode) {
    return Theme{.color = ansi::Style::detect(mode).color_enabled()};
}

}  // namespace apogee::tui
