#pragma once

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
///   output is a frame pattern, and writes `pcm_bytes` of silence to stdout
///   when it is `-`. Either way it says something on stderr first. With
///   `fail` set it says that and exits 1.
class FakeFfmpeg {
public:
    explicit FakeFfmpeg(std::string streams = "video audio", std::string duration = "15",
                        std::string frames = "0 5 10", int pcm_bytes = 32000)
        : dir_{"fake-ffmpeg-" + std::to_string(std::random_device{}())},
          path_{"PATH", (dir_.path() / "bin").string() + ":/bin:/usr/bin"} {
        std::filesystem::create_directories(dir_.path() / "bin");
        script("ffprobe",
               "echo 'ffprobe noise' >&2\n"
               "for s in " +
                   streams +
                   "; do echo \"codec_type=$s\"; done\n"
                   "echo width=640\necho height=360\n"
                   "echo duration=" +
                   duration + "\n");
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
