#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "backends/adapters.h"
#include "backends/mlx_protocol.h"
#include "backends/model_profile.h"
#include "backends/sampling.h"
#include "contracts/config.h"
#include "contracts/provider.h"
#include "modelstore/mlx_info.h"
#include "platform/child_process.h"
#include "transport/jsonl_framer.h"

/// Local inference through Apple's MLX (27a): a second local runtime beside
/// in-process llama.cpp, as a **persistent Python child over pipes**.
///
/// MLX's core is C++, but what makes it an LLM runtime -- the model zoo, the
/// tokenizers, each family's chat template and call format -- lives in
/// Python's `mlx-lm`. So the shape is the claude-cli backend's persistent
/// child, running the training track's kind of driver: `mlx_generate.py`,
/// compiled in and seeded under `training/scripts/` like the trainers, run by
/// the interpreter of the environment Apogee owns (`training/venv`, never the
/// system Python), speaking JSONL framed by the one framer. One child per
/// provider holds the model and its attention cache across a conversation's
/// turns; closing its stdin ends it.
///
/// **Opt-in, and loud when it cannot run** (SPEC.md -> Background, the
/// 2026-10-03 amendment): llama.cpp stays the zero-dependency default
/// everywhere, and an `mlx` entry is refused at construction -- off Apple
/// silicon, without the environment, without `mlx-lm`, without a model
/// directory, without the seeded driver -- each with its own message and
/// the exact fix. Never a quiet fallback to another runtime.
///
/// **A child is not a listening socket.** The driver opens no port, and its
/// stderr is a pipe this provider drains into a bounded tail, folded into a
/// failure and dropped otherwise: `mlx-lm`'s warnings never reach the
/// terminal.
namespace apogee::backends {

/// What the refusal ladder reads about the machine, injected so every rung
/// is testable on any host.
struct MlxHost {
    /// This build's release target -- `platform::host_target()`.
    std::string target;
    /// The Python environment Apogee owns: `<home>/training/venv`.
    std::filesystem::path venv;
    /// The seeded driver: `<home>/training/scripts/mlx_generate.py`.
    std::filesystem::path driver;

    /// The running build against `home`.
    [[nodiscard]] static MlxHost at(const std::filesystem::path& home);
    /// The running build against `apogee_home()`.
    [[nodiscard]] static MlxHost current();
};

/// The only target the backend runs on.
inline constexpr std::string_view kMlxTarget = "macos-arm64";

/// The driver's name under `training/scripts/`.
inline constexpr std::string_view kMlxDriverName = "mlx_generate.py";

/// Which rung of the ladder refused, in the order they are asked.
enum class MlxRefusal : std::uint8_t {
    None,
    /// Not Apple silicon macOS.
    Platform,
    /// No interpreter under `training/venv`.
    NoEnvironment,
    /// The environment has no `mlx_lm` package.
    NoMlxLm,
    /// The entry names no `model_path`.
    NoModelPath,
    /// `model_path` names nothing.
    ModelMissing,
    /// `model_path` is not a model directory (no `config.json`).
    NotAModelDirectory,
    /// The directory loads through mlx-vlm alone -- its record says mlx-vlm
    /// built it (53f) -- and the environment has no `mlx_vlm` package.
    NoMlxVlm,
    /// The seeded driver is not there.
    NoDriver,
};

[[nodiscard]] std::string_view to_string(MlxRefusal refusal) noexcept;

/// The ladder's answer: ready, or the rung that refused, why, and the fix.
struct MlxReadiness {
    MlxRefusal refusal = MlxRefusal::None;
    /// Why, in a sentence naming what was looked for and where.
    std::string reason;
    /// The exact command that fixes it, when there is one.
    std::string remedy;
    /// What was found on the way: the interpreter, the `mlx_lm` package, its
    /// version from `_version.py` (empty when that does not say) -- read
    /// from files, never by importing anything.
    std::filesystem::path interpreter;
    std::filesystem::path package;
    std::string version;
    /// The model directory, once the entry's rungs are asked.
    std::filesystem::path model_dir;

    [[nodiscard]] bool ready() const noexcept {
        return refusal == MlxRefusal::None;
    }

    /// The reason with its fix, as one line.
    [[nodiscard]] std::string message() const;
};

/// The runtime's rungs: platform, environment, `mlx-lm`, the driver.
[[nodiscard]] MlxReadiness probe_mlx_runtime(const MlxHost& host);

/// The whole ladder for one entry `name`: the platform, the environment and
/// `mlx-lm`, then its `model_path`, then mlx-vlm for a directory that loads
/// through it alone (53f), then the driver. Construction and `check` both ask
/// this, so they cannot disagree about whether an entry can run.
[[nodiscard]] MlxReadiness probe_mlx_backend(std::string_view name,
                                             const harness::BackendConfig& config,
                                             const MlxHost& host);

/// What a model directory says about itself, read from its files (27a):
/// `config.json`'s `model_type` (a vision-language model's text model's too),
/// whether a chat template ships with it, and the sampling its authors
/// recommend in `generation_config.json` -- the third rung of 26h's ladder,
/// as a GGUF's `general.sampling.*` is for llama.cpp.
struct MlxModelInfo {
    std::string model_type;
    std::string text_model_type;
    bool chat_template = false;
    SamplingRung sampling;
    /// What the directory is (53e's one reader, `read_mlx_kind`): its
    /// record's word, else `vision` where its files say so, else `chat`.
    models::MlxKind kind{};
    /// A vision model: its kind is `vision` (53f) -- recorded by mlx-vlm's
    /// conversion, or, with no record, read from its files (27c): a
    /// `vision_config` and an image processor's configuration
    /// (`preprocessor_config.json` or `processor_config.json`) beside it,
    /// what mlx-vlm needs to read a picture. A record saying it is something
    /// else -- mlx-lm's build of its language model alone -- wins over the
    /// files. Never a fact of the name.
    bool vision = false;
    /// The files carry a vision tower and its processor, whatever the record
    /// says (`models::mlx_vision_files`).
    bool vision_files = false;
    /// Every turn loads through mlx-vlm (53f): its record says mlx-vlm built
    /// it (kind `vision`, recorded), so its weights are laid out the way
    /// mlx-vlm loads them -- whether mlx-lm could load them for a text turn
    /// is not known without real weights. An unrecorded directory with
    /// vision files keeps 27c's way: text through mlx-lm, its cache kept, and
    /// mlx-vlm from the first picture on.
    bool through_vlm = false;
    /// What its `config.json` declares beside its language model (53f's one
    /// reader, `models::snapshot_towers`): `vision_config`, `audio_config`,
    /// an omni model's `thinker_config`. An audio tower is either of the last
    /// two -- the thinker nests the audio encoder beside the vision one.
    std::vector<std::string> towers{};
};

[[nodiscard]] MlxModelInfo inspect_mlx_model(const std::filesystem::path& dir);

/// The fix that installs the vision dependency (27c).
inline constexpr std::string_view kMlxVisionRemedy = "apogee train setup --with mlx-vlm";

/// mlx-vlm as installed in the environment (27c): its package directory
/// and the version its distribution record names -- read from files, never
/// imported, so asking costs a directory listing.
struct MlxVlmPackage {
    std::filesystem::path dir;
    std::string version;

    [[nodiscard]] bool installed() const noexcept {
        return !dir.empty();
    }
};

[[nodiscard]] MlxVlmPackage find_mlx_vlm(const MlxHost& host);

/// Whether an MLX model reads images natively (27c), from file facts alone:
/// the directory is a vision model (`MlxModelInfo::vision`, by its kind
/// since 53f) and mlx-vlm is installed in the environment. The provider's `accepts_images`, `check`
/// and `models info/status` all ask this, so no surface claims a capability
/// another denies.
struct MlxVision {
    /// The directory is a vision model.
    bool model = false;
    MlxVlmPackage vlm;
    /// Why it does not read images, in a sentence; and the fix, when one
    /// command is it (mlx-vlm missing). Both empty when it does.
    std::string reason;
    std::string remedy;

    [[nodiscard]] bool reads_images() const noexcept {
        return model && vlm.installed();
    }
};

[[nodiscard]] MlxVision probe_mlx_vision(const MlxModelInfo& info, const MlxHost& host);

/// Whether the installed mlx-vlm has a module for `model_type` (53g): the type
/// as mlx-vlm's own loader resolves it -- through `utils.py`'s
/// `MODEL_REMAPPING` when the table names it -- a package under its `models/`.
/// Read from files, never imported. (The training track's `mlx_model_types`
/// lists every type a converter builds; this layer cannot reach it, and asks
/// the one type's question the loader asks.)
[[nodiscard]] bool mlx_vlm_has_module(const MlxVlmPackage& vlm, std::string_view model_type);

/// Which rung of the audio ladder refused (53g), in the order they are asked.
enum class MlxAudioRefusal : std::uint8_t {
    None,
    /// Not a directory mlx-vlm built (its record does not say `vision`):
    /// mlx-lm's build, and one with no record, keep no audio tower.
    NotMlxVlmBuild,
    /// Its `config.json` declares no audio tower.
    NoAudioTower,
    /// mlx-vlm is not installed in the environment.
    NoMlxVlm,
    /// The installed mlx-vlm has no module for its type.
    NoTypeModule,
};

[[nodiscard]] std::string_view to_string(MlxAudioRefusal refusal) noexcept;

/// Whether an MLX model hears a recording natively (53g), from file facts
/// alone: a directory mlx-vlm built (recorded `vision`, 53f), whose
/// `config.json` declares an audio tower, with mlx-vlm installed and holding
/// its type's module. The provider's `accepts_audio` asks this; anything
/// else has no native audio, and a recording reaches it as the
/// transcription role's text (26e) -- or, sent to it anyway, is refused
/// before anything starts, the rung said.
struct MlxAudio {
    MlxAudioRefusal refusal = MlxAudioRefusal::None;
    MlxVlmPackage vlm{};
    /// Why it does not hear, in a sentence; and the fix, when one command is
    /// it. Both empty when it does.
    std::string reason{};
    std::string remedy{};

    [[nodiscard]] bool hears() const noexcept {
        return refusal == MlxAudioRefusal::None;
    }

    /// The rung, the reason and its fix, as one line: "no audio tower: its
    /// config.json declares none ...". Empty when it hears.
    [[nodiscard]] std::string message() const;
};

[[nodiscard]] MlxAudio probe_mlx_audio(const MlxModelInfo& info, const MlxHost& host);

/// The family profile for a model directory: its `model_type` (and a
/// vision-language model's text model's) in each spelling the profiles list
/// architectures under -- `gpt_oss` is the GGUF's `gpt-oss` -- and then the
/// names in `name_hint`, the ladder's lower rung. Null when nothing matches.
[[nodiscard]] const ModelProfile* resolve_mlx_profile(const MlxModelInfo& info,
                                                      std::string_view name_hint);

class MlxLocalProvider final : public harness::LLMProvider,
                               public harness::ModelBehaviorReporting,
                               public harness::StatusReporting,
                               public harness::ContextWindowReporting,
                               public harness::VisionCapable,
                               public harness::AudioCapable,
                               public harness::ResidencyHolding {
public:
    /// Starts the driver. Injected so the provider is tested over a scripted
    /// child: a driver that dies mid-turn, a stream cut between any two bytes,
    /// a load that fails -- none of which a real model does on demand.
    using Spawner = std::function<std::unique_ptr<platform::ChildProcess>(
        const platform::ChildCommand&, std::string&)>;
    using Clock = std::function<std::chrono::steady_clock::time_point()>;

    struct Options {
        std::string backend_name = "mlx";
        /// Display name; the model directory's name when the entry sets none.
        std::string model;
        std::filesystem::path model_dir;
        std::filesystem::path interpreter;
        std::filesystem::path driver;
        /// What the model directory says (`inspect_mlx_model`).
        MlxModelInfo info;
        /// Whether it reads images natively, and why not (`probe_mlx_vision`):
        /// a turn carrying a picture is answered by a driver loaded through
        /// mlx-vlm (27c).
        bool vision = false;
        std::string vision_gap;
        /// Whether it hears a recording natively, and why not
        /// (`probe_mlx_audio`'s rung and reason): a turn carrying one is
        /// answered by a driver loaded through mlx-vlm, each recording handed
        /// to it as a WAV file of the turn's own (53g).
        bool audio = false;
        std::string audio_gap{};
        /// Cap on generated tokens when the request sets none.
        std::int64_t max_tokens = 2048;
        /// The window, when the entry sets one; 0 leaves it to the model.
        std::int64_t context_size = 0;
        /// What `config.json` says the model was trained for (27b), 0 when it
        /// declares nothing; and whether it could be read at all.
        std::int64_t trained_window = 0;
        bool config_read = false;
        /// The config's rung of the sampling ladder, and its seed (26h).
        SamplingRung sampling;
        std::optional<std::uint32_t> seed;
        /// The adapter the driver loads over the model (53a): at most one --
        /// mlx-lm's `adapter_path`, a directory its training wrote -- with
        /// the pin to the base it was registered against. Empty, the
        /// default, starts the driver exactly as before.
        std::vector<harness::AdapterConfig> adapters{};
        /// Where the model directory's hash, which each start checks the pin
        /// against, comes from: `cache/weight-hashes.json` from `options_from`.
        WeightHashing weight_hashing{};
        /// Ends the child after this long with no request; zero never does.
        std::chrono::seconds idle_unload{0};
        /// How long a load may take before the driver is given up on: a
        /// 20B model off a cold disk is tens of seconds.
        std::chrono::milliseconds load_timeout{std::chrono::minutes{10}};
        /// How long a request may go without a single event.
        std::chrono::milliseconds stall_timeout{std::chrono::minutes{10}};
        /// How long a cancelled request may take to say it is done before
        /// the child is ended instead.
        std::chrono::milliseconds cancel_timeout{std::chrono::seconds{10}};
        /// How long the driver gets to exit once its stdin closes.
        std::chrono::milliseconds shutdown_timeout{std::chrono::seconds{10}};
        Clock clock;
    };

    MlxLocalProvider(Options options, Spawner spawner);

    // A live child is owned here: copying or moving would duplicate or orphan it.
    MlxLocalProvider(const MlxLocalProvider&) = delete;
    MlxLocalProvider& operator=(const MlxLocalProvider&) = delete;
    MlxLocalProvider(MlxLocalProvider&&) = delete;
    MlxLocalProvider& operator=(MlxLocalProvider&&) = delete;

    ~MlxLocalProvider() override;

    /// Builds one over the real process seam, after the whole refusal
    /// ladder. Throws harness::ProviderError naming the rung and its fix.
    [[nodiscard]] static std::unique_ptr<MlxLocalProvider> from_config(
        const std::string& backend_name, const harness::BackendConfig& config,
        const MlxHost& host = MlxHost::current());

    /// What an entry configures, as `from_config` builds it, without the
    /// ladder -- testable on any host.
    [[nodiscard]] static Options options_from(const std::string& backend_name,
                                              const harness::BackendConfig& config,
                                              const MlxHost& host);

    [[nodiscard]] std::string_view backend_name() const noexcept override;

    /// Local weights cost nothing per call.
    [[nodiscard]] bool generation_is_metered() const noexcept override {
        return false;
    }

    [[nodiscard]] harness::ChatResponse chat(
        const harness::ChatRequest& request,
        const harness::CancellationToken& cancellation) override;

    [[nodiscard]] harness::ChatResponse stream_chat(const harness::ChatRequest& request,
                                                    const harness::StreamOptions& options) override;

    [[nodiscard]] std::vector<harness::ModelInfo> list_models(
        const harness::CancellationToken& cancellation) override;

    /// What `request` samples with, each value with its rung (26h).
    [[nodiscard]] ResolvedSampling sampling_for(const harness::ChatRequest& request) const;

    // --- ModelBehaviorReporting ---------------------------------------------

    /// The family's profile, resolved from `config.json`'s `model_type` and
    /// then the names; a directory with no chat template is a base model.
    [[nodiscard]] harness::ModelBehavior model_behavior() const override;

    // --- StatusReporting ----------------------------------------------------

    [[nodiscard]] harness::StatusEvent model_status() const override;

    /// Starts the driver and waits for its model to load.
    void preload(const harness::StatusSink& on_status) override;

    /// Every load from now on is said to `listener` too (27e).
    void set_load_listener(const harness::StatusSink& listener) override;

    // --- ResidencyHolding (27e) ----------------------------------------------

    /// While held, `idle_unload` never ends the driver; let go, the clock
    /// rules again from the last use.
    void hold_resident(bool held) noexcept override;
    [[nodiscard]] bool held_resident() const noexcept override;

    // --- ContextWindowReporting ---------------------------------------------

    /// `context_size` when the entry sets one, else 26a's default over the
    /// window `config.json` declares (27b): 32,768, or the trained window
    /// when smaller -- the window a GGUF chat of the model would get, so
    /// warnings and compaction fire at the same place. 0 when `config.json`
    /// cannot be read.
    [[nodiscard]] std::int64_t context_window() const override;

    // --- VisionCapable --------------------------------------------------------

    /// True for a vision model with mlx-vlm installed (27c), from the files
    /// alone -- asked before a turn, never at the cost of a load. Otherwise
    /// false, so every surface routes the image through the vision role with
    /// the one shared message instead of sending one this model cannot read.
    [[nodiscard]] bool accepts_images() const noexcept override {
        return options_.vision;
    }

    // --- AudioCapable (53g) ---------------------------------------------------

    /// True for an omni directory mlx-vlm built, with mlx-vlm and its type's
    /// module installed (`probe_mlx_audio`), from the files alone -- asked
    /// before a turn, never at the cost of a load. Otherwise false, so every
    /// surface has a recording transcribed by the transcription role instead.
    /// The rate is left at the contract's default: a recording is decoded to
    /// 16 kHz, and mlx-vlm converts it to its feature extractor's own.
    [[nodiscard]] bool accepts_audio() const noexcept override {
        return options_.audio;
    }

    /// How many children this provider has started: "one per session" is a
    /// claim only if it can be counted.
    [[nodiscard]] int spawn_count() const noexcept;

    /// Whether a driver is running now.
    [[nodiscard]] bool has_live_child() const noexcept;

    /// Whether the running driver loaded the model through mlx-vlm (27c).
    [[nodiscard]] bool has_vision_child() const noexcept;

    /// What the driver said when it was ready, once it has.
    [[nodiscard]] const std::optional<mlx::Event>& driver_ready() const noexcept {
        return ready_;
    }

    /// Ends the driver: stdin closed, a bounded wait, then terminated.
    void end_session();

private:
    /// Starts the driver if none is running and waits for `ready`. Throws
    /// ProviderError with the driver's own words when it cannot. `vision`
    /// asks for one loaded through mlx-vlm: a running text driver is ended
    /// and the model loaded again that way (27c); a running vision driver
    /// answers text turns too, so it is never swapped back. A directory
    /// mlx-vlm built (`MlxModelInfo::through_vlm`, 53f) is always asked for
    /// one, its first turn and every turn after.
    void ensure_child(const harness::StreamOptions& options, bool vision = false);

    /// Refuses a request carrying what this model cannot read: an image
    /// when it reads none, one that is not a data: URI, a recording when it
    /// hears none -- the rung said -- or one that is not a WAV.
    void check_media(const harness::ChatRequest& request) const;

    /// Reads the driver's startup: `ready`, or the error that ends it.
    void await_ready(const harness::CancellationToken& cancellation);

    /// The `generate` a request becomes, with the notices it is owed.
    [[nodiscard]] mlx::GenerateRequest make_request(const harness::ChatRequest& request,
                                                    const harness::StreamOptions& options);

    /// Ends a turn the driver stopped talking in: cancelled, silent, or dead.
    [[noreturn]] void fail_without_event(std::int64_t id, bool timed_out,
                                         const harness::CancellationToken& cancellation);

    /// Ends a turn on the driver's `error`: a request it could not answer
    /// leaves the child; anything else ends it.
    [[noreturn]] void fail_with(const mlx::Event& error);

    /// The cache line for a session turn (`--verbose`).
    void report_cache(const mlx::Event& done, const harness::StreamOptions& options) const;

    /// A load status, named for the model.
    void say(const harness::StreamOptions& options, harness::StatusEvent::Type type,
             harness::StatusEvent::Phase phase, std::string detail) const;

    /// Reads the next event, draining stderr on the way. Nullopt on EOF.
    [[nodiscard]] std::optional<mlx::Event> next_event(std::chrono::milliseconds stall,
                                                       const harness::CancellationToken* cancel,
                                                       bool& timed_out);

    /// Keeps the last of what the driver wrote to stderr.
    void drain_stderr();

    /// The driver died or failed: the message, with its stderr's last line.
    [[nodiscard]] std::string with_tail(const std::string& message) const;

    /// Forgets the child (it is gone or being replaced).
    void drop_child();

    void expire_if_idle();

    [[nodiscard]] const ModelProfile* profile() const;

    [[nodiscard]] harness::ChatResponse run(const harness::ChatRequest& request,
                                            const harness::StreamOptions& options);

    /// Asks the driver to stop request `id` and waits for its `done`; ends
    /// the child when it does not come.
    void cancel_request(std::int64_t id);

    Options options_;
    Spawner spawner_;
    /// One request at a time: the child is one conversation, and a served
    /// deployment may ask from several threads.
    std::mutex mutex_;
    std::unique_ptr<platform::ChildProcess> child_;
    JsonlFramer framer_;
    std::deque<std::string> lines_;
    std::optional<mlx::Event> ready_;
    /// The running driver was started with `--vision`.
    bool vision_child_ = false;
    std::string stderr_tail_;
    std::int64_t next_id_ = 0;
    int spawns_ = 0;
    mutable const ModelProfile* profile_ = nullptr;
    mutable bool profile_resolved_ = false;
    bool schema_noticed_ = false;
    bool budget_noticed_ = false;
    std::chrono::steady_clock::time_point last_use_;
    bool used_ = false;
    /// A session's in-use hold (27e).
    std::atomic<bool> held_{false};
    /// Hears every load (27e); set before first use.
    harness::StatusSink load_listener_;
};

}  // namespace apogee::backends
