#!/usr/bin/env python3
"""A noisy, dying MCP server must not paint the terminal, under a real PTY.

Two failures this guards against: `chat` coming up behind a wall of output
because a stdio MCP server inherits stderr and narrates its whole handshake
onto the terminal, and a server that dies mid-handshake holding the status line
for the full connect timeout.  Both are locked here on the real binary:

  * every row of the startup region is Apogee's own -- `[mcp]`, `[apogee]`,
    the answer -- and the server's NOISE_ lines appear nowhere;
  * the connect failure is reported, WITH the server's last stderr line folded
    into it (discarding stderr costs no diagnostics);
  * it all happens well inside the connect timeout, because a dead server
    releases its waiters at once.

Since 47b a second phase does the same for the code-execution server Apogee
ships: a model's call whose code prints hundreds of lines on both streams
goes to the model as the tool's result, and none of it -- nor anything the
server says -- reaches the screen.

POSIX only -- `pty` has no Windows equivalent.
"""

import json
import os
import shutil
import stat
import subprocess
import sys
import tempfile
import time

sys.dont_write_bytecode = True  # importing a sibling must not litter the tests tree
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_startup_check import drive, render  # noqa: E402

NOISY_SERVER = """#!/bin/sh
echo "[9999] NOISE_USING_EXISTING_CLIENT_PORT: 3736" >&2
echo "[9999] NOISE_DISCOVERING_OAUTH_CONFIGURATION" >&2
sleep 0.5
echo "[9999] NOISE_FATAL_CONNECTION_REFUSED" >&2
exit 1
"""


def main():
    if len(sys.argv) < 2:
        print("usage: pty_mcp_check.py <apogee-binary>", file=sys.stderr)
        return 2
    binary = sys.argv[1]
    work = tempfile.mkdtemp(prefix="apogee-pty-mcp-")
    try:
        env = dict(os.environ)
        env["APOGEE_HOME"] = work
        env.pop("NO_COLOR", None)
        server = os.path.join(work, "noisy.sh")
        with open(server, "w", encoding="utf-8") as handle:
            handle.write(NOISY_SERVER)
        os.chmod(server, os.stat(server).st_mode | stat.S_IXUSR)

        for args in (
            ["config", "init"],
            ["config", "add-backend", "mock", "--type", "mock"],
            ["config", "set-default", "mock"],
            ["mcp", "create", "noisy", "--command", server],
        ):
            if subprocess.run([binary, *args], env=env, stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL).returncode != 0:
                print(f"setup failed: {args}", file=sys.stderr)
                return 1

        started = time.monotonic()
        raw, code = drive(binary, ["complete", "--tools", "hello"], env)
        elapsed = time.monotonic() - started
        if code != 0:
            print(f"apogee exited {code}\n{raw!r}", file=sys.stderr)
            return 1

        screen = render(raw)
        text = "\n".join(screen)
        failures = []

        if "mock response" not in text:
            failures.append(f"the answer is missing:\n{screen!r}")
        # The server's own stderr never reached the terminal as itself.
        for row in screen:
            if row.startswith("[9999]"):
                failures.append(f"a raw server stderr line reached the terminal: {row!r}")
        # Every non-empty row is Apogee's own.
        for row in screen:
            if row.strip() and not (row.startswith("[mcp]") or row.startswith("[apogee]")
                                    or "mock response" in row):
                failures.append(f"a row that is not Apogee's own: {row!r}")
        # The failure is reported, with the tail folded in, on one row.
        warning_rows = [r for r in screen if "connect failed" in r]
        if len(warning_rows) != 1:
            failures.append(f"expected exactly one connect-failed row, saw {len(warning_rows)}:\n{screen!r}")
        elif "NOISE_FATAL_CONNECTION_REFUSED" not in warning_rows[0]:
            failures.append(f"the stderr tail was not folded into the failure: {warning_rows[0]!r}")
        # A dead server never costs the connect timeout (20 s).
        if elapsed > 10:
            failures.append(f"the dead server held startup for {elapsed:.1f}s")

        if failures:
            for failure in failures:
                print(f"FAIL: {failure}", file=sys.stderr)
            return 1
        print("a noisy dying MCP server stays off the terminal and fails fast - OK")
    finally:
        shutil.rmtree(work, ignore_errors=True)
    return code_exec_phase(binary)


# The code a model runs through the code-execution server (47b), loud on both
# streams. The marker is built at run time, so the call's own arguments --
# which a tool line may show -- never spell it. It leaves a file at its end,
# which a second, quiet call lists into the answer: proof the noisy run ran.
NOISY_CODE = (
    "import sys\n"
    "mark = 'NOI' + 'SE_'\n"
    "for i in range(200):\n"
    "    print(mark + 'OUT', i)\n"
    "    print(mark + 'ERR', i, file=sys.stderr)\n"
    "open('noisy-done.txt', 'w').write('done')\n"
)
QUIET_CODE = "import os\nprint(sorted(os.listdir('.')))\n"


def code_exec_phase(binary):
    """A noisy run through the seeded code-execution server: everything the
    code prints goes to the model as the tool's result, and none of it -- nor
    anything the server says -- reaches the terminal."""
    if shutil.which("python3") is None:
        print("the code-exec phase needs python3; skipping it")
        return 0
    work = tempfile.mkdtemp(prefix="apogee-pty-code-exec-")
    try:
        env = dict(os.environ)
        env["APOGEE_HOME"] = work
        env.pop("NO_COLOR", None)
        script = os.path.join(work, "training", "scripts", "code_exec_server.py")
        runner = os.path.join(work, "runner.json")
        with open(runner, "w", encoding="utf-8") as handle:
            json.dump({"turns": [
                {"text": "", "tool_calls": [
                    {"name": "mcp__code__run_python", "arguments": {"code": NOISY_CODE}}]},
                {"text": "", "tool_calls": [
                    {"name": "mcp__code__run_python", "arguments": {"code": QUIET_CODE}}]},
                {"text": "ran it quietly: {{last_tool_result}}"},
            ]}, handle)
        for args in (
            ["config", "init"],
            ["check", "--fix"],
            ["mcp", "create", "code", "--command", "python3", "--args", script],
            ["config", "add-backend", "runner", "--type", "mock", "--model-path", runner],
        ):
            done = subprocess.run([binary, *args], env=env, stdin=subprocess.DEVNULL,
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            if done.returncode != 0 and args[0] != "check":
                print(f"setup failed: {args}", file=sys.stderr)
                return 1

        raw, code = drive(binary, ["complete", "-m", "runner", "--tools", "--allow",
                                   "mcp__code__run_python", "hello"], env)
        if code != 0:
            print(f"apogee exited {code}\n{raw!r}", file=sys.stderr)
            return 1
        screen = render(raw)
        text = "\n".join(screen)
        failures = []
        if "ran it quietly: ['noisy-done.txt']" not in text:
            failures.append(f"the answer is missing, or the noisy run never ran:\n{screen!r}")
        if "NOISE_" in raw.decode("utf-8", "replace"):
            failures.append(f"the code's output reached the terminal:\n{screen!r}")
        if "code-exec:" in text:
            failures.append(f"the server's own stderr reached the terminal:\n{screen!r}")
        if failures:
            for failure in failures:
                print(f"FAIL: {failure}", file=sys.stderr)
            return 1
        print("a noisy code-exec run stays off the terminal - OK")
        return 0
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
