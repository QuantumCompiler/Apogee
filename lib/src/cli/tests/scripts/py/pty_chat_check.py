#!/usr/bin/env python3
"""Chat behaviours that only reproduce against a real terminal, or a real kill.

Seven checks:

  typeahead   Text typed BEFORE the first prompt is discarded once; text typed
              after it is honoured.  Only reproducible on a PTY -- `tcflush`
              applies to a terminal input queue, and on a pipe the buffered
              bytes ARE the input, so the code deliberately does nothing.

  crash       A `kill -9` mid-conversation leaves every completed turn on disk.
              The point of persisting after every turn rather than at exit.

  resume      A killed session reopens with its history intact.

  spacing     A blank line after the banner, and one between a question and
              whatever answers it.  Only on a terminal: a pipe gets no banner
              and no decoration at all.

  markdown    An answer's Markdown is rendered on a terminal (no `**` left
              around bold text), and shown as written with --raw.

  typeahead-hidden
              Words typed while a reply streams are not echoed into it; the
              next prompt shows them, once.

  interrupt   Ctrl-C in the middle of a reply ends the process with the
              terminal's echo back on -- it was off during the turn, and a
              shell left with echo off takes input blind.

POSIX only -- `pty` and SIGKILL have no portable Windows equivalent.  Recorded
as a per-item skip in CLAUDE.md -> Platforms.
"""

import json
import os
import pty
import re
import termios
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


def check_spacing(binary, home, env):
    """The banner and each question stand apart from what follows them.

    Asked for directly (2026-09-25): a blank line after the `[apogee]` banner,
    before the first prompt, and one between the question and the thinking
    block or answer under it.
    """
    primary, secondary = pty.openpty()
    process = subprocess.Popen(
        [binary, "chat"],
        stdin=secondary, stdout=secondary, stderr=secondary,
        env=env, close_fds=True,
    )
    os.close(secondary)
    selector = selectors.DefaultSelector()
    selector.register(primary, selectors.EVENT_READ)
    seen = bytearray()

    def drain(seconds):
        deadline = time.time() + seconds
        while time.time() < deadline:
            if not selector.select(timeout=0.1):
                continue
            try:
                chunk = os.read(primary, 65536)
            except OSError:
                return
            if not chunk:
                return
            seen.extend(chunk)

    drain(2.0)
    os.write(primary, b"hello there\r")  # raw mode at the prompt: Enter is \r
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

    # The line editor redraws as it goes, so escapes and carriage returns are
    # dropped and only what is stable across redraws is asserted on.
    text = re.sub(rb"\x1b\[[0-9;?]*[A-Za-z]", b"", bytes(seen)).replace(b"\r", b"")
    failures = []
    if b"/help for commands\n\nYou:" not in text:
        failures.append(f"no blank line between the banner and the first prompt: {text!r}")
    # The reply is painted as it streams (its first frames partial), and a
    # spinner frame may come first, so only its start is stable.
    if not re.search(rb"hello there\n\n(\xe2\x9c\xbb Thinking\xe2\x80\xa6)*mock", text):
        failures.append(f"no blank line between the question and its answer: {text!r}")
    return failures


def scripted(binary, env, home, turns):
    """Points the default backend at a mock script of `turns`."""
    script = os.path.join(home, "script.json")
    with open(script, "w", encoding="utf-8") as handle:
        json.dump({"turns": turns}, handle)
    for args in (["config", "add-backend", "scripted", "--type", "mock",
                  "--model-path", script],
                 ["config", "set-default", "scripted"]):
        if subprocess.run([binary, *args], env=env,
                          stdout=subprocess.DEVNULL).returncode != 0:
            raise RuntimeError(f"setup failed: {args}")


class Pty:
    """A chat on a pseudo-terminal, its output collected as it runs."""

    def __init__(self, binary, env, extra=()):
        self.primary, self.secondary = pty.openpty()
        self.process = subprocess.Popen(
            [binary, "chat", *extra],
            stdin=self.secondary, stdout=self.secondary, stderr=self.secondary,
            env=env, close_fds=True,
        )
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.primary, selectors.EVENT_READ)
        self.seen = bytearray()

    def drain(self, seconds):
        deadline = time.time() + seconds
        while time.time() < deadline:
            if not self.selector.select(timeout=0.05):
                continue
            try:
                chunk = os.read(self.primary, 65536)
            except OSError:
                return
            if not chunk:
                return
            self.seen.extend(chunk)

    def send(self, data):
        os.write(self.primary, data)

    def echo_on(self):
        return bool(termios.tcgetattr(self.secondary)[3] & termios.ECHO)

    def text(self):
        return re.sub(rb"\x1b\[[0-9;?]*[A-Za-z]|\x1b\][^\x07\x1b]*(\x07|\x1b\\)", b"",
                      bytes(self.seen)).replace(b"\r", b"")

    def close(self):
        self.selector.close()
        try:
            self.process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait(timeout=10)
        os.close(self.primary)
        os.close(self.secondary)


def check_markdown(binary, home, env):
    """Rendered on a terminal; as written with --raw."""
    scripted(binary, env, home, [{"text": "Use **bold** here.\n"}])
    failures = []
    for extra, want_markers in (((), False), (("--raw",), True)):
        term = Pty(binary, env, extra)
        term.drain(2.0)
        term.send(b"hello\r")
        term.drain(2.0)
        term.send(b"/exit\r")
        term.drain(2.0)
        term.close()
        text = term.text()
        has_markers = b"**bold**" in text
        if b"bold" not in text:
            failures.append(f"no answer with {extra}: {text!r}")
        elif has_markers != want_markers:
            failures.append(f"{'raw' if want_markers else 'rendered'} expected with "
                            f"{list(extra)}: {text!r}")
    return failures


def check_typeahead_hidden(binary, home, env):
    """Typed during a reply: not echoed into it, shown once at the prompt."""
    scripted(binary, env, home, [
        {"text": "A slow reply that streams for a while. " * 3 + "\n", "delay_ms": 120},
        {"text": "second\n"}])
    term = Pty(binary, env)
    term.drain(2.0)
    term.send(b"go\r")
    term.drain(0.6)
    term.send(b"typed meanwhile")
    term.drain(4.0)
    term.send(b"\r")
    term.drain(2.0)
    term.send(b"/exit\r")
    term.drain(2.0)
    term.close()
    text = term.text()
    failures = []
    count = text.count(b"typed meanwhile")
    if count == 0:
        failures.append(f"the typed words were lost: {text!r}")
    # The line editor may redraw the prompt line as it takes the keystrokes,
    # so count the reply's own rows: none of them may contain the words.
    reply = text[text.find(b"go\n"):text.find(b"You: ", text.find(b"go\n"))]
    if b"typed meanwhile" in reply:
        failures.append(f"typing echoed into the reply: {reply!r}")
    return failures


def check_interrupt(binary, home, env):
    """Echo is off during a turn and back on when Ctrl-C ends the process."""
    scripted(binary, env, home, [
        {"text": "A slow reply that is interrupted. " * 4 + "\n", "delay_ms": 150}])
    term = Pty(binary, env)
    term.drain(2.0)
    term.send(b"go\r")
    term.drain(0.8)
    failures = []
    if term.echo_on():
        failures.append("echo was still on in the middle of a turn")
    term.process.send_signal(signal.SIGINT)
    term.drain(2.0)
    try:
        term.process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        failures.append("Ctrl-C did not end the process")
    if not term.echo_on():
        failures.append("the terminal was left with echo off")
    term.close()
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
                        ("crash+resume", check_crash_and_resume),
                        ("spacing", check_spacing),
                        ("markdown", check_markdown),
                        ("typeahead-hidden", check_typeahead_hidden),
                        ("interrupt", check_interrupt)):
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

    print("typeahead discarded once; completed turns survive a kill -9; "
          "the banner and each question stand apart; answers render, raw with --raw; "
          "typing mid-reply waits for the prompt; Ctrl-C restores echo - OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
