#include "models/snapshot.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <random>
#include <set>
#include <system_error>

#include "models/sidecar.h"

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

bool is_download_record(const std::filesystem::path& file) {
    std::ifstream in{file, std::ios::binary};
    if (!in) {
        return false;
    }
    const nlohmann::json record = nlohmann::json::parse(in, nullptr, false);
    // A sidecar's own fields; no model configuration carries them.
    return record.is_object() && record.contains("source_url") && record.contains("verification") &&
           record.contains("file_digest");
}

bool config_is_download_record(const std::filesystem::path& dir) {
    return is_download_record(dir / "config.json");
}

SnapshotDamage find_snapshot_damage(const std::filesystem::path& dir) {
    SnapshotDamage damage;
    const std::optional<Snapshot> record = load_snapshot(dir);
    if (!record.has_value()) {
        damage.error = "no " + snapshot_record_path(dir).filename().string() + " in " +
                       dir.string() + ", so there is nothing to judge its files against";
        return damage;
    }
    std::set<std::string> listed;
    for (const SnapshotFile& file : record->files) {
        listed.insert(file.path);
    }
    std::error_code code;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir, code)) {
        if (!entry.is_regular_file(code) || entry.path().extension() != ".json" ||
            entry.path() == snapshot_record_path(dir)) {
            continue;
        }
        const std::string relative = entry.path().lexically_relative(dir).generic_string();
        if (!listed.contains(relative) && is_download_record(entry.path())) {
            damage.stray_records.push_back(entry.path());
        }
    }
    std::ranges::sort(damage.stray_records);
    // Small files are hashed as well as sized: a tokenizer replaced by a record
    // of the right size would otherwise pass. Shards are sized only -- hashing
    // fifty gigabytes to repair a configuration file is not a repair.
    constexpr std::int64_t kHashBelow = 64LL * 1024 * 1024;
    for (const SnapshotFile& file : record->files) {
        const std::filesystem::path path = dir / file.path;
        const bool broken = [&] {
            if (!std::filesystem::is_regular_file(path, code)) {
                return true;
            }
            const auto size = static_cast<std::int64_t>(std::filesystem::file_size(path, code));
            if (file.size > 0 && size != file.size) {
                return true;
            }
            if (is_download_record(path)) {
                return true;
            }
            return !file.sha256.empty() && size < kHashBelow && file_sha256(path) != file.sha256;
        }();
        if (broken) {
            damage.refetch.push_back(file);
        }
    }
    return damage;
}

SnapshotRepair repair_snapshot(const std::filesystem::path& dir, const SnapshotDamage& damage,
                               const SnapshotFetchFn& fetch) {
    SnapshotRepair repair;
    std::error_code code;
    for (const SnapshotFile& file : damage.refetch) {
        const std::filesystem::path path = dir / file.path;
        std::filesystem::path fresh = path;
        fresh += ".repair";
        std::filesystem::remove(fresh, code);
        std::string error = fetch(file, fresh);
        if (error.empty() && file.size > 0 &&
            static_cast<std::int64_t>(std::filesystem::file_size(fresh, code)) != file.size) {
            error = "the fetched copy is not the recorded size";
        }
        if (error.empty() && !file.sha256.empty() && file_sha256(fresh) != file.sha256) {
            error = "the fetched copy does not match the recorded sha256";
        }
        if (error.empty()) {
            std::filesystem::rename(fresh, path, code);
            if (code) {
                error = "could not put it in place: " + code.message();
            }
        }
        if (!error.empty()) {
            std::filesystem::remove(fresh, code);
            repair.error = file.path + ": " + error;
            return repair;
        }
        repair.fetched.push_back(file.path);
    }
    for (const std::filesystem::path& stray : damage.stray_records) {
        if (std::filesystem::remove(stray, code)) {
            repair.removed.push_back(stray.lexically_relative(dir).generic_string());
        }
    }
    return repair;
}

std::string damaged_snapshot_error(const std::filesystem::path& dir) {
    if (!config_is_download_record(dir)) {
        return {};
    }
    return dir.string() +
           "'s config.json is an Apogee download record, not the model's configuration: "
           "'models pull --safetensors' replaced every JSON file of a snapshot this way until "
           "2026-09-23. Delete it and pull it again";
}

std::optional<std::int64_t> snapshot_elements(
    const std::filesystem::path& dir, const std::function<bool(std::string_view)>& include) {
    // A header past this is not a SafeTensors header: the format caps it at
    // 100 MB, and reading an arbitrary length would be reading the weights.
    constexpr std::uint64_t kMaxHeader = 100ULL * 1024 * 1024;
    std::int64_t total = 0;
    bool any = false;
    std::error_code code;
    for (const auto& entry : std::filesystem::directory_iterator(dir, code)) {
        if (code) {
            return std::nullopt;
        }
        if (!entry.is_regular_file(code) || entry.path().extension() != ".safetensors") {
            continue;
        }
        std::ifstream in{entry.path(), std::ios::binary};
        std::array<char, 8> length_bytes{};
        if (!in.read(length_bytes.data(), length_bytes.size())) {
            return std::nullopt;
        }
        std::uint64_t length = 0;  // little-endian
        for (std::size_t i = 0; i < length_bytes.size(); ++i) {
            length |= static_cast<std::uint64_t>(static_cast<unsigned char>(length_bytes.at(i)))
                      << (8 * i);
        }
        if (length == 0 || length > kMaxHeader) {
            return std::nullopt;
        }
        std::string header(static_cast<std::size_t>(length), '\0');
        if (!in.read(header.data(), static_cast<std::streamsize>(length))) {
            return std::nullopt;
        }
        const nlohmann::json table = nlohmann::json::parse(header, nullptr, false);
        if (!table.is_object()) {
            return std::nullopt;
        }
        for (const auto& [name, tensor] : table.items()) {
            if (name == "__metadata__" || !tensor.is_object() || (include && !include(name))) {
                continue;
            }
            const auto shape = tensor.find("shape");
            if (shape == tensor.end() || !shape->is_array()) {
                return std::nullopt;
            }
            std::int64_t elements = 1;
            for (const nlohmann::json& dimension : *shape) {
                if (!dimension.is_number_integer() || dimension.get<std::int64_t>() < 0) {
                    return std::nullopt;
                }
                elements *= dimension.get<std::int64_t>();
            }
            total += elements;
        }
        any = true;
    }
    if (!any) {
        return std::nullopt;
    }
    return total;
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
