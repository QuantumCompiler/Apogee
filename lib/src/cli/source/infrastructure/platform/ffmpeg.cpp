#include "platform/ffmpeg.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <system_error>
#include <utility>

#include "platform/child_process.h"

namespace apogee::platform {

namespace {

/// Bytes of a child's stderr kept for an error message.
constexpr std::size_t kStderrTail = 8 * 1024;

/// What one run produced.
struct Run {
    std::string out;
    std::string tail;
    /// Why the run did not finish cleanly, or empty.
    std::string error;
};

/// The last line in `tail` with anything on it.
[[nodiscard]] std::string last_line(std::string_view tail) {
    while (!tail.empty() && (tail.back() == '\n' || tail.back() == '\r' || tail.back() == ' ')) {
        tail.remove_suffix(1);
    }
    const std::size_t start = tail.find_last_of('\n');
    return std::string{start == std::string_view::npos ? tail : tail.substr(start + 1)};
}

/// Runs `program` with `arguments` to completion under `limits`. Each line of
/// stderr goes to `on_line` as it arrives; the last `kStderrTail` bytes are
/// kept for the error.
[[nodiscard]] Run run(const std::string& program, std::vector<std::string> arguments,
                      const FfmpegLimits& limits,
                      const std::function<void(std::string_view)>& on_line = {}) {
    Run out;
    ChildCommand command;
    command.program = program;
    command.arguments = std::move(arguments);
    std::string error;
    const std::unique_ptr<ChildProcess> child = start_child(command, error);
    if (child == nullptr) {
        out.error = "could not start " + program + (error.empty() ? "" : ": " + error);
        return out;
    }
    child->close_stdin();

    const auto deadline = std::chrono::steady_clock::now() + limits.timeout;
    std::string pending;  // a stderr line not yet ended
    const auto take_stderr = [&](std::string_view piece) {
        out.tail += piece;
        if (out.tail.size() > kStderrTail) {
            out.tail.erase(0, out.tail.size() - kStderrTail);
        }
        if (!on_line) {
            return;
        }
        pending += piece;
        for (std::size_t end = pending.find('\n'); end != std::string::npos;
             end = pending.find('\n')) {
            on_line(std::string_view{pending}.substr(0, end));
            pending.erase(0, end + 1);
        }
    };
    const auto stop = [&](std::string why) {
        child->terminate();
        (void)child->wait_for_exit(std::chrono::seconds{2});
        out.error = std::move(why);
    };

    bool out_open = true;
    bool err_open = true;
    std::string piece;
    while (out_open || err_open) {
        if (limits.stopped && limits.stopped()) {
            stop("stopped");
            return out;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            stop(program + " took longer than " +
                 std::to_string(
                     std::chrono::duration_cast<std::chrono::seconds>(limits.timeout).count()) +
                 " seconds");
            return out;
        }
        if (out_open) {
            switch (child->read_stdout(piece, std::chrono::milliseconds{50})) {
                case ReadStatus::Data:
                    out.out += piece;
                    if (out.out.size() > limits.max_output) {
                        stop(program + " wrote more than " +
                             std::to_string(limits.max_output / (1024 * 1024)) + " MB");
                        return out;
                    }
                    break;
                case ReadStatus::Timeout:
                    break;
                case ReadStatus::Eof:
                case ReadStatus::Error:
                    out_open = false;
                    break;
            }
        }
        if (err_open) {
            switch (child->read_stderr(piece, std::chrono::milliseconds{out_open ? 0 : 50})) {
                case ReadStatus::Data:
                    take_stderr(piece);
                    break;
                case ReadStatus::Timeout:
                    break;
                case ReadStatus::Eof:
                case ReadStatus::Error:
                    err_open = false;
                    break;
            }
        }
    }
    if (!pending.empty() && on_line) {
        on_line(pending);
    }
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    const std::optional<int> status =
        child->wait_for_exit(std::max(remaining, std::chrono::milliseconds{1000}));
    if (!status.has_value()) {
        stop(program + " did not exit");
        return out;
    }
    if (*status != 0) {
        const std::string said = last_line(out.tail);
        out.error = program + " failed" + (said.empty() ? "" : ": " + said);
    }
    return out;
}

/// `seconds` as ffmpeg takes a time: plain decimal, never an exponent.
[[nodiscard]] std::string time_argument(double seconds) {
    std::array<char, 32> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%.3f", std::max(seconds, 0.0));
    return buffer.data();
}

/// The `-ss`/`-t` arguments for a span, before `-i`: input seeking, which is
/// fast, and which restarts the timestamps at the span's start.
void add_span(std::vector<std::string>& arguments, double start, double length) {
    if (start > 0.0) {
        arguments.insert(arguments.end(), {"-ss", time_argument(start)});
    }
    if (length > 0.0) {
        arguments.insert(arguments.end(), {"-t", time_argument(length)});
    }
}

/// The video filter for `options`: which frames, scaled, each logged.
[[nodiscard]] std::string frame_filter(const FrameOptions& options) {
    std::string select;
    if (options.fps > 0.0) {
        select = "fps=" + time_argument(options.fps);
    } else {
        // The first frame, then one `every` seconds after the last taken, and
        // any scene change besides. Quoted, so the commas stay the
        // expression's rather than ending the filter.
        select = "select='isnan(prev_selected_t)+gte(t-prev_selected_t," +
                 time_argument(std::max(options.every, 0.1)) + ")";
        if (options.scene > 0.0) {
            select += "+gt(scene," + time_argument(options.scene) + ")";
        }
        select += "'";
    }
    const std::string side = std::to_string(std::max(options.max_side, 16));
    return select + ",scale='min(" + side + ",iw)':'min(" + side +
           ",ih)':force_original_aspect_ratio=decrease,showinfo";
}

/// ffprobe's `r_frame_rate` (`24/1`, `30000/1001`) as frames a second; 0
/// for `0/0` or anything unreadable.
[[nodiscard]] double frame_rate(const std::string& value) {
    const std::size_t slash = value.find('/');
    const double top = std::strtod(value.c_str(), nullptr);
    const double bottom =
        slash == std::string::npos ? 1.0 : std::strtod(value.c_str() + slash + 1, nullptr);
    const double rate = bottom > 0.0 ? top / bottom : 0.0;
    return std::isfinite(rate) && rate > 0.0 ? rate : 0.0;
}

}  // namespace

std::string ffmpeg_path() {
    return find_on_path("ffmpeg");
}

std::string ffprobe_path() {
    return find_on_path("ffprobe");
}

std::optional<MediaInfo> probe_media(const std::filesystem::path& file, const FfmpegLimits& limits,
                                     std::string& error) {
    // Each stream a `[STREAM]` block, so a codec is read with its own
    // stream's type rather than the line's neighbour's.
    const Run probed = run("ffprobe",
                           {"-v", "error", "-show_entries",
                            "format=duration:stream=codec_type,codec_name,width,height,r_frame_"
                            "rate,sample_rate,channels",
                            "-of", "default", file.string()},
                           limits);
    if (!probed.error.empty()) {
        error = probed.error;
        return std::nullopt;
    }
    MediaInfo info;
    std::map<std::string, std::string, std::less<>> stream;
    const auto take_stream = [&info, &stream] {
        const auto field = [&stream](std::string_view key) {
            const auto found = stream.find(key);
            return found == stream.end() ? std::string{} : found->second;
        };
        const std::string type = field("codec_type");
        if (type == "video" && !info.video) {
            info.video = true;
            info.video_codec = field("codec_name");
            info.width = std::atoi(field("width").c_str());
            info.height = std::atoi(field("height").c_str());
            info.fps = frame_rate(field("r_frame_rate"));
        } else if (type == "audio" && !info.audio) {
            info.audio = true;
            info.audio_codec = field("codec_name");
            info.sample_rate = std::atoi(field("sample_rate").c_str());
            info.channels = std::atoi(field("channels").c_str());
        }
        stream.clear();
    };
    std::string_view rest = probed.out;
    while (!rest.empty()) {
        const std::size_t end = rest.find('\n');
        std::string_view line = rest.substr(0, end);
        rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end + 1);
        while (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line == "[/STREAM]") {
            take_stream();
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) {
            continue;
        }
        const std::string_view key = line.substr(0, equals);
        const std::string value{line.substr(equals + 1)};
        if (key == "duration") {
            if (value != "N/A") {
                const double seconds = std::strtod(value.c_str(), nullptr);
                info.duration = std::isfinite(seconds) && seconds > 0.0 ? seconds : 0.0;
            }
        } else {
            if (stream.contains(key)) {
                take_stream();  // a key again: the next stream, its block unmarked
            }
            stream[std::string{key}] = value;
        }
    }
    take_stream();
    return info;
}

std::vector<ExtractedFrame> extract_frames(const std::filesystem::path& file,
                                           const FrameOptions& options,
                                           const std::filesystem::path& directory,
                                           const FfmpegLimits& limits, std::string& error) {
    std::error_code code;
    std::filesystem::create_directories(directory, code);
    const auto arguments = [&](std::string_view mode_flag) {
        std::vector<std::string> out{"-nostdin", "-hide_banner", "-nostats", "-loglevel", "info"};
        add_span(out, options.start, options.length);
        out.insert(out.end(), {"-i", file.string(), "-an", "-vf", frame_filter(options),
                               std::string{mode_flag}, "vfr"});
        if (options.max_frames > 0) {
            out.insert(out.end(), {"-frames:v", std::to_string(options.max_frames)});
        }
        out.insert(out.end(), {"-q:v", "3", (directory / "f%05d.jpg").string()});
        return out;
    };
    std::vector<double> times;
    const auto log = [&times](std::string_view line) {
        const std::vector<double> found = showinfo_times(line);
        times.insert(times.end(), found.begin(), found.end());
    };
    // Variable rate, so each frame selected is one file: without it ffmpeg
    // repeats frames to fill a steady rate. `-fps_mode` since ffmpeg 5.1;
    // an older one is asked again with the spelling it knows.
    Run ran = run("ffmpeg", arguments("-fps_mode"), limits, log);
    if (!ran.error.empty() && ran.tail.find("fps_mode") != std::string::npos) {
        times.clear();
        ran = run("ffmpeg", arguments("-vsync"), limits, log);
    }
    if (!ran.error.empty()) {
        error = ran.error;
        return {};
    }
    std::vector<ExtractedFrame> frames;
    for (std::size_t index = 0; index < times.size(); ++index) {
        std::array<char, 32> name{};
        std::snprintf(name.data(), name.size(), "f%05zu.jpg", index + 1);
        const std::filesystem::path path = directory / name.data();
        if (!std::filesystem::exists(path, code)) {
            break;
        }
        frames.push_back(ExtractedFrame{.seconds = options.start + times[index], .file = path});
    }
    if (frames.empty()) {
        error = "ffmpeg found no frames in it";
    }
    return frames;
}

std::string encode_video(const std::filesystem::path& directory, int fps,
                         const std::filesystem::path& output, const FfmpegLimits& limits,
                         std::string& error) {
    const auto arguments = [&](const std::vector<std::string>& codec) {
        std::vector<std::string> out{"-nostdin",
                                     "-hide_banner",
                                     "-loglevel",
                                     "error",
                                     "-y",
                                     "-framerate",
                                     std::to_string(fps),
                                     "-start_number",
                                     "1",
                                     "-i",
                                     (directory / "f%05d.png").string(),
                                     "-vf",
                                     "scale=trunc(iw/2)*2:trunc(ih/2)*2"};
        out.insert(out.end(), codec.begin(), codec.end());
        out.insert(out.end(), {"-movflags", "+faststart", output.string()});
        return out;
    };
    Run ran = run("ffmpeg", arguments({"-c:v", "libx264", "-pix_fmt", "yuv420p"}), limits);
    if (ran.error.empty()) {
        return "h264";
    }
    // A build without x264 says so by name; anything else is the failure.
    if (ran.tail.find("libx264") == std::string::npos) {
        error = ran.error;
        return {};
    }
    ran = run("ffmpeg", arguments({"-c:v", "mpeg4", "-q:v", "2", "-pix_fmt", "yuv420p"}), limits);
    if (!ran.error.empty()) {
        error = ran.error;
        return {};
    }
    return "mpeg4";
}

bool extract_last_frame(const std::filesystem::path& file, const std::filesystem::path& output,
                        const FfmpegLimits& limits, std::string& error) {
    const auto arguments = [&](bool tail) {
        std::vector<std::string> out{"-nostdin", "-hide_banner", "-loglevel", "error", "-y"};
        if (tail) {
            out.insert(out.end(), {"-sseof", "-1"});
        }
        out.insert(out.end(), {"-i", file.string(), "-an", "-update", "1", output.string()});
        return out;
    };
    Run ran = run("ffmpeg", arguments(true), limits);
    std::error_code code;
    if (!ran.error.empty() || !std::filesystem::is_regular_file(output, code)) {
        // Shorter than the second asked for: the whole clip, its last frame
        // the one left.
        ran = run("ffmpeg", arguments(false), limits);
    }
    if (!ran.error.empty()) {
        error = ran.error;
        return false;
    }
    if (!std::filesystem::is_regular_file(output, code)) {
        error = "ffmpeg wrote no frame from " + file.string();
        return false;
    }
    return true;
}

std::string decode_audio_wav(const std::filesystem::path& file, int rate, double start,
                             double length, const FfmpegLimits& limits, std::string& error) {
    std::vector<std::string> arguments{"-nostdin", "-hide_banner", "-loglevel", "error"};
    add_span(arguments, start, length);
    // Raw samples rather than a WAV on the pipe: ffmpeg cannot go back to
    // write a WAV header's sizes on a pipe, so the header is written here.
    arguments.insert(arguments.end(), {"-i", file.string(), "-vn", "-ac", "1", "-ar",
                                       std::to_string(rate), "-f", "s16le", "-"});
    const Run ran = run("ffmpeg", std::move(arguments), limits);
    if (!ran.error.empty()) {
        error = ran.error;
        return {};
    }
    if (ran.out.empty()) {
        return {};
    }
    return wav_file(ran.out, rate);
}

double WavAudio::seconds() const noexcept {
    const int frame = channels * (bits / 8);
    return rate > 0 && frame > 0 ? static_cast<double>(pcm.size()) / static_cast<double>(frame) /
                                       static_cast<double>(rate)
                                 : 0.0;
}

std::optional<WavAudio> read_wav(const std::filesystem::path& file, std::string& error) {
    std::ifstream in{file, std::ios::binary};
    const std::string bytes{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    const auto le = [&bytes](std::size_t at, std::size_t width) {
        std::uint32_t value = 0;
        for (std::size_t index = 0; index < width; ++index) {
            value |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + index]))
                     << (8U * index);
        }
        return value;
    };
    if (bytes.size() < 12 || bytes.compare(0, 4, "RIFF") != 0 || bytes.compare(8, 4, "WAVE") != 0) {
        error = file.string() + " is not a WAV file";
        return std::nullopt;
    }
    WavAudio audio;
    bool format = false;
    for (std::size_t at = 12; at + 8 <= bytes.size();) {
        const std::string id = bytes.substr(at, 4);
        const std::size_t size = le(at + 4, 4);
        const std::size_t body = at + 8;
        if (body + size > bytes.size()) {
            if (id == "data") {
                error = file.string() + " is cut short: its data chunk runs past its end";
                return std::nullopt;
            }
            break;
        }
        if (id == "fmt " && size >= 16) {
            if (le(body, 2) != 1) {
                error = file.string() + " is not PCM";
                return std::nullopt;
            }
            audio.channels = static_cast<int>(le(body + 2, 2));
            audio.rate = static_cast<int>(le(body + 4, 4));
            audio.bits = static_cast<int>(le(body + 14, 2));
            format = true;
        } else if (id == "data") {
            audio.pcm = bytes.substr(body, size);
            if (!format) {
                break;
            }
            return audio;
        }
        at = body + size + (size % 2);
    }
    error = file.string() + " holds no PCM format and data";
    return std::nullopt;
}

std::string wav_file(std::string_view pcm, int rate) {
    std::string out;
    out.reserve(44 + pcm.size());
    const auto u32 = [&out](std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) {
            out.push_back(static_cast<char>((value >> static_cast<unsigned>(shift)) & 0xFFU));
        }
    };
    const auto u16 = [&out](std::uint16_t value) {
        out.push_back(static_cast<char>(value & 0xFFU));
        out.push_back(static_cast<char>((value >> 8U) & 0xFFU));
    };
    const auto size = static_cast<std::uint32_t>(pcm.size() - pcm.size() % 2);
    const auto hz = static_cast<std::uint32_t>(rate);
    out += "RIFF";
    u32(36 + size);
    out += "WAVEfmt ";
    u32(16);
    u16(1);  // PCM
    u16(1);  // mono
    u32(hz);
    u32(hz * 2);  // bytes a second
    u16(2);       // bytes a frame
    u16(16);      // bits a sample
    out += "data";
    u32(size);
    out.append(pcm.substr(0, size));
    return out;
}

std::vector<double> showinfo_times(std::string_view log) {
    std::vector<double> out;
    constexpr std::string_view kKey = "pts_time:";
    for (std::size_t at = log.find(kKey); at != std::string_view::npos;
         at = log.find(kKey, at + kKey.size())) {
        // Only showinfo's lines: ffmpeg's own progress can say pts_time too.
        const std::size_t line_start = log.rfind('\n', at);
        const std::string_view line =
            log.substr(line_start == std::string_view::npos ? 0 : line_start + 1,
                       at - (line_start == std::string_view::npos ? 0 : line_start + 1));
        if (line.find("showinfo") == std::string_view::npos) {
            continue;
        }
        const std::string value{log.substr(at + kKey.size(), 32)};
        char* end = nullptr;
        const double seconds = std::strtod(value.c_str(), &end);
        if (end != value.c_str() && std::isfinite(seconds)) {
            out.push_back(std::max(seconds, 0.0));
        }
    }
    return out;
}

// ── Assembly (50b) ──────────────────────────────────────────────────────────

namespace {

/// The head of every assembly run: quiet, never asking, writing over.
[[nodiscard]] std::vector<std::string> assembly_head() {
    return {"-nostdin", "-hide_banner", "-loglevel", "error", "-y"};
}

/// `seconds` for a message: `1.25 s`.
[[nodiscard]] std::string seconds_words(double seconds) {
    std::array<char, 32> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%.2f s", seconds);
    return buffer.data();
}

/// The side a `yuv420p` frame can have: even.
[[nodiscard]] int even_side(int side) noexcept {
    return side - side % 2;
}

/// The profile's video arguments: its codec, or its fallback.
[[nodiscard]] std::vector<std::string> video_codec_args(const EncodeProfile& profile,
                                                        bool fallback) {
    if (fallback) {
        return {"-c:v",     std::string{profile.fallback_codec},
                "-q:v",     std::to_string(profile.fallback_quality),
                "-pix_fmt", "yuv420p"};
    }
    return {"-c:v", std::string{profile.video_codec}, "-preset",  std::string{profile.preset},
            "-crf", std::to_string(profile.crf),      "-pix_fmt", "yuv420p"};
}

/// The profile's sound arguments.
[[nodiscard]] std::vector<std::string> audio_codec_args(const EncodeProfile& profile) {
    return {"-c:a", std::string{profile.audio_codec},   "-b:a", std::string{profile.audio_bitrate},
            "-ar",  std::to_string(profile.sample_rate)};
}

/// The sound as the profile writes it, in words.
[[nodiscard]] std::string audio_words(const EncodeProfile& profile) {
    return std::string{profile.audio_codec} + " " + std::string{profile.audio_bitrate} + " at " +
           std::to_string(profile.sample_rate) + " Hz, stereo";
}

/// An encode by the profile, in words, then what became of the sound.
[[nodiscard]] std::string encode_words(const EncodeProfile& profile, bool fallback,
                                       std::string_view sound) {
    std::string out = "the " + std::string{profile.name} + " profile: ";
    out += fallback ? std::string{profile.fallback_codec} + " at quality " +
                          std::to_string(profile.fallback_quality) + " (this ffmpeg has no " +
                          std::string{profile.video_codec} + ")"
                    : std::string{profile.video_format} + " at crf " + std::to_string(profile.crf) +
                          ", preset " + std::string{profile.preset};
    return out + ", yuv420p; " + std::string{sound};
}

/// The audio format every assembled track is brought to.
[[nodiscard]] std::string stereo_format(const EncodeProfile& profile) {
    return "aformat=sample_rates=" + std::to_string(profile.sample_rate) +
           ":channel_layouts=stereo";
}

/// Whether an mp4 holds the codec as it is.
[[nodiscard]] bool mp4_holds(std::string_view codec) noexcept {
    for (const std::string_view held : {"h264", "hevc", "mpeg4", "av1", "aac", "mp3", "alac"}) {
        if (codec == held) {
            return true;
        }
    }
    return false;
}

/// `text` with each character in `special`, and whitespace, escaped by a
/// backslash -- one level of ffmpeg's option and graph parsing.
[[nodiscard]] std::string backslash_escaped(std::string_view text, std::string_view special) {
    std::string out;
    out.reserve(text.size() * 2);
    for (const char c : text) {
        if (special.find(c) != std::string_view::npos || c == ' ' || c == '\t' || c == '\n' ||
            c == '\r') {
            out += '\\';
        }
        out += c;
    }
    return out;
}

/// `output` removed, and `why` the error: nothing half-written is left.
void failed(const std::filesystem::path& output, std::string why, std::string& error) {
    std::error_code code;
    std::filesystem::remove(output, code);
    error = std::move(why);
}

/// `inputs` probed, each named in its error.
[[nodiscard]] std::optional<std::vector<ClipFacts>> probe_all(
    const std::vector<std::filesystem::path>& inputs, const FfmpegLimits& limits,
    std::string& error) {
    std::vector<ClipFacts> out;
    out.reserve(inputs.size());
    for (const std::filesystem::path& input : inputs) {
        std::string why;
        const std::optional<MediaInfo> info = probe_media(input, limits, why);
        if (!info.has_value()) {
            error = "could not read " + input.string() + ": " + why;
            return std::nullopt;
        }
        out.push_back(ClipFacts{.file = input, .info = *info});
    }
    return out;
}

/// An encode by the profile: `arguments(false)` first, then -- when this
/// ffmpeg names the profile's encoder missing -- `arguments(true)` for the
/// fallback. Whether the fallback ran, or nullopt with `error` filled
/// (`explain` may word a failure) and `output` removed.
[[nodiscard]] std::optional<bool> run_encode(
    const std::function<std::vector<std::string>(bool)>& arguments, const EncodeProfile& profile,
    const std::filesystem::path& output, const FfmpegLimits& limits, std::string& error,
    const std::function<std::string(const std::string&)>& explain = {}) {
    bool fallback = false;
    Run ran = run("ffmpeg", arguments(false), limits);
    if (!ran.error.empty() &&
        ran.tail.find(std::string{profile.video_codec}) != std::string::npos) {
        fallback = true;
        ran = run("ffmpeg", arguments(true), limits);
    }
    if (!ran.error.empty()) {
        std::string why = explain ? explain(ran.tail) : std::string{};
        failed(output, why.empty() ? ran.error : std::move(why), error);
        return std::nullopt;
    }
    return fallback;
}

}  // namespace

std::string concat_problem(const std::vector<ClipFacts>& clips, double crossfade) {
    if (clips.empty()) {
        return "there are no clips to join";
    }
    if (!std::isfinite(crossfade) || crossfade < 0.0) {
        return "a crossfade is 0 seconds or more";
    }
    for (const ClipFacts& clip : clips) {
        if (!clip.info.video) {
            return clip.file.string() + " has no picture";
        }
        if (clip.info.width < 2 || clip.info.height < 2) {
            return clip.file.string() + " states no picture size";
        }
        if (!(clip.info.duration > 0.0)) {
            return clip.file.string() + " states no length";
        }
        if (clips.size() > 1 && crossfade * 2.0 > clip.info.duration) {
            return "a " + seconds_words(crossfade) + " crossfade at each end does not fit in " +
                   clip.file.string() + " (" + seconds_words(clip.info.duration) +
                   ") -- a shorter crossfade, or 0 for a cut";
        }
    }
    return {};
}

bool concat_can_copy(const std::vector<ClipFacts>& clips, double crossfade) {
    if (clips.empty() || (clips.size() > 1 && crossfade > 0.0)) {
        return false;
    }
    const MediaInfo& first = clips.front().info;
    for (const ClipFacts& clip : clips) {
        const MediaInfo& info = clip.info;
        if (!mp4_holds(info.video_codec) || info.video_codec != first.video_codec ||
            info.width != first.width || info.height != first.height || !(info.fps > 0.0) ||
            std::abs(info.fps - first.fps) > 1e-3 || info.audio != first.audio) {
            return false;
        }
        if (info.audio &&
            (!mp4_holds(info.audio_codec) || info.audio_codec != first.audio_codec ||
             info.sample_rate != first.sample_rate || info.channels != first.channels)) {
            return false;
        }
    }
    return true;
}

std::string concat_list(const std::vector<ClipFacts>& clips) {
    std::string out;
    for (const ClipFacts& clip : clips) {
        std::string path;
        for (const char c : clip.file.string()) {
            path += c == '\'' ? std::string{"'\\''"} : std::string(1, c);
        }
        out += "file '" + path + "'\n";
    }
    return out;
}

std::string concat_filter(const std::vector<ClipFacts>& clips, double crossfade,
                          const EncodeProfile& profile) {
    if (clips.empty()) {
        return {};
    }
    const MediaInfo& first = clips.front().info;
    const std::string width = std::to_string(even_side(first.width));
    const std::string height = std::to_string(even_side(first.height));
    const std::string fps = time_argument(first.fps > 0.0 ? first.fps : profile.fps);
    const bool sound =
        std::ranges::any_of(clips, [](const ClipFacts& clip) { return clip.info.audio; });
    const std::size_t count = clips.size();
    const bool one = count == 1;

    std::vector<std::string> parts;
    for (std::size_t index = 0; index < count; ++index) {
        const std::string at = std::to_string(index);
        parts.push_back("[" + at + ":v]scale=" + width + ":" + height +
                        ":force_original_aspect_ratio=decrease,pad=" + width + ":" + height +
                        ":(ow-iw)/2:(oh-ih)/2,setsar=1,fps=" + fps + ",format=yuv420p[" +
                        (one ? "vout" : "v" + at) + "]");
        if (sound) {
            // A clip without sound is as long a silence, so the sound stays
            // with its pictures.
            const std::string length = time_argument(clips[index].info.duration);
            const std::string label = "[" + (one ? std::string{"aout"} : "a" + at) + "]";
            parts.push_back(clips[index].info.audio
                                ? "[" + at + ":a]" + stereo_format(profile) +
                                      ",apad,atrim=duration=" + length + label
                                : "anullsrc=r=" + std::to_string(profile.sample_rate) +
                                      ":cl=stereo,atrim=duration=" + length + label);
        }
    }
    if (!one && crossfade > 0.0) {
        const std::string fade = time_argument(crossfade);
        std::string video = "v0";
        std::string audio = "a0";
        double offset = 0.0;
        for (std::size_t index = 1; index < count; ++index) {
            const std::string at = std::to_string(index);
            const bool last = index + 1 == count;
            // Each fade starts `crossfade` before the joined part ends.
            offset += clips[index - 1].info.duration - crossfade;
            const std::string next_video = last ? "vout" : "x" + at;
            parts.push_back("[" + video + "][v" + at + "]xfade=transition=fade:duration=" + fade +
                            ":offset=" + time_argument(offset) + "[" + next_video + "]");
            video = next_video;
            if (sound) {
                const std::string next_audio = last ? "aout" : "y" + at;
                parts.push_back("[" + audio + "][a" + at + "]acrossfade=d=" + fade + "[" +
                                next_audio + "]");
                audio = next_audio;
            }
        }
    } else if (!one) {
        std::string inputs;
        for (std::size_t index = 0; index < count; ++index) {
            const std::string at = std::to_string(index);
            inputs += "[v" + at + "]" + (sound ? "[a" + at + "]" : std::string{});
        }
        parts.push_back(inputs + "concat=n=" + std::to_string(count) +
                        ":v=1:a=" + (sound ? "1" : "0") + "[vout]" + (sound ? "[aout]" : ""));
    }
    std::string out;
    for (const std::string& part : parts) {
        out += (out.empty() ? "" : ";") + part;
    }
    return out;
}

std::vector<std::string> concat_args(const std::vector<ClipFacts>& clips, double crossfade,
                                     const std::filesystem::path& output, bool fallback,
                                     const EncodeProfile& profile) {
    std::vector<std::string> out = assembly_head();
    for (const ClipFacts& clip : clips) {
        out.insert(out.end(), {"-i", clip.file.string()});
    }
    const bool sound =
        std::ranges::any_of(clips, [](const ClipFacts& clip) { return clip.info.audio; });
    out.insert(out.end(),
               {"-filter_complex", concat_filter(clips, crossfade, profile), "-map", "[vout]"});
    if (sound) {
        out.insert(out.end(), {"-map", "[aout]"});
    }
    const std::vector<std::string> video = video_codec_args(profile, fallback);
    out.insert(out.end(), video.begin(), video.end());
    if (sound) {
        const std::vector<std::string> audio = audio_codec_args(profile);
        out.insert(out.end(), audio.begin(), audio.end());
    } else {
        out.emplace_back("-an");
    }
    out.insert(out.end(), {"-movflags", "+faststart", output.string()});
    return out;
}

std::vector<std::string> concat_copy_args(const std::filesystem::path& list,
                                          const std::filesystem::path& output) {
    std::vector<std::string> out = assembly_head();
    out.insert(out.end(), {"-f", "concat", "-safe", "0", "-i", list.string(), "-c", "copy",
                           "-movflags", "+faststart", output.string()});
    return out;
}

std::optional<Assembled> concat_clips(const std::vector<std::filesystem::path>& clips,
                                      double crossfade, const std::filesystem::path& output,
                                      const FfmpegLimits& limits, std::string& error) {
    const std::optional<std::vector<ClipFacts>> facts = probe_all(clips, limits, error);
    if (!facts.has_value()) {
        return std::nullopt;
    }
    if (std::string problem = concat_problem(*facts, crossfade); !problem.empty()) {
        error = std::move(problem);
        return std::nullopt;
    }
    if (concat_can_copy(*facts, crossfade)) {
        // The demuxer's list beside the output, gone with the run.
        const std::filesystem::path list =
            output.parent_path() / ("." + output.filename().string() + ".concat.txt");
        {
            std::ofstream out{list, std::ios::binary | std::ios::trunc};
            out << concat_list(*facts);
            if (!out) {
                error = "could not write " + list.string();
                return std::nullopt;
            }
        }
        const Run ran = run("ffmpeg", concat_copy_args(list, output), limits);
        std::error_code code;
        std::filesystem::remove(list, code);
        if (!ran.error.empty()) {
            failed(output, ran.error, error);
            return std::nullopt;
        }
        return Assembled{.codec = "copy",
                         .settings = "the streams copied as they were -- no re-encode"};
    }
    const std::optional<bool> fallback = run_encode(
        [&](bool fallback_codec) { return concat_args(*facts, crossfade, output, fallback_codec); },
        kFinalRender, output, limits, error);
    if (!fallback.has_value()) {
        return std::nullopt;
    }
    const bool sound =
        std::ranges::any_of(*facts, [](const ClipFacts& clip) { return clip.info.audio; });
    return Assembled{
        .codec = std::string{*fallback ? kFinalRender.fallback_codec : kFinalRender.video_format},
        .settings =
            encode_words(kFinalRender, *fallback, sound ? audio_words(kFinalRender) : "no sound")};
}

std::string mix_problem(const ClipFacts& video, const AudioMix& mix) {
    if (!video.info.video) {
        return video.file.string() + " has no picture";
    }
    if (!(video.info.duration > 0.0)) {
        return video.file.string() + " states no length";
    }
    if (mix.narration.empty() && mix.bed.empty()) {
        return "there is nothing to mix -- no narration and no bed";
    }
    for (std::size_t index = 0; index < mix.narration.size(); ++index) {
        const double offset = mix.narration[index].offset;
        const std::string piece = "narration piece " + std::to_string(index + 1);
        if (!std::isfinite(offset) || offset < 0.0) {
            return piece + " starts before the video does";
        }
        if (offset >= video.info.duration) {
            return piece + " starts at " + seconds_words(offset) + ", past the video's end at " +
                   seconds_words(video.info.duration);
        }
    }
    return {};
}

std::string mix_filter(const ClipFacts& video, const AudioMix& mix, const EncodeProfile& profile) {
    const std::string format = stereo_format(profile);
    const std::string length = time_argument(video.info.duration);
    std::vector<std::string> parts;

    std::vector<std::string> pieces;
    for (std::size_t index = 0; index < mix.narration.size(); ++index) {
        const std::string delay =
            std::to_string(std::llround(std::max(mix.narration[index].offset, 0.0) * 1000.0));
        const std::string label = "n" + std::to_string(index);
        parts.push_back("[" + std::to_string(index + 1) + ":a]" + format + ",adelay=" + delay +
                        "|" + delay + "[" + label + "]");
        pieces.push_back(label);
    }
    // Mixed at their own levels: `normalize=0`, or every piece would be
    // quieter for the others' silence.
    const auto amix = [](const std::vector<std::string>& labels) {
        std::string out;
        for (const std::string& label : labels) {
            out += "[" + label + "]";
        }
        return out + "amix=inputs=" + std::to_string(labels.size()) +
               ":duration=longest:normalize=0";
    };
    std::string voice;
    if (pieces.size() == 1) {
        voice = pieces.front();
    } else if (pieces.size() > 1) {
        parts.push_back(amix(pieces) + "[voice]");
        voice = "voice";
    }

    std::vector<std::string> mixed;
    if (video.info.audio) {
        parts.push_back("[0:a]" + format + "[orig]");
        mixed.emplace_back("orig");
    }
    if (!mix.bed.empty()) {
        parts.push_back("[" + std::to_string(mix.narration.size() + 1) + ":a]" + format +
                        ",volume=" + time_argument(kBedVolume) + ",atrim=duration=" + length +
                        "[bedraw]");
        if (voice.empty()) {
            mixed.emplace_back("bedraw");
        } else {
            // The narration keys the compressor that ducks the bed.
            parts.push_back("[" + voice + "]asplit=2[voicemix][key]");
            parts.push_back("[bedraw][key]" + std::string{kDuckFilter} + "[bed]");
            mixed.emplace_back("voicemix");
            mixed.emplace_back("bed");
        }
    } else if (!voice.empty()) {
        mixed.push_back(voice);
    }
    const std::string tail = "apad,atrim=duration=" + length + "[aout]";
    parts.push_back(mixed.size() == 1 ? "[" + mixed.front() + "]" + tail
                                      : amix(mixed) + "," + tail);

    std::string out;
    for (const std::string& part : parts) {
        out += (out.empty() ? "" : ";") + part;
    }
    return out;
}

std::vector<std::string> mix_args(const ClipFacts& video, const AudioMix& mix,
                                  const std::filesystem::path& output,
                                  const EncodeProfile& profile) {
    std::vector<std::string> out = assembly_head();
    out.insert(out.end(), {"-i", video.file.string()});
    for (const NarrationPiece& piece : mix.narration) {
        out.insert(out.end(), {"-i", piece.file.string()});
    }
    if (!mix.bed.empty()) {
        // Looped, so a short bed lasts the video; the graph cuts it there.
        out.insert(out.end(), {"-stream_loop", "-1", "-i", mix.bed.string()});
    }
    out.insert(out.end(), {"-filter_complex", mix_filter(video, mix, profile), "-map", "0:v:0",
                           "-map", "[aout]", "-c:v", "copy"});
    const std::vector<std::string> audio = audio_codec_args(profile);
    out.insert(out.end(), audio.begin(), audio.end());
    out.insert(out.end(), {"-movflags", "+faststart", output.string()});
    return out;
}

std::optional<Assembled> mix_audio(const std::filesystem::path& video, const AudioMix& mix,
                                   const std::filesystem::path& output, const FfmpegLimits& limits,
                                   std::string& error) {
    std::vector<std::filesystem::path> inputs{video};
    for (const NarrationPiece& piece : mix.narration) {
        inputs.push_back(piece.file);
    }
    if (!mix.bed.empty()) {
        inputs.push_back(mix.bed);
    }
    const std::optional<std::vector<ClipFacts>> facts = probe_all(inputs, limits, error);
    if (!facts.has_value()) {
        return std::nullopt;
    }
    if (std::string problem = mix_problem(facts->front(), mix); !problem.empty()) {
        error = std::move(problem);
        return std::nullopt;
    }
    for (std::size_t index = 1; index < facts->size(); ++index) {
        if (!(*facts)[index].info.audio) {
            error = (*facts)[index].file.string() + " has no sound";
            return std::nullopt;
        }
    }
    const Run ran = run("ffmpeg", mix_args(facts->front(), mix, output), limits);
    if (!ran.error.empty()) {
        failed(output, ran.error, error);
        return std::nullopt;
    }
    std::string what;
    if (!mix.narration.empty()) {
        what = "narration (" + std::to_string(mix.narration.size()) + " piece" +
               (mix.narration.size() == 1 ? "" : "s") + ")";
    }
    if (!mix.bed.empty()) {
        what += what.empty() ? "the bed" : " over the bed, ducked";
    }
    if (facts->front().info.audio) {
        what += ", with the video's own sound";
    }
    return Assembled{
        .codec = "copy",
        .settings = "the picture copied as it was; " + audio_words(kFinalRender) + ": " + what};
}

std::string filter_path(const std::filesystem::path& path) {
    return backslash_escaped(backslash_escaped(path.string(), "\\':"), "\\'[],;");
}

std::vector<std::string> burn_args(const std::filesystem::path& video,
                                   const std::filesystem::path& captions,
                                   const std::filesystem::path& output, bool fallback,
                                   const EncodeProfile& profile) {
    std::vector<std::string> out = assembly_head();
    out.insert(out.end(),
               {"-i", video.string(), "-vf", "subtitles=filename=" + filter_path(captions)});
    const std::vector<std::string> codec = video_codec_args(profile, fallback);
    out.insert(out.end(), codec.begin(), codec.end());
    out.insert(out.end(), {"-c:a", "copy", "-movflags", "+faststart", output.string()});
    return out;
}

std::optional<Assembled> burn_captions(const std::filesystem::path& video,
                                       const std::filesystem::path& captions,
                                       const std::filesystem::path& output,
                                       const FfmpegLimits& limits, std::string& error) {
    std::error_code code;
    if (!std::filesystem::is_regular_file(captions, code)) {
        error = "there are no captions at " + captions.string();
        return std::nullopt;
    }
    const std::optional<std::vector<ClipFacts>> facts = probe_all({video}, limits, error);
    if (!facts.has_value()) {
        return std::nullopt;
    }
    if (!facts->front().info.video) {
        error = video.string() + " has no picture";
        return std::nullopt;
    }
    const std::optional<bool> fallback = run_encode(
        [&](bool fallback_codec) { return burn_args(video, captions, output, fallback_codec); },
        kFinalRender, output, limits, error,
        [](const std::string& tail) {
            return tail.find("No such filter: 'subtitles'") != std::string::npos
                       ? std::string{"this ffmpeg cannot burn captions: it has no subtitles "
                                     "filter (it was built without libass) -- an ffmpeg "
                                     "built with libass burns them"}
                       : std::string{};
        });
    if (!fallback.has_value()) {
        return std::nullopt;
    }
    return Assembled{
        .codec = std::string{*fallback ? kFinalRender.fallback_codec : kFinalRender.video_format},
        .settings =
            encode_words(kFinalRender, *fallback,
                         facts->front().info.audio ? "the sound copied as it was" : "no sound") +
            "; the captions burned in"};
}

bool filter_listed(std::string_view listing, std::string_view name) {
    while (!listing.empty()) {
        const std::size_t end = listing.find('\n');
        const std::string_view line = listing.substr(0, end);
        listing = end == std::string_view::npos ? std::string_view{} : listing.substr(end + 1);
        // ` TS. name  A->A  Description`: flags, the name, its pads.
        std::vector<std::string_view> words;
        for (std::size_t at = 0; at < line.size() && words.size() < 3;) {
            at = line.find_first_not_of(" \t\r", at);
            if (at == std::string_view::npos) {
                break;
            }
            const std::size_t stop = std::min(line.find_first_of(" \t\r", at), line.size());
            words.push_back(line.substr(at, stop - at));
            at = stop;
        }
        if (words.size() == 3 && words[2].find("->") != std::string_view::npos &&
            words[1] == name) {
            return true;
        }
    }
    return false;
}

bool ffmpeg_has_filter(std::string_view name, const FfmpegLimits& limits) {
    const Run ran = run("ffmpeg", {"-hide_banner", "-filters"}, limits);
    return ran.error.empty() && filter_listed(ran.out, name);
}

bool encoder_listed(std::string_view listing, std::string_view name) {
    while (!listing.empty()) {
        const std::size_t end = listing.find('\n');
        const std::string_view line = listing.substr(0, end);
        listing = end == std::string_view::npos ? std::string_view{} : listing.substr(end + 1);
        // ` V....D libx264  libx264 H.264 …`: six flag columns, then the name.
        const std::size_t flags = line.find_first_not_of(" \t");
        if (flags == std::string_view::npos) {
            continue;
        }
        const std::size_t gap = line.find_first_of(" \t", flags);
        if (gap == std::string_view::npos || gap - flags != 6) {
            continue;
        }
        const std::size_t at = line.find_first_not_of(" \t", gap);
        if (at == std::string_view::npos) {
            continue;
        }
        const std::size_t stop = std::min(line.find_first_of(" \t\r", at), line.size());
        if (line.substr(at, stop - at) == name) {
            return true;
        }
    }
    return false;
}

std::optional<std::string> ffmpeg_listing(std::string_view flag, const FfmpegLimits& limits,
                                          std::string& error) {
    const Run ran = run("ffmpeg", {"-hide_banner", std::string{flag}}, limits);
    if (!ran.error.empty()) {
        error = ran.error;
        return std::nullopt;
    }
    return ran.out;
}

}  // namespace apogee::platform
