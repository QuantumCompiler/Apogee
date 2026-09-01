#pragma once

/// The startup typeahead gate.
namespace apogee::commands {

/// Discards anything typed before the first prompt is drawn.
///
/// Chat startup is not instant — every configured backend is constructed first
/// — and the terminal buffers whatever the user types meanwhile. Without this,
/// an impatient keystroke becomes the session's opening message the moment the
/// prompt appears.
///
/// **Called exactly once, immediately before the first prompt. Never between
/// turns.** Typing a follow-up while the model is generating is legitimate
/// typeahead, and eating it would be worse than the problem this fixes.
void discard_startup_typeahead();

}  // namespace apogee::commands
