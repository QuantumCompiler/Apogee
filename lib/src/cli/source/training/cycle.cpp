#include "training/cycle.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <ranges>
#include <stdexcept>
#include <system_error>
#include <utility>

#include "harness/config_edit.h"
#include "platform/platform.h"

namespace apogee::training {
namespace {

std::string percent_of(double score) {
    return std::to_string(static_cast<int>(std::lround(score * 100.0))) + "%";
}

/// The anchor the gate compares against: the config's pinned version with
/// the score the history recorded for it, else the history's own.
struct Anchor {
    int version = 0;
    double score = 0.0;
};

Anchor effective_anchor(const CycleHistory& history, const harness::CycleConfig& config) {
    if (config.anchor_version <= 0) {
        return Anchor{.version = history.anchor_version, .score = history.anchor_score};
    }
    Anchor anchor{.version = config.anchor_version};
    for (const CycleRunRecord& run : history.runs) {
        if (run.gate == kGatePass && run.promoted_version == config.anchor_version) {
            anchor.score = run.cycle_score;
        }
    }
    return anchor;
}

}  // namespace

nlohmann::json history_to_json(const CycleHistory& history) {
    nlohmann::json runs = nlohmann::json::array();
    for (const CycleRunRecord& run : history.runs) {
        nlohmann::json row{{"run_at", run.run_at},
                           {"source", run.source},
                           {"dataset_rows", run.dataset_rows},
                           {"pipeline_id", run.pipeline_id},
                           {"gate", run.gate},
                           {"gate_reason", run.gate_reason},
                           {"cycle_score", run.cycle_score},
                           {"prev_score", run.prev_score},
                           {"anchor_score", run.anchor_score},
                           {"promoted_version", run.promoted_version}};
        if (!run.note.empty()) {
            row["note"] = run.note;
        }
        runs.push_back(std::move(row));
    }
    return nlohmann::json{{"backend", history.backend},
                          {"anchor_version", history.anchor_version},
                          {"anchor_score", history.anchor_score},
                          {"consecutive_fails", history.consecutive_fails},
                          {"total_runs", history.total_runs},
                          {"halted", history.halted},
                          {"halt_reason", history.halt_reason},
                          {"sessions_until", history.sessions_until},
                          {"runs", std::move(runs)}};
}

CycleHistory history_from_json(const nlohmann::json& json) {
    if (!json.is_object()) {
        throw std::runtime_error("cycle history: expected an object");
    }
    CycleHistory history;
    history.backend = json.value("backend", std::string{});
    history.anchor_version = json.value("anchor_version", 0);
    history.anchor_score = json.value("anchor_score", 0.0);
    history.consecutive_fails = json.value("consecutive_fails", 0);
    history.total_runs = json.value("total_runs", 0);
    history.halted = json.value("halted", false);
    history.halt_reason = json.value("halt_reason", std::string{});
    history.sessions_until = json.value("sessions_until", std::string{});
    if (const auto runs = json.find("runs"); runs != json.end() && !runs->is_null()) {
        if (!runs->is_array()) {
            throw std::runtime_error("cycle history: runs must be an array");
        }
        for (const nlohmann::json& row : *runs) {
            if (!row.is_object()) {
                throw std::runtime_error("cycle history: a run must be an object");
            }
            CycleRunRecord run;
            run.run_at = row.value("run_at", std::string{});
            run.source = row.value("source", std::string{});
            run.dataset_rows = row.value("dataset_rows", 0);
            run.pipeline_id = row.value("pipeline_id", std::string{});
            run.gate = row.value("gate", std::string{});
            run.gate_reason = row.value("gate_reason", std::string{});
            run.cycle_score = row.value("cycle_score", 0.0);
            run.prev_score = row.value("prev_score", 0.0);
            run.anchor_score = row.value("anchor_score", 0.0);
            run.promoted_version = row.value("promoted_version", 0);
            run.note = row.value("note", std::string{});
            history.runs.push_back(std::move(run));
        }
    }
    return history;
}

std::filesystem::path history_path(const std::filesystem::path& cycle_dir) {
    return cycle_dir / kHistoryFileName;
}

CycleHistory load_history(const std::filesystem::path& cycle_dir, std::string_view backend,
                          std::string& error) {
    error.clear();
    CycleHistory fresh;
    fresh.backend = std::string{backend};
    const std::filesystem::path path = history_path(cycle_dir);
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        return fresh;
    }
    const nlohmann::json json = nlohmann::json::parse(in, nullptr, false);
    if (json.is_discarded()) {
        error = path.string() + " is not JSON";
        return fresh;
    }
    try {
        CycleHistory history = history_from_json(json);
        if (history.backend.empty()) {
            history.backend = std::string{backend};
        }
        return history;
    } catch (const std::runtime_error& e) {
        error = path.string() + ": " + e.what();
        return fresh;
    }
}

bool history_exists(const std::filesystem::path& cycle_dir) {
    std::error_code code;
    return std::filesystem::is_regular_file(history_path(cycle_dir), code);
}

std::string save_history(const std::filesystem::path& cycle_dir, const CycleHistory& history) {
    std::error_code code;
    std::filesystem::create_directories(cycle_dir, code);
    if (code) {
        return "could not create " + cycle_dir.string() + ": " + code.message();
    }
    try {
        harness::write_file_atomically(history_path(cycle_dir),
                                       history_to_json(history).dump(2) + "\n");
    } catch (const std::exception& e) {
        return e.what();
    }
    return {};
}

// --- the lock -----------------------------------------------------------------

CycleLock::CycleLock(std::filesystem::path path) : path_{std::move(path)} {}

CycleLock::~CycleLock() {
    release();
}

CycleLock::CycleLock(CycleLock&& other) noexcept : path_{std::move(other.path_)} {
    other.path_.clear();
}

CycleLock& CycleLock::operator=(CycleLock&& other) noexcept {
    if (this != &other) {
        release();
        path_ = std::move(other.path_);
        other.path_.clear();
    }
    return *this;
}

std::optional<CycleLock> CycleLock::acquire(const std::filesystem::path& cycle_dir,
                                            std::string& error) {
    error.clear();
    std::error_code code;
    std::filesystem::create_directories(cycle_dir, code);
    if (code) {
        error = "could not create " + cycle_dir.string() + ": " + code.message();
        return std::nullopt;
    }
    const std::filesystem::path path = cycle_dir / kLockFileName;
    // One atomic create-or-fail through the platform seam, so two schedulers
    // firing at once cannot both win.
    bool exists = false;
    if (!platform::create_exclusive_file(
            path, std::to_string(platform::current_process_id()) + "\n", exists)) {
        if (exists) {
            error = "another cycle is already running (lock: " + path.string() +
                    "). If none is, remove the file and run again";
        } else {
            error = "could not create the lock " + path.string();
        }
        return std::nullopt;
    }
    return CycleLock{path};
}

void CycleLock::release() noexcept {
    if (path_.empty()) {
        return;
    }
    std::error_code code;
    std::filesystem::remove(path_, code);
    path_.clear();
}

bool cycle_lock_held(const std::filesystem::path& cycle_dir) {
    std::error_code code;
    return std::filesystem::exists(cycle_dir / kLockFileName, code);
}

// --- the breaker ----------------------------------------------------------------

std::string check_circuit_breaker(const CycleHistory& history, int k) {
    if (history.halted) {
        return "the cycle is halted: " +
               (history.halt_reason.empty() ? std::string{"'apogee train cycle halt' was run"}
                                            : history.halt_reason) +
               ". Review the data and the suite, then 'apogee train cycle resume'";
    }
    if (k > 0 && history.consecutive_fails >= k) {
        return "circuit breaker: " + std::to_string(history.consecutive_fails) +
               " consecutive failed cycle(s) (circuit_breaker_k " + std::to_string(k) +
               "). Review the data source and the eval suite, then 'apogee train cycle "
               "resume'";
    }
    return {};
}

void halt_cycle(CycleHistory& history, std::string reason) {
    history.halted = true;
    history.halt_reason = std::move(reason);
}

bool resume_cycle(CycleHistory& history) {
    const bool was_halted = history.halted;
    history.halted = false;
    history.halt_reason.clear();
    history.consecutive_fails = 0;
    return was_halted;
}

// --- the sources ----------------------------------------------------------------

DirectoryCollection collect_directory(const std::filesystem::path& dir) {
    DirectoryCollection collection;
    std::error_code code;
    std::filesystem::create_directories(dir, code);
    if (code) {
        collection.error = "could not create " + dir.string() + ": " + code.message();
        return collection;
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir, code)) {
        if (entry.is_regular_file(code) && entry.path().extension() == ".jsonl") {
            collection.files.push_back(entry.path());
        }
    }
    if (code) {
        collection.error = "could not read " + dir.string() + ": " + code.message();
        return collection;
    }
    std::ranges::sort(collection.files);
    return collection;
}

std::string consume_directory_files(const std::vector<std::filesystem::path>& files,
                                    const std::filesystem::path& queue_dir) {
    const std::filesystem::path consumed = queue_dir / kConsumedDirName;
    std::error_code code;
    std::filesystem::create_directories(consumed, code);
    if (code) {
        return "could not create " + consumed.string() + ": " + code.message();
    }
    for (const std::filesystem::path& file : files) {
        const std::filesystem::path target = consumed / file.filename();
        std::filesystem::rename(file, target, code);
        if (code) {
            return "could not move " + file.string() + " to " + target.string() + ": " +
                   code.message();
        }
    }
    return {};
}

SessionCollection collect_sessions(const std::vector<SessionView>& views,
                                   const harness::CycleSourceConfig& source,
                                   std::string_view watermark) {
    SessionCollection collection;
    if (!source.log_consent) {
        collection.error =
            "the sessions source needs explicit consent: set log_consent: true on it under "
            "training.cycle.sources. Two risks to read first -- your chat sessions are your "
            "own data (privacy), and training a model on its own answers reinforces its "
            "mistakes (self-reinforcement)";
        return collection;
    }
    SessionFilter filter;
    filter.backend = source.backend;
    filter.since = source.since;
    if (const std::string problem = validate_session_filter(filter); !problem.empty()) {
        collection.error = problem;
        return collection;
    }
    std::vector<SessionView> fresh;
    for (const SessionView& view : views) {
        if (!watermark.empty() && view.started_at <= watermark) {
            continue;
        }
        fresh.push_back(view);
        if (view.started_at > collection.newest_started_at) {
            collection.newest_started_at = view.started_at;
        }
    }
    const MinedSessions mined = mine_sessions(fresh, filter);
    collection.lines = mined.lines;
    collection.sessions = mined.sessions;
    collection.skipped = mined.skipped;
    return collection;
}

int merge_datasets(const std::vector<std::filesystem::path>& sources,
                   const std::filesystem::path& out, std::string& error) {
    error.clear();
    std::string text;
    int rows = 0;
    for (const std::filesystem::path& source : sources) {
        std::ifstream in{source, std::ios::binary};
        if (!in) {
            error = "cannot read " + source.string();
            return -1;
        }
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.find_first_not_of(" \t") == std::string::npos) {
                continue;
            }
            text += line;
            text += '\n';
            ++rows;
        }
    }
    std::error_code code;
    std::filesystem::create_directories(out.parent_path(), code);
    if (code) {
        error = "could not create " + out.parent_path().string() + ": " + code.message();
        return -1;
    }
    try {
        harness::write_file_atomically(out, text);
    } catch (const std::exception& e) {
        error = e.what();
        return -1;
    }
    return rows;
}

// --- the gate -------------------------------------------------------------------

AnchorGate anchor_gate_pass(double current, double prev, double anchor, double threshold) noexcept {
    return AnchorGate{.prev_ok = prev == 0.0 || current >= prev - threshold,
                      .anchor_ok = anchor == 0.0 || current >= anchor - threshold};
}

double last_passing_cycle_score(const CycleHistory& history) noexcept {
    for (const CycleRunRecord& run : std::views::reverse(history.runs)) {
        if (run.gate == kGatePass) {
            return run.cycle_score;
        }
    }
    return 0.0;
}

void record_cycle_run(CycleHistory& history, CycleRunRecord record) {
    ++history.total_runs;
    if (record.gate == kGateFail) {
        ++history.consecutive_fails;
    } else if (record.gate == kGatePass) {
        history.consecutive_fails = 0;
    }
    history.runs.push_back(std::move(record));
}

bool trip_breaker(CycleHistory& history, int k) {
    if (k <= 0 || history.consecutive_fails < k) {
        return false;
    }
    halt_cycle(history, "circuit breaker: " + std::to_string(history.consecutive_fails) +
                            " consecutive failed cycle(s) reached circuit_breaker_k " +
                            std::to_string(k));
    return true;
}

// --- one pass -------------------------------------------------------------------

CycleOutcome run_cycle(const CycleRequest& request) {
    CycleOutcome outcome;
    if (request.spec == nullptr || request.trainer == nullptr || !request.promote) {
        outcome.error = request.spec == nullptr      ? "no pipeline"
                        : request.trainer == nullptr ? "no trainer"
                                                     : "no promote path";
        return outcome;
    }
    if (request.stages.size() != request.spec->stages.size()) {
        outcome.error = "the pipeline has " + std::to_string(request.spec->stages.size()) +
                        " stage(s) but " + std::to_string(request.stages.size()) +
                        " suites were resolved";
        return outcome;
    }
    auto say = [&request](const std::string& text) {
        if (request.on_message) {
            request.on_message(text);
        }
    };

    std::string lock_error;
    std::optional<CycleLock> lock = CycleLock::acquire(request.cycle_dir, lock_error);
    if (!lock.has_value()) {
        outcome.error = lock_error;
        return outcome;
    }
    std::string history_error;
    CycleHistory history = load_history(request.cycle_dir, request.config.backend, history_error);
    if (!history_error.empty()) {
        outcome.error = "the cycle history is unreadable: " + history_error;
        return outcome;
    }
    if (const std::string refusal =
            check_circuit_breaker(history, request.config.circuit_breaker_k);
        !refusal.empty()) {
        outcome.error = refusal;
        outcome.history = history;
        return outcome;
    }
    const std::string run_at = rfc3339_now();
    const std::filesystem::path work = request.cycle_dir / kWorkDirName;
    auto save = [&](const std::string& what) {
        if (const std::string failure = save_history(request.cycle_dir, history);
            !failure.empty()) {
            say("[cycle] warning: could not save the history after " + what + ": " + failure);
        }
    };

    // --- collect ------------------------------------------------------------
    std::vector<std::filesystem::path> collected;
    std::vector<std::pair<std::filesystem::path, std::vector<std::filesystem::path>>> queues;
    std::vector<std::string> types;
    std::string newest_session;
    bool override_used = false;
    int index = 0;
    for (const harness::CycleSourceConfig& source : request.config.sources) {
        if (source.type == "directory") {
            std::filesystem::path dir = source.dir.empty() ? request.cycle_dir / kQueueDirName
                                                           : std::filesystem::path{source.dir};
            if (!request.source_override.empty() && !override_used) {
                dir = request.source_override;
                override_used = true;
            }
            const DirectoryCollection found = collect_directory(dir);
            if (!found.error.empty()) {
                outcome.error = "directory source: " + found.error;
                return outcome;
            }
            if (found.files.empty()) {
                say("[cycle] directory: no new files in " + dir.string());
            } else {
                say("[cycle] directory: " + std::to_string(found.files.size()) + " file(s) in " +
                    dir.string());
                collected.insert(collected.end(), found.files.begin(), found.files.end());
                queues.emplace_back(dir, found.files);
                types.emplace_back("directory");
            }
        } else if (source.type == "sessions") {
            const SessionCollection mined =
                collect_sessions(request.sessions, source, history.sessions_until);
            if (!mined.error.empty()) {
                outcome.error = "sessions source: " + mined.error;
                return outcome;
            }
            if (mined.newest_started_at > newest_session) {
                newest_session = mined.newest_started_at;
            }
            if (mined.lines.empty()) {
                say("[cycle] sessions: no new sessions since " + (history.sessions_until.empty()
                                                                      ? std::string{"the beginning"}
                                                                      : history.sessions_until));
            } else {
                const std::filesystem::path out =
                    work / ("sessions-" + std::to_string(index) + ".jsonl");
                std::string text;
                for (const std::string& line : mined.lines) {
                    text += line;
                    text += '\n';
                }
                std::error_code code;
                std::filesystem::create_directories(work, code);
                try {
                    harness::write_file_atomically(out, text);
                } catch (const std::exception& e) {
                    outcome.error = std::string{"sessions source: "} + e.what();
                    return outcome;
                }
                say("[cycle] sessions: " + std::to_string(mined.lines.size()) +
                    " exchange(s) from " + std::to_string(mined.sessions) + " session(s)");
                collected.push_back(out);
                types.emplace_back("sessions");
            }
        } else {
            outcome.error = "unknown source type '" + source.type + "'";
            return outcome;
        }
        ++index;
    }
    std::string source_label;
    for (const std::string& type : types) {
        source_label += (source_label.empty() ? "" : "+") + type;
    }

    // --- no data: skipped, and no failure counted ---------------------------
    CycleRunRecord record;
    record.run_at = run_at;
    record.source = source_label;
    if (collected.empty()) {
        record.gate = std::string{kGateSkipped};
        record.gate_reason = "no new training data in any source";
        record_cycle_run(history, record);
        if (!newest_session.empty()) {
            history.sessions_until = newest_session;
        }
        save("the skip");
        say("[cycle] no new data -- skipped");
        outcome.ok = true;
        outcome.record = record;
        outcome.history = history;
        return outcome;
    }

    // --- merge --------------------------------------------------------------
    const std::filesystem::path merged = work / "merged.jsonl";
    std::string merge_error;
    const int rows = merge_datasets(collected, merged, merge_error);
    if (rows < 0) {
        outcome.error = "merging the sources: " + merge_error;
        return outcome;
    }
    record.dataset_rows = rows;
    say("[cycle] " + std::to_string(rows) + " example(s) from " + source_label);

    // --- the pipeline, every stage on the merged dataset -----------------------
    harness::PipelineSpec spec = *request.spec;
    std::vector<ResolvedStage> stages = request.stages;
    for (std::size_t i = 0; i < spec.stages.size(); ++i) {
        spec.stages[i].dataset = merged.string();
        stages[i].dataset = merged;
    }
    PipelineRequest pipeline;
    pipeline.spec = &spec;
    pipeline.stages = std::move(stages);
    pipeline.pipeline_run_id = new_run_id(request.training_dir / kPipelinesDirName,
                                          std::chrono::system_clock::now(), kCycleIdPrefix);
    pipeline.base_model = request.base_model;
    pipeline.training_dir = request.training_dir;
    pipeline.trainer = request.trainer;
    pipeline.judge_backend = request.judge_backend;
    pipeline.judge = request.judge;
    pipeline.gate_mode = request.gate_mode;
    pipeline.continue_on_fail = false;
    pipeline.on_progress = request.on_progress;
    pipeline.cancellation = request.cancellation;
    record.pipeline_id = pipeline.pipeline_run_id;
    say("[cycle] pipeline " + pipeline.pipeline_run_id + " (" + spec.name +
        "): " + std::to_string(spec.stages.size()) + " stage(s)");
    const PipelineOutcome ran = run_pipeline(pipeline);
    if (ran.cancelled) {
        outcome.cancelled = true;
        outcome.error = "cancelled";
        outcome.history = history;
        return outcome;
    }

    auto fail = [&](std::string reason) {
        record.gate = std::string{kGateFail};
        record.gate_reason = std::move(reason);
        record_cycle_run(history, record);
        outcome.halted_now = trip_breaker(history, request.config.circuit_breaker_k);
        if (outcome.halted_now) {
            history.runs.back().note = history.halt_reason;
        }
        save("the failure");
        say("[cycle] FAILED -- " + history.runs.back().gate_reason);
        if (outcome.halted_now) {
            say("[cycle] " + history.halt_reason +
                " -- the loop is halted; 'apogee train cycle "
                "resume' after fixing the data or the suite");
        }
        outcome.ok = true;
        outcome.record = history.runs.back();
        outcome.history = history;
        return outcome;
    };
    if (!ran.ok) {
        return fail("pipeline " + ran.manifest.status +
                    (ran.error.empty() ? std::string{} : ": " + ran.error));
    }
    // The candidate is the FINAL stage, and its cumulative score is the
    // cycle's. Not the last *passed* stage: a passed stage scored 100% by
    // the gate's own definition, so a score read from there can never
    // regress and the dual gate below would be dead code -- as it was in
    // the reference. Under the hard gate a complete pipeline has every
    // stage passed and the two readings agree; under `gate_mode: soft` the
    // per-stage gate is advisory and THIS gate is the one that holds.
    if (ran.manifest.stages.empty() || !ran.manifest.stages.back().eval.has_value()) {
        return fail("the pipeline completed without evaluating its final stage");
    }
    const PipelineStageRecord& final_stage = ran.manifest.stages.back();
    record.cycle_score = final_stage.cumulative_score;
    record.prev_score = last_passing_cycle_score(history);
    const Anchor anchor = effective_anchor(history, request.config);
    record.anchor_score = anchor.score;
    const double threshold = request.config.regression_threshold;
    say("[cycle] gate -- current " + percent_of(record.cycle_score) + ", previous " +
        percent_of(record.prev_score) + ", anchor " + percent_of(anchor.score) +
        (anchor.version > 0 ? " (v" + std::to_string(anchor.version) + ")" : "") + ", threshold " +
        percent_of(threshold));
    const AnchorGate gate =
        anchor_gate_pass(record.cycle_score, record.prev_score, anchor.score, threshold);
    if (!gate.pass()) {
        std::string reason;
        if (!gate.prev_ok) {
            reason = "regressed against the previous passing cycle (" +
                     percent_of(record.cycle_score) + " < " + percent_of(record.prev_score) +
                     " - " + percent_of(threshold) + ")";
        }
        if (!gate.anchor_ok) {
            reason += (reason.empty() ? "" : "; ");
            reason += "regressed against anchor v" + std::to_string(anchor.version) + " (" +
                      percent_of(record.cycle_score) + " < " + percent_of(anchor.score) + " - " +
                      percent_of(threshold) + ")";
        }
        return fail(reason);
    }

    // --- promote, consume, record --------------------------------------------
    say("[cycle] gate PASSED (" + percent_of(record.cycle_score) + ") -- promoting " +
        final_stage.run_id + " into " + request.config.backend);
    const CyclePromotion promoted = request.promote(final_stage.run_id);
    if (!promoted.ok) {
        return fail("promote: " + promoted.error);
    }
    for (const auto& [queue, files] : queues) {
        if (const std::string failure = consume_directory_files(files, queue); !failure.empty()) {
            say("[cycle] warning: could not move the consumed files: " + failure);
        }
    }
    if (!newest_session.empty()) {
        history.sessions_until = newest_session;
    }
    if (history.anchor_version == 0 && request.config.anchor_version <= 0) {
        history.anchor_version = promoted.version;
        history.anchor_score = record.cycle_score;
        say("[cycle] anchor set to v" + std::to_string(promoted.version) + " (" +
            percent_of(record.cycle_score) + ")");
    } else if (request.config.anchor_version > 0) {
        history.anchor_version = anchor.version;
        history.anchor_score = anchor.score;
    }
    record.gate = std::string{kGatePass};
    record.promoted_version = promoted.version;
    record_cycle_run(history, record);
    save("the pass");
    say("[cycle] complete -- " + request.config.backend + " promoted to v" +
        std::to_string(promoted.version) + " (" + promoted.gguf_path + ")");
    outcome.ok = true;
    outcome.record = record;
    outcome.history = history;
    return outcome;
}

}  // namespace apogee::training
