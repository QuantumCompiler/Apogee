#pragma once

#include <nlohmann/json_fwd.hpp>

#include <optional>
#include <string>

/// The training track's reads the command line and the control plane share
/// (37f, the 28h idiom): `train status --output-format json` prints the very
/// body `GET /v1/admin/training/status` serves, and `train versions
/// --output-format json` the very body of `GET /v1/admin/training/versions`.
/// Here rather than in either surface, so neither includes the other. Each
/// reads the filesystem the CLI writes, under the layout's `training/` row.
namespace apogee::operations {

/// `{"runs", "running": [ids], "recent_runs": [the newest five runs' summaries],
/// "versions": [{backend, active_version, kept, total}], "pipelines",
/// "active_pipeline": id | null, "latest_pipeline": summary | null,
/// "cycle_active", "cycle": {backend, halted, halt_reason, consecutive_fails,
/// anchor_version, anchor_score, total_runs} | null}`.
[[nodiscard]] nlohmann::json training_status_document();

/// Every version ledger, `{"object": "list", "data": [ledger]}`; with
/// `backend`, that backend's ledger alone, or none when it has no history.
[[nodiscard]] std::optional<nlohmann::json> training_versions_document(
    const std::string& backend = {});

}  // namespace apogee::operations
