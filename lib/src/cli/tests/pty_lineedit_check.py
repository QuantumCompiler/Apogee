#!/usr/bin/env python3
"""Line editing, verified against a real terminal.

replxx only engages when stdin AND stdout are both terminals, so none of this
reproduces on a pipe -- a pipe deliberately gets `std::getline` instead.  What
this locks:

  * an arrow key RECALLS previous input rather than arriving as a literal
    escape sequence in the message (which is exactly what happened before);
  * the input history file is written, so recall survives across sessions;
  * the recalled text is sent as a real message;
  * suggestions (item 24): `/mo` draws rows of commands with their
    descriptions, Tab completes `/model ` and then a backend, `@li` completes
    a real folder in the working directory and the message is sent as typed --
    and no row reaches the terminal's last column, and none is left behind on
    or under a line sent in one burst;
  * a pipe gets none of it: no prompt, no row, no escape sequence.

POSIX only -- `pty` has no Windows equivalent.
"""

import fcntl
import json
import os
import pty
import selectors
import struct
import subprocess
import sys
import tempfile
import termios
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


class Screen:
    """Enough of a terminal to say what replxx's rows leave on the screen.

    Printable cells, CR, LF (the tty has already turned "\\n" into CR LF),
    backspace, the cursor moves replxx makes (`ESC[nA`, `B`, `C`, `D`, `G`),
    `ESC[K` and `ESC[J`, and colour (ignored). A character written in the last
    column leaves the cursor there until the next one arrives -- the deferred
    wrap every real terminal has -- and each row remembers the rightmost
    column written, so a row that touched the edge is caught even after it is
    erased. The check's text is ASCII, one cell a character.
    """

    def __init__(self, width, height=200):
        self.width = width
        self.cells = [[" "] * width for _ in range(height)]
        self.touched = [-1] * height
        self.row = self.col = 0
        self.pending = False
        self.unhandled = []

    def put(self, ch):
        if self.pending:
            self.row, self.col, self.pending = self.row + 1, 0, False
        self.cells[self.row][self.col] = ch
        self.touched[self.row] = max(self.touched[self.row], self.col)
        if self.col == self.width - 1:
            self.pending = True
        else:
            self.col += 1

    def clear_from(self, row, col):
        self.cells[row][col:] = [" "] * (self.width - col)

    def csi(self, params, final):
        if params.startswith("?") or final == "m":
            return
        n = int(params) if params.isdigit() else 1
        self.pending = False
        if final == "A":
            self.row = max(0, self.row - n)
        elif final == "B":
            self.row += n
        elif final == "C":
            self.col = min(self.width - 1, self.col + n)
        elif final == "D":
            self.col = max(0, self.col - n)
        elif final == "G":
            self.col = min(self.width - 1, n - 1)
        elif final == "K" and params in ("", "0"):
            self.clear_from(self.row, self.col)
        elif final == "K" and params == "2":
            self.clear_from(self.row, 0)
        elif final == "J" and params in ("", "0"):
            self.clear_from(self.row, self.col)
            for row in range(self.row + 1, len(self.cells)):
                self.clear_from(row, 0)
        else:
            self.unhandled.append(params + final)

    def feed(self, data):
        text = data.decode("utf-8", "replace")
        i = 0
        while i < len(text):
            ch = text[i]
            if ch == "\x1b" and text[i + 1:i + 2] == "[":
                j = i + 2
                while j < len(text) and not "@" <= text[j] <= "~":
                    j += 1
                self.csi(text[i + 2:j], text[j:j + 1])
                i = j + 1
                continue
            if ch == "\x1b" and text[i + 1:i + 2] == "]":  # OSC, to BEL or ST
                j = i + 2
                while j < len(text) and text[j] != "\x07" and text[j:j + 2] != "\x1b\\":
                    j += 1
                i = j + (1 if text[j:j + 1] == "\x07" else 2)
                continue
            if ch == "\r":
                self.col, self.pending = 0, False
            elif ch == "\n":
                self.row, self.pending = self.row + 1, False
            elif ch == "\b":
                self.col, self.pending = max(0, self.col - 1), False
            elif ch >= " ":
                self.put(ch)
            i += 1

    def lines(self):
        out = ["".join(row).rstrip() for row in self.cells]
        while out and not out[-1]:
            out.pop()
        return out


def run_chat(binary, env, script, settle=0.7, cwd=None, width=None, snapshots=None):
    """Drives chat under a PTY, writing each element of `script` in turn.

    With `snapshots`, the screen as it stands after each element is appended
    to it (a `Screen` of `width` columns).
    """
    primary, secondary = pty.openpty()
    if width is not None:
        fcntl.ioctl(secondary, termios.TIOCSWINSZ, struct.pack("HHHH", 40, width, 0, 0))
    process = subprocess.Popen(
        [binary, "chat"], stdin=secondary, stdout=secondary, stderr=secondary,
        env=env, close_fds=True, cwd=cwd,
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
        if snapshots is not None:
            screen = Screen(width)
            screen.feed(b"".join(chunks))
            snapshots.append(screen)

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


def below_prompt(screen):
    """The prompt row and the rows under it: what the editor drew."""
    lines = screen.lines()
    starts = [i for i, line in enumerate(lines) if line.startswith("You:")]
    return lines[starts[-1]:] if starts else []


def check_suggestions(binary, env, home, failures):
    width = 44
    work = tempfile.mkdtemp(prefix="apogee-lineedit-work-")
    try:
        os.mkdir(os.path.join(work, "library"))
        with open(os.path.join(work, "listing.txt"), "w", encoding="utf-8") as handle:
            handle.write("x")
        before = len(user_messages(home))
        screens = []
        run_chat(binary, env, [
            b"/mo",        # rows: /model, /models, described
            b"\t",         # Tab: "/model " and the backends
            b"\t\r",       # Tab again takes "mock"; sent
            b"@li",        # rows: @library/, @listing.txt
            b"\t",         # Tab: the folder
            b"\r",         # sent as typed
            b"/mo",        # rows again ...
            # ... then keys faster than replxx repaints: it sends the line
            # redrawing the rows it last drew, which must not survive.
            b"\x1b[D\x1b[C\r",
            b"/exit\r",    # one burst: nothing may stay drawn on or under it
        ], cwd=work, width=width, snapshots=screens)

        mo, tab, sent, at, picked, _, _, _, exited = [below_prompt(s) for s in screens]
        if not (len(mo) >= 3 and mo[0] == "You: /mo"
                and any("/models" in row and "List the configured backends" in row
                        for row in mo[1:])):
            failures.append(f"typing /mo drew no described command rows: {mo!r}")
        # One backend is one suggestion, which replxx draws inline after the
        # cursor rather than as a row.
        if not (tab and tab[0].startswith("You: /model") and "mock" in "".join(tab)):
            failures.append(f"Tab did not complete /model and offer the backend: {tab!r}")
        if not any("switched to mock" in line for line in screens[2].lines()):
            failures.append("a second Tab did not complete the backend: no 'switched to mock'")
        if not (len(at) >= 3 and any("@library/" in row for row in at[1:])
                and any("@listing.txt" in row for row in at[1:])):
            failures.append(f"typing @li drew no rows of the working directory: {at!r}")
        if not (picked and picked[0] == "You: @library/"):
            failures.append(f"Tab did not complete @li to the folder: {picked!r}")
        # Anywhere on the screen: a leftover row is overwritten by whatever
        # prints next, so what survives of it can land above the new prompt.
        if any("List the configured backends" in line for line in screens[7].lines()):
            failures.append("rows drawn as a line was sent stayed on the screen: "
                            f"{screens[7].lines()[-4:]!r}")
        if not (exited and exited[0] == "You: /exit"):
            failures.append(f"a line sent in one burst kept a suggestion on it: {exited!r}")
        if any("Save and leave" in line for line in screens[-1].lines()):
            failures.append("a suggestion for /exit was left on the screen")

        for screen in screens:
            if screen.unhandled:
                failures.append(f"unmodelled sequences {screen.unhandled!r}: extend the model")
        # While a line is being typed, everything from its prompt down is the
        # editor's: the line and its rows. None may touch the last column.
        for number in (0, 1, 3, 4, 6):
            lines = screens[number].lines()
            prompt = max(i for i, line in enumerate(lines) if line.startswith("You:"))
            if any(col >= width - 1 for col in screens[number].touched[prompt:]):
                failures.append(f"step {number}: a row under the prompt reached the last "
                                f"column: {lines[prompt:]!r}")

        sent = user_messages(home)[before:]
        if "@library/" not in sent:
            failures.append(f"the message was not sent as typed: {sent!r}")
    finally:
        shutil.rmtree(work, ignore_errors=True)


def check_pipe(binary, env, failures):
    """A pipe is a first-class way to drive chat: it must get bytes and nothing else."""
    work = tempfile.mkdtemp(prefix="apogee-lineedit-pipe-")
    try:
        os.mkdir(os.path.join(work, "library"))
        result = subprocess.run(
            [binary, "chat"], input=b"/help\n/mo\t\n@li\t\n/exit\n",
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env, cwd=work, timeout=30,
        )
        output = result.stdout + result.stderr
        if b"\x1b" in output:
            failures.append(f"a piped chat wrote an escape sequence: {output[:400]!r}")
        if b"You:" in result.stdout:
            failures.append("a piped chat wrote a prompt to stdout")
        if b"/retriever" not in output or b"Show or set how documents are searched" not in output:
            failures.append(f"/help on a pipe does not list /retriever described: {output!r}")
    finally:
        shutil.rmtree(work, ignore_errors=True)


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

        check_suggestions(binary, env, home, failures)
        check_pipe(binary, env, failures)

        if failures:
            for failure in failures:
                print(f"FAIL: {failure}", file=sys.stderr)
            return 1

        print("arrow-key recall, history, suggestions and the pipe contract - OK")
        return 0
    except Exception as error:  # noqa: BLE001 -- report, do not mask
        print(f"FAIL: {error!r}", file=sys.stderr)
        return 1
    finally:
        shutil.rmtree(home, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
