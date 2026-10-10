#!/usr/bin/env python3
"""The full-screen shell against a real terminal (32b).

Each check is a fresh `apogee` on a pseudo-terminal, started by a shell that
records the terminal's modes (`stty -g`) before it and after it:

  quit      `q` leaves the shell: exit 0, the alternate screen entered and left,
            the cursor shown again, the modes exactly as they were.
  ctrl-c    Ctrl-C with nothing running to stop: the same.
  sigint    SIGINT from outside: the terminal put back, and then the process
            dies of the signal, as a program interrupted should, so the
            shell that started it knows (exit 128+SIGINT).
  sigterm   SIGTERM from outside: the same (128+SIGTERM).
  crash     SIGABRT -- a planted crash: the process dies of it, and the
            terminal is still put back.
  views     Tab shows the next view and F1 the first: the tab strip's bracket
            follows.
  session   With a configured model (32c): the conversation on the screen --
            a line sent, a tool's permission prompt answered `a`, the answer
            rendered, the config holding `always`'s one edit and the tool's
            file written -- then Ctrl-D, the terminal put back.
  pipe      A pipe on stdout: no screen at all, the help as `--help` prints it.
  dumb      TERM=dumb: the refusal said, the help printed, no screen.

POSIX only: Windows has no pty module, and its console path is the said
refusal the unit suite pins through a fake probe.
"""

import os
import pty
import re
import select
import signal
import struct
import subprocess
import sys
import tempfile
import time

import fcntl
import json
import termios

ENTER_ALT = b"\x1b[?1049h"
LEAVE_ALT = b"\x1b[?1049l"
SHOW_CURSOR = b"\x1b[?25h"


def fail(name, message, output=b""):
    tail = output[-2000:].decode("utf-8", errors="replace")
    sys.exit(f"FAIL [{name}]: {message}\n--- output tail ---\n{tail}")


class Session:
    """`script` under /bin/sh on a fresh 80x24 pseudo-terminal."""

    def __init__(self, script, env):
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.execve("/bin/sh", ["/bin/sh", "-c", script], env)
        fcntl.ioctl(self.fd, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
        self.output = b""
        self.closed = False

    def read_for(self, seconds):
        deadline = time.time() + seconds
        while time.time() < deadline and not self.closed:
            ready, _, _ = select.select([self.fd], [], [], 0.05)
            if not ready:
                continue
            try:
                data = os.read(self.fd, 65536)
            except OSError:
                data = b""
            if not data:
                self.closed = True
                break
            self.output += data

    def wait_for(self, needle, seconds, name):
        deadline = time.time() + seconds
        while needle not in self.output:
            if time.time() > deadline or self.closed:
                fail(name, f"never saw {needle!r}", self.output)
            self.read_for(0.1)

    def send(self, data):
        os.write(self.fd, data)

    def apogee_pid(self, name):
        found = subprocess.run(["pgrep", "-P", str(self.pid)], capture_output=True, text=True)
        pids = found.stdout.split()
        if not pids:
            fail(name, "no apogee process under the shell", self.output)
        return int(pids[0])

    def finish(self, name):
        self.read_for(10)
        if not self.closed:
            os.kill(self.pid, signal.SIGKILL)
            fail(name, "the shell never ended", self.output)
        os.waitpid(self.pid, 0)
        return self.output


def without_pendin(state):
    """`stty -g` with PENDIN cleared: a state bit the kernel itself sets when a
    terminal goes back to canonical mode with input queued -- "retype pending
    input" -- never a mode a program sets or leaves behind. lflag is named on
    BSD (`lflag=hex`, PENDIN 0x20000000) and the fourth field on Linux
    (PENDIN 0o40000)."""
    if state.startswith("gfmt1:"):
        def clear(match):
            return "lflag=%x" % (int(match.group(1), 16) & ~0x20000000)
        return re.sub(r"lflag=([0-9a-fA-F]+)", clear, state)
    fields = state.split(":")
    if len(fields) > 3:
        fields[3] = "%x" % (int(fields[3], 16) & ~0o40000)
    return ":".join(fields)


def modes_and_exit(name, output):
    """The two `stty -g` lines and the recorded exit status."""
    text = output.decode("utf-8", errors="replace")
    states = re.findall(r"^([0-9a-zA-Z=:]+:[0-9a-zA-Z=:]+)\r?$", text, re.MULTILINE)
    status = re.search(r"EXIT:(\d+)", text)
    if len(states) < 2 or status is None:
        fail(name, "the shell's records are missing", output)
    return without_pendin(states[0]), without_pendin(states[-1]), int(status.group(1))


def screen_case(binary, env, name, act, expect_exit, ready=b"New chat"):
    session = Session(f'stty -g; "{binary}"; echo "EXIT:$?"; stty -g', env)
    # The session view first (32c): with no model configured its chat cannot
    # open, and it waits on the picker, where q and Ctrl-C are the shell's.
    session.wait_for(b"[1 Session]", 15, name)
    session.wait_for(ready, 15, name)
    act(session)
    output = session.finish(name)
    before, after, code = modes_and_exit(name, output)
    if code != expect_exit:
        fail(name, f"exit {code}, expected {expect_exit}", output)
    if ENTER_ALT not in output:
        fail(name, "the alternate screen was never entered", output)
    if output.rfind(LEAVE_ALT) < output.rfind(ENTER_ALT):
        fail(name, "the alternate screen was not left", output)
    if output.rfind(SHOW_CURSOR) < output.rfind(b"\x1b[?25l"):
        fail(name, "the cursor was left hidden", output)
    if before != after:
        fail(name, f"the terminal's modes changed:\n  before {before}\n  after  {after}", output)
    print(f"ok   {name}: exit {code}, screen left, modes restored")


def main():
    binary = sys.argv[1]
    with tempfile.TemporaryDirectory(prefix="apogee-tui-") as home:
        env = dict(os.environ, TERM="xterm-256color", APOGEE_HOME=home, HOME=home)
        env.pop("NO_COLOR", None)

        screen_case(binary, env, "quit", lambda s: s.send(b"q"), 0)
        screen_case(binary, env, "ctrl-c", lambda s: s.send(b"\x03"), 0)
        screen_case(binary, env, "sigint",
                    lambda s: os.kill(s.apogee_pid("sigint"), signal.SIGINT),
                    128 + signal.SIGINT)
        screen_case(binary, env, "sigterm",
                    lambda s: os.kill(s.apogee_pid("sigterm"), signal.SIGTERM),
                    128 + signal.SIGTERM)
        screen_case(binary, env, "crash",
                    lambda s: os.kill(s.apogee_pid("crash"), signal.SIGABRT),
                    128 + signal.SIGABRT)

        def switch_views(session):
            session.send(b"\t")
            session.wait_for(b"[2 Models]", 5, "views")
            mark = len(session.output)
            session.send(b"\x1bOP")  # F1
            deadline = time.time() + 5
            while b"[1 Session]" not in session.output[mark:]:
                if time.time() > deadline:
                    fail("views", "F1 did not bring back the first view", session.output)
                session.read_for(0.1)
            session.send(b"q")

        screen_case(binary, env, "views", switch_views, 0)

    with tempfile.TemporaryDirectory(prefix="apogee-tui-session-") as home:
        work = os.path.join(home, "work")
        os.makedirs(os.path.join(home, "config"))
        os.makedirs(work)
        script = os.path.join(home, "script.json")
        with open(script, "w") as out:
            json.dump({"turns": [
                {"text": "", "tool_calls": [{"name": "write_file", "arguments":
                    json.dumps({"path": "note.txt", "content": "hi"})}]},
                {"text": "I wrote **note.txt** for you."}]}, out)
        config = os.path.join(home, "config", "config.json")
        with open(config, "w") as out:
            json.dump({"backends": {"local": {"type": "mock", "model_path": script}},
                       "models": {"default": "local"}, "tools": {"fs_root": work}}, out)
        env = dict(os.environ, TERM="xterm-256color", APOGEE_HOME=home, HOME=home)
        env.pop("NO_COLOR", None)

        def converse(session):
            session.wait_for(b"/help for commands", 15, "session")
            session.send(b"please write a note\r")
            session.wait_for(b"Allow?", 15, "session")
            session.send(b"a")
            session.wait_for(b"for you.", 15, "session")
            if b"**note.txt**" in session.output:
                fail("session", "the answer's Markdown was not rendered", session.output)
            session.send(b"\x04")  # Ctrl-D

        screen_case(binary, env, "session", converse, 0, ready=b"[1 Session]")
        with open(config) as written:
            if json.load(written).get("permissions", {}).get("write_file") != "allow":
                fail("session", "always did not write permissions.write_file = allow")
        with open(os.path.join(work, "note.txt")) as note:
            if note.read() != "hi":
                fail("session", "the tool's file was not written")
        print("ok   session: a permission-prompted turn on screen, always written once")

        help_text = subprocess.run([binary, "--help"], stdin=subprocess.DEVNULL,
                                   capture_output=True, env=env).stdout
        piped = Session(f'"{binary}" | cat; echo "EXIT:$?"', env)
        output = piped.finish("pipe")
        if ENTER_ALT in output:
            fail("pipe", "a screen was drawn into a pipe", output)
        if help_text.replace(b"\n", b"\r\n").strip() not in output:
            fail("pipe", "the help was not printed as --help prints it", output)
        print("ok   pipe: no screen, the help")

        dumb = Session(f'"{binary}"; echo "EXIT:$?"', dict(env, TERM="dumb"))
        output = dumb.finish("dumb")
        if ENTER_ALT in output:
            fail("dumb", "a screen was drawn on a dumb terminal", output)
        if b"no full screen here" not in output or b"EXIT:0" not in output:
            fail("dumb", "the refusal and the help's success were not both there", output)
        if help_text.splitlines()[0] not in output:
            fail("dumb", "the help was not printed", output)
        print("ok   dumb: the refusal said, the help printed")


if __name__ == "__main__":
    main()
