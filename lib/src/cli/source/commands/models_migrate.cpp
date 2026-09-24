#include "commands/models_migrate.h"

#include <CLI/CLI.hpp>

#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "commands/models_pull.h"
#include "commands/train.h"
#include "embedstore/store.h"
#include "harness/config.h"
#include "harness/config_edit.h"
#include "harness/layout.h"
#include "harness/paths.h"
#include "models/gguf_inspect.h"
#include "models/migrate.h"
#include "models/sidecar.h"
#include "models/snapshot.h"
#include "models/store.h"
#include "training/manifest.h"
#include "training/pipeline.h"
#include "training/store.h"

namespace apogee::commands {
namespace {

[[noreturn]] void fail(const std::string& message) {
    std::cerr << "apogee models: " << message << "\n";
    throw CLI::RuntimeError(1);
}

[[nodiscard]] std::filesystem::path normal(const std::filesystem::path& path) {
    return path.lexically_normal();
}

/// A promoted training version, moving from `training/versions/` to beside
/// the model it was trained from.
struct PromotedMove {
    std::string backend;
    int version = 0;
    std::string run_id;
    std::string promoted_at;
    std::filesystem::path from;
    /// The `<id>` directory, and the file inside it.
    std::filesystem::path destination;
    std::filesystem::path to;
    bool duplicate = false;
};

struct ConfigEdit {
    std::string backend;
    std::string field;
    std::string old_value;
    std::string new_value;
    /// This backend names its model by path (no `model:`), so collections it
    /// embedded recorded the old path as their model.
    bool identity_changes = false;
};

struct Plan {
    models::MigrationPlan files;
    std::vector<PromotedMove> promoted;
    std::vector<ConfigEdit> edits;
    /// Old snapshot directory -> its new one, for manifests.
    std::map<std::filesystem::path, std::filesystem::path> snapshots;
    /// Moved snapshots an older pull damaged, and how badly.
    std::map<std::filesystem::path, std::size_t> damaged;

    [[nodiscard]] bool empty() const noexcept {
        return files.items.empty() && promoted.empty();
    }
};

[[nodiscard]] std::optional<harness::Config> load_quietly(const std::filesystem::path& path) {
    try {
        std::error_code code;
        if (!std::filesystem::exists(path, code)) {
            return std::nullopt;
        }
        return harness::load_config(path);
    } catch (const harness::ConfigError&) {
        return std::nullopt;
    }
}

[[nodiscard]] bool under(const std::filesystem::path& path, const std::filesystem::path& root) {
    const std::filesystem::path relative = normal(path).lexically_relative(normal(root));
    return !relative.empty() && *relative.begin() != "..";
}

[[nodiscard]] Plan make_plan(const models::StoreRoots& roots,
                             const std::optional<harness::Config>& config) {
    Plan plan;
    const auto on_hash = [](const std::filesystem::path& file) {
        std::cout << "hashing " << file.string() << " to name its directory...\n";
    };
    plan.files = models::plan_migration(roots, on_hash);

    // Where each old file now lives -- every GGUF and projector.
    std::map<std::filesystem::path, std::filesystem::path> relocated;
    for (const models::MigrationItem& item : plan.files.items) {
        if (item.kind == models::MigrationItem::Kind::Snapshot) {
            plan.snapshots[normal(item.moves.front().first)] = item.destination;
            const models::SnapshotDamage damage =
                models::find_snapshot_damage(item.moves.front().first);
            if (damage.error.empty() && !damage.empty()) {
                plan.damaged[item.destination] =
                    damage.refetch.size() + damage.stray_records.size();
            }
            continue;
        }
        for (const auto& [from, to] : item.moves) {
            if (from.extension() != ".gguf") {
                continue;
            }
            std::filesystem::path target = to;
            if (item.duplicate) {
                for (const models::StoredGguf& stored :
                     models::list_store_ggufs(roots, item.model)) {
                    if (stored.id == item.id) {
                        target = from.stem().string().ends_with("-mmproj") ? stored.projector
                                                                           : stored.file;
                    }
                }
            }
            relocated[normal(from)] = target;
        }
    }

    // Promoted versions still under training/versions/.
    const training::TrainingStore store{harness::training_dir()};
    for (const training::VersionLedger& ledger : store.all_versions()) {
        for (const training::VersionEntry& entry : ledger.versions) {
            std::error_code code;
            const std::filesystem::path file{entry.gguf_path};
            if (entry.pruned() || entry.gguf_path.empty() ||
                !std::filesystem::is_regular_file(file, code) || under(file, roots.models)) {
                continue;
            }
            PromotedMove move;
            move.backend = ledger.backend;
            move.version = entry.version;
            move.run_id = entry.run_id;
            move.promoted_at = entry.promoted_at;
            move.from = file;
            std::string model = models::safe_model_name(ledger.backend);
            if (const std::optional<training::RunManifest> manifest = store.get_run(entry.run_id)) {
                model = base_model_name(roots, store, *manifest);
            }
            on_hash(file);
            std::string id = models::weight_id_from_digest(models::file_sha256(file));
            if (id.empty()) {
                id = models::random_weight_id();
            }
            move.destination = models::weights_dir(roots, models::kGgufFormat, model, id);
            move.to = move.destination /
                      (ledger.backend + "-v" + std::to_string(entry.version) + ".gguf");
            move.duplicate = std::filesystem::exists(move.destination, code);
            if (move.duplicate) {
                for (const models::StoredGguf& stored : models::list_store_ggufs(roots, model)) {
                    if (stored.id == id) {
                        move.to = stored.file;
                    }
                }
            }
            relocated[normal(file)] = move.to;
            plan.promoted.push_back(std::move(move));
        }
    }

    // Every backend that named an old path.
    if (config.has_value()) {
        for (const auto& [name, backend] : config->backends) {
            for (const auto& [field, value] :
                 {std::pair<std::string, std::string>{"model_path", backend.model_path},
                  std::pair<std::string, std::string>{"mmproj_path", backend.mmproj_path}}) {
                if (value.empty()) {
                    continue;
                }
                const auto found = relocated.find(
                    normal(std::filesystem::path{harness::expand_env_and_home(value)}));
                if (found == relocated.end()) {
                    continue;
                }
                plan.edits.push_back({name, field, value, found->second.string(),
                                      field == "model_path" && backend.model.empty()});
            }
        }
    }
    return plan;
}

void print_plan(const Plan& plan) {
    if (!plan.files.items.empty()) {
        std::cout << "will move into the model store:\n";
        for (const models::MigrationItem& item : plan.files.items) {
            std::cout << "  " << item.moves.front().first.string() << "\n    -> "
                      << item.destination.string();
            if (item.duplicate) {
                std::cout << "\n       (identical weights are already there: the old copy stays, "
                             "for you to remove)";
            }
            if (const auto damaged = plan.damaged.find(item.destination);
                damaged != plan.damaged.end()) {
                std::cout << "\n       (damaged by an older pull: " << damaged->second
                          << " file(s) to fetch again or remove, repaired after the move)";
            }
            std::cout << "\n";
        }
    }
    if (!plan.promoted.empty()) {
        std::cout << "\nwill move promoted training versions beside the model they came from:\n";
        for (const PromotedMove& move : plan.promoted) {
            std::cout << "  " << move.backend << " v" << move.version << "  " << move.from.string()
                      << "\n    -> " << move.to.string() << "\n";
        }
    }
    if (!plan.edits.empty()) {
        std::cout << "\nwill update the config:\n";
        for (const ConfigEdit& edit : plan.edits) {
            std::cout << "  backends." << edit.backend << "." << edit.field << " -> "
                      << edit.new_value << "\n";
            if (edit.identity_changes) {
                std::cout << "    and any vector collection that recorded '" << edit.old_value
                          << "' as its embedding model is rebound to the new path\n";
            }
        }
    }
}

/// The promoted version's GGUF, moved and given the record every stored GGUF
/// has. Its ledger entry is the caller's to update.
[[nodiscard]] std::string move_promoted(const PromotedMove& move) {
    if (move.duplicate) {
        return {};
    }
    if (std::string error = models::move_path(move.from, move.to); !error.empty()) {
        return error;
    }
    models::Sidecar record;
    record.ref = move.backend + " v" + std::to_string(move.version);
    record.source = "train";
    record.pulled_at = move.promoted_at;
    record.transform = "promote";
    record.transform_note = "run " + move.run_id;
    record.verification.header_checked = true;
    record.verification.header_parsed = models::inspect_gguf(move.to).parsed;
    record.file = move.to.filename().string();
    record.file_digest = models::file_sha256(move.to);
    std::error_code code;
    record.file_size = static_cast<std::int64_t>(std::filesystem::file_size(move.to, code));
    if (!models::write_sidecar(move.to, record)) {
        return "moved " + move.to.string() + " but could not write its record";
    }
    return {};
}

void apply_plan(const Plan& plan, const std::filesystem::path& config_path) {
    for (const models::MigrationItem& item : plan.files.items) {
        if (std::string error = models::apply_migration(item); !error.empty()) {
            fail(error + " -- what moved before this stays moved; re-run to continue");
        }
        std::cout << (item.duplicate ? "kept     " : "moved    ") << item.destination.string()
                  << "\n";
    }

    // Promoted versions, and their ledgers.
    const training::TrainingStore store{harness::training_dir()};
    std::map<std::string, std::vector<const PromotedMove*>> by_backend;
    for (const PromotedMove& move : plan.promoted) {
        if (std::string error = move_promoted(move); !error.empty()) {
            fail(error);
        }
        by_backend[move.backend].push_back(&move);
        std::cout << "moved    " << move.to.string() << "\n";
    }
    for (const auto& [backend, moves] : by_backend) {
        std::string error;
        std::optional<training::VersionLedger> ledger =
            training::load_ledger(store.versions_dir(), backend, error);
        if (!ledger.has_value()) {
            fail(error.empty() ? "the ledger for '" + backend + "' vanished mid-migration" : error);
        }
        for (training::VersionEntry& entry : ledger->versions) {
            for (const PromotedMove* move : moves) {
                if (entry.version == move->version) {
                    entry.gguf_path = move->to.string();
                }
            }
        }
        if (const std::string saved = training::save_ledger(store.versions_dir(), *ledger);
            !saved.empty()) {
            fail(saved);
        }
    }

    // The config, through its one mutation path.
    if (!plan.edits.empty()) {
        harness::edit_config_file(config_path, [&plan](std::string_view content) {
            std::string edited{content};
            for (const ConfigEdit& edit : plan.edits) {
                edited =
                    edit.field == "model_path"
                        ? harness::set_backend_model_path(edited, edit.backend, edit.new_value)
                        : harness::set_backend_mmproj_path(edited, edit.backend, edit.new_value);
            }
            return edited;
        });
        std::cout << "updated  " << config_path.string() << "\n";
    }

    // Collections that recorded an old path as their embedding model.
    std::map<std::string, std::string> renamed;
    for (const ConfigEdit& edit : plan.edits) {
        if (edit.identity_changes) {
            renamed[edit.old_value] = edit.new_value;
        }
    }
    std::error_code code;
    if (!renamed.empty() && std::filesystem::is_directory(harness::embeddings_dir(), code)) {
        for (const auto& entry :
             std::filesystem::directory_iterator(harness::embeddings_dir(), code)) {
            if (entry.path().extension() != ".db") {
                continue;
            }
            try {
                embedstore::Store collection{entry.path()};
                const embedstore::Store::EmbeddingBinding binding = collection.embedding_model();
                if (const auto found = renamed.find(binding.model); found != renamed.end()) {
                    collection.set_embedding_model(found->second, binding.dimension);
                    std::cout << "rebound  " << entry.path().filename().string() << "\n";
                }
            } catch (const std::exception& e) {
                std::cout << "note: could not open " << entry.path().string() << ": " << e.what()
                          << "\n";
            }
        }
    }

    // Manifests whose base model was a moved snapshot.
    for (const training::RunSummary& summary : store.list_runs()) {
        std::string error;
        std::optional<training::RunManifest> manifest =
            training::read_manifest(store.run_dir(summary.id), error);
        if (!manifest.has_value()) {
            continue;
        }
        if (const auto found = plan.snapshots.find(normal(manifest->base_model));
            found != plan.snapshots.end()) {
            manifest->base_model = found->second.string();
            (void)training::write_manifest(store.run_dir(summary.id), *manifest);
        }
    }
    for (const training::PipelineSummary& summary : store.list_pipelines()) {
        std::string error;
        std::optional<training::PipelineRunManifest> manifest =
            training::read_pipeline_manifest(store.pipeline_dir(summary.id), error);
        if (!manifest.has_value()) {
            continue;
        }
        if (const auto found = plan.snapshots.find(normal(manifest->base_model));
            found != plan.snapshots.end()) {
            manifest->base_model = found->second.string();
            (void)training::write_pipeline_manifest(store.pipeline_dir(summary.id), *manifest);
        }
    }

    // Snapshots an older pull damaged: repaired now they are in place.
    bool repaired = true;
    for (const auto& [dir, count] : plan.damaged) {
        std::cout << "\nrepairing " << dir.string() << "\n";
        repaired = repair_snapshot_in_place(dir) && repaired;
    }
    if (!repaired) {
        fail(
            "every model moved, but a damaged snapshot could not be repaired -- re-run "
            "'apogee models repair' on it when the network is back");
    }
}

}  // namespace

void bind_model_migrate(CLI::App& models, const std::filesystem::path& models_dir,
                        const RootContext& context) {
    auto yes = std::make_shared<bool>(false);
    CLI::App* migrate =
        models.add_subcommand("migrate", "Move models from the old flat layout into the store");
    migrate->add_flag("-y,--yes", *yes, "Do it; without this, only show what would change");
    migrate->callback([yes, models_dir, &context]() {
        const models::StoreRoots roots = store_roots(models_dir, context.config_path);
        const std::filesystem::path config_path = harness::resolve_config_path(context.config_path);
        const std::optional<harness::Config> config = load_quietly(config_path);
        const Plan plan = make_plan(roots, config);
        if (plan.empty()) {
            std::cout << "nothing to migrate -- every model is already in the store.\n";
            return;
        }
        print_plan(plan);
        if (!*yes) {
            std::cout << "\nre-run with --yes to apply.\n";
            return;
        }
        std::cout << "\n";
        apply_plan(plan, config_path);
        std::cout << "\nmigrated.\n";
    });
}

}  // namespace apogee::commands
