#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/cancellation.h"
#include "contracts/types.h"
#include "tasks/task.h"

/// The task runner: an outer loop over the one agent loop (27h).
///
/// **The runner decides whether to open another turn, never how a turn
/// runs.** A turn arrives as a closure -- `cli/task_cmd.cpp` hands in one
/// ordinary chat turn of the task's session, the chat's own: the suite's
/// resolution, retrieval, compaction, the per-turn save, consult and
/// validation with `--tools`, the loop's `max_iterations` and its half-turn
/// rollback, all untouched. What the runner adds is the cycle around them:
/// the plan turn, then rounds -- each checked against the acceptance the
/// user stated, each after the first composed from what the round before it
/// failed -- until the task is done, its budget spent, the breaker trips, a
/// turn fails, or a halt or cancel arrives. The ledger is written on every
/// transition, so a task killed at any moment resumes from it.
///
/// Never a daemon: one invocation drives the task until it stops, and exits.
namespace apogee::tasks {

/// One turn the runner asks for.
struct TurnRequest {
    std::string message;
    /// 0 for the plan.
    int round = 0;
    /// `plan`, `execute` or `correct`.
    std::string kind;
    int budget = 0;
    harness::CancellationToken cancellation;
};

/// What a turn did.
struct TurnResult {
    /// The turn ran to an answer, and the session was saved with it.
    bool completed = false;
    /// Ended by the cancellation token.
    bool cancelled = false;
    /// Why it failed: a provider's error.
    std::string error;
    /// The question the model asked with nobody present to answer it.
    std::string question;
    std::string answer;
    std::int64_t tokens = 0;
    bool tokens_estimated = false;
    std::vector<ToolUse> tools;
    std::vector<Denial> denied;
    /// The gated calls let through, each with its authority (27i).
    std::vector<Permit> allowed;
    /// The questions answered, and by whom (27i).
    std::vector<Answered> answered;
};

using TurnFn = std::function<TurnResult(const TurnRequest& request)>;

struct RunRequest {
    /// The `tasks/` directory.
    std::filesystem::path root;
    /// As created, or as loaded for a resume.
    Task task;
    bool resume = false;
    TurnFn turn;
    /// Each step said as it happens: the plan, a round starting, its checks.
    std::function<void(std::string_view)> say;
    /// The command's own interrupt -- Ctrl-C -- honoured as a cancel.
    harness::CancellationToken interrupt;
    /// How often a halt or cancel left by another process is looked for.
    std::chrono::milliseconds poll{200};
};

struct RunOutcome {
    Task task;
    /// A ledger write failed: the run stopped there, the ledger as it was
    /// last written.
    std::string error;
};

/// Why `task` cannot be resumed -- it is finished -- or empty when it can.
[[nodiscard]] std::string resume_refusal(const Task& task);

/// Drives `request.task` from where its ledger stands -- a new task from its
/// plan, a resumed one from its first unfinished turn -- until it stops. The
/// caller holds the task lock.
[[nodiscard]] RunOutcome run_task(const RunRequest& request);

/// The answer to `message` in `messages`, when a turn sent it and finished:
/// the last user message reading exactly `message`, then the answer that
/// closed its turn. nullopt when there is none -- the turn never ran, or was
/// cut short.
[[nodiscard]] std::optional<std::string> answer_in_session(
    const std::vector<harness::ChatMessage>& messages, std::string_view message);

}  // namespace apogee::tasks
