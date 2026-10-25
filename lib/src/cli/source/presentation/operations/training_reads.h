#pragma once

#include <nlohmann/json_fwd.hpp>

#include <optional>
#include <string>
#include <string_view>

/// The training track's reads the command line and the control plane share
/// (37f, the 28h idiom): `train status --output-format json` prints the very
/// body `GET /v1/admin/training/status` serves, and `train versions
/// --output-format json` the very body of `GET /v1/admin/training/versions`
/// -- and since 55h `train pipeline status` and `train cycle status` the runs
/// route's and the cycle route's.
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

/// Every run's and every pipeline run's summary, newest first by start time,
/// each tagged with its `kind`, `{"object": "list", "data": [summary]}` --
/// `GET /v1/admin/training/runs`'s body; `kind` `run` or `pipeline` keeps
/// one (55h: `train pipeline status --output-format json` is the pipelines').
/// The caller refuses any other kind.
[[nodiscard]] nlohmann::json training_runs_document(std::string_view kind = {});

/// One run's manifest, `{"kind": "run", "run": manifest}`, or one pipeline
/// run's, as `training_pipeline_document` -- `GET
/// /v1/admin/training/runs/{id}`'s body; none when neither. The caller
/// refuses an id that is not a plain name (`training::valid_run_id`).
[[nodiscard]] std::optional<nlohmann::json> training_run_document(std::string_view id);

/// One pipeline run's manifest, `{"kind": "pipeline", "pipeline":
/// manifest}`, or none (55h): `train pipeline status <id> --output-format
/// json`, the very body the runs route serves for a pipeline's id.
[[nodiscard]] std::optional<nlohmann::json> training_pipeline_document(std::string_view id);

/// The cycle's history with `active`, whether a pass holds the lock now --
/// `GET /v1/admin/training/cycle`'s body and `train cycle status
/// --output-format json` (55h). None until the first pass has written the
/// history, and none with `error` set when the history cannot be read.
[[nodiscard]] std::optional<nlohmann::json> training_cycle_document(std::string& error);

}  // namespace apogee::operations
