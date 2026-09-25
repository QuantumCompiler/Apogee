#include "models/migrate.h"

#include <system_error>

#include "models/sidecar.h"
#include "models/snapshot.h"

namespace apogee::models {
namespace {

/// A GGUF's id: its record's digest when it has one, else its bytes hashed.
[[nodiscard]] std::string gguf_id(
    const LegacyGguf& gguf, const std::function<void(const std::filesystem::path&)>& on_hash) {
    if (const std::optional<Sidecar> record = load_sidecar(gguf.file); record.has_value()) {
        if (std::string id = weight_id_from_digest(record->file_digest); !id.empty()) {
            return id;
        }
    }
    if (on_hash) {
        on_hash(gguf.file);
    }
    std::string id = weight_id_from_digest(file_sha256(gguf.file));
    return id.empty() ? random_weight_id() : id;
}

/// A snapshot's id: from its record's shard digests, else from its shards
/// hashed.
[[nodiscard]] std::string snapshot_id(
    const std::filesystem::path& dir,
    const std::function<void(const std::filesystem::path&)>& on_hash) {
    if (const std::optional<Snapshot> record = load_snapshot(dir); record.has_value()) {
        if (std::string id = snapshot_weight_id(record->files); !id.empty()) {
            return id;
        }
    }
    std::vector<SnapshotFile> shards;
    std::error_code code;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir, code)) {
        if (entry.is_regular_file(code) && entry.path().extension() == ".safetensors") {
            if (on_hash) {
                on_hash(entry.path());
            }
            shards.push_back({entry.path().lexically_relative(dir).generic_string(), 0,
                              file_sha256(entry.path())});
        }
    }
    std::string id = snapshot_weight_id(shards);
    return id.empty() ? random_weight_id() : id;
}

[[nodiscard]] bool inside(const std::filesystem::path& path, const std::filesystem::path& dir) {
    const std::filesystem::path relative = path.lexically_relative(dir);
    return !relative.empty() && *relative.begin() != "..";
}

}  // namespace

MigrationPlan plan_migration(const StoreRoots& roots,
                             const std::function<void(const std::filesystem::path&)>& on_hash) {
    MigrationPlan plan;
    const LegacyLayout legacy = find_legacy(roots);
    std::error_code code;
    for (const LegacySnapshot& snapshot : legacy.snapshots) {
        MigrationItem item;
        item.kind = MigrationItem::Kind::Snapshot;
        item.model = snapshot.model;
        item.id = snapshot_id(snapshot.dir, on_hash);
        item.destination = weights_dir(roots, kSafetensorsFormat, item.model, item.id);
        item.duplicate = std::filesystem::exists(item.destination, code);
        item.moves.emplace_back(snapshot.dir, item.destination);
        plan.items.push_back(std::move(item));
    }
    for (const LegacyGguf& gguf : legacy.ggufs) {
        MigrationItem item;
        item.kind = MigrationItem::Kind::Gguf;
        item.model = gguf.model;
        item.id = gguf_id(gguf, on_hash);
        item.destination = weights_dir(roots, kGgufFormat, item.model, item.id);
        item.duplicate = std::filesystem::exists(item.destination, code);
        for (const std::filesystem::path& file :
             {gguf.file, gguf.sidecar, gguf.projector, gguf.projector_sidecar}) {
            if (!file.empty()) {
                item.moves.emplace_back(file, item.destination / file.filename());
            }
        }
        plan.items.push_back(std::move(item));
    }
    return plan;
}

std::string apply_migration(const MigrationItem& item) {
    if (item.duplicate) {
        return {};
    }
    std::error_code code;
    for (const auto& [from, to] : item.moves) {
        std::filesystem::path source = from;
        if (item.kind == MigrationItem::Kind::Snapshot && inside(to, from)) {
            // `<models>/<name>` becomes `<models>/<name>/safetensors/<id>`:
            // move it aside, so the model directory can be made where it was.
            source = from.parent_path() /
                     ("." + from.filename().string() + ".migrating-" + random_weight_id());
            std::filesystem::rename(from, source, code);
            if (code) {
                return "could not move " + from.string() + " aside: " + code.message();
            }
        }
        if (std::string error = move_path(source, to); !error.empty()) {
            if (source != from) {
                // Put it back: a failed migration never leaves a model under
                // a hidden name. The empty skeleton the failed move may have
                // made at the old name goes first -- non-recursively, so only
                // if it really is empty.
                for (std::filesystem::path dir = to.parent_path(); inside(dir, from.parent_path());
                     dir = dir.parent_path()) {
                    std::filesystem::remove(dir, code);
                }
                std::filesystem::rename(source, from, code);
            }
            return error;
        }
    }
    return {};
}

}  // namespace apogee::models
