#!/usr/bin/env python3
"""Chat behaviours that only reproduce against a real terminal, or a real kill.

Three checks:

  typeahead   Text typed BEFORE the first prompt is discarded once; text typed
              after it is honoured.  Only reproducible on a PTY -- `tcflush`
              applies to a terminal input queue, and on a pipe the buffered
              bytes ARE the input, so the code deliberately does nothing.

  crash       A `kill -9` mid-conversation leaves every completed turn on disk.
              The point of persisting after every turn rather than at exit.

  resume      A killed session reopens with its history intact.

POSIX only -- `pty` and SIGKILL have no portable Windows equivalent.  Recorded
as a per-item skip in CLAUDE.md -> Platforms.
"""

import json
import os
import pty
import signal
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
    for args in (
        ["config", "init"],
        ["config", "add-backend", "mock", "--type", "mock"],
        ["config", "set-default", "mock"],
    ):
        if subprocess.run([binary, *args], env=env,
                          stdout=subprocess.DEVNULL).returncode != 0:
            raise RuntimeError(f"setup failed: {args}")
    return env


def sessions(home):
    directory = os.path.join(home, "sessions")
    if not os.path.isdir(directory):
        return []
    out = []
    for name in os.listdir(directory):
        if name.endswith(".json"):
            with open(os.path.join(directory, name), encoding="utf-8") as handle:
                out.append(json.load(handle))
    return out


def check_typeahead(binary, home, env):
    """Text typed before the prompt must not become the first message."""
    primary, secondary = pty.openpty()
    process = subprocess.Popen(
        [binary, "chat"],
        stdin=secondary, stdout=secondary, stderr=secondary,
        env=env, close_fds=True,
    )
    os.close(secondary)

    selector = selectors.DefaultSelector()
    selector.register(primary, selectors.EVENT_READ)

    def drain(seconds):
        """Reads whatever is available for `seconds`.

        Draining WHILE waiting, not only at the end: the PTY buffer is small,
        and a child blocked writing into a full one never gets round to reading
        the next line typed at it.
        """
        deadline = time.time() + seconds
        while time.time() < deadline:
            if not selector.select(timeout=0.1):
                continue
            try:
                if not os.read(primary, 65536):
                    return
            except OSError:
                return

    # Typed immediately -- before the backends are constructed and long before
    # the prompt is drawn. This is the impatient keystroke the gate exists for.
    # Still canonical mode here, so \n is what a terminal would deliver.
    os.write(primary, b"TYPEAHEAD-MUST-BE-DISCARDED\n")

    # Let startup reach the flush and draw the first prompt.
    drain(2.0)

    # Now a real message, typed AT the prompt -- where the line editor has the
    # terminal in RAW mode. Enter is \r there: the kernel does no \n -> \r
    # translation, so a bare \n would land in the edit buffer as literal text
    # instead of submitting the line.
    os.write(primary, b"a real question\r")
    drain(2.0)
    os.write(primary, b"/exit\r")
    drain(2.0)

    selector.close()
    os.close(primary)
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
        return ["chat did not exit"]

    saved = sessions(home)
    if not saved:
        return ["no session was written"]

    contents = [m.get("content", "") for m in saved[0].get("messages", [])
                if m.get("role") == "user"]
    failures = []
    if any("TYPEAHEAD-MUST-BE-DISCARDED" in str(c) for c in contents):
        failures.append(f"pre-prompt typeahead became a message: {contents}")
    if not any("a real question" in str(c) for c in contents):
        failures.append(f"the message typed at the prompt was lost: {contents}")
    return failures


def check_crash_and_resume(binary, home, env):
    """kill -9 mid-conversation; every completed turn must survive, and reopen."""
    process = subprocess.Popen(
        [binary, "chat"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
        env=env, close_fds=True,
    )
    # Two complete turns, then killed without an exit command.
    process.stdin.write(b"first turn\n")
    process.stdin.flush()
    time.sleep(0.8)
    process.stdin.write(b"second turn\n")
    process.stdin.flush()
    time.sleep(0.8)

    os.kill(process.pid, signal.SIGKILL)
    process.wait(timeout=10)

    saved = [s for s in sessions(home) if s.get("turns", 0) >= 2]
    if not saved:
        return [f"no session survived the kill: {sessions(home)}"]

    session = saved[0]
    users = [str(m.get("content", "")) for m in session["messages"]
             if m.get("role") == "user"]
    failures = []
    if not any("first turn" in u for u in users):
        failures.append(f"the first completed turn was lost: {users}")
    if not any("second turn" in u for u in users):
        failures.append(f"the second completed turn was lost: {users}")

    # And it reopens with that history.
    resumed = subprocess.run(
        [binary, "chat", "--resume", session["chat_id"]],
        input=b"third turn\n/exit\n", stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL, env=env, timeout=20,
    )
    if resumed.returncode != 0:
        failures.append(f"resume exited {resumed.returncode}")

    after = [s for s in sessions(home) if s["chat_id"] == session["chat_id"]]
    if after:
        users_after = [str(m.get("content", "")) for m in after[0]["messages"]
                       if m.get("role") == "user"]
        if not any("first turn" in u for u in users_after):
            failures.append(f"resume lost earlier history: {users_after}")
        if not any("third turn" in u for u in users_after):
            failures.append(f"the resumed turn was not saved: {users_after}")
    return failures


def main():
    if len(sys.argv) < 2:
        print("usage: pty_chat_check.py <apogee-binary>", file=sys.stderr)
        return 2
    binary = sys.argv[1]

    failures = []
    for name, check in (("typeahead", check_typeahead),
                        ("crash+resume", check_crash_and_resume)):
        home = tempfile.mkdtemp(prefix=f"apogee-chat-{name}-")
        try:
            env = setup(binary, home)
            for failure in check(binary, home, env):
                failures.append(f"{name}: {failure}")
        except Exception as error:  # noqa: BLE001 -- report, do not mask
            failures.append(f"{name}: {error!r}")
        finally:
            shutil.rmtree(home, ignore_errors=True)

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        return 1

    print("typeahead discarded once; completed turns survive a kill -9 - OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
