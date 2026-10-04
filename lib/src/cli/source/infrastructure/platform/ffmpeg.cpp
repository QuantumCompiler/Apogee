#include "platform/ffmpeg.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

}  // namespace

std::string ffmpeg_path() {
    return find_on_path("ffmpeg");
}

std::string ffprobe_path() {
    return find_on_path("ffprobe");
}

std::optional<MediaInfo> probe_media(const std::filesystem::path& file, const FfmpegLimits& limits,
                                     std::string& error) {
    const Run probed =
        run("ffprobe",
            {"-v", "error", "-show_entries", "format=duration:stream=codec_type,width,height",
             "-of", "default=noprint_wrappers=1", file.string()},
            limits);
    if (!probed.error.empty()) {
        error = probed.error;
        return std::nullopt;
    }
    MediaInfo info;
    std::string_view rest = probed.out;
    while (!rest.empty()) {
        const std::size_t end = rest.find('\n');
        std::string_view line = rest.substr(0, end);
        rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end + 1);
        while (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) {
            continue;
        }
        const std::string_view key = line.substr(0, equals);
        const std::string value{line.substr(equals + 1)};
        if (key == "codec_type") {
            info.video = info.video || value == "video";
            info.audio = info.audio || value == "audio";
        } else if (key == "width" && info.width == 0) {
            info.width = std::atoi(value.c_str());
        } else if (key == "height" && info.height == 0) {
            info.height = std::atoi(value.c_str());
        } else if (key == "duration" && value != "N/A") {
            const double seconds = std::strtod(value.c_str(), nullptr);
            info.duration = std::isfinite(seconds) && seconds > 0.0 ? seconds : 0.0;
        }
    }
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

}  // namespace apogee::platform
