#pragma once

#include <string>
#include <string_view>

#include "contracts/cancellation.h"
#include "contracts/errors.h"
#include "contracts/types.h"
#include "harness/harness.h"

/// `auto` thinking, decided per question (26i).
///
/// Reasoning is most of a thinking model's time on an ordinary answer -- 26
/// of 57 seconds on the reference machine (2026-09-25) -- and the same cost
/// for arithmetic as for small talk. `auto` asks once per question whether
/// this one needs it: the utility model, when the config names one, as a
/// one-word side request; otherwise, and whenever that judge cannot answer, a
/// rule needing no model at all.
namespace apogee::agentloop {

/// Questions longer than this are reasoned about whatever they hold.
inline constexpr std::size_t kThinkingQuestionBytes = 200;

/// Whether `question` needs reasoning, judged without a model: longer than
/// `kThinkingQuestionBytes`, or holding code, arithmetic, or a "why" or "how".
[[nodiscard]] bool question_needs_thinking(std::string_view question);

/// One question's decision, and what made it -- said in `--verbose`.
struct ThinkingDecision {
    bool think = true;
    /// "the utility model", or the rule's reason ("a short question with no
    /// code, maths, why or how").
    std::string by;
};

/// Decides `auto` for `question`: `judge` -- a backend name, empty for none --
/// asked yes or no, its own reasoning off and greedy; on no judge, or any
/// answer that is not yes or no, the rule above.
[[nodiscard]] ThinkingDecision decide_thinking(const harness::Harness& harness,
                                               std::string_view judge, std::string_view question,
                                               const harness::CancellationToken& cancellation);

/// The request a judge is asked, for a test to inspect.
[[nodiscard]] harness::ChatRequest thinking_judge_request(std::string_view judge,
                                                          std::string_view question);

/// A turn's thinking: `on` and `off` as asked; `auto` decided for `question`.
/// The budget passes through either way.
[[nodiscard]] harness::Thinking resolve_turn_thinking(
    const harness::Harness& harness, const harness::Thinking& asked, std::string_view judge,
    std::string_view question, const harness::CancellationToken& cancellation,
    ThinkingDecision* decision = nullptr);

}  // namespace apogee::agentloop
