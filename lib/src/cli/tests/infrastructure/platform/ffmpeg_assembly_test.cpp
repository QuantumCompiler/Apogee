#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "platform/child_process.h"
#include "platform/ffmpeg.h"
#include "support/env_guard.h"
#include "support/fake_ffmpeg.h"

/// Assembly on the seam (50b): clips joined with crossfades, narration laid
/// over a ducked bed, captions burned. The builders are pinned argv-exact --
/// each boundary's offset, the ducking shape, the two levels of a path's
/// escaping -- without a process; the executors run over the fake ffmpeg
/// (the argv, the copy decided by the probe, the x264 fallback, the failure
/// tail, the deadline and the stop, nothing left behind); and where a real
/// ffmpeg is on PATH, tiny generated clips are assembled and probed again.
namespace {

using apogee::platform::Assembled;
using apogee::platform::AudioMix;
using apogee::platform::ClipFacts;
using apogee::platform::FfmpegLimits;
using apogee::testing::FakeFfmpeg;

using Args = std::vector<std::string>;

/// A clip as the probe would describe it: by default 1280x704 H.264 at 24
/// frames a second with AAC sound -- the reference pipeline's shape.
[[nodiscard]] ClipFacts clip(const std::string& name, double seconds, bool audio = true,
                             int width = 1280, int height = 704, double fps = 24.0,
                             const std::string& codec = "h264") {
    ClipFacts out;
    out.file = name;
    out.info.duration = seconds;
    out.info.video = true;
    out.info.width = width;
    out.info.height = height;
    out.info.fps = fps;
    out.info.video_codec = codec;
    out.info.audio = audio;
    if (audio) {
        out.info.audio_codec = "aac";
        out.info.sample_rate = 48000;
        out.info.channels = 2;
    }
    return out;
}

/// A clip's picture brought to the first's size and rate.
[[nodiscard]] std::string scaled(int index, const std::string& label,
                                 const std::string& size = "1280:704",
                                 const std::string& fps = "24.000") {
    return "[" + std::to_string(index) + ":v]scale=" + size +
           ":force_original_aspect_ratio=decrease,pad=" + size +
           ":(ow-iw)/2:(oh-ih)/2,setsar=1,fps=" + fps + ",format=yuv420p[" + label + "]";
}

/// A clip's sound brought to 48 kHz stereo and held to its length.
[[nodiscard]] std::string sound(int index, const std::string& length, const std::string& label) {
    return "[" + std::to_string(index) +
           ":a]aformat=sample_rates=48000:channel_layouts=stereo,apad,atrim=duration=" + length +
           "[" + label + "]";
}

constexpr std::string_view kStereo = "aformat=sample_rates=48000:channel_layouts=stereo";

[[nodiscard]] std::string slurp(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

struct Folder {
    apogee::testing::TempDir dir{"ffmpeg-assembly-" + std::to_string(std::random_device{}())};

    [[nodiscard]] std::filesystem::path file(const std::string& name,
                                             const std::string& bytes = "media") const {
        const std::filesystem::path path = dir.path() / name;
        std::ofstream{path, std::ios::binary} << bytes;
        return path;
    }
};

}  // namespace

TEST_CASE("a join's filter graph: each boundary's crossfade offset summed from the durations",
          "[platform][ffmpeg][assembly]") {
    using apogee::platform::concat_filter;
    // Two clips: the fade starts half a second before the first one ends.
    CHECK(concat_filter({clip("a.mp4", 4.0), clip("b.mp4", 3.0)}, 0.5) ==
          scaled(0, "v0") + ";" + sound(0, "4.000", "a0") + ";" + scaled(1, "v1") + ";" +
              sound(1, "3.000", "a1") +
              ";[v0][v1]xfade=transition=fade:duration=0.500:offset=3.500[vout]"
              ";[a0][a1]acrossfade=d=0.500[aout]");
    // Three: each offset is the joined length so far less one fade; a clip
    // without sound is as long a silence.
    CHECK(concat_filter({clip("a.mp4", 4.0), clip("b.mp4", 3.0, false), clip("c.mp4", 5.0)}, 0.5) ==
          scaled(0, "v0") + ";" + sound(0, "4.000", "a0") + ";" + scaled(1, "v1") +
              ";anullsrc=r=48000:cl=stereo,atrim=duration=3.000[a1];" + scaled(2, "v2") + ";" +
              sound(2, "5.000", "a2") +
              ";[v0][v1]xfade=transition=fade:duration=0.500:offset=3.500[x1]"
              ";[a0][a1]acrossfade=d=0.500[y1]"
              ";[x1][v2]xfade=transition=fade:duration=0.500:offset=6.000[vout]"
              ";[y1][a2]acrossfade=d=0.500[aout]");
    // A hard cut joins with concat; clips with no sound join without any.
    CHECK(concat_filter({clip("a.mp4", 4.0), clip("b.mp4", 3.0)}, 0.0) ==
          scaled(0, "v0") + ";" + sound(0, "4.000", "a0") + ";" + scaled(1, "v1") + ";" +
              sound(1, "3.000", "a1") + ";[v0][a0][v1][a1]concat=n=2:v=1:a=1[vout][aout]");
    CHECK(concat_filter({clip("a.mp4", 4.0, false), clip("b.mp4", 3.0, false)}, 0.0) ==
          scaled(0, "v0") + ";" + scaled(1, "v1") + ";[v0][v1]concat=n=2:v=1:a=0[vout]");
    // One clip has no boundary to fade.
    CHECK(concat_filter({clip("a.mp4", 4.0)}, 0.5) ==
          scaled(0, "vout") + ";" + sound(0, "4.000", "aout"));
    // The first clip's size, each side even; its rate, or the profile's.
    CHECK(concat_filter({clip("a.mp4", 2.0, false, 1279, 705, 0.0)}, 0.0) ==
          scaled(0, "vout", "1278:704", "24.000"));
    CHECK(concat_filter({clip("a.mp4", 2.0, false, 640, 360, 30000.0 / 1001.0)}, 0.0) ==
          scaled(0, "vout", "640:360", "29.970"));
    CHECK(concat_filter({}, 0.5).empty());
}

TEST_CASE("a join's command line, encoded once by the final profile or copied",
          "[platform][ffmpeg][assembly]") {
    using apogee::platform::concat_args;
    const std::vector<ClipFacts> clips{clip("a.mp4", 4.0), clip("b.mp4", 3.0)};
    const std::string graph = apogee::platform::concat_filter(clips, 0.5);
    CHECK(concat_args(clips, 0.5, "out.mp4", false) ==
          Args{"-nostdin",   "-hide_banner", "-loglevel", "error",  "-y",
               "-i",         "a.mp4",        "-i",        "b.mp4",  "-filter_complex",
               graph,        "-map",         "[vout]",    "-map",   "[aout]",
               "-c:v",       "libx264",      "-preset",   "medium", "-crf",
               "20",         "-pix_fmt",     "yuv420p",   "-c:a",   "aac",
               "-b:a",       "192k",         "-ar",       "48000",  "-movflags",
               "+faststart", "out.mp4"});
    // The fallback codec for an ffmpeg without x264.
    CHECK(concat_args(clips, 0.5, "out.mp4", true) ==
          Args{"-nostdin", "-hide_banner", "-loglevel", "error",      "-y",
               "-i",       "a.mp4",        "-i",        "b.mp4",      "-filter_complex",
               graph,      "-map",         "[vout]",    "-map",       "[aout]",
               "-c:v",     "mpeg4",        "-q:v",      "2",          "-pix_fmt",
               "yuv420p",  "-c:a",         "aac",       "-b:a",       "192k",
               "-ar",      "48000",        "-movflags", "+faststart", "out.mp4"});
    // No sound anywhere: none written.
    const std::vector<ClipFacts> silent{clip("a.mp4", 4.0, false)};
    CHECK(concat_args(silent, 0.0, "out.mp4", false) ==
          Args{"-nostdin",
               "-hide_banner",
               "-loglevel",
               "error",
               "-y",
               "-i",
               "a.mp4",
               "-filter_complex",
               apogee::platform::concat_filter(silent, 0.0),
               "-map",
               "[vout]",
               "-c:v",
               "libx264",
               "-preset",
               "medium",
               "-crf",
               "20",
               "-pix_fmt",
               "yuv420p",
               "-an",
               "-movflags",
               "+faststart",
               "out.mp4"});
    CHECK(apogee::platform::concat_copy_args("list.txt", "out.mp4") ==
          Args{"-nostdin", "-hide_banner", "-loglevel", "error", "-y", "-f", "concat", "-safe", "0",
               "-i", "list.txt", "-c", "copy", "-movflags", "+faststart", "out.mp4"});
    // The demuxer's list: a quote in a path closed, escaped and opened again.
    CHECK(apogee::platform::concat_list({clip("a.mp4", 1.0), clip("it's.mp4", 1.0)}) ==
          "file 'a.mp4'\nfile 'it'\\''s.mp4'\n");
}

TEST_CASE("clips are copied only when the probe says every one is alike and nothing fades",
          "[platform][ffmpeg][assembly]") {
    using apogee::platform::concat_can_copy;
    CHECK(concat_can_copy({clip("a.mp4", 4.0), clip("b.mp4", 3.0)}, 0.0));
    CHECK_FALSE(concat_can_copy({clip("a.mp4", 4.0), clip("b.mp4", 3.0)}, 0.5));
    CHECK(concat_can_copy({clip("a.mp4", 4.0)}, 0.5));  // no boundary to fade
    CHECK_FALSE(concat_can_copy({}, 0.0));
    const auto alike = [](ClipFacts second) {
        return concat_can_copy({clip("a.mp4", 4.0), std::move(second)}, 0.0);
    };
    CHECK_FALSE(alike(clip("b.mp4", 3.0, true, 1920, 1080)));
    CHECK_FALSE(alike(clip("b.mp4", 3.0, true, 1280, 704, 30.0)));
    CHECK_FALSE(alike(clip("b.mp4", 3.0, true, 1280, 704, 24.0, "hevc")));
    CHECK_FALSE(alike(clip("b.mp4", 3.0, false)));
    ClipFacts other_rate = clip("b.mp4", 3.0);
    other_rate.info.sample_rate = 44100;
    CHECK_FALSE(alike(other_rate));
    ClipFacts mono = clip("b.mp4", 3.0);
    mono.info.channels = 1;
    CHECK_FALSE(alike(mono));
    // A codec an mp4 does not hold as it is, or a rate nobody stated.
    CHECK_FALSE(concat_can_copy({clip("a.webm", 4.0, false, 1280, 704, 24.0, "vp8"),
                                 clip("b.webm", 3.0, false, 1280, 704, 24.0, "vp8")},
                                0.0));
    CHECK_FALSE(concat_can_copy({clip("a.mp4", 4.0, true, 1280, 704, 0.0)}, 0.0));
}

TEST_CASE("a join is refused before ffmpeg runs, saying why", "[platform][ffmpeg][assembly]") {
    using apogee::platform::concat_problem;
    CHECK(concat_problem({}, 0.0) == "there are no clips to join");
    CHECK(concat_problem({clip("a.mp4", 4.0)}, -1.0) == "a crossfade is 0 seconds or more");
    ClipFacts blind = clip("a.mp4", 4.0);
    blind.info.video = false;
    CHECK(concat_problem({blind}, 0.0) == "a.mp4 has no picture");
    CHECK(concat_problem({clip("a.mp4", 4.0, true, 0, 0)}, 0.0) == "a.mp4 states no picture size");
    CHECK(concat_problem({clip("a.mp4", 0.0)}, 0.0) == "a.mp4 states no length");
    CHECK(concat_problem({clip("a.mp4", 4.0), clip("b.mp4", 3.0)}, 1.6) ==
          "a 1.60 s crossfade at each end does not fit in b.mp4 (3.00 s) -- a shorter "
          "crossfade, or 0 for a cut");
    CHECK(concat_problem({clip("a.mp4", 4.0), clip("b.mp4", 3.0)}, 1.5).empty());
    CHECK(concat_problem({clip("a.mp4", 1.0)}, 2.0).empty());  // one clip fades nothing
}

TEST_CASE("narration is laid at its offsets over a ducked bed, the video's length kept",
          "[platform][ffmpeg][assembly]") {
    using apogee::platform::mix_filter;
    const std::string stereo{kStereo};
    const ClipFacts silent = clip("video.mp4", 10.0, false);
    const ClipFacts speaking = clip("video.mp4", 10.0, true);
    // One piece alone.
    CHECK(mix_filter(silent, AudioMix{.narration = {{"n1.wav", 0.5}}}) ==
          "[1:a]" + stereo + ",adelay=500|500[n0];[n0]apad,atrim=duration=10.000[aout]");
    // The whole shape: two pieces mixed at their own levels, the video's own
    // sound, and the bed lowered, held to the video and ducked under the
    // narration that keys it.
    const AudioMix full{.narration = {{"n1.wav", 0.5}, {"n2.wav", 4.25}}, .bed = "bed.mp3"};
    const std::string graph =
        "[1:a]" + stereo + ",adelay=500|500[n0];[2:a]" + stereo +
        ",adelay=4250|4250[n1];[n0][n1]amix=inputs=2:duration=longest:normalize=0[voice];[0:a]" +
        stereo + "[orig];[3:a]" + stereo +
        ",volume=0.300,atrim=duration=10.000[bedraw];[voice]asplit=2[voicemix][key];[bedraw]["
        "key]sidechaincompress=threshold=0.03:ratio=8:attack=20:release=400[bed];[orig]["
        "voicemix][bed]amix=inputs=3:duration=longest:normalize=0,apad,atrim=duration=10.000["
        "aout]";
    CHECK(mix_filter(speaking, full) == graph);
    // A bed alone is not ducked: nothing speaks over it.
    CHECK(mix_filter(silent, AudioMix{.bed = "bed.mp3"}) ==
          "[1:a]" + stereo +
              ",volume=0.300,atrim=duration=10.000[bedraw];[bedraw]apad,atrim=duration=10.000["
              "aout]");
    CHECK(mix_filter(speaking, AudioMix{.narration = {{"n1.wav", 0.0}}}) ==
          "[1:a]" + stereo + ",adelay=0|0[n0];[0:a]" + stereo +
              "[orig];[orig][n0]amix=inputs=2:duration=longest:normalize=0,apad,atrim=duration="
              "10.000[aout]");

    CHECK(apogee::platform::mix_args(speaking, full, "out.mp4") ==
          Args{"-nostdin", "-hide_banner", "-loglevel",  "error",
               "-y",       "-i",           "video.mp4",  "-i",
               "n1.wav",   "-i",           "n2.wav",     "-stream_loop",
               "-1",       "-i",           "bed.mp3",    "-filter_complex",
               graph,      "-map",         "0:v:0",      "-map",
               "[aout]",   "-c:v",         "copy",       "-c:a",
               "aac",      "-b:a",         "192k",       "-ar",
               "48000",    "-movflags",    "+faststart", "out.mp4"});
}

TEST_CASE("a mix is refused before ffmpeg runs, saying why", "[platform][ffmpeg][assembly]") {
    using apogee::platform::mix_problem;
    const ClipFacts video = clip("video.mp4", 10.0, false);
    const AudioMix one{.narration = {{"n1.wav", 0.5}}};
    ClipFacts blind = video;
    blind.info.video = false;
    CHECK(mix_problem(blind, one) == "video.mp4 has no picture");
    CHECK(mix_problem(clip("video.mp4", 0.0), one) == "video.mp4 states no length");
    CHECK(mix_problem(video, {}) == "there is nothing to mix -- no narration and no bed");
    CHECK(mix_problem(video, AudioMix{.narration = {{"n1.wav", -0.1}}}) ==
          "narration piece 1 starts before the video does");
    CHECK(mix_problem(video, AudioMix{.narration = {{"n1.wav", 1.0}, {"n2.wav", 10.0}}}) ==
          "narration piece 2 starts at 10.00 s, past the video's end at 10.00 s");
    CHECK(mix_problem(video, one).empty());
    CHECK(mix_problem(video, AudioMix{.bed = "bed.mp3"}).empty());
}

TEST_CASE("a caption file's path survives both levels of a filter graph's parsing",
          "[platform][ffmpeg][assembly]") {
    using apogee::platform::filter_path;
    CHECK(filter_path("captions.srt") == "captions.srt");
    CHECK(filter_path("a:b.srt") == "a\\\\:b.srt");
    CHECK(filter_path("it's.srt") == "it\\\\\\'s.srt");
    CHECK(filter_path("a,b[1];c.srt") == "a\\,b\\[1\\]\\;c.srt");
    CHECK(filter_path("my dir/x.srt") == "my\\\\\\ dir/x.srt");
    CHECK(filter_path("C:\\d\\x.srt") == "C\\\\:\\\\\\\\d\\\\\\\\x.srt");

    CHECK(apogee::platform::burn_args("v.mp4", "c.srt", "o.mp4", false) ==
          Args{"-nostdin", "-hide_banner", "-loglevel",
               "error",    "-y",           "-i",
               "v.mp4",    "-vf",          "subtitles=filename=c.srt",
               "-c:v",     "libx264",      "-preset",
               "medium",   "-crf",         "20",
               "-pix_fmt", "yuv420p",      "-c:a",
               "copy",     "-movflags",    "+faststart",
               "o.mp4"});
    CHECK(apogee::platform::burn_args("v.mp4", "c.srt", "o.mp4", true) ==
          Args{"-nostdin",   "-hide_banner", "-loglevel",
               "error",      "-y",           "-i",
               "v.mp4",      "-vf",          "subtitles=filename=c.srt",
               "-c:v",       "mpeg4",        "-q:v",
               "2",          "-pix_fmt",     "yuv420p",
               "-c:a",       "copy",         "-movflags",
               "+faststart", "o.mp4"});
}

TEST_CASE("ffmpeg's filter listing is read by name, never by description",
          "[platform][ffmpeg][assembly]") {
    using apogee::platform::filter_listed;
    const std::string listing =
        "Filters:\n"
        "  T.. = Timeline support\n"
        "  ------\n"
        " TS aap               AA->A      Apply Affine Projection algorithm.\n"
        " T.. acompressor       A->A       Audio compressor.\n"
        " .. subtitles         V->V       Render subtitles onto input video using libass.\n";
    CHECK(filter_listed(listing, "subtitles"));
    CHECK(filter_listed(listing, "acompressor"));  // an older ffmpeg's three flags
    CHECK(filter_listed(listing, "aap"));
    CHECK_FALSE(filter_listed(listing, "libass"));
    CHECK_FALSE(filter_listed(listing, "Timeline"));
    CHECK_FALSE(filter_listed(listing, "sub"));
    CHECK_FALSE(filter_listed("", "subtitles"));
}

TEST_CASE("ffprobe's stream blocks give each codec to its own stream",
          "[platform][ffmpeg][assembly]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("no child processes on this platform");
    }
    const Folder folder;
    const FakeFfmpeg fake;
    std::string error;
    const auto info = apogee::platform::probe_media(folder.file("talk.mp4"), {}, error);
    REQUIRE(info.has_value());
    CHECK(info->video_codec == "h264");
    CHECK(info->fps == 24.0);
    CHECK(info->width == 640);
    CHECK(info->audio_codec == "aac");
    CHECK(info->sample_rate == 48000);
    CHECK(info->channels == 2);
    // A rate as a fraction; and output with no blocks, a stream begun at
    // each type.
    fake.probe_as("ntsc.mp4",
                  "[STREAM]\ncodec_name=h264\ncodec_type=video\nwidth=720\n"
                  "height=480\nr_frame_rate=30000/1001\n[/STREAM]\n[FORMAT]\n"
                  "duration=2.5\n[/FORMAT]\n");
    const auto ntsc = apogee::platform::probe_media(folder.file("ntsc.mp4"), {}, error);
    REQUIRE(ntsc.has_value());
    CHECK(std::abs(ntsc->fps - 29.97) < 0.001);
    CHECK_FALSE(ntsc->audio);
    fake.probe_as("bare.mp4",
                  "codec_name=aac\ncodec_type=audio\nsample_rate=24000\n"
                  "codec_name=mpeg4\ncodec_type=video\nwidth=64\nheight=48\n"
                  "duration=1\n");
    const auto bare = apogee::platform::probe_media(folder.file("bare.mp4"), {}, error);
    REQUIRE(bare.has_value());
    CHECK(bare->audio_codec == "aac");
    CHECK(bare->sample_rate == 24000);
    CHECK(bare->video_codec == "mpeg4");
    CHECK(bare->width == 64);
}

TEST_CASE("a join runs as the probe decides: copied when alike, else encoded once",
          "[platform][ffmpeg][assembly]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("no child processes on this platform");
    }
    const Folder folder;
    const FakeFfmpeg fake;
    const std::filesystem::path a = folder.file("a.mp4");
    const std::filesystem::path b = folder.file("b.mp4");
    const std::filesystem::path output = folder.dir.path() / "out.mp4";
    std::string error;

    // Alike and cut: copied through the demuxer's list, which is gone after.
    std::optional<Assembled> made = apogee::platform::concat_clips({a, b}, 0.0, output, {}, error);
    REQUIRE(made.has_value());
    CHECK(made->codec == "copy");
    CHECK(made->settings == "the streams copied as they were -- no re-encode");
    const std::filesystem::path list = folder.dir.path() / ".out.mp4.concat.txt";
    CHECK(fake.logged() == "-nostdin -hide_banner -loglevel error -y -f concat -safe 0 -i " +
                               list.string() + " -c copy -movflags +faststart " + output.string() +
                               "\n");
    CHECK_FALSE(std::filesystem::exists(list));
    CHECK(slurp(output) == "MP4 copy");

    // Faded: encoded once, the offsets from each clip's own probed length.
    fake.probe_as("a.mp4", FakeFfmpeg::probe_text("video", "4"));
    fake.probe_as("b.mp4", FakeFfmpeg::probe_text("video audio", "3"));
    made = apogee::platform::concat_clips({a, b}, 0.5, output, {}, error);
    REQUIRE(made.has_value());
    CHECK(made->codec == "h264");
    CHECK(made->settings ==
          "the final profile: h264 at crf 20, preset medium, yuv420p; aac 192k "
          "at 48000 Hz, stereo");
    CHECK(fake.logged().find("anullsrc=r=48000:cl=stereo,atrim=duration=4.000[a0]") !=
          std::string::npos);
    CHECK(fake.logged().find("xfade=transition=fade:duration=0.500:offset=3.500[vout]") !=
          std::string::npos);
    CHECK(slurp(output) == "MP4 libx264");

    // An ffmpeg without x264 is asked again for the fallback, and it is said.
    fake.no_x264();
    made = apogee::platform::concat_clips({a, b}, 0.5, output, {}, error);
    REQUIRE(made.has_value());
    CHECK(made->codec == "mpeg4");
    CHECK(made->settings ==
          "the final profile: mpeg4 at quality 2 (this ffmpeg has no libx264), "
          "yuv420p; aac 192k at 48000 Hz, stereo");

    // A clip with no picture is refused before ffmpeg is asked anything.
    const FakeFfmpeg quiet;
    quiet.probe_as("voice.mp4", FakeFfmpeg::probe_text("audio", "3"));
    const std::filesystem::path voice = folder.file("voice.mp4");
    CHECK_FALSE(apogee::platform::concat_clips({a, voice}, 0.0, output, {}, error).has_value());
    CHECK(error == voice.string() + " has no picture");
    CHECK(quiet.logged().empty());
}

TEST_CASE("a failed or stopped assembly leaves nothing at its output, saying why",
          "[platform][ffmpeg][assembly]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("no child processes on this platform");
    }
    const Folder folder;
    const FakeFfmpeg fake;
    const std::filesystem::path a = folder.file("a.mp4");
    const std::filesystem::path output = folder.dir.path() / "out.mp4";
    std::string error;
    SECTION("the failure's tail") {
        fake.fail();
        std::ofstream{output} << "an earlier file";
        CHECK_FALSE(apogee::platform::concat_clips({a, a}, 0.5, output, {}, error).has_value());
        CHECK(error == "ffmpeg failed: Invalid data found when processing input");
    }
    SECTION("the deadline") {
        fake.slow();
        FfmpegLimits limits;
        limits.timeout = std::chrono::milliseconds{300};
        const auto began = std::chrono::steady_clock::now();
        CHECK_FALSE(apogee::platform::concat_clips({a}, 0.0, output, limits, error).has_value());
        CHECK(error.find("took longer than") != std::string::npos);
        CHECK(std::chrono::steady_clock::now() - began < std::chrono::seconds{4});
    }
    SECTION("the stop") {
        fake.slow();
        FfmpegLimits limits;
        // Stopped once ffmpeg itself runs -- the probes before it share the
        // limits, and are let finish.
        limits.stopped = [&fake] { return std::filesystem::exists(fake.log()); };
        CHECK_FALSE(
            apogee::platform::mix_audio(a, AudioMix{.bed = a}, output, limits, error).has_value());
        CHECK(error == "stopped");
    }
    CHECK_FALSE(std::filesystem::exists(output));
    CHECK_FALSE(std::filesystem::exists(folder.dir.path() / ".out.mp4.concat.txt"));
}

TEST_CASE("a mix probes every track for sound, then copies the picture",
          "[platform][ffmpeg][assembly]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("no child processes on this platform");
    }
    const Folder folder;
    const FakeFfmpeg fake;
    fake.probe_as("video.mp4", FakeFfmpeg::probe_text("video", "10"));
    const std::filesystem::path video = folder.file("video.mp4");
    const std::filesystem::path n1 = folder.file("n1.wav");
    const std::filesystem::path n2 = folder.file("n2.wav");
    const std::filesystem::path bed = folder.file("bed.mp3");
    const std::filesystem::path output = folder.dir.path() / "mixed.mp4";
    std::string error;
    const std::optional<Assembled> made = apogee::platform::mix_audio(
        video, AudioMix{.narration = {{n1, 0.5}, {n2, 4.0}}, .bed = bed}, output, {}, error);
    REQUIRE(made.has_value());
    CHECK(made->codec == "copy");
    CHECK(made->settings ==
          "the picture copied as it was; aac 192k at 48000 Hz, stereo: "
          "narration (2 pieces) over the bed, ducked");
    const std::string argv = fake.logged();
    CHECK(argv.find("-i " + n2.string() + " -stream_loop -1 -i " + bed.string() +
                    " -filter_complex ") != std::string::npos);
    CHECK(argv.find("[bedraw][key]sidechaincompress=") != std::string::npos);
    CHECK(argv.find("-map 0:v:0 -map [aout] -c:v copy -c:a aac") != std::string::npos);
    CHECK(slurp(output) == "MP4 copy");

    // A piece with no sound, or one past the end, is refused by name.
    fake.probe_as("n3.wav", FakeFfmpeg::probe_text("video", "2"));
    const std::filesystem::path n3 = folder.file("n3.wav");
    CHECK_FALSE(
        apogee::platform::mix_audio(video, AudioMix{.narration = {{n3, 0.0}}}, output, {}, error)
            .has_value());
    CHECK(error == n3.string() + " has no sound");
    CHECK_FALSE(
        apogee::platform::mix_audio(video, AudioMix{.narration = {{n1, 12.0}}}, output, {}, error)
            .has_value());
    CHECK(error == "narration piece 1 starts at 12.00 s, past the video's end at 10.00 s");
}

TEST_CASE("captions burn into the frames; an ffmpeg without libass is refused by name",
          "[platform][ffmpeg][assembly]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("no child processes on this platform");
    }
    const Folder folder;
    const FakeFfmpeg fake;
    const std::filesystem::path video = folder.file("video.mp4");
    const std::filesystem::path captions =
        folder.file("captions.srt", "1\n00:00:00,000 --> 00:00:01,000\nHello.\n\n");
    const std::filesystem::path output = folder.dir.path() / "captioned.mp4";
    std::string error;
    CHECK(apogee::platform::ffmpeg_has_filter("subtitles", {}));
    std::optional<Assembled> made =
        apogee::platform::burn_captions(video, captions, output, {}, error);
    REQUIRE(made.has_value());
    CHECK(made->codec == "h264");
    CHECK(made->settings ==
          "the final profile: h264 at crf 20, preset medium, yuv420p; the "
          "sound copied as it was; the captions burned in");
    CHECK(fake.logged().find("-vf subtitles=filename=" + apogee::platform::filter_path(captions) +
                             " -c:v libx264") != std::string::npos);

    CHECK_FALSE(
        apogee::platform::burn_captions(video, folder.dir.path() / "none.srt", output, {}, error)
            .has_value());
    CHECK(error == "there are no captions at " + (folder.dir.path() / "none.srt").string());

    fake.no_libass();
    CHECK_FALSE(apogee::platform::ffmpeg_has_filter("subtitles", {}));
    CHECK(apogee::platform::ffmpeg_has_filter("xfade", {}));
    made = apogee::platform::burn_captions(video, captions, output, {}, error);
    CHECK_FALSE(made.has_value());
    CHECK(error ==
          "this ffmpeg cannot burn captions: it has no subtitles filter (it was built "
          "without libass) -- an ffmpeg built with libass burns them");
    CHECK_FALSE(std::filesystem::exists(output));
}

namespace {

/// Runs the real ffmpeg to make a fixture; whether it succeeded.
[[nodiscard]] bool generate(const std::vector<std::string>& arguments) {
    apogee::platform::ChildCommand command;
    command.program = "ffmpeg";
    command.arguments = {"-nostdin", "-hide_banner", "-loglevel", "error", "-y"};
    command.arguments.insert(command.arguments.end(), arguments.begin(), arguments.end());
    std::string error;
    const std::unique_ptr<apogee::platform::ChildProcess> child =
        apogee::platform::start_child(command, error);
    if (child == nullptr) {
        return false;
    }
    child->close_stdin();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{60};
    bool out_open = true;
    bool err_open = true;
    std::string piece;
    while ((out_open || err_open) && std::chrono::steady_clock::now() < deadline) {
        using apogee::platform::ReadStatus;
        if (out_open) {
            const ReadStatus read = child->read_stdout(piece, std::chrono::milliseconds{20});
            out_open = read == ReadStatus::Data || read == ReadStatus::Timeout;
        }
        if (err_open) {
            const ReadStatus read = child->read_stderr(piece, std::chrono::milliseconds{20});
            err_open = read == ReadStatus::Data || read == ReadStatus::Timeout;
        }
    }
    const std::optional<int> status = child->wait_for_exit(std::chrono::seconds{5});
    return status.has_value() && *status == 0;
}

/// What the real ffprobe reads of `file`.
[[nodiscard]] apogee::platform::MediaInfo probed(const std::filesystem::path& file) {
    std::string error;
    const std::optional<apogee::platform::MediaInfo> info =
        apogee::platform::probe_media(file, {}, error);
    INFO(error);
    REQUIRE(info.has_value());
    return *info;
}

}  // namespace

TEST_CASE("on a real ffmpeg, clips join with a fade and narration mixes over a bed",
          "[platform][ffmpeg][assembly][real]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("no child processes on this platform");
    }
    if (apogee::platform::ffmpeg_path().empty() || apogee::platform::ffprobe_path().empty()) {
        SKIP("no ffmpeg and ffprobe on PATH -- the real-fixture pass is skipped");
    }
    const Folder folder;
    const auto at = [&folder](const std::string& name) { return folder.dir.path() / name; };
    // Two clips unlike each other: a silent 1.5 s at 64x48 and 10 frames a
    // second, and a sounding 2 s at 80x48 and 12.
    REQUIRE(generate({"-f", "lavfi", "-i", "testsrc=duration=1.5:size=64x48:rate=10", "-c:v",
                      "mpeg4", "-pix_fmt", "yuv420p", at("a.mp4").string()}));
    REQUIRE(generate({"-f", "lavfi", "-i", "testsrc2=duration=2:size=80x48:rate=12", "-f", "lavfi",
                      "-i", "sine=frequency=440:duration=2", "-c:v", "mpeg4", "-pix_fmt", "yuv420p",
                      "-c:a", "aac", "-shortest", at("b.mp4").string()}));
    REQUIRE(generate({"-f", "lavfi", "-i", "sine=frequency=300:duration=0.8", "-ar", "24000", "-ac",
                      "1", at("narration.wav").string()}));
    REQUIRE(generate({"-f", "lavfi", "-i", "anoisesrc=d=1:a=0.2", at("bed.wav").string()}));

    std::string error;
    const std::optional<Assembled> joined = apogee::platform::concat_clips(
        {at("a.mp4"), at("b.mp4")}, 0.5, at("joined.mp4"), {}, error);
    INFO(error);
    REQUIRE(joined.has_value());
    CHECK(joined->codec != "copy");
    const apogee::platform::MediaInfo whole = probed(at("joined.mp4"));
    CHECK(whole.video);
    CHECK(whole.audio);  // the silent clip's silence, then the sine
    CHECK(whole.width == 64);
    CHECK(whole.height == 48);
    CHECK(std::abs(whole.duration - 3.0) < 0.2);  // 1.5 + 2 - 0.5

    // The same clip twice, cut: alike, so copied -- and as long as both.
    const std::optional<Assembled> copied = apogee::platform::concat_clips(
        {at("a.mp4"), at("a.mp4")}, 0.0, at("copied.mp4"), {}, error);
    REQUIRE(copied.has_value());
    CHECK(copied->codec == "copy");
    CHECK(std::abs(probed(at("copied.mp4")).duration - 3.0) < 0.2);

    const std::optional<Assembled> mixed = apogee::platform::mix_audio(
        at("joined.mp4"), AudioMix{.narration = {{at("narration.wav"), 0.5}}, .bed = at("bed.wav")},
        at("mixed.mp4"), {}, error);
    REQUIRE(mixed.has_value());
    const apogee::platform::MediaInfo mix = probed(at("mixed.mp4"));
    CHECK(mix.video);
    CHECK(mix.audio);
    CHECK(mix.audio_codec == "aac");
    CHECK(mix.video_codec == whole.video_codec);  // the picture as it was
    CHECK(std::abs(mix.duration - whole.duration) < 0.1);
}

TEST_CASE("on a real ffmpeg with libass, captions burn into a clip",
          "[platform][ffmpeg][assembly][real]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("no child processes on this platform");
    }
    if (apogee::platform::ffmpeg_path().empty() || apogee::platform::ffprobe_path().empty()) {
        SKIP("no ffmpeg and ffprobe on PATH -- the real burn is skipped");
    }
    if (!apogee::platform::ffmpeg_has_filter("subtitles", {})) {
        SKIP(
            "this ffmpeg has no subtitles filter (built without libass) -- the real burn is "
            "skipped");
    }
    const Folder folder;
    const std::filesystem::path clip_file = folder.dir.path() / "clip.mp4";
    REQUIRE(generate({"-f", "lavfi", "-i", "testsrc=duration=1:size=160x120:rate=10", "-c:v",
                      "mpeg4", "-pix_fmt", "yuv420p", clip_file.string()}));
    const std::filesystem::path captions =
        folder.file("it's: captions.srt", "1\n00:00:00,000 --> 00:00:00,900\nHello, café.\n\n");
    std::string error;
    const std::optional<Assembled> burned = apogee::platform::burn_captions(
        clip_file, captions, folder.dir.path() / "burned.mp4", {}, error);
    INFO(error);
    REQUIRE(burned.has_value());
    const apogee::platform::MediaInfo info = probed(folder.dir.path() / "burned.mp4");
    CHECK(info.video);
    CHECK(std::abs(info.duration - 1.0) < 0.2);
}
