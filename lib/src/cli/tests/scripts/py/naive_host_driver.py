#!/usr/bin/env python3
"""The naive host: a third-party application embedding `apogee chat`.

Spike evidence for backlog item 27 (the machine-mode integration contract).
This driver deliberately knows ONLY what lib/documentation/reference/
machine-mode.md says -- it is NOT reference_driver.py, and it must not learn
from Apogee's source. Where the documented contract leaves it blind, it records
a WALL with the evidence instead of peeking.

It runs three measurements against a real binary in a throwaway APOGEE_HOME:

  1. THE CONVERSATION -- embed `apogee chat` through a tool-using,
     ask_user-answering, permission-prompted conversation, exactly as an
     integrator's application would, and log every wall hit on the way.
  2. HOST-SUPPLIED TOOLS -- the host offers its own tool to Apogee's agent
     loop through the only currently-existing path: running an MCP stdio
     server and registering it with `apogee mcp create`. Measures what that
     achieves today and what is missing (per-run wiring).
  3. THE READ SURFACE -- what a host UI gets when it shells out to the read
     commands the protocol delegates to ("everything else is a CLI command").

Usage: naive_host_driver.py <apogee-binary> <work-dir>
Exit 0 = the conversation completed and the evidence was written; the walls
are observations, not failures. Everything it learns lands in
<work-dir>/findings/ (transcripts, walls.md).
"""

import json
import os
import selectors
import shutil
import subprocess
import sys
import time
from pathlib import Path

DOCUMENTED_EVENTS = {
    # The whole outbound vocabulary machine-mode.md names. Anything else is an
    # unknown type the doc tells us to ignore (rule 1).
    "session", "thinking", "thinking_delta", "tool_status",
    "answer_start", "answer_delta", "answer_end", "result",
    "question", "error",
}

WALLS = []          # [(id, title, evidence)]
TRANSCRIPT = []     # every JSONL line both directions, annotated


def wall(wall_id: str, title: str, evidence: str) -> None:
    WALLS.append((wall_id, title, evidence))
    print(f"  WALL {wall_id}: {title}", flush=True)


def note(direction: str, line: str) -> None:
    TRANSCRIPT.append(f"{direction} {line}")


def run_cli(binary, args, env, cwd, may_fail=False):
    """A host performing a mutation or read the documented way: a CLI command."""
    proc = subprocess.run([binary, *args], env=env, cwd=cwd, capture_output=True,
                          text=True, stdin=subprocess.DEVNULL, timeout=60)
    if proc.returncode != 0 and not may_fail:
        sys.exit(f"cli {' '.join(args)} failed: {proc.stderr.strip()}")
    return proc


class Child:
    """One embedded `apogee chat` over its pipes, per the doc's three rules."""

    def __init__(self, binary, env, cwd, log_name):
        self.proc = subprocess.Popen(
            [binary, "chat", "--output-format", "stream-json",
             "--input-format", "stream-json", "--tools"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,  # rule 3: stderr is read separately
            env=env, cwd=cwd, text=True, bufsize=1)
        self.sel = selectors.DefaultSelector()
        self.sel.register(self.proc.stdout, selectors.EVENT_READ)
        self.log_name = log_name
        self.unknown_types = []

    def send(self, obj) -> None:
        line = json.dumps(obj)
        note(f"[{self.log_name}] ->", line)
        self.proc.stdin.write(line + "\n")
        self.proc.stdin.flush()

    def events(self, deadline_s=90):
        """Yield documented events until the stream ends; ignore unknowns."""
        deadline = time.monotonic() + deadline_s
        while True:
            if time.monotonic() > deadline:
                sys.exit(f"[{self.log_name}] timed out waiting for events")
            if not self.sel.select(timeout=1.0):
                if self.proc.poll() is not None:
                    return
                continue
            line = self.proc.stdout.readline()
            if line == "":
                return
            line = line.rstrip("\n")
            if not line:
                continue
            note(f"[{self.log_name}] <-", line)
            try:
                event = json.loads(line)
            except json.JSONDecodeError:
                sys.exit(f"[{self.log_name}] stdout carried a non-JSON line "
                         f"(the doc forbids this): {line!r}")
            etype = event.get("type", "")
            if etype not in DOCUMENTED_EVENTS:
                self.unknown_types.append(etype)  # rule 1: tolerate, note
                continue
            yield event

    def close(self) -> int:
        try:
            self.proc.stdin.close()
        except BrokenPipeError:
            pass
        status = self.proc.wait(timeout=30)
        self.stderr_tail = self.proc.stderr.read()
        return status


def drive_conversation(child: Child, turns):
    """Send user turns; answer questions per the doc; collect results."""
    results, questions_seen = [], []
    turn_iter = iter(turns)
    child.send({"type": "user", "text": next(turn_iter)})
    for event in child.events():
        if event["type"] == "question":
            questions_seen.append(event)
            # A permission question carries kind/tool/target; ask_user does
            # not. The host's whole dispatch is this one field.
            if event.get("kind") == "permission":
                child.send({"type": "answer", "text": "yes"})
            else:
                for _ in event.get("questions", [{}]):
                    child.send({"type": "answer", "text": "blue"})
        elif event["type"] == "result":
            results.append(event)
            try:
                child.send({"type": "user", "text": next(turn_iter)})
            except StopIteration:
                break
    return results, questions_seen


HOST_MCP_SERVER = r'''#!/usr/bin/env python3
"""The host application's own tool, served over MCP stdio -- 40 lines."""
import json, sys

TOOL = {
    "name": "host_lookup",
    "description": "Look up a value only the host application knows.",
    "inputSchema": {"type": "object", "properties": {
        "key": {"type": "string"}}, "required": ["key"]},
    "annotations": {"readOnlyHint": True},
}

for raw in sys.stdin:
    raw = raw.strip()
    if not raw:
        continue
    msg = json.loads(raw)
    if "id" not in msg:      # a notification (notifications/initialized)
        continue
    method, mid = msg.get("method"), msg["id"]
    if method == "initialize":
        result = {"protocolVersion": msg["params"].get("protocolVersion", ""),
                  "capabilities": {"tools": {}},
                  "serverInfo": {"name": "naive-host", "version": "0.0.1"}}
    elif method == "tools/list":
        result = {"tools": [TOOL]}
    elif method == "tools/call":
        key = json.loads(json.dumps(msg["params"])).get("arguments", {}).get("key", "?")
        result = {"content": [{"type": "text",
                               "text": f"host-answer:{key}=42-and-sunny"}]}
    else:
        result = {}
    sys.stdout.write(json.dumps({"jsonrpc": "2.0", "id": mid, "result": result}) + "\n")
    sys.stdout.flush()
'''


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    binary, work = sys.argv[1], Path(sys.argv[2]).absolute()
    shutil.rmtree(work, ignore_errors=True)
    home, project, findings = work / "home", work / "project", work / "findings"
    for d in (home, project, findings):
        d.mkdir(parents=True)

    env = dict(os.environ, APOGEE_HOME=str(home))
    env.pop("APOGEE_CONFIG", None)

    # ---- The host's model actor: a scripted mock backend. The script is the
    # conversation's other side; the HOST code above knows nothing of it.
    script = {
        "turns": [
            {"tool_calls": [{"name": "ask_user", "arguments": {"questions": [
                {"header": "Colour", "question": "Blue or green?",
                 "multi_select": False,
                 "options": [{"label": "blue", "description": "Cool"},
                             {"label": "green", "description": "Calm"}]}]}}]},
            {"tool_calls": [{"name": "write_file", "arguments": {
                "path": str(project / "banner.txt"),
                "content": "the banner is blue\n"}}]},
            {"text": "Done: {{last_tool_result}}"},
            {"text": "You're welcome."},
        ]
    }
    (work / "actor.json").write_text(json.dumps(script))

    print("phase 0: sandbox install", flush=True)
    run_cli(binary, ["config", "init"], env, project)
    run_cli(binary, ["config", "add-backend", "actor", "--type", "mock",
                     "--model-path", str(work / "actor.json")], env, project)
    run_cli(binary, ["config", "set-default", "actor"], env, project)

    # ================= Phase 1: the conversation =============================
    print("phase 1: the embedded conversation", flush=True)

    # W1 -- before the first byte: how does a host know what this binary
    # speaks? Nothing to call: the child talks first, and only after a turn.
    wall("W1", "No handshake or discovery: the host learns protocol_version "
               "only from the session event after spawning, and cannot "
               "declare itself or ask what this binary supports",
         "machine-mode.md names no request; `session` is emitted, not asked")

    child = Child(binary, env, project, "chat")
    results, questions = drive_conversation(
        child, ["Which colour should the banner be? Then write it down.",
                "thanks!"])
    status = child.close()

    ok = True
    if status != 0:
        ok = False
        print(f"  FAIL: child exited {status}; stderr: {child.stderr_tail[-2000:]}")
    if len(results) != 2:
        ok = False
        print(f"  FAIL: expected 2 results, saw {len(results)}")
    kinds = [q.get("kind", "ask_user") for q in questions]
    if "permission" not in kinds or len(questions) < 2:
        ok = False
        print(f"  FAIL: expected an ask_user and a permission question, saw {kinds}")
    banner = project / "banner.txt"
    print(f"  conversation: results={len(results)} questions={kinds} "
          f"banner.txt={'written' if banner.exists() else 'ABSENT'} exit={status}")

    # Walls the conversation itself evidences.
    wall("W2", "No machine-readable schema: the host's event vocabulary is "
               "hand-transcribed from prose, and nothing ships to validate a "
               "stream against",
         "DOCUMENTED_EVENTS in this file IS the transcription")
    wall("W3", "No turn or correlation ids: deltas and results belong to 'the "
               "current turn' by position only, so a host cannot pipeline "
               "turns or attribute events after a race",
         "result events carry no id; the driver serializes turns to stay safe")
    wall("W4", "No cancel: the only way out of an in-flight turn is killing "
               "the child or failing the turn by closing stdin",
         "machine-mode.md: closing stdin with a question outstanding fails "
         "the turn; no interrupt message exists")
    if child.unknown_types:
        wall("W5", "Undocumented event types observed (rule 1 absorbed them, "
                   "but a host cannot know if they mattered)",
             f"ignored types: {sorted(set(child.unknown_types))}")
    final_texts = " ".join(r.get("text", "") for r in results)
    if "outside the allowed root" in final_texts:
        wall("W8", "The embedded child's tool sandbox is scoped to the USER'S "
                   "config (fs root = the user's home by default), not to the "
                   "host's workspace: the host cannot declare 'this child "
                   "works in my project directory' per run -- only a config "
                   "mutation (tools.fs_root) changes it. The permission flow "
                   "itself held: the refusal came back as a tool result and "
                   "the turn continued",
         final_texts[:300])

    # ================= Phase 2: host-supplied tools ==========================
    print("phase 2: host-supplied tools over MCP", flush=True)
    server_py = work / "host_mcp_server.py"
    server_py.write_text(HOST_MCP_SERVER)
    server_py.chmod(0o755)

    reg = run_cli(binary, ["mcp", "create", "host", "--command", sys.executable,
                           "--args", str(server_py)], env, project, may_fail=True)
    mcp_ok = reg.returncode == 0
    if not mcp_ok:
        print(f"  mcp create failed: {reg.stderr.strip()[:400]}")

    wall("W6", "Host tools require a config mutation, not a per-run flag: "
               "`mcp create` writes the (sandboxed) install's config, so a "
               "host wiring tools for ONE embedded child must mutate state "
               "shared with every other run, then clean it up",
         "no `--mcp <command>` per-invocation wiring exists on chat/complete")

    if mcp_ok:
        script2 = {"turns": [
            {"tool_calls": [{"name": "mcp__host__host_lookup",
                             "arguments": {"key": "weather"}}]},
            {"text": "The host says: {{last_tool_result}}"},
        ]}
        (work / "actor2.json").write_text(json.dumps(script2))
        run_cli(binary, ["config", "add-backend", "actor2", "--type", "mock",
                         "--model-path", str(work / "actor2.json")], env, project)
        run_cli(binary, ["config", "set-default", "actor2"], env, project)

        child2 = Child(binary, env, project, "mcp-chat")
        results2, questions2 = drive_conversation(child2, ["What does the host know?"])
        status2 = child2.close()
        answer = results2[0].get("text", "") if results2 else ""
        round_trip = "host-answer:weather=42-and-sunny" in answer
        print(f"  host tool round trip: {'OK' if round_trip else 'FAILED'} "
              f"(questions asked: {len(questions2)}, exit {status2})")
        if questions2:
            print("  NOTE: a read-only host tool still prompted "
                  f"({[q.get('kind') for q in questions2]})")
    else:
        round_trip = False

    # ================= Phase 3: the read surface =============================
    print("phase 3: the read surface a host UI gets", flush=True)
    models_out = run_cli(binary, ["models"], env, project, may_fail=True)
    first = (models_out.stdout or models_out.stderr).strip().splitlines()
    is_json = False
    try:
        json.loads(first[0]) if first else None
        is_json = True
    except (json.JSONDecodeError, IndexError):
        pass
    wall("W7", "Reads are prose: 'everything else is a CLI command', but the "
               "read commands emit human text, so a host UI listing models or "
               "servers screen-scrapes or re-reads config files itself",
         f"`apogee models` first line: {first[0][:100] if first else '(empty)'!r} "
         f"(json={is_json}); no --output-format on read commands")

    # ---- write the evidence -------------------------------------------------
    (findings / "transcript.txt").write_text("\n".join(TRANSCRIPT) + "\n")
    walls_md = ["# Walls the naive host hit\n"]
    for wid, title, evidence in WALLS:
        walls_md.append(f"## {wid}: {title}\n\n{evidence}\n")
    (findings / "walls.md").write_text("\n".join(walls_md))
    print(f"\nevidence: {findings}/transcript.txt, {findings}/walls.md")
    print(f"conversation={'OK' if ok else 'FAILED'} "
          f"host_tool={'OK' if round_trip else 'FAILED'} walls={len(WALLS)}")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
