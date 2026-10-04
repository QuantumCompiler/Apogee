#include "agentloop/media.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <system_error>
#include <utility>

#include "contracts/errors.h"
#include "contracts/sha256.h"
#include "harness/roles.h"
#include "platform/ffmpeg.h"

namespace apogee::agentloop {

namespace {

constexpr std::string_view kBase64Alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/// A description's budget: an image's text copied out can be long.
constexpr std::int64_t kDescribeTokens = 1024;
/// A frame's: one moment of a recording, read as many times as it has frames.
constexpr std::int64_t kFrameTokens = 384;
/// Thirty seconds of speech is well under this.
constexpr std::int64_t kTranscribeTokens = 1024;
/// The rate audio is decoded to when the model cannot say its own yet.
constexpr int kDefaultSampleRate = 16000;
/// The most frames a timeline's extraction writes before thinning: a video
/// that changes scene every second must not fill the disk.
constexpr std::size_t kMostFramesExtracted = 4 * kMaxTimelineFrames;

constexpr std::string_view kDescribePrompt =
    "Describe it for someone who cannot see it. First say what it shows, in a sentence or two: "
    "the scene or the application on screen, its layout and colours, any people and objects. "
    "Then copy every piece of text in it exactly as written -- labels, numbers, names, code, "
    "anything on a screen. Plain prose, no preamble.";

constexpr std::string_view kTranscribePrompt =
    "Transcribe this audio exactly, word for word. Reply with the transcript alone. If nothing "
    "is said, reply with nothing.";

[[nodiscard]] std::string lowercase(std::string text) {
    std::ranges::transform(text, text.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

[[nodiscard]] std::string trimmed(std::string_view text) {
    const std::size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) {
        return {};
    }
    const std::size_t end = text.find_last_not_of(" \t\r\n");
    return std::string{text.substr(begin, end - begin + 1)};
}

/// `text` on one line: a timeline entry is one, so a chunk boundary falls
/// between entries and each line's stamp dates all of it.
[[nodiscard]] std::string one_line(std::string_view text) {
    std::string out;
    bool space = false;
    for (const char c : trimmed(text)) {
        if (c == '\n' || c == '\r' || c == '\t' || c == ' ') {
            space = true;
            continue;
        }
        if (space && !out.empty()) {
            out.push_back(' ');
        }
        space = false;
        out.push_back(c);
    }
    return out;
}

[[nodiscard]] platform::FfmpegLimits limits_for(const harness::CancellationToken& cancellation) {
    platform::FfmpegLimits limits;
    limits.stopped = [cancellation] { return cancellation.stop_requested(); };
    return limits;
}

/// Asks `harness` and returns the reply's text, trimmed; empty with `error`
/// filled when the call failed. Cancellation propagates.
[[nodiscard]] std::string ask(const harness::Harness& harness, const harness::ChatRequest& request,
                              const harness::CancellationToken& cancellation, std::string& error) {
    try {
        return trimmed(harness.chat(request, cancellation).message.content.plain_text());
    } catch (const harness::CancelledError&) {
        throw;
    } catch (const std::exception& e) {
        error = e.what();
        return {};
    }
}

/// A reply's budget when the model reasons first.
constexpr std::int64_t kReasonedTokens = 4096;

/// Asks `request` with the model's reasoning skipped, unless `reasoning`; a
/// reply that comes back empty that way is asked again with it on, and
/// `reasoning` keeps it on for the rest of the file. Gemma 4 answers nothing
/// about audio with its thinking switched off (found on real weights), where
/// Qwen describes faster without it.
[[nodiscard]] std::string ask_media(const harness::Harness& harness, harness::ChatRequest request,
                                    bool& reasoning, const harness::CancellationToken& cancellation,
                                    std::string& error) {
    if (reasoning) {
        request.thinking.mode = harness::ThinkingMode::On;
        request.max_tokens =
            std::max<std::int64_t>(request.max_tokens.value_or(0), kReasonedTokens);
        return ask(harness, request, cancellation, error);
    }
    std::string reply = ask(harness, request, cancellation, error);
    if (reply.empty() && error.empty()) {
        reasoning = true;
        return ask_media(harness, std::move(request), reasoning, cancellation, error);
    }
    return reply;
}

/// Why audio and video need what is missing, or empty.
[[nodiscard]] std::string ffmpeg_missing() {
    if (!platform::ffmpeg_path().empty() && !platform::ffprobe_path().empty()) {
        return {};
    }
    return "audio and video need ffmpeg (and its ffprobe) on PATH, which was not found -- "
           "install ffmpeg";
}

/// `path` as a `data:` URI: as it is when the vendors take its format, else
/// converted to JPEG by ffmpeg (HEIC, TIFF).
[[nodiscard]] std::string image_data_uri(const std::filesystem::path& path,
                                         const std::filesystem::path& scratch,
                                         const harness::CancellationToken& cancellation,
                                         std::string& error) {
    if (const std::string type = image_media_type(path); !type.empty()) {
        const std::string bytes = read_file(path);
        if (bytes.empty()) {
            error = "empty or unreadable";
            return {};
        }
        return "data:" + type + ";base64," + base64_encode(bytes);
    }
    if (std::string missing = ffmpeg_missing(); !missing.empty()) {
        error = "this image format is read through ffmpeg: " + missing;
        return {};
    }
    platform::FrameOptions still;
    still.every = 1.0e6;  // the first frame alone
    still.scene = 0.0;
    still.max_frames = 1;
    still.max_side = 2048;
    const std::vector<platform::ExtractedFrame> frames =
        platform::extract_frames(path, still, scratch / "convert", limits_for(cancellation), error);
    if (frames.empty()) {
        return {};
    }
    return "data:image/jpeg;base64," + base64_encode(read_file(frames.front().file));
}

/// One transcript window.
struct Said {
    double start = 0.0;
    double end = 0.0;
    std::string text;
};

/// A track's transcript, and how its windows went.
struct Transcript {
    std::vector<Said> said;
    int windows = 0;
    int failed = 0;
    std::string why;
};

/// `path`'s sound transcribed by `job.readers.transcriber`, a window at a
/// time; each window that fails is a note, never the end of the rest.
[[nodiscard]] Transcript transcribe_track(const std::filesystem::path& path, double duration,
                                          const MediaJob& job, std::vector<std::string>& notes) {
    Transcript transcript;
    std::vector<Said>& out = transcript.said;
    const std::string& transcriber = job.readers.transcriber;
    const std::string name = path.filename().string();
    int& failed = transcript.failed;
    std::string& why = transcript.why;
    bool reasoning = false;
    for (double start = 0.0; start < duration; start += kTranscriptWindow) {
        job.cancellation.throw_if_cancelled();
        ++transcript.windows;
        const double length = std::min(kTranscriptWindow, duration - start);
        if (job.status) {
            job.status("transcribing " + name + ": " + clock_time(start) + " of " +
                       clock_time(duration) + " with " + transcriber);
        }
        // The model's own rate, once it is loaded to say it (26e).
        const int said_rate = job.harness->audio_sample_rate(transcriber);
        const int rate = said_rate > 0 ? said_rate : kDefaultSampleRate;
        std::string error;
        const std::string wav = platform::decode_audio_wav(path, rate, start, length,
                                                           limits_for(job.cancellation), error);
        job.cancellation.throw_if_cancelled();
        if (!error.empty()) {
            ++failed;
            why = error;
            continue;
        }
        if (wav.empty()) {
            continue;
        }
        const std::string text = ask_media(*job.harness, transcribe_request(transcriber, wav),
                                           reasoning, job.cancellation, error);
        if (!error.empty()) {
            ++failed;
            why = error;
            continue;
        }
        if (!text.empty()) {
            out.push_back(Said{.start = start, .end = start + length, .text = one_line(text)});
        }
    }
    if (failed > 0 && failed < transcript.windows) {
        notes.push_back(name + ": " + std::to_string(failed) + " of its " +
                        std::to_string(transcript.windows) + " " +
                        std::to_string(static_cast<int>(kTranscriptWindow)) +
                        "-second windows could not be transcribed (" + why + ")");
    }
    return transcript;
}

[[nodiscard]] std::string said_line(const Said& said) {
    return "[" + clock_time(said.start) + "–" + clock_time(said.end) + "] said: " + said.text;
}

[[nodiscard]] MediaText read_image(const std::filesystem::path& path, const MediaJob& job) {
    MediaText out;
    const std::string& describer = job.readers.describer;
    if (describer.empty()) {
        out.reason = "no model here reads images";
        return out;
    }
    std::string error;
    const std::string uri = image_data_uri(path, job.scratch, job.cancellation, error);
    if (uri.empty()) {
        out.reason = error.empty() ? "could not be read" : error;
        return out;
    }
    if (job.status) {
        job.status("describing " + path.filename().string() + " with " + describer);
    }
    bool reasoning = false;
    const std::string description = ask_media(*job.harness, describe_request(describer, uri, {}),
                                              reasoning, job.cancellation, error);
    if (!error.empty()) {
        out.reason = "describing it failed (" + error + ")";
        return out;
    }
    if (description.empty()) {
        out.reason = describer + " described nothing";
        return out;
    }
    out.text =
        "(An image. Its description, with its text copied as written:)\n\n" + description + "\n";
    out.reader = "vision: " + describer;
    return out;
}

[[nodiscard]] MediaText read_audio(const std::filesystem::path& path, const MediaJob& job) {
    MediaText out;
    if (job.readers.transcriber.empty()) {
        out.reason = "no model here hears audio";
        return out;
    }
    if (std::string missing = ffmpeg_missing(); !missing.empty()) {
        out.reason = std::move(missing);
        return out;
    }
    std::string error;
    const std::optional<platform::MediaInfo> info =
        platform::probe_media(path, limits_for(job.cancellation), error);
    if (!info.has_value() || !info->audio || info->duration <= 0.0) {
        out.reason = info.has_value() ? "no sound found in it" : error;
        return out;
    }
    out.duration = info->duration;
    const Transcript transcript = transcribe_track(path, info->duration, job, out.notes);
    if (transcript.windows > 0 && transcript.failed == transcript.windows) {
        // Not "nothing was said": that would be indexed, and copied to every
        // chat that attaches the same recording, as if it were true.
        out.reason = "transcribing it failed (" + transcript.why + ")";
        return out;
    }
    const std::vector<Said>& said = transcript.said;
    out.text = "(Audio, " + clock_time(info->duration) + " long, transcribed in " +
               std::to_string(static_cast<int>(kTranscriptWindow)) + "-second windows.)\n";
    if (said.empty()) {
        out.text += "Nothing was said in it.\n";
    }
    for (const Said& window : said) {
        out.text += said_line(window) + "\n";
    }
    out.reader = "transcription: " + job.readers.transcriber;
    return out;
}

/// `frames` thinned, evenly, to at most `most`.
[[nodiscard]] std::vector<platform::ExtractedFrame> thinned(
    std::vector<platform::ExtractedFrame> frames, std::size_t most) {
    if (frames.size() <= most || most == 0) {
        return frames;
    }
    std::vector<platform::ExtractedFrame> out;
    out.reserve(most);
    for (std::size_t index = 0; index < most; ++index) {
        out.push_back(frames[index * frames.size() / most]);
    }
    return out;
}

[[nodiscard]] MediaText read_video(const std::filesystem::path& path, const MediaJob& job) {
    MediaText out;
    const std::string name = path.filename().string();
    if (job.readers.describer.empty() && job.readers.transcriber.empty()) {
        out.reason = "no model here reads a video's frames or hears its sound";
        return out;
    }
    if (std::string missing = ffmpeg_missing(); !missing.empty()) {
        out.reason = std::move(missing);
        return out;
    }
    std::string error;
    const std::optional<platform::MediaInfo> info =
        platform::probe_media(path, limits_for(job.cancellation), error);
    if (!info.has_value()) {
        out.reason = error;
        return out;
    }
    out.duration = info->duration;

    // What was on screen.
    std::vector<std::pair<double, std::string>> screens;
    if (info->video && !job.readers.describer.empty()) {
        if (job.status) {
            job.status("finding the frames of " + name);
        }
        platform::FrameOptions options;
        options.every = std::max(kFrameEvery, info->duration / kMaxTimelineFrames);
        options.scene = kSceneChange;
        options.max_frames = kMostFramesExtracted;
        const std::vector<platform::ExtractedFrame> frames =
            thinned(platform::extract_frames(path, options, job.scratch / "frames",
                                             limits_for(job.cancellation), error),
                    kMaxTimelineFrames);
        job.cancellation.throw_if_cancelled();
        if (frames.empty()) {
            out.notes.push_back(name + ": its frames could not be read (" + error + ")");
        }
        int failed = 0;
        std::string why;
        bool reasoning = false;
        // A frame the same to the byte as one already described -- a slide
        // left up, a paused screen -- keeps its line in the timeline, so a
        // moment there is still found, but is not described again.
        std::map<std::string, std::string> described;
        for (std::size_t index = 0; index < frames.size(); ++index) {
            job.cancellation.throw_if_cancelled();
            const std::string bytes = read_file(frames[index].file);
            const std::string key = models::sha256_hex(bytes);
            if (const auto seen = described.find(key); seen != described.end()) {
                screens.emplace_back(frames[index].seconds, seen->second);
                continue;
            }
            if (job.status) {
                job.status("describing frame " + std::to_string(index + 1) + " of " +
                           std::to_string(frames.size()) + " of " + name + " with " +
                           job.readers.describer);
            }
            const std::string uri = "data:image/jpeg;base64," + base64_encode(bytes);
            std::string asked;
            const std::string description = ask_media(
                *job.harness,
                describe_request(job.readers.describer, uri,
                                 "one frame of a video, at " + clock_time(frames[index].seconds)),
                reasoning, job.cancellation, asked);
            if (!asked.empty() || description.empty()) {
                ++failed;
                why = asked.empty() ? "nothing described" : asked;
                continue;
            }
            screens.emplace_back(frames[index].seconds, one_line(description));
            described.emplace(key, screens.back().second);
        }
        if (failed > 0) {
            out.notes.push_back(name + ": " + std::to_string(failed) + " of its " +
                                std::to_string(frames.size()) + " frames could not be described (" +
                                why + ")");
        }
    } else if (info->video) {
        out.notes.push_back(name +
                            ": its frames were not described -- no model here reads images; set "
                            "one with 'apogee config set-default-vision'");
    }

    // What was said.
    std::vector<Said> said;
    if (info->audio && !job.readers.transcriber.empty()) {
        Transcript transcript = transcribe_track(path, info->duration, job, out.notes);
        if (transcript.windows > 0 && transcript.failed == transcript.windows) {
            out.notes.push_back(name + ": its sound could not be transcribed (" + transcript.why +
                                ")");
        }
        said = std::move(transcript.said);
    } else if (info->audio) {
        out.notes.push_back(name +
                            ": its sound was not transcribed -- no model here hears audio; set "
                            "one with 'apogee config set-default-transcription'");
    }

    if (screens.empty() && said.empty()) {
        out.reason = "nothing in it could be read";
        return out;
    }

    // In time order; at one moment, what was said opens before the screen.
    struct Entry {
        double at = 0.0;
        int order = 0;
        std::string line;
    };

    std::vector<Entry> entries;
    for (const auto& [at, description] : screens) {
        entries.push_back(
            Entry{.at = at, .order = 1, .line = "[" + clock_time(at) + "] screen: " + description});
    }
    for (const Said& window : said) {
        entries.push_back(Entry{.at = window.start, .order = 0, .line = said_line(window)});
    }
    std::ranges::stable_sort(entries, [](const Entry& a, const Entry& b) {
        return a.at != b.at ? a.at < b.at : a.order < b.order;
    });
    out.text = "(A video, " + clock_time(info->duration) +
               " long: what was on screen every few seconds and at each scene change, and what "
               "was said, in time order.)\n";
    for (const Entry& entry : entries) {
        out.text += entry.line + "\n";
    }
    // Which models built it: "timeline: X" when only the frames or only
    // the sound were read, "timeline: X and Y" when both.
    std::string by;
    if (!screens.empty()) {
        by = job.readers.describer;
    }
    if (!said.empty() && job.readers.transcriber != by) {
        by += (by.empty() ? "" : " and ") + job.readers.transcriber;
    }
    out.reader = "timeline: " + by;
    return out;
}

/// Parses a clock time at the front of `text` -- `m:ss` or `h:mm:ss` --
/// returning its seconds and how many bytes it took; nullopt when none.
[[nodiscard]] std::optional<std::pair<double, std::size_t>> clock_at(std::string_view text) {
    std::array<int, 3> parts{};
    std::size_t count = 0;
    std::size_t at = 0;
    while (count < parts.size()) {
        const std::size_t start = at;
        int value = 0;
        while (at < text.size() && std::isdigit(static_cast<unsigned char>(text[at])) != 0 &&
               at - start < 2) {
            value = value * 10 + (text[at] - '0');
            ++at;
        }
        if (at == start || (count > 0 && at - start != 2)) {
            break;
        }
        parts.at(count++) = value;
        if (at < text.size() && text[at] == ':' && at + 1 < text.size() &&
            std::isdigit(static_cast<unsigned char>(text[at + 1])) != 0) {
            ++at;
            continue;
        }
        break;
    }
    if (count < 2) {
        return std::nullopt;
    }
    if (parts.at(count - 1) >= 60 || (count == 3 && parts.at(1) >= 60)) {
        return std::nullopt;
    }
    const double seconds =
        count == 2 ? parts[0] * 60.0 + parts[1] : parts[0] * 3600.0 + parts[1] * 60.0 + parts[2];
    return std::pair{seconds, at};
}

/// The stamp a line opens with, `[m:ss]` or `[m:ss–m:ss]`; nullopt for none.
[[nodiscard]] std::optional<TimeSpan> stamp_of(std::string_view line) {
    if (!line.starts_with('[')) {
        return std::nullopt;
    }
    const auto first = clock_at(line.substr(1));
    if (!first.has_value()) {
        return std::nullopt;
    }
    std::string_view rest = line.substr(1 + first->second);
    TimeSpan span{.start = first->first, .end = first->first};
    for (const std::string_view dash : {std::string_view{"–"}, std::string_view{"-"}}) {
        if (rest.starts_with(dash)) {
            if (const auto second = clock_at(rest.substr(dash.size())); second.has_value()) {
                span.end = second->first;
                rest = rest.substr(dash.size() + second->second);
            }
            break;
        }
    }
    if (!rest.starts_with(']')) {
        return std::nullopt;
    }
    return span;
}

}  // namespace

std::optional<harness::Medium> medium_of(const std::filesystem::path& path) {
    const std::string extension = lowercase(path.extension().string());
    for (const std::string_view image :
         {".png", ".jpg", ".jpeg", ".gif", ".webp", ".bmp", ".tif", ".tiff", ".heic", ".heif"}) {
        if (extension == image) {
            return harness::Medium::Image;
        }
    }
    for (const std::string_view audio : {".wav", ".mp3", ".m4a", ".aac", ".flac", ".ogg", ".oga",
                                         ".opus", ".aiff", ".aif", ".wma"}) {
        if (extension == audio) {
            return harness::Medium::Audio;
        }
    }
    for (const std::string_view video :
         {".mp4", ".mov", ".m4v", ".mkv", ".webm", ".avi", ".wmv", ".mpg", ".mpeg"}) {
        if (extension == video) {
            return harness::Medium::Video;
        }
    }
    return std::nullopt;
}

std::string image_media_type(const std::filesystem::path& path) {
    // The set the cloud vendors actually accept. An unrecognised extension is
    // reported rather than guessed: the wire format needs an explicit media
    // type, and sending the wrong one fails with a far less clear message.
    static const std::array<std::pair<std::string_view, std::string_view>, 6> kTypes{{
        {".png", "image/png"},
        {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".gif", "image/gif"},
        {".webp", "image/webp"},
        {".bmp", "image/bmp"},
    }};
    const std::string extension = lowercase(path.extension().string());
    for (const auto& [candidate, media_type] : kTypes) {
        if (extension == candidate) {
            return std::string{media_type};
        }
    }
    return {};
}

std::string base64_encode(std::string_view bytes) {
    std::string out;
    out.reserve(((bytes.size() + 2) / 3) * 4);

    std::size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        const auto a = static_cast<unsigned char>(bytes[i]);
        const auto b = static_cast<unsigned char>(bytes[i + 1]);
        const auto c = static_cast<unsigned char>(bytes[i + 2]);
        const std::uint32_t triple = (static_cast<std::uint32_t>(a) << 16U) |
                                     (static_cast<std::uint32_t>(b) << 8U) |
                                     static_cast<std::uint32_t>(c);
        out.push_back(kBase64Alphabet[(triple >> 18U) & 0x3FU]);
        out.push_back(kBase64Alphabet[(triple >> 12U) & 0x3FU]);
        out.push_back(kBase64Alphabet[(triple >> 6U) & 0x3FU]);
        out.push_back(kBase64Alphabet[triple & 0x3FU]);
    }

    if (i < bytes.size()) {
        const auto a = static_cast<unsigned char>(bytes[i]);
        std::uint32_t triple = static_cast<std::uint32_t>(a) << 16U;
        const bool has_second = i + 1 < bytes.size();
        if (has_second) {
            triple |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i + 1])) << 8U;
        }
        out.push_back(kBase64Alphabet[(triple >> 18U) & 0x3FU]);
        out.push_back(kBase64Alphabet[(triple >> 12U) & 0x3FU]);
        out.push_back(has_second ? kBase64Alphabet[(triple >> 6U) & 0x3FU] : '=');
        out.push_back('=');
    }
    return out;
}

std::string clock_time(double seconds) {
    const auto whole = static_cast<std::int64_t>(std::floor(std::max(seconds, 0.0)));
    const std::int64_t hours = whole / 3600;
    const std::int64_t minutes = (whole % 3600) / 60;
    const std::int64_t rest = whole % 60;
    const auto two = [](std::int64_t value) {
        return (value < 10 ? "0" : "") + std::to_string(value);
    };
    if (hours > 0) {
        return std::to_string(hours) + ":" + two(minutes) + ":" + two(rest);
    }
    return std::to_string(minutes) + ":" + two(rest);
}

std::vector<double> moments_in(std::string_view question) {
    std::vector<double> out;
    for (std::size_t at = 0; at < question.size(); ++at) {
        if (std::isdigit(static_cast<unsigned char>(question[at])) == 0) {
            continue;
        }
        // At the start of a number only: "14:30" is one time, not "4:30" too.
        if (at > 0 && (std::isdigit(static_cast<unsigned char>(question[at - 1])) != 0 ||
                       question[at - 1] == ':')) {
            continue;
        }
        const auto clock = clock_at(question.substr(at));
        if (!clock.has_value()) {
            continue;
        }
        const std::size_t end = at + clock->second;
        if (end < question.size() &&
            (std::isdigit(static_cast<unsigned char>(question[end])) != 0 ||
             question[end] == ':')) {
            continue;
        }
        out.push_back(clock->first);
        at = end;
    }
    return out;
}

std::optional<TimeSpan> time_span(std::string_view text, std::size_t begin, std::size_t end) {
    begin = std::min(begin, text.size());
    end = std::min(std::max(end, begin), text.size());
    // From the line the span starts in, so a chunk opening mid-line is dated
    // by that line's stamp.
    const std::size_t line_start = begin == 0 ? 0 : text.rfind('\n', begin - 1);
    std::size_t at = line_start == std::string_view::npos || begin == 0 ? 0 : line_start + 1;
    std::optional<TimeSpan> out;
    while (at < end) {
        const std::size_t line_end = text.find('\n', at);
        const std::string_view line = text.substr(
            at, line_end == std::string_view::npos ? std::string_view::npos : line_end - at);
        if (const std::optional<TimeSpan> stamp = stamp_of(line); stamp.has_value()) {
            if (!out.has_value()) {
                out = stamp;
            } else {
                out->start = std::min(out->start, stamp->start);
                out->end = std::max(out->end, stamp->end);
            }
        }
        if (line_end == std::string_view::npos) {
            break;
        }
        at = line_end + 1;
    }
    return out;
}

MediaReaders media_readers(const harness::Harness& harness, const std::string& chat,
                           harness::Medium medium) {
    MediaReaders out;
    out.native = !chat.empty() && harness.can_read(chat, medium);
    // The role's own model when it can read the medium, else the chat's when
    // it can: a helper is used automatically (the user's call), and with none
    // set the chat model reads its own media (26e, default taken).
    const auto helper = [&](harness::ModelRole role, harness::Medium readable) -> std::string {
        const std::string key = harness::resolve_backend_key(
            harness.config(), harness::RoleRequest{.role = role, .conversation = chat});
        if (!key.empty() && harness.config().find_backend(key) != nullptr &&
            harness.can_read(key, readable)) {
            return key;
        }
        if (!chat.empty() && harness.can_read(chat, readable)) {
            return chat;
        }
        return {};
    };
    if (medium == harness::Medium::Image || medium == harness::Medium::Video) {
        out.describer = helper(harness::ModelRole::Vision, harness::Medium::Image);
    }
    if (medium == harness::Medium::Audio || medium == harness::Medium::Video) {
        out.transcriber = helper(harness::ModelRole::Transcription, harness::Medium::Audio);
    }
    return out;
}

harness::ChatRequest describe_request(const std::string& backend, const std::string& data_uri,
                                      std::string_view context) {
    harness::ChatRequest request;
    request.model = backend;
    // What it is opens the prompt: after it, on a line of its own, it read as
    // a cue to copy the text alone, and a frame lost its colours (found on
    // real weights).
    std::string prompt = "This is " + (context.empty() ? "an image" : std::string{context}) + ". " +
                         std::string{kDescribePrompt};
    // The picture before the words about it, as vision models were trained.
    request.messages.push_back(
        harness::ChatMessage{.role = harness::Role::User,
                             .content = harness::MessageContent::from_parts(
                                 {harness::ContentPart::from_image_url(data_uri),
                                  harness::ContentPart::from_text(std::move(prompt))})});
    request.max_tokens = context.empty() ? kDescribeTokens : kFrameTokens;
    request.temperature = 0.0;
    // Not a turn of the conversation: a local model reads it on its own
    // context, and the chat's cache is untouched.
    request.transient.side_request = true;
    request.thinking.mode = harness::ThinkingMode::Off;
    return request;
}

harness::ChatRequest transcribe_request(const std::string& backend, std::string_view wav) {
    harness::ChatRequest request;
    request.model = backend;
    request.messages.push_back(harness::ChatMessage{
        .role = harness::Role::User,
        .content = harness::MessageContent::from_parts(
            {harness::ContentPart::from_audio(base64_encode(wav), "wav"),
             harness::ContentPart::from_text(std::string{kTranscribePrompt})})});
    request.max_tokens = kTranscribeTokens;
    request.temperature = 0.0;
    request.transient.side_request = true;
    request.thinking.mode = harness::ThinkingMode::Off;
    return request;
}

MediaText read_media(const std::filesystem::path& path, harness::Medium medium,
                     const MediaJob& job) {
    if (job.harness == nullptr) {
        return MediaText{.reason = "no model to read it with"};
    }
    std::error_code code;
    std::filesystem::create_directories(job.scratch, code);
    MediaText out;
    switch (medium) {
        case harness::Medium::Image:
            out = read_image(path, job);
            break;
        case harness::Medium::Audio:
            out = read_audio(path, job);
            break;
        case harness::Medium::Video:
            out = read_video(path, job);
            break;
    }
    std::filesystem::remove_all(job.scratch, code);
    return out;
}

std::vector<harness::ContentPart> native_parts(const std::filesystem::path& path,
                                               harness::Medium medium, int sample_rate,
                                               bool with_sound,
                                               const std::filesystem::path& scratch,
                                               const harness::CancellationToken& cancellation,
                                               std::string& error) {
    std::error_code code;
    std::filesystem::create_directories(scratch, code);
    std::vector<harness::ContentPart> out;
    const int rate = sample_rate > 0 ? sample_rate : kDefaultSampleRate;
    if (medium == harness::Medium::Image) {
        const std::string uri = image_data_uri(path, scratch, cancellation, error);
        if (!uri.empty()) {
            out.push_back(harness::ContentPart::from_image_url(uri));
        }
    } else if (std::string missing = ffmpeg_missing(); !missing.empty()) {
        error = std::move(missing);
    } else if (medium == harness::Medium::Audio) {
        const std::string wav = platform::decode_audio_wav(path, rate, 0.0, kNativeSeconds,
                                                           limits_for(cancellation), error);
        if (!wav.empty()) {
            out.push_back(harness::ContentPart::from_audio(base64_encode(wav), "wav"));
        } else if (error.empty()) {
            error = "no sound found in it";
        }
    } else {
        platform::FrameOptions options;
        options.fps = kNativeFramesPerSecond;
        options.max_side = kNativeFrameSide;
        options.length = kNativeSeconds;
        options.max_frames = static_cast<std::size_t>(kNativeSeconds * kNativeFramesPerSecond);
        const std::vector<platform::ExtractedFrame> frames = platform::extract_frames(
            path, options, scratch / "native", limits_for(cancellation), error);
        if (!frames.empty()) {
            const double length = frames.back().seconds + 1.0 / kNativeFramesPerSecond;
            out.push_back(harness::ContentPart::from_text(
                "(A video clip, " + clock_time(length) + " long, as its frames, one a second.)\n"));
            for (std::size_t index = 0; index < frames.size(); ++index) {
                // Its time every five seconds, as mtmd's own video helper
                // marks one.
                if (index % static_cast<std::size_t>(kFrameEvery * kNativeFramesPerSecond) == 0) {
                    out.push_back(harness::ContentPart::from_text(
                        "[" + clock_time(frames[index].seconds) + "]"));
                }
                harness::ContentPart frame = harness::ContentPart::from_image_url(
                    "data:image/jpeg;base64," + base64_encode(read_file(frames[index].file)));
                frame.video_frame = true;
                out.push_back(std::move(frame));
            }
            if (with_sound) {
                std::string ignored;
                const std::string wav = platform::decode_audio_wav(
                    path, rate, 0.0, kNativeSeconds, limits_for(cancellation), ignored);
                if (!wav.empty()) {
                    out.push_back(harness::ContentPart::from_text("\n"));
                    out.push_back(harness::ContentPart::from_audio(base64_encode(wav), "wav"));
                }
            }
        }
    }
    std::filesystem::remove_all(scratch, code);
    return out;
}

double media_duration(const std::filesystem::path& path,
                      const harness::CancellationToken& cancellation) {
    if (!ffmpeg_missing().empty()) {
        return 0.0;
    }
    std::string error;
    const std::optional<platform::MediaInfo> info =
        platform::probe_media(path, limits_for(cancellation), error);
    return info.has_value() ? info->duration : 0.0;
}

}  // namespace apogee::agentloop
