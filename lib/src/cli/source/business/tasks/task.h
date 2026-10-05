#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// A task: a goal the application drives to done over successive turns of
/// one ordinary chat session, with nobody at the keyboard (27h).
///
/// **The record is the task.** Its goal, its acceptance checks, its budget,
/// the plan the first turn answered, every round's outcome and the session
/// it drives -- never the session's transcript, which the record names and
/// does not copy. Everything that decides what happens next -- the message a
/// round is sent, whether a round made progress, whether the task is done,
/// spent or stalled -- is a function of this record, so a task resumed in
/// another process continues exactly where the last one stopped.
///
/// **Bounded by construction.** `rounds_budget` is a field with a floor of
/// one and a ceiling of `kMaxRounds`, and no value means "unbounded": an
/// unbounded autonomous loop is unrepresentable, not discouraged. The plan is
/// one more turn, outside the budget, and every turn is the agent loop's own
/// bounded turn (`max_iterations`), untouched.
namespace apogee::tasks {

/// The budget when none is given, and the most a task may be given.
inline constexpr int kDefaultRounds = 8;
inline constexpr int kMaxRounds = 32;
/// Consecutive rounds with no newly-passing check and no new tool activity
/// after which a task is stalled -- the breaker.
inline constexpr int kBreakerRounds = 2;
/// The most of the plan quoted into a round's message; the ledger keeps it
/// whole.
inline constexpr std::size_t kPlanQuoteLimit = 8000;

inline constexpr int kLedgerSchemaVersion = 1;

// --- statuses ------------------------------------------------------------------

/// The plan turn is in flight -- or was, when its process died.
inline constexpr std::string_view kPlanning = "planning";
/// A round is in flight -- or was, when its process died.
inline constexpr std::string_view kRunning = "running";
/// Stopped at a round boundary by `task halt`; resumable.
inline constexpr std::string_view kHalted = "halted";
/// Stopped mid-turn by `task cancel` or Ctrl-C; resumable.
inline constexpr std::string_view kCancelled = "cancelled";
/// Every check passed and the model reported the task done. Final.
inline constexpr std::string_view kDone = "done";
/// The round budget is spent with the task not done. Final.
inline constexpr std::string_view kExhausted = "exhausted";
/// The breaker: rounds without progress. Resumable, the count cleared.
inline constexpr std::string_view kStalled = "stalled";
/// An unrecoverable error, or a question nobody was present to answer.
/// Resumable.
inline constexpr std::string_view kFailed = "failed";

// --- round kinds and outcomes -----------------------------------------------------

inline constexpr std::string_view kPlanRound = "plan";
inline constexpr std::string_view kExecuteRound = "execute";
inline constexpr std::string_view kCorrectRound = "correct";

inline constexpr std::string_view kCompleted = "completed";
/// The round's outcome when it ended any other way; it is run again, under
/// the same number, when the task resumes.
inline constexpr std::string_view kInterrupted = "interrupted";

// --- transitions --------------------------------------------------------------------

inline constexpr std::string_view kCreatedEvent = "created";
inline constexpr std::string_view kStartedEvent = "started";
inline constexpr std::string_view kResumedEvent = "resumed";
inline constexpr std::string_view kPlanStartedEvent = "plan_started";
inline constexpr std::string_view kPlanRecordedEvent = "plan_recorded";
inline constexpr std::string_view kRoundStartedEvent = "round_started";
inline constexpr std::string_view kRoundEndedEvent = "round_ended";
inline constexpr std::string_view kFinishedEvent = "finished";

/// The line a round's answer ends with, `TASK STATUS: DONE` or
/// `TASK STATUS: NOT DONE` -- the model's self-report.
inline constexpr std::string_view kStatusMarker = "TASK STATUS:";

// --- acceptance ---------------------------------------------------------------------

enum class CheckKind : std::uint8_t {
    /// `--require TEXT`: the round's answer contains the text, exactly.
    Require,
    /// `--require-file PATH`: the file exists and is not empty. The path is
    /// absolute, made so where the task was started.
    RequireFile,
};

[[nodiscard]] std::string_view to_string(CheckKind kind) noexcept;
[[nodiscard]] std::optional<CheckKind> check_kind_from_string(std::string_view text) noexcept;

struct Check {
    CheckKind kind = CheckKind::Require;
    std::string value;
};

/// What the check asks, for a person and the model alike: `the answer
/// contains "42"`, `the file /tmp/out.txt exists and is not empty`.
[[nodiscard]] std::string describe(const Check& check);

struct CheckResult {
    bool passed = false;
    /// Why, in a few words: `found`, `not in the answer`, `no such file`.
    std::string detail;
};

/// `check` against a round's answer and the filesystem as it is now.
[[nodiscard]] CheckResult evaluate(const Check& check, std::string_view answer);

/// What the model said of its own work.
enum class SelfReport : std::uint8_t { Unreported, Done, NotDone };

[[nodiscard]] std::string_view to_string(SelfReport report) noexcept;
[[nodiscard]] SelfReport self_report_from_string(std::string_view text) noexcept;
/// The report as a person reads it: `reported done`, `reported not done`,
/// `no status reported`.
[[nodiscard]] std::string_view report_words(SelfReport report) noexcept;

/// The last `TASK STATUS:` line in `answer`, read through any Markdown around
/// it, case aside: `DONE` or `NOT DONE`. Anything else, or no such line, is
/// unreported -- which is never taken for done.
[[nodiscard]] SelfReport parse_self_report(std::string_view answer);

// --- what a round did ------------------------------------------------------------------

/// A tool call that ran -- through the gate, not refused at it.
struct ToolUse {
    std::string tool;
    /// The call's name and arguments, hashed: the same call twice is the
    /// same activity, and what the breaker counts as new is a new one.
    std::string fingerprint;
};

/// The first sixteen hex digits of the SHA-256 of `tool`, a NUL and
/// `arguments`.
[[nodiscard]] std::string fingerprint(std::string_view tool, std::string_view arguments);

/// A gated call the gate refused: `deny`, or `ask` with nobody to answer.
struct Denial {
    std::string tool;
    std::string target;
};

struct Round {
    /// 0 is the plan; the budget's rounds are 1..rounds_budget.
    int index = 0;
    /// `plan`, `execute` or `correct`.
    std::string kind;
    std::string started_at;
    std::string ended_at;
    /// The session's completed turns when the round started: a restart that
    /// finds more knows the round's turn finished, and takes its answer from
    /// the session rather than asking again.
    int session_turns_before = 0;
    /// `completed`, `interrupted`, or empty while in flight.
    std::string outcome;
    /// Whether the round's answer was found in the session after a restart.
    bool adopted = false;
    /// The self-report, as `to_string(SelfReport)`; empty for the plan.
    std::string self_report;
    /// One per task check, in order; empty for the plan.
    std::vector<CheckResult> checks;
    std::vector<ToolUse> tools;
    std::vector<Denial> denied;
    std::int64_t tokens = 0;
    bool tokens_estimated = false;
    /// A check passed that had never passed before, or a tool call ran that
    /// had never run before.
    bool progress = false;
};

/// One entry of the ledger's transition log, oldest first.
struct Transition {
    std::string at;
    std::string event;
    /// The task's status after it.
    std::string status;
    int round = 0;
    std::string detail;
};

struct Task {
    int schema_version = kLedgerSchemaVersion;
    std::string id;
    std::string goal;
    std::vector<Check> checks;
    int rounds_budget = kDefaultRounds;
    /// Where the task was started: its tools work there, and it resumes
    /// only there.
    std::string working_directory;
    /// The chat session the task drives.
    std::string session_id;
    /// Whether its turns were offered tools (`--tools`).
    bool tools = false;
    std::string status = std::string{kPlanning};
    /// Why it stopped, said in full: the unpassed checks, the error, the
    /// question nobody answered. Empty while it runs.
    std::string reason;
    /// The plan turn's answer, quoted into every round.
    std::string plan;
    /// The plan first, then the rounds, in order.
    std::vector<Round> rounds;
    /// Consecutive completed rounds without progress.
    int no_progress = 0;
    std::string created_at;
    std::string updated_at;
    std::vector<Transition> transitions;
};

/// A status a task cannot move on from: `done` or `exhausted`.
[[nodiscard]] bool finished(const Task& task) noexcept;

/// Whether the task still holds its session -- planning, running or halted:
/// the session it writes, or will write again, is not to be deleted under
/// it. Cancelling a task is the way to let its session go.
[[nodiscard]] bool live(const Task& task) noexcept;

/// The rounds of the budget the task has used: completed ones, the plan
/// aside.
[[nodiscard]] int rounds_used(const Task& task) noexcept;

/// The newest completed round other than the plan, or null.
[[nodiscard]] const Round* last_completed_round(const Task& task) noexcept;

/// The checks that did not pass in the newest completed round -- every check,
/// before one has run -- each `describe`d.
[[nodiscard]] std::vector<std::string> unpassed_checks(const Task& task);

/// Appends a transition stamped `at`, and sets `updated_at`.
void record_transition(Task& task, std::string_view event, std::string at, int round = 0,
                       std::string detail = {});

/// The message the plan turn is sent.
[[nodiscard]] std::string plan_message(const Task& task);

/// The message round `index` (1-based) is sent: the first carries out the
/// plan, every later one says what the round before it failed. A function
/// of the record alone, so a round run again after a restart is sent exactly
/// what it was sent the first time.
[[nodiscard]] std::string round_message(const Task& task, int index);

/// Fills in the task's last round, completed, from what its turn answered
/// -- each check evaluated, the self-report read, progress decided against
/// every round before it -- and moves the task on: `done`; `exhausted` once
/// the budget is used; `stalled` after `kBreakerRounds` rounds without
/// progress, the reason naming what did not pass; or still `running`.
void conclude_round(Task& task, std::string_view answer);

/// The reason a task stopped short, naming each check that has not passed
/// and the self-report when it was not `done`.
[[nodiscard]] std::string shortfall(const Task& task);

[[nodiscard]] nlohmann::json task_to_json(const Task& task);
/// Throws std::runtime_error on a wrong shape or a missing id.
[[nodiscard]] Task task_from_json(const nlohmann::json& json);

}  // namespace apogee::tasks
