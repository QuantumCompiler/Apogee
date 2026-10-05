#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "agent/tool.h"
#include "agentloop/question.h"
#include "tasks/task.h"

/// What a task's turns do with nobody present (27h): the gate's own rule
/// kept byte-for-byte and watched, and a question that ends the task.
///
/// **Deny-by-default survives autonomy.** A task's turns get no confirm
/// function, so an `ask`-level tool resolves to deny exactly as on any pipe
/// -- the denial a tool result the model reads, the turn going on. Nothing
/// here decides anything: the recorder only watches the checker's answers
/// and the calls that ran, so the ledger can say what was refused and what
/// was done. Widening the gate for a task is [27i]'s, by declared grants;
/// never a side effect of the runner.
namespace apogee::tasks {

/// Watches one task's turns: the calls that ran, and the gated calls the
/// gate refused. Copies share what they record.
class TurnRecorder {
public:
    TurnRecorder();

    /// `registry` with each tool's run watched: a call that reaches its
    /// tool -- past the gate -- is recorded with its fingerprint. The tools,
    /// their definitions and the environment note are otherwise `registry`'s.
    [[nodiscard]] agent::ToolRegistry observe(const agent::ToolRegistry& registry) const;

    /// `checker` with each refusal recorded: `Deny`, and `Ask` -- which a
    /// task, having no one to confirm, resolves to deny. The decision is the
    /// checker's own, returned unchanged.
    [[nodiscard]] agent::PermissionChecker gate(agent::PermissionChecker checker) const;

    /// What was recorded since the last take, oldest first.
    [[nodiscard]] std::vector<ToolUse> take_tools() const;
    [[nodiscard]] std::vector<Denial> take_denials() const;

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

/// The `ask_user` an unattended task offers its model: it answers nothing,
/// and ends the turn with `UnansweredQuestion` -- 27h's fail-on-question.
[[nodiscard]] agentloop::AskFn fail_on_question();

}  // namespace apogee::tasks
