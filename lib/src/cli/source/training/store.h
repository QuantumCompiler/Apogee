#pragma once

#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "training/manifest.h"
#include "training/pipeline.h"

/// Reading training state back: the runs under `training/runs/` and the
/// version ledgers under `training/versions/`. The filesystem is the
/// source of truth -- `train status`, `train versions` and the HTTP reads
/// all go through this and nothing is cached -- and the reads are what the
/// control plane serves; the writes (`save_ledger`) are the promote and
/// rollback commands' alone.
namespace apogee::training {

struct VersionEntry {
    int version = 0;
    std::string run_id;
    std::string gguf_path;
    std::string promoted_at;
    /// Absent when no eval ran before the promotion (`--force`).
    std::optional<double> eval_score;
    std::optional<bool> eval_passed;
    /// Set when retention removed the file; the entry stays as history, so
    /// a rollback can name what it can no longer reach.
    std::string pruned_at;

    [[nodiscard]] bool pruned() const noexcept {
        return !pruned_at.empty();
    }
};

/// `training/versions/<backend>.json`.
struct VersionLedger {
    std::string backend;
    /// The version `model_path` points at; 0 with no versions.
    int active_version = 0;
    /// Ascending by version.
    std::vector<VersionEntry> versions;

    [[nodiscard]] const VersionEntry* find(int version) const noexcept;
    [[nodiscard]] const VersionEntry* active() const noexcept;
    /// Entries retention has not removed.
    [[nodiscard]] int kept() const noexcept;
};

[[nodiscard]] nlohmann::json ledger_to_json(const VersionLedger& ledger);
/// Throws std::runtime_error on a wrong shape.
[[nodiscard]] VersionLedger ledger_from_json(const nlohmann::json& json);

[[nodiscard]] std::filesystem::path ledger_path(const std::filesystem::path& versions_dir,
                                                std::string_view backend);

/// nullopt when there is no ledger; `error` set when there is one that
/// cannot be read.
[[nodiscard]] std::optional<VersionLedger> load_ledger(const std::filesystem::path& versions_dir,
                                                       std::string_view backend,
                                                       std::string& error);

/// Writes through a temp file and a rename. The error text, or empty.
[[nodiscard]] std::string save_ledger(const std::filesystem::path& versions_dir,
                                      const VersionLedger& ledger);

/// A run as a listing shows it.
struct RunSummary {
    std::string id;
    std::string trainer;
    std::string status;
    std::string base_model;
    std::string dataset;
    std::string method;
    double final_loss = 0.0;
    int iterations = 0;
    std::string started_at;
    std::string finished_at;
    std::optional<bool> eval_passed;
    std::optional<double> eval_score;
};

[[nodiscard]] RunSummary summarize(const RunManifest& manifest);
[[nodiscard]] nlohmann::json run_summary_json(const RunSummary& summary);

/// A pipeline run as a listing shows it.
struct PipelineSummary {
    std::string id;
    std::string spec_name;
    std::string status;
    int stages = 0;
    int passed = 0;
    std::string started_at;
    std::string completed_at;
};

[[nodiscard]] PipelineSummary summarize(const PipelineRunManifest& manifest);
[[nodiscard]] nlohmann::json pipeline_summary_json(const PipelineSummary& summary);

class TrainingStore {
public:
    /// `root` is the `training` directory.
    explicit TrainingStore(std::filesystem::path root);

    [[nodiscard]] std::filesystem::path runs_dir() const;
    [[nodiscard]] std::filesystem::path versions_dir() const;
    [[nodiscard]] std::filesystem::path pipelines_dir() const;
    [[nodiscard]] std::filesystem::path run_dir(std::string_view id) const;
    [[nodiscard]] std::filesystem::path pipeline_dir(std::string_view id) const;

    /// Every run with a readable manifest, newest first (ids are
    /// timestamp-prefixed, so descending order is reverse-chronological).
    [[nodiscard]] std::vector<RunSummary> list_runs() const;

    /// nullopt for an unknown id, an invalid one, or an unreadable
    /// manifest.
    [[nodiscard]] std::optional<RunManifest> get_run(std::string_view id) const;

    /// Every pipeline run with a readable manifest, newest first.
    [[nodiscard]] std::vector<PipelineSummary> list_pipelines() const;

    /// nullopt for an unknown, invalid or unreadable one.
    [[nodiscard]] std::optional<PipelineRunManifest> get_pipeline(std::string_view id) const;

    /// The newest pipeline whose manifest says `running`, if any.
    [[nodiscard]] std::optional<PipelineSummary> active_pipeline() const;

    [[nodiscard]] std::optional<VersionLedger> list_versions(std::string_view backend) const;

    /// Every readable ledger, by backend name.
    [[nodiscard]] std::vector<VersionLedger> all_versions() const;

private:
    std::filesystem::path root_;
};

}  // namespace apogee::training
