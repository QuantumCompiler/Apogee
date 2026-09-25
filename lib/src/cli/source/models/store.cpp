#include "models/store.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <optional>
#include <random>
#include <set>
#include <system_error>
#include <utility>

#include "models/sha256.h"
#include "models/sidecar.h"
#include "models/source_hf.h"
#include "platform/platform.h"

namespace apogee::models {
namespace {

constexpr std::string_view kIncomingPrefix = ".incoming-";

[[nodiscard]] bool hidden(const std::filesystem::path& path) {
    const std::string name = path.filename().string();
    return name.empty() || name.front() == '.';
}

[[nodiscard]] bool is_format(std::string_view text) noexcept {
    return text == kGgufFormat || text == kSafetensorsFormat;
}

/// The visible subdirectories of `dir`, sorted.
[[nodiscard]] std::vector<std::filesystem::path> subdirectories(const std::filesystem::path& dir) {
    std::vector<std::filesystem::path> out;
    std::error_code code;
    if (!std::filesystem::is_directory(dir, code)) {
        return out;
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir, code)) {
        if (code) {
            break;
        }
        if (entry.is_directory(code) && !hidden(entry.path()) &&
            !entry.path().filename().string().ends_with(".staging")) {
            out.push_back(entry.path());
        }
    }
    std::ranges::sort(out);
    return out;
}

[[nodiscard]] bool is_projector_file(const std::filesystem::path& path) {
    return path.stem().string().ends_with("-mmproj");
}

/// The one `*-mmproj.gguf` directly inside `dir`, if any.
[[nodiscard]] std::optional<std::filesystem::path> projector_in(const std::filesystem::path& dir) {
    std::error_code code;
    for (const auto& entry : std::filesystem::directory_iterator(dir, code)) {
        if (entry.is_regular_file(code) && entry.path().extension() == ".gguf" &&
            is_projector_file(entry.path())) {
            return entry.path();
        }
    }
    return std::nullopt;
}

/// The model a flat-layout GGUF belongs to: its record's ref when it has one.
[[nodiscard]] std::string legacy_model_name(const std::filesystem::path& file) {
    if (const std::optional<Sidecar> record = load_sidecar(file); record.has_value()) {
        if (record->source == "huggingface") {
            if (const std::optional<HfRef> ref = parse_hf_ref(record->ref); ref.has_value()) {
                return repo_directory_name(*ref);
            }
        }
        if (record->source == "ollama" && !record->ref.empty()) {
            return safe_model_name(record->ref);
        }
    }
    return safe_model_name(file.stem().string());
}

[[nodiscard]] bool under(const std::filesystem::path& path, const std::filesystem::path& root) {
    const std::filesystem::path relative = path.lexically_relative(root);
    return !relative.empty() && *relative.begin() != "..";
}

}  // namespace

StoreRoots StoreRoots::at(const std::filesystem::path& models) {
    return StoreRoots{models, models};
}

const std::filesystem::path& StoreRoots::root_for(std::string_view format) const noexcept {
    return format == kSafetensorsFormat ? safetensors : models;
}

std::string safe_model_name(std::string_view text) {
    std::string name;
    name.reserve(text.size());
    for (const char c : text) {
        name += (c == '/' || c == ':' || c == '@' || c == ' ' || c == '\\') ? '-' : c;
    }
    const std::size_t first = name.find_first_not_of('.');
    name = first == std::string::npos ? std::string{} : name.substr(first);
    return name.empty() ? std::string{"model"} : name;
}

bool is_weight_id(std::string_view text) noexcept {
    return text.size() == kWeightIdLength && std::ranges::all_of(text, [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}

std::string weight_id_from_digest(std::string_view sha256) {
    if (sha256.starts_with("sha256:")) {
        sha256.remove_prefix(7);
    }
    if (sha256.size() != 64) {
        return {};
    }
    std::string id;
    for (const char c : sha256.substr(0, kWeightIdLength)) {
        const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (!((lower >= '0' && lower <= '9') || (lower >= 'a' && lower <= 'f'))) {
            return {};
        }
        id += lower;
    }
    return id;
}

std::string snapshot_weight_id(const std::vector<SnapshotFile>& files) {
    std::vector<const SnapshotFile*> shards;
    for (const SnapshotFile& file : files) {
        if (file.path.ends_with(".safetensors")) {
            if (weight_id_from_digest(file.sha256).empty()) {
                return {};
            }
            shards.push_back(&file);
        }
    }
    if (shards.empty()) {
        return {};
    }
    std::ranges::sort(
        shards, [](const SnapshotFile* a, const SnapshotFile* b) { return a->path < b->path; });
    Sha256 hasher;
    for (const SnapshotFile* shard : shards) {
        std::string digest = shard->sha256;
        std::ranges::transform(digest, digest.begin(), [](char c) {
            return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        });
        hasher.update(shard->path + "\t" + digest + "\n");
    }
    return hasher.hex_digest().substr(0, kWeightIdLength);
}

std::string random_weight_id() {
    std::random_device device;
    std::mt19937_64 generator{(static_cast<std::uint64_t>(device()) << 32U) ^ device()};
    std::uniform_int_distribution<int> digit{0, 15};
    std::string id;
    for (std::size_t i = 0; i < kWeightIdLength; ++i) {
        id += "0123456789abcdef"[digit(generator)];
    }
    return id;
}

std::filesystem::path model_dir(const StoreRoots& roots, std::string_view format,
                                std::string_view model) {
    return roots.root_for(format) / std::string{model};
}

std::filesystem::path weights_dir(const StoreRoots& roots, std::string_view format,
                                  std::string_view model, std::string_view id) {
    return model_dir(roots, format, model) / std::string{format} / std::string{id};
}

std::filesystem::path find_weights_dir(const StoreRoots& roots, std::string_view format,
                                       std::string_view model, std::string_view id) {
    const std::filesystem::path preferred = weights_dir(roots, format, model, id);
    std::error_code code;
    if (std::filesystem::is_directory(preferred, code)) {
        return preferred;
    }
    for (const std::filesystem::path& root : {roots.models, roots.safetensors}) {
        const std::filesystem::path candidate =
            root / std::string{model} / std::string{format} / std::string{id};
        if (std::filesystem::is_directory(candidate, code)) {
            return candidate;
        }
    }
    return preferred;
}

std::filesystem::path incoming_path(const StoreRoots& roots, std::string_view format,
                                    std::string_view model) {
    const std::filesystem::path dir = model_dir(roots, format, model) / std::string{format} /
                                      (std::string{kIncomingPrefix} + random_weight_id());
    // Claimed now, so the one check that asks -- is its owner still running? --
    // never reads a live staging directory as abandoned. Best effort: without
    // a marker, an hour of no change is what marks it abandoned.
    std::error_code code;
    std::filesystem::create_directories(dir.parent_path(), code);
    std::ofstream{staging_owner_path(dir), std::ios::binary} << platform::current_process_id()
                                                             << "\n";
    return dir;
}

std::filesystem::path staging_owner_path(const std::filesystem::path& staging) {
    std::filesystem::path owner = staging;
    owner += ".owner";
    return owner;
}

std::filesystem::path make_incoming_dir(const StoreRoots& roots, std::string_view format,
                                        std::string_view model) {
    const std::filesystem::path dir = incoming_path(roots, format, model);
    std::error_code code;
    std::filesystem::create_directories(dir, code);
    return dir;
}

Commit commit_weights(const std::filesystem::path& staged, const std::filesystem::path& final_dir) {
    Commit commit;
    commit.dir = final_dir;
    std::error_code code;
    // Committed either way below: the claim on the staging name ends here.
    std::filesystem::remove(staging_owner_path(staged), code);
    if (std::filesystem::exists(final_dir, code)) {
        // Same id, same weights: what is already there is kept, and the copy
        // that was just made is not -- but for a projector the stored copy
        // lacks. The id is the model file's hash alone, so a projector made
        // after the model (a later `convert`, an Ollama layer that failed the
        // first time) has nowhere else to go.
        const std::optional<std::filesystem::path> staged_projector = projector_in(staged);
        if (staged_projector.has_value() && !projector_in(final_dir).has_value()) {
            const std::filesystem::path record = sidecar_path_for(*staged_projector);
            commit.error = move_path(*staged_projector, final_dir / staged_projector->filename());
            if (commit.error.empty() && std::filesystem::exists(record, code)) {
                commit.error = move_path(record, final_dir / record.filename());
            }
            commit.projector_added = commit.error.empty();
        }
        std::filesystem::remove_all(staged, code);
        commit.existed = true;
        return commit;
    }
    commit.error = move_path(staged, final_dir);
    return commit;
}

std::string move_path(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::error_code code;
    std::filesystem::create_directories(to.parent_path(), code);
    std::filesystem::rename(from, to, code);
    if (!code) {
        return {};
    }
    if (code != std::errc::cross_device_link) {
        return "could not move " + from.string() + " to " + to.string() + ": " + code.message();
    }
    // Another filesystem: copy, and only once the copy is whole, remove.
    std::error_code copy_code;
    std::filesystem::copy(
        from, to,
        std::filesystem::copy_options::recursive | std::filesystem::copy_options::copy_symlinks,
        copy_code);
    if (copy_code) {
        std::error_code cleanup;
        std::filesystem::remove_all(to, cleanup);
        return "could not copy " + from.string() + " to " + to.string() + ": " +
               copy_code.message();
    }
    std::filesystem::remove_all(from, code);
    return {};
}

std::filesystem::path projector_path_for(const std::filesystem::path& model_file) {
    return model_file.parent_path() / (model_file.stem().string() + "-mmproj.gguf");
}

std::string write_record(const std::filesystem::path& file, Sidecar record,
                         const HashProgress& progress) {
    std::error_code code;
    record.file = file.filename().string();
    if (record.pulled_at.empty()) {
        record.pulled_at = now_rfc3339();
    }
    bool stopped = false;
    HashProgress watched;
    if (progress) {
        watched = [&progress, &stopped](std::int64_t hashed) {
            stopped = !progress(hashed);
            return !stopped;
        };
    }
    record.file_digest = file_sha256(file, watched);
    if (stopped) {
        return std::string{kStopped};
    }
    record.file_size = static_cast<std::int64_t>(std::filesystem::file_size(file, code));
    if (code || record.file_digest.empty()) {
        return "could not read " + file.string() + " to record it";
    }
    record.verification.size_checked = true;
    record.verification.size_matched = true;
    if (!write_sidecar(file, record)) {
        return "could not write the record beside " + file.string();
    }
    return {};
}

std::string share_projector(const std::filesystem::path& projector,
                            const std::filesystem::path& dir) {
    std::error_code code;
    const std::filesystem::path link = dir / projector.filename();
    if (std::filesystem::exists(link, code)) {
        return "a file already exists at " + link.string();
    }
    std::filesystem::create_directories(dir, code);
    std::filesystem::create_hard_link(projector, link, code);
    if (code) {
        // Another filesystem, or one without hard links: a copy.
        code.clear();
        std::filesystem::copy_file(projector, link, code);
        if (code) {
            std::error_code cleanup;
            std::filesystem::remove(link, cleanup);
            return "could not copy " + projector.string() + " to " + dir.string() + ": " +
                   code.message();
        }
    }
    // The record is copied, never linked: it is small, and a record is
    // rewritten in place where the weights never are.
    const std::filesystem::path record = sidecar_path_for(projector);
    if (std::filesystem::exists(record, code)) {
        std::filesystem::copy_file(record, dir / record.filename(), code);
        if (code) {
            return "could not copy " + record.string() + " to " + dir.string() + ": " +
                   code.message();
        }
    }
    return {};
}

StoredFile commit_gguf(const StoreRoots& roots, std::string_view model,
                       const std::filesystem::path& staging, const std::filesystem::path& file,
                       Sidecar record, const HashProgress& progress) {
    StoredFile stored;
    if (std::string error = write_record(file, std::move(record), progress); !error.empty()) {
        stored.error = std::move(error);
        return stored;
    }
    const std::optional<Sidecar> written = load_sidecar(file);
    std::string id = written.has_value() ? weight_id_from_digest(written->file_digest) : "";
    if (id.empty()) {
        id = random_weight_id();
    }
    const Commit commit = commit_weights(staging, weights_dir(roots, kGgufFormat, model, id));
    if (!commit.error.empty()) {
        stored.error = commit.error;
        return stored;
    }
    stored.existed = commit.existed;
    stored.projector_added = commit.projector_added;
    stored.file = commit.dir / file.filename();
    for (const StoredGguf& existing : list_store_ggufs(roots, model)) {
        if (existing.id == id) {
            stored.file = existing.file;
            stored.projector = existing.projector;
        }
    }
    return stored;
}

std::vector<StoredGguf> list_store_ggufs(const StoreRoots& roots, std::string_view model) {
    std::vector<StoredGguf> out;
    for (const std::filesystem::path& model_path : subdirectories(roots.models)) {
        const std::string name = model_path.filename().string();
        if (!model.empty() && name != model) {
            continue;
        }
        for (const std::filesystem::path& id_dir :
             subdirectories(model_path / std::string{kGgufFormat})) {
            StoredGguf stored;
            stored.model = name;
            stored.id = id_dir.filename().string();
            stored.dir = id_dir;
            std::error_code code;
            std::vector<std::filesystem::path> files;
            for (const auto& entry : std::filesystem::directory_iterator(id_dir, code)) {
                if (entry.is_regular_file(code) && entry.path().extension() == ".gguf") {
                    files.push_back(entry.path());
                }
            }
            std::ranges::sort(files);
            for (const std::filesystem::path& file : files) {
                if (is_projector_file(file)) {
                    stored.projector = file;
                } else if (stored.file.empty()) {
                    stored.file = file;
                }
            }
            if (!stored.file.empty()) {
                out.push_back(std::move(stored));
            }
        }
    }
    return out;
}

std::vector<StoredSnapshot> list_store_snapshots(const StoreRoots& roots, std::string_view model) {
    std::vector<StoredSnapshot> out;
    std::vector<std::filesystem::path> model_paths = subdirectories(roots.safetensors);
    if (roots.models != roots.safetensors) {
        for (const std::filesystem::path& path : subdirectories(roots.models)) {
            model_paths.push_back(path);
        }
    }
    for (const std::filesystem::path& model_path : model_paths) {
        const std::string name = model_path.filename().string();
        if (!model.empty() && name != model) {
            continue;
        }
        for (const std::filesystem::path& id_dir :
             subdirectories(model_path / std::string{kSafetensorsFormat})) {
            if (!is_snapshot_dir(id_dir)) {
                continue;
            }
            StoredSnapshot stored;
            stored.model = name;
            stored.id = id_dir.filename().string();
            stored.dir = id_dir;
            if (const std::optional<Snapshot> record = load_snapshot(id_dir)) {
                stored.arrived = record->pulled_at;
            }
            std::error_code code;
            const std::filesystem::path record_path = snapshot_record_path(id_dir);
            stored.landed = std::filesystem::exists(record_path, code)
                                ? std::filesystem::last_write_time(record_path, code)
                                : std::filesystem::last_write_time(id_dir, code);
            out.push_back(std::move(stored));
        }
    }
    std::ranges::sort(out, [](const StoredSnapshot& a, const StoredSnapshot& b) {
        return a.model != b.model ? a.model < b.model : a.id < b.id;
    });
    return out;
}

std::optional<StoredSnapshot> newest_snapshot(const StoreRoots& roots, std::string_view model) {
    std::vector<StoredSnapshot> snapshots = list_store_snapshots(roots, model);
    if (snapshots.empty()) {
        return std::nullopt;
    }
    return *std::ranges::max_element(
        snapshots,
        [](const StoredSnapshot& a, const StoredSnapshot& b) { return a.landed < b.landed; });
}

std::vector<std::string> list_store_models(const StoreRoots& roots) {
    std::vector<std::string> out;
    for (const std::filesystem::path& root : {roots.models, roots.safetensors}) {
        for (const std::filesystem::path& model_path : subdirectories(root)) {
            std::error_code code;
            if (std::filesystem::is_directory(model_path / std::string{kGgufFormat}, code) ||
                std::filesystem::is_directory(model_path / std::string{kSafetensorsFormat}, code)) {
                out.push_back(model_path.filename().string());
            }
        }
    }
    std::ranges::sort(out);
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

namespace {

/// The newest change to anything under `dir`, `dir` included.
[[nodiscard]] std::filesystem::file_time_type newest_change(const std::filesystem::path& dir) {
    std::error_code code;
    std::filesystem::file_time_type newest = std::filesystem::last_write_time(dir, code);
    for (auto it = std::filesystem::recursive_directory_iterator(dir, code);
         !code && it != std::filesystem::recursive_directory_iterator(); it.increment(code)) {
        const std::filesystem::file_time_type time = it->last_write_time(code);
        if (!code && time > newest) {
            newest = time;
        }
    }
    return newest;
}

[[nodiscard]] std::uintmax_t bytes_under(const std::filesystem::path& dir) {
    std::uintmax_t total = 0;
    std::error_code code;
    for (auto it = std::filesystem::recursive_directory_iterator(dir, code);
         !code && it != std::filesystem::recursive_directory_iterator(); it.increment(code)) {
        if (it->is_regular_file(code)) {
            total += it->file_size(code);
        }
    }
    return total;
}

/// Whether nothing owns `staging` any more: its marker names a process that
/// is gone, or it has none and an hour has passed without a change -- well
/// past any live writer, which grows a partial file or a download as it goes.
[[nodiscard]] bool abandoned(const std::filesystem::path& staging) {
    std::ifstream owner{staging_owner_path(staging)};
    long pid = 0;
    if (owner >> pid) {
        return !platform::process_running(pid);
    }
    return std::filesystem::file_time_type::clock::now() - newest_change(staging) >
           std::chrono::hours{1};
}

}  // namespace

std::vector<AbandonedStaging> find_abandoned_staging(const StoreRoots& roots) {
    std::vector<AbandonedStaging> out;
    std::set<std::filesystem::path> seen;  // the two roots are often one
    for (const std::filesystem::path& root : {roots.models, roots.safetensors}) {
        for (const std::filesystem::path& model_path : subdirectories(root)) {
            for (const std::string_view format : {kGgufFormat, kSafetensorsFormat}) {
                std::error_code code;
                for (const auto& entry :
                     std::filesystem::directory_iterator(model_path / std::string{format}, code)) {
                    if (entry.is_directory(code) &&
                        entry.path().filename().string().starts_with(kIncomingPrefix) &&
                        seen.insert(entry.path()).second && abandoned(entry.path())) {
                        out.push_back({.dir = entry.path(), .bytes = bytes_under(entry.path())});
                    }
                }
            }
        }
    }
    return out;
}

StoreTarget resolve_store_target(const StoreRoots& roots, std::string_view given) {
    StoreTarget target;
    std::error_code code;
    const std::filesystem::path given_path{std::string{given}};

    // A path that exists: read it back into its parts, or hand it over as
    // outside the store.
    if (!given.empty() && std::filesystem::exists(given_path, code)) {
        const std::filesystem::path full = std::filesystem::weakly_canonical(given_path, code);
        for (const std::filesystem::path& root : {roots.models, roots.safetensors}) {
            const std::filesystem::path canonical_root =
                std::filesystem::weakly_canonical(root, code);
            if (!under(full, canonical_root)) {
                continue;
            }
            std::vector<std::string> parts;
            for (const std::filesystem::path& part : full.lexically_relative(canonical_root)) {
                parts.push_back(part.string());
            }
            target.model = parts.at(0);
            target.path = root / target.model;
            if (parts.size() >= 2 && is_format(parts.at(1))) {
                target.format = parts.at(1);
                target.path = root / target.model / target.format;
                if (parts.size() >= 3) {
                    target.id = parts.at(2);
                    target.path = root / target.model / target.format / target.id;
                }
            }
            return target;
        }
        target.outside = true;
        target.path = given_path;
        return target;
    }

    // A bare id: unique across every model and format, or named as ambiguous.
    if (is_weight_id(given)) {
        std::vector<StoreTarget> matches;
        for (const StoredGguf& gguf : list_store_ggufs(roots)) {
            if (gguf.id == given) {
                matches.push_back({gguf.model, std::string{kGgufFormat}, gguf.id, gguf.dir});
            }
        }
        for (const StoredSnapshot& snapshot : list_store_snapshots(roots)) {
            if (snapshot.id == given) {
                matches.push_back(
                    {snapshot.model, std::string{kSafetensorsFormat}, snapshot.id, snapshot.dir});
            }
        }
        if (matches.size() == 1) {
            return matches.front();
        }
        if (matches.size() > 1) {
            target.error = "the id " + std::string{given} + " is in more than one place:";
            for (const StoreTarget& match : matches) {
                target.error += "\n  " + match.path.string();
            }
            target.error += "\nname it as <model>/<format>/<id>";
            return target;
        }
    }

    // `<model>/<format>/<id>` or `<model>/<format>`, written relative to the store.
    const std::vector<std::string> segments = [&given] {
        std::vector<std::string> out;
        std::string current;
        for (const char c : given) {
            if (c == '/') {
                out.push_back(current);
                current.clear();
            } else {
                current += c;
            }
        }
        out.push_back(current);
        return out;
    }();
    if (segments.size() >= 2 && segments.size() <= 3 && is_format(segments.at(1)) &&
        segments.at(0).find("..") == std::string::npos) {
        target.model = segments.at(0);
        target.format = segments.at(1);
        target.id = segments.size() == 3 ? segments.at(2) : std::string{};
        target.path = target.id.empty()
                          ? model_dir(roots, target.format, target.model) / target.format
                          : find_weights_dir(roots, target.format, target.model, target.id);
        if (std::filesystem::is_directory(target.path, code) &&
            target.id.find("..") == std::string::npos) {
            return target;
        }
        StoreTarget missing;
        missing.error = "nothing at " + target.path.string();
        return missing;
    }

    // A model: `owner/repo` as it was pulled, or the directory name itself.
    std::string name{given};
    if (const std::optional<HfRef> ref = parse_hf_ref(given);
        ref.has_value() && ref->file.empty()) {
        name = repo_directory_name(*ref);
    }
    if (!name.empty() && name.find("..") == std::string::npos &&
        name.find_first_of("/\\") == std::string::npos) {
        for (const std::filesystem::path& root : {roots.models, roots.safetensors}) {
            if (std::filesystem::is_directory(root / name / std::string{kGgufFormat}, code) ||
                std::filesystem::is_directory(root / name / std::string{kSafetensorsFormat},
                                              code)) {
                target.model = name;
                target.path = root / name;
                return target;
            }
        }
    }
    target.error = "no model '" + std::string{given} + "' in " + roots.models.string() +
                   (roots.safetensors == roots.models ? "" : " or " + roots.safetensors.string());
    return target;
}

std::string remove_weights(const std::filesystem::path& weights_dir) {
    std::error_code code;
    std::filesystem::remove_all(weights_dir, code);
    if (code) {
        return "could not remove " + weights_dir.string() + ": " + code.message();
    }
    if (weights_dir.filename().string().starts_with(kIncomingPrefix)) {
        std::filesystem::remove(staging_owner_path(weights_dir), code);
    }
    // Tidy upward: an emptied format directory, then an emptied model one.
    const std::filesystem::path format_dir = weights_dir.parent_path();
    if (is_format(format_dir.filename().string()) && std::filesystem::is_empty(format_dir, code) &&
        !code) {
        std::filesystem::remove(format_dir, code);
        const std::filesystem::path model = format_dir.parent_path();
        if (std::filesystem::is_empty(model, code) && !code) {
            std::filesystem::remove(model, code);
        }
    }
    return {};
}

LegacyLayout find_legacy(const StoreRoots& roots) {
    LegacyLayout legacy;
    std::error_code code;
    if (std::filesystem::is_directory(roots.models, code)) {
        std::vector<std::filesystem::path> files;
        for (const auto& entry : std::filesystem::directory_iterator(roots.models, code)) {
            if (entry.is_regular_file(code) && entry.path().extension() == ".gguf") {
                files.push_back(entry.path());
            }
        }
        std::ranges::sort(files);
        for (const std::filesystem::path& file : files) {
            if (is_projector_file(file)) {
                std::string base = file.stem().string();
                base.erase(base.size() - std::string_view{"-mmproj"}.size());
                if (std::filesystem::exists(file.parent_path() / (base + ".gguf"), code)) {
                    continue;  // travels with its model
                }
            }
            LegacyGguf gguf;
            gguf.file = file;
            if (std::filesystem::exists(sidecar_path_for(file), code)) {
                gguf.sidecar = sidecar_path_for(file);
            }
            const std::filesystem::path projector = projector_path_for(file);
            if (!is_projector_file(file) && std::filesystem::exists(projector, code)) {
                gguf.projector = projector;
                if (std::filesystem::exists(sidecar_path_for(projector), code)) {
                    gguf.projector_sidecar = sidecar_path_for(projector);
                }
            }
            gguf.model = legacy_model_name(file);
            legacy.ggufs.push_back(std::move(gguf));
        }
    }
    std::vector<std::filesystem::path> roots_to_scan{roots.models};
    if (roots.safetensors != roots.models) {
        roots_to_scan.push_back(roots.safetensors);
    }
    for (const std::filesystem::path& root : roots_to_scan) {
        for (const std::filesystem::path& dir : subdirectories(root)) {
            if (is_snapshot_dir(dir)) {
                legacy.snapshots.push_back({dir, dir.filename().string()});
            }
        }
    }
    return legacy;
}

std::string legacy_refusal(const StoreRoots& roots, std::string_view given) {
    std::string name{given};
    if (const std::optional<HfRef> ref = parse_hf_ref(given);
        ref.has_value() && ref->file.empty()) {
        name = repo_directory_name(*ref);
    }
    const std::filesystem::path given_path{std::string{given}};
    const LegacyLayout legacy = find_legacy(roots);
    const auto matches = [&](const std::filesystem::path& path, std::string_view model) {
        return model == name || path.filename().string() == given ||
               path.stem().string() == given || path == given_path;
    };
    for (const LegacySnapshot& snapshot : legacy.snapshots) {
        if (matches(snapshot.dir, snapshot.model)) {
            return "'" + std::string{given} + "' is in the old flat layout (" +
                   snapshot.dir.string() +
                   ") -- run 'apogee models migrate' to move it into the new one";
        }
    }
    for (const LegacyGguf& gguf : legacy.ggufs) {
        if (matches(gguf.file, gguf.model)) {
            return "'" + std::string{given} + "' is in the old flat layout (" + gguf.file.string() +
                   ") -- run 'apogee models migrate' to move it into the new one";
        }
    }
    return {};
}

}  // namespace apogee::models
