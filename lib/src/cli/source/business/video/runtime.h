#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

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
namespace apogee::video {

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
};

[[nodiscard]] std::string_view to_string(VideoRefusal refusal) noexcept;

/// Where this install keeps what a generation runs on.
struct VideoHost {
    /// `<home>/training/venv`, the environment Apogee owns.
    std::filesystem::path venv;
    /// `<home>/training/scripts/video_generate.py`, the seeded driver.
    std::filesystem::path driver;
    /// `ffmpeg` on PATH, or empty.
    std::string ffmpeg;

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

    [[nodiscard]] bool ready() const noexcept {
        return refusal == VideoRefusal::None;
    }

    /// `reason -- remedy`, or the reason alone.
    [[nodiscard]] std::string message() const;
};

/// The ladder, in order: the environment, torch, diffusers, then -- when
/// `pipeline` is given -- the pipeline read whole (`read_pipeline_info`,
/// `handle` naming it in the fix), then the driver and ffmpeg.
[[nodiscard]] VideoReadiness probe_video(const VideoHost& host,
                                         const std::filesystem::path& pipeline = {},
                                         std::string_view handle = {});

/// The fix every package rung names.
inline constexpr std::string_view kVideoSetupRemedy = "apogee train setup --with video";

}  // namespace apogee::video
