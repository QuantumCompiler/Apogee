#!/usr/bin/env python3
"""Chat behaviours that only reproduce against a real terminal, or a real kill.

Twelve checks:

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

  presets     `--allow write_file` writes with no prompt, and the same session
              still asks before `run_command` (26o).

  base-model  A base model's session says so in its banner and its spinner,
              and says once that its tools are off (26r).

  side-calls  A follow-up's rewrite by the utility model is narrated inside
              the thinking block on a terminal, which collapses as reasoning
              does; the saved chat carries none of it (26n).

  interrupt   Ctrl-C in the middle of a reply ends the process with the
              terminal's echo back on -- it was off during the turn, and a
              shell left with echo off takes input blind.

  task-attended
              An attended `task run` (27i): a tool granted with `--allow`
              runs with no prompt, one not granted is asked about at the
              terminal -- policy adds to the human path, never replaces it --
              and a question with no declared answer is put to the person
              there; each answer is recorded in the ledger as the person's.

  task-machine
              The same task in machine mode (27j), on the same terminal: a
              machine-mode run reads no input, so nothing is asked -- the
              ungranted tool is denied by nobody, the question ends the task
              -- and the run ends by itself, its lifecycle on the stream.

  execute     `apogee execute` (27s): the banner names the suite and how
              many symphonies it can play; Tab completes a symphony's name
              after `/play`; the play's stages are narrated in the thinking
              block, which collapses, and its output is rendered as the
              session's answer; the saved chat keeps the play as one exchange
              and none of the narration.

  orchestrate An orchestrating `execute` (27t): the banner says how many
              symphonies the model is offered; the model's own play is a
              labeled line -- its choice and its member calls -- with its
              stage lines beneath it in the thinking block, each closed with
              what it took, the block collapsing, and the answer that reads
              its output is rendered.

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

    def __init__(self, binary, env, extra=(), cwd=None, command=("chat",)):
        self.primary, self.secondary = pty.openpty()
        self.process = subprocess.Popen(
            [os.path.abspath(binary), *command, *extra],
            stdin=self.secondary, stdout=self.secondary, stderr=self.secondary,
            env=env, close_fds=True, cwd=cwd,
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

    def wait_for(self, needle, seconds):
        """Drains until `needle` has been seen, or `seconds` pass."""
        deadline = time.time() + seconds
        while time.time() < deadline and needle not in self.text():
            self.drain(0.1)
        return needle in self.text()

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


def check_side_calls(binary, home, env):
    """A follow-up's rewrite is narrated inside the thinking block, and the
    block collapses; the saved chat carries none of it (26n)."""
    docs = os.path.join(home, "docs")
    os.makedirs(docs)
    with open(os.path.join(docs, "heron.txt"), "w", encoding="utf-8") as handle:
        handle.write("Project Heron is the billing ledger. It deploys to Frankfurt.\n")
    helper = os.path.join(home, "helper.json")
    with open(helper, "w", encoding="utf-8") as handle:
        json.dump({"turns": [{"text": "Heron Notes"},
                             {"text": "Project Heron deployment region"}]}, handle)
    scripted(binary, env, home, [{"text": "Heron is the billing ledger."},
                                 {"text": "It deploys to Frankfurt."}])
    for args in (["config", "add-backend", "helper", "--type", "mock", "--model-path", helper],
                 ["config", "set-default-utility", "helper"],
                 ["embed", "ingest", "notes", docs]):
        if subprocess.run([binary, *args], env=env, stdout=subprocess.DEVNULL,
                          stderr=subprocess.DEVNULL).returncode != 0:
            raise RuntimeError(f"setup failed: {args}")
    term = Pty(binary, env, ("--rag", "notes"))
    term.drain(2.0)
    term.send(b"Tell me about Project Heron.\r")
    term.drain(3.0)
    term.send(b"where does it deploy?\r")
    term.drain(3.0)
    term.send(b"/exit\r")
    term.drain(2.0)
    term.close()
    text = term.text()
    failures = []
    if b"utility \xe2\x80\x94 rewriting the follow-up into a search query with helper" not in text:
        failures.append(f"the rewrite was not narrated in the block: {text!r}")
    if b"Worked for" not in text:
        failures.append(f"a block of side calls alone did not collapse to 'Worked for': {text!r}")
    for session in sessions(home):
        if "rewriting the follow-up" in json.dumps(session):
            failures.append("the narration reached the saved chat")
    return failures


def check_presets(binary, home, env):
    """`chat --allow write_file` writes with no prompt and still asks for
    `run_command`; `/allow run_command` stops that ask (26o)."""
    work = os.path.join(home, "work")
    os.makedirs(work)
    scripted(binary, env, home, [
        {"tool_calls": [{"name": "write_file",
                         "arguments": {"path": "out.txt", "content": "hello"}}]},
        {"tool_calls": [{"name": "run_command", "arguments": {"command": "echo hi"}}]},
        {"text": "done"},
    ])
    # The tools work in the folder the chat starts in.
    term = Pty(binary, env, ("--tools", "--allow", "write_file"), cwd=work)
    term.drain(2.0)
    term.send(b"go\r")
    term.drain(3.0)
    term.send(b"n\r")
    term.drain(3.0)
    term.send(b"/exit\r")
    term.drain(2.0)
    term.close()
    text = term.text()
    failures = []
    if not os.path.exists(os.path.join(work, "out.txt")):
        failures.append("the preset write did not happen")
    asks = text.count(b"Allow? [y]es")
    if asks != 1:
        failures.append(f"expected one prompt -- for run_command -- saw {asks}: {text!r}")
    elif b"run_command" not in text.split(b"Allow? [y]es")[0].splitlines()[-2] + \
            text.split(b"Allow? [y]es")[0].splitlines()[-1]:
        failures.append(f"the prompt was not run_command's: {text!r}")
    return failures


def check_task_attended(binary, home, env):
    """A task run at a terminal: granted runs, ungranted asks, a question
    goes to the person, each answer recorded as theirs (27i)."""
    work = os.path.join(home, "work")
    os.makedirs(work)
    scripted(binary, env, home, [
        {"text": "1. Write, run, ask."},
        {"tool_calls": [{"name": "write_file",
                         "arguments": {"path": "out.txt", "content": "hello"}}]},
        {"tool_calls": [{"name": "run_command", "arguments": {"command": "echo hi"}}]},
        {"tool_calls": [{"name": "ask_user", "arguments": {"questions": [
            {"header": "Colour", "question": "Which colour?",
             "options": [{"label": "Red"}, {"label": "Green"}]}]}}]},
        {"text": "Done: {{last_tool_result}}\nTASK STATUS: DONE"},
    ])
    term = Pty(binary, env, ("Do the work", "--tools", "--allow", "write_file", "--rounds", "1"),
               cwd=work, command=("task", "run"))
    failures = []
    if not term.wait_for(b"Allow? [y]es", 15.0):
        failures.append(f"run_command was never asked about: {term.text()!r}")
    term.send(b"n\r")
    if not term.wait_for(b"Choose a number, or type your own answer", 10.0):
        failures.append(f"the question was never put: {term.text()!r}")
    term.send(b"blue\r")
    term.drain(3.0)
    term.close()
    text = term.text()
    if term.process.returncode != 0:
        failures.append(f"the task did not end done ({term.process.returncode}): {text!r}")
    if not os.path.exists(os.path.join(work, "out.txt")):
        failures.append("the granted write did not happen")
    asks = text.count(b"Allow? [y]es")
    if asks != 1:
        failures.append(f"expected one prompt -- for run_command -- saw {asks}: {text!r}")
    elif b"run_command" not in text.split(b"Allow? [y]es")[0].splitlines()[-2] + \
            text.split(b"Allow? [y]es")[0].splitlines()[-1]:
        failures.append(f"the prompt was not run_command's: {text!r}")
    tasks = os.path.join(home, "tasks")
    ledgers = [os.path.join(tasks, name, "task.json") for name in os.listdir(tasks)
               if os.path.isfile(os.path.join(tasks, name, "task.json"))] \
        if os.path.isdir(tasks) else []
    if len(ledgers) != 1:
        return failures + [f"expected one task ledger, found {ledgers}"]
    with open(ledgers[0], encoding="utf-8") as handle:
        task = json.load(handle)
    round_ = task["rounds"][-1]
    if round_.get("allowed") != [{"by": "grant", "target": "out.txt", "tool": "write_file"}]:
        failures.append(f"the grant's use is not recorded: {round_.get('allowed')}")
    if round_.get("denied") != [{"by": "person", "target": "echo hi", "tool": "run_command"}]:
        failures.append(f"the person's no is not recorded: {round_.get('denied')}")
    if round_.get("answered") != [{"answer": "blue", "by": "person",
                                   "question": "Which colour?"}]:
        failures.append(f"the person's answer is not recorded: {round_.get('answered')}")
    if task.get("status") != "done":
        failures.append(f"the task is {task.get('status')}: {task.get('reason')}")
    return failures


def check_task_machine(binary, home, env):
    """A task in machine mode at a terminal: nobody is asked anything (27j)."""
    work = os.path.join(home, "work")
    os.makedirs(work)
    scripted(binary, env, home, [
        {"text": "1. Write, run, ask."},
        {"tool_calls": [{"name": "write_file",
                         "arguments": {"path": "out.txt", "content": "hello"}}]},
        {"tool_calls": [{"name": "run_command", "arguments": {"command": "echo hi"}}]},
        {"tool_calls": [{"name": "ask_user", "arguments": {"questions": [
            {"header": "Colour", "question": "Which colour?",
             "options": [{"label": "Red"}, {"label": "Green"}]}]}}]},
        {"text": "Done.\nTASK STATUS: DONE"},
    ])
    term = Pty(binary, env, ("Do the work", "--tools", "--allow", "write_file", "--rounds", "1",
                             "--output-format", "stream-json"),
               cwd=work, command=("task", "run"))
    # Nothing is typed: a prompt would wait here until the close kills it.
    term.wait_for(b'"type":"task_finished"', 15.0)
    term.drain(1.0)
    term.close()
    text = term.text()
    failures = []
    if term.process.returncode != 1:
        failures.append(f"the task did not end failed on its own ({term.process.returncode}): "
                        f"{text!r}")
    if b"Allow? [y]es" in text or b"Choose a number" in text:
        failures.append(f"machine mode asked at the terminal: {text!r}")
    if b'"type":"task_started"' not in text:
        failures.append(f"no task events on the stream: {text!r}")
    tasks = os.path.join(home, "tasks")
    ledgers = [os.path.join(tasks, name, "task.json") for name in os.listdir(tasks)
               if os.path.isfile(os.path.join(tasks, name, "task.json"))] \
        if os.path.isdir(tasks) else []
    if len(ledgers) != 1:
        return failures + [f"expected one task ledger, found {ledgers}"]
    with open(ledgers[0], encoding="utf-8") as handle:
        task = json.load(handle)
    round_ = task["rounds"][-1]
    if round_.get("denied") != [{"by": "nobody", "target": "echo hi", "tool": "run_command"}]:
        failures.append(f"run_command was not denied by nobody: {round_.get('denied')}")
    if task.get("status") != "failed" or "Which colour?" not in task.get("reason", ""):
        failures.append(f"the question did not end the task: {task.get('status')} "
                        f"{task.get('reason')}")
    return failures


def check_execute(binary, home, env):
    """`execute` at a terminal (27s): its banner, a play narrated stage by
    stage in the thinking block, the output rendered as the answer."""
    helper = os.path.join(home, "helper.json")
    with open(helper, "w", encoding="utf-8") as handle:
        json.dump({"turns": [{"text": "A cat sat on a mat."}]}, handle)
    scripted(binary, env, home, [{"text": "The **final** summary.\n"}])
    for args in (["config", "add-backend", "helper", "--type", "mock", "--model-path", helper],
                 ["config", "add-suite", "duo", "--chat", "scripted", "--utility", "helper"]):
        if subprocess.run([binary, *args], env=env, stdout=subprocess.DEVNULL,
                          stderr=subprocess.DEVNULL).returncode != 0:
            raise RuntimeError(f"setup failed: {args}")
    term = Pty(binary, env, ("--suite", "duo", "--no-recall"), command=("execute",))
    term.drain(2.0)
    # The symphony's name by Tab, from execute's table and its catalog.
    term.send(b"/play su")
    term.drain(0.5)
    term.send(b"\t")
    term.drain(0.5)
    term.send(b" The cat sat on the mat.\r")
    term.drain(3.0)
    term.send(b"/exit\r")
    term.drain(2.0)
    term.close()
    text = term.text()
    failures = []
    if b"scripted  \xc2\xb7  suite duo  \xc2\xb7  3 symphonies  \xc2\xb7  chat " not in text:
        failures.append(f"the banner does not name the suite and its symphonies: {text!r}")
    for stage in ("stage 1/2 summarize \u2014 asking utility (helper)",
                  "stage 2/2 verify \u2014 asking chat (scripted)"):
        if stage.encode() not in text:
            failures.append(f"'{stage}' was not narrated: {text!r}")
    if b"Worked for" not in text:
        failures.append(f"the block of stages did not collapse to 'Worked for': {text!r}")
    if b"The final summary." not in text or b"**final**" in text:
        failures.append(f"the output was not rendered as the answer: {text!r}")
    saved = sessions(home)
    if len(saved) != 1:
        failures.append(f"expected one saved chat, found {len(saved)}")
    else:
        kept = [(message["role"], json.dumps(message["content"])) for message in saved[0]["messages"]]
        if [role for role, _ in kept] != ["user", "assistant"] or \
                "/play summarize-verify The cat sat on the mat." not in kept[0][1]:
            failures.append(f"the play, its name taken by Tab, was not kept as one exchange: {kept}")
        if "asking utility" in json.dumps(saved[0]):
            failures.append("the narration reached the saved chat")
    return failures


def check_orchestrate(binary, home, env):
    """An orchestrating `execute` at a terminal (27t): the banner says how many
    symphonies the model is offered; the model's own play is a labeled line
    -- its choice and its cost -- with its stage lines beneath, in the
    thinking block, and the answer that reads its output is rendered."""
    helper = os.path.join(home, "helper.json")
    with open(helper, "w", encoding="utf-8") as handle:
        json.dump({"turns": [{"text": "A cat sat on a mat."}]}, handle)
    scripted(binary, env, home, [
        {"text": "", "tool_calls": [{"name": "play_summarize-verify",
                                     "arguments": json.dumps({"input": "The cat sat on the mat."})}]},
        {"text": "A cat sat on the mat."},
        {"text": "The **played** summary.\n"}])
    for args in (["config", "add-backend", "helper", "--type", "mock", "--model-path", helper],
                 ["config", "add-suite", "duo", "--chat", "scripted", "--utility", "helper"]):
        if subprocess.run([binary, *args], env=env, stdout=subprocess.DEVNULL,
                          stderr=subprocess.DEVNULL).returncode != 0:
            raise RuntimeError(f"setup failed: {args}")
    term = Pty(binary, env, ("--suite", "duo", "--orchestrate", "--no-recall"),
               command=("execute",))
    term.drain(2.0)
    term.send(b"Summarize: the cat sat on the mat.\r")
    term.drain(3.0)
    term.send(b"/exit\r")
    term.drain(2.0)
    term.close()
    text = term.text()
    failures = []
    if b"3 symphonies  \xc2\xb7  orchestrating 2  \xc2\xb7  chat " not in text:
        failures.append(f"the banner does not say what the model is offered: {text!r}")
    for line in ("play \u2014 the model chose summarize-verify: 2 member calls, utility \u2192 chat",
                 "stage 1/2 summarize \u2014 asking utility (helper)",
                 "stage 2/2 verify \u2014 asking chat (scripted)"):
        if line.encode() not in text:
            failures.append(f"'{line}' was not narrated: {text!r}")
    # What it costs: the labeled line says its member calls, and each stage
    # closes with what it took.
    if not re.search(rb"asking chat \(scripted\): [^\n]*\xc2\xb7 [0-9.]+ s", text):
        failures.append(f"the play's stages did not close with their cost: {text!r}")
    if b"Worked for" not in text:
        failures.append(f"the block did not collapse to 'Worked for': {text!r}")
    if b"The played summary." not in text or b"**played**" in text:
        failures.append(f"the answer was not rendered: {text!r}")
    return failures


def check_base_model(binary, home, env):
    """A base model's chat: `base model` in the banner and the spinner, and
    `tools off` said once (26r)."""
    script = os.path.join(home, "script.json")
    with open(script, "w", encoding="utf-8") as handle:
        json.dump({"turns": [{"text": "It continues the text.\n", "delay_ms": 600}],
                   "base_model": True}, handle)
    for args in (["config", "add-backend", "scripted", "--type", "mock",
                  "--model-path", script],
                 ["config", "set-default", "scripted"]):
        if subprocess.run([binary, *args], env=env,
                          stdout=subprocess.DEVNULL).returncode != 0:
            raise RuntimeError(f"setup failed: {args}")
    term = Pty(binary, env, ("--tools",))
    term.drain(2.0)
    term.send(b"hello\r")
    term.drain(3.0)
    term.send(b"/exit\r")
    term.drain(2.0)
    term.close()
    text = term.text()
    failures = []
    if b"scripted  \xc2\xb7  base model  \xc2\xb7  chat " not in text:
        failures.append(f"the banner does not say base model: {text!r}")
    if text.count(b"tools off: scripted is a base model") != 1:
        failures.append(f"tools off was not said exactly once: {text!r}")
    if "Thinking… · base model".encode() not in text:
        failures.append(f"the spinner did not say base model: {text!r}")
    if b"It continues the text." not in text:
        failures.append(f"the turn did not run: {text!r}")
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
                        ("side-calls", check_side_calls),
                        ("presets", check_presets),
                        ("task-attended", check_task_attended),
                        ("task-machine", check_task_machine),
                        ("base-model", check_base_model),
                        ("execute", check_execute),
                        ("orchestrate", check_orchestrate),
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
          "typing mid-reply waits for the prompt; side calls are narrated in the block; a preset is the session answer given early; an attended task asks for what it was not granted, and one in machine mode asks nothing; a base model is said and its tools are off; "
          "execute's banner names its suite and symphonies and a play is narrated in the block; "
          "the model's own play is a labeled line with its stages and its cost; "
          "Ctrl-C restores echo - OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
