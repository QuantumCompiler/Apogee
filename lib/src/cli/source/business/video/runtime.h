#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "modelstore/store.h"

/// The video runtime (49b): diffusers behind the house's own driver
/// discipline.
///
/// Generating video locally is a diffusion pipeline run in Python -- torch
/// and diffusers -- which a C++ harness never links. As the mlx backend
/// reaches mlx-lm (27a), this reaches diffusers through a seeded driver
/// (`video_generate.py`) run under the Python environment Apogee owns, over
/// the training track's one script runner and its JSONL protocol; the
/// packages are a requirement set of that environment (`video`).
///
/// **Refused before any spawn.** Each fact a generation needs -- the
/// environment, torch, diffusers, the pipeline, the driver, ffmpeg -- is a
/// rung of one ladder, read from files and never by importing anything, and
/// a missing one refuses with its fix named (27a's shape: the reason, then
/// " -- ", then the one command that fixes it).
///
/// **Two engines, one generation** (53l). A diffusers pipeline runs on
/// PyTorch through `video_generate.py`; an MLX video model -- mlx-video's
/// build of Wan's original layout (53k) -- runs on MLX through
/// `video_generate_mlx.py`, over mlx-video's own Wan generator. The model's
/// directory says which (`engine_of_directory`); each engine has its ladder;
/// the request, the progress protocol, the working directory and the record
/// are the one generation's (`generation.h`).
///
/// **Two engines, one speech** (53m), the same way: a voice model as Kokoro
/// publishes it speaks on PyTorch through `voice_say.py`; mlx-audio's build
/// of one -- an MLX directory whose kind is `voice` -- on MLX through
/// `voice_say_mlx.py`, over mlx-audio's own Kokoro (`voice.h`).
namespace apogee::video {

/// What a clip runs on (53l): PyTorch (diffusers) or MLX (mlx-video).
enum class VideoEngine : std::uint8_t { Torch, Mlx };

/// `torch`, `mlx`: the word the record and `--engine` use.
[[nodiscard]] std::string_view to_string(VideoEngine engine) noexcept;

/// `torch` or `mlx` read back; nullopt for anything else -- `auto` included,
/// which names no engine of its own.
[[nodiscard]] std::optional<VideoEngine> video_engine_from_string(std::string_view text) noexcept;

/// `--engine`'s words, `auto` first: what the flag takes and completes.
[[nodiscard]] std::span<const std::string_view> video_engine_names() noexcept;

/// What the model at `dir` runs on, from its files (53l): a diffusers
/// pipeline (`model_index.json` at its root) on PyTorch, an MLX directory
/// whose kind is `video` (53e's reader, `read_mlx_kind`) on MLX; nullopt for
/// anything else. The model says its engine: a person picks the model.
[[nodiscard]] std::optional<VideoEngine> engine_of_directory(const std::filesystem::path& dir);

/// The engine a run takes (53l): the model's, `directory`. `asked` -- an
/// `--engine`, or the engine a record ran on -- only names it or refuses it:
/// empty or `auto` takes the model's, the model's own is it, and the other
/// engine is refused, saying what `name` is, what runs it, and how a model
/// for the asked engine is had; any other word is refused naming the words.
/// Empty means `out` is set.
[[nodiscard]] std::string choose_video_engine(std::string_view asked, VideoEngine directory,
                                              std::string_view name, VideoEngine& out);

/// The first rung a generation cannot pass, in the order they are asked.
enum class VideoRefusal : std::uint8_t {
    None,
    /// No Python environment Apogee owns.
    NoEnvironment,
    /// torch, or diffusers, is not installed in it.
    NoTorch,
    NoDiffusers,
    /// Nothing at the pipeline's directory, or it cannot load.
    NoPipeline,
    NotAPipeline,
    /// The seeded driver is missing.
    NoDriver,
    /// No `ffmpeg` on PATH to assemble the frames with.
    NoFfmpeg,
    /// The voice runtime's own rungs (50a): kokoro not installed, no voice
    /// model at the directory, or one that cannot load.
    NoKokoro,
    NoVoiceModel,
    NotAVoiceModel,
    /// The MLX engine's own rungs (53l): this host is not Apple silicon,
    /// where MLX runs; mlx-video's Wan generator (with the torch its set
    /// brings) is not installed; the T5 tokenizer the generator reads is not
    /// in the store. `NoPipeline` and `NotAPipeline` say the MLX video
    /// model's directory, as they say a pipeline's.
    NotAppleSilicon,
    NoMlxVideo,
    NoTokenizer,
    /// The MLX speech engine's own rung (53m): mlx-audio's Kokoro, with the
    /// misaki it reads text through, is not installed. `NoVoiceModel` and
    /// `NotAVoiceModel` say the MLX voice model's directory, as they say a
    /// voice model's.
    NoMlxAudio,
};

[[nodiscard]] std::string_view to_string(VideoRefusal refusal) noexcept;

/// Where this install keeps what a generation runs on.
struct VideoHost {
    /// `<home>/training/venv`, the environment Apogee owns.
    std::filesystem::path venv;
    /// `<home>/training/scripts/video_generate.py`, the seeded driver.
    std::filesystem::path driver;
    /// `<home>/training/scripts/voice_say.py`, the speech driver (50a).
    std::filesystem::path voice_driver;
    /// `ffmpeg` on PATH, or empty.
    std::string ffmpeg;
    /// `<home>/training/scripts/video_generate_mlx.py`, the MLX engine's
    /// driver (53l).
    std::filesystem::path mlx_driver{};
    /// This build's release target (`platform::host_target()`): MLX runs on
    /// `macos-arm64` alone.
    std::string target{};
    /// The model store, where an MLX video model's source set -- and the
    /// tokenizer it stores -- is found: `<home>/models` for both roots; a
    /// command whose config moves SafeTensors sets (`paths.hf_dir`) sets its
    /// own.
    models::StoreRoots roots{};
    /// `<home>/training/scripts/voice_say_mlx.py`, the MLX engine's speech
    /// driver (53m).
    std::filesystem::path voice_mlx_driver{};

    [[nodiscard]] static VideoHost at(const std::filesystem::path& home);
    /// This install's: `apogee_home()`, and the PATH this process has.
    [[nodiscard]] static VideoHost current();
};

/// What the ladder found: ready, or the first rung refused.
struct VideoReadiness {
    VideoRefusal refusal = VideoRefusal::None;
    std::string reason{};
    std::string remedy{};
    /// The environment's interpreter and the driver, once found.
    std::filesystem::path interpreter{};
    std::filesystem::path driver{};
    std::string ffmpeg{};
    /// The installed versions, read from their distribution records.
    std::string torch_version{};
    std::string diffusers_version{};
    /// The speech runtime's version, for a voice probe (50a).
    std::string kokoro_version{};
    /// Whether peft -- what diffusers loads a LoRA through (49e) -- is
    /// installed, and its version when its record says one. Not a rung:
    /// only a run asked for a LoRA needs it.
    bool peft = false;
    std::string peft_version{};
    /// The engine the ladder was asked for (53l), and on MLX what it found:
    /// the T5 tokenizer the generator is pointed at, and mlx-video's and
    /// mlx's versions from their distribution records.
    VideoEngine engine = VideoEngine::Torch;
    std::filesystem::path tokenizer{};
    std::string mlx_video_version{};
    std::string mlx_version{};
    /// mlx-audio's version, for an MLX voice probe (53m).
    std::string mlx_audio_version{};

    [[nodiscard]] bool ready() const noexcept {
        return refusal == VideoRefusal::None;
    }

    /// `reason -- remedy`, or the reason alone.
    [[nodiscard]] std::string message() const;
};

/// The PyTorch engine's ladder, in order: the environment, torch,
/// diffusers, then -- when `pipeline` is given -- the pipeline read whole
/// (`read_pipeline_info`, `handle` naming it in the fix), then the driver and
/// ffmpeg.
[[nodiscard]] VideoReadiness probe_video(const VideoHost& host,
                                         const std::filesystem::path& pipeline = {},
                                         std::string_view handle = {});

/// The fix every package rung names.
inline constexpr std::string_view kVideoSetupRemedy = "apogee train setup --with video";

/// The LoRA file `given` names (49e), from file facts alone -- Apogee never
/// reads a LoRA's weights: a `.safetensors` file, or a directory holding
/// exactly one. The refusal names the path and what it is instead; empty
/// means `out` is set.
[[nodiscard]] std::string lora_file(const std::filesystem::path& given, std::filesystem::path& out);

/// The voice ladder (50a), the video one's environment rungs shared: the
/// environment, torch, kokoro, then -- when `model` is given -- the voice
/// model read whole (`read_voice_info`, `handle` naming it in the fix),
/// then the speech driver. No ffmpeg: speech is written as WAV.
[[nodiscard]] VideoReadiness probe_voice(const VideoHost& host,
                                         const std::filesystem::path& model = {},
                                         std::string_view handle = {});

/// The fix the voice package rungs name.
inline constexpr std::string_view kVoiceSetupRemedy = "apogee train setup --with voice";

/// The fix the MLX engine's package rungs name (53l): mlx-video is opt-in
/// (53k), installed on the person's word alone, never asked about here.
inline constexpr std::string_view kMlxVideoSetupRemedy = "apogee train setup --with mlx-video";

/// The target MLX runs on.
inline constexpr std::string_view kVideoMlxTarget = "macos-arm64";

/// The tokenizer mlx-video's Wan generator asks the Hugging Face hub for
/// (`AutoTokenizer.from_pretrained("google/umt5-xxl")` at the pinned
/// commit) -- and where Wan's original layout stores that same tokenizer,
/// under its own name (53j): `google/umt5-xxl/`.
inline constexpr std::string_view kMlxVideoTokenizer = "google/umt5-xxl";

/// The T5 tokenizer for the MLX video model at `dir` (53l), never the
/// hub's: the `google/umt5-xxl/` of the Wan set it was converted from --
/// the set its record's `ref` names, a store handle found under `roots`, or
/// a path -- holding `tokenizer_config.json` and `tokenizer.json` or
/// `spiece.model`, and every file the set's own record lists under it.
/// Empty, with `reason` and `remedy` naming what is missing, when it is not
/// there.
[[nodiscard]] std::filesystem::path find_mlx_video_tokenizer(const models::StoreRoots& roots,
                                                             const std::filesystem::path& dir,
                                                             std::string& reason,
                                                             std::string& remedy);

/// The mode an MLX video model's `config.json` declares -- `model_type`, as
/// mlx-video's generator reads it: `t2v`, `i2v` or `ti2v` -- or empty.
[[nodiscard]] std::string mlx_video_mode(const std::filesystem::path& dir);

/// Whether `frames` is a count mlx-video's Wan generator takes (53l): `4n+1`,
/// the VAE compressing time four frames to one past the first.
[[nodiscard]] bool mlx_video_frames(int frames) noexcept;

/// The MLX engine's ladder (53l), in order: Apple silicon, the environment,
/// mlx-video's Wan generator with the torch its set brings
/// (`find_mlx_video_generator`), then -- when `dir` is given -- the MLX video
/// model read whole by 53k's rule (`read_mlx_info`, its kind `video`) and
/// the T5 tokenizer its source stores (`find_mlx_video_tokenizer`), then the
/// MLX driver and ffmpeg. No consent is asked: mlx-video is opt-in, and every
/// package rung names `kMlxVideoSetupRemedy`.
[[nodiscard]] VideoReadiness probe_video_mlx(const VideoHost& host,
                                             const std::filesystem::path& dir = {},
                                             std::string_view handle = {});

/// The ladder for `engine`: `probe_video` for PyTorch, `probe_video_mlx` for
/// MLX -- the one question every caller asks of a model it runs.
[[nodiscard]] VideoReadiness probe_video_engine(const VideoHost& host, VideoEngine engine,
                                                const std::filesystem::path& dir = {},
                                                std::string_view handle = {});

/// The fix the MLX speech engine's package rungs name (53m): mlx-audio is
/// opt-in, installed on the person's word alone, never asked about here.
inline constexpr std::string_view kMlxAudioSetupRemedy = "apogee train setup --with mlx-audio";

/// The MLX speech engine's ladder (53m), in order: Apple silicon, the
/// environment, mlx-audio's Kokoro with the misaki it reads text through
/// (`training::find_mlx_audio_kokoro`), then -- when `model` is given -- the
/// MLX voice model read whole by its kind's rule (`read_mlx_info`, its kind
/// `voice`), then the MLX speech driver. No ffmpeg, as on PyTorch, and no
/// consent asked: mlx-audio is opt-in, and every package rung names
/// `kMlxAudioSetupRemedy`.
[[nodiscard]] VideoReadiness probe_voice_mlx(const VideoHost& host,
                                             const std::filesystem::path& model = {},
                                             std::string_view handle = {});

/// The speech ladder for `engine`: `probe_voice` for PyTorch,
/// `probe_voice_mlx` for MLX.
[[nodiscard]] VideoReadiness probe_voice_engine(const VideoHost& host, VideoEngine engine,
                                                const std::filesystem::path& model = {},
                                                std::string_view handle = {});

}  // namespace apogee::video
