#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "agentloop/question.h"
#include "ansi/ansi.h"
#include "views/status_line.h"

/// The terminal implementation of `ask_user`.
///
/// Deliberately minimal: a numbered menu, one question at a time. The rich
/// interactive layer arrives with chat-cli and will replace this; what matters
/// now is that the seam is real and the availability rule is enforced end to
/// end rather than stubbed.
///
/// **The prompt is written to stderr, and answers are read from stdin.** stdout
/// carries the answer and nothing else — a run whose stdout is redirected still
/// prompts on the terminal and still pipes clean text.
namespace apogee::commands {

/// An AskFn backed by the terminal, or a null AskFn when there is no terminal
/// to prompt on.
///
/// Returning null is the point: a null AskFn means the loop never advertises
/// `ask_user` at all. A model told it may ask questions with nobody attached
/// will eventually ask one, and then either hang or invent the answer.
///
/// The prompt is written through `status` rather than raw stderr, so it obeys
/// the same single-line discipline as every other interactive output and cannot
/// land on top of a spinner frame.
[[nodiscard]] agentloop::AskFn terminal_ask_fn(StatusLine& status, ansi::Style style);

/// The lines the terminal's prompt draws for `question`, the `index`th of
/// `total` (57p): the question as text beside its tag, each later row of it
/// under the first row's text; each option numbered, its label drawn as one
/// line -- so the option shown is the one its number picks -- and its
/// description as text, its later rows indented under the label. Shown,
/// never obeyed: no row starts at column 0 but the first, no row of the
/// question starts where an option does, and no control character of the
/// model's reaches the terminal.
[[nodiscard]] std::vector<std::string> ask_prompt_lines(const agentloop::Question& question,
                                                        std::size_t index, std::size_t total,
                                                        const ansi::Style& style);

/// What `typed` answers `question` with: a bare number naming one of its
/// options is that option's label; anything else, free text, as typed --
/// trimmed. Empty is no answer. The terminal's prompt and the full-screen
/// shell's (32c) both read an answer through it.
[[nodiscard]] std::string chosen_answer(const agentloop::Question& question,
                                        std::string_view typed);

}  // namespace apogee::commands
