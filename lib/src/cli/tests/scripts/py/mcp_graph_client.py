#!/usr/bin/env python3
"""An external MCP client for the graph toolset (27l), over stdio.

Spawns `apogee __mcp-tools` -- the in-binary server, which serves exactly the
tools that do not write -- speaks JSON-RPC to it line by line as any MCP host
would, and checks that the four graph tools are listed, marked read-only, and
answer each call with the document `apogee graph <verb> --output-format json`
printed for the same question, byte for byte.

usage: mcp_graph_client.py <apogee-binary> <calls.json>

`calls.json` is a list of {"tool": ..., "arguments": {...}, "expect": <path
of the CLI's JSON output>}. Exit 0 on success; a failure names what differed.
The server's lifetime is bounded by its stdin: every request is written, the
pipe closed, and the server exits at EOF.
"""

import json
import subprocess
import sys

GRAPH_TOOLS = {"graph_query", "graph_path", "graph_explain", "graph_neighbors"}


def fail(message):
    print("mcp_graph_client: " + message, file=sys.stderr)
    sys.exit(1)


def main():
    if len(sys.argv) != 3:
        fail("usage: mcp_graph_client.py <apogee-binary> <calls.json>")
    binary, calls_path = sys.argv[1], sys.argv[2]
    with open(calls_path, encoding="utf-8") as handle:
        calls = json.load(handle)

    requests = [
        {"jsonrpc": "2.0", "id": 1, "method": "initialize",
         "params": {"protocolVersion": "2025-03-26", "capabilities": {},
                    "clientInfo": {"name": "mcp-graph-client", "version": "1"}}},
        {"jsonrpc": "2.0", "method": "notifications/initialized"},
        {"jsonrpc": "2.0", "id": 2, "method": "tools/list"},
    ]
    for index, call in enumerate(calls):
        requests.append({"jsonrpc": "2.0", "id": 10 + index, "method": "tools/call",
                         "params": {"name": call["tool"], "arguments": call["arguments"]}})
    stdin = "".join(json.dumps(request) + "\n" for request in requests)
    completed = subprocess.run([binary, "__mcp-tools"], input=stdin, capture_output=True,
                               text=True, timeout=120, check=False)
    if completed.returncode != 0:
        fail("the server exited %d: %s" % (completed.returncode, completed.stderr))

    replies = {}
    for line in completed.stdout.splitlines():
        if not line.strip():
            continue
        try:
            frame = json.loads(line)
        except ValueError:
            fail("stdout carried a line that is not JSON-RPC: %r" % line)
        replies[frame.get("id")] = frame

    initialized = replies.get(1, {}).get("result", {})
    if "tools" not in initialized.get("capabilities", {}):
        fail("initialize did not offer tools: %r" % replies.get(1))

    listed = {tool["name"]: tool for tool in replies.get(2, {}).get("result", {}).get("tools", [])}
    missing = GRAPH_TOOLS - set(listed)
    if missing:
        fail("tools/list lacks %s: %s" % (sorted(missing), sorted(listed)))
    for name in sorted(GRAPH_TOOLS):
        tool = listed[name]
        if tool.get("annotations", {}).get("readOnlyHint") is not True:
            fail("%s is not marked read-only: %r" % (name, tool.get("annotations")))
        if tool.get("inputSchema", {}).get("type") != "object":
            fail("%s has no object input schema" % name)
    # Nothing that writes is reachable this way.
    for writer in ("write_file", "write_note", "run_command"):
        if writer in listed:
            fail("a writing tool was listed: " + writer)

    for index, call in enumerate(calls):
        reply = replies.get(10 + index)
        if reply is None:
            fail("no reply to %s" % call["tool"])
        result = reply.get("result", {})
        if result.get("isError"):
            fail("%s answered an error: %r" % (call["tool"], result))
        text = result.get("content", [{}])[0].get("text", "")
        with open(call["expect"], encoding="utf-8") as handle:
            expected = handle.read()
        if expected.endswith("\n"):
            expected = expected[:-1]
        if text != expected:
            fail("%s differs from the CLI's JSON:\n  tool: %s\n  cli:  %s"
                 % (call["tool"], text, expected))
        print("%s: the CLI's document, byte for byte" % call["tool"])
    print("mcp_graph_client: OK")


if __name__ == "__main__":
    main()
