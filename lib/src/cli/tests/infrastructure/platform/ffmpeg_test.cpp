#include "platform/ffmpeg.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "platform/child_process.h"
#include "support/env_guard.h"
#include "support/fake_ffmpeg.h"

using apogee::platform::decode_audio_wav;
using apogee::platform::extract_frames;
using apogee::platform::FfmpegLimits;
using apogee::platform::FrameOptions;
using apogee::platform::probe_media;
using apogee::platform::showinfo_times;
using apogee::platform::wav_file;
using apogee::testing::FakeFfmpeg;

namespace {

/// A little-endian number from `bytes` at `at`.
[[nodiscard]] std::uint32_t le32(const std::string& bytes, std::size_t at) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + index]))
                 << (8U * index);
    }
    return value;
}

struct Folder {
    apogee::testing::TempDir dir{"ffmpeg-" + std::to_string(std::random_device{}())};
};

}  // namespace

TEST_CASE("a WAV file's header states its rate and its size", "[platform][ffmpeg]") {
    const std::string pcm(1000, '\0');
    const std::string wav = wav_file(pcm, 24000);
    REQUIRE(wav.size() == 44 + 1000);
    CHECK(wav.substr(0, 4) == "RIFF");
    CHECK(le32(wav, 4) == 36 + 1000);
    CHECK(wav.substr(8, 8) == "WAVEfmt ");
    CHECK(le32(wav, 24) == 24000);
    CHECK(le32(wav, 28) == 48000);
    CHECK(wav.substr(36, 4) == "data");
    CHECK(le32(wav, 40) == 1000);
    // An odd byte is no sample.
    CHECK(wav_file(std::string(7, '\0'), 16000).size() == 44 + 6);
}

TEST_CASE("frame times are read from showinfo's lines alone", "[platform][ffmpeg]") {
    const std::string log =
        "frame=   10 fps=0.0 q=-0.0 size=N/A time=00:00:05 pts_time:99\n"
        "[Parsed_showinfo_2 @ 0x1] n:   0 pts:      0 pts_time:0       duration:1\n"
        "[Parsed_showinfo_2 @ 0x1] n:   1 pts:  64000 pts_time:7.5\n";
    CHECK(showinfo_times(log) == std::vector<double>{0.0, 7.5});
}

TEST_CASE("ffprobe's streams and length", "[platform][ffmpeg]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("no child processes on this platform");
    }
    const Folder folder;
    std::string error;
    {
        const FakeFfmpeg ffmpeg{"video audio", "600.4"};
        const auto info = probe_media(folder.dir.path() / "talk.mp4", {}, error);
        REQUIRE(info.has_value());
        CHECK(info->video);
        CHECK(info->audio);
        CHECK(info->duration == 600.4);
        CHECK(info->width == 640);
    }
    {
        // A still image has no length.
        const FakeFfmpeg ffmpeg{"video", "N/A"};
        const auto info = probe_media(folder.dir.path() / "shot.png", {}, error);
        REQUIRE(info.has_value());
        CHECK_FALSE(info->audio);
        CHECK(info->duration == 0.0);
    }
}

TEST_CASE("frames are written to the folder given, each with its time", "[platform][ffmpeg]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("no child processes on this platform");
    }
    const FakeFfmpeg ffmpeg{"video", "30", "0 2.5 4"};
    const Folder folder;
    FrameOptions options;
    options.start = 10.0;
    options.length = 5.0;
    std::string error;
    const auto frames = extract_frames(folder.dir.path() / "clip.mp4", options,
                                       folder.dir.path() / "out", {}, error);
    REQUIRE(error.empty());
    REQUIRE(frames.size() == 3);
    // A span's timestamps start again at its start, so it is added back.
    CHECK(frames[0].seconds == 10.0);
    CHECK(frames[1].seconds == 12.5);
    CHECK(frames[2].seconds == 14.0);
    CHECK(frames[1].file == folder.dir.path() / "out" / "f00002.jpg");
    CHECK(std::filesystem::exists(frames[2].file));
    const std::string logged = ffmpeg.logged();
    CHECK(logged.find("-ss 10.000 -t 5.000 -i") != std::string::npos);
    CHECK(logged.find("-fps_mode vfr") != std::string::npos);
}

TEST_CASE("audio is decoded to the rate asked for, wrapped as WAV", "[platform][ffmpeg]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("no child processes on this platform");
    }
    const FakeFfmpeg ffmpeg{"audio", "60", "0", 6400};
    const Folder folder;
    std::string error;
    const std::string wav =
        decode_audio_wav(folder.dir.path() / "note.m4a", 24000, 30.0, 30.0, {}, error);
    REQUIRE(error.empty());
    CHECK(wav.size() == 44 + 6400);
    CHECK(le32(wav, 24) == 24000);
    CHECK(ffmpeg.logged().find("-ss 30.000 -t 30.000 -i") != std::string::npos);
    CHECK(ffmpeg.logged().find("-ac 1 -ar 24000 -f s16le -") != std::string::npos);
}

TEST_CASE("a failing run says what ffmpeg said last", "[platform][ffmpeg]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("no child processes on this platform");
    }
    const FakeFfmpeg ffmpeg;
    ffmpeg.fail();
    const Folder folder;
    std::string error;
    CHECK(decode_audio_wav(folder.dir.path() / "note.m4a", 16000, 0, 0, {}, error).empty());
    CHECK(error == "ffmpeg failed: Invalid data found when processing input");
}

TEST_CASE("a run is bounded: a deadline, a cap on its output, and a stop", "[platform][ffmpeg]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("no child processes on this platform");
    }
    const Folder folder;
    std::string error;
    SECTION("the deadline") {
        const FakeFfmpeg ffmpeg;
        ffmpeg.slow();
        FfmpegLimits limits;
        limits.timeout = std::chrono::milliseconds{300};
        const auto began = std::chrono::steady_clock::now();
        CHECK(decode_audio_wav(folder.dir.path() / "a.wav", 16000, 0, 0, limits, error).empty());
        CHECK(error.find("took longer than") != std::string::npos);
        CHECK(std::chrono::steady_clock::now() - began < std::chrono::seconds{4});
    }
    SECTION("the output cap") {
        const FakeFfmpeg ffmpeg{"audio", "60", "0", 200000};
        FfmpegLimits limits;
        limits.max_output = 1024;
        CHECK(decode_audio_wav(folder.dir.path() / "a.wav", 16000, 0, 0, limits, error).empty());
        CHECK(error.find("wrote more than") != std::string::npos);
    }
    SECTION("the stop") {
        const FakeFfmpeg ffmpeg;
        ffmpeg.slow();
        FfmpegLimits limits;
        limits.stopped = [] { return true; };
        CHECK(decode_audio_wav(folder.dir.path() / "a.wav", 16000, 0, 0, limits, error).empty());
        CHECK(error == "stopped");
    }
}
