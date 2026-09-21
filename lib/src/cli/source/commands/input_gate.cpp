#include "commands/input_gate.h"

#include "platform/platform.h"

namespace apogee::commands {

void discard_startup_typeahead() {
    // The mechanism (tcflush vs FlushConsoleInputBuffer, and the not-a-terminal
    // check) lives in the platform seam; the once-before-the-first-prompt rule
    // lives here, where the call site makes it visible.
    platform::discard_pending_input();
}

}  // namespace apogee::commands
