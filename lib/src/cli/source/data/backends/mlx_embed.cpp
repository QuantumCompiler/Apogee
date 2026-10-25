#include "backends/mlx_embed.h"

#include <cstddef>
#include <system_error>
#include <utility>

#include "backends/embedding_batch.h"
#include "backends/llamacpp_embed.h"
#include "contracts/errors.h"
#include "contracts/layout.h"
#include "contracts/paths.h"
#include "modelstore/mlx_info.h"

namespace apogee::backends {
namespace {

// One slice size for the local embedders: the driver's padded batch is as
// small as llama.cpp's, and a Ctrl-C lands between two as promptly.
static_assert(kTextsPerMlxBatch == kTextsPerLlamaBatch);

/// The most of the driver's stderr kept for a failure message -- the chat
/// driver's bound.
constexpr std::size_t kStderrTailBytes = 8192;

/// How long one read waits before the loop looks at the cancellation token.
constexpr std::chrono::milliseconds kPoll{100};

/// The last non-empty line of `text`, so a failure fits one status line.
[[nodiscard]] std::string last_line(std::string_view text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
        text.remove_suffix(1);
    }
    const std::size_t cut = text.find_last_of('\n');
    return std::string{cut == std::string_view::npos ? text : text.substr(cut + 1)};
}

}  // namespace

bool mlx_entry_embeds(const harness::BackendConfig& config) {
    if (config.type != harness::BackendType::Mlx || config.model_path.empty()) {
        return false;
    }
    const std::filesystem::path dir{harness::expand_env_and_home(config.model_path)};
    std::error_code code;
    return std::filesystem::is_directory(dir, code) && models::read_mlx_kind(dir).embeds();
}

MlxEmbedProvider::MlxEmbedProvider(Options options, Spawner spawner)
    : options_{std::move(options)}, spawner_{std::move(spawner)} {
    if (!options_.clock) {
        options_.clock = [] { return std::chrono::steady_clock::now(); };
    }
}

MlxEmbedProvider::~MlxEmbedProvider() {
    end_session();
}

MlxEmbedProvider::Options MlxEmbedProvider::options_from(const std::string& backend_name,
                                                         const harness::BackendConfig& config,
                                                         const MlxHost& host) {
    Options options;
    options.backend_name = backend_name;
    options.model = config.model.empty() ? config.model_path : config.model;
    options.model_dir = std::filesystem::path{harness::expand_env_and_home(config.model_path)};
    options.interpreter = host.venv / "bin" / "python";
    options.driver = host.embedder;
    // The width the directory's own config states (53h): no shard opened, no
    // model loaded -- this runs for every entry at every startup.
    const models::MlxEmbeddingWidth width = models::read_mlx_embedding_width(options.model_dir);
    options.width = width.known() ? static_cast<std::size_t>(width.width) : 0;
    if (config.idle_unload_seconds.has_value() && *config.idle_unload_seconds > 0) {
        options.idle_unload = std::chrono::seconds{*config.idle_unload_seconds};
    }
    // The adapter, as the loader read it, applied as the chat driver applies
    // one (55e); and where the model's hash its pin is checked against is
    // remembered.
    options.adapters = config.adapters;
    options.weight_hashing.memo = harness::weight_hash_cache_path();
    return options;
}

std::unique_ptr<MlxEmbedProvider> MlxEmbedProvider::from_config(
    const std::string& backend_name, const harness::BackendConfig& config, const MlxHost& host) {
    const MlxReadiness readiness = probe_mlx_backend(backend_name, config, host);
    if (!readiness.ready()) {
        throw harness::ProviderError(backend_name, readiness.message());
    }
    return std::make_unique<MlxEmbedProvider>(
        options_from(backend_name, config, host),
        [](const platform::ChildCommand& command, std::string& error) {
            return platform::start_child(command, error);
        });
}

std::string_view MlxEmbedProvider::backend_name() const noexcept {
    return options_.backend_name;
}

std::string MlxEmbedProvider::chat_refusal() const {
    return options_.model + " is " + models::describe_mlx_kind(models::kMlxEmbedding) +
           " -- this mlx backend embeds through mlx-embeddings and answers no chat or "
           "completion; point the embedding role at it ('apogee config set-default-embedding " +
           options_.backend_name + "') and chat on another backend";
}

harness::ChatResponse MlxEmbedProvider::chat(const harness::ChatRequest& request,
                                             const harness::CancellationToken& cancellation) {
    (void)request;
    cancellation.throw_if_cancelled();
    throw harness::ProviderError(options_.backend_name, chat_refusal());
}

harness::ChatResponse MlxEmbedProvider::stream_chat(const harness::ChatRequest& request,
                                                    const harness::StreamOptions& options) {
    (void)request;
    options.cancellation.throw_if_cancelled();
    throw harness::ProviderError(options_.backend_name, chat_refusal());
}

std::vector<harness::ModelInfo> MlxEmbedProvider::list_models(
    const harness::CancellationToken& cancellation) {
    cancellation.throw_if_cancelled();
    harness::ModelInfo info;
    info.id = options_.model;
    info.name = options_.model;
    info.provider = "mlx";
    info.backend = options_.backend_name;
    return {info};
}

std::size_t MlxEmbedProvider::embedding_dimensions() const noexcept {
    const std::size_t seen = seen_width_.load();
    return seen > 0 ? seen : options_.width;
}

std::string MlxEmbedProvider::embedding_model_name() const {
    return embedding_space("mlx:" + options_.model, options_.adapters, options_.weight_hashing);
}

int MlxEmbedProvider::spawn_count() const noexcept {
    return spawns_;
}

bool MlxEmbedProvider::has_live_child() const noexcept {
    return child_ != nullptr;
}

harness::StatusEvent MlxEmbedProvider::model_status() const {
    harness::StatusEvent event;
    event.type = ready_.has_value() && child_ != nullptr ? harness::StatusEvent::Type::ModelReady
                                                         : harness::StatusEvent::Type::ModelLoading;
    event.phase = harness::StatusEvent::Phase::Done;
    event.name = options_.model;
    return event;
}

void MlxEmbedProvider::preload(const harness::StatusSink& on_status) {
    const std::scoped_lock lock{mutex_};
    ensure_child(on_status, {});
}

void MlxEmbedProvider::set_load_listener(const harness::StatusSink& listener) {
    load_listener_ = listener;
}

void MlxEmbedProvider::hold_resident(bool held) noexcept {
    held_.store(held);
}

bool MlxEmbedProvider::held_resident() const noexcept {
    return held_.load();
}

void MlxEmbedProvider::say(const harness::StatusSink& on_status, harness::StatusEvent::Type type,
                           harness::StatusEvent::Phase phase, std::string detail) const {
    if (!on_status && !load_listener_) {
        return;
    }
    harness::StatusEvent event;
    event.type = type;
    event.phase = phase;
    event.name = options_.model;
    event.detail = std::move(detail);
    if (on_status) {
        on_status(event);
    }
    // The session's listener hears every load, whatever request made it (27e).
    if (load_listener_) {
        load_listener_(event);
    }
}

void MlxEmbedProvider::drain_stderr() {
    if (child_ == nullptr) {
        return;
    }
    std::string chunk;
    while (child_->read_stderr(chunk, std::chrono::milliseconds{0}) == platform::ReadStatus::Data) {
        stderr_tail_ += chunk;
        if (stderr_tail_.size() > kStderrTailBytes) {
            stderr_tail_.erase(0, stderr_tail_.size() - kStderrTailBytes);
        }
    }
}

std::string MlxEmbedProvider::older_copy() const {
    return "the MLX embedding driver at " + options_.driver.string() +
           " does not apply adapters -- it is an older copy: 'apogee check --fix' brings an "
           "earlier Apogee's up to this build's, and an edited one is kept, so delete it first";
}

std::string MlxEmbedProvider::with_tail(const std::string& message) const {
    const std::string tail = last_line(stderr_tail_);
    return tail.empty() ? message : message + ": " + tail;
}

void MlxEmbedProvider::drop_child() {
    if (child_ != nullptr) {
        child_->terminate();
        (void)child_->wait_for_exit(std::chrono::seconds{2});
    }
    child_.reset();
    framer_.reset();
    lines_.clear();
    ready_.reset();
}

void MlxEmbedProvider::end_session() {
    if (child_ == nullptr) {
        return;
    }
    // Closing stdin, not killing: the driver's read ends and it exits on its
    // own, its model freed the ordinary way.
    child_->close_stdin();
    if (!child_->wait_for_exit(options_.shutdown_timeout).has_value()) {
        child_->terminate();
        (void)child_->wait_for_exit(std::chrono::seconds{2});
    }
    child_.reset();
    framer_.reset();
    lines_.clear();
    ready_.reset();
}

void MlxEmbedProvider::expire_if_idle() {
    // Held by a session using it (27e): the clock waits for the hold to go.
    if (options_.idle_unload.count() <= 0 || !used_ || child_ == nullptr || held_.load()) {
        return;
    }
    if (options_.clock() - last_use_ >= options_.idle_unload) {
        end_session();
    }
}

std::optional<mlx::Event> MlxEmbedProvider::next_event(std::chrono::milliseconds stall,
                                                       const harness::CancellationToken* cancel,
                                                       bool& timed_out) {
    timed_out = false;
    const auto deadline = std::chrono::steady_clock::now() + stall;
    std::string chunk;
    for (;;) {
        while (!lines_.empty()) {
            const std::string line = std::move(lines_.front());
            lines_.pop_front();
            if (std::optional<mlx::Event> event = mlx::parse_event(line)) {
                return event;
            }
        }
        if (cancel != nullptr && cancel->stop_requested()) {
            return std::nullopt;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            timed_out = true;
            return std::nullopt;
        }
        drain_stderr();
        const platform::ReadStatus status = child_->read_stdout(chunk, kPoll);
        if (status == platform::ReadStatus::Data) {
            framer_.feed(chunk, [this](std::string_view line) { lines_.emplace_back(line); });
            continue;
        }
        if (status == platform::ReadStatus::Timeout) {
            continue;
        }
        // End of file: whatever was buffered is the child's last word.
        framer_.flush([this](std::string_view line) { lines_.emplace_back(line); });
        drain_stderr();
        if (lines_.empty()) {
            return std::nullopt;
        }
    }
}

void MlxEmbedProvider::ensure_child(const harness::StatusSink& on_status,
                                    const harness::CancellationToken& cancellation) {
    if (child_ != nullptr && ready_.has_value() && !child_->exited()) {
        return;
    }
    if (child_ != nullptr) {
        drop_child();  // it died between calls; a fresh one reloads the model
    }
    // The adapter's file facts before a driver is started (55e, 53a's
    // gate): not there, not an adapter directory with its weights, pinned to
    // no base, or pinned to weights the model directory no longer holds --
    // each refused by name.
    if (const std::string refused =
            adapter_refusal(options_.adapters, AdapterCheck{.backend = options_.backend_name,
                                                            .base = options_.model_dir,
                                                            .directories = true,
                                                            .hashing = options_.weight_hashing});
        !refused.empty()) {
        throw harness::ProviderError(options_.backend_name, refused);
    }
    stderr_tail_.clear();
    say(on_status, harness::StatusEvent::Type::ModelLoading, harness::StatusEvent::Phase::Start,
        "through mlx-embeddings, which built it");

    platform::ChildCommand command;
    command.program = options_.interpreter.string();
    command.arguments = {options_.driver.string(), "--model", options_.model_dir.string()};
    // mlx-embeddings' own `adapter_path`, applied at load and never fused.
    for (const harness::AdapterConfig& adapter : options_.adapters) {
        command.arguments.emplace_back("--adapter");
        command.arguments.push_back(adapter.path);
    }
    ++spawns_;
    std::string error;
    child_ = spawner_(command, error);
    if (child_ == nullptr) {
        throw harness::ProviderError(options_.backend_name,
                                     "could not start the MLX embedding driver" +
                                         (error.empty() ? std::string{} : ": " + error));
    }
    await_ready(cancellation);
    if (!ready_->embedding) {
        // Not the embedding driver: whatever answered would never send
        // vectors, and every batch would wait out its stall bound.
        drop_child();
        throw harness::ProviderError(
            options_.backend_name,
            "the driver at " + options_.driver.string() +
                " did not load an embedding model -- it is not the shipped mlx_embed.py: "
                "delete it and run 'apogee check --fix' to restore it");
    }
    if (!options_.adapters.empty() && ready_->adapter.empty()) {
        // A driver that ignored --adapter and loaded the bare model, whose
        // vectors would be filed as the adapter's.
        drop_child();
        throw harness::ProviderError(options_.backend_name, older_copy());
    }
    say(on_status, harness::StatusEvent::Type::ModelReady, harness::StatusEvent::Phase::Done,
        "mlx-embeddings " +
            (ready_->mlx_embeddings_version.empty() ? std::string{"(version unstated)"}
                                                    : ready_->mlx_embeddings_version) +
            ", " + (ready_->model_type.empty() ? std::string{"unknown"} : ready_->model_type) +
            (ready_->adapter.empty()
                 ? std::string{}
                 : ", adapter " + std::filesystem::path{ready_->adapter}.filename().string()));
}

void MlxEmbedProvider::await_ready(const harness::CancellationToken& cancellation) {
    for (;;) {
        bool timed_out = false;
        const std::optional<mlx::Event> event =
            next_event(options_.load_timeout, &cancellation, timed_out);
        if (!event.has_value()) {
            if (cancellation.stop_requested()) {
                drop_child();
                throw harness::CancelledError{};
            }
            const std::string message =
                with_tail(timed_out ? "the MLX embedding driver did not finish loading " +
                                          options_.model_dir.string() + " in time"
                                    : "the MLX embedding driver exited while loading " +
                                          options_.model_dir.string());
            drop_child();
            throw harness::ProviderError(options_.backend_name, message);
        }
        if (event->kind == mlx::Event::Kind::Error) {
            // A missing dependency names its own fix; a load failure names
            // the directory and mlx-embeddings' reason. A usage refusal of an
            // `--adapter` is a driver from before 55e, which takes none.
            const std::string message =
                event->error_kind == "protocol" && !options_.adapters.empty() ? older_copy()
                : event->text.empty() ? with_tail("the MLX embedding driver failed")
                                      : event->text;
            drop_child();
            throw harness::ProviderError(options_.backend_name, message);
        }
        if (event->kind != mlx::Event::Kind::Ready) {
            continue;
        }
        if (event->protocol != mlx::kProtocolVersion) {
            drop_child();
            throw harness::ProviderError(
                options_.backend_name,
                "the MLX embedding driver speaks protocol " + std::to_string(event->protocol) +
                    ", and this build speaks " + std::to_string(mlx::kProtocolVersion) +
                    " -- delete it and run 'apogee check --fix' to restore the shipped one");
        }
        ready_ = *event;
        return;
    }
}

void MlxEmbedProvider::settle(std::int64_t id) {
    // The batch in flight is the driver's to finish -- a forward pass is not
    // stopped midway -- so its answer is read and dropped, and the next call
    // starts on a quiet channel; a driver that will not answer is ended.
    const auto deadline = std::chrono::steady_clock::now() + options_.cancel_timeout;
    for (;;) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (left.count() <= 0) {
            drop_child();
            return;
        }
        bool timed_out = false;
        const std::optional<mlx::Event> event = next_event(left, nullptr, timed_out);
        if (!event.has_value()) {
            drop_child();
            return;
        }
        if ((event->kind == mlx::Event::Kind::Vectors || event->kind == mlx::Event::Kind::Error) &&
            event->id == id) {
            return;
        }
    }
}

std::vector<std::vector<float>> MlxEmbedProvider::embed_slice(
    std::vector<std::string> texts, const harness::CancellationToken& cancellation) {
    const std::size_t count = texts.size();
    const mlx::EmbedRequest request{.id = ++next_id_, .texts = std::move(texts)};
    if (!child_->write_stdin(mlx::embed_line(request) + "\n")) {
        const std::string message = with_tail("the MLX embedding driver closed its input");
        drop_child();
        throw harness::ProviderError(options_.backend_name, message);
    }
    for (;;) {
        bool timed_out = false;
        std::optional<mlx::Event> event =
            next_event(options_.stall_timeout, &cancellation, timed_out);
        if (!event.has_value()) {
            if (cancellation.stop_requested()) {
                settle(request.id);
                throw harness::CancelledError{};
            }
            const std::string message =
                timed_out ? "the MLX embedding driver went silent mid-batch"
                          : with_tail("the MLX embedding driver exited mid-batch");
            drop_child();
            throw harness::ProviderError(options_.backend_name, message);
        }
        if (event->id.has_value() && *event->id != request.id) {
            continue;  // a cancelled batch's late answer
        }
        if (event->kind == mlx::Event::Kind::Error) {
            if (event->error_kind == "request") {
                // A batch it could not embed; the model stays loaded.
                throw harness::ProviderError(
                    options_.backend_name,
                    "the MLX embedding driver could not embed: " + event->text);
            }
            const std::string message =
                event->text.empty() ? with_tail("the MLX embedding driver failed") : event->text;
            drop_child();
            throw harness::ProviderError(options_.backend_name, message);
        }
        if (event->kind != mlx::Event::Kind::Vectors) {
            continue;
        }
        const std::vector<std::vector<float>>& vectors = event->vectors;
        if (vectors.size() != count) {
            throw harness::ProviderError(options_.backend_name,
                                         "the MLX embedding driver returned " +
                                             std::to_string(vectors.size()) + " vector(s) for " +
                                             std::to_string(count) + " text(s)");
        }
        // One space: every vector as wide as every other this provider has
        // handed out -- a store never holds a fraction at another width.
        for (const std::vector<float>& vector : vectors) {
            std::size_t expected = seen_width_.load();
            if (expected == 0) {
                expected = vector.size();
            }
            if (vector.empty() || vector.size() != expected) {
                throw harness::ProviderError(
                    options_.backend_name,
                    "the MLX embedding driver returned a vector " + std::to_string(vector.size()) +
                        " wide among vectors " + std::to_string(expected) + " wide");
            }
            seen_width_.store(expected);
        }
        return std::move(event->vectors);
    }
}

std::vector<std::vector<float>> MlxEmbedProvider::embed(
    const std::vector<std::string>& inputs, const harness::CancellationToken& cancellation) {
    const std::scoped_lock lock{mutex_};
    cancellation.throw_if_cancelled();
    std::vector<std::vector<float>> vectors;
    if (inputs.empty()) {
        return vectors;
    }
    // The idle policy on the way in as on the way out, as llama.cpp's
    // embedder keeps it.
    expire_if_idle();
    ensure_child({}, cancellation);
    vectors.reserve(inputs.size());
    for (const auto& [begin, end] : batch_ranges(inputs.size(), kTextsPerMlxBatch)) {
        cancellation.throw_if_cancelled();
        std::vector<std::vector<float>> got = embed_slice(
            std::vector<std::string>(inputs.begin() + static_cast<std::ptrdiff_t>(begin),
                                     inputs.begin() + static_cast<std::ptrdiff_t>(end)),
            cancellation);
        for (std::vector<float>& vector : got) {
            vectors.push_back(std::move(vector));
        }
        // A use like any other: a long ingest never unloads under itself.
        used_ = true;
        last_use_ = options_.clock();
    }
    return vectors;
}

}  // namespace apogee::backends
