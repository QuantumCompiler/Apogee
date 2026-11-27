#!/usr/bin/env python3
"""An external MCP client for `apogee mcp serve` (54a), over stdio.

Spawns the binary's MCP server under each spelling it answers to -- the
public `mcp serve` and the hidden alias `__mcp-tools` -- speaks JSON-RPC to it
line by line as any MCP host would, and checks that:

- stdout carries JSON-RPC frames and nothing else;
- both spellings list the same tools, every one marked read-only, no writer
  among them, the five status reads last and in order;
- each status read answers with the document `apogee <verb>
  --output-format json` printed for the same question, byte for byte.

With `--callables=<name,...>` (54b), the calls the config lists follow the
status reads, each marked not read-only, and a call's expected text is what
the command it serves printed -- `apogee complete -m <backend> ...` for the
served `complete`.

usage: mcp_serve_client.py <apogee-binary> <calls.json> [--callables=<name,...>]

`calls.json` is a list of {"tool": ..., "arguments": {...}, "expect": <path
of the CLI's JSON output>}. Exit 0 on success; a failure names what differed.
The server's lifetime is bounded by its stdin: every request is written, the
pipe closed, and the server exits at EOF.
"""

import json
import subprocess
import sys

STATUS_READS = ["models_list", "models_status", "train_status", "train_versions", "check"]
WRITERS = ("write_file", "edit_file", "write_note", "run_command")
SPELLINGS = (["mcp", "serve"], ["__mcp-tools"])


def fail(message):
    print("mcp_serve_client: " + message, file=sys.stderr)
    sys.exit(1)


def converse(binary, spelling, calls):
    requests = [
        {"jsonrpc": "2.0", "id": 1, "method": "initialize",
         "params": {"protocolVersion": "2025-03-26", "capabilities": {},
                    "clientInfo": {"name": "mcp-serve-client", "version": "1"}}},
        {"jsonrpc": "2.0", "method": "notifications/initialized"},
        {"jsonrpc": "2.0", "id": 2, "method": "tools/list"},
    ]
    for index, call in enumerate(calls):
        requests.append({"jsonrpc": "2.0", "id": 10 + index, "method": "tools/call",
                         "params": {"name": call["tool"], "arguments": call["arguments"]}})
    stdin = "".join(json.dumps(request) + "\n" for request in requests)
    completed = subprocess.run([binary] + spelling, input=stdin, capture_output=True,
                               text=True, timeout=120, check=False)
    words = " ".join(spelling)
    if completed.returncode != 0:
        fail("%s exited %d: %s" % (words, completed.returncode, completed.stderr))
    replies = {}
    for line in completed.stdout.splitlines():
        try:
            frame = json.loads(line)
        except ValueError:
            fail("%s: stdout carried a line that is not JSON-RPC: %r" % (words, line))
        replies[frame.get("id")] = frame
    return replies


def main():
    if len(sys.argv) not in (3, 4):
        fail("usage: mcp_serve_client.py <apogee-binary> <calls.json> [--callables=<name,...>]")
    binary, calls_path = sys.argv[1], sys.argv[2]
    callables = []
    if len(sys.argv) == 4:
        if not sys.argv[3].startswith("--callables="):
            fail("unknown argument: " + sys.argv[3])
        callables = [name for name in sys.argv[3][len("--callables="):].split(",") if name]
    tail = STATUS_READS + callables
    with open(calls_path, encoding="utf-8") as handle:
        calls = json.load(handle)

    listings = []
    for spelling in SPELLINGS:
        words = " ".join(spelling)
        replies = converse(binary, spelling, calls)
        initialized = replies.get(1, {}).get("result", {})
        if "tools" not in initialized.get("capabilities", {}):
            fail("%s: initialize did not offer tools: %r" % (words, replies.get(1)))
        tools = replies.get(2, {}).get("result", {}).get("tools", [])
        names = [tool["name"] for tool in tools]
        if names[-len(tail):] != tail:
            fail("%s: the status reads and calls are not listed last, in order: %s"
                 % (words, names))
        for tool in tools:
            read_only = tool.get("annotations", {}).get("readOnlyHint")
            if tool["name"] in callables:
                if read_only is not False:
                    fail("%s: the call %s is not marked as one" % (words, tool["name"]))
            elif read_only is not True:
                fail("%s: %s is not marked read-only" % (words, tool["name"]))
            if tool.get("inputSchema", {}).get("type") != "object":
                fail("%s: %s has no object input schema" % (words, tool["name"]))
        for writer in WRITERS:
            if writer in names:
                fail("%s: a writing tool was listed: %s" % (words, writer))
        listings.append(tools)

        for index, call in enumerate(calls):
            reply = replies.get(10 + index)
            if reply is None:
                fail("%s: no reply to %s" % (words, call["tool"]))
            result = reply.get("result", {})
            if result.get("isError"):
                fail("%s: %s answered an error: %r" % (words, call["tool"], result))
            text = result.get("content", [{}])[0].get("text", "")
            with open(call["expect"], encoding="utf-8") as handle:
                expected = handle.read()
            if expected.endswith("\n"):
                expected = expected[:-1]
            if text != expected:
                fail("%s: %s differs from the CLI's JSON:\n  tool: %s\n  cli:  %s"
                     % (words, call["tool"], text, expected))
            print("%s: %s is the CLI's document, byte for byte" % (words, call["tool"]))

    if listings[0] != listings[1]:
        fail("mcp serve and __mcp-tools list different tools")
    print("mcp_serve_client: OK (%d tools under both spellings)" % len(listings[0]))


if __name__ == "__main__":
    main()
