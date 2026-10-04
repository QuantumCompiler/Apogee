#!/usr/bin/env python3
"""ffmpeg's own output must never reach the terminal, under a real PTY (26e).

Audio and video attachments are decoded by `ffmpeg`, which narrates its
banner, its stream mapping and its progress onto stderr.  Apogee runs it as a
child whose stderr is a pipe it owns -- the "a child's stderr is captured,
never inherited" invariant -- so none of that may paint the screen.  Locked
here on the real binary with a fake ffmpeg and ffprobe that do nothing but
make that noise and hand back a few frames:

  * every row is Apogee's own -- `[apogee]`, `[warn]`, the answer -- and the
    NOISE_ lines appear nowhere;
  * the video was attached, as a timeline its frames were described into.

POSIX only -- `pty` has no Windows equivalent, and neither has the child.
"""

import os
import shutil
import stat
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True  # importing a sibling must not litter the tests tree
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_startup_check import drive, render  # noqa: E402

FAKE_FFPROBE = """#!/bin/sh
echo "NOISE_FFPROBE_BANNER ffprobe version 8.1 Copyright (c) the FFmpeg developers" >&2
echo codec_type=video
echo codec_type=audio
echo width=640
echo height=360
echo duration=12
"""

FAKE_FFMPEG = """#!/bin/sh
echo "NOISE_FFMPEG_BANNER ffmpeg version 8.1 Copyright (c) the FFmpeg developers" >&2
echo "NOISE_FFMPEG_MAPPING Stream #0:0 -> #0:0 (h264 (native) -> mjpeg (native))" >&2
out=''
for a in "$@"; do out="$a"; done
case "$out" in
  *%05d.jpg)
    dir=$(dirname "$out"); n=1
    for t in 0 5 10; do
      f=$(printf '%s/f%05d.jpg' "$dir" $n); printf 'JPEG%s' "$t" > "$f"
      echo "[Parsed_showinfo_2 @ 0x1] n: $((n-1)) pts: 0 pts_time:$t NOISE_SHOWINFO" >&2
      n=$((n+1))
    done ;;
  -) head -c 32000 /dev/zero ;;
esac
echo "NOISE_FFMPEG_PROGRESS frame=  3 fps=0.0 q=-0.0 Lsize=N/A time=00:00:10.00" >&2
exit 0
"""


def main():
    if len(sys.argv) < 2:
        print("usage: pty_ffmpeg_check.py <apogee-binary>", file=sys.stderr)
        return 2
    binary = sys.argv[1]
    work = tempfile.mkdtemp(prefix="apogee-pty-ffmpeg-")
    try:
        bin_dir = os.path.join(work, "bin")
        os.makedirs(bin_dir)
        for name, body in (("ffmpeg", FAKE_FFMPEG), ("ffprobe", FAKE_FFPROBE)):
            path = os.path.join(bin_dir, name)
            with open(path, "w", encoding="utf-8") as handle:
                handle.write(body)
            os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR)
        clip = os.path.join(work, "clip.mp4")
        with open(clip, "wb") as handle:
            handle.write(b"not really a video")

        env = dict(os.environ)
        env["APOGEE_HOME"] = os.path.join(work, "home")
        env["PATH"] = bin_dir + os.pathsep + "/bin" + os.pathsep + "/usr/bin"
        env.pop("NO_COLOR", None)
        for args in (
            ["config", "init"],
            ["config", "add-backend", "mock", "--type", "mock"],
            ["config", "set-default", "mock"],
        ):
            if subprocess.run([binary, *args], env=env, stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL).returncode != 0:
                print(f"setup failed: {args}", file=sys.stderr)
                return 1

        raw, code = drive(binary, ["complete", "--attach", clip, "what happens in it?"], env)
        if code != 0:
            print(f"apogee exited {code}\n{raw!r}", file=sys.stderr)
            return 1
        screen = render(raw)
        failures = []
        if "NOISE_" in raw.decode("utf-8", "replace"):
            failures.append("ffmpeg's own output reached the terminal")
        text = "\n".join(screen)
        if "mock response" not in text:
            failures.append(f"the answer is missing:\n{screen!r}")
        if "attached " not in text or "made a timeline by mock" not in text:
            failures.append(f"the video was not attached as a timeline:\n{screen!r}")
        for row in screen:
            if row.strip() and not (row.startswith("[apogee]") or row.startswith("[warn]")
                                    or "mock response" in row):
                failures.append(f"a row that is not Apogee's own: {row!r}")
        if failures:
            for failure in failures:
                print(f"FAIL: {failure}", file=sys.stderr)
            return 1
        print("ffmpeg's own output stays off the terminal - OK")
        return 0
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
