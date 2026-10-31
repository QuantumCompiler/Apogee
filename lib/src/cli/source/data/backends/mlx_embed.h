#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "backends/adapters.h"
#include "backends/mlx_local.h"
#include "backends/mlx_protocol.h"
#include "contracts/config.h"
#include "contracts/provider.h"
#include "platform/child_process.h"
#include "transport/jsonl_framer.h"

/// An `mlx` entry over an embedding model (53h): vectors through
/// mlx-embeddings, as a persistent Python child over pipes -- the chat
/// provider's shape (27a), a different driver.
///
/// The directory is one `models convert <set> --mlx` built through
/// `mlx_embeddings.convert` and recorded as kind `embedding` (53e's reader).
/// One child per provider -- the seeded `mlx_embed.py` under `training/venv`'s
/// interpreter -- holds the model and answers batches of texts with their
/// pooled, normalized vectors, framed by the one JSONL framer. Its stderr is
/// drained into a bounded tail folded into a failure, never inherited.
///
/// **Embeds, and nothing else.** `EmbeddingCapable` is the capability the
/// Harness discovers (never a cast at a call site), so the embedding role,
/// retrieval and the vector store ask it like any embedder. It answers no
/// chat: a model mlx-embeddings built pools a sentence into one vector and
/// generates nothing, so a chat or completion sent to it is refused before
/// any child starts, saying what it is and what uses it (2026-10-21, the
/// conservative call: never a silent fallback to another backend).
///
/// **Its adapter applies** (55e, 53a's contract: "chat and embedding
/// alike"): the entry's one adapter -- an mlx-lm adapter directory, pinned to
/// the model directory's weights -- is checked before the driver starts and
/// handed to it as mlx-embeddings' own `adapter_path`, and a driver that
/// does not name it back in its `ready` is refused, never left to embed
/// through the bare model while the config says otherwise.
///
/// **Its vectors are its own space** (one retriever, resolved once): it names
/// them `mlx:<model>`, so a collection built through it is bound to this
/// backend and a GGUF build of the same set -- whose llamacpp embedder names
/// its own -- is another model to the store, demoting the collection to
/// lexical rather than scoring across the two. Its adapter is part of the
/// space (`embedding_space`, 55e).
namespace apogee::backends {

/// Texts per `embed` request: the in-process local embedder's slice
/// (`kTextsPerLlamaBatch`), so a cancellation lands between two as promptly
/// and the driver's padded batch stays as small.
inline constexpr std::size_t kTextsPerMlxBatch = 64;

/// Whether `config`, an `mlx` entry, names an embedding model's directory:
/// its record's kind is `embedding` (53e's one reader). What the provider
/// factory builds this provider for, rather than the chat one.
[[nodiscard]] bool mlx_entry_embeds(const harness::BackendConfig& config);

class MlxEmbedProvider final : public harness::LLMProvider,
                               public harness::EmbeddingCapable,
                               public harness::StatusReporting,
                               public harness::VisionCapable,
                               public harness::ResidencyHolding {
public:
    /// Starts the driver; injected so the provider is tested over a scripted
    /// child, as the chat provider is.
    using Spawner = MlxLocalProvider::Spawner;
    using Clock = MlxLocalProvider::Clock;

    struct Options {
        std::string backend_name = "mlx";
        /// Display name: the entry's `model`, else its `model_path`.
        std::string model{};
        std::filesystem::path model_dir{};
        std::filesystem::path interpreter{};
        /// The seeded embedding driver.
        std::filesystem::path driver{};
        /// How wide its vectors are, as the directory's own config says
        /// (`models::read_mlx_embedding_width`); 0 when it says nothing.
        std::size_t width = 0;
        /// The adapter the driver applies over the model (55e): at most one
        /// -- mlx-embeddings' `adapter_path`, the directory mlx-lm's training
        /// wrote -- with the pin to the weights it was registered against.
        /// Empty, the default, starts the driver exactly as before.
        std::vector<harness::AdapterConfig> adapters{};
        /// Where the model directory's hash, which each start checks the pin
        /// against, comes from: `cache/weight-hashes.json` from `options_from`.
        WeightHashing weight_hashing{};
        /// Ends the child after this long with no request; zero never does.
        std::chrono::seconds idle_unload{0};
        /// How long a load may take before the driver is given up on.
        std::chrono::milliseconds load_timeout{std::chrono::minutes{10}};
        /// How long a batch may go without its answer.
        std::chrono::milliseconds stall_timeout{std::chrono::minutes{10}};
        /// How long a cancelled call waits for the batch in flight to be
        /// answered -- and dropped -- before the child is ended instead.
        std::chrono::milliseconds cancel_timeout{std::chrono::seconds{10}};
        /// How long the driver gets to exit once its stdin closes.
        std::chrono::milliseconds shutdown_timeout{std::chrono::seconds{10}};
        Clock clock{};
    };

    MlxEmbedProvider(Options options, Spawner spawner);

    // A live child is owned here: copying or moving would duplicate or orphan it.
    MlxEmbedProvider(const MlxEmbedProvider&) = delete;
    MlxEmbedProvider& operator=(const MlxEmbedProvider&) = delete;
    MlxEmbedProvider(MlxEmbedProvider&&) = delete;
    MlxEmbedProvider& operator=(MlxEmbedProvider&&) = delete;

    ~MlxEmbedProvider() override;

    /// Builds one over the real process seam, after the mlx ladder
    /// (`probe_mlx_backend`, which asks an embedding directory for
    /// mlx-embeddings and the embedding driver). Throws harness::ProviderError
    /// naming the rung and its fix.
    [[nodiscard]] static std::unique_ptr<MlxEmbedProvider> from_config(
        const std::string& backend_name, const harness::BackendConfig& config,
        const MlxHost& host = MlxHost::current());

    /// What an entry configures, without the ladder -- testable on any host.
    [[nodiscard]] static Options options_from(const std::string& backend_name,
                                              const harness::BackendConfig& config,
                                              const MlxHost& host);

    [[nodiscard]] std::string_view backend_name() const noexcept override;

    /// Local weights cost nothing per call -- and it generates nothing.
    [[nodiscard]] bool generation_is_metered() const noexcept override {
        return false;
    }

    /// Refused, before anything starts: an embedding model answers no chat.
    [[nodiscard]] harness::ChatResponse chat(
        const harness::ChatRequest& request,
        const harness::CancellationToken& cancellation) override;

    /// Refused the same way.
    [[nodiscard]] harness::ChatResponse stream_chat(const harness::ChatRequest& request,
                                                    const harness::StreamOptions& options) override;

    [[nodiscard]] std::vector<harness::ModelInfo> list_models(
        const harness::CancellationToken& cancellation) override;

    /// Why a chat or completion is refused, in the one wording.
    [[nodiscard]] std::string chat_refusal() const;

    // --- EmbeddingCapable ---------------------------------------------------

    /// One vector per input, in order: the inputs in slices of
    /// `kTextsPerMlxBatch`, one `embed` request each, the token asked between
    /// two. Throws ProviderError with the driver's words, CancelledError when
    /// the token fires.
    [[nodiscard]] std::vector<std::vector<float>> embed(
        const std::vector<std::string>& inputs,
        const harness::CancellationToken& cancellation) override;

    /// The width its vectors have had, once one call has answered; before
    /// that, the width the directory's config states (0 when it states none).
    [[nodiscard]] std::size_t embedding_dimensions() const noexcept override;

    /// `mlx:<model>`: keyed by the backend, so a GGUF build of the same set
    /// is never taken for the same vector space -- and with its adapter,
    /// when it applies one (55e, `embedding_space`).
    [[nodiscard]] std::string embedding_model_name() const override;

    /// Local weights: unmetered, so a collection may be vectorised through it
    /// on Apogee's initiative, as llama.cpp's embedder may.
    [[nodiscard]] bool embedding_is_metered() const noexcept override {
        return false;
    }

    // --- StatusReporting ----------------------------------------------------

    [[nodiscard]] harness::StatusEvent model_status() const override;

    /// Starts the driver and waits for its model to load.
    void preload(const harness::StatusSink& on_status) override;

    /// Every load from now on is said to `listener` too (27e).
    void set_load_listener(const harness::StatusSink& listener) override;

    // --- VisionCapable --------------------------------------------------------

    /// Never: it reads no images (an undeclared provider is assumed to).
    [[nodiscard]] bool accepts_images() const noexcept override {
        return false;
    }

    // --- ResidencyHolding (27e) ----------------------------------------------

    void hold_resident(bool held) noexcept override;
    [[nodiscard]] bool held_resident() const noexcept override;

    /// How many children this provider has started.
    [[nodiscard]] int spawn_count() const noexcept;

    /// Whether a driver is running now.
    [[nodiscard]] bool has_live_child() const noexcept;

    /// What the driver said when it was ready, once it has.
    [[nodiscard]] const std::optional<mlx::Event>& driver_ready() const noexcept {
        return ready_;
    }

    /// Ends the driver: stdin closed, a bounded wait, then terminated.
    void end_session();

private:
    /// Starts the driver if none is running and waits for `ready`.
    void ensure_child(const harness::StatusSink& on_status,
                      const harness::CancellationToken& cancellation);

    void await_ready(const harness::CancellationToken& cancellation);

    /// One slice's vectors: its request written, its answer read.
    [[nodiscard]] std::vector<std::vector<float>> embed_slice(
        std::vector<std::string> texts, const harness::CancellationToken& cancellation);

    /// After a cancel: the batch in flight answered and dropped, or the child
    /// ended when it will not answer in time.
    void settle(std::int64_t id);

    [[nodiscard]] std::optional<mlx::Event> next_event(std::chrono::milliseconds stall,
                                                       const harness::CancellationToken* cancel,
                                                       bool& timed_out);
    void drain_stderr();
    [[nodiscard]] std::string with_tail(const std::string& message) const;
    /// Why a driver that applies no adapter is refused one (55e): it is an
    /// earlier Apogee's copy, which `check --fix` brings up to this build's.
    [[nodiscard]] std::string older_copy() const;
    void drop_child();
    void expire_if_idle();
    void say(const harness::StatusSink& on_status, harness::StatusEvent::Type type,
             harness::StatusEvent::Phase phase, std::string detail) const;

    Options options_;
    Spawner spawner_;
    /// One batch at a time: the child is one model, and a served deployment
    /// may ask from several threads.
    std::mutex mutex_;
    std::unique_ptr<platform::ChildProcess> child_;
    JsonlFramer framer_;
    std::deque<std::string> lines_;
    std::optional<mlx::Event> ready_;
    std::string stderr_tail_;
    std::int64_t next_id_ = 0;
    int spawns_ = 0;
    /// The width an answer has had; 0 until the first.
    std::atomic<std::size_t> seen_width_{0};
    std::chrono::steady_clock::time_point last_use_{};
    bool used_ = false;
    std::atomic<bool> held_{false};
    harness::StatusSink load_listener_{};
};

}  // namespace apogee::backends
