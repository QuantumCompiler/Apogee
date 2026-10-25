#include "cli/models_pull.h"

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
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

#include "backends/mlx_local.h"
#include "cli/config_cmd.h"
#include "cli/helpers.h"
#include "cli/interrupt.h"
#include "cli/job_stream.h"
#include "cli/model_chain.h"
#include "cli/models.h"
#include "cli/models_migrate.h"
#include "contracts/config.h"
#include "contracts/config_edit.h"
#include "contracts/errors.h"
#include "contracts/layout.h"
#include "contracts/paths.h"
#include "models/acquire.h"
#include "models/convert.h"
#include "models/quantize.h"
#include "models/source_hf.h"
#include "models/source_ollama.h"
#include "modelstore/gguf_inspect.h"
#include "modelstore/mlx_info.h"
#include "modelstore/pipeline_info.h"
#include "modelstore/snapshot.h"
#include "modelstore/store.h"
#include "modelstore/voice_info.h"
#include "operations/renames.h"
#include "operations/store_references.h"
#include "platform/platform.h"
#include "training/convert.h"
#include "training/mlx_convert.h"
#include "training/python_env.h"
#include "transport/http_client.h"
#include "views/download_progress.h"
#include "views/status_line.h"

namespace apogee::commands {
namespace {

/// What installs mlx-video's Wan converter (53k): opt-in, never at install.
constexpr std::string_view kMlxVideoRemedy = "apogee train setup --with mlx-video";

/// What installs mlx-audio's converter and its Kokoro (53m): opt-in too.
constexpr std::string_view kMlxAudioRemedy = "apogee train setup --with mlx-audio";

/// Whether `conversions` -- what `models::snapshot_conversions` answers for a
/// set -- holds `format`.
[[nodiscard]] bool offers(const std::vector<std::string>& conversions, std::string_view format) {
    return std::ranges::find(conversions, format) != conversions.end();
}

/// A failure said on the job's stream too, when one is open (52b): its
/// `error` event carries the words stderr gets.
void tell_job(const std::string& message) {
    if (JobStream* job = JobStream::current(); job != nullptr) {
        job->failed(message);
    }
}

[[noreturn]] void fail(const std::string& message) {
    std::cerr << "apogee models: " << message << "\n";
    tell_job(message);
    throw CLI::RuntimeError(1);
}

/// A Ctrl-C said: `cancelled -- <what>`, on stderr and as the stream's
/// error, exit 130.
[[noreturn]] void say_cancelled(const std::string& what) {
    std::cerr << "apogee models: cancelled -- " << what << "\n";
    tell_job("cancelled -- " + what);
    throw CLI::RuntimeError(kCancelled);
}

/// A run Ctrl-C stopped: what it staged removed (none when `staging` is
/// empty), and said.
[[noreturn]] void cancelled_nothing_written(const std::filesystem::path& staging) {
    if (!staging.empty()) {
        (void)models::remove_weights(staging);
    }
    say_cancelled("nothing was written");
}

/// A set of weights the pull stored, for the stream's documents (52b).
void note_stored(const std::string& model, std::string_view format, const std::string& id) {
    if (JobStream* job = JobStream::current(); job != nullptr) {
        job->stored(models::weights_handle(model, format, id));
    }
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
    DownloadProgress progress{std::cout, job_download_options()};
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
        DownloadProgress progress{std::cout, job_download_options()};
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
        tell_job("cancelled -- nothing was written");
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

/// The one command that puts a stored model to use, its projector included
/// -- and for an embedding model (M21), the embedding role pointed at the
/// entry, since it answers no chat (55d).
void print_backend_hint(const std::filesystem::path& model_file,
                        const std::filesystem::path& projector, bool embedding = false) {
    std::cout << (embedding ? "\nUse it as the embedding role by adding a backend:\n"
                            : "\nUse it by adding a backend:\n")
              << "  apogee config add-backend <name> --type llamacpp --model-path "
              << model_file.string();
    if (!projector.empty()) {
        std::cout << " --mmproj-path " << projector.string();
    }
    std::cout << "\n";
    if (embedding) {
        std::cout << "  apogee config set-default-embedding <name>\n";
    }
}

/// What a delete strands (M14, 55d): every reference the one walk lists
/// (`store_references`) that points inside one of the weights directories it
/// removes. A backend whose weights go is stranded whole -- its entry is what
/// is removed -- and any other reference into what goes is one to clear: a
/// `draft:`, an adapter, `models.default_video`. Best effort: a config that
/// does not load strands nothing.
struct Stranded {
    std::filesystem::path path{};
    harness::Config config{};
    /// The entries whose weights go.
    std::vector<std::string> backends{};
    /// The references to clear, none on an entry that goes whole.
    std::vector<StoreReference> cleared{};
};

[[nodiscard]] Stranded stranded_by(const models::StoreRoots& roots, const std::string& config_flag,
                                   const std::vector<std::filesystem::path>& dirs) {
    Stranded out;
    try {
        out.path = harness::resolve_config_path(config_flag);
        std::error_code code;
        if (!std::filesystem::exists(out.path, code)) {
            return out;
        }
        out.config = harness::load_config(out.path);
    } catch (const harness::ConfigError&) {
        // `check` reports a broken config; this only warns.
        return out;
    }
    std::vector<StoreReference> others;
    for (StoreReference& reference : store_references(out.config)) {
        const bool inside = std::ranges::any_of(dirs, [&](const std::filesystem::path& dir) {
            return points_inside(roots, reference, dir);
        });
        if (!inside) {
            continue;
        }
        if (!reference.weights) {
            others.push_back(std::move(reference));
        } else if (std::ranges::find(out.backends, reference.backend) == out.backends.end()) {
            out.backends.push_back(reference.backend);
        }
    }
    for (StoreReference& reference : others) {
        if (std::ranges::find(out.backends, reference.backend) == out.backends.end()) {
            out.cleared.push_back(std::move(reference));
        }
    }
    return out;
}

/// A reference a delete clears, as its lines name it.
[[nodiscard]] std::string stranded_name(const StoreReference& reference) {
    if (reference.backend.empty()) {
        return reference.where();
    }
    if (reference.field == "adapters") {
        return "the adapter " + reference.value + " on backend '" + reference.backend + "'";
    }
    return "the " + reference.field + " of backend '" + reference.backend + "'";
}

/// What stops working when `reference` names nothing, said before (55d).
[[nodiscard]] std::string stranded_warning(const StoreReference& reference) {
    if (reference.backend.empty()) {
        return reference.where() +
               " names this, and 'apogee video generate' without --model finds nothing until it "
               "names another.";
    }
    if (reference.field == "adapters") {
        return "backend '" + reference.backend + "' applies an adapter from this (" +
               reference.value + ") and is refused until it is dropped.";
    }
    return "backend '" + reference.backend + "' drafts with this (its " + reference.field +
           ") and is refused until it is cleared.";
}

/// `names` as "a", "a and b", "a, b and c".
[[nodiscard]] std::string spoken_list(const std::vector<std::string>& names) {
    std::string out;
    for (std::size_t index = 0; index < names.size(); ++index) {
        out += (index == 0 ? "" : index + 1 == names.size() ? " and " : ", ") + names[index];
    }
    return out;
}

/// What the delete does about the references it clears once the weights are
/// gone (55d): with `clear`, every one let go in one edit through the one
/// config editor (`clear_references`) and its own line said; else each kept,
/// the one command that clears it said.
[[nodiscard]] std::vector<std::string> settle_references(const Stranded& stranded, bool clear) {
    std::vector<std::string> out;
    if (stranded.cleared.empty()) {
        return out;
    }
    if (clear) {
        harness::edit_config_file(stranded.path, [&stranded](std::string_view content) {
            return clear_references(std::string{content}, stranded.config, stranded.cleared);
        });
    }
    for (const StoreReference& reference : stranded.cleared) {
        out.push_back(
            clear ? "cleared " + stranded_name(reference) + " in " + stranded.path.string()
                  : "kept " + stranded_name(reference) + ", which names nothing now -- clear it: " +
                        clear_door(stranded.config, stranded.cleared, reference));
    }
    return out;
}

}  // namespace

bool remove_backends_too(
    bool and_backends, const std::vector<std::string>& backends,
    const std::function<std::optional<std::string>(const std::string& question)>& ask,
    const std::vector<std::string>& cleared) {
    if (backends.empty() && cleared.empty()) {
        return false;
    }
    if (and_backends) {
        return true;
    }
    if (!ask) {
        return false;
    }
    std::string named;
    for (const std::string& backend : backends) {
        named += (named.empty() ? "" : ", ") + backend;
    }
    std::string question;
    if (!backends.empty()) {
        question = std::string{"Remove the backend "} +
                   (backends.size() == 1 ? "entry " : "entries ") + named;
    }
    if (!cleared.empty()) {
        question += (question.empty() ? "Clear " : ", and clear ") + spoken_list(cleared);
    }
    const std::optional<std::string> answer = ask(question + " too? [y/N] ");
    return answer.has_value() && (*answer == "y" || *answer == "Y" || *answer == "yes");
}

std::string backend_doors(const std::string& name) {
    return "repoint it: apogee config set-backend " + name +
           " --model-path <file>; or remove it: apogee config delete-backend " + name;
}

std::vector<std::string> settle_backends(const std::filesystem::path& config_path,
                                         const std::vector<std::string>& backends, bool remove) {
    std::vector<std::string> out;
    for (const std::string& backend : backends) {
        out.push_back(remove ? remove_backend(config_path, backend)
                             : "kept backend '" + backend + "', which points at nothing now -- " +
                                   backend_doors(backend));
    }
    return out;
}

namespace {

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
    return model_base_name(model);  // the base its backends are named after (46a reads it too)
}

/// How a pull of a SafeTensors set ends, standalone: what to do next with
/// what it is. A diffusion model's weights say what they are and what runs
/// them (M20, 53j) -- its MLX conversion said made where one stands in the
/// store under `roots` (M24). Every other set is told the conversions the one
/// decision offers it (`snapshot_conversions`, 55d), so each command it names
/// is one the binary accepts on that set: a draft head's MLX drafter where the
/// installed mlx-vlm drafts its type, and why not where it does not -- never
/// a GGUF, nor a registration, which a draft head is refused.
void print_snapshot_next(const models::StoreRoots& roots, const std::filesystem::path& dir,
                         const std::string& repo_id) {
    if (const std::optional<models::DiffusionModel> diffusion =
            models::stored_diffusion_model(roots, dir, installed_mlx_video_converts())) {
        std::cout << "\nThe set is " << models::describe_diffusion_model(*diffusion) << "\n";
        return;
    }
    const std::vector<std::string> conversions = models::snapshot_conversions(
        dir, installed_mlx_embedding_builds(), installed_mlx_drafter_builds());
    if (const std::optional<models::DraftHead> head = models::snapshot_draft_head(dir)) {
        std::cout << "\nThe set is "
                  << models::describe_draft_head(*head, models::draft_head_bases(roots, *head))
                  << "\n";
        if (offers(conversions, models::kMlxFormat)) {
            std::cout << "Make the drafter of it -- an MLX build an mlx backend over its base runs "
                         "beside it, named as that backend's draft -- with\n  apogee models "
                         "convert "
                      << repo_id << " --mlx\n";
        } else {
            const MlxOffer offer = installed_mlx_offer(dir);
            std::cout << "Its one conversion, an MLX drafter, is not offered here: " << offer.gap
                      << (offer.remedy.empty() ? std::string{} : " -- " + offer.remedy) << "\n";
        }
        return;
    }
    if (!offers(conversions, models::kGgufFormat)) {
        std::cout << "\nNo conversion takes these weights.\n";
        return;
    }
    std::cout << "\nA full-weight snapshot: trainable with 'apogee train', not runnable -- make "
                 "a GGUF of it with\n  apogee models convert "
              << repo_id
              << "\nNext time one command pulls, converts and registers it:\n  apogee models "
                 "pull "
              << repo_id << " --safetensors --register-with Q4_K_M\n";
}

/// The items of a tree download over `client`, each promising what the
/// listing published of its file.
[[nodiscard]] std::vector<models::TreeItem> tree_items(
    backends::HttpClient& client, const models::HfRef& ref,
    const std::vector<models::HfFile>& files, std::string_view token,
    const harness::CancellationToken& cancellation) {
    std::vector<models::TreeItem> items;
    for (const models::HfFile& file : files) {
        models::SourcePromise promise;
        models::TreeItem item;
        item.relative = file.path;
        item.source = models::http_source(client, ref, models::HfRepoKind::Model, file, token,
                                          cancellation, promise);
        item.promise = promise;
        items.push_back(std::move(item));
    }
    return items;
}

/// Pulling a set already stored again (53j): what its record lacks of the
/// files the pull takes, fetched into the same set -- which keeps its id --
/// each checked against what the repository publishes and recorded as it
/// lands.
[[nodiscard]] std::vector<std::string> complete_stored_snapshot(
    backends::HttpClient& client, const models::HfRef& ref, const std::filesystem::path& dir,
    const std::vector<models::HfFile>& gaps, std::string_view token,
    const harness::CancellationToken& cancellation) {
    std::int64_t total = 0;
    for (const models::HfFile& file : gaps) {
        total += file.size;
    }
    std::cout << "its record lacks " << gaps.size() << " file(s) this pull takes, "
              << human_size(total) << ": fetching them into the same set\n";
    DownloadProgress progress{std::cout, job_download_options()};
    const models::AcquireTreeResult result = models::complete_snapshot(
        dir, tree_items(client, ref, gaps, token, cancellation),
        [&progress](std::size_t index, std::size_t count, std::string_view relative,
                    std::int64_t written,
                    std::int64_t size) { progress.file(index, count, relative, written, size); });
    progress.finish();
    std::vector<std::string> landed;
    for (std::size_t i = 0; i < result.sidecars.size() && i < gaps.size(); ++i) {
        landed.push_back(gaps[i].path);
    }
    if (!result.ok || cancellation.stop_requested()) {
        // What landed before the failure -- or the Ctrl-C (55c) -- stays, on
        // the record: pulling again fetches the rest.
        std::string kept;
        for (const std::string& path : landed) {
            kept += (kept.empty() ? "" : ", ") + path;
        }
        if (cancellation.stop_requested()) {
            say_cancelled(kept.empty() ? std::string{"nothing was written"}
                                       : kept +
                                             " landed and stays on the set's record; pull again "
                                             "for the rest");
        }
        fail(result.error + (kept.empty() ? std::string{}
                                          : " (" + kept +
                                                " landed and stays on the set's record; pull "
                                                "again for the rest)"));
    }
    std::size_t digests = 0;
    for (const models::Sidecar& sidecar : result.sidecars) {
        digests += sidecar.verification.digest_checked ? 1U : 0U;
    }
    std::string fetched;
    for (const std::string& path : landed) {
        fetched += (fetched.empty() ? "" : ", ") + path;
    }
    std::cout << "\n"
              << dir.string() << "\n"
              << "completed: " << fetched << "\n"
              << "verified: " << result.files << " file(s), " << human_size(result.bytes) << ", "
              << digests << " checked against a published sha256 -- the set keeps its id\n";
    return landed;
}

}  // namespace

PulledSnapshot pull_snapshot(const std::string& ref, const models::StoreRoots& roots,
                             backends::HttpClient& client,
                             const harness::CancellationToken& cancellation, bool chained) {
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
    const std::string token = models::hf_token({});

    const models::HfTree tree =
        models::list_repo_tree(client, *parsed, models::HfRepoKind::Model, token, cancellation);
    if (!tree.ok) {
        fail(tree.error);
    }
    // A pipeline's shards sit in its components' directories, and a
    // snapshot holds a model's at its root: taken as one, it would land
    // where nothing lists it (49a).
    if (std::ranges::any_of(tree.files, [](const models::HfFile& file) {
            return file.path == models::kModelIndex;
        })) {
        fail("'" + parsed->repo_id() +
             "' is a diffusion pipeline (model_index.json at its root) -- pull it without "
             "--safetensors; it lands whole in the store's pipeline/ format");
    }
    if (std::ranges::none_of(tree.files, [](const models::HfFile& file) {
            return models::snapshot_wanted(file.path) && file.path.ends_with(".safetensors");
        })) {
        fail("'" + parsed->repo_id() +
             "' holds no .safetensors shards -- nothing to snapshot. For a GGUF, pull it "
             "without --safetensors");
    }
    // Wan's original layout comes whole, its root checkpoints with it (53j);
    // every other repository's list is the snapshot rule's alone.
    const models::HfWanLayout wan =
        models::read_wan_layout(client, *parsed, tree.files, token, cancellation);
    if (!wan.ok) {
        fail(wan.error);
    }
    const std::vector<models::HfFile> wanted = models::snapshot_files(tree.files, wan.layout());
    std::vector<models::SnapshotFile> published;
    std::int64_t total = 0;
    std::string checkpoints;
    for (const models::HfFile& file : wanted) {
        total += file.size;
        published.push_back({file.path, file.size, file.sha256});
        if (models::root_checkpoint(file.path)) {
            checkpoints += (checkpoints.empty() ? "" : ", ") + file.path;
        }
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
            PulledSnapshot pulled{.model = model, .id = known_id, .dir = existing, .completed = {}};
            // Its id is its shards' hash, so a Wan set pulled before the pull
            // took its checkpoints is this one: completed in place, never moved
            // (53j). Any other set is found and left exactly as before.
            const std::optional<models::Snapshot> record =
                wan.layout() ? models::load_snapshot(existing) : std::nullopt;
            const std::vector<models::HfFile> gaps = record.has_value()
                                                         ? models::snapshot_gaps(*record, wanted)
                                                         : std::vector<models::HfFile>{};
            if (!gaps.empty()) {
                pulled.completed =
                    complete_stored_snapshot(client, *parsed, existing, gaps, token, cancellation);
                if (!chained) {
                    print_snapshot_next(roots, existing, parsed->repo_id());
                }
            }
            return pulled;
        }
    }
    // Without a published digest on every shard the id waits for the bytes.
    const std::filesystem::path destination =
        known_id.empty() ? models::incoming_path(roots, models::kSafetensorsFormat, model)
                         : models::weights_dir(roots, models::kSafetensorsFormat, model, known_id);
    if (wan.layout()) {
        std::cout << "Wan's original layout (" << wan.class_name
                  << "): its root checkpoints come too -- " << checkpoints << "\n";
    }
    std::cout << "downloading " << wanted.size() << " file(s), " << human_size(total) << ", into "
              << (known_id.empty() ? destination.parent_path() : destination).string() << "\n";

    DownloadProgress progress{std::cout, job_download_options()};
    const models::AcquireTreeResult result = models::acquire_tree(
        destination, tree_items(client, *parsed, wanted, token, cancellation),
        [&progress](std::size_t index, std::size_t count, std::string_view relative,
                    std::int64_t written,
                    std::int64_t size) { progress.file(index, count, relative, written, size); });
    progress.finish();
    if (cancellation.stop_requested()) {
        // The ladder removed its staging tree; the claim on the name goes too.
        cancelled_nothing_written(destination);
    }
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
            return PulledSnapshot{.model = model,
                                  .id = commit.dir.filename().string(),
                                  .dir = commit.dir,
                                  .completed = {}};
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
        print_snapshot_next(roots, final_dir, parsed->repo_id());
    }
    return PulledSnapshot{
        .model = model, .id = final_dir.filename().string(), .dir = final_dir, .completed = {}};
}

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
    // An MLX model goes whole -- the directory, never a shard alone (27b) --
    // and so does a pipeline, never one component (49a).
    for (const models::StoredMlx& stored : models::list_store_mlx(roots, target.model)) {
        if ((target.format.empty() || target.format == models::kMlxFormat) &&
            (target.id.empty() || target.id == stored.id)) {
            plan.removes.push_back(stored.dir);
        }
    }
    for (const models::StoredPipeline& stored : models::list_store_pipelines(roots, target.model)) {
        if ((target.format.empty() || target.format == models::kPipelineFormat) &&
            (target.id.empty() || target.id == stored.id)) {
            plan.removes.push_back(stored.dir);
        }
    }
    for (const models::StoredVoice& stored : models::list_store_voices(roots, target.model)) {
        if ((target.format.empty() || target.format == models::kVoiceFormat) &&
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

    // The stored weights the backend's model_path names, by handle: a GGUF
    // file, or an MLX model's directory (27b).
    struct Pointed {
        std::filesystem::path dir;
        std::string handle;
    };

    std::optional<Pointed> pointed_at;
    if (backend != config.backends.end() && !backend->second.model_path.empty()) {
        const std::filesystem::path path{harness::expand_env_and_home(backend->second.model_path)};
        if (const std::optional<models::StoredGguf> gguf =
                models::stored_gguf_at(models::list_store_ggufs(roots), path)) {
            pointed_at = Pointed{
                .dir = gguf->dir,
                .handle = models::weights_handle(gguf->model, models::kGgufFormat, gguf->id)};
        } else if (const std::optional<models::StoredMlx> mlx =
                       models::stored_mlx_at(models::list_store_mlx(roots), path)) {
            pointed_at =
                Pointed{.dir = mlx->dir,
                        .handle = models::weights_handle(mlx->model, models::kMlxFormat, mlx->id)};
        }
    }

    DeletePlan plan = plan_delete(roots, name);
    if (plan.ok) {
        // The store's meaning stands; a backend of the same name is said.
        if (pointed_at.has_value() &&
            std::ranges::find(plan.removes, pointed_at->dir) == plan.removes.end()) {
            plan.also_backend = std::string{name};
            plan.also_backend_weights = pointed_at->handle;
        }
        return plan;
    }
    if (backend == config.backends.end()) {
        return plan;
    }
    if (pointed_at.has_value()) {
        DeletePlan by_backend = plan_delete(roots, pointed_at->handle);
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
    if (!target.error.empty() || target.outside ||
        (!target.format.empty() && target.format != models::kGgufFormat)) {
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

namespace {

/// An embedding model's dense layers are there: llama.cpp's converter takes
/// them only when asked (M21).
[[nodiscard]] bool dense_modules(const std::filesystem::path& snapshot) {
    const std::optional<models::EmbeddingModel> embedding =
        models::snapshot_embedding_model(snapshot);
    return embedding.has_value() && embedding->dense;
}

/// Which set `given` names, before asking what kind of weights it holds.
SnapshotChoice locate_snapshot(const models::StoreRoots& roots, std::string_view given,
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
    if (target.format == models::kMlxFormat) {
        choice.error = "'" + std::string{given} +
                       "' is an MLX model; this reads full-weight SafeTensors weights";
        return choice;
    }
    if (target.format == models::kPipelineFormat || target.format == models::kVoiceFormat) {
        choice.error =
            "'" + std::string{given} + "' is a " +
            (target.format == models::kVoiceFormat ? "voice model" : "diffusion pipeline") +
            "; this reads a language model's full-weight SafeTensors weights";
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

}  // namespace

SnapshotChoice choose_snapshot(const models::StoreRoots& roots, std::string_view given,
                               std::string_view from_id, bool drafters, bool videos) {
    SnapshotChoice choice = locate_snapshot(roots, given, from_id);
    if (choice.path.empty()) {
        return choice;
    }
    if (const std::optional<models::DraftHead> head = models::snapshot_draft_head(choice.path);
        head.has_value() && !drafters) {
        // Its one conversion named where it is offered -- the installed
        // mlx-vlm drafting its type, by the one decision (53i, 55d).
        const bool drafter =
            offers(models::snapshot_conversions(choice.path, {}, installed_mlx_drafter_builds()),
                   models::kMlxFormat);
        choice.error = models::draft_head_refusal(
            "'" + std::string{given} + "'", *head, models::draft_head_bases(roots, *head),
            drafter ? "apogee models convert " + std::string{given} + " --mlx" : std::string{});
        choice.path.clear();
    } else if (const std::optional<models::DiffusionModel> diffusion =
                   models::stored_diffusion_model(roots, choice.path,
                                                  installed_mlx_video_converts());
               diffusion.has_value() && !(videos && diffusion->mlx_convertible())) {
        // Neither language model's converter takes one (M20): refused before
        // any Python starts, saying what it is -- for Wan's original layout,
        // what it lacks and whether mlx-video is there to convert it, which
        // the MLX door alone passes it to once both are (53k).
        choice.error = models::diffusion_model_refusal("'" + std::string{given} + "'", *diffusion);
        choice.path.clear();
    }
    return choice;
}

std::string_view MlxRoute::kind() const noexcept {
    if (drafter) {
        return models::kMlxDrafter;
    }
    switch (converter) {
        case training::MlxConverterKind::MlxVlm:
            return models::kMlxVision;
        case training::MlxConverterKind::MlxEmbeddings:
            return models::kMlxEmbedding;
        case training::MlxConverterKind::MlxVideo:
            return models::kMlxVideo;
        case training::MlxConverterKind::MlxAudio:
            return models::kMlxVoice;
        case training::MlxConverterKind::MlxLm:
            break;
    }
    return models::kMlxChat;
}

MlxRoute mlx_route(const std::filesystem::path& snapshot, bool text_only) {
    MlxRoute route;
    route.towers = models::snapshot_towers(snapshot);
    route.embedding = models::snapshot_embedding_model(snapshot).has_value();
    route.drafter = models::snapshot_draft_head(snapshot).has_value();
    route.video = models::snapshot_wan_layout(snapshot).has_value();
    route.converter = training::choose_mlx_converter(!route.towers.empty(), text_only,
                                                     route.embedding, route.drafter, route.video);
    return route;
}

std::vector<std::string> installed_mlx_model_types(training::MlxConverterKind converter) {
    const backends::MlxHost host = backends::MlxHost::current();
    return training::mlx_model_types(
        training::find_site_package(host.venv, training::mlx_converter_package(converter)));
}

std::optional<bool> installed_mlx_builds(const std::filesystem::path& snapshot, std::string_view as,
                                         bool text_only) {
    const std::string type =
        as.empty() ? models::read_mlx_config(snapshot).model_type : std::string{as};
    const std::vector<std::string> types =
        installed_mlx_model_types(mlx_route(snapshot, text_only).converter);
    if (type.empty() || types.empty()) {
        return std::nullopt;
    }
    return std::ranges::find(types, type) != types.end();
}

models::EmbeddingBuilds installed_mlx_embedding_builds() {
    return
        [types = std::optional<std::vector<std::string>>{}](std::string_view model_type) mutable {
            if (!types.has_value()) {
                types = installed_mlx_model_types(training::MlxConverterKind::MlxEmbeddings);
            }
            return std::ranges::find(*types, model_type) != types->end();
        };
}

bool installed_mlx_video() {
    return !training::find_mlx_video_converter(backends::MlxHost::current().venv).empty();
}

models::VideoConverts installed_mlx_video_converts() {
    return [installed = std::optional<bool>{}]() mutable {
        if (!installed.has_value()) {
            installed = installed_mlx_video();
        }
        return *installed;
    };
}

bool installed_mlx_audio() {
    return !training::find_mlx_audio_converter(backends::MlxHost::current().venv).empty();
}

models::VoiceConverts installed_mlx_audio_converts() {
    return [installed = std::optional<bool>{}]() mutable {
        if (!installed.has_value()) {
            installed = installed_mlx_audio();
        }
        return *installed;
    };
}

std::optional<VoiceSetChoice> locate_voice_set(const models::StoreRoots& roots,
                                               std::string_view given, std::string_view from_id) {
    const models::StoreTarget target = models::resolve_store_target(roots, given);
    if (!target.error.empty()) {
        return std::nullopt;
    }
    if (target.outside) {
        if (!models::is_voice_dir(target.path)) {
            return std::nullopt;
        }
        return VoiceSetChoice{.path = target.path,
                              .model = models::safe_model_name(target.path.filename().string()),
                              .id = {},
                              .error = {}};
    }
    // A model by name is a voice model's only where it holds no SafeTensors
    // set: what `convert` read before is read the same way.
    if (target.format.empty() && !target.id.empty()) {
        return std::nullopt;
    }
    if (target.format != models::kVoiceFormat &&
        !(target.format.empty() && models::list_store_snapshots(roots, target.model).empty())) {
        return std::nullopt;
    }
    const std::vector<models::StoredVoice> voices = models::list_store_voices(roots, target.model);
    if (voices.empty()) {
        if (target.format.empty()) {
            return std::nullopt;
        }
        return VoiceSetChoice{.path = {},
                              .model = target.model,
                              .id = {},
                              .error = "no voice model " + target.id + " for " + target.model};
    }
    VoiceSetChoice choice{.path = {}, .model = target.model, .id = {}, .error = {}};
    const std::string id = !target.id.empty() ? target.id : std::string{from_id};
    for (const models::StoredVoice& stored : voices) {
        if (stored.id == id || (id.empty() && voices.size() == 1)) {
            choice.path = stored.dir;
            choice.id = stored.id;
        }
    }
    if (choice.path.empty()) {
        choice.error = id.empty() ? "'" + std::string{given} + "' holds " +
                                        std::to_string(voices.size()) + " voice models -- name one:"
                                  : "no voice model " + id + " for " + target.model + " -- it has:";
        for (const models::StoredVoice& stored : voices) {
            choice.error +=
                "\n  " + models::weights_handle(stored.model, models::kVoiceFormat, stored.id);
        }
    }
    return choice;
}

models::DrafterBuilds installed_mlx_drafter_builds() {
    return [vlm = std::optional<backends::MlxVlmPackage>{}](std::string_view model_type) mutable {
        if (!vlm.has_value()) {
            vlm = backends::find_mlx_vlm(backends::MlxHost::current());
        }
        return backends::mlx_vlm_drafts(*vlm, model_type);
    };
}

MlxOffer installed_mlx_offer(const std::filesystem::path& snapshot) {
    MlxOffer offer;
    offer.towers = models::snapshot_towers(snapshot);
    offer.type = models::read_mlx_config(snapshot).model_type;
    if (const std::optional<models::WanLayout> wan = models::snapshot_wan_layout(snapshot)) {
        // mlx-video's build of Wan's original layout (53k), exactly where the
        // set is whole and mlx-video installed; with either missing, nothing
        // is offered, and the gap says which.
        const bool installed = installed_mlx_video();
        if (wan->whole() && installed) {
            offer.converter = training::MlxConverterKind::MlxVideo;
            return offer;
        }
        std::string missing;
        for (const std::string& file : wan->missing()) {
            missing += (missing.empty() ? "" : ", ") + file;
        }
        offer.gap = wan->whole()
                        ? std::string{"mlx-video is not installed in Apogee's Python environment"}
                        : "the set lacks " + missing;
        if (!installed) {
            offer.remedy = std::string{kMlxVideoRemedy};
        }
        return offer;
    }
    if (models::snapshot_draft_head(snapshot).has_value()) {
        // mlx-vlm's drafter, where the one installed drafts its type (53i):
        // with none installed to ask, the draft-heads change's answer stands
        // -- nothing is offered.
        const backends::MlxVlmPackage vlm = backends::find_mlx_vlm(backends::MlxHost::current());
        if (backends::mlx_vlm_drafts(vlm, offer.type)) {
            offer.converter = training::MlxConverterKind::MlxVlm;
        } else if (vlm.installed()) {
            offer.gap =
                "the mlx-vlm in Apogee's Python environment drafts no " +
                (offer.type.empty() ? std::string{"model of its type"} : offer.type + " model");
        } else {
            offer.gap = "mlx-vlm is not installed in Apogee's Python environment";
            offer.remedy = std::string{backends::kMlxVisionRemedy};
        }
        return offer;
    }
    if (models::snapshot_embedding_model(snapshot).has_value()) {
        // mlx-embeddings' build, where the one installed builds its type
        // (53h): with none installed to ask, M21's answer stands -- nothing
        // is offered, and the GGUF is the conversion.
        if (installed_mlx_builds(snapshot) == true) {
            offer.converter = training::MlxConverterKind::MlxEmbeddings;
        } else {
            offer.gap =
                "mlx-embeddings in Apogee's Python environment builds no " +
                (offer.type.empty() ? std::string{"model of its type"} : offer.type + " model");
        }
        return offer;
    }
    // mlx-lm's answer: the whole route of a language model, the text-only
    // one of a set with towers.
    const std::optional<bool> lm = installed_mlx_builds(snapshot, {}, /*text_only=*/true);
    if (offer.towers.empty()) {
        if (lm != false) {
            offer.converter = training::MlxConverterKind::MlxLm;
        }
        return offer;
    }
    // Towers kept by default (2026-10-21): mlx-vlm's build where it can be
    // made, mlx-lm's text-only one where it can, and nothing decided where
    // neither is installed to ask (M21).
    const std::optional<bool> vlm = installed_mlx_builds(snapshot);
    const bool undecided = !vlm.has_value() && !lm.has_value();
    if (vlm == true || undecided) {
        offer.converter = training::MlxConverterKind::MlxVlm;
    } else if (vlm.has_value()) {
        offer.gap = "the mlx-vlm in Apogee's Python environment builds no " +
                    (offer.type.empty() ? std::string{"model of its type"} : offer.type + " model");
    } else {
        offer.gap = "mlx-vlm is not installed in Apogee's Python environment";
        offer.remedy = std::string{backends::kMlxVisionRemedy};
    }
    offer.text_only = lm == true || undecided;
    return offer;
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
    if (target.format == models::kMlxFormat) {
        choice.error = "'" + std::string{given} +
                       "' is an MLX model, already quantized for the mlx backend; quantize "
                       "makes smaller GGUFs";
        return choice;
    }
    if (target.format == models::kPipelineFormat || target.format == models::kVoiceFormat) {
        choice.error =
            "'" + std::string{given} + "' is a " +
            (target.format == models::kVoiceFormat ? "voice model" : "diffusion pipeline") +
            "; quantize makes GGUFs";
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

// --- MLX models (27b) ------------------------------------------------------------------

std::string mlx_summary(const models::MlxInfo& info, const harness::BackendConfig& backend) {
    // A video model has no token window (53k), nor a voice model (53m).
    std::string line =
        info.quantization.describe() + ", " +
        (info.video || info.voice ? std::string{}
                                  : models::describe(models::mlx_window(info, backend)) + ", ") +
        format_progress_size(static_cast<std::int64_t>(info.bytes));
    if (!info.model_type.empty()) {
        line = info.model_type + ", " + line;
    }
    return line;
}

namespace {

/// The one command that puts a stored MLX model to use, under the name
/// `config add-backend` would fill it with.
void print_mlx_backend_hint(const models::StoredMlx& stored) {
    std::cout << "\nUse it by adding a backend:\n"
              << "  apogee config add-backend " << models::stored_mlx_name(stored)
              << " --type mlx --model-path " << stored.dir.string() << "\n";
    if (platform::host_target() != "macos-arm64") {
        std::cout << "(the mlx backend runs on Apple silicon macOS only; this build is "
                  << platform::host_target() << ")\n";
    }
}

/// What a drafter runs beside, as its conversion says it (53i): the bases its
/// record names, else the model it measures -- "a 2560-wide gemma4 model, none
/// stored".
[[nodiscard]] std::string drafts_beside(const std::optional<models::DraftHead>& head,
                                        const std::vector<std::string>& bases) {
    std::string out;
    for (const std::string& base : bases) {
        out += (out.empty() ? "" : ", ") + base;
    }
    if (!out.empty() || !head.has_value()) {
        return out.empty() ? std::string{"its base model"} : out;
    }
    return "a " + std::to_string(head->backbone_hidden_size) + "-wide " +
           (head->family.empty() ? std::string{"model"} : head->family + " model") +
           ", none stored";
}

/// How a drafter is put to use (53i): named as its base's `mlx` entry's
/// draft, never registered itself -- it runs beside that base and never alone.
void print_mlx_drafter_hint(const models::StoredMlx& stored) {
    std::cout << "\nDraft with it beside its base -- an mlx backend over its base's MLX build "
                 "(converted with --mlx) -- by naming it as that backend's draft:\n"
              << "  apogee config set-backend <that backend> --draft " << stored.dir.string()
              << "\n";
    if (platform::host_target() != "macos-arm64") {
        std::cout << "(the mlx backend runs on Apple silicon macOS only; this build is "
                  << platform::host_target() << ")\n";
    }
}

/// What an `mlx` entry over an embedding model is for (53h): the role that
/// names it, in the one wording the standalone verb and the chain share.
[[nodiscard]] std::string embedding_role_note(const std::string& name) {
    return "note: an embedding model -- the backend embeds and answers no chat; point the "
           "embedding role at it: apogee config set-default-embedding " +
           name;
}

/// The one thing to type once a chain has registered `name` (M3, M16): chat
/// with it -- or, for an embedding model's entry, which answers no chat,
/// point the embedding role at it (55d), on either route.
[[nodiscard]] std::string next_step(const std::string& name, bool embedding) {
    return embedding
               ? "point the embedding role at it:  apogee config set-default-embedding " + name
               : "chat with one:  apogee chat -m " + name;
}

/// The two commands that put an MLX embedding model to use (53h): an `mlx`
/// entry over it, and the embedding role pointed at that entry.
void print_mlx_embedding_hint(const models::StoredMlx& stored) {
    const std::string name = models::stored_mlx_name(stored);
    std::cout << "\nUse it as the embedding role by adding a backend:\n"
              << "  apogee config add-backend " << name << " --type mlx --model-path "
              << stored.dir.string() << "\n"
              << "  apogee config set-default-embedding " << name << "\n";
    if (platform::host_target() != "macos-arm64") {
        std::cout << "(the mlx backend runs on Apple silicon macOS only; this build is "
                  << platform::host_target() << ")\n";
    }
}

/// An embedding model's modules, as its `modules.json` names them (M21):
/// "Transformer, Pooling, Dense, Dense, Normalize".
[[nodiscard]] std::string embedding_modules(const std::filesystem::path& snapshot) {
    std::string out;
    if (const std::optional<models::EmbeddingModel> model =
            models::snapshot_embedding_model(snapshot)) {
        for (const std::string& word : model->modules) {
            out += (out.empty() ? "" : ", ") + word;
        }
    }
    return out;
}

/// What mlx-video's README says an MLX build of a Wan transformer weighs
/// (53k), as its Wan README states it at the pinned commit (read 2026-10-21):
/// the 1.3B's and the 14B's, in bf16 and at 4 bits, and no other model or
/// precision. Each is known by its transformer's width (`dim`), the fact its
/// config states that tells them apart.
struct WanReadmeSize {
    std::int64_t dim = 0;
    std::string_view model{};
    std::string_view bf16{};
    std::string_view four_bit{};
};

constexpr std::array<WanReadmeSize, 2> kWanReadmeSizes{{
    {.dim = 1536, .model = "Wan2.1's 1.3B", .bf16 = "2.7 GB", .four_bit = "799 MB"},
    {.dim = 5120, .model = "Wan's 14B", .bf16 = "about 28 GB", .four_bit = "about 8 GB"},
}};

/// The size a conversion of Wan's original layout says beside its precision
/// (53k): the README's figure for its transformer where it states one, and
/// that it states none otherwise -- never a figure of Apogee's own.
[[nodiscard]] std::string wan_readme_estimate(const std::filesystem::path& snapshot,
                                              const training::MlxPrecision& precision) {
    std::ifstream in{snapshot / "config.json", std::ios::binary};
    const nlohmann::json config = nlohmann::json::parse(in, nullptr, false);
    const std::int64_t dim =
        config.is_object() && config.contains("dim") && config["dim"].is_number_integer()
            ? config["dim"].get<std::int64_t>()
            : 0;
    const auto row = std::ranges::find(kWanReadmeSizes, dim, &WanReadmeSize::dim);
    const std::string_view figure = row == kWanReadmeSizes.end() ? std::string_view{}
                                    : precision.name == "bf16"   ? row->bf16
                                    : precision.name == "4bit"   ? row->four_bit
                                                                 : std::string_view{};
    if (figure.empty()) {
        return "no size estimate -- mlx-video's README states none for this model" +
               (row == kWanReadmeSizes.end() ? std::string{}
                                             : " at " + std::string{precision.name});
    }
    return "its transformer " + std::string{figure} + ", as mlx-video's README states for " +
           std::string{row->model};
}

[[nodiscard]] models::StoredMlx stored_at(const models::StoreRoots& roots, std::string_view model,
                                          const std::filesystem::path& dir) {
    for (const models::StoredMlx& stored : models::list_store_mlx(roots, model)) {
        if (stored.dir == dir) {
            return stored;
        }
    }
    return models::StoredMlx{
        .model = std::string{model}, .id = dir.filename().string(), .dir = dir, .arrived = {}};
}

/// The bytes under `dir`, for a progress line watching a directory grow.
[[nodiscard]] std::int64_t bytes_under(const std::filesystem::path& dir) {
    std::int64_t total = 0;
    std::error_code code;
    for (auto it = std::filesystem::recursive_directory_iterator(dir, code);
         !code && it != std::filesystem::recursive_directory_iterator(); it.increment(code)) {
        if (it->is_regular_file(code)) {
            total += static_cast<std::int64_t>(it->file_size(code));
        }
    }
    return total;
}

/// What a pulled tree's files are on record as: each as it landed.
[[nodiscard]] models::Snapshot pulled_record(const models::HfRef& ref,
                                             const std::vector<models::HfFile>& wanted,
                                             const models::AcquireTreeResult& result) {
    models::Snapshot record;
    record.ref = ref.repo_id();
    record.revision = ref.revision.empty() ? "main" : ref.revision;
    record.source = "huggingface";
    for (std::size_t i = 0; i < wanted.size() && i < result.sidecars.size(); ++i) {
        const models::Sidecar& sidecar = result.sidecars[i];
        record.files.push_back(
            {.path = wanted[i].path, .size = sidecar.file_size, .sha256 = sidecar.file_digest});
        if (record.pulled_at.empty()) {
            record.pulled_at = sidecar.pulled_at;
        }
    }
    return record;
}

/// Which checks a tree pull ran, never the bare word: a repository that
/// publishes no digest for a file is said to have published none.
[[nodiscard]] std::string pull_verification(const models::AcquireTreeResult& result,
                                            std::string_view read_whole) {
    std::size_t sized = 0;
    std::size_t digests = 0;
    for (const models::Sidecar& sidecar : result.sidecars) {
        sized += sidecar.verification.size_checked ? 1 : 0;
        digests += sidecar.verification.digest_checked ? 1 : 0;
    }
    const std::size_t unpublished = result.files - digests;
    std::string none;
    if (unpublished > 0) {
        none = digests == 0 ? " (none published)"
                            : " (" + std::to_string(unpublished) + " had none published)";
    }
    return "verified: " + std::to_string(result.files) + " file(s), " + human_size(result.bytes) +
           " -- " + std::to_string(sized) + " size(s) checked, " + std::to_string(digests) +
           " against a published sha256" + none + "; " + std::string{read_whole};
}

/// What a pulled MLX set's last rung read whole, by its layout (M22): a
/// language model's configuration, tokenizer and shards, or a video or voice
/// model's own files, as its conversion says them.
[[nodiscard]] std::string mlx_read_whole(const models::MlxInfo& info,
                                         const std::filesystem::path& dir) {
    const std::string shards = std::to_string(info.shards.size()) + " shard header(s)";
    if (info.video) {
        return std::string{models::kMlxVideoFiles} + " -- " + shards + " read whole";
    }
    if (info.voice) {
        return std::string{models::kMlxVoiceFiles} + " -- " + shards + " and " +
               std::to_string(models::read_mlx_voice_info(dir).voices.size()) +
               " voice(s) read whole";
    }
    return "config.json, a tokenizer and " + shards + " read whole";
}

/// How a pulled MLX set is put to use, by what its files say it is (M22): a
/// model an mlx backend chats with gets a backend's hint, as every pull did;
/// any other kind its kind line, then what uses it, as its conversion says.
void print_pulled_mlx_hint(const models::StoredMlx& stored, const models::MlxKind& kind) {
    if (kind.backend()) {
        print_mlx_backend_hint(stored);
        return;
    }
    std::cout << "kind: " << models::describe(kind) << "\n";
    if (kind.embeds()) {
        print_mlx_embedding_hint(stored);
    } else if (kind.word == models::kMlxDrafter) {
        print_mlx_drafter_hint(stored);
    } else if (kind.word == models::kMlxVoice) {
        std::cout << "\nSpeak with it on MLX:\n  apogee voice say \"<text>\" -o speech.wav --model "
                  << models::weights_handle(stored.model, models::kMlxFormat, stored.id) << "\n";
    }
    // A video model has no backend to add (53k): its kind line says what runs it.
}

}  // namespace

PulledMlx pull_mlx(const std::string& ref, const models::StoreRoots& roots,
                   backends::HttpClient& client, const harness::CancellationToken& cancellation) {
    const std::optional<models::HfRef> parsed = models::parse_hf_ref(ref);
    if (!parsed.has_value() || !parsed->file.empty()) {
        fail("'" + ref + "' is not a Hugging Face repository (owner/repo[@revision])");
    }
    const std::string token = models::hf_token({});
    const models::HfTree tree =
        models::list_repo_tree(client, *parsed, models::HfRepoKind::Model, token, cancellation);
    if (!tree.ok) {
        fail(tree.error);
    }
    std::vector<models::HfFile> wanted;
    std::vector<models::SnapshotFile> published;
    bool any_shard = false;
    bool config = false;
    std::int64_t total = 0;
    for (const models::HfFile& file : tree.files) {
        if (!models::snapshot_wanted(file.path)) {
            continue;
        }
        any_shard = any_shard || file.path.ends_with(".safetensors");
        config = config || file.path == "config.json";
        total += file.size;
        wanted.push_back(file);
        published.push_back({.path = file.path, .size = file.size, .sha256 = file.sha256});
    }
    // Refused before a byte moves: what cannot be a model is not downloaded.
    if (!any_shard || !config) {
        fail("'" + parsed->repo_id() + "' says it is an MLX model but holds no " +
             (config ? std::string{".safetensors weights"} : std::string{"config.json"}) +
             " -- nothing mlx-lm can load");
    }

    PulledMlx pulled;
    pulled.model = models::repo_directory_name(*parsed);
    // Hugging Face publishes every shard's sha256, so weights already here are
    // found before a byte moves -- the SafeTensors rule's id.
    if (const std::string known = models::snapshot_weight_id(published); !known.empty()) {
        const std::filesystem::path existing =
            models::weights_dir(roots, models::kMlxFormat, pulled.model, known);
        std::error_code code;
        if (std::filesystem::is_directory(existing, code)) {
            std::cout << "already here -- these exact weights are at\n  " << existing.string()
                      << "\n";
            pulled.dir = existing;
            pulled.existed = true;
            pulled.info = models::read_mlx_info(existing);
            print_pulled_mlx_hint(stored_at(roots, pulled.model, existing),
                                  models::read_mlx_kind(existing));
            return pulled;
        }
    }

    // Claimed by this process from the moment it is named: an interrupted
    // pull is a leftover `check` finds and `check --fix` removes.
    const std::filesystem::path staging =
        models::incoming_path(roots, models::kMlxFormat, pulled.model);
    std::cout << "an MLX model: downloading " << wanted.size() << " file(s), " << human_size(total)
              << ", into " << staging.parent_path().string() << "\n";
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
    DownloadProgress progress{std::cout, job_download_options()};
    const models::AcquireTreeResult result = models::acquire_tree(
        staging, items,
        [&progress](std::size_t index, std::size_t count, std::string_view relative,
                    std::int64_t written,
                    std::int64_t size) { progress.file(index, count, relative, written, size); });
    progress.finish();
    if (cancellation.stop_requested()) {
        cancelled_nothing_written(staging);
    }
    if (!result.ok) {
        (void)models::remove_weights(staging);
        fail(result.error);
    }

    // The last rung: the files make a model mlx-lm can load -- its
    // configuration, a tokenizer, every shard the index names and each
    // shard's header within its file -- or, where its files say it is a
    // video or voice model (M22), that layout's files, each whole. A
    // repository that is not one lands nothing.
    pulled.info = models::read_mlx_info(staging);
    if (!pulled.info.complete) {
        (void)models::remove_weights(staging);
        fail("what '" + parsed->repo_id() + "' holds is not a loadable MLX model -- " +
             pulled.info.problem + "; nothing was kept");
    }

    const models::StoredDirectory stored =
        models::commit_mlx(roots, pulled.model, staging, pulled_record(*parsed, wanted, result));
    if (!stored.error.empty()) {
        (void)models::remove_weights(staging);
        fail(stored.error);
    }
    pulled.dir = stored.dir;
    pulled.existed = stored.existed;
    if (stored.existed) {
        std::cout << "\nalready here -- these exact weights are at\n  " << stored.dir.string()
                  << "\n";
    } else {
        std::cout << "\n"
                  << stored.dir.string() << "\n"
                  << pull_verification(result, mlx_read_whole(pulled.info, pulled.dir)) << "\n";
    }
    std::cout << "mlx model: " << mlx_summary(pulled.info) << "\n";
    // What its files say it is (M22) decides what is said of mlx-lm, and
    // what puts it to use.
    const models::MlxKind kind = models::read_mlx_kind(pulled.dir);
    if (!pulled.info.mlx_format && kind.backend()) {
        std::cout << "note: its files carry no mlx-lm marks (no quantization, shards not saved by "
                     "mlx-lm) -- mlx-lm reads such weights all the same\n";
    }
    print_pulled_mlx_hint(stored_at(roots, pulled.model, pulled.dir), kind);
    return pulled;
}

std::string pipeline_summary(const models::PipelineInfo& info) {
    return models::describe(info) + ", " +
           format_progress_size(static_cast<std::int64_t>(info.bytes));
}

PulledPipeline pull_pipeline(const std::string& ref, const models::StoreRoots& roots,
                             backends::HttpClient& client,
                             const harness::CancellationToken& cancellation) {
    const std::optional<models::HfRef> parsed = models::parse_hf_ref(ref);
    if (!parsed.has_value() || !parsed->file.empty()) {
        fail("'" + ref + "' is not a Hugging Face repository (owner/repo[@revision])");
    }
    const std::string token = models::hf_token({});
    // The index first: it names the directories that are the pipeline, so a
    // repository that also carries a single-file checkpoint or sample media
    // does not spend them.
    const models::HfText index = models::fetch_repo_text(client, *parsed, models::HfRepoKind::Model,
                                                         models::kModelIndex, token, cancellation);
    if (!index.ok) {
        fail(index.error);
    }
    const std::vector<std::string> components = models::pipeline_components(index.text);
    if (components.empty()) {
        fail("'" + parsed->repo_id() +
             "' has a model_index.json that names no components -- not a diffusion pipeline");
    }
    const models::HfTree tree =
        models::list_repo_tree(client, *parsed, models::HfRepoKind::Model, token, cancellation);
    if (!tree.ok) {
        fail(tree.error);
    }
    const std::vector<models::HfFile> wanted = models::pipeline_wanted(tree.files, components);
    std::vector<models::SnapshotFile> published;
    std::int64_t total = 0;
    for (const models::HfFile& file : wanted) {
        total += file.size;
        published.push_back({.path = file.path, .size = file.size, .sha256 = file.sha256});
    }
    // Refused before a byte moves: a component the index names that the
    // repository holds nothing for is a pipeline that cannot load.
    for (const std::string& component : components) {
        if (std::ranges::none_of(wanted, [&component](const models::HfFile& file) {
                return file.path.starts_with(component + "/");
            })) {
            fail("'" + parsed->repo_id() + "' names the " + component +
                 " component in model_index.json but holds no " + component +
                 "/ files -- nothing diffusers can load; nothing was downloaded");
        }
    }

    PulledPipeline pulled;
    pulled.model = models::repo_directory_name(*parsed);
    // Hugging Face publishes every shard's sha256, so weights already here
    // are found before a byte moves -- the SafeTensors rule's id.
    if (const std::string known = models::snapshot_weight_id(published); !known.empty()) {
        const std::filesystem::path existing =
            models::weights_dir(roots, models::kPipelineFormat, pulled.model, known);
        std::error_code code;
        if (std::filesystem::is_directory(existing, code)) {
            std::cout << "already here -- these exact weights are at\n  " << existing.string()
                      << "\n";
            pulled.dir = existing;
            pulled.existed = true;
            pulled.info = models::read_pipeline_info(existing);
            return pulled;
        }
    }

    // Claimed by this process from the moment it is named: an interrupted
    // pull is a leftover `check` finds and `check --fix` removes.
    const std::filesystem::path staging =
        models::incoming_path(roots, models::kPipelineFormat, pulled.model);
    std::cout << "a diffusion pipeline (" << components.size() << " components): downloading "
              << wanted.size() << " file(s), " << human_size(total) << ", into "
              << staging.parent_path().string() << "\n";
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
    DownloadProgress progress{std::cout, job_download_options()};
    const models::AcquireTreeResult result = models::acquire_tree(
        staging, items,
        [&progress](std::size_t index, std::size_t count, std::string_view relative,
                    std::int64_t written,
                    std::int64_t size) { progress.file(index, count, relative, written, size); });
    progress.finish();
    if (cancellation.stop_requested()) {
        cancelled_nothing_written(staging);
    }
    if (!result.ok) {
        (void)models::remove_weights(staging);
        fail(result.error);
    }

    // The last rung: the files make a pipeline diffusers can load -- the
    // index, every component it names with what its kind needs, every shard
    // whole. A repository that is not one lands nothing.
    pulled.info = models::read_pipeline_info(staging);
    if (!pulled.info.complete) {
        (void)models::remove_weights(staging);
        fail("what '" + parsed->repo_id() + "' holds is not a loadable pipeline -- " +
             pulled.info.problem + "; nothing was kept");
    }

    const models::StoredDirectory stored = models::commit_pipeline(
        roots, pulled.model, staging, pulled_record(*parsed, wanted, result));
    if (!stored.error.empty()) {
        (void)models::remove_weights(staging);
        fail(stored.error);
    }
    pulled.dir = stored.dir;
    pulled.existed = stored.existed;
    if (stored.existed) {
        std::cout << "\nalready here -- these exact weights are at\n  " << stored.dir.string()
                  << "\n";
    } else {
        std::cout << "\n"
                  << stored.dir.string() << "\n"
                  << pull_verification(result, "model_index.json and " +
                                                   std::to_string(pulled.info.components.size()) +
                                                   " component(s) read whole, " +
                                                   std::to_string(pulled.info.shard_count()) +
                                                   " shard header(s) among them")
                  << "\n";
    }
    std::cout << "pipeline: " << pipeline_summary(pulled.info) << "\n";
    return pulled;
}

std::string voice_summary(const models::VoiceInfo& info) {
    return models::describe(info) + ", " +
           format_progress_size(static_cast<std::int64_t>(info.bytes));
}

PulledVoice pull_voice(const std::string& ref, const models::StoreRoots& roots,
                       backends::HttpClient& client,
                       const harness::CancellationToken& cancellation) {
    const std::optional<models::HfRef> parsed = models::parse_hf_ref(ref);
    if (!parsed.has_value() || !parsed->file.empty()) {
        fail("'" + ref + "' is not a Hugging Face repository (owner/repo[@revision])");
    }
    const std::string token = models::hf_token({});
    const models::HfTree tree =
        models::list_repo_tree(client, *parsed, models::HfRepoKind::Model, token, cancellation);
    if (!tree.ok) {
        fail(tree.error);
    }
    const std::vector<models::HfFile> wanted = models::voice_wanted(tree.files);
    std::vector<models::SnapshotFile> published;
    std::int64_t total = 0;
    std::size_t packs = 0;
    for (const models::HfFile& file : wanted) {
        total += file.size;
        packs += file.path.starts_with("voices/") ? 1U : 0U;
        published.push_back({.path = file.path, .size = file.size, .sha256 = file.sha256});
    }

    PulledVoice pulled;
    pulled.model = models::repo_directory_name(*parsed);
    // The repository publishes every LFS file's sha256, so a voice model
    // already here is found before a byte moves.
    if (const std::string known = models::voice_weight_id(published); !known.empty()) {
        const std::filesystem::path existing =
            models::weights_dir(roots, models::kVoiceFormat, pulled.model, known);
        std::error_code code;
        if (std::filesystem::is_directory(existing, code)) {
            std::cout << "already here -- these exact weights are at\n  " << existing.string()
                      << "\n";
            pulled.dir = existing;
            pulled.existed = true;
            pulled.info = models::read_voice_info(existing);
            return pulled;
        }
    }

    const std::filesystem::path staging =
        models::incoming_path(roots, models::kVoiceFormat, pulled.model);
    std::cout << "a voice model (" << packs << " voices): downloading " << wanted.size()
              << " file(s), " << human_size(total) << ", into " << staging.parent_path().string()
              << "\n";
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
    DownloadProgress progress{std::cout, job_download_options()};
    const models::AcquireTreeResult result = models::acquire_tree(
        staging, items,
        [&progress](std::size_t index, std::size_t count, std::string_view relative,
                    std::int64_t written,
                    std::int64_t size) { progress.file(index, count, relative, written, size); });
    progress.finish();
    if (cancellation.stop_requested()) {
        cancelled_nothing_written(staging);
    }
    if (!result.ok) {
        (void)models::remove_weights(staging);
        fail(result.error);
    }

    // The last rung: a configuration, one checkpoint and every voice whole.
    pulled.info = models::read_voice_info(staging);
    if (!pulled.info.complete) {
        (void)models::remove_weights(staging);
        fail("what '" + parsed->repo_id() + "' holds is not a loadable voice model -- " +
             pulled.info.problem + "; nothing was kept");
    }
    const models::StoredDirectory stored =
        models::commit_voice(roots, pulled.model, staging, pulled_record(*parsed, wanted, result));
    if (!stored.error.empty()) {
        (void)models::remove_weights(staging);
        fail(stored.error);
    }
    pulled.dir = stored.dir;
    pulled.existed = stored.existed;
    if (stored.existed) {
        std::cout << "\nalready here -- these exact weights are at\n  " << stored.dir.string()
                  << "\n";
    } else {
        std::cout << "\n"
                  << stored.dir.string() << "\n"
                  << pull_verification(result, "config.json, the checkpoint and " +
                                                   std::to_string(pulled.info.voices.size()) +
                                                   " voice(s) read whole")
                  << "\n";
    }
    std::cout << "voice model: " << voice_summary(pulled.info) << "\n";
    return pulled;
}

ConvertedMlx convert_model_to_mlx(const models::StoreRoots& roots, std::string_view given,
                                  std::string_view from, const training::MlxPrecision& precision,
                                  const training::MlxConverter& converter,
                                  std::string_view model_type, bool chained, bool text_only) {
    // A draft head is chosen too (53i): its MLX build is a drafter, which
    // the command's door has already asked mlx-vlm about -- and Wan's
    // original layout, whole with mlx-video installed (53k).
    const SnapshotChoice source =
        choose_snapshot(roots, given, from, /*drafters=*/true, /*videos=*/true);
    if (!source.error.empty()) {
        fail(source.error);
    }
    // Every refusal before anything is announced.
    if (const std::string damaged = models::damaged_snapshot_error(source.path); !damaged.empty()) {
        fail(damaged + " -- or repair it in place with 'apogee models repair " + source.model +
             "'");
    }
    // Which converter builds it, from its own files (53f): mlx-vlm keeps a
    // set's towers, and mlx-lm builds a language model -- or, asked, a
    // towered set's language model alone; mlx-embeddings an embedding model
    // whole (53h), which the ladder lets through for it alone.
    const MlxRoute route = mlx_route(source.path, text_only);
    const bool vlm = route.converter == training::MlxConverterKind::MlxVlm;
    // The ladder lifts M21's refusal for the embedding route only where the
    // mlx-embeddings installed builds the set's type -- the door's own
    // question, asked again here so no other way in skips it.
    const bool embeds = route.embedding && installed_mlx_builds(source.path, model_type) == true;
    if (const std::string refused = models::mlx_conversion_refusal(
            source.path, {}, route.embedding && !embeds ? std::string_view{} : route.kind());
        !refused.empty()) {
        fail(refused);
    }
    if (text_only && route.towers.empty() && !route.video) {
        std::cout << "the snapshot declares no vision or audio tower -- --text-only changes "
                     "nothing; converting it as it is\n";
    }

    ConvertedMlx out;
    out.model = source.model;
    models::Snapshot record;
    record.ref =
        source.id.empty() ? source.path.string() : source.model + "/safetensors/" + source.id;
    record.source = "convert";
    record.transform = std::string{training::mlx_converter_entry(route.converter)} + " " +
                       std::string{precision.name};
    // A drafter names the bases it runs beside (53i): the stored models of
    // its family as wide as it reads, measured now -- the draft-heads
    // change's own finding, by the name each is stored under.
    const std::optional<models::DraftHead> head = models::snapshot_draft_head(source.path);
    if (route.drafter && head.has_value()) {
        record.bases = models::draft_head_bases(roots, *head);
    }
    // The user's word on the type (M15), recorded for good.
    std::string declared;
    if (!model_type.empty()) {
        declared = models::snapshot_model_type(source.path);
        if (declared == model_type) {
            std::cout << "the snapshot's config.json already says model_type " << model_type
                      << " -- converting it as it is\n";
            model_type = {};
        } else {
            record.transform += ", converted as " + std::string{model_type} + " (model_type was " +
                                (declared.empty() ? std::string{"unstated"} : declared) + ")";
        }
    }

    // Recognised before it runs: the same set at the same precision is the
    // same weights.
    for (const models::StoredMlx& stored : models::list_store_mlx(roots, source.model)) {
        const std::optional<models::Snapshot> made = models::load_snapshot(stored.dir);
        if (made.has_value() && made->source == "convert" && made->ref == record.ref &&
            made->transform == record.transform) {
            std::cout << "already converted:\n  " << stored.dir.string() << "\n"
                      << "(to convert it again, delete it first: apogee models delete "
                      << models::weights_handle(stored.model, models::kMlxFormat, stored.id)
                      << ")\n";
            out.dir = stored.dir;
            out.existed = true;
            out.info = models::read_mlx_info(stored.dir);
            if (!chained && route.drafter) {
                print_mlx_drafter_hint(stored);
            } else if (!chained && route.embedding) {
                print_mlx_embedding_hint(stored);
            } else if (!chained && !route.video) {
                // A video model has no backend to add (53k).
                print_mlx_backend_hint(stored);
            }
            return out;
        }
    }

    // A video model's size is mlx-video's README's to state, where it does
    // (53k): never Apogee's own reckoning of its parts.
    const std::int64_t estimate =
        route.video ? 0
                    : models::estimated_mlx_bytes(
                          models::snapshot_elements(source.path).value_or(0), precision.bits);
    const std::string towers = models::describe_towers(route.towers);
    std::cout << "converting " << source.path.string() << " to MLX (" << precision.name
              << (route.video    ? "; " + wan_readme_estimate(source.path, precision)
                  : estimate > 0 ? ", about " + format_progress_size(estimate)
                                 : std::string{})
              << ") -- "
              << (route.video     ? "mlx-video's converter, Wan's original layout (" +
                                        models::snapshot_wan_layout(source.path)->class_name +
                                        ") built into an MLX video model"
                  : route.drafter ? "mlx-vlm's converter, a drafter to run beside " +
                                        drafts_beside(head, record.bases)
                  : route.embedding
                      ? "mlx-embeddings' converter, an embedding model built whole (" +
                            embedding_modules(source.path) + ")"
                  : vlm ? "mlx-vlm's converter, its towers kept (" + towers + ")"
                  : route.towers.empty()
                      ? std::string{"mlx-lm's converter"}
                      : "mlx-lm's converter, its language model alone (--text-only: " + towers +
                            " left out)")
              << ", in Apogee's Python environment\n";
    // A view the converter reads as the named type, gone however this ends.
    std::filesystem::path overlay;
    if (!model_type.empty()) {
        std::cout << "converting it as model_type " << model_type << " (its config.json says "
                  << (declared.empty() ? std::string{"none"} : declared)
                  << ") -- through a view of it; the snapshot itself is untouched\n";
        overlay = models::incoming_path(roots, models::kMlxFormat, source.model);
        if (std::string staged = models::stage_model_type_overlay(source.path, overlay, model_type);
            !staged.empty()) {
            (void)models::remove_weights(overlay);
            fail(staged);
        }
    }

    const struct OverlayRemoved {
        std::filesystem::path dir;

        explicit OverlayRemoved(std::filesystem::path path) : dir{std::move(path)} {}

        ~OverlayRemoved() {
            if (!dir.empty()) {
                (void)models::remove_weights(dir);
            }
        }

        OverlayRemoved(const OverlayRemoved&) = delete;
        OverlayRemoved& operator=(const OverlayRemoved&) = delete;
        OverlayRemoved(OverlayRemoved&&) = delete;
        OverlayRemoved& operator=(OverlayRemoved&&) = delete;
    } overlay_removed{overlay};

    // Named and claimed, never created: mlx_lm.convert makes the directory
    // itself and refuses one that exists.
    const std::filesystem::path staging =
        models::incoming_path(roots, models::kMlxFormat, source.model);
    models::MlxConvertResult result;
    {
        DownloadProgress progress{std::cout, job_download_options()};
        {
            const Ticker ticker{[&progress, &staging, estimate]() {
                progress.bytes(bytes_under(staging), estimate);
            }};
            const InterruptScope interrupt;
            result = models::convert_snapshot_to_mlx(
                source.path, staging, precision.bits,
                [&converter, &overlay](const std::filesystem::path& snapshot,
                                       const std::filesystem::path& output,
                                       const harness::CancellationToken& cancellation) {
                    return converter(overlay.empty() ? snapshot : overlay, output, {},
                                     cancellation);
                },
                InterruptScope::token(), route.kind());
        }
        progress.finish();
    }
    if (result.cancelled) {
        cancelled_nothing_written(staging);
    }
    if (!result.ok) {
        (void)models::remove_weights(staging);
        fail(result.error);
    }
    // What the converter made, beside how (53e).
    record.kind = result.kind;

    // Its id is a digest over its shards' hashes: hashed now, said, and
    // stoppable like the conversion.
    std::cout << "hashing it ("
              << format_progress_size(static_cast<std::int64_t>(result.info.bytes))
              << ") -- its shards' SHA-256 names its folder in the store\n";
    models::StoredDirectory stored;
    {
        DownloadProgress progress{std::cout, job_download_options()};
        const InterruptScope interrupt;
        const auto bytes = static_cast<std::int64_t>(result.info.bytes);
        stored = models::commit_mlx(roots, source.model, staging, record,
                                    [&progress, bytes](std::int64_t hashed) {
                                        progress.bytes(hashed, bytes);
                                        return !InterruptScope::token().stop_requested();
                                    });
        progress.finish();
    }
    if (stored.error == models::kStopped) {
        cancelled_nothing_written(staging);
    }
    if (!stored.error.empty()) {
        (void)models::remove_weights(staging);
        fail(stored.error);
    }
    out.dir = stored.dir;
    out.existed = stored.existed;
    out.info = models::read_mlx_info(stored.dir);
    std::cout << "\n"
              << (stored.existed ? "already here -- these exact weights are at\n  " : "")
              << stored.dir.string() << "\n"
              << "verified: "
              << (route.video ? std::string{models::kMlxVideoFiles} + " -- "
                              : std::string{"config.json, a tokenizer and "})
              << out.info.shards.size() << " shard header(s) read whole\n"
              << "mlx model: " << mlx_summary(out.info) << "\n";
    if (vlm || route.embedding || route.video) {
        // What its record now says it is (53e), and so what runs it.
        std::cout << "kind: " << models::describe(models::read_mlx_kind(out.dir)) << "\n";
    }
    if (const std::optional<models::MlxDrafter> drafter = models::read_mlx_drafter(out.dir);
        route.drafter && drafter.has_value()) {
        // What it drafts for, and the bases its record names (53i).
        std::cout << "drafter: " << models::describe(*drafter) << "\n";
    }
    if (route.embedding) {
        // How wide its vectors are, as its own config says (53h).
        std::cout << "vectors: " << models::describe(models::read_mlx_embedding_width(out.dir))
                  << "\n";
    }
    if (!chained) {
        if (route.video) {
            // No backend runs a video model (53k): its kind line says what does.
            return out;
        }
        if (route.drafter) {
            print_mlx_drafter_hint(stored_at(roots, out.model, out.dir));
            return out;
        }
        if (route.embedding) {
            print_mlx_embedding_hint(stored_at(roots, out.model, out.dir));
            return out;
        }
        if (!models::snapshot_has_chat_template(source.path)) {
            std::cout << base_model_note() << "\n";
        }
        print_mlx_backend_hint(stored_at(roots, out.model, out.dir));
    }
    return out;
}

ConvertedMlx convert_voice_to_mlx(const models::StoreRoots& roots, const VoiceSetChoice& source,
                                  const training::MlxPrecision& precision,
                                  const training::MlxConverter& converter) {
    ConvertedMlx out;
    out.model = source.model;
    const std::string handle =
        source.id.empty() ? source.path.string()
                          : models::weights_handle(source.model, models::kVoiceFormat, source.id);
    models::Snapshot record;
    record.ref = handle;
    record.source = "convert";
    record.transform =
        std::string{training::mlx_converter_entry(training::MlxConverterKind::MlxAudio)} + " " +
        std::string{precision.name};

    // Recognised before it runs: the same model at the same precision is the
    // same weights.
    for (const models::StoredMlx& stored : models::list_store_mlx(roots, source.model)) {
        const std::optional<models::Snapshot> made = models::load_snapshot(stored.dir);
        if (made.has_value() && made->source == "convert" && made->ref == record.ref &&
            made->transform == record.transform) {
            std::cout << "already converted:\n  " << stored.dir.string() << "\n"
                      << "(to convert it again, delete it first: apogee models delete "
                      << models::weights_handle(stored.model, models::kMlxFormat, stored.id)
                      << ")\n";
            out.dir = stored.dir;
            out.existed = true;
            out.info = models::read_mlx_info(stored.dir);
            return out;
        }
    }

    const models::VoiceInfo voice = models::read_voice_info(source.path);
    std::cout << "converting " << source.path.string() << " to MLX (" << precision.name
              << ") -- mlx-audio's converter, Kokoro's checkpoint and its " << voice.voices.size()
              << (voice.voices.size() == 1 ? " voice" : " voices")
              << " built into an MLX voice model, in Apogee's Python environment\n";
    // Named and claimed, never created: the driver makes the directory.
    const std::filesystem::path staging =
        models::incoming_path(roots, models::kMlxFormat, source.model);
    models::MlxConvertResult result;
    {
        DownloadProgress progress{std::cout, job_download_options()};
        {
            const Ticker ticker{
                [&progress, &staging]() { progress.bytes(bytes_under(staging), 0); }};
            const InterruptScope interrupt;
            result = models::convert_snapshot_to_mlx(
                source.path, staging, precision.bits,
                [&converter](const std::filesystem::path& model,
                             const std::filesystem::path& output,
                             const harness::CancellationToken& cancellation) {
                    return converter(model, output, {}, cancellation);
                },
                InterruptScope::token(), models::kMlxVoice);
        }
        progress.finish();
    }
    if (result.cancelled) {
        cancelled_nothing_written(staging);
    }
    if (!result.ok) {
        (void)models::remove_weights(staging);
        fail(result.error);
    }
    record.kind = result.kind;

    std::cout << "hashing it ("
              << format_progress_size(static_cast<std::int64_t>(result.info.bytes))
              << ") -- its shards' SHA-256 names its folder in the store\n";
    models::StoredDirectory stored;
    {
        DownloadProgress progress{std::cout, job_download_options()};
        const InterruptScope interrupt;
        const auto bytes = static_cast<std::int64_t>(result.info.bytes);
        stored = models::commit_mlx(roots, source.model, staging, record,
                                    [&progress, bytes](std::int64_t hashed) {
                                        progress.bytes(hashed, bytes);
                                        return !InterruptScope::token().stop_requested();
                                    });
        progress.finish();
    }
    if (stored.error == models::kStopped) {
        cancelled_nothing_written(staging);
    }
    if (!stored.error.empty()) {
        (void)models::remove_weights(staging);
        fail(stored.error);
    }
    out.dir = stored.dir;
    out.existed = stored.existed;
    out.info = models::read_mlx_info(stored.dir);
    const models::VoiceInfo built = models::read_mlx_voice_info(stored.dir);
    const std::string made =
        models::weights_handle(source.model, models::kMlxFormat, stored.dir.filename().string());
    std::cout << "\n"
              << (stored.existed ? "already here -- these exact weights are at\n  " : "")
              << stored.dir.string() << "\n"
              << "verified: " << models::kMlxVoiceFiles << " -- " << out.info.shards.size()
              << " shard header(s) and " << built.voices.size() << " voice(s) read whole\n"
              << "mlx model: " << mlx_summary(out.info) << "\n"
              << "kind: " << models::describe(models::read_mlx_kind(out.dir)) << "\n"
              << "\nSpeak with it on MLX:\n  apogee voice say \"<text>\" -o speech.wav --model "
              << made << "\n";
    return out;
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
    /// An embedding model (M21): its GGUF embeds and answers no chat -- the
    /// embedding role's, never a base model to chat with once tuned (55d).
    bool embedding = false;
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
    out.embedding = models::snapshot_embedding_model(source.path).has_value();
    out.base_model = !out.embedding && !models::snapshot_has_chat_template(source.path);
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
            print_backend_hint(done->file, done->projector, out.embedding);
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
        tell_job("cancelled -- nothing was written");
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
        const models::ConvertResult result =
            run_conversion(source.path, model_output,
                           training::script_converter(env.interpreter(), script, type,
                                                      training::ConverterOutput::Model,
                                                      dense_modules(source.path)),
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
        print_backend_hint(model_file, projector, out.embedding);
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
/// -- its weights, by the one walk (`store_references`, 55d), inside this
/// model's directory -- is not in the way.
void check_registrations(const models::StoreRoots& roots, const std::filesystem::path& config_path,
                         const std::string& model, const std::vector<std::string>& names,
                         std::string_view format = models::kGgufFormat) {
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
    const std::filesystem::path home = models::model_dir(roots, format, model).lexically_normal();
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
        // This model's own, when its weights -- by the one walk (55d) -- lie
        // in this model's directory.
        const std::vector<StoreReference> held = backend_references(name, found->second);
        const bool ours = std::ranges::any_of(held, [&roots, &home](const StoreReference& each) {
            return each.weights && points_inside(roots, each, home);
        });
        if (!ours) {
            const std::filesystem::path path = configured_path(found->second.model_path);
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
                       const std::vector<Registration>& wanted, bool base_model, bool embedding,
                       ChainLog& log) {
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
        if (embedding) {
            // What the entry is for (M21, 55d): the embedding role, as the
            // MLX route says it.
            log.note(embedding_role_note(entry.name));
        }
    }
    if (!wanted.empty()) {
        log.next = next_step(wanted.back().name, embedding);
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

TransportMaker curl_transport() {
    return [] { return std::make_unique<backends::CurlTransport>(); };
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
                              // Ctrl-C stops the download as it stops a
                              // conversion (55c); each later stage has its own.
                              const InterruptScope interrupt;
                              backends::HttpClient client{tools.transport()};
                              PulledSnapshot pulled;
                              try {
                                  pulled = pull_snapshot(request.pull_ref, roots, client,
                                                         InterruptScope::token(),
                                                         /*chained=*/true);
                              } catch (const harness::CancelledError&) {
                                  // Stopped in a request, before a byte was staged.
                                  say_cancelled("nothing was written");
                              }
                              note_stored(pulled.model, models::kSafetensorsFormat, pulled.id);
                              snapshot = pulled.model + "/safetensors/" + pulled.id;
                              // A set no GGUF is made of -- a draft head, a
                              // diffusion model -- stops here, its refusal
                              // naming its one door: no stage after this one
                              // could run, so there is nothing to resume (55d).
                              if (const SnapshotChoice landed =
                                      choose_snapshot(roots, snapshot, {});
                                  !landed.error.empty()) {
                                  chain.resume.clear();
                                  fail(landed.error);
                              }
                              // Everything after the pull runs offline: resuming
                              // need not reach Hugging Face again.
                              chain.resume = convert_resume(snapshot);
                          }});
    }
    stages.push_back({"convert to F16", [&](ChainLog& chain) {
                          f16 = convert_model(roots, snapshot, snapshot_from, "f16",
                                              /*chained=*/true);
                          note_stored(f16.model, models::kGgufFormat, f16.id);
                          for (const std::string& warning : f16.warnings) {
                              chain.note(warning);
                          }
                      }});
    for (const std::string& level : levels) {
        stages.push_back({"quantize to " + level, [&, level](ChainLog& chain) {
                              QuantizedModel made =
                                  quantize_model(roots, f16.model + "/gguf/" + f16.id, {}, level,
                                                 tools.quantize, /*chained=*/true);
                              note_stored(made.model, models::kGgufFormat, made.id);
                              for (const std::string& warning : made.warnings) {
                                  chain.note(warning);
                              }
                              quants.push_back(std::move(made));
                          }});
    }
    stages.push_back(
        {names.size() == 1 ? "register " + names.front()
                           : "register " + std::to_string(names.size()) + " backends",
         [&](ChainLog& chain) {
             std::vector<Registration> wanted{
                 {.name = names.front(), .file = f16.file, .projector = f16.projector}};
             for (std::size_t index = 0; index < quants.size(); ++index) {
                 wanted.push_back({.name = names[index + 1],
                                   .file = quants[index].file,
                                   .projector = quants[index].projector});
             }
             register_backends(request.config_path, wanted, f16.base_model, f16.embedding, chain);
         }});

    std::cout << (pulls ? "pulling " + request.pull_ref + ", then " : std::string{})
              << "converting it to F16"
              << (levels.empty() ? std::string{} : ", quantizing it to " + joined)
              << " and registering "
              << (names.size() == 1 ? "it" : std::to_string(names.size()) + " backends") << "\n\n";
    (void)run_chain(stages, std::move(log), std::cout);
}

namespace {

/// Adds `name` as an `mlx` backend on `dir` through the one config editor --
/// the edit `config add-backend <name> --type mlx --model-path <dir>` makes,
/// byte for byte (M16). An entry already naming the same directory is left
/// as it is: a resumed chain registers what is missing and nothing twice.
void register_mlx_backend(const std::filesystem::path& config_path, const std::string& name,
                          const std::filesystem::path& dir, bool embedding, ChainLog& log) {
    harness::Config config;
    try {
        config = harness::load_config(config_path);
    } catch (const std::exception& e) {
        fail(e.what());
    }
    if (const auto found = config.backends.find(name); found != config.backends.end()) {
        if (found->second.type != harness::BackendType::Mlx ||
            configured_path(found->second.model_path) != dir.lexically_normal()) {
            fail("backend '" + name +
                 "' already exists and points elsewhere -- remove it with 'apogee config "
                 "delete-backend " +
                 name + "', then resume");
        }
        std::cout << "already registered: " << name << "\n";
    } else {
        harness::BackendConfig backend;
        backend.type = harness::BackendType::Mlx;
        backend.model_path = dir.string();
        try {
            harness::edit_config_file(config_path, [&name, &backend](std::string_view content) {
                return harness::append_backend(content, name, backend, false);
            });
        } catch (const std::exception& e) {
            fail(e.what());
        }
        std::cout << "registered " << name << "\n";
    }
    log.ready.push_back(name);
    log.next = next_step(name, embedding);
}

}  // namespace

std::vector<std::string> register_mlx_precisions(const std::vector<std::string>& given) {
    std::vector<std::string> out;
    for (const std::string& name : given) {
        if (training::find_mlx_precision(name) == nullptr) {
            std::string names;
            for (const std::string& known : training::mlx_precision_names()) {
                names += (names.empty() ? "" : ", ") + known;
            }
            fail("--register-with with --mlx takes the MLX precisions (" + names + "); '" + name +
                 "' is " +
                 (models::canonical_quant_type(name).has_value() ? std::string{"a GGUF level"}
                                                                 : std::string{"neither"}));
        }
        if (std::ranges::find(out, name) == out.end()) {
            out.push_back(name);
        }
    }
    return out;
}

void run_mlx_register_chain(const MlxRegisterRequest& request,
                            const MlxConverterFor& converter_for) {
    const models::StoreRoots& roots = request.roots;
    const SnapshotChoice source = choose_snapshot(roots, request.snapshot, request.snapshot_from);
    if (!source.error.empty()) {
        fail(source.error);
    }
    std::string snapshot = request.snapshot;
    std::string snapshot_from = request.snapshot_from;
    if (!source.id.empty()) {
        snapshot = source.model + "/safetensors/" + source.id;
        snapshot_from.clear();
    }
    // Every name known before anything runs: a chain that would refuse at
    // its end refuses now.
    const std::string base =
        request.base_name.empty() ? display_name(source.model) : request.base_name;
    std::vector<std::string> names;
    std::string joined;
    for (const std::string& precision : request.precisions) {
        names.push_back(base + "-" + precision);
        joined += (joined.empty() ? "" : ",") + precision;
    }
    check_registrations(roots, request.config_path, source.model, names, models::kMlxFormat);
    // An embedding model's entries embed, and chat with nothing (53h).
    const bool embedding = models::snapshot_embedding_model(source.path).has_value();

    ChainLog log;
    log.resume =
        "apogee models convert " + snapshot + " --mlx" +
        (request.listed ? " --register-with " + joined
                        : " --register --type " + request.precisions.front()) +
        (request.base_name.empty() ? std::string{} : " --base-name " + request.base_name) +
        (request.model_type.empty() ? std::string{} : " --model-type " + request.model_type) +
        (request.text_only ? " --text-only" : "");

    std::vector<ChainStage> stages;
    std::vector<ConvertedMlx> made(request.precisions.size());
    for (std::size_t index = 0; index < request.precisions.size(); ++index) {
        const training::MlxPrecision& precision =
            *training::find_mlx_precision(request.precisions[index]);
        stages.push_back(
            {"convert to MLX " + std::string{precision.name}, [&, index](ChainLog& chain) {
                 made[index] = convert_model_to_mlx(roots, snapshot, snapshot_from, precision,
                                                    converter_for(precision), request.model_type,
                                                    /*chained=*/true, request.text_only);
                 if (embedding) {
                     // What the entry is for (53h): the embedding role.
                     chain.note(embedding_role_note(names[index]));
                 } else if (!models::snapshot_has_chat_template(source.path)) {
                     chain.note(base_model_note());
                 }
             }});
        stages.push_back({"register " + names[index], [&, index](ChainLog& chain) {
                              register_mlx_backend(request.config_path, names[index],
                                                   made[index].dir, embedding, chain);
                          }});
    }
    std::cout << "converting it to MLX at " << joined << " and registering "
              << (names.size() == 1 ? "it" : std::to_string(names.size()) + " backends") << "\n\n";
    (void)run_chain(stages, std::move(log), std::cout);
}

namespace {

/// What a draft head's MLX conversion does not take (53i), each refused
/// before anything is announced: a registration -- a drafter is no backend;
/// it runs beside its base, named as that backend's draft -- mlx-lm's build
/// (`--text-only`), which has no network for one, and a type named over its
/// own (`--model-type`): mlx-vlm builds it as the drafter its config names.
void refuse_drafter_misuse(const models::StoreRoots& roots, const std::string& subject,
                           const models::DraftHead& head, bool registers, bool text_only,
                           const std::string& model_type) {
    const std::string what =
        subject + " is " + models::describe_draft_head(head, models::draft_head_bases(roots, head));
    if (registers) {
        fail(what +
             " -- its MLX build is a drafter, which no backend runs, so --register and "
             "--register-with do not apply: convert it with --mlx alone, then name it as its "
             "base's mlx backend's draft: apogee config set-backend <that backend> --draft "
             "<the drafter>");
    }
    if (text_only) {
        fail(what +
             " -- --text-only asks for mlx-lm's build, and mlx-lm has no network for a draft "
             "head: its MLX build is mlx-vlm's drafter, made by --mlx alone");
    }
    if (!model_type.empty()) {
        fail(what +
             " -- --model-type does not apply: mlx-vlm builds it as the drafter its "
             "config.json names" +
             (head.model_type.empty() ? std::string{} : " (" + head.model_type + ")"));
    }
}

/// What Wan's original layout's MLX conversion does not take (53k), each
/// refused before anything is announced: a registration -- no backend runs a
/// video model -- mlx-lm's build of a language model (`--text-only`), which a
/// video model has none of, and a type named over its own (`--model-type`):
/// mlx-video converts it by its own config.
void refuse_video_misuse(const std::string& subject, const std::string& class_name, bool registers,
                         bool text_only, const std::string& model_type) {
    const std::string what = subject + " is Wan's original layout (" + class_name +
                             "), which mlx-video converts to an MLX video model";
    if (registers) {
        fail(what +
             " -- no backend runs a video model, so --register and --register-with do not "
             "apply: convert it with --mlx alone");
    }
    if (text_only) {
        fail(what +
             " -- --text-only asks for mlx-lm's build of a language model, and a video model "
             "has none: convert it with --mlx alone");
    }
    if (!model_type.empty()) {
        fail(what +
             " -- --model-type does not apply: mlx-video converts it by its own "
             "config.json");
    }
}

/// `models convert <voice model> --mlx` (53m), the voice route's door: what
/// it does not take refused first -- a registration (no backend runs a voice
/// model), `--text-only` and `--model-type` -- then the model read whole, the
/// precision (bf16 unless one is named), Apple silicon, mlx-audio installed
/// and building Kokoro, the environment and the seeded driver, each asked
/// before anything is announced; then the core.
void convert_voice_to_mlx_command(const models::StoreRoots& roots, const std::string& name,
                                  const VoiceSetChoice& source, std::string type,
                                  const std::string& model_type, bool registers, bool text_only) {
    const std::string what =
        "'" + name + "' is a voice model, which mlx-audio converts to an MLX voice model";
    if (registers) {
        fail(what +
             " -- no backend runs a voice model, so --register and --register-with do not "
             "apply: convert it with --mlx alone, then speak with it: apogee voice say");
    }
    if (text_only) {
        fail(what +
             " -- --text-only asks for mlx-lm's build of a language model, and a voice model "
             "has none: convert it with --mlx alone");
    }
    if (!model_type.empty()) {
        fail(what + " -- --model-type does not apply: mlx-audio converts it as Kokoro");
    }
    if (!source.error.empty()) {
        fail(source.error);
    }
    if (const models::VoiceInfo voice = models::read_voice_info(source.path); !voice.complete) {
        fail("'" + name + "' " + voice.problem +
             (source.id.empty()
                  ? std::string{}
                  : " -- apogee models repair " +
                        models::weights_handle(source.model, models::kVoiceFormat, source.id)));
    }
    // bf16 unless one is named: the build mlx-audio's README loads first for
    // Kokoro, and an 82M-parameter model costs little unquantized.
    if (type.empty()) {
        type = "bf16";
    }
    const training::MlxPrecision* precision = training::find_mlx_precision(type);
    if (precision == nullptr ||
        !training::mlx_converter_writes(training::MlxConverterKind::MlxAudio, *precision)) {
        fail("--mlx writes no '" + type + "'");
    }
    const backends::MlxHost host = backends::MlxHost::current();
    const backends::MlxReadiness runtime = backends::probe_mlx_runtime(host);
    if (runtime.refusal == backends::MlxRefusal::Platform) {
        fail("--mlx builds '" + name +
             "' through mlx-audio, an MLX voice model: " + runtime.message());
    }
    if (!installed_mlx_audio()) {
        fail(what + ", and mlx-audio is not installed in Apogee's Python environment (its " +
             std::string{training::kMlxAudioConverter} + ", its Kokoro and the torch its " +
             "checkpoint is read through, read from their files) -- " +
             std::string{kMlxAudioRemedy});
    }
    if (runtime.refusal == backends::MlxRefusal::NoEnvironment) {
        fail("--mlx builds '" + name +
             "' through mlx-audio, an MLX voice model: " + runtime.message());
    }
    const std::filesystem::path script = training::mlx_converter_script();
    std::error_code code;
    if (!std::filesystem::is_regular_file(script, code)) {
        fail("the MLX converter is missing: " + script.string() +
             " -- run 'apogee check --fix' to seed it");
    }
    const std::filesystem::path package = training::find_site_package(
        host.venv, training::mlx_converter_package(training::MlxConverterKind::MlxAudio));
    (void)convert_voice_to_mlx(
        roots, source, *precision,
        training::script_mlx_converter(runtime.interpreter, script, *precision, package,
                                       training::MlxConverterKind::MlxAudio));
}

/// `models convert <model> --mlx [--type <precision>]` (27b): the
/// precision, the runtime -- the backend's own ladder, file facts only --
/// and the seeded driver asked before anything is announced, then the core.
/// The converter is the set's route (53f): mlx-vlm for a set with towers,
/// unless `text_only`; mlx-video for Wan's original layout (53k); each
/// refused before Python starts where it cannot build the set.
void convert_to_mlx_command(const models::StoreRoots& roots, const std::string& given,
                            const std::string& from, std::string type,
                            const std::string& model_type, const MlxRegisterRequest* registers,
                            bool text_only) {
    if (!type.empty() && training::find_mlx_precision(type) == nullptr) {
        std::string names;
        for (const std::string& name : training::mlx_precision_names()) {
            names += (names.empty() ? "" : ", ") + name;
        }
        fail("--mlx writes " + names + "; '" + type + "' is a GGUF precision");
    }
    // What the weights are before what this machine has, as the GGUF path
    // orders it: a draft head is no conversion's input but this one, whose
    // MLX build is a drafter (53i).
    const std::string& name = registers != nullptr ? registers->snapshot : given;
    // A stored voice model is mlx-audio's route's alone (53m): what names
    // none is read below exactly as before.
    if (const std::optional<VoiceSetChoice> voice =
            locate_voice_set(roots, name, registers != nullptr ? registers->snapshot_from : from)) {
        convert_voice_to_mlx_command(roots, name, *voice, type,
                                     registers != nullptr ? registers->model_type : model_type,
                                     registers != nullptr, text_only);
        return;
    }
    // And Wan's original layout passes only whole, with mlx-video installed:
    // otherwise refused saying which is missing (53k).
    const SnapshotChoice source =
        registers != nullptr
            ? choose_snapshot(roots, registers->snapshot, registers->snapshot_from, true, true)
            : choose_snapshot(roots, given, from, true, true);
    if (!source.error.empty()) {
        fail(source.error);
    }
    const std::string& as = registers != nullptr ? registers->model_type : model_type;
    const std::optional<models::DraftHead> head = models::snapshot_draft_head(source.path);
    if (head.has_value()) {
        refuse_drafter_misuse(roots, "'" + name + "'", *head, registers != nullptr, text_only, as);
        // The precision the MLX community publishes its drafters at, unless
        // one is named: a drafter is small, and what it proposes is checked.
        if (type.empty()) {
            type = "bf16";
        }
    }
    const std::optional<models::WanLayout> wan = models::snapshot_wan_layout(source.path);
    if (wan.has_value()) {
        refuse_video_misuse("'" + name + "'", wan->class_name, registers != nullptr, text_only, as);
    }
    // 4bit unless one is named: the community builds' usual choice, and
    // mlx-video's README's quantization (53k).
    if (type.empty()) {
        type = "4bit";
    }
    const training::MlxPrecision* precision = training::find_mlx_precision(type);
    if (precision == nullptr) {
        fail("--mlx writes no '" + type + "'");
    }
    // mlx-video's converter quantizes 4 or 8 bits, or writes a float type
    // (53k): any other precision refused before anything runs.
    if (wan.has_value() &&
        !training::mlx_converter_writes(training::MlxConverterKind::MlxVideo, *precision)) {
        std::string names;
        for (const std::string& known :
             training::mlx_precision_names(training::MlxConverterKind::MlxVideo)) {
            names += (names.empty() ? "" : ", ") + known;
        }
        fail("--mlx builds '" + name + "' through mlx-video, whose converter writes " + names +
             "; '" + type + "' is not one of them");
    }
    // Nor is an embedding model an MLX conversion's (M21) -- unless the
    // mlx-embeddings installed builds its type (53h). Everywhere else, that
    // type unbuilt or no mlx-embeddings to ask, it is refused as M21 refused
    // it, before the chain announces a step.
    if (const std::optional<models::EmbeddingModel> embedding =
            models::snapshot_embedding_model(source.path);
        embedding.has_value() && installed_mlx_builds(source.path, as) != true) {
        fail(models::embedding_model_mlx_refusal("'" + name + "'", *embedding));
    }
    // Which converter, by the set's own files (53f): its towers kept through
    // mlx-vlm unless --text-only asks for mlx-lm's build; an embedding model
    // built whole through mlx-embeddings (53h); a draft head built as
    // mlx-vlm's drafter (53i).
    const MlxRoute route = mlx_route(source.path, text_only);
    const bool vlm = route.converter == training::MlxConverterKind::MlxVlm;
    const std::string towers = models::describe_towers(route.towers);
    const std::string_view converter_name = training::to_string(route.converter);
    const backends::MlxHost host = backends::MlxHost::current();
    if (head.has_value()) {
        // mlx-vlm drafts its type, or nothing converted from it could run:
        // asked of its own files before anything is announced (M21's rule).
        const backends::MlxVlmPackage installed = backends::find_mlx_vlm(host);
        const std::string what =
            "'" + name + "' is " +
            models::describe_draft_head(*head, models::draft_head_bases(roots, *head)) +
            " -- its MLX build is a drafter mlx-vlm makes, and ";
        if (!installed.installed()) {
            fail(what + "mlx-vlm is not installed in Apogee's Python environment -- " +
                 std::string{backends::kMlxVisionRemedy});
        }
        if (!backends::mlx_vlm_drafts(installed, head->model_type)) {
            fail(what + "the mlx-vlm in Apogee's Python environment" +
                 (installed.version.empty() ? std::string{} : " (" + installed.version + ")") +
                 " drafts no " +
                 (head->model_type.empty() ? std::string{"model of its type"}
                                           : head->model_type + " model") +
                 " -- its speculative path holds no drafter of that type");
        }
    }
    const backends::MlxReadiness runtime = backends::probe_mlx_runtime(host);
    if (route.video) {
        // mlx-video's set brings mlx-lm with it, and the backend's driver is
        // no part of a conversion: the platform and the environment are the
        // runtime's rungs here, mlx-video's own asked by the door (53k).
        if (runtime.refusal == backends::MlxRefusal::Platform ||
            runtime.refusal == backends::MlxRefusal::NoEnvironment) {
            fail("--mlx builds '" + name +
                 "' through mlx-video, an MLX video model: " + runtime.message());
        }
    } else if (!runtime.ready() && runtime.refusal != backends::MlxRefusal::NoDriver) {
        if (route.drafter) {
            // mlx-vlm's set brings mlx-lm with it.
            fail("--mlx builds '" + name + "' through mlx-vlm, as a drafter: " + runtime.reason +
                 " -- apogee train setup --with mlx-vlm");
        }
        if (route.embedding) {
            // mlx-embeddings' set brings mlx-lm with it, through mlx-vlm.
            fail("--mlx builds '" + name + "' through mlx-embeddings, an embedding model built " +
                 "whole: " + runtime.reason + " -- " + std::string{backends::kMlxEmbeddingsRemedy});
        }
        if (vlm) {
            // mlx-vlm's set brings mlx-lm with it.
            fail("--mlx builds '" + name + "' through mlx-vlm, its towers kept (" + towers +
                 "): " + runtime.reason + " -- apogee train setup --with mlx-vlm");
        }
        fail("--mlx runs mlx-lm's converter: " + runtime.message());
    }
    const std::filesystem::path script = training::mlx_converter_script();
    std::error_code code;
    if (!std::filesystem::is_regular_file(script, code)) {
        fail("the MLX converter is missing: " + script.string() +
             " -- run 'apogee check --fix' to seed it");
    }
    const std::filesystem::path package =
        training::find_site_package(host.venv, training::mlx_converter_package(route.converter));
    // The other route's answer, said beside a refusal where it would work:
    // mlx-lm's text-only build for a set whose towers mlx-vlm cannot keep,
    // mlx-vlm's for one --text-only sent to an mlx-lm that cannot build it.
    const auto other_route = [&source, &as, &route, vlm]() -> std::string {
        if (route.towers.empty()) {
            return {};
        }
        if (vlm) {
            return installed_mlx_builds(source.path, as, /*text_only=*/true) == true
                       ? ", or build its language model alone with mlx-lm: --text-only"
                       : "";
        }
        return installed_mlx_builds(source.path, as) == true
                   ? " -- without --text-only, mlx-vlm builds it with its towers kept"
                   : "";
    };
    if (vlm && package.empty()) {
        fail("'" + name + "' declares towers (" + towers +
             "): its MLX build keeps them through mlx-vlm, which is not installed in Apogee's "
             "Python environment -- apogee train setup --with mlx-vlm" +
             other_route());
    }
    // A type this converter builds no model for is refused here, by its own
    // files, rather than in it once Python has started (M21, the user's
    // call); M15's door -- naming the type it is -- stays open. A drafter's
    // type is its speculative path's, asked above (53i).
    if (const std::vector<std::string> types = training::mlx_model_types(package);
        !types.empty() && !route.drafter && !route.video) {
        const std::string wanted =
            as.empty() ? models::read_mlx_config(source.path).model_type : as;
        if (!wanted.empty() && std::ranges::find(types, wanted) == types.end()) {
            std::string nearest;
            for (const std::string& candidate : training::nearest_model_types(wanted, types)) {
                nearest += (nearest.empty() ? "" : ", ") + candidate;
            }
            const std::string kept = vlm ? " with towers (" + towers + ")" : "";
            fail(
                (as.empty()
                     ? "'" + name + "' is a " + wanted + " model" + kept + ", which the " +
                           std::string{converter_name} +
                           " in Apogee's Python environment builds no model for -- "
                           "convert it without --mlx" +
                           (vlm ? other_route() : std::string{}) +
                           ", or, if these weights are a type this " + std::string{converter_name} +
                           " knows, convert them as it: --model-type <type>"
                     : "--model-type " + wanted + " is no type the " + std::string{converter_name} +
                           " in Apogee's Python environment builds") +
                (nearest.empty() ? std::string{} : " (nearest here: " + nearest + ")") +
                (vlm ? std::string{} : other_route()));
        }
    }
    if (registers != nullptr) {
        // M16: the hint executed -- each precision converted and registered.
        run_mlx_register_chain(*registers, [&runtime, &script, &package,
                                            &route](const training::MlxPrecision& wanted) {
            return training::script_mlx_converter(runtime.interpreter, script, wanted, package,
                                                  route.converter);
        });
        return;
    }
    (void)convert_model_to_mlx(roots, given, from, *precision,
                               training::script_mlx_converter(runtime.interpreter, script,
                                                              *precision, package, route.converter),
                               model_type, /*chained=*/false, text_only);
}

/// `source`, asked between its chunks whether Ctrl-C was pressed (55c): the
/// copy from the Ollama store reads a local file, which no transport stops.
[[nodiscard]] models::ByteSource stoppable(models::ByteSource source,
                                           const harness::CancellationToken& cancellation) {
    return [source = std::move(source), cancellation](
               const std::function<bool(std::string_view)>& write, std::string& error) {
        const bool ok = source(
            [&write, &cancellation](std::string_view chunk) {
                return !cancellation.stop_requested() && write(chunk);
            },
            error);
        if (cancellation.stop_requested()) {
            error = "cancelled";
            return false;
        }
        return ok;
    };
}

/// A pull from Hugging Face or the Ollama store -- a SafeTensors snapshot,
/// a pipeline, a voice model, an MLX model, or a GGUF -- every request, byte
/// and file of it under `cancellation`, the one scope `pull_model` arms.
void pull_stored(const PullRequest& request, const TransportMaker& transport,
                 const harness::CancellationToken& cancellation) {
    const std::string& ref = request.ref;
    const models::StoreRoots& roots = request.roots;
    if (request.safetensors) {
        backends::HttpClient client{transport()};
        const PulledSnapshot pulled =
            pull_snapshot(ref, roots, client, cancellation, /*chained=*/false);
        note_stored(pulled.model, models::kSafetensorsFormat, pulled.id);
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

    if (entry.has_value()) {
        promise = models::promise_for(*entry);
        source = stoppable(models::blob_source(*entry), cancellation);
    } else if (looks_like_hf(ref)) {
        std::optional<models::HfRef> parsed = models::parse_hf_ref(ref);
        if (!parsed.has_value()) {
            fail("'" + ref + "' is not a Hugging Face ref (expected owner/repo[:file.gguf])");
        }
        hf = *parsed;

        client = std::make_unique<backends::HttpClient>(transport());
        const std::string token = models::hf_token({});

        if (hf.file.empty()) {
            // A diffusion pipeline is a directory, pulled whole, when
            // its root carries model_index.json (49a); an MLX model is
            // one when its model card says so and there is no GGUF to
            // prefer (27b).
            const models::HfListing listing =
                models::list_gguf_files(*client, hf, token, cancellation);
            if (listing.ok && listing.pipeline()) {
                const auto pulled = pull_pipeline(ref, roots, *client, cancellation);
                note_stored(pulled.model, models::kPipelineFormat, pulled.dir.filename().string());
                return;
            }
            // A voice model is a directory too, pulled whole when its
            // files are laid out as one (50a).
            if (listing.ok && listing.gguf_files.empty() && listing.voice()) {
                const auto pulled = pull_voice(ref, roots, *client, cancellation);
                note_stored(pulled.model, models::kVoiceFormat, pulled.dir.filename().string());
                return;
            }
            if (listing.ok && listing.gguf_files.empty() && listing.mlx()) {
                const auto pulled = pull_mlx(ref, roots, *client, cancellation);
                note_stored(pulled.model, models::kMlxFormat, pulled.dir.filename().string());
                return;
            }
            std::string error;
            if (!models::choose_file(listing, hf, error)) {
                fail(error);
            }
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
    const std::string file_name =
        entry.has_value() ? model + ".gguf" : std::filesystem::path{hf.file}.filename().string();

    // Ollama names its blob by digest, so weights already here are found
    // before a byte moves. (Hugging Face promises no digest for a single
    // file; those are found once they land, below.)
    if (const std::string known = models::weight_id_from_digest(promise.digest); !known.empty()) {
        for (const models::StoredGguf& stored : models::list_store_ggufs(roots, model)) {
            if (stored.id == known) {
                std::cout << "already here -- these exact weights are at\n  "
                          << stored.file.string() << "\n";
                note_stored(model, models::kGgufFormat, stored.id);
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
    if (entry.has_value() && !request.yes) {
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
    DownloadProgress progress{std::cout, job_download_options()};
    const models::AcquireResult result = models::acquire(
        staging / file_name, promise, source,
        [&progress](std::int64_t written, std::int64_t total) { progress.bytes(written, total); });
    progress.finish();
    if (cancellation.stop_requested()) {
        cancelled_nothing_written(staging);
    }
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
                            stoppable(models::projector_source(*entry), cancellation));
        if (cancellation.stop_requested()) {
            cancelled_nothing_written(staging);
        }
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
    const models::Commit commit =
        models::commit_weights(staging, models::weights_dir(roots, models::kGgufFormat, model, id));
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
    note_stored(model, models::kGgufFormat, id);
}

/// `models pull` on its one path (55c): the register chain, whose download
/// has its own scope beside its conversions', or any other pull under one
/// `InterruptScope` over all of it -- a listing, a download, a copy -- so a
/// Ctrl-C anywhere is noticed between chunks.
void pull_model(const PullRequest& request, const ChainTools& tools) {
    check_base_name(request.base_name, request.base_name_given, request.registers);
    if (request.registers) {
        if (!request.safetensors) {
            fail(
                "--register needs --safetensors: the chain converts a full-weight pull. A "
                "GGUF pull is runnable as it lands -- it prints the backend to add");
        }
        run_register_chain(
            RegisterChainRequest{
                .roots = request.roots,
                .config_path = request.config_path,
                .pull_ref = request.ref,
                .snapshot = {},
                .snapshot_from = {},
                .levels = buildable_levels(request.register_with),
                .base_name = request.base_name,
            },
            tools);
        return;
    }
    const InterruptScope interrupt;
    try {
        pull_stored(request, tools.transport, InterruptScope::token());
    } catch (const harness::CancelledError&) {
        // Stopped in a request, before anything was staged.
        cancelled_nothing_written({});
    }
}

}  // namespace

void run_pull(const PullRequest& request, const ChainTools& tools) {
    if (!request.stream) {
        pull_model(request, tools);
        return;
    }
    // The same pull, its progress as events (52b), and each set it stored
    // said last as `models info` says it.
    JobStream job{"models pull"};
    try {
        pull_model(request, tools);
    } catch (const CLI::RuntimeError&) {
        job.failed("the pull failed; stderr says why");
        throw;
    } catch (const std::exception& e) {
        job.failed(e.what());
        throw;
    }
    harness::Config config;
    try {
        config = harness::load_config(request.config_path);
    } catch (const std::exception&) {
        config = harness::Config{};
    }
    for (const std::string& handle : job.handles()) {
        const nlohmann::json document = model_info_document(config, handle, request.config_path);
        if (document.is_null()) {
            job.events().on_notice("no document for " + handle);
            continue;
        }
        job.events().emit_document(document);
    }
}

void bind_model_mutations(CLI::App& models, const std::filesystem::path& models_dir,
                          const RootContext& context) {
    // `models rename` (46a): a model by the name its backends share -- every
    // quantization renamed together, the directory following -- or by its
    // stored directory, through the one core.
    auto rename_old = std::make_shared<std::string>();
    auto rename_new = std::make_shared<std::string>();
    CLI::App* rename = models.add_subcommand(
        "rename",
        "Rename a model -- every quantization's backend, and the stored directory they run from");
    rename
        ->add_option("model", *rename_old,
                     "The model: its backends' name without the level (-F16, -Q4KM), or its "
                     "stored directory")
        ->type_name(kModelNameValue)
        ->required();
    rename->add_option("new-name", *rename_new, "Its new name")->required();
    rename->callback([&context, models_dir, rename_old, rename_new]() {
        const RenameOutcome outcome = rename_stored_model(
            harness::resolve_config_path(context.config_path),
            store_roots(models_dir, context.config_path), *rename_old, *rename_new);
        if (!outcome.refusal.empty()) {
            fail(outcome.refusal);
        }
        for (const std::string& line : outcome.said) {
            std::cout << line << "\n";
        }
    });

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

    auto pull_format = std::make_shared<JobFormat>(JobFormat::Text);
    add_job_format(pull, pull_format, /*document=*/false);

    pull->callback([pull_ref, pull_yes, pull_safetensors, pull_register, pull_register_with,
                    pull_register_option, pull_base_name, pull_base_name_option, pull_format,
                    models_dir, &context]() {
        run_pull(PullRequest{
            .roots = store_roots(models_dir, context.config_path),
            .config_path = harness::resolve_config_path(context.config_path),
            .ref = *pull_ref,
            .yes = *pull_yes,
            .safetensors = *pull_safetensors,
            .registers = *pull_register || pull_register_option->count() > 0,
            .register_with = *pull_register_with,
            .base_name = *pull_base_name,
            .base_name_given = pull_base_name_option->count() > 0,
            .stream = *pull_format == JobFormat::StreamJson,
        });
    });

    // ---- delete -------------------------------------------------------------
    auto delete_name = std::make_shared<std::string>();
    auto delete_yes = std::make_shared<bool>(false);
    auto delete_backends = std::make_shared<bool>(false);
    CLI::App* remove = models.add_subcommand("delete", "Remove a model, or one set of its weights");
    remove
        ->add_option("name", *delete_name,
                     "A model (owner/repo or its directory name), <model>/<format>/<id>, an id, "
                     "or a backend whose model is in the store")
        ->type_name(kModelOrBackendValue)
        ->required();
    remove->add_flag("-y,--yes", *delete_yes, "Do not ask for confirmation");
    remove->add_flag("--and-backends", *delete_backends,
                     "Also remove the backend entries that point into what goes, through the "
                     "config editor (asked at a terminal otherwise; -y never implies it)");

    remove->callback([delete_name, delete_yes, delete_backends, models_dir, &context]() {
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
        // A backend pointing into what goes stops working, and so does every
        // other reference into it: say which, and what will be done about
        // them, before (M14, 55d) -- every one the one walk lists.
        const Stranded stranded = stranded_by(roots, context.config_path, plan.removes);
        const std::vector<std::string>& affected = stranded.backends;
        std::vector<std::string> cleared;
        for (const std::string& backend : affected) {
            std::cout << "\nbackend '" << backend
                      << "' points into this and will stop working until it is repointed.\n";
        }
        for (const StoreReference& reference : stranded.cleared) {
            std::cout << "\n" << stranded_warning(reference) << "\n";
            cleared.push_back(stranded_name(reference));
        }
        if (!affected.empty()) {
            const bool one = affected.size() == 1;
            std::cout << (*delete_backends ? std::string{"--and-backends: "} +
                                                 (one ? "its entry goes"
                                                      : "their " + std::to_string(affected.size()) +
                                                            " entries go") +
                                                 " too, through the config editor.\n"
                                           : std::string{"the backend "} +
                                                 (one ? "entry stays" : "entries stay") +
                                                 " unless you say -- --and-backends removes " +
                                                 (one ? "it" : "them") + " with the weights.\n");
        }
        if (!cleared.empty()) {
            const bool one = cleared.size() == 1;
            std::cout << (*delete_backends
                              ? "--and-backends: " + spoken_list(cleared) + (one ? " is" : " are") +
                                    " cleared too, through the config editor.\n"
                              : spoken_list(cleared) + (one ? " stays" : " stay") +
                                    " unless you say -- --and-backends clears " +
                                    (one ? "it" : "them") + " with the weights.\n");
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
        // The config half on its own consent: the flag, or a yes at a
        // terminal -- never -y alone, never on a pipe without the flag.
        const bool interactive =
            platform::is_terminal(platform::StandardStream::In) && !stdin_is_piped();
        const bool remove_entries = remove_backends_too(
            *delete_backends, affected,
            [interactive](const std::string& question) -> std::optional<std::string> {
                if (!interactive) {
                    return std::nullopt;
                }
                std::cerr << "\n" << question << std::flush;
                std::string answer;
                if (!std::getline(std::cin, answer)) {
                    return std::string{};
                }
                return answer;
            },
            cleared);
        for (const std::filesystem::path& dir : plan.removes) {
            if (const std::string error = models::remove_weights(dir); !error.empty()) {
                fail(error);
            }
        }
        std::cout << "\nremoved.\n";
        if (affected.empty() && cleared.empty()) {
            return;
        }
        try {
            for (const std::string& line :
                 settle_backends(stranded.path, affected, remove_entries)) {
                std::cout << line << "\n";
            }
            for (const std::string& line : settle_references(stranded, remove_entries)) {
                std::cout << line << "\n";
            }
        } catch (const std::exception& e) {
            fail(e.what());
        }
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
    auto convert_mlx = std::make_shared<bool>(false);
    CLI::App* convert = models.add_subcommand(
        "convert", "Make a GGUF -- or with --mlx an MLX model -- from SafeTensors weights");
    convert
        ->add_option("model", *convert_in,
                     "A model (its newest SafeTensors download), <model>/safetensors/<id>, or a "
                     "snapshot directory -- with --mlx, a voice model too (<model>/voice/<id>)")
        ->type_name(kSnapshotValue)
        ->required();
    // One --type, two engines' precisions: each is checked against its own
    // list in the callback, where --mlx is known -- and completion offers the
    // one `--mlx` earlier on the line chooses.
    std::vector<std::string> convert_types = training::converter_out_types();
    for (const std::string& name : training::mlx_precision_names()) {
        if (std::ranges::find(convert_types, name) == convert_types.end()) {
            convert_types.push_back(name);
        }
    }
    const CLI::Option* convert_type_option =
        convert
            ->add_option("-t,--type", *convert_type,
                         "Precision to write: a GGUF's (default f16; make it smaller with "
                         "'models quantize'), or with --mlx an MLX model's (default 4bit; bf16 "
                         "for a draft head's drafter and a voice model)")
            ->type_name(kConvertPrecisionValue)
            ->check(CLI::IsMember(convert_types));
    convert->add_flag("--mlx", *convert_mlx,
                      "Make an MLX model (Apple silicon), quantized to --type, into the model's "
                      "mlx/: mlx-lm's converter, or for weights with a vision or audio tower "
                      "mlx-vlm's, the towers kept -- for a draft head mlx-vlm's drafter, run "
                      "beside its base, for Wan's original layout mlx-video's video model, and "
                      "for a voice model mlx-audio's");
    auto convert_text_only = std::make_shared<bool>(false);
    convert->add_flag("--text-only", *convert_text_only,
                      "With --mlx: build only the language model of weights with a vision or "
                      "audio tower, with mlx-lm's converter -- the towers left out");
    convert->add_option("--from", *convert_from, "Which of the model's SafeTensors sets, by id")
        ->type_name(kSnapshotIdValue);
    auto convert_model_type = std::make_shared<std::string>();
    convert
        ->add_option("--model-type", *convert_model_type,
                     "With --mlx: convert these weights as a type their converter knows (mlx-lm, "
                     "or mlx-vlm for weights with a tower) -- your word, for a vendor's variant "
                     "it does not list; the snapshot is untouched and the record says so")
        ->type_name(kMlxModelTypeValue);
    auto convert_register = std::make_shared<bool>(false);
    auto convert_register_with = std::make_shared<std::vector<std::string>>();
    convert->add_flag("--register", *convert_register,
                      "Register what is made as a backend: the F16, or with --mlx the --type "
                      "build");
    const CLI::Option* register_with_option =
        convert
            ->add_option("--register-with", *convert_register_with,
                         "--register, and quantize to each of these levels too, each its own "
                         "backend (comma-separated) -- with --mlx the MLX precisions, one "
                         "conversion each")
            ->delimiter(',')
            ->type_name(kRegisterLevelValue);
    auto convert_base_name = std::make_shared<std::string>();
    const CLI::Option* convert_base_name_option = convert->add_option(
        "--base-name", *convert_base_name,
        "With --register: what each backend's name begins with, before -F16, -Q4KM ... -- or "
        "with --mlx -4bit, -8bit ... (default: the model's name)");

    convert->callback([convert_in, convert_type, convert_type_option, convert_mlx, convert_from,
                       convert_model_type, convert_text_only, convert_register,
                       convert_register_with, register_with_option, convert_base_name,
                       convert_base_name_option, models_dir, &context]() {
        const models::StoreRoots roots = store_roots(models_dir, context.config_path);
        const bool registers = *convert_register || register_with_option->count() > 0;
        if (*convert_mlx) {
            const std::string type = convert_type_option->count() > 0 ? *convert_type : "4bit";
            check_base_name(*convert_base_name, convert_base_name_option->count() > 0, registers);
            if (!registers) {
                // Unnamed, the precision is the set's own default: 4bit, or
                // bf16 for a drafter (53i) and a voice model (53m).
                convert_to_mlx_command(
                    roots, *convert_in, *convert_from,
                    convert_type_option->count() > 0 ? *convert_type : std::string{},
                    *convert_model_type, nullptr, *convert_text_only);
                return;
            }
            // M16: with --mlx the chain converts once a precision.
            const bool listed = register_with_option->count() > 0;
            if (listed && convert_type_option->count() > 0) {
                fail(
                    "with --mlx, --register-with names every precision to convert -- drop "
                    "--type, or name it in the list");
            }
            const MlxRegisterRequest request{
                .roots = roots,
                .config_path = harness::resolve_config_path(context.config_path),
                .snapshot = *convert_in,
                .snapshot_from = *convert_from,
                .precisions = listed ? register_mlx_precisions(*convert_register_with)
                                     : std::vector<std::string>{type},
                .base_name = *convert_base_name,
                .model_type = *convert_model_type,
                .listed = listed,
                .text_only = *convert_text_only,
            };
            convert_to_mlx_command(roots, *convert_in, *convert_from, request.precisions.front(),
                                   *convert_model_type, &request, *convert_text_only);
            return;
        }
        if (!convert_model_type->empty()) {
            fail(
                "--model-type names the type mlx-lm converts these weights as -- it works with "
                "--mlx; the GGUF converter finds a model by its architectures");
        }
        if (*convert_text_only) {
            fail(
                "--text-only asks --mlx for mlx-lm's build of a model's language model alone, "
                "its vision or audio tower left out -- it works with --mlx");
        }
        if (std::ranges::find(training::converter_out_types(), *convert_type) ==
            training::converter_out_types().end()) {
            fail("--type " + *convert_type +
                 " is an MLX precision -- add --mlx to make an MLX model, or name a GGUF "
                 "precision (f16, bf16, f32, q8_0, auto)");
        }
        check_base_name(*convert_base_name, convert_base_name_option->count() > 0, registers);
        // An MLX precision is --mlx's: said by name (M16).
        for (const std::string& level : *convert_register_with) {
            if (!models::canonical_quant_type(level).has_value() &&
                training::find_mlx_precision(level) != nullptr) {
                fail("'" + level +
                     "' is an MLX precision -- add --mlx to register MLX builds, or name "
                     "quantization levels for --register-with");
            }
        }
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
        // An MLX model is judged against its own record, file by file, and
        // re-fetched where it was pulled from -- then read whole (27b).
        if (target.format.empty() || target.format == models::kMlxFormat) {
            for (const models::StoredMlx& stored : models::list_store_mlx(roots, target.model)) {
                if (!target.id.empty() && target.id != stored.id) {
                    continue;
                }
                std::cout << (any ? "\n" : "") << "mlx:      "
                          << models::weights_handle(stored.model, models::kMlxFormat, stored.id)
                          << "\n";
                any = true;
                if (!repair_snapshot_in_place(stored.dir)) {
                    throw CLI::RuntimeError(1);
                }
                const models::MlxInfo info = models::read_mlx_info(stored.dir);
                std::cout << "files:    "
                          << (info.complete ? "whole -- " + mlx_summary(info) : info.problem)
                          << "\n";
            }
        }
        // A pipeline the same way, every component's files against the one
        // record, then read whole (49a).
        if (target.format.empty() || target.format == models::kPipelineFormat) {
            for (const models::StoredPipeline& stored :
                 models::list_store_pipelines(roots, target.model)) {
                if (!target.id.empty() && target.id != stored.id) {
                    continue;
                }
                std::cout << (any ? "\n" : "") << "pipeline: "
                          << models::weights_handle(stored.model, models::kPipelineFormat,
                                                    stored.id)
                          << "\n";
                any = true;
                if (!repair_snapshot_in_place(stored.dir)) {
                    throw CLI::RuntimeError(1);
                }
                const models::PipelineInfo info = models::read_pipeline_info(stored.dir);
                std::cout << "files:    "
                          << (info.complete ? "whole -- " + pipeline_summary(info) : info.problem)
                          << "\n";
            }
        }
        // A voice model the same way (50a).
        if (target.format.empty() || target.format == models::kVoiceFormat) {
            for (const models::StoredVoice& stored :
                 models::list_store_voices(roots, target.model)) {
                if (!target.id.empty() && target.id != stored.id) {
                    continue;
                }
                std::cout << (any ? "\n" : "") << "voice:    "
                          << models::weights_handle(stored.model, models::kVoiceFormat, stored.id)
                          << "\n";
                any = true;
                if (!repair_snapshot_in_place(stored.dir)) {
                    throw CLI::RuntimeError(1);
                }
                const models::VoiceInfo info = models::read_voice_info(stored.dir);
                std::cout << "files:    "
                          << (info.complete ? "whole -- " + voice_summary(info) : info.problem)
                          << "\n";
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
