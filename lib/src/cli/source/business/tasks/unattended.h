#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "agent/tool.h"
#include "agentloop/question.h"
#include "tasks/task.h"

/// What a task's turns do with nobody present (27h), and with the authority
/// handed to it beforehand (27i): the gate's own rule kept byte-for-byte and
/// watched, and a question answered as the task's policy declares.
///
/// **Deny-by-default survives autonomy.** With nobody present a task's turns
/// get no confirm function, so an `ask`-level tool resolves to deny exactly
/// as on any pipe -- the denial a tool result the model reads, the turn
/// going on. Nothing here decides anything: the recorder only watches what
/// the gate the composition root built decides -- the config's levels, then
/// the task's declared grants (27i), then the person at the terminal when
/// there is one -- so the ledger can say what ran on whose authority, what
/// was refused, and which questions were answered by whom.
namespace apogee::tasks {

/// The gate's two halves, as a task's turns run behind them.
struct WatchedGate {
    agent::PermissionChecker permission;
    /// Null with nobody present: `ask` denies.
    agent::ConfirmFn confirm;
};

/// Watches one task's turns: the calls that ran, every decision the gate
/// made and on whose authority, and every question answered. Copies share
/// what they record.
class TurnRecorder {
public:
    TurnRecorder();

    /// `registry` with each tool's run watched: a call that reaches its
    /// tool -- past the gate -- is recorded with its fingerprint. The tools,
    /// their definitions and the environment note are otherwise `registry`'s.
    [[nodiscard]] agent::ToolRegistry observe(const agent::ToolRegistry& registry) const;

    /// `permission` and `confirm` with every decision recorded, each
    /// returned unchanged. A call the checker allows is recorded with its
    /// authority: `config` when `standing` -- the config's own levels,
    /// nothing granted or answered -- allows it too; `grant` when `grants`
    /// names the tool (never an outbound one: those are asked about per
    /// website); `person` otherwise -- a `session` or `always` answer given
    /// earlier in the run. One it denies is refused by `config`. `ask`
    /// with no `confirm` is refused by `nobody`; with one, the person's
    /// answer is recorded either way.
    [[nodiscard]] WatchedGate watch(agent::PermissionChecker permission,
                                    agent::PermissionChecker standing,
                                    std::vector<std::string> grants,
                                    agent::ConfirmFn confirm) const;

    /// The `ask_user` of a task that declared its answer (27i): every
    /// question gets `answer`, each recorded as answered by `declared`.
    [[nodiscard]] agentloop::AskFn declared_answer(std::string answer) const;

    /// `ask` -- the terminal's, with someone present -- with each question
    /// and the answer given recorded as answered by `person`.
    [[nodiscard]] agentloop::AskFn person(agentloop::AskFn ask) const;

    /// What was recorded since the last take, oldest first.
    [[nodiscard]] std::vector<ToolUse> take_tools() const;
    [[nodiscard]] std::vector<Denial> take_denials() const;
    [[nodiscard]] std::vector<Permit> take_allowed() const;
    [[nodiscard]] std::vector<Answered> take_answered() const;

private:
    struct State;
    std::shared_ptr<State> state_;
};

/// Thrown from a task's `ask_user`: the model asked a question and nobody
/// is present to answer it. The loop rolls the half-turn back as for any
/// aborted question, and the task fails naming the question.
class UnansweredQuestion : public std::runtime_error {
public:
    explicit UnansweredQuestion(std::string question);

    [[nodiscard]] const std::string& question() const noexcept {
        return question_;
    }

private:
    std::string question_;
};

/// The question every one of `request`'s asks, joined: what a task that
/// failed on one names.
[[nodiscard]] std::string question_text(const agentloop::QuestionRequest& request);

/// The `ask_user` an unattended task with no declared answer offers its
/// model: it answers nothing, and ends the turn with `UnansweredQuestion` --
/// 27h's fail-on-question.
[[nodiscard]] agentloop::AskFn fail_on_question();

}  // namespace apogee::tasks
