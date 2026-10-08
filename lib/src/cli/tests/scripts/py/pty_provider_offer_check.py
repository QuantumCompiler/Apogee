#!/usr/bin/env python3
"""The first-launch registration offer (28b), under a real pseudo-terminal.

The offer exists only where a person can answer it -- stdin and stdout both a
terminal -- so a pipe-based test proves only that it stays silent.  This one
drives `apogee chat` the way a person meets it, with fake `claude` and `codex`
on PATH and a scan already cached:

  decline   The first interactive chat asks once, naming both; `n` leaves
            the config byte-identical and the chat runs; the next chat asks
            nothing.
  accept    In a fresh install, `y` registers both, names each entry, and the
            chat runs on.

POSIX only -- `pty` has no Windows equivalent.
"""

import os
import pty
import selectors
import shutil
import subprocess
import sys
import tempfile
import time


def sandbox(binary, root):
    bin_dir = os.path.join(root, "bin")
    home = os.path.join(root, "home")
    apogee_home = os.path.join(root, "apogee")
    os.makedirs(bin_dir)
    os.makedirs(home)
    with open(os.path.join(bin_dir, "claude"), "w", encoding="utf-8") as out:
        out.write("#!/bin/sh\necho '9.9.9 (Claude Code)'\n")
    with open(os.path.join(bin_dir, "codex"), "w", encoding="utf-8") as out:
        out.write("#!/bin/sh\n[ \"$1\" = login ] && exit 0\necho 'codex-cli 9.9.9'\n")
    for tool in ("claude", "codex"):
        os.chmod(os.path.join(bin_dir, tool), 0o755)
    env = dict(os.environ)
    env.update({"APOGEE_HOME": apogee_home, "HOME": home,
                "PATH": bin_dir + ":/usr/bin:/bin"})
    for key in ("ANTHROPIC_API_KEY", "OPENAI_API_KEY", "GEMINI_API_KEY", "GOOGLE_API_KEY",
                "NO_COLOR"):
        env.pop(key, None)
    for args in (["config", "init"],
                 ["config", "add-backend", "mock", "--type", "mock", "--model", "mock-1"],
                 ["config", "set-default", "mock"],
                 ["providers", "scan"]):
        if subprocess.run([binary, *args], env=env, stdin=subprocess.DEVNULL,
                          stdout=subprocess.DEVNULL).returncode != 0:
            raise RuntimeError(f"setup failed: {args}")
    return env, os.path.join(apogee_home, "config", "config.yaml")


def chat(binary, env, answer):
    """Runs one interactive chat: answers the offer with `answer` if it is
    asked, then exits.  Returns everything the terminal showed."""
    primary, secondary = pty.openpty()
    process = subprocess.Popen([binary, "chat"], stdin=secondary, stdout=secondary,
                               stderr=secondary, env=env, close_fds=True)
    os.close(secondary)
    selector = selectors.DefaultSelector()
    selector.register(primary, selectors.EVENT_READ)
    seen = bytearray()

    def drain(seconds, until=None):
        deadline = time.time() + seconds
        while time.time() < deadline:
            if until is not None and until in seen:
                return True
            if not selector.select(timeout=0.1):
                continue
            try:
                data = os.read(primary, 65536)
            except OSError:
                return False
            if not data:
                return False
            seen.extend(data)
        return until is not None and until in seen

    asked = drain(10.0, until=b"[y/N]")
    if asked and answer is not None:
        os.write(primary, answer)  # canonical mode still: \n ends the line
    drain(3.0)
    os.write(primary, b"/exit\r")
    drain(3.0)
    selector.close()
    os.close(primary)
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
        raise RuntimeError("chat did not exit")
    return seen.decode("utf-8", errors="replace"), asked


def read(path):
    with open(path, "rb") as handle:
        return handle.read()


def main():
    binary = sys.argv[1]
    root = tempfile.mkdtemp(prefix="apogee-offer-")
    failures = []
    try:
        env, config = sandbox(binary, os.path.join(root, "decline"))
        before = read(config)
        screen, asked = chat(binary, env, b"n\n")
        if not asked:
            failures.append(f"decline: the first interactive chat did not ask:\n{screen}")
        elif "Found claude and codex -- register them as backends?" not in screen:
            failures.append(f"decline: the offer did not name both:\n{screen}")
        if "providers scan --register" not in screen:
            failures.append("decline: the way back was not named")
        if read(config) != before:
            failures.append("decline: the config changed")
        screen, asked = chat(binary, env, None)
        if asked or "register" in screen:
            failures.append(f"decline: the second chat asked again:\n{screen}")
        if read(config) != before:
            failures.append("decline: the second chat changed the config")

        env, config = sandbox(binary, os.path.join(root, "accept"))
        screen, asked = chat(binary, env, b"y\n")
        if not asked:
            failures.append(f"accept: not asked:\n{screen}")
        for line in ("registered claude (claude-cli)", "registered codex (codex-cli)"):
            if line not in screen:
                failures.append(f"accept: '{line}' not said:\n{screen}")
        text = read(config).decode("utf-8")
        if "type: claude-cli" not in text or "type: codex-cli" not in text:
            failures.append("accept: the entries are not in the config")
        screen, asked = chat(binary, env, None)
        if asked:
            failures.append("accept: asked again after registering")
    finally:
        shutil.rmtree(root, ignore_errors=True)

    if failures:
        for failure in failures:
            print("provider offer: " + failure, file=sys.stderr)
        return 1
    print("provider offer: asked once, declined untouched, accepted registered - OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
