#!/usr/bin/env python3
"""Startup output discipline, verified under a real pseudo-terminal.

`apogee` decides what to render by asking whether stdout is a terminal, so a
pipe-based test proves nothing about the interactive path -- it exercises the
branch that deliberately emits nothing.  A PTY is the only way to see what the
user actually sees.

What this locks (the "startup speaks on one line" invariant, which Ommi
retrofitted as OMMI-14 after the fact):

  * a verbose run's startup notice appears exactly ONCE, on one line;
  * no spinner frame survives into the final screen -- a transient indicator
    that outlives its turn sits in the scrollback forever;
  * the answer is present and intact.

The escape codes are replayed to reconstruct the final screen rather than
matched textually: `\r`, `\033[2K` and `\033[A` mean the bytes on the wire and
the characters on the screen are different things, and only the second one is
what the user is left looking at.

POSIX only -- `pty` has no Windows equivalent.  Recorded as a per-item skip in
CLAUDE.md -> Platforms.
"""

import os
import pty
import selectors
import subprocess
import sys
import tempfile
import shutil


def drive(binary, args, env, timeout=30.0):
    """Runs `binary` with stdout attached to a PTY; returns the raw bytes."""
    primary, secondary = pty.openpty()
    process = subprocess.Popen(
        [binary, *args],
        stdout=secondary,
        stderr=secondary,
        stdin=subprocess.DEVNULL,
        env=env,
        close_fds=True,
    )
    os.close(secondary)

    chunks = []
    selector = selectors.DefaultSelector()
    selector.register(primary, selectors.EVENT_READ)
    try:
        while True:
            if not selector.select(timeout=timeout):
                break
            try:
                data = os.read(primary, 65536)
            except OSError:
                break
            if not data:
                break
            chunks.append(data)
    finally:
        selector.close()
        os.close(primary)
        process.wait(timeout=timeout)

    return b"".join(chunks), process.returncode


def render(raw, width=200):
    """Replays the escape codes and returns the final screen as a list of rows.

    Handles exactly what the thinking view and status line emit: carriage
    return, erase-line, and cursor-up.  Anything else is treated as text, which
    is the conservative direction -- an unhandled code shows up as noise in an
    assertion rather than silently vanishing.
    """
    rows = [""]
    row, col = 0, 0
    i = 0
    while i < len(raw):
        byte = raw[i : i + 1]
        if raw[i : i + 4] == b"\x1b[2K":
            rows[row] = ""
            col = 0
            i += 4
        elif raw[i : i + 3] == b"\x1b[A":
            row = max(0, row - 1)
            col = len(rows[row])
            i += 3
        elif raw[i : i + 2] == b"\x1b[":
            # Any other CSI: skip to its final byte.
            j = i + 2
            while j < len(raw) and not (0x40 <= raw[j] <= 0x7E):
                j += 1
            i = j + 1
        elif byte == b"\r":
            col = 0
            i += 1
        elif byte == b"\n":
            row += 1
            if row >= len(rows):
                rows.append("")
            col = 0
            i += 1
        else:
            text = byte.decode("utf-8", errors="replace")
            line = rows[row]
            if col < len(line):
                rows[row] = line[:col] + text + line[col + 1 :]
            else:
                rows[row] = line + text
            col += 1
            i += 1
    return rows


def main():
    if len(sys.argv) < 2:
        print("usage: pty_startup_check.py <apogee-binary>", file=sys.stderr)
        return 2
    binary = sys.argv[1]

    work = tempfile.mkdtemp(prefix="apogee-pty-")
    try:
        env = dict(os.environ)
        env["APOGEE_HOME"] = work
        env.pop("NO_COLOR", None)

        for args in (
            ["config", "init"],
            ["config", "add-backend", "mock", "--type", "mock"],
            ["config", "set-default", "mock"],
        ):
            if subprocess.run([binary, *args], env=env,
                              stdout=subprocess.DEVNULL).returncode != 0:
                print(f"setup failed: {args}", file=sys.stderr)
                return 1

        raw, code = drive(binary, ["complete", "--verbose", "hello"], env)
        if code != 0:
            print(f"apogee exited {code}\n{raw!r}", file=sys.stderr)
            return 1

        screen = render(raw)
        text = "\n".join(screen)

        failures = []

        # The answer survived.
        if "mock response" not in text:
            failures.append(f"the answer is missing from the final screen:\n{screen!r}")

        # The startup notice appears exactly once. Twice means something wrote
        # it raw as well as through the status line.
        notices = sum(row.count("[apogee]") for row in screen)
        if notices != 1:
            failures.append(f"expected exactly 1 startup notice, saw {notices}:\n{screen!r}")

        # It is on ONE line -- the whole point of the invariant.
        notice_rows = [r for r in screen if "[apogee]" in r]
        if len(notice_rows) != 1:
            failures.append(f"startup notice spans {len(notice_rows)} rows:\n{screen!r}")

        # No spinner frame survives. A transient indicator left in the
        # scrollback is the failure the generation counter exists to prevent.
        for glyph in ("✻ Thinking", "✽", "✼", "✺"):
            if glyph in text:
                failures.append(f"a transient spinner frame survived: {glyph!r}\n{screen!r}")

        if failures:
            for failure in failures:
                print(f"FAIL: {failure}", file=sys.stderr)
            return 1

        print("startup speaks on one line; no spinner frame survived - OK")
        return 0
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
