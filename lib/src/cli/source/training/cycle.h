#pragma once

#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "harness/cancellation.h"
#include "harness/config.h"
#include "training/datasets.h"
#include "training/pipeline.h"
#include "training/trainer.h"

/// The continuous cycle: unattended, scheduler-invoked training. One
/// invocation is one gated pass -- collect, merge, run the named pipeline,
/// gate, promote or discard -- meant for launchd or cron; there is no
/// daemon and no `--watch`, by rule. The state is two files under
/// `training/cycle/`: `cycle.lock` (a PID, created exclusively) and
/// `history.json` (written atomically), and the filesystem is the source of
/// truth.
///
/// **The dual gate and the circuit breaker are the safety.** A candidate
/// must clear both the last passing cycle's score and the pinned anchor's
/// within `regression_threshold`, because per-cycle no-regression alone
/// lets tiny regressions accumulate into drift; `k` consecutive failures
/// halt the loop until `cycle resume`; and a failing candidate never
/// reaches inference -- promotion happens only after the gate, through the
/// promote path, whose own gate runs again.
///
/// **Sessions are consumed once, and only with consent.** A `sessions`
/// source refuses without `log_consent: true`, naming the two risks, and
/// the history keeps a watermark so a conversation is never trained on
/// twice.
namespace apogee::training {

inline constexpr std::string_view kCycleIdPrefix = "cycle-";
inline constexpr std::string_view kCycleDirName = "cycle";
inline constexpr std::string_view kHistoryFileName = "history.json";
inline constexpr std::string_view kLockFileName = "cycle.lock";
inline constexpr std::string_view kQueueDirName = "queue";
inline constexpr std::string_view kConsumedDirName = "consumed";
inline constexpr std::string_view kWorkDirName = "work";

inline constexpr std::string_view kGatePass = "pass";
inline constexpr std::string_view kGateFail = "fail";
inline constexpr std::string_view kGateSkipped = "skipped";

/// One `cycle run`.
struct CycleRunRecord {
    std::string run_at;
    /// `directory`, `sessions`, or `directory+sessions`.
    std::string source;
    int dataset_rows = 0;
    std::string pipeline_id;
    /// `pass`, `fail` or `skipped`.
    std::string gate;
    std::string gate_reason;
    double cycle_score = 0.0;
    double prev_score = 0.0;
    double anchor_score = 0.0;
    int promoted_version = 0;
    std::string note;
};

/// `training/cycle/history.json`.
struct CycleHistory {
    std::string backend;
    /// 0 until the first passing cycle.
    int anchor_version = 0;
    double anchor_score = 0.0;
    int consecutive_fails = 0;
    int total_runs = 0;
    bool halted = false;
    std::string halt_reason;
    /// The `started_at` of the newest session consumed; only newer ones are
    /// collected next time.
    std::string sessions_until;
    /// Oldest first.
    std::vector<CycleRunRecord> runs;
};

[[nodiscard]] nlohmann::json history_to_json(const CycleHistory& history);
/// Throws std::runtime_error on a wrong shape.
[[nodiscard]] CycleHistory history_from_json(const nlohmann::json& json);

[[nodiscard]] std::filesystem::path history_path(const std::filesystem::path& cycle_dir);
/// The history, or an empty one for `backend` when there is none yet;
/// `error` set when the file exists and cannot be read.
[[nodiscard]] CycleHistory load_history(const std::filesystem::path& cycle_dir,
                                        std::string_view backend, std::string& error);
[[nodiscard]] bool history_exists(const std::filesystem::path& cycle_dir);
/// Through a temp file and a rename. The error text, or empty.
[[nodiscard]] std::string save_history(const std::filesystem::path& cycle_dir,
                                       const CycleHistory& history);

/// The PID lock: created exclusively, removed on release. A lock left by a
/// crashed cycle is named in the refusal with the way out.
class CycleLock {
public:
    /// nullopt with `error` set when the lock is held or cannot be made.
    [[nodiscard]] static std::optional<CycleLock> acquire(const std::filesystem::path& cycle_dir,
                                                          std::string& error);
    ~CycleLock();
    CycleLock(CycleLock&& other) noexcept;
    CycleLock& operator=(CycleLock&& other) noexcept;
    CycleLock(const CycleLock&) = delete;
    CycleLock& operator=(const CycleLock&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

    /// Removes the file now; safe to call twice.
    void release() noexcept;

private:
    explicit CycleLock(std::filesystem::path path);
    std::filesystem::path path_;
};

/// Whether a cycle holds the lock now.
[[nodiscard]] bool cycle_lock_held(const std::filesystem::path& cycle_dir);

/// Why the cycle must not run -- halted, or `k` consecutive failures (0
/// disables that check) -- naming `apogee train cycle resume`. Empty when
/// it may.
[[nodiscard]] std::string check_circuit_breaker(const CycleHistory& history, int k);

void halt_cycle(CycleHistory& history, std::string reason);
/// Clears the halt and the failure count. Whether it was halted.
bool resume_cycle(CycleHistory& history);

struct DirectoryCollection {
    /// `*.jsonl` under the directory, sorted by name.
    std::vector<std::filesystem::path> files;
    std::string error;
};

/// Creates the directory when missing; ignores anything not `*.jsonl`.
[[nodiscard]] DirectoryCollection collect_directory(const std::filesystem::path& dir);

/// Moves the files into `<queue_dir>/consumed/`. The error text, or empty.
[[nodiscard]] std::string consume_directory_files(const std::vector<std::filesystem::path>& files,
                                                  const std::filesystem::path& queue_dir);

struct SessionCollection {
    std::vector<std::string> lines;
    int sessions = 0;
    int skipped = 0;
    /// The newest `started_at` considered -- the next watermark.
    std::string newest_started_at;
    std::string error;
};

/// The user's sessions as chat lines through the one miner: refused
/// without `log_consent` naming the key and the two risks; only sessions
/// newer than `watermark` (an RFC3339 `started_at`, or empty), on the
/// source's `backend` and from its `since` date when set.
[[nodiscard]] SessionCollection collect_sessions(const std::vector<SessionView>& views,
                                                 const harness::CycleSourceConfig& source,
                                                 std::string_view watermark);

/// Concatenates the files' non-blank lines into `out`. The rows written;
/// -1 with `error` set.
[[nodiscard]] int merge_datasets(const std::vector<std::filesystem::path>& sources,
                                 const std::filesystem::path& out, std::string& error);

struct AnchorGate {
    bool prev_ok = false;
    bool anchor_ok = false;

    [[nodiscard]] bool pass() const noexcept {
        return prev_ok && anchor_ok;
    }
};

/// `current >= prev - threshold` and `current >= anchor - threshold`; a
/// reference of 0 means no data for that half, which passes.
[[nodiscard]] AnchorGate anchor_gate_pass(double current, double prev, double anchor,
                                          double threshold) noexcept;

/// The newest passing run's score, or 0.
[[nodiscard]] double last_passing_cycle_score(const CycleHistory& history) noexcept;

/// Appends the record and keeps the counters: a fail counts, a pass resets,
/// a skip touches neither.
void record_cycle_run(CycleHistory& history, CycleRunRecord record);

/// Halts when `k > 0` and the failures have reached it. Whether it did.
bool trip_breaker(CycleHistory& history, int k);

/// What promotion answered: the version and the file, or why not.
struct CyclePromotion {
    bool ok = false;
    std::string error;
    int version = 0;
    std::string gguf_path;
};

/// The promote path, arriving as a closure: it edits the config, which
/// this package never does.
using PromoteFn = std::function<CyclePromotion(std::string_view run_id)>;

struct CycleRequest {
    harness::CycleConfig config;
    std::filesystem::path cycle_dir;
    std::filesystem::path training_dir;
    /// The named pipeline; every stage's dataset is overridden with the
    /// merged one, its suite kept.
    const harness::PipelineSpec* spec = nullptr;
    /// The suites resolved per stage (the datasets are ignored).
    std::vector<ResolvedStage> stages;
    std::filesystem::path base_model;
    /// `--source`: replaces the first directory source's queue for this run.
    std::filesystem::path source_override;
    /// The persisted sessions, for a `sessions` source.
    std::vector<SessionView> sessions;
    Trainer* trainer = nullptr;
    std::string judge_backend;
    JudgeFn judge;
    GateMode gate_mode = GateMode::Hard;
    PromoteFn promote;
    std::function<void(std::string_view)> on_message;
    std::function<void(const PipelineProgress&)> on_progress;
    harness::CancellationToken cancellation;
};

struct CycleOutcome {
    /// The pass ran to a recorded outcome (pass, fail or skipped).
    bool ok = false;
    bool cancelled = false;
    /// Why the pass could not run at all: the lock, the breaker, a source.
    std::string error;
    /// The record appended, when one was.
    std::optional<CycleRunRecord> record;
    CycleHistory history;
    /// The breaker tripped on this pass.
    bool halted_now = false;
};

/// One gated pass: the lock, the breaker, the sources, the merge, the
/// pipeline, the dual gate, then promote-and-consume or discard, the
/// history saved at every outcome.
[[nodiscard]] CycleOutcome run_cycle(const CycleRequest& request);

}  // namespace apogee::training
