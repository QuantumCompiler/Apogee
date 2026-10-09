#!/usr/bin/env python3
"""The unit suite, run from a terminal, is the unit suite run from a pipe.

ctest and CI give the unit binary a pipe for stdin, so a test that only passes
because nobody is at a keyboard passes there forever. Run from a developer's
terminal, the same binary stopped at a `You:` prompt: in-process chat tests fed
std::cin a buffer of their own while the line editor read the terminal behind
it, and tests that capture std::cout and std::cerr were painted into because
the decoration asked descriptor 1 and 2, not the streams (2026-10-07).

This runs the files that drive the command tree in-process -- the chat, the
session core, suites, the agents wizard -- with all three standard streams on a
pseudo-terminal, and holds two things:

  * it finishes: no output for STALL seconds is a test waiting on the
    keyboard, named by the last test that finished before it;
  * it passes, exactly as it does on a pipe.

POSIX only -- `pty` has no Windows equivalent.  Recorded as a per-item skip in
CLAUDE.md -> Platforms.
"""

import os
import pty
import re
import selectors
import signal
import subprocess
import sys
import time

STALL = 60.0
FILES = ["chat_test", "chat_session_test", "suite_residency_test", "execute_test",
         "agents_test", "line_reader_test", "platform_test"]


def main():
    binary = sys.argv[1]
    spec = ",".join(f"[#{name}]" for name in FILES)
    primary, secondary = pty.openpty()
    process = subprocess.Popen([binary, "-#", spec, "-d", "yes"],
                               stdin=secondary, stdout=secondary, stderr=secondary,
                               close_fds=True, start_new_session=True)
    os.close(secondary)
    selector = selectors.DefaultSelector()
    selector.register(primary, selectors.EVENT_READ)
    seen = bytearray()
    last = time.time()
    stalled = False
    while True:
        if selector.select(timeout=1.0):
            try:
                data = os.read(primary, 65536)
            except OSError:
                data = b""
            if data:
                seen.extend(data)
                last = time.time()
                continue
            break  # the terminal closed: the binary is done
        if process.poll() is not None:
            break
        if time.time() - last > STALL:
            stalled = True
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            break
    code = process.wait()
    os.close(primary)

    text = seen.decode("utf-8", "replace").replace("\r", "")
    finished = re.findall(r"^\d+\.\d+ s: (.+)$", text, re.M)
    if stalled:
        print(f"unit suite under a TTY: stalled for {STALL:.0f} s after "
              f"'{finished[-1] if finished else '(no test finished)'}' -- a test is waiting "
              f"on the terminal:\n{text[-600:]}", file=sys.stderr)
        return 1
    if code != 0:
        failures = re.findall(r"^(.*?FAILED:.*)$", text, re.M)
        print(f"unit suite under a TTY: exit {code}, {len(failures)} failed assertion(s) "
              f"that pass on a pipe:\n{text[-4000:]}", file=sys.stderr)
        return 1
    if not finished:
        print(f"unit suite under a TTY: no test ran:\n{text[-600:]}", file=sys.stderr)
        return 1
    print(f"unit suite under a TTY: {len(finished)} cases finished and passed, "
          f"none waited on the terminal - OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
