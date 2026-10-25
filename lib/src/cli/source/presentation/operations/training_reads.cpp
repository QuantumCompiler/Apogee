#include "operations/training_reads.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "contracts/layout.h"
#include "training/cycle.h"
#include "training/manifest.h"
#include "training/store.h"

namespace apogee::operations {

namespace {

/// How many of the newest runs the status names, as `train status` lists them.
constexpr std::size_t kRecentRuns = 5;

}  // namespace

nlohmann::json training_status_document() {
    const training::TrainingStore reads{harness::training_dir()};
    nlohmann::json running = nlohmann::json::array();
    nlohmann::json recent = nlohmann::json::array();
    const std::vector<training::RunSummary> runs = reads.list_runs();
    for (const training::RunSummary& run : runs) {
        if (run.status == training::kStatusRunning) {
            running.push_back(run.id);
        }
        if (recent.size() < kRecentRuns) {
            recent.push_back(training::run_summary_json(run));
        }
    }
    nlohmann::json versions = nlohmann::json::array();
    for (const training::VersionLedger& ledger : reads.all_versions()) {
        versions.push_back({{"backend", ledger.backend},
                            {"active_version", ledger.active_version},
                            {"kept", ledger.kept()},
                            {"total", ledger.versions.size()}});
    }
    const std::vector<training::PipelineSummary> pipelines = reads.list_pipelines();
    nlohmann::json active_pipeline = nullptr;
    for (const training::PipelineSummary& pipeline : pipelines) {
        if (pipeline.status == training::kPipelineRunning) {
            active_pipeline = pipeline.id;
            break;
        }
    }
    const std::filesystem::path cycle_dir = harness::training_cycle_dir();
    nlohmann::json cycle = nullptr;
    if (training::history_exists(cycle_dir)) {
        std::string error;
        const training::CycleHistory history = training::load_history(cycle_dir, {}, error);
        if (error.empty()) {
            cycle = nlohmann::json{{"backend", history.backend},
                                   {"halted", history.halted},
                                   {"halt_reason", history.halt_reason},
                                   {"consecutive_fails", history.consecutive_fails},
                                   {"anchor_version", history.anchor_version},
                                   {"anchor_score", history.anchor_score},
                                   {"total_runs", history.total_runs}};
        }
    }
    return nlohmann::json{
        {"runs", runs.size()},
        {"running", std::move(running)},
        {"recent_runs", std::move(recent)},
        {"versions", std::move(versions)},
        {"pipelines", pipelines.size()},
        {"active_pipeline", std::move(active_pipeline)},
        {"latest_pipeline", pipelines.empty() ? nlohmann::json(nullptr)
                                              : training::pipeline_summary_json(pipelines.front())},
        {"cycle_active", training::cycle_lock_held(cycle_dir)},
        {"cycle", std::move(cycle)}};
}

std::optional<nlohmann::json> training_versions_document(const std::string& backend) {
    const training::TrainingStore reads{harness::training_dir()};
    if (!backend.empty()) {
        const std::optional<training::VersionLedger> ledger = reads.list_versions(backend);
        if (!ledger.has_value()) {
            return std::nullopt;
        }
        return training::ledger_to_json(*ledger);
    }
    nlohmann::json data = nlohmann::json::array();
    for (const training::VersionLedger& ledger : reads.all_versions()) {
        data.push_back(training::ledger_to_json(ledger));
    }
    return nlohmann::json{{"object", "list"}, {"data", std::move(data)}};
}

nlohmann::json training_runs_document(std::string_view kind) {
    // Both kinds, newest first by start time, each row tagged with its
    // kind; the filter keeps one.
    std::vector<std::pair<std::string, nlohmann::json>> rows;
    const training::TrainingStore reads{harness::training_dir()};
    if (kind != "pipeline") {
        for (const training::RunSummary& run : reads.list_runs()) {
            rows.emplace_back(run.started_at + "\n" + run.id, training::run_summary_json(run));
        }
    }
    if (kind != "run") {
        for (const training::PipelineSummary& pipeline : reads.list_pipelines()) {
            rows.emplace_back(pipeline.started_at + "\n" + pipeline.id,
                              training::pipeline_summary_json(pipeline));
        }
    }
    std::stable_sort(rows.begin(), rows.end(),
                     [](const auto& a, const auto& b) { return a.first > b.first; });
    nlohmann::json data = nlohmann::json::array();
    for (auto& [unused, row] : rows) {
        data.push_back(std::move(row));
    }
    return nlohmann::json{{"object", "list"}, {"data", std::move(data)}};
}

std::optional<nlohmann::json> training_run_document(std::string_view id) {
    const training::TrainingStore reads{harness::training_dir()};
    if (const std::optional<training::RunManifest> manifest = reads.get_run(id);
        manifest.has_value()) {
        return nlohmann::json{{"kind", "run"}, {"run", training::manifest_to_json(*manifest)}};
    }
    return training_pipeline_document(id);
}

std::optional<nlohmann::json> training_pipeline_document(std::string_view id) {
    const training::TrainingStore reads{harness::training_dir()};
    const std::optional<training::PipelineRunManifest> pipeline = reads.get_pipeline(id);
    if (!pipeline.has_value()) {
        return std::nullopt;
    }
    return nlohmann::json{{"kind", "pipeline"},
                          {"pipeline", training::pipeline_manifest_to_json(*pipeline)}};
}

std::optional<nlohmann::json> training_cycle_document(std::string& error) {
    error.clear();
    const std::filesystem::path cycle_dir = harness::training_cycle_dir();
    if (!training::history_exists(cycle_dir)) {
        return std::nullopt;
    }
    const training::CycleHistory history = training::load_history(cycle_dir, {}, error);
    if (!error.empty()) {
        return std::nullopt;
    }
    nlohmann::json out = training::history_to_json(history);
    out["active"] = training::cycle_lock_held(cycle_dir);
    return out;
}

}  // namespace apogee::operations
