#!/usr/bin/env python3
"""A logging ACP agent, for capturing what Xcode sends (56a).

Register it in Xcode 27.1 -- Settings > Intelligence > Agents > Add an Agent
-- by its absolute path (Xcode does no `~` or PATH lookup), with the
arguments `--out <absolute path of the capture>`; then open a project, start
a conversation with it and send one prompt. It answers just enough for Xcode
to go on -- `initialize`, `session/new` (with a model and a mode option, to
see whether Xcode draws them), and one `session/prompt` -- and writes every
line Xcode sent, one JSON object a line, to the capture: the fixture
`cli.acp_conformance` replays as `tests/fixtures/acp/xcode-27.1.jsonl`.

Redacted as it is written: every `env` and `headers` value an MCP server
entry carries (an HTTP server's headers can hold a token, though this stub
takes none);
the session's folder, as `{{cwd}}`; this stub's session id, as
`{{sessionId}}`; and any other path under the home folder, as `<home>`. A
sidecar, `<capture>.environment.txt`, records what the agent process was
started with -- its arguments, its folder and its PATH (W4's and the
vendor CLIs' question), the home folder redacted the same way.

This changes Xcode's settings, so it is the person's to run, never a test's.
Remove the agent from Xcode's list afterwards.

Usage: acp_capture_stub.py --out <capture.jsonl>
"""

import json
import os
import sys

HOME = os.path.expanduser("~")
SESSION = "capture-session-1"


def redact(value, cwd):
    if isinstance(value, dict):
        out = {}
        for key, item in value.items():
            if key in ("env", "headers") and isinstance(item, list):
                out[key] = [dict(entry, value="<redacted>") if isinstance(entry, dict) else
                            "<redacted>" for entry in item]
            else:
                out[key] = redact(item, cwd)
        return out
    if isinstance(value, list):
        return [redact(item, cwd) for item in value]
    if isinstance(value, str):
        if cwd:
            value = value.replace(cwd, "{{cwd}}")
        value = value.replace(SESSION, "{{sessionId}}")
        return value.replace(HOME, "<home>")
    return value


def main():
    if len(sys.argv) != 3 or sys.argv[1] != "--out":
        print(__doc__, file=sys.stderr)
        return 2
    out_path = sys.argv[2]
    with open(out_path + ".environment.txt", "w", encoding="utf-8") as sidecar:
        for line in (f"argv: {sys.argv!r}", f"cwd: {os.getcwd()}",
                     f"PATH: {os.environ.get('PATH', '')}"):
            sidecar.write(line.replace(HOME, "<home>") + "\n")

    cwd = ""
    with open(out_path, "w", encoding="utf-8") as capture:
        for line in sys.stdin:
            line = line.strip()
            if not line:
                continue
            try:
                frame = json.loads(line)
            except ValueError:
                capture.write(json.dumps({"unparsable": line.replace(HOME, "<home>")}) + "\n")
                capture.flush()
                continue
            if frame.get("method") == "session/new":
                cwd = (frame.get("params") or {}).get("cwd", "") or ""
            capture.write(json.dumps(redact(frame, cwd)) + "\n")
            capture.flush()
            answer(frame, out_path)
    return 0


def send(frame):
    sys.stdout.write(json.dumps(frame) + "\n")
    sys.stdout.flush()


def answer(frame, out_path):
    if "id" not in frame or "method" not in frame:
        return
    method = frame["method"]
    if method == "initialize":
        result = {"protocolVersion": 1,
                  "agentCapabilities": {"loadSession": False,
                                        "promptCapabilities": {"image": False, "audio": False,
                                                               "embeddedContext": False},
                                        "mcpCapabilities": {"http": False, "sse": False}},
                  "agentInfo": {"name": "apogee-capture-stub", "version": "0"},
                  "authMethods": []}
    elif method == "session/new":
        result = {"sessionId": SESSION,
                  "modes": {"currentModeId": "manual",
                            "availableModes": [{"id": "manual", "name": "Manual"},
                                               {"id": "plan", "name": "Plan"}]},
                  "configOptions": [
                      {"id": "model", "name": "Model", "category": "model", "type": "select",
                       "currentValue": "first",
                       "options": [{"value": "first", "name": "First model"},
                                   {"value": "second", "name": "Second model"}]},
                      {"id": "mode", "name": "Permission mode", "category": "mode",
                       "type": "select", "currentValue": "manual",
                       "options": [{"value": "manual", "name": "Manual"},
                                   {"value": "plan", "name": "Plan"}]}]}
    elif method == "session/prompt":
        send({"jsonrpc": "2.0", "method": "session/update",
              "params": {"sessionId": SESSION,
                         "update": {"sessionUpdate": "agent_message_chunk",
                                    "content": {"type": "text",
                                                "text": "Recorded. The capture is in " +
                                                        out_path + "."}}}})
        result = {"stopReason": "end_turn"}
    elif method == "session/set_config_option":
        result = {"configOptions": []}
    elif method == "session/set_mode":
        result = {}
    else:
        send({"jsonrpc": "2.0", "id": frame["id"],
              "error": {"code": -32601, "message": "the capture stub answers no " + method}})
        return
    send({"jsonrpc": "2.0", "id": frame["id"], "result": result})


if __name__ == "__main__":
    sys.exit(main())
