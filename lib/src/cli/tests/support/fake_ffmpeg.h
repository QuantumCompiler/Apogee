#pragma once

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>

#include "support/env_guard.h"

/// An `ffmpeg` and `ffprobe` on `PATH` that write frames, timestamps and
/// silence -- and noise on stderr -- without a codec in sight (26e). Apart
/// from the media provider (support/media_fakes.h) because the platform seam's
/// suite drives them alone, and sits below the contracts that provider
/// implements (ADR 0004). POSIX only: the fakes are shell scripts, and Windows
/// has no child processes here yet.
namespace apogee::testing {

/// An `ffmpeg` and an `ffprobe` on `PATH`, each a shell script.
///
/// - `ffprobe` reports the streams in `streams` and `duration` seconds.
/// - `ffmpeg` appends its arguments to `log()`, writes a JPEG-named file
///   for each time in `frames` with a `showinfo` line on stderr when its
///   output is a frame pattern, writes `pcm_bytes` of silence to stdout
///   when it is `-`, and writes a file naming its codec when it is an `.mp4`
///   or a `.png` (49b, 49d). Either way it says something on stderr first.
///   With `fail` set it says that and exits 1; with `no_x264` set, a run
///   asking for `libx264` is refused as a build without it refuses one;
///   with `no_libass` set, a `subtitles` filter is refused and left out of
///   `-filters` (50b).
class FakeFfmpeg {
public:
    explicit FakeFfmpeg(std::string streams = "video audio", std::string duration = "15",
                        std::string frames = "0 5 10", int pcm_bytes = 32000)
        : dir_{"fake-ffmpeg-" + std::to_string(std::random_device{}())},
          path_{"PATH", (dir_.path() / "bin").string() + ":/bin:/usr/bin"} {
        std::filesystem::create_directories(dir_.path() / "bin");
        std::filesystem::create_directories(dir_.path() / "probe");
        script("ffprobe",
               "echo 'ffprobe noise' >&2\n"
               "last=''\nfor a in \"$@\"; do last=\"$a\"; done\n"
               "f='" +
                   (dir_.path() / "probe").string() +
                   "'/\"$(basename \"$last\")\"\n"
                   "if [ -f \"$f\" ]; then cat \"$f\"; exit 0; fi\n" +
                   "cat <<'EOF'\n" + probe_text(streams, duration) + "EOF\n");
        script("ffmpeg",
               "echo \"$@\" >> '" + log().string() +
                   "'\n"
                   "echo 'ffmpeg version fake -- a banner nobody should see' >&2\n"
                   "if [ -f '" +
                   (dir_.path() / "fail").string() +
                   "' ]; then echo 'Invalid data found when processing input' >&2; exit 1; fi\n"
                   "if [ -f '" +
                   (dir_.path() / "slow").string() +
                   "' ]; then sleep 5; fi\n"
                   "if [ -f '" +
                   (dir_.path() / "no_x264").string() +
                   "' ]; then case \" $* \" in *' libx264 '*) echo \"Unknown encoder "
                   "'libx264'\" >&2; exit 1 ;; esac; fi\n"
                   "if [ -f '" +
                   (dir_.path() / "no_libass").string() +
                   "' ]; then case \" $* \" in *subtitles=*) echo \"[AVFilterGraph @ 0x1] No such "
                   "filter: 'subtitles'\" >&2; echo 'Error opening output files: Filter not found' "
                   ">&2; exit 8 ;; esac; fi\n"
                   "if [ \"$*\" = '-hide_banner -filters' ]; then\n"
                   "  echo 'Filters:'; echo '  T.. = Timeline support'; echo '  ------'\n"
                   "  echo ' TS aap               AA->A      Affine projection.'\n"
                   "  echo ' .. anullsrc          |->A       Null audio source.'\n"
                   "  [ -f '" +
                   (dir_.path() / "no_libass").string() +
                   "' ] || echo ' .. subtitles         V->V       Render text subtitles.'\n"
                   "  echo ' .S xfade             VV->V      Cross fade.'\n"
                   "  exit 0\n"
                   "fi\n"
                   "out=''\nfor a in \"$@\"; do out=\"$a\"; done\n"
                   "case \"$out\" in\n"
                   "  *%05d.jpg)\n"
                   "    dir=$(dirname \"$out\"); n=1\n"
                   "    for t in " +
                   frames +
                   "; do\n"
                   "      f=$(printf '%s/f%05d.jpg' \"$dir\" $n); printf 'JPEG%s' \"$t\" > \"$f\"\n"
                   "      echo \"[Parsed_showinfo_2 @ 0x1] n: $((n-1)) pts: 0 pts_time:$t\" >&2\n"
                   "      n=$((n+1))\n"
                   "    done ;;\n"
                   "  *.mp4) codec=''; prev=''\n"
                   "    for a in \"$@\"; do case \"$prev\" in -c:v|-c) codec=\"$a\" ;; esac; "
                   "prev=\"$a\"; done\n"
                   "    printf 'MP4 %s' \"$codec\" > \"$out\" ;;\n"
                   "  *.png) printf 'PNG last frame' > \"$out\" ;;\n"
                   "  -) head -c " +
                   std::to_string(pcm_bytes) + " /dev/zero ;;\n" + "esac\nexit 0\n");
    }

    /// Every ffmpeg command line, one a line.
    [[nodiscard]] std::filesystem::path log() const {
        return dir_.path() / "ffmpeg.log";
    }

    [[nodiscard]] std::string logged() const {
        std::ifstream in{log()};
        return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    }

    /// Makes every ffmpeg run fail from now on.
    void fail() const {
        std::ofstream{dir_.path() / "fail"} << "1";
    }

    /// Makes a run asking for `libx264` fail as an ffmpeg built without it.
    void no_x264() const {
        std::ofstream{dir_.path() / "no_x264"} << "1";
    }

    /// Makes the ffmpeg one built without libass: no `subtitles` filter (50b).
    void no_libass() const {
        std::ofstream{dir_.path() / "no_libass"} << "1";
    }

    /// Makes ffprobe answer `answer` -- `probe_text`'s, or any text -- for a
    /// file named `name`, whatever its directory (50b).
    void probe_as(const std::string& name, const std::string& answer) const {
        std::ofstream{dir_.path() / "probe" / name} << answer;
    }

    /// ffprobe's answer: each stream a block, as `-of default` prints them --
    /// a video stream H.264 at 640x360 and 24 frames a second, a sound AAC at
    /// 48 kHz in stereo -- then the length.
    [[nodiscard]] static std::string probe_text(const std::string& streams,
                                                const std::string& duration) {
        std::string out;
        std::size_t at = 0;
        while (at < streams.size()) {
            const std::size_t end = std::min(streams.find(' ', at), streams.size());
            const std::string kind = streams.substr(at, end - at);
            at = end + 1;
            if (kind == "video") {
                out +=
                    "[STREAM]\ncodec_name=h264\ncodec_type=video\nwidth=640\nheight=360\n"
                    "r_frame_rate=24/1\n[/STREAM]\n";
            } else if (kind == "audio") {
                out +=
                    "[STREAM]\ncodec_name=aac\ncodec_type=audio\nsample_rate=48000\n"
                    "channels=2\nr_frame_rate=0/0\n[/STREAM]\n";
            }
        }
        return out + "[FORMAT]\nduration=" + duration + "\n[/FORMAT]\n";
    }

    /// Makes every ffmpeg run take five seconds from now on.
    void slow() const {
        std::ofstream{dir_.path() / "slow"} << "1";
    }

private:
    void script(const std::string& name, const std::string& body) const {
        const std::filesystem::path path = dir_.path() / "bin" / name;
        std::ofstream{path} << "#!/bin/sh\n" << body;
        std::filesystem::permissions(path, std::filesystem::perms::owner_all,
                                     std::filesystem::perm_options::replace);
    }

    TempDir dir_;
    EnvGuard path_;
};

}  // namespace apogee::testing
