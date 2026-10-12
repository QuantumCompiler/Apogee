#include "httpserver/admin_training.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "contracts/layout.h"
#include "operations/training_reads.h"
#include "training/cycle.h"
#include "training/manifest.h"
#include "training/store.h"

namespace apogee::httpserver {
namespace {

[[nodiscard]] training::TrainingStore store() {
    return training::TrainingStore{harness::training_dir()};
}

}  // namespace

HttpResponse admin_training_status(const AdminConfigContext& /*context*/) {
    // The CLI's `train status --output-format json`, byte for byte (37f).
    return json_response(200, operations::training_status_document());
}

HttpResponse admin_list_training_runs(const AdminConfigContext& /*context*/,
                                      const HttpRequest& request) {
    const std::string kind = request.query_value("kind");
    if (!kind.empty() && kind != "run" && kind != "pipeline") {
        return error_response(400, "kind must be run or pipeline");
    }
    // Both kinds, newest first by start time, each row tagged with its
    // kind; the filter keeps one.
    std::vector<std::pair<std::string, nlohmann::json>> rows;
    const training::TrainingStore reads = store();
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
    return json_response(200, nlohmann::json{{"object", "list"}, {"data", std::move(data)}});
}

HttpResponse admin_get_training_run(const AdminConfigContext& /*context*/, std::string_view id) {
    if (!training::valid_run_id(id)) {
        return error_response(400, "'" + std::string{id} + "' is not a run id");
    }
    const training::TrainingStore reads = store();
    if (const std::optional<training::RunManifest> manifest = reads.get_run(id);
        manifest.has_value()) {
        return json_response(
            200, nlohmann::json{{"kind", "run"}, {"run", training::manifest_to_json(*manifest)}});
    }
    if (const std::optional<training::PipelineRunManifest> pipeline = reads.get_pipeline(id);
        pipeline.has_value()) {
        return json_response(
            200, nlohmann::json{{"kind", "pipeline"},
                                {"pipeline", training::pipeline_manifest_to_json(*pipeline)}});
    }
    return error_response(404, "no run or pipeline '" + std::string{id} + "'");
}

HttpResponse admin_training_cycle(const AdminConfigContext& /*context*/) {
    const std::filesystem::path cycle_dir = harness::training_cycle_dir();
    if (!training::history_exists(cycle_dir)) {
        return error_response(404, "no cycle history yet ('apogee train cycle run' writes it)");
    }
    std::string error;
    const training::CycleHistory history = training::load_history(cycle_dir, {}, error);
    if (!error.empty()) {
        return error_response(500, "the cycle history is unreadable: " + error);
    }
    nlohmann::json out = training::history_to_json(history);
    out["active"] = training::cycle_lock_held(cycle_dir);
    return json_response(200, std::move(out));
}

HttpResponse admin_list_training_versions(const AdminConfigContext& /*context*/,
                                          const HttpRequest& request) {
    // The CLI's `train versions --output-format json`, byte for byte (37f).
    const std::string backend = request.query_value("backend");
    std::optional<nlohmann::json> document = operations::training_versions_document(backend);
    if (!document.has_value()) {
        return error_response(404, "no version history for backend '" + backend + "'");
    }
    return json_response(200, std::move(*document));
}

}  // namespace apogee::httpserver
