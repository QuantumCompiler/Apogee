#include "training/manifest.h"

#include <nlohmann/json.hpp>

#include <array>
#include <ctime>
#include <fstream>
#include <system_error>

#include "harness/config_edit.h"
#include "models/sha256.h"

namespace apogee::training {
namespace {

std::string format_utc(std::chrono::system_clock::time_point at, const char* format) {
    const std::time_t seconds = std::chrono::system_clock::to_time_t(at);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &seconds);
#else
    gmtime_r(&seconds, &utc);
#endif
    std::array<char, 32> buffer{};
    std::strftime(buffer.data(), buffer.size(), format, &utc);
    return buffer.data();
}

}  // namespace

nlohmann::json manifest_to_json(const RunManifest& manifest) {
    nlohmann::json out{{"run_id", manifest.run_id},
                       {"trainer", manifest.trainer},
                       {"base_model", manifest.base_model},
                       {"dataset", manifest.dataset},
                       {"dataset_hash", manifest.dataset_hash},
                       {"method", manifest.method},
                       {"iters", manifest.iters},
                       {"batch_size", manifest.batch_size},
                       {"num_layers", manifest.num_layers},
                       {"grad_checkpoint", manifest.grad_checkpoint},
                       {"mask_prompt", manifest.mask_prompt},
                       {"final_loss", manifest.final_loss},
                       {"iterations", manifest.iterations},
                       {"adapter_dir", manifest.adapter_dir},
                       {"status", manifest.status},
                       {"started_at", manifest.started_at}};
    if (!manifest.error.empty()) {
        out["error"] = manifest.error;
    }
    if (!manifest.finished_at.empty()) {
        out["finished_at"] = manifest.finished_at;
    }
    if (manifest.eval.has_value()) {
        out["eval_results"] = eval_results_to_json(*manifest.eval);
    }
    if (!manifest.parent_run.empty()) {
        out["parent_run"] = manifest.parent_run;
    }
    if (!manifest.pipeline_run_id.empty()) {
        out["pipeline_run_id"] = manifest.pipeline_run_id;
    }
    return out;
}

RunManifest manifest_from_json(const nlohmann::json& json) {
    if (!json.is_object()) {
        throw std::runtime_error("manifest: expected an object");
    }
    RunManifest manifest;
    manifest.run_id = json.value("run_id", std::string{});
    if (manifest.run_id.empty()) {
        throw std::runtime_error("manifest: no run_id");
    }
    manifest.trainer = json.value("trainer", std::string{});
    manifest.base_model = json.value("base_model", std::string{});
    manifest.dataset = json.value("dataset", std::string{});
    manifest.dataset_hash = json.value("dataset_hash", std::string{});
    manifest.method = json.value("method", std::string{});
    manifest.iters = json.value("iters", 0);
    manifest.batch_size = json.value("batch_size", 0);
    manifest.num_layers = json.value("num_layers", 0);
    manifest.grad_checkpoint = json.value("grad_checkpoint", false);
    manifest.mask_prompt = json.value("mask_prompt", false);
    manifest.final_loss = json.value("final_loss", 0.0);
    manifest.iterations = json.value("iterations", 0);
    manifest.adapter_dir = json.value("adapter_dir", std::string{});
    manifest.status = json.value("status", std::string{kStatusComplete});
    manifest.error = json.value("error", std::string{});
    manifest.started_at = json.value("started_at", std::string{});
    manifest.finished_at = json.value("finished_at", std::string{});
    if (const auto eval = json.find("eval_results"); eval != json.end() && !eval->is_null()) {
        manifest.eval = eval_results_from_json(*eval);
    }
    manifest.parent_run = json.value("parent_run", std::string{});
    manifest.pipeline_run_id = json.value("pipeline_run_id", std::string{});
    return manifest;
}

std::filesystem::path manifest_path(const std::filesystem::path& run_dir) {
    return run_dir / kManifestFileName;
}

std::string write_manifest(const std::filesystem::path& run_dir, const RunManifest& manifest) {
    std::error_code code;
    std::filesystem::create_directories(run_dir, code);
    if (code) {
        return "could not create " + run_dir.string() + ": " + code.message();
    }
    try {
        harness::write_file_atomically(manifest_path(run_dir),
                                       manifest_to_json(manifest).dump(2) + "\n");
    } catch (const std::exception& e) {
        return e.what();
    }
    return {};
}

std::optional<RunManifest> read_manifest(const std::filesystem::path& run_dir, std::string& error) {
    error.clear();
    std::ifstream in{manifest_path(run_dir), std::ios::binary};
    if (!in) {
        error = "no manifest at " + manifest_path(run_dir).string();
        return std::nullopt;
    }
    const nlohmann::json json = nlohmann::json::parse(in, nullptr, false);
    if (json.is_discarded()) {
        error = manifest_path(run_dir).string() + " is not JSON";
        return std::nullopt;
    }
    try {
        return manifest_from_json(json);
    } catch (const std::runtime_error& e) {
        error = manifest_path(run_dir).string() + ": " + e.what();
        return std::nullopt;
    }
}

std::string new_run_id(const std::filesystem::path& runs_dir,
                       std::chrono::system_clock::time_point now, std::string_view prefix) {
    const std::string base = std::string{prefix} + format_utc(now, "%Y%m%d-%H%M%S");
    std::string id = base;
    std::error_code code;
    for (int suffix = 2; std::filesystem::exists(runs_dir / id, code); ++suffix) {
        id = base + "-" + std::to_string(suffix);
    }
    return id;
}

bool valid_run_id(std::string_view id) noexcept {
    if (id.empty() || id.size() > 64) {
        return false;
    }
    for (const char c : id) {
        const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                        (c >= 'A' && c <= 'Z') || c == '-' || c == '_';
        if (!ok) {
            return false;
        }
    }
    return true;
}

std::string dataset_digest(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        return {};
    }
    models::Sha256 hash;
    std::array<char, 65536> buffer{};
    while (in.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) || in.gcount() > 0) {
        hash.update(std::string_view{buffer.data(), static_cast<std::size_t>(in.gcount())});
    }
    return hash.hex_digest().substr(0, 12);
}

}  // namespace apogee::training
