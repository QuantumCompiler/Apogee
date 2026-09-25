#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "models/store.h"

/// The flat layout's files, moved into the model store (`store.h`).
///
/// Only the files: what points AT them -- a backend's `model_path`, a
/// collection's recorded embedding model, a promotion ledger -- is the
/// command's to rewrite (`commands/models_migrate.cpp`), since the config has
/// one mutation path and this package is not it.
namespace apogee::models {

/// One thing the flat layout left, and where it goes.
struct MigrationItem {
    enum class Kind : std::uint8_t { Gguf, Snapshot };
    Kind kind = Kind::Gguf;
    std::string model;
    std::string id;
    /// The `<id>` directory it lands in.
    std::filesystem::path destination;
    /// Each file or directory that moves, and where to. A GGUF brings its
    /// record, its projector and the projector's record; a snapshot is one
    /// directory.
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> moves;
    /// Identical weights are already stored under this id: nothing moves, and
    /// the old copy is left for the user to remove -- never deleted here.
    bool duplicate = false;
};

struct MigrationPlan {
    /// Snapshots first: a GGUF from the same repository lands in a model
    /// directory named like the snapshot's old one, which must have moved
    /// aside by then.
    std::vector<MigrationItem> items;
};

/// The moves that turn the flat layout into the store. A file with no
/// recorded digest is hashed to name its directory -- slow for a large model,
/// so `on_hash` hears about each one first.
[[nodiscard]] MigrationPlan plan_migration(
    const StoreRoots& roots, const std::function<void(const std::filesystem::path&)>& on_hash = {});

/// Carries out one item. A snapshot directory whose new home is inside
/// itself -- `<models>/<name>` becoming `<models>/<name>/safetensors/<id>` --
/// is renamed aside first. Error, or empty.
[[nodiscard]] std::string apply_migration(const MigrationItem& item);

}  // namespace apogee::models
