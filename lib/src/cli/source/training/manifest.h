#pragma once

#include <nlohmann/json_fwd.hpp>

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "training/eval.h"

/// The run manifest: `training/runs/<run-id>/manifest.json`, the record of
/// one fine-tuning run -- what was trained on what, how, the outcome, and
/// the eval that gated it. The filesystem is the source of truth: `status`,
/// `versions` and the HTTP reads all read these, nothing is cached.
///
/// Written when the run STARTS (`status: running`) and rewritten when it
/// ends (`complete` | `failed` | `cancelled`), so a run in flight is visible
/// for what it is, and a crashed driver leaves a failed run with its error
/// rather than a directory nobody can explain.
namespace apogee::training {

inline constexpr std::string_view kManifestFileName = "manifest.json";
inline constexpr std::string_view kStatusRunning = "running";
inline constexpr std::string_view kStatusComplete = "complete";
inline constexpr std::string_view kStatusFailed = "failed";
inline constexpr std::string_view kStatusCancelled = "cancelled";

struct RunManifest {
    std::string run_id;
    /// `mlx`, `peft`, `mock`.
    std::string trainer;
    std::string base_model;
    std::string dataset;
    /// The dataset's sha256, first twelve hex digits.
    std::string dataset_hash;
    std::string method;
    int iters = 0;
    int batch_size = 0;
    int num_layers = 0;
    bool grad_checkpoint = false;
    bool mask_prompt = false;
    double final_loss = 0.0;
    int iterations = 0;
    std::string adapter_dir;
    std::string status;
    /// Why it failed, when it did.
    std::string error;
    std::string started_at;
    std::string finished_at;
    std::optional<EvalResults> eval;
    /// A pipeline stage's lineage: the stage run before it (empty for stage
    /// 0 and for a plain run) and the pipeline run it belongs to.
    std::string parent_run;
    std::string pipeline_run_id;

    [[nodiscard]] bool complete() const noexcept {
        return status == kStatusComplete;
    }
};

[[nodiscard]] nlohmann::json manifest_to_json(const RunManifest& manifest);
/// Throws std::runtime_error on a wrong shape or a missing `run_id`.
[[nodiscard]] RunManifest manifest_from_json(const nlohmann::json& json);

[[nodiscard]] std::filesystem::path manifest_path(const std::filesystem::path& run_dir);

/// Writes through a temp file and a rename, creating the directory.
/// The error text, or empty.
[[nodiscard]] std::string write_manifest(const std::filesystem::path& run_dir,
                                         const RunManifest& manifest);

/// nullopt with `error` set when absent or unreadable.
[[nodiscard]] std::optional<RunManifest> read_manifest(const std::filesystem::path& run_dir,
                                                       std::string& error);

/// `[<prefix>]YYYYMMDD-HHMMSS` in UTC, with `-2`, `-3`, … appended while a
/// run of that id already exists under `runs_dir` -- two runs started in one
/// second must not share a directory. The prefix (`pipe-`, `regime-`,
/// `cycle-`) is what tells a pipeline run from a plain one at a glance.
[[nodiscard]] std::string new_run_id(
    const std::filesystem::path& runs_dir,
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now(),
    std::string_view prefix = {});

/// Letters, digits, `-` and `_` only: a run id is a directory name and
/// must never reach outside `runs/`.
[[nodiscard]] bool valid_run_id(std::string_view id) noexcept;

/// The sha256 of the file's bytes, streamed, as its first twelve hex
/// digits; empty when the file cannot be read.
[[nodiscard]] std::string dataset_digest(const std::filesystem::path& path);

}  // namespace apogee::training
