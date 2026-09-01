#!/usr/bin/env python3
"""Line editing, verified against a real terminal.

replxx only engages when stdin AND stdout are both terminals, so none of this
reproduces on a pipe -- a pipe deliberately gets `std::getline` instead.  What
this locks:

  * an arrow key RECALLS previous input rather than arriving as a literal
    escape sequence in the message (which is exactly what happened before);
  * the input history file is written, so recall survives across sessions;
  * the recalled text is sent as a real message.

POSIX only -- `pty` has no Windows equivalent.
"""

import json
import os
import pty
import selectors
import subprocess
import sys
import tempfile
import shutil
import time


def setup(binary, home):
    env = dict(os.environ)
    env["APOGEE_HOME"] = home
    env.pop("NO_COLOR", None)
    for args in (["config", "init"],
                 ["config", "add-backend", "mock", "--type", "mock"],
                 ["config", "set-default", "mock"]):
        if subprocess.run([binary, *args], env=env,
                          stdout=subprocess.DEVNULL).returncode != 0:
            raise RuntimeError(f"setup failed: {args}")
    return env


def run_chat(binary, env, script, settle=0.7):
    """Drives chat under a PTY, writing each element of `script` in turn."""
    primary, secondary = pty.openpty()
    process = subprocess.Popen(
        [binary, "chat"], stdin=secondary, stdout=secondary, stderr=secondary,
        env=env, close_fds=True,
    )
    os.close(secondary)

    time.sleep(1.2)  # let startup finish and the first prompt appear
    chunks = []
    selector = selectors.DefaultSelector()
    selector.register(primary, selectors.EVENT_READ)

    def drain(seconds):
        deadline = time.time() + seconds
        while time.time() < deadline:
            if not selector.select(timeout=0.1):
                continue
            try:
                data = os.read(primary, 65536)
            except OSError:
                return
            if not data:
                return
            chunks.append(data)

    drain(0.3)
    for keys in script:
        os.write(primary, keys)
        drain(settle)

    selector.close()
    os.close(primary)
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
    return b"".join(chunks)


def sessions(home):
    directory = os.path.join(home, "sessions")
    if not os.path.isdir(directory):
        return []
    out = []
    for name in sorted(os.listdir(directory)):
        if name.endswith(".json"):
            with open(os.path.join(directory, name), encoding="utf-8") as handle:
                out.append(json.load(handle))
    return out


def user_messages(home):
    out = []
    for session in sessions(home):
        out.extend(str(m.get("content", "")) for m in session.get("messages", [])
                   if m.get("role") == "user")
    return out


def main():
    if len(sys.argv) < 2:
        print("usage: pty_lineedit_check.py <apogee-binary>", file=sys.stderr)
        return 2
    binary = sys.argv[1]

    home = tempfile.mkdtemp(prefix="apogee-lineedit-")
    failures = []
    try:
        env = setup(binary, home)

        # Session 1: type a message, then recall it with Up and send it again.
        # Enter is CARRIAGE RETURN, not newline. replxx puts the terminal in
        # raw mode, so the kernel performs no \n -> \r translation and a bare
        # \n arrives as literal text in the edit buffer rather than submitting
        # the line. A real terminal sends \r when the user presses Enter.
        run_chat(binary, env, [
            b"remember this line\r",
            b"\x1b[A",   # Up: recall "remember this line"
            b"\r",       # send the recalled text
            b"/exit\r",
        ])

        messages = user_messages(home)

        # The escape sequence must never arrive as literal text -- that is
        # precisely what happened before replxx was wired.
        if any("\x1b" in m or "[A" in m for m in messages):
            failures.append(f"an arrow key reached the message as text: {messages!r}")

        recalled = [m for m in messages if m == "remember this line"]
        if len(recalled) < 2:
            failures.append(
                f"Up did not recall the previous line (expected it twice): {messages!r}")

        history = os.path.join(home, "chat_history")
        if not os.path.exists(history):
            failures.append("no input history file was written")
        else:
            with open(history, encoding="utf-8", errors="replace") as handle:
                if "remember this line" not in handle.read():
                    failures.append("the history file does not contain the typed line")

        if failures:
            for failure in failures:
                print(f"FAIL: {failure}", file=sys.stderr)
            return 1

        print("arrow-key recall works and history persists - OK")
        return 0
    except Exception as error:  # noqa: BLE001 -- report, do not mask
        print(f"FAIL: {error!r}", file=sys.stderr)
        return 1
    finally:
        shutil.rmtree(home, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
