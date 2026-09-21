#include "training/store.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <system_error>

#include "harness/config_edit.h"

namespace apogee::training {

const VersionEntry* VersionLedger::find(int version) const noexcept {
    for (const VersionEntry& entry : versions) {
        if (entry.version == version) {
            return &entry;
        }
    }
    return nullptr;
}

const VersionEntry* VersionLedger::active() const noexcept {
    return find(active_version);
}

int VersionLedger::kept() const noexcept {
    int count = 0;
    for (const VersionEntry& entry : versions) {
        if (!entry.pruned()) {
            ++count;
        }
    }
    return count;
}

nlohmann::json ledger_to_json(const VersionLedger& ledger) {
    nlohmann::json versions = nlohmann::json::array();
    for (const VersionEntry& entry : ledger.versions) {
        nlohmann::json row{{"version", entry.version},
                           {"run_id", entry.run_id},
                           {"gguf_path", entry.gguf_path},
                           {"promoted_at", entry.promoted_at}};
        if (entry.eval_score.has_value()) {
            row["eval_score"] = *entry.eval_score;
        }
        if (entry.eval_passed.has_value()) {
            row["eval_passed"] = *entry.eval_passed;
        }
        if (entry.pruned()) {
            row["pruned_at"] = entry.pruned_at;
        }
        versions.push_back(std::move(row));
    }
    return nlohmann::json{{"backend_name", ledger.backend},
                          {"active_version", ledger.active_version},
                          {"versions", std::move(versions)}};
}

VersionLedger ledger_from_json(const nlohmann::json& json) {
    if (!json.is_object()) {
        throw std::runtime_error("ledger: expected an object");
    }
    VersionLedger ledger;
    ledger.backend = json.value("backend_name", std::string{});
    ledger.active_version = json.value("active_version", 0);
    if (const auto versions = json.find("versions"); versions != json.end()) {
        if (!versions->is_array()) {
            throw std::runtime_error("ledger.versions: expected an array");
        }
        for (const nlohmann::json& row : *versions) {
            if (!row.is_object()) {
                throw std::runtime_error("ledger.versions: expected objects");
            }
            VersionEntry entry;
            entry.version = row.value("version", 0);
            entry.run_id = row.value("run_id", std::string{});
            entry.gguf_path = row.value("gguf_path", std::string{});
            entry.promoted_at = row.value("promoted_at", std::string{});
            if (const auto score = row.find("eval_score");
                score != row.end() && score->is_number()) {
                entry.eval_score = score->get<double>();
            }
            if (const auto passed = row.find("eval_passed");
                passed != row.end() && passed->is_boolean()) {
                entry.eval_passed = passed->get<bool>();
            }
            entry.pruned_at = row.value("pruned_at", std::string{});
            ledger.versions.push_back(std::move(entry));
        }
    }
    std::ranges::sort(ledger.versions, [](const VersionEntry& a, const VersionEntry& b) {
        return a.version < b.version;
    });
    return ledger;
}

std::filesystem::path ledger_path(const std::filesystem::path& versions_dir,
                                  std::string_view backend) {
    return versions_dir / (std::string{backend} + ".json");
}

std::optional<VersionLedger> load_ledger(const std::filesystem::path& versions_dir,
                                         std::string_view backend, std::string& error) {
    error.clear();
    const std::filesystem::path path = ledger_path(versions_dir, backend);
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        return std::nullopt;
    }
    const nlohmann::json json = nlohmann::json::parse(in, nullptr, false);
    if (json.is_discarded()) {
        error = path.string() + " is not JSON";
        return std::nullopt;
    }
    try {
        VersionLedger ledger = ledger_from_json(json);
        if (ledger.backend.empty()) {
            ledger.backend = std::string{backend};
        }
        return ledger;
    } catch (const std::runtime_error& e) {
        error = path.string() + ": " + e.what();
        return std::nullopt;
    }
}

std::string save_ledger(const std::filesystem::path& versions_dir, const VersionLedger& ledger) {
    std::error_code code;
    std::filesystem::create_directories(versions_dir, code);
    if (code) {
        return "could not create " + versions_dir.string() + ": " + code.message();
    }
    try {
        harness::write_file_atomically(ledger_path(versions_dir, ledger.backend),
                                       ledger_to_json(ledger).dump(2) + "\n");
    } catch (const std::exception& e) {
        return e.what();
    }
    return {};
}

RunSummary summarize(const RunManifest& manifest) {
    RunSummary summary;
    summary.id = manifest.run_id;
    summary.trainer = manifest.trainer;
    summary.status = manifest.status;
    summary.base_model = manifest.base_model;
    summary.dataset = manifest.dataset;
    summary.method = manifest.method;
    summary.final_loss = manifest.final_loss;
    summary.iterations = manifest.iterations;
    summary.started_at = manifest.started_at;
    summary.finished_at = manifest.finished_at;
    if (manifest.eval.has_value()) {
        summary.eval_passed = manifest.eval->passed;
        summary.eval_score = manifest.eval->score;
    }
    return summary;
}

nlohmann::json run_summary_json(const RunSummary& summary) {
    nlohmann::json out{{"kind", "run"},
                       {"id", summary.id},
                       {"trainer", summary.trainer},
                       {"status", summary.status},
                       {"base_model", summary.base_model},
                       {"dataset", summary.dataset},
                       {"method", summary.method},
                       {"final_loss", summary.final_loss},
                       {"iterations", summary.iterations},
                       {"started_at", summary.started_at}};
    if (!summary.finished_at.empty()) {
        out["finished_at"] = summary.finished_at;
    }
    if (summary.eval_passed.has_value()) {
        out["eval_passed"] = *summary.eval_passed;
        out["eval_score"] = summary.eval_score.value_or(0.0);
    }
    return out;
}

PipelineSummary summarize(const PipelineRunManifest& manifest) {
    PipelineSummary summary;
    summary.id = manifest.pipeline_run_id;
    summary.spec_name = manifest.spec_name;
    summary.status = manifest.status;
    summary.stages = static_cast<int>(manifest.stages.size());
    for (const PipelineStageRecord& stage : manifest.stages) {
        if (stage.status == kStagePassed) {
            ++summary.passed;
        }
    }
    summary.started_at = manifest.started_at;
    summary.completed_at = manifest.completed_at;
    return summary;
}

nlohmann::json pipeline_summary_json(const PipelineSummary& summary) {
    nlohmann::json out{{"kind", "pipeline"},
                       {"id", summary.id},
                       {"spec_name", summary.spec_name},
                       {"status", summary.status},
                       {"stages", summary.stages},
                       {"passed", summary.passed},
                       {"started_at", summary.started_at}};
    if (!summary.completed_at.empty()) {
        out["completed_at"] = summary.completed_at;
    }
    return out;
}

TrainingStore::TrainingStore(std::filesystem::path root) : root_{std::move(root)} {}

std::filesystem::path TrainingStore::runs_dir() const {
    return root_ / "runs";
}

std::filesystem::path TrainingStore::versions_dir() const {
    return root_ / "versions";
}

std::filesystem::path TrainingStore::pipelines_dir() const {
    return root_ / kPipelinesDirName;
}

std::filesystem::path TrainingStore::run_dir(std::string_view id) const {
    return runs_dir() / std::string{id};
}

std::filesystem::path TrainingStore::pipeline_dir(std::string_view id) const {
    return pipelines_dir() / std::string{id};
}

std::vector<RunSummary> TrainingStore::list_runs() const {
    std::vector<RunSummary> runs;
    std::error_code code;
    for (const auto& entry : std::filesystem::directory_iterator(runs_dir(), code)) {
        if (!entry.is_directory(code)) {
            continue;
        }
        std::string error;
        const std::optional<RunManifest> manifest = read_manifest(entry.path(), error);
        if (manifest.has_value()) {
            runs.push_back(summarize(*manifest));
        }
    }
    std::ranges::sort(runs, [](const RunSummary& a, const RunSummary& b) { return a.id > b.id; });
    return runs;
}

std::optional<RunManifest> TrainingStore::get_run(std::string_view id) const {
    if (!valid_run_id(id)) {
        return std::nullopt;
    }
    std::string error;
    return read_manifest(run_dir(id), error);
}

std::vector<PipelineSummary> TrainingStore::list_pipelines() const {
    std::vector<PipelineSummary> pipelines;
    std::error_code code;
    for (const auto& entry : std::filesystem::directory_iterator(pipelines_dir(), code)) {
        if (!entry.is_directory(code)) {
            continue;
        }
        std::string error;
        const std::optional<PipelineRunManifest> manifest =
            read_pipeline_manifest(entry.path(), error);
        if (manifest.has_value()) {
            pipelines.push_back(summarize(*manifest));
        }
    }
    std::ranges::sort(pipelines, [](const PipelineSummary& a, const PipelineSummary& b) {
        return a.started_at != b.started_at ? a.started_at > b.started_at : a.id > b.id;
    });
    return pipelines;
}

std::optional<PipelineRunManifest> TrainingStore::get_pipeline(std::string_view id) const {
    if (!valid_run_id(id)) {
        return std::nullopt;
    }
    std::string error;
    return read_pipeline_manifest(pipeline_dir(id), error);
}

std::optional<PipelineSummary> TrainingStore::active_pipeline() const {
    for (const PipelineSummary& pipeline : list_pipelines()) {
        if (pipeline.status == kPipelineRunning) {
            return pipeline;
        }
    }
    return std::nullopt;
}

std::optional<VersionLedger> TrainingStore::list_versions(std::string_view backend) const {
    std::string error;
    return load_ledger(versions_dir(), backend, error);
}

std::vector<VersionLedger> TrainingStore::all_versions() const {
    std::vector<VersionLedger> ledgers;
    std::error_code code;
    for (const auto& entry : std::filesystem::directory_iterator(versions_dir(), code)) {
        if (!entry.is_regular_file(code) || entry.path().extension() != ".json") {
            continue;
        }
        std::string error;
        const std::optional<VersionLedger> ledger =
            load_ledger(versions_dir(), entry.path().stem().string(), error);
        if (ledger.has_value()) {
            ledgers.push_back(*ledger);
        }
    }
    std::ranges::sort(ledgers, [](const VersionLedger& a, const VersionLedger& b) {
        return a.backend < b.backend;
    });
    return ledgers;
}

}  // namespace apogee::training
