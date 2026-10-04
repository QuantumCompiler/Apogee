#include "commands/models_pull.h"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "backends/http_client.h"
#include "commands/download_progress.h"
#include "commands/helpers.h"
#include "commands/interrupt.h"
#include "commands/model_chain.h"
#include "commands/models_migrate.h"
#include "commands/status_line.h"
#include "harness/config.h"
#include "harness/config_edit.h"
#include "harness/layout.h"
#include "harness/paths.h"
#include "models/acquire.h"
#include "models/convert.h"
#include "models/gguf_inspect.h"
#include "models/quantize.h"
#include "models/snapshot.h"
#include "models/source_hf.h"
#include "models/source_ollama.h"
#include "models/store.h"
#include "training/convert.h"
#include "training/python_env.h"

namespace apogee::commands {
namespace {

[[noreturn]] void fail(const std::string& message) {
    std::cerr << "apogee models: " << message << "\n";
    throw CLI::RuntimeError(1);
}

/// A ref shaped "owner/repo…" is Hugging Face; anything else is an Ollama ref.
///
/// Ollama refs are `name:tag` with no slash (or `namespace/name:tag`, which is
/// ambiguous with HF and resolved in Ollama's favour only when the store
/// actually has it — see `pull_one`).
[[nodiscard]] bool looks_like_hf(std::string_view ref) noexcept {
    return ref.find('/') != std::string_view::npos;
}

/// Human-readable byte count.
[[nodiscard]] std::string human_size(std::int64_t bytes) {
    if (bytes <= 0) {
        return "unknown size";
    }
    if (bytes < 1024LL * 1024) {
        return std::to_string(bytes / 1024) + " KiB";
    }
    if (bytes < 1024LL * 1024 * 1024) {
        return std::to_string(bytes / (1024LL * 1024)) + " MiB";
    }
    return std::to_string(bytes / (1024LL * 1024 * 1024)) + " GiB";
}

/// Calls `tick` every quarter second on its own thread until destroyed.
///
/// What watches a conversion: the converter writes its progress to a stderr
/// Apogee captures and does not show, so the output file's growth is the one
/// honest signal. RAII so the thread is joined on every path out, a throw
/// included -- an unjoined std::thread terminates the process.
class Ticker {
public:
    explicit Ticker(std::function<void()> tick)
        : thread_{[this, tick = std::move(tick)]() {
              std::unique_lock<std::mutex> lock{mutex_};
              while (!stopped_) {
                  tick();
                  wake_.wait_for(lock, std::chrono::milliseconds{250}, [this] { return stopped_; });
              }
          }} {}

    ~Ticker() {
        {
            const std::lock_guard<std::mutex> lock{mutex_};
            stopped_ = true;
        }
        wake_.notify_all();
        thread_.join();
    }

    Ticker(const Ticker&) = delete;
    Ticker& operator=(const Ticker&) = delete;
    Ticker(Ticker&&) = delete;
    Ticker& operator=(Ticker&&) = delete;

private:
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stopped_ = false;
    std::thread thread_;  // last: it starts running before the body of the constructor
};

/// One run of the converter into `output`, the partial file's growth shown
/// against `estimate` (unknown when zero). Ctrl-C cancels it.
models::ConvertResult run_conversion(const std::filesystem::path& snapshot,
                                     const std::filesystem::path& output,
                                     const training::Converter& converter, std::int64_t estimate) {
    const std::filesystem::path partial = models::conversion_partial_path(output);
    models::ConvertResult result;
    DownloadProgress progress{std::cout, stdout_download_options()};
    {
        const Ticker ticker{[&progress, &partial, estimate]() {
            std::error_code code;
            const std::uintmax_t size = std::filesystem::file_size(partial, code);
            progress.bytes(code ? 0 : static_cast<std::int64_t>(size), estimate);
        }};
        const InterruptScope interrupt;
        result = models::convert_snapshot(
            snapshot, output,
            [&converter](const std::filesystem::path& from, const std::filesystem::path& gguf,
                         const harness::CancellationToken& cancellation) {
                return converter(from, gguf, {}, cancellation);
            },
            InterruptScope::token());
    }
    progress.finish();
    return result;
}

/// `commit_gguf`, with its hash on a progress line and Ctrl-C able to stop it.
///
/// The id is the GGUF's SHA-256, and a converted model is tens of gigabytes:
/// the hash ran for three silent minutes after a 51 GiB conversion and read
/// as a hang (2026-09-24) -- and a Ctrl-C there killed the process outright,
/// leaving the staging directory behind at full size. A stopped hash is
/// cancelled like a stopped conversion: the staging directory goes, and the
/// exit code says so.
models::StoredFile commit_with_progress(const models::StoreRoots& roots, std::string_view model,
                                        const std::filesystem::path& staging,
                                        const std::filesystem::path& file, models::Sidecar record) {
    std::error_code code;
    const auto total = static_cast<std::int64_t>(std::filesystem::file_size(file, code));
    std::cout << "hashing it (" << format_progress_size(total)
              << ") -- its SHA-256 names its folder in the store\n";
    models::StoredFile stored;
    {
        DownloadProgress progress{std::cout, stdout_download_options()};
        const InterruptScope interrupt;
        stored = models::commit_gguf(roots, model, staging, file, std::move(record),
                                     [&progress, total](std::int64_t hashed) {
                                         progress.bytes(hashed, total);
                                         return !InterruptScope::token().stop_requested();
                                     });
        progress.finish();
    }
    if (stored.error == models::kStopped) {
        (void)models::remove_weights(staging);
        std::cerr << "apogee models: cancelled -- nothing was written\n";
        throw CLI::RuntimeError(kCancelled);
    }
    if (!stored.error.empty()) {
        (void)models::remove_weights(staging);
        fail(stored.error);
    }
    return stored;
}

/// Why a projector could not be made, in words a user can act on. The
/// converter says an architecture "is not supported" when it has no projector
/// class for it -- the model itself converted fine.
std::string projector_failure(const std::string& error) {
    if (error.find("is not supported") != std::string::npos) {
        return "the converter shipped with this build's llama.cpp cannot make a projector for "
               "this model's architecture";
    }
    return error;
}

/// What a snapshot with no chat template is said to be: a base model, whose
/// GGUF continues text rather than answering -- which reads as a broken chat
/// unless it is said. One wording, standalone and chained (M3).
[[nodiscard]] std::string base_model_note() {
    return "note: this model has no chat template -- it is a base model, which continues "
           "text rather than answering.\n"
           "      To chat, convert its instruction-tuned release (often named '-it' or "
           "'-Instruct'); a base model is what fine-tuning starts from.";
}

/// The one command that puts a stored model to use, its projector included.
void print_backend_hint(const std::filesystem::path& model_file,
                        const std::filesystem::path& projector) {
    std::cout << "\nUse it by adding a backend:\n"
              << "  apogee config add-backend <name> --type llamacpp --model-path "
              << model_file.string();
    if (!projector.empty()) {
        std::cout << " --mmproj-path " << projector.string();
    }
    std::cout << "\n";
}

/// The configured backends whose model or projector lies inside one of `dirs`.
/// Best effort: a config that does not load names none.
[[nodiscard]] std::vector<std::string> backends_inside(
    const std::string& config_flag, const std::vector<std::filesystem::path>& dirs) {
    std::vector<std::string> names;
    try {
        const std::filesystem::path path = harness::resolve_config_path(config_flag);
        std::error_code code;
        if (!std::filesystem::exists(path, code)) {
            return names;
        }
        const harness::Config config = harness::load_config(path);
        for (const auto& [name, backend] : config.backends) {
            for (const std::string& field : {backend.model_path, backend.mmproj_path}) {
                if (field.empty()) {
                    continue;
                }
                const std::filesystem::path file{harness::expand_env_and_home(field)};
                const bool inside = std::ranges::any_of(dirs, [&file](const auto& dir) {
                    const std::filesystem::path relative = file.lexically_relative(dir);
                    return !relative.empty() && *relative.begin() != "..";
                });
                if (inside) {
                    names.push_back(name);
                    break;
                }
            }
        }
    } catch (const harness::ConfigError&) {
        // `check` reports a broken config; this only warns.
    }
    return names;
}

/// Fetches a snapshot's files from where its record says it came -- the
/// repository and revision it was pulled at -- each checked by the ladder
/// against the size and sha256 the record holds. Only a Hugging Face pull can
/// be fetched again.
[[nodiscard]] models::SnapshotFetchFn snapshot_fetcher(const models::Snapshot& record) {
    if (record.source != "huggingface") {
        return [source = record.source](const models::SnapshotFile&, const std::filesystem::path&) {
            return "it came from '" + source + "', which cannot be fetched again";
        };
    }
    std::string ref = record.ref;
    if (!record.revision.empty() && record.revision != "main") {
        ref += "@" + record.revision;
    }
    const std::optional<models::HfRef> parsed = models::parse_hf_ref(ref);
    auto client =
        std::make_shared<backends::HttpClient>(std::make_unique<backends::CurlTransport>());
    return [parsed, client](const models::SnapshotFile& file,
                            const std::filesystem::path& to) -> std::string {
        if (!parsed.has_value()) {
            return "its record names no repository to fetch from";
        }
        models::HfFile wanted;
        wanted.path = file.path;
        wanted.size = file.size;
        wanted.sha256 = file.sha256;
        models::SourcePromise promise;
        const models::ByteSource source =
            models::http_source(*client, *parsed, models::HfRepoKind::Model, wanted,
                                models::hf_token({}), harness::CancellationToken{}, promise);
        const models::AcquireResult result = models::acquire_file(to, promise, source);
        return result.ok ? std::string{} : result.error;
    };
}

/// A model's short name for the files made from it: the repository part of
/// `owner--repo`, else the name as it is.
[[nodiscard]] std::string display_name(const std::string& model) {
    const std::size_t separator = model.rfind("--");
    return separator == std::string::npos ? model : model.substr(separator + 2);
}

/// What `pull --safetensors` stored, or found already stored.
struct PulledSnapshot {
    std::string model;
    std::string id;
    std::filesystem::path dir;
};

/// `models pull <owner>/<repo> --safetensors`: the whole full-weight
/// repository -- shards, configuration, tokenizer -- through the tree ladder,
/// each shard checked against the sha256 Hugging Face publishes for it, into
/// `<model>/safetensors/<id>` and committed by rename so a half snapshot
/// never appears. What the training track fine-tunes and `convert` reads.
///
/// The id is known before a byte moves -- Hugging Face publishes every shard's
/// sha256 -- so weights already here are found, not fetched again.
PulledSnapshot pull_snapshot(const std::string& ref, const models::StoreRoots& roots,
                             bool chained) {
    if (!looks_like_hf(ref)) {
        fail("--safetensors takes a Hugging Face repository (owner/repo); '" + ref +
             "' is not one");
    }
    const std::optional<models::HfRef> parsed = models::parse_hf_ref(ref);
    if (!parsed.has_value()) {
        fail("'" + ref + "' is not a Hugging Face ref (expected owner/repo[@revision])");
    }
    if (!parsed->file.empty()) {
        fail("--safetensors downloads the whole repository; drop ':" + parsed->file + "'");
    }
    backends::HttpClient client{std::make_unique<backends::CurlTransport>()};
    const std::string token = models::hf_token({});
    const harness::CancellationToken cancellation;

    const models::HfTree tree =
        models::list_repo_tree(client, *parsed, models::HfRepoKind::Model, token, cancellation);
    if (!tree.ok) {
        fail(tree.error);
    }
    std::vector<models::HfFile> wanted;
    std::vector<models::SnapshotFile> published;
    bool any_shard = false;
    std::int64_t total = 0;
    for (const models::HfFile& file : tree.files) {
        if (!models::snapshot_wanted(file.path)) {
            continue;
        }
        any_shard = any_shard || file.path.ends_with(".safetensors");
        total += file.size;
        wanted.push_back(file);
        published.push_back({file.path, file.size, file.sha256});
    }
    if (!any_shard) {
        fail("'" + parsed->repo_id() +
             "' holds no .safetensors shards -- nothing to snapshot. For a GGUF, pull it "
             "without --safetensors");
    }

    const std::string model = models::repo_directory_name(*parsed);
    const std::string known_id = models::snapshot_weight_id(published);
    if (!known_id.empty()) {
        const std::filesystem::path existing =
            models::weights_dir(roots, models::kSafetensorsFormat, model, known_id);
        std::error_code code;
        if (std::filesystem::is_directory(existing, code)) {
            std::cout << "already here -- these exact weights are at\n  " << existing.string()
                      << "\n";
            return PulledSnapshot{.model = model, .id = known_id, .dir = existing};
        }
    }
    // Without a published digest on every shard the id waits for the bytes.
    const std::filesystem::path destination =
        known_id.empty() ? models::incoming_path(roots, models::kSafetensorsFormat, model)
                         : models::weights_dir(roots, models::kSafetensorsFormat, model, known_id);
    std::cout << "downloading " << wanted.size() << " file(s), " << human_size(total) << ", into "
              << (known_id.empty() ? destination.parent_path() : destination).string() << "\n";

    std::vector<models::TreeItem> items;
    std::vector<models::SourcePromise> promises;
    promises.reserve(wanted.size());
    for (const models::HfFile& file : wanted) {
        promises.emplace_back();
        models::TreeItem item;
        item.relative = file.path;
        item.source = models::http_source(client, *parsed, models::HfRepoKind::Model, file, token,
                                          cancellation, promises.back());
        item.promise = promises.back();
        items.push_back(std::move(item));
    }

    DownloadProgress progress{std::cout, stdout_download_options()};
    const models::AcquireTreeResult result = models::acquire_tree(
        destination, items,
        [&progress](std::size_t index, std::size_t count, std::string_view relative,
                    std::int64_t written,
                    std::int64_t size) { progress.file(index, count, relative, written, size); });
    progress.finish();
    if (!result.ok) {
        fail(result.error);
    }

    models::Snapshot record;
    record.ref = parsed->repo_id();
    record.revision = parsed->revision.empty() ? "main" : parsed->revision;
    record.source = "huggingface";
    record.pulled_at = result.sidecars.empty() ? std::string{} : result.sidecars.front().pulled_at;
    for (std::size_t i = 0; i < wanted.size() && i < result.sidecars.size(); ++i) {
        record.files.push_back(
            {wanted[i].path, result.sidecars[i].file_size, result.sidecars[i].file_digest});
    }

    std::filesystem::path final_dir = destination;
    if (known_id.empty()) {
        std::string id = models::snapshot_weight_id(record.files);
        if (id.empty()) {
            id = models::random_weight_id();
        }
        const models::Commit commit = models::commit_weights(
            destination, models::weights_dir(roots, models::kSafetensorsFormat, model, id));
        if (!commit.error.empty()) {
            fail(commit.error);
        }
        if (commit.existed) {
            std::cout << "\nalready here -- these exact weights are at\n  " << commit.dir.string()
                      << "\n";
            return PulledSnapshot{
                .model = model, .id = commit.dir.filename().string(), .dir = commit.dir};
        }
        final_dir = commit.dir;
    }
    if (!models::write_snapshot(final_dir, record)) {
        std::cout << "note: the snapshot landed but its record could not be written\n";
    }

    std::size_t digests = 0;
    for (const models::Sidecar& sidecar : result.sidecars) {
        if (sidecar.verification.digest_checked) {
            ++digests;
        }
    }
    std::cout << "\n"
              << final_dir.string() << "\n"
              << "verified: " << result.files << " file(s), " << human_size(result.bytes) << ", "
              << digests << " checked against a published sha256\n";
    if (!chained) {
        std::cout
            << "\nA full-weight snapshot: trainable with 'apogee train', not runnable -- make "
               "a GGUF of it with\n  apogee models convert "
            << parsed->repo_id()
            << "\nNext time one command pulls, converts and registers it:\n  apogee models "
               "pull "
            << parsed->repo_id() << " --safetensors --register-with Q4_K_M\n";
    }
    return PulledSnapshot{.model = model, .id = final_dir.filename().string(), .dir = final_dir};
}

}  // namespace

DeletePlan plan_delete(const models::StoreRoots& roots, std::string_view name) {
    DeletePlan plan;
    plan.target = models::resolve_store_target(roots, name);
    if (!plan.target.error.empty()) {
        const std::string legacy = models::legacy_refusal(roots, name);
        plan.error = legacy.empty() ? plan.target.error : legacy;
        return plan;
    }
    // "Delete a model by name" must never become "delete a file by path".
    if (plan.target.outside) {
        plan.error =
            "a model is named, not pathed: '" + std::string{name} + "' is outside the model store";
        return plan;
    }

    const models::StoreTarget& target = plan.target;
    for (const models::StoredGguf& stored : models::list_store_ggufs(roots, target.model)) {
        if ((target.format.empty() || target.format == models::kGgufFormat) &&
            (target.id.empty() || target.id == stored.id)) {
            plan.removes.push_back(stored.dir);
            if (const std::optional<models::Sidecar> record = models::load_sidecar(stored.file);
                record.has_value() && record->source == "ollama") {
                plan.ollama_sourced = true;
                plan.ollama_ref = record->ref;
            }
        }
    }
    for (const models::StoredSnapshot& stored : models::list_store_snapshots(roots, target.model)) {
        if ((target.format.empty() || target.format == models::kSafetensorsFormat) &&
            (target.id.empty() || target.id == stored.id)) {
            plan.removes.push_back(stored.dir);
        }
    }
    if (plan.removes.empty()) {
        plan.error = "nothing stored under '" + std::string{name} + "'";
        return plan;
    }
    plan.ok = true;
    return plan;
}

DeletePlan plan_delete(const models::StoreRoots& roots, const harness::Config& config,
                       std::string_view name) {
    const auto backend = config.backends.find(std::string{name});
    std::optional<models::StoredGguf> pointed_at;
    if (backend != config.backends.end() && !backend->second.model_path.empty()) {
        pointed_at =
            models::stored_gguf_at(models::list_store_ggufs(roots),
                                   harness::expand_env_and_home(backend->second.model_path));
    }
    const auto handle_of = [](const models::StoredGguf& stored) {
        return models::weights_handle(stored.model, models::kGgufFormat, stored.id);
    };

    DeletePlan plan = plan_delete(roots, name);
    if (plan.ok) {
        // The store's meaning stands; a backend of the same name is said.
        if (pointed_at.has_value() &&
            std::ranges::find(plan.removes, pointed_at->dir) == plan.removes.end()) {
            plan.also_backend = std::string{name};
            plan.also_backend_weights = handle_of(*pointed_at);
        }
        return plan;
    }
    if (backend == config.backends.end()) {
        return plan;
    }
    if (pointed_at.has_value()) {
        DeletePlan by_backend = plan_delete(roots, handle_of(*pointed_at));
        by_backend.backend = std::string{name};
        return by_backend;
    }
    plan.error = backend->second.model_path.empty()
                     ? "backend '" + std::string{name} + "' (" +
                           std::string{harness::to_string(backend->second.type)} +
                           ") has no model file to delete -- remove the backend itself with "
                           "'apogee config delete-backend " +
                           std::string{name} + "'"
                     : "backend '" + std::string{name} + "' points at " +
                           harness::expand_env_and_home(backend->second.model_path) +
                           ", outside the model store -- Apogee deletes only what is in it";
    return plan;
}

std::string render_repair(const models::StoreRoots& roots, std::string_view name) {
    const models::StoreTarget target = models::resolve_store_target(roots, name);
    if (!target.error.empty() || target.outside || target.format == models::kSafetensorsFormat) {
        return {};
    }
    std::ostringstream out;
    for (const models::StoredGguf& stored : models::list_store_ggufs(roots, target.model)) {
        if (!target.id.empty() && target.id != stored.id) {
            continue;
        }
        if (out.tellp() > 0) {
            out << "\n";
        }
        out << "model:   " << target.model << "/gguf/" << stored.id << "/"
            << stored.file.filename().string() << "\n";

        const std::optional<models::Sidecar> sidecar = models::load_sidecar(stored.file);
        if (!sidecar.has_value()) {
            // Placed by hand, with no record: not something integrity can be
            // rechecked against, which is fine.
            const models::GgufInfo info = models::inspect_gguf(stored.file);
            out << "record:  none (not acquired through Apogee)\n";
            out << "header:  " << (info.parsed ? "ok" : "FAILED -- " + info.parse_error) << "\n";
            if (!info.parsed) {
                out << "\nThis file is not a readable GGUF. Replace it, or delete it with\n"
                    << "  apogee models delete " << target.model << "/gguf/" << stored.id << "\n";
            }
            continue;
        }
        out << "source:  " << sidecar->source << "  (" << sidecar->ref << ")\n";
        out << "pulled:  " << sidecar->pulled_at << "\n";
        out << "at pull: " << sidecar->verification.summary() << "\n";
        const models::Verification now = models::reverify(stored.file, *sidecar);
        out << "now:     " << now.summary() << "\n";
        if (now.sound()) {
            out << "\nNothing to repair.\n";
            continue;
        }
        out << "\nThis model no longer matches its record. Re-acquire it:\n";
        out << "  apogee models delete " << target.model << "/gguf/" << stored.id << "\n";
        if (sidecar->source == "huggingface" || sidecar->source == "ollama") {
            out << "  apogee models pull " << sidecar->ref << "\n";
        }
    }
    return out.str();
}

std::filesystem::path snapshot_root(const std::filesystem::path& models_dir,
                                    const std::string& config_flag) {
    try {
        const std::filesystem::path path = harness::resolve_config_path(config_flag);
        std::error_code code;
        if (!std::filesystem::exists(path, code)) {
            return models_dir;
        }
        const harness::Config config = harness::load_config(path);
        if (config.paths.hf_dir.empty()) {
            return models_dir;
        }
        return std::filesystem::path{harness::expand_env_and_home(config.paths.hf_dir)};
    } catch (const harness::ConfigError&) {
        // A config that does not parse is `check`'s problem to report; a
        // pull still has somewhere sensible to land.
        return models_dir;
    }
}

models::StoreRoots store_roots(const std::filesystem::path& models_dir,
                               const std::string& config_flag) {
    return models::StoreRoots{models_dir, snapshot_root(models_dir, config_flag)};
}

SnapshotChoice choose_snapshot(const models::StoreRoots& roots, std::string_view given,
                               std::string_view from_id) {
    SnapshotChoice choice;
    const models::StoreTarget target = models::resolve_store_target(roots, given);
    if (!target.error.empty()) {
        const std::string legacy = models::legacy_refusal(roots, given);
        choice.error = legacy.empty() ? target.error : legacy;
        return choice;
    }
    if (target.outside) {
        if (!models::is_snapshot_dir(target.path)) {
            choice.error = target.path.string() +
                           " is not a SafeTensors snapshot (a directory holding config.json and "
                           "at least one *.safetensors file)";
            return choice;
        }
        choice.path = target.path;
        choice.model = models::safe_model_name(target.path.filename().string());
        return choice;
    }
    choice.model = target.model;
    if (target.format == models::kGgufFormat) {
        choice.error = "'" + std::string{given} + "' is a GGUF; this reads SafeTensors weights";
        return choice;
    }
    const std::string id = !target.id.empty() ? target.id : std::string{from_id};
    if (!id.empty()) {
        const std::filesystem::path dir =
            models::find_weights_dir(roots, models::kSafetensorsFormat, target.model, id);
        if (!models::is_snapshot_dir(dir)) {
            choice.error = "no SafeTensors set " + id + " for " + target.model;
            const std::vector<models::StoredSnapshot> available =
                models::list_store_snapshots(roots, target.model);
            for (std::size_t i = 0; i < available.size(); ++i) {
                choice.error += (i == 0 ? " -- it has: " : ", ") + available.at(i).id;
            }
            return choice;
        }
        choice.path = dir;
        choice.id = id;
        return choice;
    }
    const std::optional<models::StoredSnapshot> newest =
        models::newest_snapshot(roots, target.model);
    if (!newest.has_value()) {
        choice.error = "no SafeTensors weights for " + target.model +
                       " -- pull them with 'apogee models pull <owner>/<repo> --safetensors'";
        return choice;
    }
    choice.path = newest->dir;
    choice.id = newest->id;
    return choice;
}

GgufChoice choose_gguf(const models::StoreRoots& roots, std::string_view given,
                       std::string_view from_id) {
    GgufChoice choice;
    const models::StoreTarget target = models::resolve_store_target(roots, given);
    if (!target.error.empty()) {
        const std::string legacy = models::legacy_refusal(roots, given);
        choice.error = legacy.empty() ? target.error : legacy;
        return choice;
    }
    if (target.outside) {
        std::error_code code;
        if (target.path.extension() != ".gguf" ||
            !std::filesystem::is_regular_file(target.path, code)) {
            choice.error = target.path.string() + " is not a .gguf file";
            return choice;
        }
        choice.file = target.path;
        choice.model = models::safe_model_name(target.path.stem().string());
        if (const std::filesystem::path projector = models::projector_path_for(target.path);
            std::filesystem::is_regular_file(projector, code)) {
            choice.projector = projector;
        }
        return choice;
    }
    choice.model = target.model;
    if (target.format == models::kSafetensorsFormat) {
        choice.error = "'" + std::string{given} +
                       "' is SafeTensors weights; make a GGUF of them first with 'apogee models "
                       "convert " +
                       target.model + "'";
        return choice;
    }
    const std::string id = !target.id.empty() ? target.id : std::string{from_id};
    const std::vector<models::StoredGguf> stored = models::list_store_ggufs(roots, target.model);
    if (!id.empty()) {
        for (const models::StoredGguf& gguf : stored) {
            if (gguf.id == id) {
                choice.file = gguf.file;
                choice.projector = gguf.projector;
                choice.id = gguf.id;
                return choice;
            }
        }
        choice.error = "no GGUF " + id + " for " + target.model;
        return choice;
    }
    // Newest unquantized: quantizing what is already quantized loses quality
    // twice, so it is never the default.
    const models::StoredGguf* best = nullptr;
    std::filesystem::file_time_type best_time{};
    for (const models::StoredGguf& gguf : stored) {
        if (models::inspect_gguf(gguf.file).is_quantized()) {
            continue;
        }
        std::error_code code;
        const auto time = std::filesystem::last_write_time(gguf.file, code);
        if (best == nullptr || time > best_time) {
            best = &gguf;
            best_time = time;
        }
    }
    if (best == nullptr) {
        choice.error = "no unquantized GGUF of " + target.model + " to quantize from";
        if (!stored.empty()) {
            choice.error += " -- name one with --from <id>; it has:";
            for (const models::StoredGguf& gguf : stored) {
                choice.error += " " + gguf.id;
            }
        } else {
            choice.error += " -- make one with 'apogee models convert " + target.model + "'";
        }
        return choice;
    }
    choice.file = best->file;
    choice.projector = best->projector;
    choice.id = best->id;
    return choice;
}

std::optional<models::StoredGguf> find_conversion(const models::StoreRoots& roots,
                                                  std::string_view model, std::string_view ref,
                                                  std::string_view out_type) {
    const std::string note = "--outtype " + std::string{out_type};
    for (const models::StoredGguf& stored : models::list_store_ggufs(roots, model)) {
        const std::optional<models::Sidecar> record = models::load_sidecar(stored.file);
        if (record.has_value() && record->source == "convert" && record->ref == ref &&
            record->transform_note == note) {
            return stored;
        }
    }
    return std::nullopt;
}

/// Finds what an older pull damaged in the snapshot at `dir` and fixes it,
/// saying what it did. False when it could not.
bool repair_snapshot_in_place(const std::filesystem::path& dir) {
    const models::SnapshotDamage damage = models::find_snapshot_damage(dir);
    if (!damage.error.empty()) {
        std::cout << "record:   none -- " << damage.error << "\n";
        return true;
    }
    if (damage.empty()) {
        std::cout << "Nothing to repair.\n";
        return true;
    }
    std::cout << "damaged:  " << damage.refetch.size() << " file(s) to fetch again, "
              << damage.stray_records.size() << " stray download record(s) to remove\n";
    const std::optional<models::Snapshot> record = models::load_snapshot(dir);
    const models::SnapshotRepair repaired =
        models::repair_snapshot(dir, damage, snapshot_fetcher(*record));
    for (const std::string& fetched : repaired.fetched) {
        std::cout << "  fetched  " << fetched << "\n";
    }
    for (const std::string& removed : repaired.removed) {
        std::cout << "  removed  " << removed << "\n";
    }
    if (!repaired.error.empty()) {
        std::cerr << "apogee models: " << repaired.error << "\n";
        return false;
    }
    std::cout << "repaired.\n";
    return true;
}

namespace {

/// What `convert_model` made, or found made.
struct ConvertedModel {
    std::string model;
    std::string id;
    std::filesystem::path file;
    std::filesystem::path projector;
    /// No chat template: a base model, registered all the same.
    bool base_model = false;
    /// Each as the standalone verb prints it, prefix and all.
    std::vector<std::string> warnings;
};

/// What `quantize_model` made, or found made.
struct QuantizedModel {
    std::string model;
    std::string id;
    std::filesystem::path file;
    std::filesystem::path projector;
    std::vector<std::string> warnings;
};

/// `models convert`: a GGUF -- and its projector, when the snapshot can see or
/// hear -- from a SafeTensors set, into the store. Standalone it prints what
/// it made, its warnings and what to do next; `chained` (M3), the warnings
/// come back in the result for the chain's summary and nothing is suggested.
ConvertedModel convert_model(const models::StoreRoots& roots, std::string_view given,
                             std::string_view from, const std::string& type, bool chained) {
    const SnapshotChoice source = choose_snapshot(roots, given, from);
    if (!source.error.empty()) {
        fail(source.error);
    }
    // Every refusal BEFORE anything is announced or any Python is looked
    // for: the cheap local checks first, then the environment.
    if (const std::string damaged = models::damaged_snapshot_error(source.path); !damaged.empty()) {
        fail(damaged + " -- or repair it in place with 'apogee models repair " + source.model +
             "'");
    }
    const training::PythonEnv env{harness::training_venv_dir()};
    const std::filesystem::path script = training::converter_script();
    if (const std::string why = training::converter_unavailable(env, script); !why.empty()) {
        fail(why);
    }

    ConvertedModel out;
    out.model = source.model;
    out.base_model = !models::snapshot_has_chat_template(source.path);
    const auto finish = [&out, chained]() {
        if (!chained) {
            for (const std::string& warning : out.warnings) {
                std::cout << warning << "\n";
            }
        }
    };

    const models::Encoders encoders = models::snapshot_encoders(source.path);
    models::Sidecar record;
    record.ref =
        source.id.empty() ? source.path.string() : source.model + "/safetensors/" + source.id;
    record.source = "convert";
    record.transform = "convert";
    record.transform_note = "--outtype " + type;
    record.verification.header_checked = true;
    record.verification.header_parsed = true;
    models::Sidecar projector_record = record;
    projector_record.transform_note = "--mmproj --outtype " + type;

    // Recognised before it runs: the same set at the same precision is the
    // same bytes, and only a projector it still lacks is worth making.
    const std::optional<models::StoredGguf> done =
        find_conversion(roots, source.model, record.ref, type);
    if (done.has_value() && (!encoders.any() || !done->projector.empty())) {
        std::cout << "already converted:\n  " << done->file.string() << "\n";
        if (!done->projector.empty()) {
            std::cout << "  " << done->projector.string() << "\n";
        }
        if (!chained) {
            std::cout << "(to convert it again, delete it first: apogee models delete "
                      << done->model << "/gguf/" << done->id << ")\n";
        }
        if (out.base_model) {
            out.warnings.push_back(base_model_note());
        }
        out.file = done->file;
        out.projector = done->projector;
        out.id = done->id;
        finish();
        if (!chained) {
            print_backend_hint(done->file, done->projector);
        }
        return out;
    }
    // The encoder's tensors go to the projector, the rest to the model.
    std::int64_t model_estimate = 0;
    std::int64_t projector_estimate = 0;
    if (const std::optional<std::int64_t> all = models::snapshot_elements(source.path)) {
        const std::int64_t encoder =
            models::snapshot_elements(source.path, models::is_encoder_tensor).value_or(0);
        model_estimate = models::estimated_gguf_bytes(*all - encoder, type);
        projector_estimate = models::estimated_gguf_bytes(encoder, type);
    }
    std::string precision = type;
    for (char& c : precision) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    const std::filesystem::path staging =
        models::make_incoming_dir(roots, models::kGgufFormat, source.model);
    const auto cancelled = [&staging]() {
        (void)models::remove_weights(staging);
        std::cerr << "apogee models: cancelled -- nothing was written\n";
        throw CLI::RuntimeError(kCancelled);
    };

    // ---- the model ------------------------------------------------------
    std::filesystem::path model_output;
    models::GgufInfo model_info;
    if (done.has_value()) {
        std::cout << "already converted:\n  " << done->file.string() << "\n";
    } else {
        model_output = staging / (display_name(source.model) +
                                  (type == "auto" ? std::string{} : "-" + precision) + ".gguf");
        std::cout << "converting " << source.path.string() << " to a GGUF ("
                  << (type == "auto" ? "precision chosen by the converter" : precision)
                  << (model_estimate > 0 ? ", about " + format_progress_size(model_estimate)
                                         : std::string{})
                  << ")\n";
        const models::ConvertResult result = run_conversion(
            source.path, model_output, training::script_converter(env.interpreter(), script, type),
            model_estimate);
        if (result.cancelled) {
            cancelled();
        }
        if (!result.ok) {
            (void)models::remove_weights(staging);
            fail(result.error);
        }
        model_info = result.info;
    }

    // ---- its projector --------------------------------------------------
    // A separate GGUF beside the model (`mmproj_path`): without it a model
    // that can see or hear is text-only, and nothing would say so.
    const std::filesystem::path projector_output =
        staging /
        models::projector_path_for(done.has_value() ? done->file : model_output).filename();
    std::string projector_problem;
    models::GgufInfo projector_info;
    if (encoders.any()) {
        std::cout << (done.has_value() ? "making its projector" : "and its projector")
                  << ", so it can read " << encoders.reads()
                  << (projector_estimate > 0
                          ? " (about " + format_progress_size(projector_estimate) + ")"
                          : std::string{})
                  << "\n";
        const models::ConvertResult made =
            run_conversion(source.path, projector_output,
                           training::script_converter(env.interpreter(), script, type,
                                                      training::ConverterOutput::Projector),
                           projector_estimate);
        if (made.cancelled) {
            cancelled();
        }
        if (!made.ok) {
            projector_problem = projector_failure(made.error);
        } else if (!made.info.is_projector()) {
            projector_problem = "the converter's output holds a text model, not a projector";
        } else {
            projector_problem = models::write_record(projector_output, projector_record);
            projector_info = made.info;
        }
        if (!projector_problem.empty()) {
            std::error_code code;
            std::filesystem::remove(projector_output, code);
            if (done.has_value()) {
                (void)models::remove_weights(staging);
                fail("its projector could not be made -- " + projector_problem);
            }
        }
    }

    // ---- into the store -------------------------------------------------
    // Its id is the model's own hash, so the directory can only be named
    // now; a projector for a model already stored joins it there.
    std::filesystem::path model_file;
    std::filesystem::path projector;
    bool existed = false;
    if (done.has_value()) {
        const models::Commit commit = models::commit_weights(staging, done->dir);
        if (!commit.error.empty()) {
            (void)models::remove_weights(staging);
            fail(commit.error);
        }
        model_file = done->file;
        projector = done->dir / projector_output.filename();
    } else {
        const models::StoredFile stored =
            commit_with_progress(roots, source.model, staging, model_output, record);
        model_file = stored.file;
        projector = stored.projector;
        existed = stored.existed;
    }

    std::cout << "\n" << (existed ? "already here -- these exact weights are at\n" : "");
    if (!done.has_value()) {
        std::cout << model_file.string() << "\n"
                  << "verified: GGUF header parsed -- " << model_info.architecture << ", "
                  << model_info.tensors << " tensors, " << human_size(model_info.file_size) << "\n";
    }
    if (!projector.empty()) {
        std::cout << projector.string() << "\n";
        if (projector_info.parsed) {
            std::cout << "verified: projector header parsed -- " << projector_info.tensors
                      << " tensors, " << human_size(projector_info.file_size) << "\n";
        }
    }
    if (!projector_problem.empty()) {
        out.warnings.push_back("warning: its projector could not be made -- " + projector_problem +
                               "\n         the model works for text; it cannot read " +
                               encoders.reads() + " without one");
    }
    if (out.base_model) {
        out.warnings.push_back(base_model_note());
    }
    if (!done.has_value() && models::is_known_unrunnable(model_info.architecture)) {
        out.warnings.push_back("warning: architecture '" + model_info.architecture +
                               "' is not known to run in Apogee's llama.cpp.");
    }
    out.file = model_file;
    out.projector = projector;
    out.id = done.has_value() ? done->id : model_file.parent_path().filename().string();
    finish();
    if (!chained) {
        if (type != "q8_0") {
            std::cout << "\nMake it smaller:\n  apogee models quantize " << source.model
                      << " --type Q4_K_M\n";
        }
        print_backend_hint(model_file, projector);
    }
    return out;
}

/// The quantization of `ref` at `level` already in the store, found by its
/// record -- the source it was made from and the level -- so a second run,
/// a resumed chain's above all, does not spend minutes making it again.
[[nodiscard]] std::optional<models::StoredGguf> find_quantization(const models::StoreRoots& roots,
                                                                  std::string_view model,
                                                                  std::string_view ref,
                                                                  std::string_view level) {
    for (const models::StoredGguf& stored : models::list_store_ggufs(roots, model)) {
        const std::optional<models::Sidecar> record = models::load_sidecar(stored.file);
        if (record.has_value() && record->source == "quantize" && record->ref == ref &&
            models::canonical_quant_type(record->transform_note) ==
                models::canonical_quant_type(level)) {
            return stored;
        }
    }
    return std::nullopt;
}

/// `models quantize`: a smaller copy of a GGUF, its projector carried along,
/// into the store. llama.cpp's own log stays off the terminal; the busy line
/// counts its tensors instead (M3). Standalone it prints what to do next;
/// `chained`, warnings come back in the result and nothing is suggested.
QuantizedModel quantize_model(const models::StoreRoots& roots, std::string_view given,
                              std::string_view from, const std::string& type,
                              const Quantizer& quantizer, bool chained) {
    const GgufChoice input = choose_gguf(roots, given, from);
    if (!input.error.empty()) {
        fail(input.error);
    }
    // The table's spelling, so `q4_k_m` and `Q4_K_M` are one level, in the
    // file's name and in its record alike.
    const std::string level = models::canonical_quant_type(type).value_or(type);

    QuantizedModel out;
    out.model = input.model;
    const std::string ref =
        input.id.empty() ? input.file.string() : input.model + "/gguf/" + input.id;
    if (const std::optional<models::StoredGguf> made =
            find_quantization(roots, input.model, ref, level);
        made.has_value()) {
        std::cout << "already quantized:\n  " << made->file.string() << "\n";
        if (!made->projector.empty()) {
            std::cout << "  " << made->projector.string() << "\n";
        }
        out.file = made->file;
        out.projector = made->projector;
        out.id = made->id;
        if (!chained) {
            print_backend_hint(made->file, made->projector);
        }
        return out;
    }

    const std::filesystem::path staging =
        models::make_incoming_dir(roots, models::kGgufFormat, input.model);
    const std::filesystem::path output =
        staging / (display_name(input.model) + "-" + level + ".gguf");
    models::QuantizeResult result;
    {
        BusyLine busy{std::cerr, "quantizing to " + level, busy_options(false)};
        result = quantizer(input.file, output, level,
                           [&busy, &level](std::size_t done, std::size_t total) {
                               busy.report("quantizing to " + level, done, total);
                           });
    }
    if (!result.ok) {
        // Reported BEFORE any "this will take a while" note: announcing
        // work and then refusing to do it reads as a crash rather than as
        // a refusal.
        (void)models::remove_weights(staging);
        fail(result.error);
    }

    // The projector is the same for every quantization of a model, so it
    // comes along -- a hard link, no second copy -- and the smaller model
    // still reads images. (Declared on 2026-09-23 and never called: the
    // first Q4_K_M made with it stored no projector.) Committed with the
    // model; into an identical model's directory that lacks one, adopted.
    std::string projector_problem;
    if (!input.projector.empty()) {
        projector_problem = models::share_projector(input.projector, staging);
    }

    models::Sidecar record;
    record.ref = ref;
    record.source = "quantize";
    record.transform = "quantize";
    record.transform_note = level;
    record.verification.header_checked = true;
    record.verification.header_parsed = models::inspect_gguf(output).parsed;
    const models::StoredFile stored =
        commit_with_progress(roots, input.model, staging, output, record);
    std::cout << "\n"
              << (stored.existed ? "already here -- these exact weights are at\n" : "")
              << stored.file.string() << "\n"
              << human_size(result.input_bytes) << " -> " << human_size(result.output_bytes)
              << "\n";
    if (!stored.projector.empty()) {
        std::cout << stored.projector.string()
                  << (stored.projector_added ? "  (its projector, added now)\n" : "\n");
    }
    if (!projector_problem.empty()) {
        out.warnings.push_back("warning: its projector did not come along -- " + projector_problem +
                               "\n         point mmproj_path at " + input.projector.string());
    }
    out.file = stored.file;
    out.projector = stored.projector;
    out.id = stored.file.parent_path().filename().string();
    if (!chained) {
        for (const std::string& warning : out.warnings) {
            std::cout << warning << "\n";
        }
        print_backend_hint(stored.file, stored.projector);
    }
    return out;
}

/// A path a config field names, comparable with one the store gives; empty
/// for an empty field.
[[nodiscard]] std::filesystem::path configured_path(const std::string& field) {
    return field.empty()
               ? std::filesystem::path{}
               : std::filesystem::path{harness::expand_env_and_home(field)}.lexically_normal();
}

/// The backend a chain registers at `level`: `<base>-F16`, `<base>-Q4KM` --
/// the level without its underscores, as the user names backends by hand
/// (2026-10-03). The file keeps the table's spelling.
[[nodiscard]] std::string backend_name(const std::string& base, std::string_view level) {
    std::string name = base + "-";
    for (const char c : level) {
        if (c != '_') {
            name += c;
        }
    }
    return name;
}

/// Refuses, before the first stage, a chain whose registrations would refuse
/// at its last: no config to register into, or a name already taken by a
/// backend that is not this model's. One a previous run of this chain made
/// -- pointing inside this model's directory -- is not in the way.
void check_registrations(const models::StoreRoots& roots, const std::filesystem::path& config_path,
                         const std::string& model, const std::vector<std::string>& names) {
    std::error_code code;
    if (!std::filesystem::exists(config_path, code)) {
        fail("there is no config to register into at " + config_path.string() +
             " -- run 'apogee config init' first");
    }
    harness::Config config;
    try {
        config = harness::load_config(config_path);
    } catch (const std::exception& e) {
        fail(e.what());
    }
    std::string text;
    {
        std::ifstream in{config_path, std::ios::binary};
        text.assign(std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{});
    }
    std::vector<std::string> existing;
    existing.reserve(config.backends.size());
    for (const auto& [name, backend] : config.backends) {
        existing.push_back(name);
    }
    const std::filesystem::path home =
        models::model_dir(roots, models::kGgufFormat, model).lexically_normal();
    for (const std::string& name : names) {
        if (const std::optional<std::string> clash = harness::fold_collision(existing, name);
            clash.has_value()) {
            fail("backend '" + name + "' would collide with '" + *clash +
                 "': names that differ only in case are one backend");
        }
        const auto found = config.backends.find(name);
        if (found == config.backends.end()) {
            // A name the config cannot hold is refused now by the one edit
            // that will add it, run on the text and kept nowhere.
            try {
                harness::BackendConfig stub;
                stub.type = harness::BackendType::LlamaCpp;
                stub.model_path = name;
                text = harness::append_backend(text, name, stub, false);
            } catch (const std::exception& e) {
                fail(e.what());
            }
            continue;
        }
        const std::filesystem::path path = configured_path(found->second.model_path);
        const std::filesystem::path relative = path.lexically_relative(home);
        if (path.empty() || relative.empty() || *relative.begin() == "..") {
            fail("backend '" + name + "' already exists" +
                 (path.empty() ? std::string{} : " and points at " + path.string()) +
                 " -- remove it with 'apogee config delete-backend " + name +
                 "' first, or register this one by hand under another name");
        }
    }
}

/// One backend the chain registers.
struct Registration {
    std::string name;
    std::filesystem::path file;
    std::filesystem::path projector;
};

/// Adds each as a `llamacpp` backend through the one config editor -- the
/// very edit `config add-backend <name> --type llamacpp --model-path <file>
/// [--mmproj-path <projector>]` makes, byte for byte. An entry already
/// naming the same files is left as it is: a resumed chain registers what is
/// missing and nothing twice.
void register_backends(const std::filesystem::path& config_path,
                       const std::vector<Registration>& wanted, bool base_model, ChainLog& log) {
    for (const Registration& entry : wanted) {
        harness::Config config;
        try {
            config = harness::load_config(config_path);
        } catch (const std::exception& e) {
            fail(e.what());
        }
        if (const auto found = config.backends.find(entry.name); found != config.backends.end()) {
            const bool same =
                found->second.type == harness::BackendType::LlamaCpp &&
                configured_path(found->second.model_path) == entry.file.lexically_normal() &&
                configured_path(found->second.mmproj_path) == entry.projector.lexically_normal();
            if (!same) {
                fail("backend '" + entry.name +
                     "' already exists and points elsewhere -- remove it "
                     "with 'apogee config delete-backend " +
                     entry.name + "', then resume");
            }
            std::cout << "already registered: " << entry.name << "\n";
        } else {
            harness::BackendConfig backend;
            backend.type = harness::BackendType::LlamaCpp;
            backend.model_path = entry.file.string();
            backend.mmproj_path = entry.projector.string();
            try {
                harness::edit_config_file(
                    config_path, [&entry, &backend](std::string_view content) {
                        return harness::append_backend(content, entry.name, backend, false);
                    });
            } catch (const std::exception& e) {
                fail(e.what());
            }
            std::cout << "registered " << entry.name << "\n";
        }
        log.ready.push_back(entry.name +
                            (base_model ? "  (a base model: it continues text rather than "
                                          "answering -- to chat, convert its instruction-tuned "
                                          "release)"
                                        : std::string{}));
    }
    if (!wanted.empty()) {
        log.next = "chat with one:  apogee chat -m " + wanted.back().name;
    }
}

}  // namespace

std::vector<std::string> register_levels(const std::vector<std::string>& given) {
    std::vector<std::string> levels;
    for (const std::string& name : given) {
        const std::optional<std::string> level = models::canonical_quant_type(name);
        if (!level.has_value()) {
            std::string accepted;
            for (const std::string& known : models::quant_type_names()) {
                accepted += (accepted.empty() ? "" : ", ") + known;
            }
            fail("unknown quantization level '" + name +
                 "' for --register-with. Accepted: " + accepted);
        }
        // The F16 is always made and registered; naming it again is a no-op.
        if (*level != "F16" && std::ranges::find(levels, *level) == levels.end()) {
            levels.push_back(*level);
        }
    }
    return levels;
}

namespace {

/// The levels `--register-with` names, refused up front when this build has
/// no llama.cpp to make them with -- before a pull or a conversion that would
/// be wasted on a chain that could not finish.
[[nodiscard]] std::vector<std::string> buildable_levels(const std::vector<std::string>& given) {
    std::vector<std::string> levels = register_levels(given);
    if (!levels.empty() && !models::quantize_supported()) {
        fail(
            "this build cannot quantize: llama.cpp was not compiled in. Rebuild with "
            "-DAPOGEE_ENABLE_LLAMA=ON, or drop --register-with to register the F16 alone");
    }
    return levels;
}

/// `--base-name` names what `--register` makes: refused without it, or
/// given empty -- before anything is fetched or made.
void check_base_name(const std::string& base_name, bool given, bool registers) {
    if (given && !registers) {
        fail(
            "--base-name names the backends --register makes: add --register or "
            "--register-with");
    }
    if (given && base_name.empty()) {
        fail("--base-name cannot be empty -- leave it out to name the backends after the model");
    }
}

}  // namespace

Quantizer default_quantizer() {
    return [](const std::filesystem::path& input, const std::filesystem::path& output,
              std::string_view level, const models::QuantizeProgress& progress) {
        return models::quantize(input, output, level, progress);
    };
}

void run_register_chain(const RegisterChainRequest& request, const ChainTools& tools) {
    const models::StoreRoots& roots = request.roots;
    const std::vector<std::string>& levels = request.levels;

    // The model -- and so every backend's name -- is known before anything
    // runs, so a chain that would refuse at its end refuses now.
    std::string model;
    std::string snapshot = request.snapshot;
    std::string snapshot_from = request.snapshot_from;
    const bool pulls = !request.pull_ref.empty();
    if (pulls) {
        const std::optional<models::HfRef> parsed = models::parse_hf_ref(request.pull_ref);
        if (!looks_like_hf(request.pull_ref) || !parsed.has_value() || !parsed->file.empty()) {
            fail("--safetensors takes a Hugging Face repository (owner/repo); '" +
                 request.pull_ref + "' is not one");
        }
        model = models::repo_directory_name(*parsed);
    } else {
        const SnapshotChoice source = choose_snapshot(roots, snapshot, snapshot_from);
        if (!source.error.empty()) {
            fail(source.error);
        }
        model = source.model;
        if (!source.id.empty()) {
            snapshot = source.model + "/safetensors/" + source.id;
            snapshot_from.clear();
        }
    }
    const std::string base = request.base_name.empty() ? display_name(model) : request.base_name;
    std::vector<std::string> names{backend_name(base, "F16")};
    for (const std::string& level : levels) {
        names.push_back(backend_name(base, level));
    }
    check_registrations(roots, request.config_path, model, names);

    std::string joined;
    for (const std::string& level : levels) {
        joined += (joined.empty() ? "" : ",") + level;
    }
    const std::string with =
        (levels.empty() ? " --register" : " --register-with " + joined) +
        (request.base_name.empty() ? std::string{} : " --base-name " + request.base_name);
    const auto convert_resume = [&with](const std::string& from) {
        return "apogee models convert " + from + with;
    };

    ChainLog log;
    log.resume = pulls ? "apogee models pull " + request.pull_ref + " --safetensors" + with
                       : convert_resume(snapshot);

    ConvertedModel f16;
    std::vector<QuantizedModel> quants;
    std::vector<ChainStage> stages;
    if (pulls) {
        stages.push_back({"pull " + request.pull_ref + "'s full weights", [&](ChainLog& chain) {
                              const PulledSnapshot pulled =
                                  pull_snapshot(request.pull_ref, roots, /*chained=*/true);
                              snapshot = pulled.model + "/safetensors/" + pulled.id;
                              // Everything after the pull runs offline: resuming
                              // need not reach Hugging Face again.
                              chain.resume = convert_resume(snapshot);
                          }});
    }
    stages.push_back({"convert to F16", [&](ChainLog& chain) {
                          f16 = convert_model(roots, snapshot, snapshot_from, "f16",
                                              /*chained=*/true);
                          for (const std::string& warning : f16.warnings) {
                              chain.note(warning);
                          }
                      }});
    for (const std::string& level : levels) {
        stages.push_back({"quantize to " + level, [&, level](ChainLog& chain) {
                              QuantizedModel made =
                                  quantize_model(roots, f16.model + "/gguf/" + f16.id, {}, level,
                                                 tools.quantize, /*chained=*/true);
                              for (const std::string& warning : made.warnings) {
                                  chain.note(warning);
                              }
                              quants.push_back(std::move(made));
                          }});
    }
    stages.push_back({names.size() == 1 ? "register " + names.front()
                                        : "register " + std::to_string(names.size()) + " backends",
                      [&](ChainLog& chain) {
                          std::vector<Registration> wanted{{.name = names.front(),
                                                            .file = f16.file,
                                                            .projector = f16.projector}};
                          for (std::size_t index = 0; index < quants.size(); ++index) {
                              wanted.push_back({.name = names[index + 1],
                                                .file = quants[index].file,
                                                .projector = quants[index].projector});
                          }
                          register_backends(request.config_path, wanted, f16.base_model, chain);
                      }});

    std::cout << (pulls ? "pulling " + request.pull_ref + ", then " : std::string{})
              << "converting it to F16"
              << (levels.empty() ? std::string{} : ", quantizing it to " + joined)
              << " and registering "
              << (names.size() == 1 ? "it" : std::to_string(names.size()) + " backends") << "\n\n";
    (void)run_chain(stages, std::move(log), std::cout);
}

void bind_model_mutations(CLI::App& models, const std::filesystem::path& models_dir,
                          const RootContext& context) {
    // ---- pull ---------------------------------------------------------------
    auto pull_ref = std::make_shared<std::string>();
    auto pull_yes = std::make_shared<bool>(false);
    CLI::App* pull = models.add_subcommand("pull", "Download a model from Hugging Face or Ollama");
    pull->add_option("ref", *pull_ref,
                     "owner/repo[:file.gguf] for Hugging Face, or name:tag for Ollama")
        ->type_name(kPullRefValue)
        ->required();
    pull->add_flag("-y,--yes", *pull_yes, "Do not stop for the unrunnable-architecture warning");
    auto pull_safetensors = std::make_shared<bool>(false);
    pull->add_flag("--safetensors", *pull_safetensors,
                   "Download a Hugging Face repository's full-weight SafeTensors snapshot "
                   "(trainable, not runnable) instead of a GGUF");
    auto pull_register = std::make_shared<bool>(false);
    auto pull_register_with = std::make_shared<std::vector<std::string>>();
    pull->add_flag("--register", *pull_register,
                   "With --safetensors: then convert it to F16 and register that as a backend");
    const CLI::Option* pull_register_option =
        pull->add_option("--register-with", *pull_register_with,
                         "With --safetensors: --register, and quantize to each of these levels "
                         "too, each its own backend (comma-separated)")
            ->delimiter(',')
            ->type_name(words_value(models::quant_type_names()));
    auto pull_base_name = std::make_shared<std::string>();
    const CLI::Option* pull_base_name_option = pull->add_option(
        "--base-name", *pull_base_name,
        "With --register: what each backend's name begins with, before -F16, -Q4KM ... "
        "(default: the model's name)");

    pull->callback([pull_ref, pull_yes, pull_safetensors, pull_register, pull_register_with,
                    pull_register_option, pull_base_name, pull_base_name_option, models_dir,
                    &context]() {
        const std::string& ref = *pull_ref;
        const models::StoreRoots roots = store_roots(models_dir, context.config_path);
        const bool registers = *pull_register || pull_register_option->count() > 0;
        check_base_name(*pull_base_name, pull_base_name_option->count() > 0, registers);
        if (registers) {
            if (!*pull_safetensors) {
                fail(
                    "--register needs --safetensors: the chain converts a full-weight pull. A "
                    "GGUF pull is runnable as it lands -- it prints the backend to add");
            }
            run_register_chain(RegisterChainRequest{
                .roots = roots,
                .config_path = harness::resolve_config_path(context.config_path),
                .pull_ref = ref,
                .snapshot = {},
                .snapshot_from = {},
                .levels = buildable_levels(*pull_register_with),
                .base_name = *pull_base_name,
            });
            return;
        }
        if (*pull_safetensors) {
            (void)pull_snapshot(ref, roots, /*chained=*/false);
            return;
        }

        models::SourcePromise promise;
        models::ByteSource source;

        // Ollama first when the store actually has it: a ref like "user/model"
        // is ambiguous, and a local copy is free while a download is not.
        const std::filesystem::path store = models::ollama_store_root();
        std::optional<models::OllamaEntry> entry = models::find_in_store(store, ref);

        std::unique_ptr<backends::HttpClient> client;
        models::HfRef hf;
        const harness::CancellationToken cancellation;

        if (entry.has_value()) {
            promise = models::promise_for(*entry);
            source = models::blob_source(*entry);
        } else if (looks_like_hf(ref)) {
            std::optional<models::HfRef> parsed = models::parse_hf_ref(ref);
            if (!parsed.has_value()) {
                fail("'" + ref + "' is not a Hugging Face ref (expected owner/repo[:file.gguf])");
            }
            hf = *parsed;

            client =
                std::make_unique<backends::HttpClient>(std::make_unique<backends::CurlTransport>());
            const std::string token = models::hf_token({});

            std::string error;
            if (!models::resolve_file(*client, hf, token, cancellation, error)) {
                fail(error);
            }
            source = models::http_source(*client, hf, token, cancellation, promise);
        } else if (models::store_has_manifest(store, ref)) {
            // Found in the store, but with no model layer: a CLOUD model, whose
            // weights are on Ollama's servers. Telling this user to `ollama
            // pull` would send them to re-fetch what they already have.
            fail("'" + ref +
                 "' is an Ollama cloud model -- its weights run on Ollama's servers, so there is "
                 "nothing local to copy. Use it through a 'ollama-cli' backend instead, or pull a "
                 "local variant such as '" +
                 std::string{models::split_ref(ref).first} + "' without the -cloud tag");
        } else {
            fail("'" + ref +
                 "' is not in your Ollama store and is not a Hugging Face ref. Pull it with "
                 "'ollama pull " +
                 ref + "' first, or name a Hugging Face repository as owner/repo");
        }

        // One directory per model, one per set of weights inside it.
        const std::string model =
            entry.has_value() ? models::safe_model_name(ref) : models::repo_directory_name(hf);
        const std::string file_name = entry.has_value()
                                          ? model + ".gguf"
                                          : std::filesystem::path{hf.file}.filename().string();

        // Ollama names its blob by digest, so weights already here are found
        // before a byte moves. (Hugging Face promises no digest for a single
        // file; those are found once they land, below.)
        if (const std::string known = models::weight_id_from_digest(promise.digest);
            !known.empty()) {
            for (const models::StoredGguf& stored : models::list_store_ggufs(roots, model)) {
                if (stored.id == known) {
                    std::cout << "already here -- these exact weights are at\n  "
                              << stored.file.string() << "\n";
                    return;
                }
            }
        }
        std::cout << (entry.has_value()
                          ? "copying from your Ollama store (" + human_size(entry->size) + ")"
                          : "downloading " + promise.source_url)
                  << "\n";

        // The advisory warning, BEFORE the bytes move. It never refuses -- the
        // open-model policy means the user's answer is final.
        if (entry.has_value() && !*pull_yes) {
            const models::GgufInfo peek = models::inspect_gguf(entry->blob);
            if (peek.parsed && models::is_known_unrunnable(peek.architecture)) {
                std::cout << "warning: architecture '" << peek.architecture
                          << "' is not known to run in Apogee's llama.cpp.\n"
                          << "         Pulling anyway is fine -- pass --yes to skip this notice.\n";
            }
        }

        // Everything lands in a staging directory first and is renamed to its
        // id once the bytes are verified and hashed.
        const std::filesystem::path staging =
            models::make_incoming_dir(roots, models::kGgufFormat, model);
        DownloadProgress progress{std::cout, stdout_download_options()};
        const models::AcquireResult result =
            models::acquire(staging / file_name, promise, source,
                            [&progress](std::int64_t written, std::int64_t total) {
                                progress.bytes(written, total);
                            });
        progress.finish();
        if (!result.ok) {
            std::error_code code;
            (void)models::remove_weights(staging);
            fail(result.error);
        }

        // A vision model's projector is a SEPARATE layer in Ollama's manifest,
        // and useless to leave behind: without it the model can only do text,
        // and the user has no way to know a second file was sitting there. It
        // lives beside its model, in the same directory.
        bool projector_copied = false;
        const std::string projector_name =
            std::filesystem::path{file_name}.stem().string() + "-mmproj.gguf";
        if (entry.has_value() && entry->has_projector()) {
            std::cout << "this model has a vision projector; copying that too ("
                      << human_size(entry->projector_size) << ")\n";
            const models::AcquireResult vision =
                models::acquire(staging / projector_name, models::projector_promise_for(*entry),
                                models::projector_source(*entry));
            if (!vision.ok) {
                // The model is already in place and usable for text, so this is
                // a warning rather than a failure of the whole pull.
                std::cout << "warning: the projector could not be copied -- " << vision.error
                          << "\n         this model will work for text but not for images\n";
            } else {
                projector_copied = true;
            }
        }

        std::string id = models::weight_id_from_digest(result.sidecar.file_digest);
        if (id.empty()) {
            id = models::random_weight_id();
        }
        const models::Commit commit = models::commit_weights(
            staging, models::weights_dir(roots, models::kGgufFormat, model, id));
        if (!commit.error.empty()) {
            fail(commit.error);
        }
        std::filesystem::path model_file = commit.dir / file_name;
        std::filesystem::path projector = projector_copied ? commit.dir / projector_name : "";
        if (commit.existed) {
            // Identical weights were already stored, perhaps under another name.
            for (const models::StoredGguf& stored : models::list_store_ggufs(roots, model)) {
                if (stored.id == id) {
                    model_file = stored.file;
                    projector = stored.projector;
                }
            }
            std::cout << "\nalready here -- these exact weights are at\n  " << model_file.string()
                      << "\n";
        } else {
            std::cout << "\n" << model_file.string() << "\n";
            std::cout << "verified: " << result.sidecar.verification.summary() << "\n";
            if (!result.error.empty()) {
                std::cout << "note: " << result.error << "\n";
            }
        }

        print_backend_hint(model_file, projector);
    });

    // ---- delete -------------------------------------------------------------
    auto delete_name = std::make_shared<std::string>();
    auto delete_yes = std::make_shared<bool>(false);
    CLI::App* remove = models.add_subcommand("delete", "Remove a model, or one set of its weights");
    remove
        ->add_option("name", *delete_name,
                     "A model (owner/repo or its directory name), <model>/<format>/<id>, an id, "
                     "or a backend whose model is in the store")
        ->type_name(kModelOrBackendValue)
        ->required();
    remove->add_flag("-y,--yes", *delete_yes, "Do not ask for confirmation");

    remove->callback([delete_name, delete_yes, models_dir, &context]() {
        const models::StoreRoots roots = store_roots(models_dir, context.config_path);
        // The config, for a backend's name (M7); without one, names mean the
        // store alone, as they always did.
        harness::Config config;
        try {
            const std::filesystem::path path = harness::resolve_config_path(context.config_path);
            std::error_code code;
            if (std::filesystem::exists(path, code)) {
                config = harness::load_config(path);
            }
        } catch (const harness::ConfigError&) {
            // `check` reports a broken config; a store name still deletes.
        }
        const DeletePlan plan = plan_delete(roots, config, *delete_name);
        if (!plan.ok) {
            fail(plan.error);
        }
        if (!plan.also_backend.empty()) {
            std::cout << "'" << plan.also_backend << "' is also a backend -- its model, "
                      << plan.also_backend_weights
                      << ", is not what this removes; name that to delete it.\n\n";
        }

        std::cout << "will remove:\n";
        for (const std::filesystem::path& dir : plan.removes) {
            std::cout << "  " << dir.string() << "\n";
        }
        // A backend pointing into what goes stops working: say which, before.
        for (const std::string& backend : backends_inside(context.config_path, plan.removes)) {
            std::cout << "\nbackend '" << backend
                      << "' points into this and will stop working until it is repointed.\n";
        }
        if (plan.ollama_sourced) {
            // Apogee's copy only. Ollama's blobs are shared between models, so
            // deleting one by hand corrupts every sibling that referenced it --
            // `ollama rm` is the only supported way to remove the original.
            std::cout << "\nthis was copied from your Ollama store; the original stays.\n"
                      << "to remove that too:  ollama rm " << plan.ollama_ref << "\n";
        }

        if (!*delete_yes) {
            std::cout << "\nre-run with --yes to remove.\n";
            return;
        }
        for (const std::filesystem::path& dir : plan.removes) {
            if (const std::string error = models::remove_weights(dir); !error.empty()) {
                fail(error);
            }
        }
        std::cout << "\nremoved.\n";
    });

    // ---- quantize -----------------------------------------------------------
    auto quant_in = std::make_shared<std::string>();
    auto quant_type = std::make_shared<std::string>("Q4_K_M");
    auto quant_from = std::make_shared<std::string>();
    auto quant_list = std::make_shared<bool>(false);
    CLI::App* quantize = models.add_subcommand("quantize", "Make a smaller copy of a GGUF");
    // Not `->required()`: --types is a listing, and CLI11 would demand the
    // positional before ever reaching the callback that prints the list.
    quantize
        ->add_option("model", *quant_in,
                     "A model (its newest unquantized GGUF), <model>/gguf/<id>, or a .gguf file")
        ->type_name(kGgufValue);
    quantize->add_option("-t,--type", *quant_type, "Quantization type (default Q4_K_M)")
        ->type_name(words_value(models::quant_type_names()));
    quantize->add_option("--from", *quant_from, "Which of the model's GGUFs, by id")
        ->type_name(kGgufIdValue);
    quantize->add_flag("--types", *quant_list, "List the accepted quantization types and exit");

    quantize->callback([quant_in, quant_type, quant_from, quant_list, models_dir, &context]() {
        if (*quant_list) {
            for (const models::QuantType& type : models::quant_types()) {
                std::cout << "  " << type.name << "   " << type.summary << "\n";
            }
            return;
        }
        if (quant_in->empty()) {
            fail(
                "quantize needs a model, e.g.\n"
                "  apogee models quantize Qwen/Qwen3-8B --type Q4_K_M\n"
                "  apogee models quantize --types    (to see the choices)");
        }
        const models::StoreRoots roots = store_roots(models_dir, context.config_path);
        (void)quantize_model(roots, *quant_in, *quant_from, *quant_type, default_quantizer(),
                             /*chained=*/false);
    });

    // ---- convert ------------------------------------------------------------
    auto convert_in = std::make_shared<std::string>();
    auto convert_type = std::make_shared<std::string>("f16");
    auto convert_from = std::make_shared<std::string>();
    CLI::App* convert = models.add_subcommand("convert", "Make a GGUF from SafeTensors weights");
    convert
        ->add_option("model", *convert_in,
                     "A model (its newest SafeTensors download), <model>/safetensors/<id>, or a "
                     "snapshot directory")
        ->type_name(kSnapshotValue)
        ->required();
    convert
        ->add_option("-t,--type", *convert_type,
                     "Precision to write (default f16; make it smaller with 'models quantize')")
        ->check(CLI::IsMember(training::converter_out_types()));
    convert->add_option("--from", *convert_from, "Which of the model's SafeTensors sets, by id")
        ->type_name(kSnapshotIdValue);
    auto convert_register = std::make_shared<bool>(false);
    auto convert_register_with = std::make_shared<std::vector<std::string>>();
    convert->add_flag("--register", *convert_register,
                      "Register the F16 as a backend once it is made");
    const CLI::Option* register_with_option =
        convert
            ->add_option("--register-with", *convert_register_with,
                         "--register, and quantize to each of these levels too, each its own "
                         "backend (comma-separated)")
            ->delimiter(',')
            ->type_name(words_value(models::quant_type_names()));
    auto convert_base_name = std::make_shared<std::string>();
    const CLI::Option* convert_base_name_option = convert->add_option(
        "--base-name", *convert_base_name,
        "With --register: what each backend's name begins with, before -F16, -Q4KM ... "
        "(default: the model's name)");

    convert->callback([convert_in, convert_type, convert_from, convert_register,
                       convert_register_with, register_with_option, convert_base_name,
                       convert_base_name_option, models_dir, &context]() {
        const models::StoreRoots roots = store_roots(models_dir, context.config_path);
        const bool registers = *convert_register || register_with_option->count() > 0;
        check_base_name(*convert_base_name, convert_base_name_option->count() > 0, registers);
        if (registers) {
            if (*convert_type != "f16") {
                fail(
                    "--register makes the F16 -- the source every quantization starts from; "
                    "drop --type");
            }
            run_register_chain(RegisterChainRequest{
                .roots = roots,
                .config_path = harness::resolve_config_path(context.config_path),
                .pull_ref = {},
                .snapshot = *convert_in,
                .snapshot_from = *convert_from,
                .levels = buildable_levels(*convert_register_with),
                .base_name = *convert_base_name,
            });
            return;
        }
        (void)convert_model(roots, *convert_in, *convert_from, *convert_type, /*chained=*/false);
    });

    // ---- repair -------------------------------------------------------------
    auto repair_name = std::make_shared<std::string>();
    CLI::App* repair = models.add_subcommand(
        "repair", "Re-verify a model against its record; fix a damaged SafeTensors set in place");
    repair
        ->add_option("name", *repair_name,
                     "A model (owner/repo or its directory name), <model>/<format>/<id>, or an id")
        ->type_name(kModelValue)
        ->required();
    repair->callback([repair_name, models_dir, &context]() {
        const models::StoreRoots roots = store_roots(models_dir, context.config_path);
        const models::StoreTarget target = models::resolve_store_target(roots, *repair_name);
        if (!target.error.empty() || target.outside) {
            const std::string legacy = models::legacy_refusal(roots, *repair_name);
            fail(!legacy.empty()         ? legacy
                 : !target.error.empty() ? target.error
                                         : "a model is named, not pathed: '" + *repair_name +
                                               "' is outside the model store");
        }
        bool any = false;
        if (const std::string body = render_repair(roots, *repair_name); !body.empty()) {
            std::cout << body;
            any = true;
        }
        if (target.format.empty() || target.format == models::kSafetensorsFormat) {
            for (const models::StoredSnapshot& stored :
                 models::list_store_snapshots(roots, target.model)) {
                if (!target.id.empty() && target.id != stored.id) {
                    continue;
                }
                std::cout << (any ? "\n" : "") << "snapshot: " << stored.model << "/safetensors/"
                          << stored.id << "\n";
                any = true;
                if (!repair_snapshot_in_place(stored.dir)) {
                    throw CLI::RuntimeError(1);
                }
            }
        }
        if (!any) {
            fail("nothing stored under '" + *repair_name + "'");
        }
    });

    // ---- migrate ------------------------------------------------------------
    bind_model_migrate(models, models_dir, context);
}

}  // namespace apogee::commands
