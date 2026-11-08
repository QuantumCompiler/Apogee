#pragma once

#include <cstddef>
#include <string>
#include <string_view>

/// Text a model, a tool's caller or a third party chose, made safe to draw:
/// shown, never obeyed (57p).
///
/// **One neutraliser for every surface.** Item 23 made the rendered answer
/// safe with a private copy of this rule in the Markdown renderer; every other
/// plain-terminal surface -- the permission and question prompts, the
/// reasoning, a replayed chat, a title, a pull request's title, a GGUF name,
/// task narration -- printed what it was handed, so a CR and an erase drew a
/// forged prompt line over the real one, and an OSC 52 wrote the clipboard.
/// The rule now lives here, and those surfaces and the renderer all call it.
///
/// **Byte by byte, never a parser.** Each control character -- C0, DEL, and
/// C1 whether as UTF-8's two bytes (`C2 80`..`C2 9F`) or a lone 8-bit byte --
/// is drawn as U+FFFD. A sequence is never parsed, so there is no state to
/// confuse; nothing is stripped, so the person sees that something was there;
/// and U+FFFD is one cell, so the width arithmetic of every view that erases
/// by counted rows stays exact. Valid text with no control character passes
/// byte for byte. Text is already UTF-8 where it entered (the Harness, a
/// tool's dispatch), so nothing here mends it a second time.
///
/// What is stored and every JSON form keep the true bytes: only drawing goes
/// through here.
namespace apogee::ansi {

/// U+FFFD: what a control character is drawn as.
inline constexpr std::string_view kReplacementCharacter = "\xEF\xBF\xBD";

/// `raw` as one line -- a label: a tool name, a target, an option, a title, a
/// name. Every control character, CR, LF and TAB included, as U+FFFD, so the
/// line can neither end early nor draw over another.
[[nodiscard]] std::string inert_line(std::string_view raw);

/// `raw` as text -- payload: an answer, reasoning, a question, a command. LF
/// kept, TAB expanded to the next four-column stop, CR dropped, and every
/// other control character as U+FFFD.
[[nodiscard]] std::string inert_text(std::string_view raw);

/// The same, the text starting at `column` cells into its row -- so the tab
/// stops of a chunk streamed after another fall where they would in the
/// whole -- and `column` left where the text ends.
[[nodiscard]] std::string inert_text(std::string_view raw, std::size_t& column);

/// `raw` with every control character dropped rather than drawn: for a
/// model's text that is stored as well as shown and is cosmetic -- a chat's
/// title (57p) -- so the file never keeps an escape sequence to replay.
[[nodiscard]] std::string without_controls(std::string_view raw);

}  // namespace apogee::ansi
