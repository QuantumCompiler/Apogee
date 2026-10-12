#include "cli/models.h"

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>

#include "backends/anthropic_wire.h"
#include "backends/google.h"
#include "backends/google_wire.h"
#include "backends/mlx_local.h"
#include "backends/model_profile.h"
#include "backends/model_roster.h"
#include "backends/openai.h"
#include "backends/openai_wire.h"
#include "backends/provider_status.h"
#include "backends/provider_table.h"
#include "backends/sampling.h"
#include "cli/helpers.h"
#include "cli/models_pull.h"
#include "contracts/layout.h"
#include "contracts/paths.h"
#include "harness/roles.h"
#include "machine/json_reporter.h"
#include "models/lineage.h"
#include "modelstore/gguf_inspect.h"
#include "modelstore/kv_cache.h"
#include "modelstore/mlx_info.h"
#include "modelstore/sidecar.h"
#include "modelstore/snapshot.h"
#include "modelstore/store.h"
#include "operations/backend_names.h"
#include "secrets/resolve.h"
#include "secrets/store.h"
#include "views/download_progress.h"

namespace apogee::commands {
namespace {

[[noreturn]] void fail(const std::string& message) {
    std::cerr << "apogee models: " << message << "\n";
    throw CLI::RuntimeError(1);
}

/// Whether this backend type names a local file Apogee can inspect.
[[nodiscard]] bool is_local(harness::BackendType type) noexcept {
    return type == harness::BackendType::LlamaCpp;
}

/// The roles pointing at `key`, as "chat, embedding".
[[nodiscard]] std::string roles_for(const harness::Config& config, const std::string& key) {
    std::string out;
    const auto note = [&out](std::string_view label) {
        if (!out.empty()) {
            out += ", ";
        }
        out += label;
    };
    // Asked through the resolver rather than compared against the raw config
    // values, so this column cannot disagree with what a run would actually do
    // -- which is the entire point of there being one resolver.
    for (const auto [role, label] : {std::pair{harness::ModelRole::Chat, "chat"},
                                     std::pair{harness::ModelRole::Embedding, "embedding"},
                                     std::pair{harness::ModelRole::Extraction, "extraction"}}) {
        if (harness::resolve_backend_key(config, harness::RoleRequest{.role = role}) == key) {
            note(label);
        }
    }
    return out;
}

/// Column widths for an aligned table.
[[nodiscard]] std::size_t width_of(const std::vector<ModelRow>& rows, std::string_view header,
                                   const std::function<const std::string&(const ModelRow&)>& get) {
    std::size_t width = header.size();
    for (const ModelRow& row : rows) {
        width = std::max(width, get(row).size());
    }
    return width;
}

void pad(std::ostringstream& out, const std::string& value, std::size_t width, bool last) {
    out << value;
    if (!last) {
        out << std::string(width - value.size() + 2, ' ');
    }
}

/// The behaviour profile a model resolves to, and whether it was verified.
///
/// "unprofiled" is a real and common answer, not a placeholder: under the
/// open-model policy any model runs, and one Apogee has never characterized is
/// handled permissively rather than refused. Marking an unverified profile as
/// such matters for the same reason -- a documented guess is worth using and
/// worth labelling.
[[nodiscard]] std::string describe_profile(std::string_view architecture,
                                           std::string_view filename) {
    const backends::ModelProfile* profile = backends::resolve_profile({}, architecture, filename);
    if (profile == nullptr) {
        return "unprofiled";
    }
    return profile->verified ? profile->name : profile->name + " (unverified)";
}

/// What a model's sidecar says was checked when it was acquired.
[[nodiscard]] std::string describe_record(const std::optional<models::Sidecar>& sidecar) {
    // "no record" rather than "unverified": a model placed by hand is
    // legitimate, it simply has nothing to be rechecked against.
    return sidecar.has_value() ? sidecar->verification.summary() : "no record";
}

/// A path as a comparison wants it: normal, and a directory written with its
/// trailing separator the same as one without.
[[nodiscard]] std::filesystem::path comparable(const std::filesystem::path& path) {
    const std::filesystem::path normal = path.lexically_normal();
    return normal.has_filename() ? normal : normal.parent_path();
}

/// What a stored MLX model's record says about where it came from (27b) --
/// a pull from its repository, or `convert --mlx` of a SafeTensors set --
/// and whether that set is still on disk.
[[nodiscard]] std::string mlx_origin(const std::optional<models::Snapshot>& record,
                                     const models::StoreRoots& roots) {
    if (!record.has_value() || record->source.empty()) {
        return "unknown -- no record says where it came from";
    }
    if (record->source == "convert") {
        std::string line = "converted from " + record->ref + " (recorded" +
                           (record->transform.empty() ? "" : ", " + record->transform) + ")";
        const models::StoreTarget target = models::resolve_store_target(roots, record->ref);
        if (!target.error.empty()) {
            line += " -- source snapshot no longer on disk";
        }
        return line;
    }
    std::string ref = record->ref;
    if (!record->revision.empty() && record->revision != "main") {
        ref += "@" + record->revision;
    }
    return "pulled from " + ref +
           (record->source == "huggingface" ? " (Hugging Face)" : " (" + record->source + ")");
}

/// `info`'s lines for an MLX model directory (27b): whether its files are
/// whole, how its weights are stored, the window `backend` gives it, and
/// its size -- all from its files.
void render_mlx_files(std::ostream& out, const models::MlxInfo& info,
                      const harness::BackendConfig& backend) {
    out << "files:        "
        << (info.complete
                ? "whole -- config.json, a tokenizer and " + std::to_string(info.shards.size()) +
                      " shard(s), each holding every byte its header lists"
                : info.problem)
        << "\n";
    if (info.config_read) {
        out << "quantization: " << info.quantization.describe()
            << (info.mlx_format
                    ? ""
                    : "; not in mlx-lm's own format -- it converts the weights as it loads them")
            << "\n";
    }
    const models::MlxWindow window = models::mlx_window(info, backend);
    out << "window:       ";
    if (window.window <= 0) {
        out << "unknown -- config.json could not be read; set context_size\n";
    } else {
        out << window.window << " tokens (" << (window.configured ? "context_size" : "the default");
        if (window.trained > 0) {
            out << "; trained for " << window.trained;
        }
        out << ")\n";
    }
    out << "size:         " << format_progress_size(static_cast<std::int64_t>(info.bytes))
        << " on disk\n";
}

/// The store `models_dir` and the config's `paths.hf_dir` make.
[[nodiscard]] models::StoreRoots store_roots_for(const harness::Config& config,
                                                 const std::filesystem::path& models_dir) {
    models::StoreRoots roots = models::StoreRoots::at(models_dir);
    if (!config.paths.hf_dir.empty()) {
        roots.safetensors =
            std::filesystem::path{harness::expand_env_and_home(config.paths.hf_dir)};
    }
    return roots;
}

/// A source as a user knows it.
[[nodiscard]] std::string source_name(std::string_view source) {
    if (source == "huggingface") {
        return "Hugging Face";
    }
    if (source == "ollama") {
        return "Ollama";
    }
    return std::string{source};
}

/// One link of a chain, as `info` says it (M4). A derivation says whether a
/// record or the store's shape is the evidence, and a broken link what is gone.
[[nodiscard]] std::string describe_link(const models::LineageLink& link) {
    const models::Origin& origin = link.origin;
    switch (origin.kind) {
        case models::OriginKind::Unknown:
            return "unknown -- no record says where it came from";
        case models::OriginKind::Pulled:
            return "pulled from " + origin.from +
                   (origin.detail.empty() ? "" : " (" + source_name(origin.detail) + ")");
        case models::OriginKind::Converted:
            return "converted from " + origin.from +
                   (origin.inferred ? " (inferred)" : " (recorded)") +
                   (link.missing ? " -- source snapshot no longer on disk" : "");
        case models::OriginKind::Quantized:
            return "quantized" + (origin.detail.empty() ? "" : " to " + origin.detail) + " from " +
                   origin.from + " (recorded)" + (link.missing ? " -- no longer on disk" : "");
        case models::OriginKind::Trained:
            return "a fine-tune" + (origin.detail.empty() ? "" : " (" + origin.detail + ")") +
                   ", promoted as " + origin.from;
    }
    return {};
}

/// `info`'s lineage lines: the chain back from `origin`, one link a line.
void render_lineage(std::ostream& out, const models::Lineage& lineage,
                    const models::Origin& origin) {
    bool first = true;
    for (const models::LineageLink& link : lineage.chain(origin)) {
        out << (first ? "lineage:      " : "              ") << describe_link(link) << "\n";
        first = false;
    }
}

/// `info`'s window and cache lines: what a conversation costs on top of the
/// weights -- memory a user never asked for, visible where they look (26a).
void render_window(std::ostream& out, const models::GgufInfo& info,
                   const harness::BackendConfig& backend) {
    const models::LocalWindow window = models::local_window(info, backend);
    out << "window:       " << window.window << " tokens ("
        << (window.configured ? "context_size" : "the default");
    if (window.trained > 0) {
        out << "; trained for " << window.trained;
    }
    out << ")\n";
    out << "cache:        ";
    if (!window.cache_bytes.has_value()) {
        out << "not known for this architecture\n";
    } else if (*window.cache_bytes == 0) {
        out << "none -- no attention layers\n";
    } else {
        out << models::mib(*window.cache_bytes) << " at " << harness::to_string(window.cache_type)
            << (backend.cache_type.has_value() ? "" : " (the default)");
        if (window.sliding_positions > 0) {
            // Why a Gemma's cache is small: most of its layers keep only
            // their window (26m).
            out << ", sliding layers at " << window.sliding_positions << " positions";
        }
        out << "\n";
    }
}

}  // namespace

namespace {

/// A provider row's STATE and VERIFIED from the one tier structure (28c): a
/// vendor CLI's binary there or not -- `no binary` needing attention, with
/// the use-time error's remedy -- and for every provider the day it last
/// answered a turn. An API row's STATE stays where its key comes from, which
/// is that tier's evidence already.
void fill_provider_columns(ModelRow& row, const harness::BackendConfig& backend,
                           const ProviderLens& providers, const secrets::CredentialStore* store,
                           const secrets::EnvSnapshot& env) {
    if (providers.view == nullptr) {
        return;
    }
    const backends::ProviderCache none;
    const std::optional<backends::ProviderStatus> status = backends::backend_provider_status(
        backend, *providers.view, store, env, providers.cache != nullptr ? *providers.cache : none);
    if (!status.has_value()) {
        return;
    }
    row.verified = status->verified.has_value() ? status->verified->date : "no turn yet";
    if (!harness::is_vendor_cli(backend.type)) {
        return;
    }
    if (!status->installed) {
        row.state = "no binary";
        row.attention = true;
        row.note = status->installed_evidence + ". " +
                   std::string{backends::provider_for_type(backend.type)->remedy};
        return;
    }
    backends::ProviderStatus present = *status;
    present.verified.reset();  // VERIFIED says that; STATE is what is there
    row.state = std::string{backends::to_string(present.tier())};
}

}  // namespace

std::vector<ModelRow> build_model_rows(const harness::Config& config,
                                       const std::filesystem::path& models_dir,
                                       const std::filesystem::path& config_path,
                                       const secrets::EnvSnapshot* env,
                                       const BusyProgress& progress,
                                       const ProviderLens& providers) {
    std::vector<ModelRow> rows;
    rows.reserve(config.backends.size());

    // What the model store holds -- listed before anything is read, so the
    // sweep below knows how many reads it has to do and can say so (M1).
    // Listing is a directory walk; the header reads are what take the time.
    const models::StoreRoots roots = store_roots_for(config, models_dir);
    std::vector<std::filesystem::path> configured;
    for (const auto& [key, backend] : config.backends) {
        if (!backend.model_path.empty()) {
            configured.push_back(comparable(
                std::filesystem::path{harness::expand_env_and_home(backend.model_path)}));
        }
    }
    const auto is_configured = [&configured](const std::filesystem::path& path) {
        return std::ranges::find(configured, comparable(path)) != configured.end();
    };
    // Every stored file, its record filled in by the sweep as it reads each
    // one -- within that file's counted step, as before -- so the lineage
    // (M4), applied once everything is read, adds no read of its own.
    std::vector<models::RecordedGguf> recorded_ggufs;
    std::vector<models::RecordedSnapshot> stored_snapshots;
    std::vector<models::StoredMlx> stored_mlx;
    if (!models_dir.empty()) {
        for (models::StoredGguf& stored : models::list_store_ggufs(roots)) {
            recorded_ggufs.push_back({.stored = std::move(stored), .record = std::nullopt});
        }
        for (models::StoredSnapshot& stored : models::list_store_snapshots(roots)) {
            stored_snapshots.push_back({.stored = std::move(stored), .record = std::nullopt});
        }
        stored_mlx = models::list_store_mlx(roots);
    }
    // The SafeTensors sets `convert --mlx` made an MLX model of, by handle --
    // a conversion consumes its source as a GGUF's does (27b).
    std::vector<std::string> mlx_sources;
    std::vector<std::size_t> stored_ggufs;
    for (std::size_t at = 0; at < recorded_ggufs.size(); ++at) {
        if (!is_configured(recorded_ggufs[at].stored.file)) {
            stored_ggufs.push_back(at);
        }
        // else its backend's row already says everything
    }
    // A local file's record, read as its row is built: kept for the lineage
    // when the file is stored.
    const auto record_for = [&recorded_ggufs](const std::filesystem::path& path) {
        std::optional<models::Sidecar> record = models::load_sidecar(path);
        const std::filesystem::path normal = path.lexically_normal();
        for (models::RecordedGguf& gguf : recorded_ggufs) {
            if (gguf.stored.file.lexically_normal() == normal) {
                gguf.record = record;
            }
        }
        return record;
    };

    // The rows the lineage decides, once every record is in: each GGUF row's
    // file and record, and each snapshot row's directory.
    struct GgufRow {
        std::size_t row;
        std::filesystem::path file;
        std::optional<models::Sidecar> record;
    };

    std::vector<GgufRow> gguf_rows;
    std::vector<std::pair<std::size_t, std::filesystem::path>> snapshot_rows;
    std::size_t reads = stored_ggufs.size() + stored_snapshots.size();
    for (const models::StoredMlx& stored : stored_mlx) {
        reads += is_configured(stored.dir) ? 0 : 1;
    }
    for (const auto& [key, backend] : config.backends) {
        if ((is_local(backend.type) || backend.type == harness::BackendType::Mlx) &&
            !harness::expand_env(backend.model_path).empty()) {
            ++reads;
        }
    }
    std::size_t read = 0;
    const auto reading = [&](std::string_view what, const std::string& name) {
        if (progress) {
            progress(std::string{what} + name, ++read, reads);
        }
    };
    std::optional<secrets::CredentialStore> store;
    if (!config_path.empty()) {
        store.emplace(secrets::credentials_path(config_path));
    }
    const secrets::EnvSnapshot& snapshot = env != nullptr ? *env : secrets::EnvSnapshot::process();

    for (const auto& [key, backend] : config.backends) {
        ModelRow row;
        row.backend = key;
        row.configured = true;
        row.type = std::string{harness::to_string(backend.type)};
        row.roles = roles_for(config, key);
        // Until the model-profiles item lands nothing resolves a profile, and
        // saying so is the honest answer rather than a placeholder: the
        // permissive-unknown rule means an unprofiled model is handled, not
        // broken.
        row.profile = "unprofiled";

        if (backend.type == harness::BackendType::Mlx) {
            // A model directory and a Python runtime (27a): what the
            // directory says, and whether the ladder lets it run -- read from
            // files, never by starting the driver -- and since 27b its files
            // read whole, its quantization and the window it gets.
            const backends::MlxReadiness readiness =
                backends::probe_mlx_backend(key, backend, backends::MlxHost::current());
            const std::filesystem::path dir{harness::expand_env_and_home(backend.model_path)};
            row.model = backend.model.empty() ? dir.filename().string() : backend.model;
            row.provenance = "local";
            row.format = std::string{models::kMlxFormat};
            const std::optional<models::StoredMlx> stored = models::stored_mlx_at(stored_mlx, dir);
            if (stored.has_value() && backend.model.empty()) {
                row.model = models::weights_handle(stored->model, models::kMlxFormat, stored->id);
            }
            const backends::MlxModelInfo info = backends::inspect_mlx_model(dir);
            row.architecture = info.model_type.empty() ? "-" : info.model_type;
            if (const backends::ModelProfile* family =
                    backends::resolve_mlx_profile(info, backend.model + " " + dir.string())) {
                row.profile = family->name;
            }
            const std::optional<models::Snapshot> record = models::load_snapshot(dir);
            row.verified = record.has_value()
                               ? std::to_string(record->files.size()) + " file(s) on record"
                               : "-";
            row.state =
                readiness.ready() ? "ready" : std::string{backends::to_string(readiness.refusal)};
            if (!backend.model_path.empty()) {
                reading("reading MLX models: ", row.model);
                const models::MlxInfo files = models::read_mlx_info(dir);
                if (files.config_read) {
                    row.quant = files.quantization.describe();
                }
                row.window = models::describe(models::mlx_window(files, backend));
                if (readiness.ready() && !files.complete) {
                    row.state = "cannot load";
                    row.attention = true;
                    row.note = files.problem;
                } else if (readiness.ready()) {
                    row.note = mlx_summary(files, backend);
                }
            }
            if (!readiness.ready()) {
                row.attention = true;
                row.note = readiness.message();
            }
            rows.push_back(std::move(row));
            continue;
        }

        if (!is_local(backend.type)) {
            row.model = backend.model;
            row.provenance = "-";
            row.architecture = "-";
            row.state = "-";
            if (secrets::takes_api_key(backend.type)) {
                // The one chain -- so this column says what a build would
                // use, and only WHERE it came from. Never the key.
                const secrets::KeyResolution resolved = secrets::resolve_api_key(
                    backend, store.has_value() ? &*store : nullptr, snapshot);
                switch (resolved.source) {
                    case secrets::KeySource::Config:
                        row.state = "key: config";
                        break;
                    case secrets::KeySource::Store:
                        row.state = "key: store";
                        break;
                    case secrets::KeySource::Environment:
                        row.state = "key: " + resolved.variable;
                        break;
                    case secrets::KeySource::None:
                        row.state = "no key";
                        row.attention = true;
                        row.note = "no API key found; run 'apogee auth add " +
                                   std::string{harness::to_string(backend.type)} + "'";
                        break;
                }
            }
            row.verified = "-";
            fill_provider_columns(row, backend, providers, store.has_value() ? &*store : nullptr,
                                  snapshot);
            rows.push_back(std::move(row));
            continue;
        }

        const std::string expanded = harness::expand_env(backend.model_path);
        row.provenance = "local";
        row.architecture = "-";
        if (expanded.empty()) {
            row.model = backend.model;
            row.state = "missing";
            row.attention = true;
            row.note = "no model_path set";
            rows.push_back(std::move(row));
            continue;
        }

        const std::filesystem::path path{expanded};
        row.model = path.filename().string();
        row.format = std::string{models::kGgufFormat};
        reading("reading model headers: ", row.model);
        const std::optional<models::Sidecar> record = record_for(path);
        row.verified = describe_record(record);
        gguf_rows.push_back({.row = rows.size(), .file = path, .record = record});

        const models::GgufInfo info = models::inspect_gguf(path);
        if (!info.parsed) {
            row.state = std::filesystem::exists(path) ? "unreadable" : "missing";
            row.attention = true;
            row.note = info.parse_error;
        } else {
            row.state = "ok";
            row.architecture = info.architecture.empty() ? "(absent)" : info.architecture;
            // The same ladder the provider uses, asked the same way -- so this
            // column cannot claim a profile the run would not resolve.
            row.profile = describe_profile(info.architecture, path.filename().string());
            if (info.is_projector()) {
                row.attention = true;
                row.note = "a multimodal projector (" + std::to_string(info.tensors) +
                           " vision tensors) -- point a backend's mmproj_path at this, not "
                           "model_path";
            } else if (info.has_vision_tensors()) {
                row.note = "combined text+vision blob (" +
                           std::to_string(info.tensors - info.text_tensors) + " vision tensors)";
            }
        }
        rows.push_back(std::move(row));
    }

    // The rest: what the model store holds that no backend points at --
    // everything `models pull`, `convert`, `quantize` and `train promote` have
    // made, until the user wires it up. Each by the handle the other verbs
    // take: `<model>/<format>/<id>`.
    if (!models_dir.empty()) {
        for (const std::size_t at : stored_ggufs) {
            const models::StoredGguf& stored = recorded_ggufs[at].stored;
            reading("reading model headers: ", stored.file.filename().string());
            ModelRow row;
            // Not a backend: it is a file waiting to be pointed at.
            row.backend = "(not configured)";
            row.type = "-";
            row.model = models::weights_handle(stored.model, models::kGgufFormat, stored.id);
            row.format = std::string{models::kGgufFormat};
            const std::optional<models::Sidecar> record = record_for(stored.file);
            row.provenance =
                record.has_value() && !record->source.empty() ? record->source : "local";
            row.verified = describe_record(record);
            gguf_rows.push_back({.row = rows.size(), .file = stored.file, .record = record});

            const models::GgufInfo info = models::inspect_gguf(stored.file);
            row.state = info.parsed ? "ok" : "unreadable";
            row.architecture = info.parsed && !info.architecture.empty() ? info.architecture : "-";
            row.profile = info.parsed
                              ? describe_profile(info.architecture, stored.file.filename().string())
                              : "unprofiled";
            if (!info.parsed) {
                row.attention = true;
                row.note = info.parse_error;
            } else if (info.has_vision_tensors()) {
                row.note = "combined text+vision blob (" +
                           std::to_string(info.tensors - info.text_tensors) + " vision tensors)";
            } else {
                row.note = stored.file.filename().string() +
                           (stored.projector.empty() ? "" : " + vision projector");
            }
            rows.push_back(std::move(row));
        }

        // SafeTensors sets -- trainable, not runnable -- listed so a user can
        // see what `convert` and `apogee train` can take, without a backend
        // ever pointing at one.
        for (models::RecordedSnapshot& recorded : stored_snapshots) {
            const models::StoredSnapshot& stored = recorded.stored;
            reading("reading snapshots: ", stored.model);
            ModelRow row;
            row.backend = "(not configured)";
            row.type = "-";
            row.model = models::weights_handle(stored.model, models::kSafetensorsFormat, stored.id);
            row.format = std::string{models::kSafetensorsFormat};
            recorded.record = models::load_snapshot(stored.dir);
            const std::optional<models::Snapshot>& record = recorded.record;
            row.provenance =
                record.has_value() && !record->source.empty() ? record->source : "local";
            const std::string architecture = models::snapshot_architecture(stored.dir);
            row.architecture = architecture.empty() ? "-" : architecture;
            row.profile = "-";
            row.state = "safetensors";
            row.verified = record.has_value()
                               ? std::to_string(record->files.size()) + " file(s) on record"
                               : "no record";
            // No note for a healthy one: its state column already says what
            // it is, and a line under every snapshot was noise.
            if (models::config_is_download_record(stored.dir)) {
                row.attention = true;
                row.note = "damaged by an older pull -- 'apogee models repair " + stored.model +
                           "/safetensors/" + stored.id + "'";
            }
            snapshot_rows.emplace_back(rows.size(), stored.dir);
            rows.push_back(std::move(row));
        }

        // MLX models (27b) -- runnable on an mlx backend, so listed with what
        // a user picks one by: its quantization and the window it gets --
        // the directory read whole, or said why it cannot load.
        for (const models::StoredMlx& stored : stored_mlx) {
            const std::optional<models::Snapshot> record = models::load_snapshot(stored.dir);
            if (record.has_value() && record->source == "convert") {
                mlx_sources.push_back(record->ref);
            }
            if (is_configured(stored.dir)) {
                continue;  // its backend's row says everything
            }
            reading("reading MLX models: ", stored.model);
            ModelRow row;
            row.backend = "(not configured)";
            row.type = "-";
            row.model = models::weights_handle(stored.model, models::kMlxFormat, stored.id);
            row.format = std::string{models::kMlxFormat};
            row.provenance = "local";
            if (record.has_value() && !record->source.empty()) {
                row.provenance = record->source == "convert" ? "converted" : record->source;
            }
            const models::MlxInfo info = models::read_mlx_info(stored.dir);
            row.architecture = info.model_type.empty() ? "-" : info.model_type;
            const backends::ModelProfile* family = backends::resolve_mlx_profile(
                backends::inspect_mlx_model(stored.dir), stored.model);
            row.profile = family == nullptr ? "unprofiled" : family->name;
            row.state = info.complete ? "mlx" : "cannot load";
            row.verified = record.has_value()
                               ? std::to_string(record->files.size()) + " file(s) on record"
                               : "no record";
            if (info.config_read) {
                row.quant = info.quantization.describe();
            }
            row.window = models::describe(models::mlx_window(info, {}));
            row.attention = !info.complete;
            row.note = info.complete ? mlx_summary(info) : info.problem;
            rows.push_back(std::move(row));
        }

        // The flat layout this one replaced: shown, never used in place.
        const models::LegacyLayout legacy = models::find_legacy(roots);
        for (const models::LegacyGguf& gguf : legacy.ggufs) {
            ModelRow row;
            row.backend = "(not configured)";
            row.type = "-";
            row.model = gguf.file.filename().string();
            row.provenance = "local";
            row.attention = true;
            row.architecture = "-";
            row.profile = "-";
            row.state = "old layout";
            row.verified = "-";
            row.note = "run 'apogee models migrate' to move it into the model store";
            rows.push_back(std::move(row));
        }
        for (const models::LegacySnapshot& flat : legacy.snapshots) {
            ModelRow row;
            row.backend = "(not configured)";
            row.type = "-";
            row.model = flat.dir.filename().string() + "/";
            row.provenance = "local";
            row.attention = true;
            row.architecture = "-";
            row.profile = "-";
            row.state = "old layout";
            row.verified = "-";
            row.note = "run 'apogee models migrate' to move it into the model store";
            rows.push_back(std::move(row));
        }
    }

    // Every record read: where each file came from (M4). A GGUF made from a
    // snapshot says so; a snapshot whose job is done -- a conversion made a
    // GGUF of it -- is folded from the table, never when it needs attention
    // or a backend points at it, and nothing about the store changes.
    const models::Lineage lineage{std::move(recorded_ggufs), std::move(stored_snapshots)};
    for (const GgufRow& gguf : gguf_rows) {
        if (lineage.converted(lineage.origin_of(gguf.file, gguf.record))) {
            rows[gguf.row].provenance = "converted";
        }
    }
    for (const auto& [at, dir] : snapshot_rows) {
        ModelRow& row = rows[at];
        row.consumed = !row.attention && !is_configured(dir) &&
                       (lineage.consumed(row.model).has_value() ||
                        std::ranges::find(mlx_sources, row.model) != mlx_sources.end());
    }

    // Every model a configured entry runs beyond its own (M13, 35): each
    // configured type's roster -- fetched, or carried for a CLI that prints
    // none -- as a row under its owner, named as it is selected
    // (`<owner>:<id>`), so it sorts beside the entry. The owner is the type's
    // first entry by key, as a bare id resolves. Zero network here: fetching
    // is registration's and --refresh's, never a listing's.
    for (const auto& [type, roster] : backends::known_rosters().rosters) {
        const auto owner =
            std::ranges::find_if(config.backends, [&wanted = type](const auto& entry) {
                return harness::to_string(entry.second.type) == wanted;
            });
        if (owner == config.backends.end()) {
            continue;
        }
        for (const backends::RosterModel& model : roster.models) {
            ModelRow row;
            row.backend = owner->first + ":" + model.id;
            row.type = type;
            row.model = model.id;
            row.provenance = roster.built_in ? "built in" : "roster";
            row.architecture = "-";
            row.profile = "-";
            row.state = "-";
            row.verified = (roster.built_in ? "reviewed " : "fetched ") + roster.fetched_at;
            row.roster = true;
            rows.push_back(std::move(row));
        }
    }

    std::ranges::sort(rows,
                      [](const ModelRow& a, const ModelRow& b) { return a.backend < b.backend; });
    return rows;
}

ModelGroup model_group(const ModelRow& row) {
    const std::optional<harness::BackendType> type = harness::backend_type_from_string(row.type);
    const auto local = [](std::string id, std::string title, std::size_t order) {
        return ModelGroup{
            .section = "local", .id = std::move(id), .title = std::move(title), .order = order};
    };
    // A local model by what it is stored as, else by what runs it.
    constexpr std::size_t kLocal = 1000;
    if (row.format == models::kSafetensorsFormat) {
        return local("safetensors", "SafeTensors", kLocal);
    }
    if (row.format == models::kGgufFormat || type == harness::BackendType::LlamaCpp) {
        return local("gguf", "GGUF", kLocal + 1);
    }
    if (row.format == models::kMlxFormat || type == harness::BackendType::Mlx) {
        return local("mlx", "MLX", kLocal + 2);
    }
    // A provider by its row in the providers' table -- any number of them,
    // in that table's order. The Ollama CLI runs models on this machine.
    if (type.has_value()) {
        if (const backends::ProviderFacts* facts = backends::provider_for_type(*type);
            facts != nullptr) {
            std::string id{harness::to_string(*type)};
            std::string title{facts->label};
            if (*type == harness::BackendType::OllamaCli) {
                return local(std::move(id), std::move(title), kLocal + 3);
            }
            return ModelGroup{
                .section = "cloud",
                .id = std::move(id),
                .title = std::move(title),
                .order = static_cast<std::size_t>(facts - backends::provider_table().data())};
        }
    }
    return local("other", "Other", kLocal + 4);
}

std::string render_model_table(const std::vector<ModelRow>& all_rows, const ansi::Style& style,
                               bool all) {
    if (all_rows.empty()) {
        return "no backends configured -- run 'apogee config init' to write a starter config\n";
    }

    // The tables, in their order, each with its rows in the listing's order
    // (a provider's models under their entry). Consumed snapshots fold out
    // unless asked for, said under the SafeTensors table (M4).
    struct Table {
        ModelGroup group;
        std::vector<ModelRow> rows;
    };

    std::vector<Table> tables;
    std::size_t folded = 0;
    const auto table_for = [&tables](const ModelGroup& group) -> Table& {
        for (Table& table : tables) {
            if (table.group.id == group.id) {
                return table;
            }
        }
        tables.push_back(Table{.group = group, .rows = {}});
        return tables.back();
    };
    for (const ModelRow& row : all_rows) {
        Table& table = table_for(model_group(row));
        if (row.consumed && !all) {
            ++folded;
        } else {
            table.rows.push_back(row);
        }
    }
    std::ranges::stable_sort(
        tables, [](const Table& a, const Table& b) { return a.group.order < b.group.order; });

    using Get = std::function<const std::string&(const ModelRow&)>;

    struct Column {
        std::string header;
        Get get;
        bool local_only = false;
    };

    const std::vector<Column> columns{
        {"BACKEND", [](const ModelRow& r) -> const std::string& { return r.backend; }},
        {"MODEL", [](const ModelRow& r) -> const std::string& { return r.model; }},
        {"ROLES", [](const ModelRow& r) -> const std::string& { return r.roles; }},
        {"SOURCE", [](const ModelRow& r) -> const std::string& { return r.provenance; }},
        {"ARCH", [](const ModelRow& r) -> const std::string& { return r.architecture; }, true},
        {"PROFILE", [](const ModelRow& r) -> const std::string& { return r.profile; }, true},
        {"STATE", [](const ModelRow& r) -> const std::string& { return r.state; }},
        {"VERIFIED", [](const ModelRow& r) -> const std::string& { return r.verified; }},
    };

    // Each line is laid out plain and coloured whole, so escape codes never
    // count toward a column's width.
    const auto paint = [&style](const ModelRow& row, const std::string& line) {
        if (row.attention) {
            return style.colorize(line, ansi::Color::Yellow);
        }
        return row.configured ? style.colorize(line, ansi::Color::Cyan) : style.dim(line);
    };
    const auto fold_line = [&style, folded]() {
        return style.dim(folded == 1 ? "1 snapshot consumed by a conversion is folded -- --all "
                                       "lists it"
                                     : std::to_string(folded) +
                                           " snapshots consumed by conversions are folded -- "
                                           "--all lists them") +
               "\n";
    };

    std::ostringstream out;
    std::string section;
    for (const Table& table : tables) {
        const bool safetensors = table.group.id == models::kSafetensorsFormat;
        if (table.rows.empty() && !(safetensors && folded > 0)) {
            continue;
        }
        if (table.group.section != section) {
            section = table.group.section;
            out << (out.tellp() > 0 ? "\n" : "")
                << style.bold(section == "cloud" ? "Cloud backends" : "Local backends") << "\n";
        }
        out << "\n"
            << style.bold(table.group.title)
            << (table.group.title == table.group.id || table.group.section == "local"
                    ? std::string{}
                    : style.dim(" · " + table.group.id))
            << "\n";
        // What this table says: its heading names the type, and a column
        // empty in every row says nothing.
        std::vector<std::pair<const Column*, std::size_t>> shown;
        for (const Column& column : columns) {
            if (column.local_only && table.group.section != "local") {
                continue;
            }
            const bool says = std::ranges::any_of(table.rows, [&column](const ModelRow& row) {
                const std::string& value = column.get(row);
                return !value.empty() && value != "-";
            });
            if (says || column.header == "BACKEND") {
                shown.emplace_back(&column, width_of(table.rows, column.header, column.get));
            }
        }
        if (!table.rows.empty()) {
            for (std::size_t i = 0; i < shown.size(); ++i) {
                pad(out, shown[i].first->header, shown[i].second, i + 1 == shown.size());
            }
            out << "\n";
        }
        for (const ModelRow& row : table.rows) {
            std::ostringstream line;
            for (std::size_t i = 0; i < shown.size(); ++i) {
                pad(line, shown[i].first->get(row), shown[i].second, i + 1 == shown.size());
            }
            out << paint(row, line.str()) << "\n";
            if (!row.note.empty()) {
                out << paint(row, "    " + row.note) << "\n";
            }
        }
        if (safetensors && folded > 0) {
            out << fold_line();
        }
    }
    return out.str();
}

nlohmann::json model_row_json(const ModelRow& row) {
    nlohmann::json object;
    object["type"] = "model";
    object["backend"] = row.backend;
    object["backend_type"] = row.type;
    object["model"] = row.model;
    object["roles"] = row.roles;
    object["source"] = row.provenance;
    object["architecture"] = row.architecture;
    object["profile"] = row.profile;
    object["state"] = row.state;
    object["verified"] = row.verified;
    // Where `models list` tables it (36): additive, the rows' order unchanged.
    const ModelGroup group = model_group(row);
    object["section"] = group.section;
    object["group"] = group.id;
    if (!row.note.empty()) {
        object["note"] = row.note;
    }
    for (const auto& [key, value] :
         {std::pair<const char*, const std::string&>{"format", row.format},
          std::pair<const char*, const std::string&>{"quant", row.quant},
          std::pair<const char*, const std::string&>{"window", row.window}}) {
        if (!value.empty()) {
            object[key] = value;
        }
    }
    return object;
}

std::string render_model_jsonl(const std::vector<ModelRow>& rows) {
    std::ostringstream out;
    for (const ModelRow& row : rows) {
        out << model_row_json(row).dump() << "\n";
    }
    return out.str();
}

nlohmann::json render_model_document(const std::vector<ModelRow>& rows, bool all) {
    // The table's rows, folded as the table folds them (M4) and the fold
    // counted -- the same facts (28h).
    nlohmann::json data = nlohmann::json::array();
    std::size_t folded = 0;
    for (const ModelRow& row : rows) {
        if (row.consumed && !all) {
            ++folded;
            continue;
        }
        data.push_back(model_row_json(row));
    }
    return nlohmann::json{{"object", "list"}, {"data", std::move(data)}, {"folded", folded}};
}

nlohmann::json render_record_document(std::string_view text) {
    // `info` and `status` print a record of `label: value` lines, a value
    // running on over the indented lines under it: the same record, as data,
    // its labels as printed (28h).
    nlohmann::json fields = nlohmann::json::array();
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        const std::string_view line = text.substr(start, end - start);
        start = end + 1;
        if (line.empty()) {
            continue;
        }
        const std::size_t colon = line.find(':');
        if (line.front() == ' ' || colon == std::string_view::npos) {
            if (!fields.empty()) {
                std::string value = fields.back()["value"].get<std::string>();
                std::string_view continued = line;
                while (!continued.empty() && continued.front() == ' ') {
                    continued.remove_prefix(1);
                }
                value += value.empty() ? "" : "\n";
                value += continued;
                fields.back()["value"] = value;
            }
            continue;
        }
        std::string_view value = line.substr(colon + 1);
        while (!value.empty() && value.front() == ' ') {
            value.remove_prefix(1);
        }
        fields.push_back(nlohmann::json{{"field", std::string{line.substr(0, colon)}},
                                        {"value", std::string{value}}});
    }
    return nlohmann::json{{"fields", std::move(fields)}};
}

namespace {

/// `info`'s sampling line (26h): what an answer from this model samples with
/// and where each value came from -- the config, the file's own
/// recommendation, its family's card, or llama.cpp's neutral value. With
/// thinking on, as a conversation starts.
void render_sampling(std::ostream& out, const models::GgufInfo& info,
                     const harness::BackendConfig& backend, const std::filesystem::path& path) {
    const backends::ModelProfile* family =
        backends::resolve_profile({}, info.architecture, path.filename().string());
    const backends::ResolvedSampling resolved = backends::resolve_sampling(backends::SamplingLadder{
        .config = backends::config_rung(backend),
        .model_file = backends::model_file_rung(info.sampling),
        .family = backends::family_rung(family, true),
        .family_source = family == nullptr ? std::string{} : family->sampling_source,
        .seed = backends::config_seed(backend)});
    out << "sampling:     " << backends::describe_sampling(resolved) << "\n";
}

/// `info`'s thinking line (26i): the mode and budget a conversation starts
/// with -- each the config's or the default -- and what this backend does
/// with them, or that it has no such control.
[[nodiscard]] std::string describe_thinking(const harness::BackendConfig& backend,
                                            const models::GgufInfo* header = nullptr) {
    const harness::ThinkingMode mode = backend.thinking.value_or(harness::ThinkingMode::On);
    std::string line = std::string{harness::to_string(mode)} +
                       (backend.thinking.has_value() ? " (config)" : " (default)");
    line += backend.thinking_budget.has_value()
                ? ", at most " + std::to_string(*backend.thinking_budget) + " tokens (config)"
                : ", no budget";
    switch (backend.type) {
        case harness::BackendType::LlamaCpp:
            // What the model's own template says it can do -- a switch to
            // render, reasoning to count, or neither.
            if (header == nullptr || !header->has_chat_template) {
                return line + " -- off renders the template's own switch, where it has one";
            }
            if (header->template_thinking.switchable) {
                return line +
                       " -- off renders the template's own switch; a budget counts "
                       "between its reasoning tags";
            }
            if (header->template_thinking.reasons) {
                return line +
                       " -- the template has no off switch, so this model thinks as "
                       "trained; a budget counts between its reasoning tags";
            }
            return line + " -- the template names no reasoning: nothing to switch off or count";
        case harness::BackendType::Anthropic:
            return line + " -- Anthropic thinks only with a budget, at least " +
                   std::to_string(backends::anthropic::kMinThinkingBudget) +
                   " tokens; off sends none";
        case harness::BackendType::OpenAI: {
            const std::string model =
                backend.model.empty() ? backends::OpenAIProvider::Options{}.model : backend.model;
            return backends::openai::is_reasoning_model(model)
                       ? line + " -- sent as the reasoning effort; off is the model's lowest"
                       : line + " -- " + model + " does not reason, so nothing is sent";
        }
        case harness::BackendType::Google: {
            const std::string model =
                backend.model.empty() ? backends::GoogleProvider::Options{}.model : backend.model;
            return backends::google::model_thinks(model)
                       ? line + " -- sent as Gemini's thinking budget; off asks for the least"
                       : line + " -- " + model + " does not think, so nothing is sent";
        }
        case harness::BackendType::Mlx:
            // The driver hands the switch to the model's own template (27a);
            // there is no sampler to count a budget with.
            return line +
                   " -- off renders the template's own switch, where it has one; a budget is "
                   "not applied (the mlx backend has no budget sampler)";
        case harness::BackendType::ClaudeCli:
        case harness::BackendType::CodexCli:
        case harness::BackendType::GeminiCli:
        case harness::BackendType::OllamaCli:
        case harness::BackendType::Mock:
            break;
    }
    return line + " -- no control here: this backend thinks as it decides";
}

/// The local-only sampling knobs `backend` sets (26h), which a cloud backend
/// does not send: its vendor is sampled with the temperature alone.
[[nodiscard]] std::string unsent_sampling(const harness::BackendConfig& backend) {
    std::string out;
    const auto add = [&out](bool set, std::string_view key) {
        if (set) {
            out += (out.empty() ? "" : ", ") + std::string{key};
        }
    };
    add(backend.top_p.has_value(), "top_p");
    add(backend.top_k.has_value(), "top_k");
    add(backend.min_p.has_value(), "min_p");
    add(backend.repeat_penalty.has_value(), "repeat_penalty");
    add(backend.presence_penalty.has_value(), "presence_penalty");
    add(backend.seed.has_value(), "seed");
    return out;
}

/// `info`'s header block for the GGUF at `path`: what the file says, and for a
/// model, the window `backend` gives it.
void render_gguf(std::ostream& out, const std::filesystem::path& path,
                 const harness::BackendConfig& backend, const BusyProgress& progress) {
    if (progress) {
        progress("reading " + path.filename().string(), 0, 0);
    }
    const models::GgufInfo info = models::inspect_gguf(path);
    if (!info.parsed) {
        // The reason, always. An unreadable header rendering as blank fields is
        // the exact failure this surface exists to prevent.
        out << "header:       FAILED -- " << info.parse_error << "\n";
        // A stored file names itself: <model>/gguf/<id>/<file>.
        const std::filesystem::path id_dir = path.parent_path();
        if (models::is_weight_id(id_dir.filename().string()) &&
            id_dir.parent_path().filename() == models::kGgufFormat) {
            out << "repair:       apogee models repair "
                << id_dir.parent_path().parent_path().filename().string() << "/gguf/"
                << id_dir.filename().string() << "\n";
        } else {
            out << "repair:       re-download the file, or point model_path at another\n";
        }
        return;
    }

    out << "header:       ok (GGUF v" << info.version << ")\n";
    out << "architecture: " << (info.architecture.empty() ? "(absent)" : info.architecture) << "\n";
    // Stated separately from the architecture above, and stated at all rather
    // than omitted: a user comparing two models needs to know how much Apogee
    // actually knows about each.
    const std::string profile = describe_profile(info.architecture, path.filename().string());
    out << "profile:      " << profile << "\n";
    if (const backends::ModelProfile* resolved =
            backends::resolve_profile({}, info.architecture, path.filename().string());
        resolved != nullptr) {
        // The evidence line is what makes `verified` auditable rather than a
        // bare adjective: it says which model was run, and when.
        out << "evidence:     " << resolved->evidence << "\n";
    }
    if (!info.name.empty()) {
        out << "name:         " << info.name << "\n";
    }
    out << "tensors:      " << info.tensors << " total, " << info.text_tensors << " text\n";
    out << "size:         " << (info.file_size / (1024LL * 1024)) << " MiB\n";
    if (!info.is_projector()) {
        // A file without one is almost always a base model, and a chat with
        // it reads as a broken chat unless it is said where a user looks.
        out << "template:     "
            << (info.has_chat_template ? std::string{"the model's own"}
                                       : "none, so it is " + models::base_model_note())
            << "\n";
        render_window(out, info, backend);
        render_sampling(out, info, backend, path);
        out << "thinking:     " << describe_thinking(backend, &info) << "\n";
    }
    if (info.is_projector()) {
        out << "note:         a multimodal projector -- belongs on mmproj_path, not model_path\n";
    } else if (info.has_vision_tensors()) {
        out << "note:         combined text+vision blob -- " << (info.tensors - info.text_tensors)
            << " vision/projector tensors\n";
    }
}

/// The backends whose `model_path` is `path`, as "a, b".
[[nodiscard]] std::string backends_on(const harness::Config& config,
                                      const std::filesystem::path& path) {
    std::string out;
    for (const auto& [key, backend] : config.backends) {
        if (!backend.model_path.empty() &&
            comparable(std::filesystem::path{harness::expand_env_and_home(backend.model_path)}) ==
                comparable(path)) {
            out += (out.empty() ? "" : ", ") + key;
        }
    }
    return out;
}

/// `info` on one stored GGUF, by its handle: the file, where it came from,
/// and its header -- for a model no backend points at yet (M4).
[[nodiscard]] std::string render_stored_gguf(const harness::Config& config,
                                             const models::StoredGguf& stored,
                                             const models::Lineage& lineage,
                                             const BusyProgress& progress) {
    std::ostringstream out;
    out << "weights:      " << models::weights_handle(stored.model, models::kGgufFormat, stored.id)
        << "\n";
    const std::string backends = backends_on(config, stored.file);
    out << "backends:     "
        << (backends.empty() ? "none -- 'apogee config add-backend <name> --type llamacpp "
                               "--model-path <the path below>'"
                             : backends)
        << "\n";
    out << "model_path:   " << stored.file.string() << "\n";
    if (!stored.projector.empty()) {
        out << "mmproj_path:  " << stored.projector.string() << "\n";
    }
    render_lineage(out, lineage, lineage.origin_of(stored.file, std::nullopt));
    render_gguf(out, stored.file, harness::BackendConfig{}, progress);
    return out.str();
}

/// `info` on one stored SafeTensors set: where it came from, what was made
/// of it, and whether the listing folds it (M4).
[[nodiscard]] std::string render_snapshot(const harness::Config& config,
                                          const models::StoredSnapshot& stored,
                                          const models::Lineage& lineage,
                                          const models::StoreRoots& roots) {
    const std::string handle =
        models::weights_handle(stored.model, models::kSafetensorsFormat, stored.id);
    std::ostringstream out;
    out << "weights:      " << handle << "\n";
    out << "path:         " << stored.dir.string() << "\n";
    out << "format:       SafeTensors -- full weights, trainable, not runnable\n";
    render_lineage(out, lineage, lineage.origin_of(handle));
    const std::string architecture = models::snapshot_architecture(stored.dir);
    out << "architecture: " << (architecture.empty() ? "-" : architecture) << "\n";
    const std::optional<models::Snapshot> record = models::load_snapshot(stored.dir);
    out << "record:       "
        << (record.has_value() ? std::to_string(record->files.size()) + " file(s) on record"
                               : "none")
        << "\n";
    const std::optional<models::Consumption> consumed = lineage.consumed(handle);
    // An MLX model made of it consumed it too (27b): its record says so.
    std::vector<std::string> mlx_made;
    for (const models::StoredMlx& mlx : models::list_store_mlx(roots, stored.model)) {
        const std::optional<models::Snapshot> made = models::load_snapshot(mlx.dir);
        if (made.has_value() && made->source == "convert" && made->ref == handle) {
            mlx_made.push_back(models::weights_handle(mlx.model, models::kMlxFormat, mlx.id));
        }
    }
    if (!consumed.has_value() && mlx_made.empty()) {
        out << "made from it: nothing yet -- 'apogee models convert " << handle
            << "' makes a GGUF of it (--mlx an MLX model)\n";
    } else {
        bool first = true;
        if (consumed.has_value()) {
            for (const std::string& gguf : consumed->by) {
                out << (first ? "made from it: " : "              ") << gguf
                    << (consumed->inferred ? " (inferred)" : " (recorded)") << "\n";
                first = false;
            }
        }
        for (const std::string& mlx : mlx_made) {
            out << (first ? "made from it: " : "              ") << mlx << " (recorded)\n";
            first = false;
        }
    }
    if (models::config_is_download_record(stored.dir)) {
        out << "repair:       apogee models repair " << handle << "\n";
    } else if ((consumed.has_value() || !mlx_made.empty()) &&
               backends_on(config, stored.dir).empty()) {
        out << "listing:      folded -- a conversion consumed it; 'apogee models list --all' "
               "shows it\n";
    }
    return out.str();
}

/// `info` on one stored MLX model (27b): where it came from, what a backend
/// over it gets, and its files read whole -- for a model no backend points
/// at yet, the one command that changes that.
[[nodiscard]] std::string render_stored_mlx(const harness::Config& config,
                                            const models::StoredMlx& stored,
                                            const models::StoreRoots& roots) {
    std::ostringstream out;
    out << "weights:      " << models::weights_handle(stored.model, models::kMlxFormat, stored.id)
        << "\n";
    const std::string backends = backends_on(config, stored.dir);
    out << "backends:     "
        << (backends.empty()
                ? "none -- 'apogee config add-backend " + models::stored_mlx_name(stored) +
                      " --type mlx --model-path <the path below>'"
                : backends)
        << "\n";
    out << "model_path:   " << stored.dir.string() << "\n";
    out << "format:       MLX -- runs on an mlx backend (Apple silicon)\n";
    out << "lineage:      " << mlx_origin(models::load_snapshot(stored.dir), roots) << "\n";
    const models::MlxInfo info = models::read_mlx_info(stored.dir);
    out << "model_type:   " << (info.model_type.empty() ? "-" : info.model_type) << "\n";
    const backends::MlxModelInfo facts = backends::inspect_mlx_model(stored.dir);
    out << "template:     "
        << (facts.chat_template ? "ships with the model" : "none -- a base model, offered no tools")
        << "\n";
    render_mlx_files(out, info, harness::BackendConfig{});
    if (!info.complete) {
        out << "repair:       apogee models repair "
            << models::weights_handle(stored.model, models::kMlxFormat, stored.id) << "\n";
    }
    return out.str();
}

/// `info` on one set of weights in the store, by its handle or id (M4);
/// empty when `name` names none.
[[nodiscard]] std::string render_stored_weights(const harness::Config& config,
                                                std::string_view name, const BusyProgress& progress,
                                                const std::filesystem::path& models_dir) {
    const models::StoreRoots roots = store_roots_for(config, models_dir);
    const models::StoreTarget target = models::resolve_store_target(roots, name);
    if (!target.error.empty() || target.outside || target.id.empty()) {
        return {};
    }
    if (progress) {
        progress("reading the store's records", 0, 0);
    }
    const models::Lineage lineage = models::read_lineage(roots);
    if (target.format == models::kGgufFormat) {
        for (const models::StoredGguf& stored : models::list_store_ggufs(roots, target.model)) {
            if (stored.id == target.id) {
                return render_stored_gguf(config, stored, lineage, progress);
            }
        }
        return {};
    }
    if (target.format == models::kMlxFormat) {
        for (const models::StoredMlx& stored : models::list_store_mlx(roots, target.model)) {
            if (stored.id == target.id) {
                return render_stored_mlx(config, stored, roots);
            }
        }
        return {};
    }
    for (const models::StoredSnapshot& stored : models::list_store_snapshots(roots, target.model)) {
        if (stored.id == target.id) {
            return render_snapshot(config, stored, lineage, roots);
        }
    }
    return {};
}

}  // namespace

namespace {

/// `models info` for a model an entry runs by name (35): the entry, the list
/// that names the model -- or none, when it is handed to the vendor as given
/// -- and the spelling that selects it.
[[nodiscard]] std::string render_pinned_model(const harness::Config& config,
                                              const SessionModel& pin) {
    const harness::BackendConfig* entry = config.find_backend(pin.backend);
    const std::string type =
        entry != nullptr ? std::string{harness::to_string(entry->type)} : std::string{};
    std::string listed = "no list names it -- handed to " + pin.backend + " as given";
    if (const backends::RosterCache rosters = backends::known_rosters();
        const backends::ProviderRoster* roster = rosters.roster_for(type)) {
        for (const backends::RosterModel& model : roster->models) {
            if (model.id == pin.pinned) {
                listed = roster->built_in ? "built into apogee (" + model.name + "), reviewed " +
                                                roster->fetched_at
                                          : type + "'s roster, fetched " + roster->fetched_at;
                break;
            }
        }
    }
    const std::string selected = pin.backend + ":" + pin.pinned;
    std::ostringstream out;
    out << "model:        " << pin.pinned << "\n";
    out << "backend:      " << pin.backend << " (" << type << ")\n";
    out << "listed:       " << listed << "\n";
    out << "runs with:    /model " << selected << " -- or 'apogee chat -m " << selected << "'\n";
    return out.str();
}

}  // namespace

std::string render_model_info(const harness::Config& config, std::string_view name,
                              const BusyProgress& progress, const std::filesystem::path& models_dir,
                              const ProviderLens& providers) {
    const auto entry = config.backends.find(std::string{name});
    if (entry == config.backends.end()) {
        // A model an entry runs by name (33-35): a roster's, or
        // `<backend>:<model>` -- which entry, from which list, how to pick it.
        if (const SessionModel pin = resolve_session_model(config, name); !pin.pinned.empty()) {
            return render_pinned_model(config, pin);
        }
        // Not a backend: one set of weights in the store (M4).
        return models_dir.empty() ? std::string{}
                                  : render_stored_weights(config, name, progress, models_dir);
    }
    const harness::BackendConfig& value = entry->second;

    std::ostringstream out;
    out << "backend:      " << name << "\n";
    out << "type:         " << harness::to_string(value.type) << "\n";
    if (!value.model.empty()) {
        out << "model:        " << value.model << "\n";
    }
    const std::string roles = roles_for(config, std::string{name});
    out << "roles:        " << (roles.empty() ? "-" : roles) << "\n";
    if (providers.view != nullptr) {
        // A provider backend's evidence line (28c): the tier with what backs
        // it, and the day it last answered a turn -- the cache and the cheap
        // checks, nothing started.
        const backends::ProviderCache none;
        if (const std::optional<backends::ProviderStatus> status =
                backends::backend_provider_status(
                    value, *providers.view, providers.store, secrets::EnvSnapshot::process(),
                    providers.cache != nullptr ? *providers.cache : none)) {
            out << "provider:     " << backends::describe_status(*status) << "\n";
            out << "verified:     "
                << (status->verified.has_value()
                        ? "last answered a turn on " + status->verified->date + " (" +
                              status->verified->backend + ")"
                        : std::string{"no turn recorded yet"})
                << "\n";
        }
    }

    if (value.type == harness::BackendType::Mlx) {
        // A model directory run by the MLX driver (27a): the ladder's answer,
        // what the directory says, and how it samples -- all from files.
        const backends::MlxReadiness readiness =
            backends::probe_mlx_backend(name, value, backends::MlxHost::current());
        const std::filesystem::path dir{harness::expand_env_and_home(value.model_path)};
        out << "model_path:   " << (value.model_path.empty() ? "(unset)" : dir.string()) << "\n";
        out << "runtime:      "
            << (readiness.ready()
                    ? "ready -- mlx-lm " +
                          (readiness.version.empty() ? std::string{"(version unknown)"}
                                                     : readiness.version) +
                          " in " + readiness.interpreter.parent_path().parent_path().string()
                    : "cannot run -- " + readiness.message())
            << "\n";
        if (!value.model_path.empty()) {
            if (!models_dir.empty()) {
                // Where it came from, when it is stored (27b).
                const models::StoreRoots roots = store_roots_for(config, models_dir);
                if (const std::optional<models::StoredMlx> stored =
                        models::stored_mlx_at(models::list_store_mlx(roots), dir)) {
                    out << "weights:      "
                        << models::weights_handle(stored->model, models::kMlxFormat, stored->id)
                        << "\n"
                        << "lineage:      " << mlx_origin(models::load_snapshot(dir), roots)
                        << "\n";
                }
            }
            const backends::MlxModelInfo info = backends::inspect_mlx_model(dir);
            out << "model_type:   " << (info.model_type.empty() ? "-" : info.model_type) << "\n";
            if (progress) {
                progress("reading " + dir.filename().string(), 0, 0);
            }
            render_mlx_files(out, models::read_mlx_info(dir), value);
            out << "template:     "
                << (info.chat_template ? "ships with the model"
                                       : "none -- a base model, offered no tools")
                << "\n";
            // Whether a picture is read as it is (27c): the same file facts
            // the provider answers by, so neither claims what the other denies.
            const backends::MlxVision vision =
                backends::probe_mlx_vision(info, backends::MlxHost::current());
            out << "vision:       "
                << (vision.reads_images()
                        ? "reads images as they are -- mlx-vlm " +
                              (vision.vlm.version.empty() ? std::string{"(version unstated)"}
                                                          : vision.vlm.version)
                        : "no -- " + vision.reason +
                              (vision.remedy.empty() ? std::string{} : " (" + vision.remedy + ")"))
                << "\n";
            const backends::ModelProfile* family =
                backends::resolve_mlx_profile(info, value.model + " " + dir.string());
            out << "profile:      " << (family == nullptr ? "unprofiled" : family->name) << "\n";
            const backends::ResolvedSampling resolved =
                backends::resolve_sampling(backends::SamplingLadder{
                    .config = backends::config_rung(value),
                    .model_file = info.sampling,
                    .family = backends::family_rung(family, true),
                    .family_source = family == nullptr ? std::string{} : family->sampling_source,
                    .seed = backends::config_seed(value)});
            out << "sampling:     " << backends::describe_sampling(resolved) << "\n";
        }
        out << "thinking:     " << describe_thinking(value) << "\n";
        return out.str();
    }

    if (!is_local(value.type)) {
        // A cloud backend has no file to inspect, and saying so beats printing
        // empty GGUF fields that read like a failed read.
        out << "source:       remote (no local file to inspect)\n";
        out << "thinking:     " << describe_thinking(value) << "\n";
        if (const std::string unsent = unsent_sampling(value); !unsent.empty()) {
            out << "sampling:     " << unsent << " set but not sent -- this "
                << harness::to_string(value.type)
                << " backend samples with the temperature alone\n";
        }
        return out.str();
    }

    const std::string expanded = harness::expand_env(value.model_path);
    if (expanded.empty()) {
        out << "model_path:   (unset)\n";
        out << "header:       skipped -- no model_path to read\n";
        return out.str();
    }

    out << "model_path:   " << expanded << "\n";
    const std::filesystem::path path{expanded};
    if (!models_dir.empty()) {
        // Where it came from, back to the upstream it was pulled as (M4).
        const models::Lineage lineage = models::read_lineage(store_roots_for(config, models_dir));
        render_lineage(out, lineage, lineage.origin_of(path, models::load_sidecar(path)));
    }
    render_gguf(out, path, value, progress);
    return out.str();
}

std::vector<ModelRow> read_model_rows(const harness::Config& config,
                                      const std::filesystem::path& config_path,
                                      const BusyProgress& progress) {
    const std::unique_ptr<backends::ExistenceView> view = backends::host_existence_view();
    const backends::ProviderCache cache = backends::load_provider_cache();
    return build_model_rows(config, harness::models_dir(), config_path, nullptr, progress,
                            ProviderLens{view.get(), &cache});
}

std::string read_model_info(const harness::Config& config, std::string_view name,
                            const std::filesystem::path& config_path,
                            const BusyProgress& progress) {
    const std::unique_ptr<backends::ExistenceView> view = backends::host_existence_view();
    const backends::ProviderCache cache = backends::load_provider_cache();
    const secrets::CredentialStore store{secrets::credentials_path(config_path)};
    return render_model_info(config, name, progress, harness::models_dir(),
                             ProviderLens{view.get(), &cache, &store});
}

std::string render_role_status(const harness::Config& config, const BusyProgress& progress,
                               const MachineBudgetSource& machine) {
    std::ostringstream out;
    // The suite every role below resolves under, when one is active (27d).
    // With none, nothing here changes from before suites.
    const harness::SuiteConfig* suite = harness::active_suite(config);
    std::string suite_name;
    for (const auto& [name, entry] : config.suites) {
        if (&entry == suite) {
            suite_name = name;  // as the file spells it
        }
    }
    if (suite != nullptr) {
        out << "suite: " << suite_name
            << (suite->description.empty() ? "" : "   (" + suite->description + ")") << "\n";
    }
    for (const auto [role, label] : {std::pair{harness::ModelRole::Chat, "chat"},
                                     std::pair{harness::ModelRole::Embedding, "embedding"},
                                     std::pair{harness::ModelRole::Extraction, "extraction"},
                                     std::pair{harness::ModelRole::Vision, "vision"},
                                     std::pair{harness::ModelRole::Transcription, "transcription"},
                                     std::pair{harness::ModelRole::Utility, "utility"}}) {
        const harness::Resolution resolved =
            harness::resolve_backend(config, harness::RoleRequest{.role = role});
        const std::string& key = resolved.key;

        out << label << ": ";
        // A helper with no pointer of its own runs on whatever the chat is
        // on, which is not a fact this command can know: say so rather than
        // naming models.default as if it were the answer.
        if (harness::is_helper(role) && !harness::is_named(resolved.from)) {
            out << "(unset -- the chat's own backend)\n";
            continue;
        }
        if (key.empty()) {
            out << "(unset -- no models.default configured)\n";
            continue;
        }
        out << key;

        // Which rung answered, straight from the resolver. Working it out here
        // would mean re-reading the chain, and a second reading is a second
        // chain -- which cli.one_role_resolver would (correctly) reject.
        if (resolved.from == harness::ResolvedFrom::Default && role != harness::ModelRole::Chat) {
            out << "   (via models.default)";
        }
        if (resolved.from == harness::ResolvedFrom::Suite) {
            out << "   (via suite " << suite_name << ")";
        }

        // Resolving and validating are separate on purpose: the resolver
        // returns a key, and each surface decides what an unconfigured one
        // means. Here it is a note; on a run path it is fatal.
        const auto entry = config.backends.find(key);
        if (entry == config.backends.end()) {
            out << "   [not configured]";
        } else if (role == harness::ModelRole::Vision &&
                   entry->second.type == harness::BackendType::Mlx) {
            // Whether this vision model can read a picture at all (27c): a
            // vision role that cannot is a gap the attachment meets later.
            const backends::MlxVision vision = backends::probe_mlx_vision(
                backends::inspect_mlx_model(
                    std::filesystem::path{harness::expand_env_and_home(entry->second.model_path)}),
                backends::MlxHost::current());
            out << (vision.reads_images()
                        ? "   [mlx: reads images as they are]"
                        : "   [mlx: cannot read images -- " + vision.reason +
                              (vision.remedy.empty() ? std::string{} : "; " + vision.remedy) + "]");
        } else if (entry->second.type == harness::BackendType::LlamaCpp &&
                   !entry->second.model_path.empty()) {
            // What loading it costs: a helper is a second model resident
            // beside the chat's, and that memory should be visible (26b).
            if (progress) {
                // No count: which roles read a header is only known as each
                // resolves, and a total guessed ahead would be a lie.
                progress("reading " + key + "'s model header", 0, 0);
            }
            const models::GgufInfo info = models::inspect_gguf(
                std::filesystem::path{harness::expand_env(entry->second.model_path)});
            if (info.parsed) {
                // At the window the backend runs at: the suite's pin, while
                // one is active (27d).
                const models::LocalWindow window =
                    models::local_window(info, harness::backend_as_run(config, key));
                out << "   [local: " << models::mib(info.file_size) << " of weights";
                if (window.cache_bytes.has_value()) {
                    out << ", " << models::mib(*window.cache_bytes) << " of cache";
                }
                out << "]";
            }
        }
        // What the suite pins on the backend, said where it holds (27d).
        if (entry != config.backends.end()) {
            if (const harness::MemberPins pins = harness::suite_pins(config, key);
                pins.context_size.has_value() || pins.toolset.has_value()) {
                std::string pinned;
                if (pins.context_size.has_value()) {
                    pinned = "window " + std::to_string(*pins.context_size);
                }
                if (pins.toolset.has_value()) {
                    std::string names;
                    for (const std::string& name : *pins.toolset) {
                        names += names.empty() ? "" : ",";
                        names += name;
                    }
                    pinned += std::string{pinned.empty() ? "" : " · "} + "toolset " +
                              (names.empty() ? std::string{"none"} : names);
                }
                out << "   [suite pins " << pinned << "]";
            }
        }
        out << "\n";
    }
    // The suite as a set (27e): what its members take of this machine.
    if (suite != nullptr) {
        if (progress) {
            progress("pricing suite " + suite_name, 0, 0);
        }
        for (const std::string& line : footprint_lines(price_suite(config, suite_name, machine))) {
            out << line << "\n";
        }
    }
    return out.str();
}

std::string_view ModelsCommand::name() const noexcept {
    return "models";
}

std::string_view ModelsCommand::summary() const noexcept {
    return "List configured models, inspect one, and show role assignments";
}

void ModelsCommand::bind(CLI::App& root, const RootContext& context) {
    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    cmd->require_subcommand(1);

    const auto load = [&context]() {
        try {
            return harness::load_config(harness::resolve_config_path(context.config_path));
        } catch (const std::exception& e) {
            fail(e.what());
        }
    };

    auto format = std::make_shared<std::string>();
    auto no_color = std::make_shared<bool>(false);
    auto list_quiet = std::make_shared<bool>(false);
    auto list_all = std::make_shared<bool>(false);
    CLI::App* list = cmd->add_subcommand("list", "List configured backends and their models");
    list->add_option("--output-format", *format,
                     "text (default), json -- one document of the table's facts -- or "
                     "stream-json, one row per line")
        ->check(CLI::IsMember({"text", "json", "stream-json"}))
        ->type_name("text|json|stream-json");
    list->add_flag("--no-color", *no_color, "Disable coloured output");
    list->add_flag("-q,--quiet", *list_quiet, "No progress line while the models are read");
    list->add_flag("--all", *list_all,
                   "Also list the SafeTensors sets a conversion has already consumed");
    list->callback([load, format, no_color, list_quiet, list_all, &context]() {
        const harness::Config config = load();
        std::vector<ModelRow> rows;
        {
            // Every stored model's header is read, and on a full store that
            // takes seconds: said on one line, gone before the table (M1).
            BusyLine busy{
                std::cerr, "reading the model store",
                busy_options(*list_quiet || *format == "stream-json" || *format == "json")};
            rows = read_model_rows(config, harness::resolve_config_path(context.config_path),
                                   busy.sink());
        }
        if (*format == "stream-json") {
            std::cout << render_model_jsonl(rows);
            return;
        }
        if (*format == "json") {
            write_document(std::cout, render_model_document(rows, *list_all));
            return;
        }
        std::cout << render_model_table(
            rows, ansi::Style::detect(*no_color ? ansi::ColorMode::Never : ansi::ColorMode::Auto),
            *list_all);
    });

    auto info_name = std::make_shared<std::string>();
    auto info_quiet = std::make_shared<bool>(false);
    CLI::App* info = cmd->add_subcommand(
        "info", "Show one backend's model, or one set of stored weights, in detail");
    info->add_option("name", *info_name,
                     "A backend key from the config, or stored weights: <model>/<format>/<id> "
                     "or an id")
        ->type_name(kBackendOrWeightsValue)
        ->required();
    info->add_flag("-q,--quiet", *info_quiet, "No progress line while the model is read");
    auto info_format = std::make_shared<ReadFormat>(ReadFormat::Text);
    add_read_format(info, info_format);
    info->callback([load, info_name, info_quiet, info_format, &context]() {
        const harness::Config config = load();
        std::string body;
        {
            BusyLine busy{std::cerr, "reading the model",
                          busy_options(*info_quiet || *info_format == ReadFormat::Json)};
            body = read_model_info(config, *info_name,
                                   harness::resolve_config_path(context.config_path), busy.sink());
        }
        if (body.empty()) {
            // A whole model is not one thing to show: name what it holds.
            const models::StoreRoots roots = store_roots_for(config, harness::models_dir());
            const models::StoreTarget target = models::resolve_store_target(roots, *info_name);
            if (target.error.empty() && !target.outside && target.id.empty()) {
                std::string sets;
                for (const models::StoredGguf& stored :
                     models::list_store_ggufs(roots, target.model)) {
                    if (target.format.empty() || target.format == models::kGgufFormat) {
                        sets += "\n  " + models::weights_handle(stored.model, models::kGgufFormat,
                                                                stored.id);
                    }
                }
                for (const models::StoredSnapshot& stored :
                     models::list_store_snapshots(roots, target.model)) {
                    if (target.format.empty() || target.format == models::kSafetensorsFormat) {
                        sets += "\n  " + models::weights_handle(
                                             stored.model, models::kSafetensorsFormat, stored.id);
                    }
                }
                for (const models::StoredMlx& stored :
                     models::list_store_mlx(roots, target.model)) {
                    if (target.format.empty() || target.format == models::kMlxFormat) {
                        sets += "\n  " +
                                models::weights_handle(stored.model, models::kMlxFormat, stored.id);
                    }
                }
                fail("'" + *info_name + "' is a model -- name one set of its weights:" + sets);
            }
            fail("no backend or stored weights named '" + *info_name + "'");
        }
        if (*info_format == ReadFormat::Json) {
            nlohmann::json document = render_record_document(body);
            document["name"] = *info_name;
            write_document(std::cout, document);
            return;
        }
        std::cout << body;
    });

    auto status_quiet = std::make_shared<bool>(false);
    CLI::App* status = cmd->add_subcommand("status", "Show which backend each role resolves to");
    status->add_flag("-q,--quiet", *status_quiet, "No progress line while the models are read");
    const auto status_suite = std::make_shared<std::string>();
    status
        ->add_option("--suite", *status_suite,
                     "Resolve under this suite instead of models.default_suite, or off for none")
        ->type_name(kModelSuiteOrOffValue);
    auto status_format = std::make_shared<ReadFormat>(ReadFormat::Text);
    add_read_format(status, status_format);
    status->callback([load, status_quiet, status_suite, status_format]() {
        harness::Config config = load();
        if (!status_suite->empty()) {
            if (const std::string refused = select_suite(config, *status_suite); !refused.empty()) {
                fail("--suite: " + refused);
            }
        }
        std::string body;
        {
            BusyLine busy{std::cerr, "resolving the roles",
                          busy_options(*status_quiet || *status_format == ReadFormat::Json)};
            body = render_role_status(config, busy.sink(), machine_budget);
        }
        if (*status_format == ReadFormat::Json) {
            write_document(std::cout, render_record_document(body));
            return;
        }
        std::cout << body;
    });

    // The mutating verbs live in their own translation unit, so "what can this
    // command destroy?" has a short answer.
    bind_model_mutations(*cmd, harness::models_dir(), context);
}

}  // namespace apogee::commands
