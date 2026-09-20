#include "httpserver/admin_training.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

#include "harness/layout.h"
#include "training/manifest.h"
#include "training/store.h"

namespace apogee::httpserver {
namespace {

[[nodiscard]] training::TrainingStore store() {
    return training::TrainingStore{harness::training_dir()};
}

}  // namespace

HttpResponse admin_training_status(const AdminConfigContext& /*context*/) {
    const training::TrainingStore reads = store();
    nlohmann::json running = nlohmann::json::array();
    const std::vector<training::RunSummary> runs = reads.list_runs();
    for (const training::RunSummary& run : runs) {
        if (run.status == training::kStatusRunning) {
            running.push_back(run.id);
        }
    }
    nlohmann::json versions = nlohmann::json::array();
    for (const training::VersionLedger& ledger : reads.all_versions()) {
        versions.push_back({{"backend", ledger.backend},
                            {"active_version", ledger.active_version},
                            {"kept", ledger.kept()},
                            {"total", ledger.versions.size()}});
    }
    return json_response(200, nlohmann::json{{"runs", runs.size()},
                                             {"running", std::move(running)},
                                             {"versions", std::move(versions)},
                                             {"active_pipeline", nullptr},
                                             {"cycle_active", false}});
}

HttpResponse admin_list_training_runs(const AdminConfigContext& /*context*/,
                                      const HttpRequest& request) {
    const std::string kind = request.query_value("kind");
    if (!kind.empty() && kind != "run" && kind != "pipeline") {
        return error_response(400, "kind must be run or pipeline");
    }
    nlohmann::json data = nlohmann::json::array();
    if (kind != "pipeline") {
        for (const training::RunSummary& run : store().list_runs()) {
            data.push_back(training::run_summary_json(run));
        }
    }
    return json_response(200, nlohmann::json{{"object", "list"}, {"data", std::move(data)}});
}

HttpResponse admin_get_training_run(const AdminConfigContext& /*context*/, std::string_view id) {
    if (!training::valid_run_id(id)) {
        return error_response(400, "'" + std::string{id} + "' is not a run id");
    }
    const std::optional<training::RunManifest> manifest = store().get_run(id);
    if (!manifest.has_value()) {
        return error_response(404, "no run '" + std::string{id} + "'");
    }
    return json_response(
        200, nlohmann::json{{"kind", "run"}, {"run", training::manifest_to_json(*manifest)}});
}

HttpResponse admin_list_training_versions(const AdminConfigContext& /*context*/,
                                          const HttpRequest& request) {
    const std::string backend = request.query_value("backend");
    if (!backend.empty()) {
        const std::optional<training::VersionLedger> ledger = store().list_versions(backend);
        if (!ledger.has_value()) {
            return error_response(404, "no version history for backend '" + backend + "'");
        }
        return json_response(200, training::ledger_to_json(*ledger));
    }
    nlohmann::json data = nlohmann::json::array();
    for (const training::VersionLedger& ledger : store().all_versions()) {
        data.push_back(training::ledger_to_json(ledger));
    }
    return json_response(200, nlohmann::json{{"object", "list"}, {"data", std::move(data)}});
}

}  // namespace apogee::httpserver
