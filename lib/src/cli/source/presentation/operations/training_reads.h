#pragma once

#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "contracts/config.h"
#include "training/pipeline.h"

/// The training track's reads the command line and the control plane share
/// (37f, the 28h idiom): `train status --output-format json` prints the very
/// body `GET /v1/admin/training/status` serves, and `train versions
/// --output-format json` the very body of `GET /v1/admin/training/versions`
/// -- and since 55h `train pipeline status` and `train cycle status` the runs
/// route's and the cycle route's. Since M30 the named pipeline specs too,
/// which no route serves.
/// Here rather than in either surface, so neither includes the other. Each
/// reads the filesystem the CLI writes, under the layout's `training/` row.
namespace apogee::operations {

/// `{"runs", "running": [ids], "recent_runs": [the newest five runs' summaries],
/// "versions": [{backend, active_version, kept, total}], "pipelines",
/// "active_pipeline": id | null, "latest_pipeline": summary | null,
/// "cycle_active", "stale_lock", "cycle": {backend, halted, halt_reason,
/// consecutive_fails, anchor_version, anchor_score, total_runs} | null,
/// "cycle_error": reason | null}` -- `cycle` null until the first pass has
/// run, or when the history cannot be read, and then `cycle_error` says why
/// (57h): never an unreadable history presented as none.
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
/// /v1/admin/training/runs/{id}`'s body; none when neither, with `error`
/// set when a pipeline's manifest is there and cannot be read (57h). The
/// caller refuses an id that is not a plain name (`training::valid_run_id`).
[[nodiscard]] std::optional<nlohmann::json> training_run_document(std::string_view id,
                                                                  const harness::Config& config,
                                                                  std::string& error);

/// One pipeline run's manifest, `{"kind": "pipeline", "pipeline": manifest,
/// "holder": {"pid"} | null, "spec_ref": ref | null, "resume": {stage, index,
/// name, pipeline, note} | null}`, or none (55h): `train pipeline status <id>
/// --output-format json`, the very body the runs route serves for a
/// pipeline's id. None with `error` set -- `<path>: <reason>` -- when its
/// manifest is there and cannot be read (57h).
///
/// `resume` (57h) is where a resume starts, as every surface says it: null
/// for a complete run, a cycle pass or a run with no stage left; else
/// `stage` (1-based, as the text says it), `index` (the manifest's own),
/// `name`, `pipeline` -- the `--pipeline` value that finds the run's spec in
/// `config` now (`training::find_resume_spec`), null when none does -- and
/// `note`, the stages before it that failed their gate and stay as they are,
/// in the text's words, or null.
[[nodiscard]] std::optional<nlohmann::json> training_pipeline_document(
    std::string_view id, const harness::Config& config, std::string& error);

/// Where a resume of `manifest` starts and what it leaves, in the words every
/// surface uses (57h): `stage 3, c`. Empty when there is no resume -- a
/// complete run, a cycle pass, no stage left.
[[nodiscard]] std::string resume_stage_text(const training::PipelineRunManifest& manifest);

/// The stages a resume leaves failed, `stage 1, a, failed its gate and stays
/// as it is`, or empty (57h).
[[nodiscard]] std::string kept_failed_text(const training::PipelineRunManifest& manifest);

/// One pipeline spec whole, as a run reads it (M30): `{"object":
/// "training.pipeline", "name", "path", "student", "stages": [{name, dataset,
/// eval_suite, gate, method, iters, batch_size, num_layers, grad_checkpoint,
/// mask_prompt, rehearsal_fraction}]}` -- `path` the spec file's, null for a
/// `training.pipelines:` entry; each stage's fields as the spec writes them
/// (`""` and `0` its defaults), and `gate` the suites its cumulative gate
/// holds it to, its own last. `train pipeline show <name> --output-format
/// json`.
[[nodiscard]] nlohmann::json training_pipeline_spec_document(
    const harness::PipelineSpec& spec, const std::filesystem::path& path = {});

/// The named pipelines, `training.pipelines:` (M30): `{"object": "list",
/// "data": [spec]}`, by name, each as `training_pipeline_spec_document`
/// without its `object` -- `train pipeline list --output-format json`.
[[nodiscard]] nlohmann::json training_pipeline_specs_document(const harness::Config& config);

/// The config a pipeline document names its spec from (57h): the file at
/// `path`, or an empty one when it is absent or does not read -- a read never
/// fails over the config, and the command line and the plane read it alike.
[[nodiscard]] harness::Config config_for_reads(const std::filesystem::path& path);

/// The cycle's history with `active`, whether a pass holds the lock now --
/// `GET /v1/admin/training/cycle`'s body and `train cycle status
/// --output-format json` (55h). None until the first pass has written the
/// history, and none with `error` set when the history cannot be read.
[[nodiscard]] std::optional<nlohmann::json> training_cycle_document(std::string& error);

}  // namespace apogee::operations
