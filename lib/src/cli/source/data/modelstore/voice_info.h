#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

/// What a voice model directory is, read from its files alone (50a): a
/// text-to-speech model as Kokoro publishes one -- `config.json`, one
/// checkpoint (`*.pth`) and a `voices/` directory of voice packs (`*.pt`) --
/// without importing anything or loading a weight.
///
/// **`mlx_info`'s honesty rule, carried over.** `complete` claims only what
/// the files can show: the configuration parses and is the model's, there is
/// exactly one checkpoint, there is at least one voice, and every one of
/// those files is whole -- a PyTorch save is a zip archive, and a whole one
/// ends in the archive's end record, which a download cut short never has.
/// Anything else is "cannot load", naming the piece. Whether this `kokoro`
/// builds the model is the load's to find out.
namespace apogee::models {

/// One voice: its name -- the pack's file name without `.pt` -- and the
/// language its name's first letter says (Kokoro's own convention:
/// `af_heart` American English, `bm_george` British English).
struct VoicePack {
    std::string name;
    std::string language;
    std::uintmax_t bytes = 0;
};

struct VoiceInfo {
    bool complete = false;
    /// Why not, in a sentence naming the missing piece. Set exactly when
    /// `complete` is false.
    std::string problem;
    /// `config.json` was read and is the model's.
    bool config_read = false;
    /// The checkpoint's file name, and its size.
    std::string checkpoint;
    std::uintmax_t checkpoint_bytes = 0;
    /// Every voice, sorted by name.
    std::vector<VoicePack> voices;
    /// Every regular file under the directory, in bytes.
    std::uintmax_t bytes = 0;

    /// The languages the voices speak, each once, in the voices' order.
    [[nodiscard]] std::vector<std::string> languages() const;
    /// Whether `name` is one of the voices.
    [[nodiscard]] bool has_voice(std::string_view name) const;
};

/// Reads `dir`. Never throws: an unreadable directory is an incomplete one,
/// with the reason.
[[nodiscard]] VoiceInfo read_voice_info(const std::filesystem::path& dir);

/// Whether `dir` looks like a voice model: `config.json`, a checkpoint at
/// its root and a `voices/` directory.
[[nodiscard]] bool is_voice_dir(const std::filesystem::path& dir);

/// The language a Kokoro voice's name says by its first letter, or
/// "unstated".
[[nodiscard]] std::string voice_language(std::string_view voice);

/// Whether the zip archive at `file`, `size` bytes, is whole: it opens with
/// a local file header and its end record lies within its last 64 KiB --
/// what a PyTorch save is, and what a cut-short download is not.
[[nodiscard]] bool zip_archive_whole(const std::filesystem::path& file, std::uintmax_t size);

/// One line: "kokoro-v1_0.pth, 54 voices (American English, British
/// English, …)".
[[nodiscard]] std::string describe(const VoiceInfo& info);

// --- a voice model on MLX (53m) ---------------------------------------------------------

/// What an MLX voice model's directory is (53m) -- mlx-audio's build of a
/// Kokoro model, its record's kind `voice` -- read as `read_voice_info` reads
/// the format it was built from: `complete` by the MLX reader's own rule for
/// the kind (`read_mlx_info`: its `config.json`, the weights and every voice
/// pack whole), `checkpoint` the weights' file (`model.safetensors`, or the
/// first of its shards), and `voices` its packs, `voices/<name>.safetensors`,
/// each named and its language said as a Kokoro voice's is. So `voice say`,
/// its completion and `models info` read the voices an MLX build carries --
/// mlx-audio's Kokoro presets -- as they read the original's.
[[nodiscard]] VoiceInfo read_mlx_voice_info(const std::filesystem::path& dir);

/// Whether mlx-audio's converter, with the Kokoro it builds, is installed in
/// Apogee's Python environment (53m) -- the caller's reading of that
/// environment, handed down as `VideoConverts` is: this floor reads none.
using VoiceConverts = std::function<bool()>;

/// The formats the stored voice model at `dir` converts to (53m) -- M20's
/// decision, one more answer: `mlx` exactly where the model is whole
/// (`read_voice_info`) and `mlx_audio` says mlx-audio is installed and builds
/// Kokoro; none anywhere else. Never a GGUF: llama.cpp's converter takes no
/// text-to-speech model.
[[nodiscard]] std::vector<std::string> voice_conversions(const std::filesystem::path& dir,
                                                         const VoiceConverts& mlx_audio = {});

}  // namespace apogee::models
