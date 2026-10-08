#!/usr/bin/env python3
"""The busy line (M1), on a real pseudo-terminal and on pipes.

`apogee models list`, `check` and `graph build` open a busy line around their
slow sweeps: one stderr line, repainted in place, gone before the results
print. What this locks, on the real binary:

  * on a terminal the line appears while the sweep works, names the phase and
    the count, repaints in place, never reaches the last column, and leaves no
    residue -- the final screen holds only the results;
  * stdout is the answer whatever stderr is: `models list` piped prints the
    same bytes with stderr a terminal (where the line paints) or a pipe, and
    holds no escape byte;
  * a pipe, and `--quiet`, get no frame at all;
  * a sweep finishing under the 150 ms gate never flickers a frame.

A slow sweep without a test seam in the product: a model's sidecar, or an
agent's schema, is a named pipe, so the read blocks until this script writes
it -- after it has seen the line on the terminal. The extractor and the
summariser are the scripted mock, slowed by its own `delay_ms` (a pause before
each streamed piece of an answer, so a small one adds up); an ingest is slow
by being big.

POSIX only -- `pty` and named pipes have no Windows equivalent (a recorded
per-item skip).
"""

import fcntl
import json
import os
import pty
import select
import shutil
import struct
import subprocess
import sys
import tempfile
import termios
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_startup_check import render  # noqa: E402

WIDTH = 60
ERASE = b"\x1b[2K"
GLYPHS = ("✻", "✽", "✼", "✺")


def minimal_gguf():
    """A GGUF header with no tensors and no metadata: parseable, and tiny."""
    return b"GGUF" + struct.pack("<IQQ", 3, 0, 0)


def run(binary, args, env, out_tty, err_tty, release=None, timeout=60.0):
    """Runs `binary`, each of stdout and stderr on a PTY or a pipe.

    `release(pty_bytes, elapsed)` is called as output arrives and returns True
    once it has let the sweep go on; it is not called again after that.
    Returns (pty bytes, stdout pipe bytes, stderr pipe bytes, exit code).
    """
    primary = secondary = None
    if out_tty or err_tty:
        primary, secondary = pty.openpty()
        fcntl.ioctl(secondary, termios.TIOCSWINSZ, struct.pack("HHHH", 24, WIDTH, 0, 0))
    process = subprocess.Popen(
        [binary, *args],
        stdout=secondary if out_tty else subprocess.PIPE,
        stderr=secondary if err_tty else subprocess.PIPE,
        stdin=subprocess.DEVNULL,
        env=env,
        close_fds=True,
    )
    if secondary is not None:
        os.close(secondary)

    streams = {}
    if primary is not None:
        streams[primary] = []
    if process.stdout is not None:
        streams[process.stdout.fileno()] = []
    if process.stderr is not None:
        streams[process.stderr.fileno()] = []
    open_streams = set(streams)

    started = time.monotonic()
    released = release is None
    while open_streams:
        if time.monotonic() - started > timeout:
            process.kill()
            raise RuntimeError(f"{args} did not finish in {timeout}s")
        ready, _, _ = select.select(list(open_streams), [], [], 0.05)
        for fd in ready:
            try:
                data = os.read(fd, 65536)
            except OSError:
                data = b""
            if not data:
                open_streams.discard(fd)
                continue
            streams[fd].append(data)
        if not released:
            tty_bytes = b"".join(streams[primary]) if primary is not None else b""
            released = release(tty_bytes, time.monotonic() - started)
    process.wait(timeout=timeout)
    if primary is not None:
        os.close(primary)

    def joined(fd):
        return b"".join(streams[fd]) if fd in streams else b""

    return (
        joined(primary) if primary is not None else b"",
        joined(process.stdout.fileno()) if process.stdout is not None else b"",
        joined(process.stderr.fileno()) if process.stderr is not None else b"",
        process.returncode,
    )


def frames(raw):
    """Each painted frame: what follows an erase, up to the next control byte,
    when it opens with a spinner glyph -- the erase that clears the line is
    followed by the command's own output, which is no frame."""
    out = []
    for piece in raw.split(ERASE)[1:]:
        end = len(piece)
        for stop in (b"\x1b", b"\r", b"\n"):
            at = piece.find(stop)
            if at != -1:
                end = min(end, at)
        text = piece[:end].decode("utf-8", errors="replace")
        if text.startswith(GLYPHS):
            out.append(text)
    return out


def fill_fifo(path, content):
    """Writes `content` into the named pipe a blocked read is waiting on."""
    with open(path, "wb") as fifo:
        fifo.write(content)


def release_after_frames(fifo, content, label, count=2, patience=10.0):
    """Lets the sweep go once `count` frames naming `label` have painted --
    or after `patience` seconds, so a binary with no line fails on the
    frames it did not paint rather than hanging."""

    def release(raw, elapsed):
        if sum(1 for frame in frames(raw) if label in frame) >= count or elapsed >= patience:
            fill_fifo(fifo, content)
            return True
        return False

    return release


def release_after(fifo, content, seconds):
    """Lets the sweep go after `seconds` -- for runs that must paint nothing."""

    def release(_raw, elapsed):
        if elapsed >= seconds:
            fill_fifo(fifo, content)
            return True
        return False

    return release


def check_screen(name, raw, results, failures):
    """The final screen holds `results` and no trace of the busy line."""
    screen = render(raw)
    text = "\n".join(screen)
    for needle in results:
        if needle not in text:
            failures.append(f"{name}: {needle!r} is not on the final screen:\n{screen!r}")
    for glyph in GLYPHS:
        if glyph in text:
            failures.append(f"{name}: a busy frame survived ({glyph!r}):\n{screen!r}")


def check_frames(name, raw, label, failures, counted=True):
    """The line appeared, repainted in place, named the phase, and fit."""
    painted = frames(raw)
    named = [frame for frame in painted if label in frame]
    if len(named) < 2:
        failures.append(f"{name}: expected the line to repaint naming {label!r}, saw {painted!r}")
    if counted and not any("/" in frame and "(" in frame for frame in named):
        failures.append(f"{name}: no frame carried a count: {named!r}")
    for frame in painted:
        # Every glyph the frames use is one cell wide, so characters are cells.
        if len(frame) > WIDTH - 1:
            failures.append(f"{name}: a frame reached the last column ({len(frame)}): {frame!r}")


def main():
    if len(sys.argv) < 2:
        print("usage: pty_busy_check.py <apogee-binary>", file=sys.stderr)
        return 2
    binary = sys.argv[1]

    work = tempfile.mkdtemp(prefix="apogee-busy-")
    try:
        env = dict(os.environ)
        env["APOGEE_HOME"] = os.path.join(work, "home")
        env.pop("NO_COLOR", None)
        for key in ("ANTHROPIC_API_KEY", "OPENAI_API_KEY", "GEMINI_API_KEY", "GOOGLE_API_KEY"):
            env.pop(key, None)

        def setup(*args):
            result = subprocess.run([binary, *args], env=env, stdin=subprocess.DEVNULL,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            if result.returncode != 0:
                raise RuntimeError(f"setup failed: {args}\n{result.stdout.decode()}")

        setup("config", "init")
        setup("check", "--fix")

        model_dir = os.path.join(work, "slow")
        os.makedirs(model_dir)
        # A long name, so every frame is cut to the width -- measured on
        # stderr, where the line paints, even when stdout is a pipe.
        stem = "slow-" + "x" * 60
        model = os.path.join(model_dir, stem + ".gguf")
        with open(model, "wb") as out:
            out.write(minimal_gguf())
        setup("config", "add-backend", "slow", "--type", "llamacpp", "--model-path", model)
        sidecar = os.path.join(model_dir, stem + ".json")

        failures = []

        # --- models list on a terminal ----------------------------------------
        os.mkfifo(sidecar)
        raw, _, _, code = run(binary, ["models", "list"], env, True, True,
                              release_after_frames(sidecar, b"{}", "reading model headers"))
        if code != 0:
            failures.append(f"models list exited {code}:\n{raw!r}")
        check_frames("models list", raw, "reading model headers: slow-", failures)
        check_screen("models list", raw, ["slow", "BACKEND"], failures)

        # --- the pipe contract ------------------------------------------------
        # Both streams piped: no decoration anywhere. The sweep is held past
        # the gate, so a line that ignored the pipe would have painted.
        _, piped_out, piped_err, _ = run(binary, ["models", "list", "--no-color"], env, False,
                                         False, release_after(sidecar, b"{}", 0.6))
        if piped_err:
            failures.append(f"models list piped wrote to stderr: {piped_err!r}")
        if b"\x1b" in piped_out:
            failures.append(f"models list piped carries escape bytes: {piped_out!r}")
        # `apogee models list | cat`: stdout a pipe, stderr the terminal the
        # line paints on -- and stdout byte for byte the same.
        raw, tty_out, _, _ = run(binary, ["models", "list", "--no-color"], env, False, True,
                                 release_after_frames(sidecar, b"{}", "reading model headers"))
        check_frames("models list | cat", raw, "reading model headers", failures)
        if tty_out != piped_out:
            failures.append("models list's stdout changed with stderr a terminal:\n"
                            f"{tty_out!r}\nvs\n{piped_out!r}")

        # --- --quiet, JSON, and a sweep under the gate -------------------------
        for args in (["--quiet"], ["--output-format", "stream-json"]):
            raw, _, _, _ = run(binary, ["models", "list", *args], env, True, True,
                               release_after(sidecar, b"{}", 0.6))
            if ERASE in raw or any(glyph.encode() in raw for glyph in GLYPHS):
                failures.append(f"models list {args} painted a frame:\n{raw!r}")
        os.remove(sidecar)
        raw, _, _, _ = run(binary, ["models", "list"], env, True, True)
        if ERASE in raw:
            failures.append(f"a sweep under 150 ms flickered a frame:\n{raw!r}")

        # --- check --------------------------------------------------------------
        prompt = os.path.join(work, "slow-prompt.md")
        with open(prompt, "w") as out:
            out.write("You are slow.\n")
        schema = os.path.join(work, "slow-schema.json")
        config = os.path.join(env["APOGEE_HOME"], "config", "config.json")
        with open(config) as source:
            text = source.read()
        # The JSONC config (28i): the section first, under the opening brace.
        at = text.index("\n{\n") + 3
        agents = ('  "agents": {"slow": {"prompts": [%s], "schemas": [%s]}},\n'
                  % (json.dumps(prompt), json.dumps(schema)))
        with open(config, "w") as out:
            out.write(text[:at] + agents + text[at:])
        os.mkfifo(schema)
        schema_text = b'{"type": "object", "properties": {}}'
        raw, _, _, _ = run(binary, ["check"], env, True, True,
                           release_after_frames(schema, schema_text, "checking agents"))
        check_frames("check", raw, "checking agents", failures, counted=False)
        check_screen("check", raw, ["agent: slow"], failures)
        raw, _, _, _ = run(binary, ["check", "--quiet"], env, True, True,
                           release_after(schema, schema_text, 0.6))
        if ERASE in raw or any(glyph.encode() in raw for glyph in GLYPHS):
            failures.append(f"check --quiet painted a frame:\n{raw!r}")
        os.remove(schema)
        with open(schema, "wb") as out:
            out.write(schema_text)

        # --- graph build ----------------------------------------------------------
        docs = os.path.join(work, "docs")
        os.makedirs(docs)
        with open(os.path.join(docs, "atlas.md"), "w") as out:
            out.write("Atlas collects readings from the field probes every night.\n")
        with open(os.path.join(docs, "vault.md"), "w") as out:
            out.write("The warehouse keeps every record for seven years.\n")
        extraction = json.dumps({
            "entities": [{"name": "Atlas", "type": "system", "description": "collects"},
                         {"name": "Vault", "type": "system", "description": "stores"}],
            "relations": [{"source": "Atlas", "target": "Vault", "relation": "writes to",
                           "description": ""}]})
        script = os.path.join(work, "extractor.json")
        with open(script, "w") as out:
            json.dump({"turns": [{"text": extraction, "delay_ms": 20}]}, out)
        setup("config", "add-backend", "extractor", "--type", "mock", "--model-path", script)
        summary = os.path.join(work, "summarizer.json")
        with open(summary, "w") as out:
            json.dump({"turns": [{"text": "Atlas writes to the Vault.", "delay_ms": 150}]}, out)
        setup("config", "add-backend", "summarizer", "--type", "mock", "--model-path", summary)
        setup("embed", "ingest", "notes", docs, "--retriever", "lexical")
        raw, _, _, code = run(binary, ["graph", "build", "notes", "-m", "extractor"], env, True,
                              True)
        if code != 0:
            failures.append(f"graph build exited {code}:\n{raw!r}")
        check_frames("graph build", raw, "extracting", failures)
        check_screen("graph build", raw, ["Graph build complete"], failures)
        raw, _, _, _ = run(binary, ["graph", "build", "notes", "-m", "extractor", "--force",
                                    "--quiet"], env, True, True)
        # Progress is silenced; what the build says it did, and why, is not.
        if (ERASE in raw or b"[graph] extracting" in raw or b"[graph] embedding" in raw
                or any(glyph.encode() in raw for glyph in GLYPHS)):
            failures.append(f"graph build --quiet showed progress:\n{raw!r}")

        # --- graph communities -------------------------------------------------
        community = ["graph", "communities", "notes", "-m", "summarizer", "--min-size", "2"]
        raw, _, _, code = run(binary, community, env, True, True)
        if code != 0:
            failures.append(f"graph communities exited {code}:\n{raw!r}")
        check_frames("graph communities", raw, "summarising communities", failures)
        check_screen("graph communities", raw, ["Communities for"], failures)
        raw, _, _, _ = run(binary, [*community, "--force", "--quiet"], env, True, True)
        if ERASE in raw or b"[graph] summarising" in raw:
            failures.append(f"graph communities --quiet showed progress:\n{raw!r}")

        # --- embed ingest --------------------------------------------------------
        # Enough files that the walk outlasts the gate on any machine: each is
        # read and stored in turn, and the line counts them.
        bulk = os.path.join(work, "bulk")
        os.makedirs(bulk)
        for index in range(8000):
            with open(os.path.join(bulk, f"f{index:05d}.md"), "w") as out:
                out.write(f"document {index} is about topic {index % 17}\n")
        raw, _, _, code = run(binary, ["embed", "ingest", "bulk", bulk, "--retriever",
                                       "lexical"], env, True, True)
        if code != 0:
            failures.append(f"embed ingest exited {code}:\n{raw[-2000:]!r}")
        check_frames("embed ingest", raw, "reading f", failures)
        check_screen("embed ingest", raw, ["8000 file(s)"], failures)
        raw, _, _, _ = run(binary, ["embed", "ingest", "bulk", bulk, "--retriever", "lexical",
                                    "--quiet"], env, True, True)
        if ERASE in raw or any(glyph.encode() in raw for glyph in GLYPHS):
            failures.append(f"embed ingest --quiet painted a frame:\n{raw[-2000:]!r}")

        if failures:
            for failure in failures:
                print(f"FAIL: {failure}", file=sys.stderr)
            return 1
        print("the busy line appears, repaints in place, clears, and stays off pipes - OK")
        return 0
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
