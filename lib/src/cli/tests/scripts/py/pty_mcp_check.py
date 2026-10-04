#!/usr/bin/env python3
"""A noisy, dying MCP server must not paint the terminal, under a real PTY.

Ommi's postmortem (its Milestone P): `chat` came up behind a wall of output
because a stdio MCP server inherited stderr and narrated its whole handshake
onto the terminal, and a server that died mid-handshake held the status line
for the full connect timeout.  Both are locked here on the real binary:

  * every row of the startup region is Apogee's own -- `[mcp]`, `[apogee]`,
    the answer -- and the server's NOISE_ lines appear nowhere;
  * the connect failure is reported, WITH the server's last stderr line folded
    into it (discarding stderr costs no diagnostics);
  * it all happens well inside the connect timeout, because a dead server
    releases its waiters at once.

POSIX only -- `pty` has no Windows equivalent.
"""

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
        return 0
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
