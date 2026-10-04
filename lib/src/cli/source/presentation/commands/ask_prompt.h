#pragma once

#include "agentloop/question.h"
#include "ansi/ansi.h"
#include "commands/status_line.h"

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

}  // namespace apogee::commands
