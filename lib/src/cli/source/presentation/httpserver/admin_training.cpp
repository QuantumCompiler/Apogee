#include "httpserver/admin_training.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <utility>

#include "operations/training_reads.h"
#include "training/manifest.h"

namespace apogee::httpserver {

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
    // With `kind=pipeline`, `train pipeline status --output-format json`
    // byte for byte (55h).
    return json_response(200, operations::training_runs_document(kind));
}

HttpResponse admin_get_training_run(const AdminConfigContext& /*context*/, std::string_view id) {
    if (!training::valid_run_id(id)) {
        return error_response(400, "'" + std::string{id} + "' is not a run id");
    }
    // A pipeline's: `train pipeline status <id> --output-format json`,
    // byte for byte (55h).
    std::optional<nlohmann::json> document = operations::training_run_document(id);
    if (!document.has_value()) {
        return error_response(404, "no run or pipeline '" + std::string{id} + "'");
    }
    return json_response(200, std::move(*document));
}

HttpResponse admin_training_cycle(const AdminConfigContext& /*context*/) {
    // `train cycle status --output-format json`, byte for byte (55h).
    std::string error;
    std::optional<nlohmann::json> document = operations::training_cycle_document(error);
    if (!error.empty()) {
        return error_response(500, "the cycle history is unreadable: " + error);
    }
    if (!document.has_value()) {
        return error_response(404, "no cycle history yet ('apogee train cycle run' writes it)");
    }
    return json_response(200, std::move(*document));
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
