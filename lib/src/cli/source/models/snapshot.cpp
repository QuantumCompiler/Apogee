#include "models/snapshot.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <random>
#include <system_error>

namespace apogee::models {

std::filesystem::path snapshot_record_path(const std::filesystem::path& dir) {
    return dir / "apogee-snapshot.json";
}

bool write_snapshot(const std::filesystem::path& dir, const Snapshot& snapshot) {
    nlohmann::json files = nlohmann::json::array();
    for (const SnapshotFile& file : snapshot.files) {
        files.push_back({{"path", file.path}, {"size", file.size}, {"sha256", file.sha256}});
    }
    const nlohmann::json record{{"ref", snapshot.ref},
                                {"revision", snapshot.revision},
                                {"source", snapshot.source},
                                {"pulled_at", snapshot.pulled_at},
                                {"files", files}};
    std::filesystem::path temp = snapshot_record_path(dir);
    temp += ".tmp-" + std::to_string(std::random_device{}());
    {
        std::ofstream out{temp, std::ios::binary};
        if (!out) {
            return false;
        }
        out << record.dump(2) << "\n";
    }
    std::error_code code;
    std::filesystem::rename(temp, snapshot_record_path(dir), code);
    if (code) {
        std::filesystem::remove(temp, code);
        return false;
    }
    return true;
}

std::optional<Snapshot> load_snapshot(const std::filesystem::path& dir) {
    std::ifstream in{snapshot_record_path(dir), std::ios::binary};
    if (!in) {
        return std::nullopt;
    }
    const nlohmann::json record = nlohmann::json::parse(in, nullptr, false);
    if (record.is_discarded() || !record.is_object()) {
        return std::nullopt;
    }
    Snapshot snapshot;
    snapshot.ref = record.value("ref", std::string{});
    snapshot.revision = record.value("revision", std::string{});
    snapshot.source = record.value("source", std::string{});
    snapshot.pulled_at = record.value("pulled_at", std::string{});
    if (const auto files = record.find("files"); files != record.end() && files->is_array()) {
        for (const nlohmann::json& file : *files) {
            if (!file.is_object()) {
                continue;
            }
            SnapshotFile entry;
            entry.path = file.value("path", std::string{});
            entry.size = file.value("size", std::int64_t{0});
            entry.sha256 = file.value("sha256", std::string{});
            snapshot.files.push_back(std::move(entry));
        }
    }
    return snapshot;
}

bool is_snapshot_dir(const std::filesystem::path& dir) {
    std::error_code code;
    if (!std::filesystem::is_directory(dir, code) ||
        !std::filesystem::is_regular_file(dir / "config.json", code)) {
        return false;
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir, code)) {
        if (code) {
            return false;
        }
        if (entry.is_regular_file(code) && entry.path().extension() == ".safetensors") {
            return true;
        }
    }
    return false;
}

std::string snapshot_architecture(const std::filesystem::path& dir) {
    std::ifstream in{dir / "config.json", std::ios::binary};
    if (!in) {
        return {};
    }
    const nlohmann::json config = nlohmann::json::parse(in, nullptr, false);
    if (config.is_discarded() || !config.is_object()) {
        return {};
    }
    if (const auto architectures = config.find("architectures");
        architectures != config.end() && architectures->is_array() && !architectures->empty() &&
        architectures->front().is_string()) {
        std::string name = architectures->front().get<std::string>();
        for (const std::string_view suffix : {"ForCausalLM", "ForConditionalGeneration"}) {
            if (name.ends_with(suffix)) {
                name.erase(name.size() - suffix.size());
                break;
            }
        }
        return name;
    }
    return config.value("model_type", std::string{});
}

std::vector<std::filesystem::path> list_snapshots(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> out;
    std::error_code code;
    if (!std::filesystem::is_directory(root, code)) {
        return out;
    }
    for (const auto& entry : std::filesystem::directory_iterator(root, code)) {
        if (code) {
            break;
        }
        if (entry.is_directory(code) && is_snapshot_dir(entry.path())) {
            out.push_back(entry.path());
        }
    }
    std::ranges::sort(out);
    return out;
}

}  // namespace apogee::models
