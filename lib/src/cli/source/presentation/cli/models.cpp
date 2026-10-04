#include "cli/models.h"

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>

#include "backends/model_profile.h"
#include "backends/sampling.h"
#include "cli/models_pull.h"
#include "contracts/layout.h"
#include "contracts/paths.h"
#include "harness/roles.h"
#include "machine/json_reporter.h"
#include "models/lineage.h"
#include "modelstore/kv_cache.h"
#include "modelstore/sidecar.h"
#include "modelstore/snapshot.h"
#include "modelstore/store.h"
#include "secrets/resolve.h"
#include "secrets/store.h"

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

std::vector<ModelRow> build_model_rows(const harness::Config& config,
                                       const std::filesystem::path& models_dir,
                                       const std::filesystem::path& config_path,
                                       const secrets::EnvSnapshot* env,
                                       const BusyProgress& progress) {
    std::vector<ModelRow> rows;
    rows.reserve(config.backends.size());

    // What the model store holds -- listed before anything is read, so the
    // sweep below knows how many reads it has to do and can say so (M1).
    // Listing is a directory walk; the header reads are what take the time.
    const models::StoreRoots roots = store_roots_for(config, models_dir);
    std::vector<std::filesystem::path> configured;
    for (const auto& [key, backend] : config.backends) {
        if (!backend.model_path.empty()) {
            configured.push_back(
                std::filesystem::path{harness::expand_env_and_home(backend.model_path)}
                    .lexically_normal());
        }
    }
    const auto is_configured = [&configured](const std::filesystem::path& path) {
        return std::ranges::find(configured, path.lexically_normal()) != configured.end();
    };
    // Every stored file, its record filled in by the sweep as it reads each
    // one -- within that file's counted step, as before -- so the lineage
    // (M4), applied once everything is read, adds no read of its own.
    std::vector<models::RecordedGguf> recorded_ggufs;
    std::vector<models::RecordedSnapshot> stored_snapshots;
    if (!models_dir.empty()) {
        for (models::StoredGguf& stored : models::list_store_ggufs(roots)) {
            recorded_ggufs.push_back({.stored = std::move(stored), .record = std::nullopt});
        }
        for (models::StoredSnapshot& stored : models::list_store_snapshots(roots)) {
            stored_snapshots.push_back({.stored = std::move(stored), .record = std::nullopt});
        }
    }
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
    for (const auto& [key, backend] : config.backends) {
        if (is_local(backend.type) && !harness::expand_env(backend.model_path).empty()) {
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
        row.consumed =
            !row.attention && !is_configured(dir) && lineage.consumed(row.model).has_value();
    }

    std::ranges::sort(rows,
                      [](const ModelRow& a, const ModelRow& b) { return a.backend < b.backend; });
    return rows;
}

std::string render_model_table(const std::vector<ModelRow>& all_rows, const ansi::Style& style,
                               bool all) {
    if (all_rows.empty()) {
        return "no backends configured -- run 'apogee config init' to write a starter config\n";
    }
    // Consumed snapshots fold out unless asked for, and the fold is said (M4).
    std::vector<ModelRow> rows;
    std::size_t folded = 0;
    for (const ModelRow& row : all_rows) {
        if (row.consumed && !all) {
            ++folded;
        } else {
            rows.push_back(row);
        }
    }

    using Get = std::function<const std::string&(const ModelRow&)>;
    const std::vector<std::pair<std::string, Get>> columns{
        {"BACKEND", [](const ModelRow& r) -> const std::string& { return r.backend; }},
        {"TYPE", [](const ModelRow& r) -> const std::string& { return r.type; }},
        {"MODEL", [](const ModelRow& r) -> const std::string& { return r.model; }},
        {"ROLES", [](const ModelRow& r) -> const std::string& { return r.roles; }},
        {"SOURCE", [](const ModelRow& r) -> const std::string& { return r.provenance; }},
        {"ARCH", [](const ModelRow& r) -> const std::string& { return r.architecture; }},
        {"PROFILE", [](const ModelRow& r) -> const std::string& { return r.profile; }},
        {"STATE", [](const ModelRow& r) -> const std::string& { return r.state; }},
        {"VERIFIED", [](const ModelRow& r) -> const std::string& { return r.verified; }},
    };

    std::vector<std::size_t> widths;
    widths.reserve(columns.size());
    for (const auto& [header, get] : columns) {
        widths.push_back(width_of(rows, header, get));
    }

    std::ostringstream out;
    for (std::size_t i = 0; i < columns.size(); ++i) {
        pad(out, columns[i].first, widths[i], i + 1 == columns.size());
    }
    out << "\n";

    // Each line is laid out plain and coloured whole, so escape codes never
    // count toward a column's width.
    const auto paint = [&style](const ModelRow& row, const std::string& line) {
        if (row.attention) {
            return style.colorize(line, ansi::Color::Yellow);
        }
        return row.configured ? style.colorize(line, ansi::Color::Cyan) : style.dim(line);
    };
    for (const ModelRow& row : rows) {
        std::ostringstream line;
        for (std::size_t i = 0; i < columns.size(); ++i) {
            pad(line, columns[i].second(row), widths[i], i + 1 == columns.size());
        }
        out << paint(row, line.str()) << "\n";
        if (!row.note.empty()) {
            out << paint(row, "    " + row.note) << "\n";
        }
    }
    if (folded > 0) {
        out << "\n"
            << style.dim(folded == 1 ? "1 snapshot consumed by a conversion is folded -- --all "
                                       "lists it"
                                     : std::to_string(folded) +
                                           " snapshots consumed by conversions are folded -- "
                                           "--all lists them")
            << "\n";
    }
    return out.str();
}

std::string render_model_jsonl(const std::vector<ModelRow>& rows) {
    std::ostringstream out;
    for (const ModelRow& row : rows) {
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
        if (!row.note.empty()) {
            object["note"] = row.note;
        }
        out << object.dump() << "\n";
    }
    return out.str();
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
            << (info.has_chat_template
                    ? "the model's own"
                    : "none -- most likely a base (pretrained) model, which continues text "
                      "rather than answering; for chat, use its instruction-tuned release")
            << "\n";
        render_window(out, info, backend);
        render_sampling(out, info, backend, path);
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
            std::filesystem::path{harness::expand_env_and_home(backend.model_path)}
                    .lexically_normal() == path.lexically_normal()) {
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
                                          const models::Lineage& lineage) {
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
    if (!consumed.has_value()) {
        out << "made from it: nothing yet -- 'apogee models convert " << handle
            << "' makes a GGUF of it\n";
    } else {
        bool first = true;
        for (const std::string& gguf : consumed->by) {
            out << (first ? "made from it: " : "              ") << gguf
                << (consumed->inferred ? " (inferred)" : " (recorded)") << "\n";
            first = false;
        }
    }
    if (models::config_is_download_record(stored.dir)) {
        out << "repair:       apogee models repair " << handle << "\n";
    } else if (consumed.has_value() && backends_on(config, stored.dir).empty()) {
        out << "listing:      folded -- a conversion consumed it; 'apogee models list --all' "
               "shows it\n";
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
    for (const models::StoredSnapshot& stored : models::list_store_snapshots(roots, target.model)) {
        if (stored.id == target.id) {
            return render_snapshot(config, stored, lineage);
        }
    }
    return {};
}

}  // namespace

std::string render_model_info(const harness::Config& config, std::string_view name,
                              const BusyProgress& progress,
                              const std::filesystem::path& models_dir) {
    const auto entry = config.backends.find(std::string{name});
    if (entry == config.backends.end()) {
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

    if (!is_local(value.type)) {
        // A cloud backend has no file to inspect, and saying so beats printing
        // empty GGUF fields that read like a failed read.
        out << "source:       remote (no local file to inspect)\n";
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

std::string render_role_status(const harness::Config& config, const BusyProgress& progress) {
    std::ostringstream out;
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
        if (harness::is_helper(role) && resolved.from != harness::ResolvedFrom::RolePointer) {
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

        // Resolving and validating are separate on purpose: the resolver
        // returns a key, and each surface decides what an unconfigured one
        // means. Here it is a note; on a run path it is fatal.
        const auto entry = config.backends.find(key);
        if (entry == config.backends.end()) {
            out << "   [not configured]";
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
                const models::LocalWindow window = models::local_window(info, entry->second);
                out << "   [local: " << models::mib(info.file_size) << " of weights";
                if (window.cache_bytes.has_value()) {
                    out << ", " << models::mib(*window.cache_bytes) << " of cache";
                }
                out << "]";
            }
        }
        out << "\n";
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
    list->add_option("--output-format", *format, "text (default) or stream-json")
        ->check(CLI::IsMember({"text", "stream-json"}));
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
            BusyLine busy{std::cerr, "reading the model store",
                          busy_options(*list_quiet || *format == "stream-json")};
            rows = build_model_rows(config, harness::models_dir(),
                                    harness::resolve_config_path(context.config_path), nullptr,
                                    busy.sink());
        }
        if (*format == "stream-json") {
            std::cout << render_model_jsonl(rows);
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
    info->callback([load, info_name, info_quiet]() {
        const harness::Config config = load();
        std::string body;
        {
            BusyLine busy{std::cerr, "reading the model", busy_options(*info_quiet)};
            body = render_model_info(config, *info_name, busy.sink(), harness::models_dir());
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
                fail("'" + *info_name + "' is a model -- name one set of its weights:" + sets);
            }
            fail("no backend or stored weights named '" + *info_name + "'");
        }
        std::cout << body;
    });

    auto status_quiet = std::make_shared<bool>(false);
    CLI::App* status = cmd->add_subcommand("status", "Show which backend each role resolves to");
    status->add_flag("-q,--quiet", *status_quiet, "No progress line while the models are read");
    status->callback([load, status_quiet]() {
        const harness::Config config = load();
        std::string body;
        {
            BusyLine busy{std::cerr, "resolving the roles", busy_options(*status_quiet)};
            body = render_role_status(config, busy.sink());
        }
        std::cout << body;
    });

    // The mutating verbs live in their own translation unit, so "what can this
    // command destroy?" has a short answer.
    bind_model_mutations(*cmd, harness::models_dir(), context);
}

}  // namespace apogee::commands
