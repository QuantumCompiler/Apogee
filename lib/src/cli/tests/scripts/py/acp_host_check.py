#!/usr/bin/env python3
"""cli.acp_conformance -- `apogee acp` driven the way an ACP host drives it.

A scripted JSON-RPC client starts the real binary over pipes, as Xcode would
(Settings > Intelligence > Agents > Add an Agent), with the mock backend, a
temporary APOGEE_HOME and a temporary project folder holding an AGENTS.md,
and checks what the Agent Client Protocol promises and what 56a decided:

  - initialize: version 1 and the capabilities advertised; a host asking for
    version 2 is answered 1;
  - session/new: the project's notices -- the persona, the project context,
    the folder attached -- the modes and config options, the default model
    first; a relative cwd refused with -32602;
  - a prompt: thought chunks, message chunks, a tool_call with its kind and
    its updates, a plan from todo_write, then end_turn;
  - cancel mid-turn: cancelled, no error, and the session takes the next one;
  - permission, every answer: allow_once runs the write, reject_once leaves
    the file byte-identical, a cancelled outcome denies;
  - a turn cancelled while its sheet is open: cancelled, the file untouched,
    the call's card ended failed -- never left pending -- and the session
    takes the next turn;
  - repeated call ids (Gemini's API uses the tool's name as the id): each call
    its own card, each sheet naming its own file;
  - a mode chosen while a sheet is open: taken from the next turn, never lost;
  - switching: the model config option across two mock backends, the next
    turn the second's; a subscription-type backend listed (a fake claude on
    PATH); plan mode refusing a write in plan's words;
  - a backend that did not start (a claude-cli whose binary is nowhere, as
    under Xcode's launchd PATH) chosen from the menu: refused in M26's words,
    the menu set back, and the next turn the previous backend's;
  - a gated call inside a `delegate` sub-session: drawn as its own card, its
    sheet naming that card, the card ending failed or completed -- never left
    pending;
  - bad input: an unknown method is -32601, a frame with unknown fields is
    still answered, a non-JSON line is -32700, and the process lives;
  - stdout: every line one JSON-RPC message, a noisy child's stderr nowhere
    on it;
  - a dying child: the prompt's error carries its last stderr line, and acp
    keeps answering; a second session/new ends the first; closing acp's stdin
    ends every child and exits 0;
  - a host that shows notices gets them as ACP's notice update;
  - the host's MCP servers (56b): a stdio server joins its session alone --
    its tool answers there and is unknown to the next session -- the config
    byte-identical and `mcp list` never naming it; an http server set aside
    and named in the one notice, a dying server's last stderr line in it
    once; an env value reaching the server and nowhere else (no stderr, no
    stdout, no argv, no file of the home, no handover left behind); a config
    entry of the same name set aside for the session, never spawned; and the
    server's child gone once its session ends, by a newer one or stdin's end;
  - the protocol's own examples (tests/fixtures/acp/schema_examples.jsonl)
    replayed, standing in for a captured Xcode transcript -- and that
    transcript replayed too, once it is captured (xcode-27.1.jsonl).

POSIX only: a session is a child process, which the Windows build cannot
start yet (a recorded skip, as for the vendor CLIs).

Usage: acp_host_check.py <apogee-binary> <work-dir> <fixtures-dir>
"""

import hashlib
import json
import os
import queue
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time

TIMEOUT = 60


class Host:
    """One `apogee acp`, and the host's side of its stdio."""

    def __init__(self, apogee, env, args=(), cwd=None, notices=False, form=False):
        self.process = subprocess.Popen(
            [apogee, "acp", *args], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, env=env, cwd=cwd, text=True, bufsize=1)
        self.frames = queue.Queue()
        self.stdout_lines = []
        self.bad_lines = []
        self.stderr = []
        self.others = []
        self.next_id = 100
        self.notices = notices
        self.form = form
        threading.Thread(target=self._read_stdout, daemon=True).start()
        threading.Thread(target=self._read_stderr, daemon=True).start()

    def _read_stdout(self):
        for line in self.process.stdout:
            self.stdout_lines.append(line)
            try:
                frame = json.loads(line)
                if not isinstance(frame, dict) or frame.get("jsonrpc") != "2.0":
                    raise ValueError("not a JSON-RPC 2.0 object")
            except ValueError:
                self.bad_lines.append(line)
                continue
            self.frames.put(frame)
        self.frames.put(None)

    def _read_stderr(self):
        for line in self.process.stderr:
            self.stderr.append(line)

    def send(self, frame):
        self.process.stdin.write(json.dumps(frame) + "\n")
        self.process.stdin.flush()

    def raw(self, text):
        self.process.stdin.write(text + "\n")
        self.process.stdin.flush()

    def next_frame(self, timeout=TIMEOUT):
        frame = self.frames.get(timeout=timeout)
        if frame is None:
            raise EOFError("apogee acp closed stdout")
        return frame

    def call(self, method, params, answer=None, on_update=None):
        """Sends a request; returns (response, updates) once it is answered.

        `answer(frame)` answers each request apogee sends meanwhile -- the
        permission sheet, a form -- with the result dict, or None to send an
        error. `on_update(update)` sees each session update as it arrives.
        """
        self.next_id += 1
        mine = self.next_id
        self.send({"jsonrpc": "2.0", "id": mine, "method": method, "params": params})
        updates = []
        while True:
            frame = self.next_frame()
            if "method" in frame and "id" in frame:
                result = answer(frame) if answer else None
                if result is None:
                    self.send({"jsonrpc": "2.0", "id": frame["id"],
                               "error": {"code": -32603, "message": "no answer"}})
                else:
                    self.send({"jsonrpc": "2.0", "id": frame["id"], "result": result})
                continue
            if frame.get("method") == "session/update":
                updates.append(frame["params"]["update"])
                if on_update:
                    on_update(frame["params"]["update"])
                continue
            if frame.get("id") == mine:
                return frame, updates
            self.others.append(frame)

    def children(self):
        listed = subprocess.run(["pgrep", "-P", str(self.process.pid)], capture_output=True,
                                text=True)
        return [int(pid) for pid in listed.stdout.split()]

    def close(self, timeout=TIMEOUT):
        self.process.stdin.close()
        code = self.process.wait(timeout=timeout)
        time.sleep(0.2)
        return code

    def kill(self):
        if self.process.poll() is None:
            self.process.kill()
            self.process.wait()


def initialize(host, version=1, extra=None):
    capabilities = {}
    if host.notices:
        capabilities["session"] = {"notices": {}}
    if host.form:
        capabilities["elicitation"] = {"form": {}}
    params = {"protocolVersion": version, "clientCapabilities": capabilities,
              "clientInfo": {"name": "acp-host-check", "version": "1"}}
    params.update(extra or {})
    response, _ = host.call("initialize", params)
    return response


def texts(updates, kind):
    return "".join(u["content"]["text"] for u in updates
                   if u.get("sessionUpdate") == kind and u["content"].get("type") == "text")


def alive(pid):
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    # A child already exited but not yet reaped reads as alive: ask ps.
    state = subprocess.run(["ps", "-o", "stat=", "-p", str(pid)], capture_output=True, text=True)
    return state.stdout.strip() not in ("", "Z")


def main():
    if len(sys.argv) != 4:
        print(__doc__)
        return 2
    apogee, work, fixtures = sys.argv[1], sys.argv[2], sys.argv[3]
    os.makedirs(os.path.dirname(work), exist_ok=True)
    work = tempfile.mkdtemp(prefix=os.path.basename(work) + ".", dir=os.path.dirname(work))
    try:
        return run(apogee, work, fixtures)
    finally:
        shutil.rmtree(work, ignore_errors=True)


def write_script(path, turns):
    with open(path, "w", encoding="utf-8") as handle:
        json.dump({"turns": turns}, handle)


def run(apogee, work, fixtures):
    home = os.path.join(work, "home")
    project = os.path.realpath(os.path.join(work, "project"))
    fakebin = os.path.join(work, "bin")
    for folder in (home, project, fakebin):
        os.makedirs(folder)
    # A repository of its own: the session attaches the folder as a project,
    # and this one sits inside the build tree, whose own repository ignores it.
    subprocess.run(["git", "init", "-q", project], check=True, stdin=subprocess.DEVNULL)
    with open(os.path.join(project, "AGENTS.md"), "w", encoding="utf-8") as handle:
        handle.write("# Rules\nAnswer in one line.\n")
    with open(os.path.join(project, "main.py"), "w", encoding="utf-8") as handle:
        handle.write("def process_data(items):\n    for item in items:\n        print(item)\n")
    # A subscription CLI that is there, so its backend is a real choice.
    claude = os.path.join(fakebin, "claude")
    with open(claude, "w", encoding="utf-8") as handle:
        handle.write("#!/bin/sh\necho 'claude 0.0.0 (fake)'\n")
    os.chmod(claude, 0o755)
    env = dict(os.environ, APOGEE_HOME=home, PATH=fakebin + os.pathsep + os.environ["PATH"])

    write_script(os.path.join(work, "main.json"), [
        {"reasoning": "weighing the rules",
         "tool_calls": [
             {"id": "todo-1", "name": "todo_write",
              "arguments": {"items": [{"text": "Read the rules", "state": "active"},
                                      {"text": "Answer", "state": "pending"}]}},
             {"id": "read-1", "name": "read_file", "arguments": {"path": "AGENTS.md"}}]},
        {"text": "read: {{last_tool_result}}"},
        {"text": "main again"},
    ])
    write_script(os.path.join(work, "second.json"), [{"text": "second answers"}])
    # The utility role's own: a follow-up's search query is rewritten by it,
    # so the scripts above answer only their sessions' turns.
    write_script(os.path.join(work, "helper.json"), [{"text": "the search query"}])
    write_script(os.path.join(work, "slow.json"), [
        {"text": "slowly " * 60, "delay_ms": 100},
        {"text": "after the cancel"},
    ])
    write = {"name": "write_file", "arguments": {"path": "out.txt", "content": "new\n"}}
    write_script(os.path.join(work, "writer.json"), [
        {"tool_calls": [dict(write, id="w-1")]}, {"text": "1: {{last_tool_result}}"},
        {"tool_calls": [dict(write, id="w-2")]}, {"text": "2: {{last_tool_result}}"},
        {"tool_calls": [dict(write, id="w-3")]}, {"text": "3: {{last_tool_result}}"},
        {"tool_calls": [dict(write, id="w-4")]}, {"text": "4: {{last_tool_result}}"},
    ])
    # Gemini's API wire: a call's id is its tool's name, so every write in a
    # session is `write_file`.
    def gemini_write(path):
        return {"id": "write_file", "name": "write_file",
                "arguments": {"path": path, "content": path + "\n"}}
    write_script(os.path.join(work, "repeat.json"), [
        {"tool_calls": [gemini_write("a.txt"), gemini_write("b.txt")]},
        {"text": "1: {{last_tool_result}}"},
        {"tool_calls": [gemini_write("c.txt")]},
        {"text": "2: {{last_tool_result}}"},
    ])
    write_script(os.path.join(work, "midsheet.json"), [
        {"tool_calls": [{"id": "m-1", "name": "write_file",
                         "arguments": {"path": "first.txt", "content": "1\n"}}]},
        {"text": "1: {{last_tool_result}}"},
        {"tool_calls": [{"id": "m-2", "name": "write_file",
                         "arguments": {"path": "second.txt", "content": "2\n"}}]},
        {"text": "2: {{last_tool_result}}"},
    ])
    # A delegation: the parent's call, then the sub-session's gated write
    # under its own id, the sub-session's answer, the parent's -- twice, the
    # first sheet denied, the second allowed.
    def delegation(n):
        return [
            {"tool_calls": [{"id": f"d-{n}", "name": "delegate",
                             "arguments": {"brief": "write sub.txt"}}]},
            {"tool_calls": [{"id": f"s-{n}", "name": "write_file",
                             "arguments": {"path": "sub.txt", "content": f"sub {n}\n"}}]},
            {"text": f"sub {n}: {{{{last_tool_result}}}}"},
            {"text": f"delegated {n}: {{{{last_tool_result}}}}"},
        ]
    write_script(os.path.join(work, "delegator.json"), delegation(1) + delegation(2))
    for args in (["config", "init"],
                 ["config", "add-backend", "main", "--type", "mock", "--model", "mock-main",
                  "--model-path", os.path.join(work, "main.json")],
                 ["config", "add-backend", "repeat", "--type", "mock", "--model", "mock-r",
                  "--model-path", os.path.join(work, "repeat.json")],
                 ["config", "add-backend", "midsheet", "--type", "mock", "--model", "mock-ms",
                  "--model-path", os.path.join(work, "midsheet.json")],
                 ["config", "add-backend", "second", "--type", "mock", "--model", "mock-2",
                  "--model-path", os.path.join(work, "second.json")],
                 ["config", "add-backend", "slow", "--type", "mock", "--model", "mock-slow",
                  "--model-path", os.path.join(work, "slow.json")],
                 ["config", "add-backend", "writer", "--type", "mock", "--model", "mock-w",
                  "--model-path", os.path.join(work, "writer.json")],
                 ["config", "add-backend", "helper", "--type", "mock", "--model", "mock-h",
                  "--model-path", os.path.join(work, "helper.json")],
                 ["config", "add-backend", "sub", "--type", "claude-cli"],
                 ["config", "add-backend", "delegator", "--type", "mock", "--model", "mock-d",
                  "--model-path", os.path.join(work, "delegator.json")],
                 # A subscription CLI that is nowhere: configured, never
                 # started -- a binary path that does not exist, so no host's
                 # own claude can answer for it.
                 ["config", "add-backend", "gone", "--type", "claude-cli"],
                 ["config", "set-backend", "gone", "--binary",
                  os.path.join(work, "nowhere", "claude")],
                 ["config", "set-default", "main"],
                 ["config", "set-default-utility", "helper"]):
        subprocess.run([apogee, *args], env=env, check=True, stdout=subprocess.DEVNULL,
                       stdin=subprocess.DEVNULL)

    failures = []

    def check(condition, message):
        if not condition:
            failures.append(message)
        return condition

    hosts = []

    def start(*args, home_env=env, **kwargs):
        host = Host(apogee, home_env, *args, **kwargs)
        hosts.append(host)
        return host

    try:
        protocol_and_session(start, project, check)
        cancel(start, project, check)
        permissions(start, project, check)
        cancel_at_sheet(start, project, check)
        repeated_ids(start, project, check)
        mode_mid_sheet(start, project, check)
        unstarted_switch(start, project, check)
        delegated_calls(start, project, check)
        dying_child(start, project, check)
        notices(start, project, check)
        host_servers(apogee, work, env, start, project, check)
        replay(start, project, fixtures, check)
    except (EOFError, queue.Empty, KeyError, TypeError) as error:
        failures.append(f"the conversation broke off: {error!r}")
    finally:
        for host in hosts:
            host.kill()
            if host.bad_lines:
                failures.append(f"stdout carried {len(host.bad_lines)} line(s) that are not "
                                f"JSON-RPC: {host.bad_lines[:3]!r}")

    if failures:
        print("acp conformance: FAILED", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        for host in hosts[-1:]:
            print("last acp's stderr:\n" + "".join(host.stderr)[-3000:], file=sys.stderr)
        return 1
    print("acp conformance: initialize, sessions, prompts, cancel, permission, a cancel at the "
          "sheet, repeated call ids, a mode chosen mid-sheet, switching, a backend that did not "
          "start, a delegated call, bad input, a dying child, notices, the host's MCP servers and "
          "the protocol's examples -- OK")
    return 0


def protocol_and_session(start, project, check):
    host = start()
    # initialize: a host asking for version 2 is answered 1, and a field
    # this build does not know is ignored.
    response = initialize(host, version=2, extra={"aFieldFromLater": {"x": 1}})
    result = response.get("result", {})
    check(result.get("protocolVersion") == 1, f"initialize answered {response!r}")
    capabilities = result.get("agentCapabilities", {})
    check(capabilities.get("loadSession") is False, "loadSession is not false")
    check(capabilities.get("promptCapabilities") ==
          {"image": False, "audio": False, "embeddedContext": False},
          f"promptCapabilities: {capabilities.get('promptCapabilities')!r}")
    check(capabilities.get("mcpCapabilities") == {"http": False, "sse": False},
          f"mcpCapabilities: {capabilities.get('mcpCapabilities')!r}")
    check(result.get("authMethods") == [], "authMethods is not empty")
    check(result.get("agentInfo", {}).get("name") == "apogee", "agentInfo names no apogee")

    # Bad input, and the process lives.
    response, _ = host.call("no/such_method", {})
    check(response.get("error", {}).get("code") == -32601, f"unknown method: {response!r}")
    host.raw("this is not json")
    frame = host.next_frame()
    check(frame.get("error", {}).get("code") == -32700 and frame.get("id") is None,
          f"a non-JSON line: {frame!r}")
    host.raw("[1, 2]")
    frame = host.next_frame()
    check(frame.get("error", {}).get("code") == -32600, f"a batch: {frame!r}")
    # Params that are not what the method takes are answered, never fatal.
    for params in (None, [1], {"sessionId": 7, "prompt": "not blocks"}):
        response, _ = host.call("session/prompt", params)
        check(response.get("error", {}).get("code") == -32602,
              f"a prompt with params {params!r}: {response!r}")
    response, _ = host.call("session/new", {"cwd": "relative/folder", "mcpServers": []})
    check(response.get("error", {}).get("code") == -32602 and
          "relative/folder" in response.get("error", {}).get("message", ""),
          f"a relative cwd: {response!r}")

    response, _ = host.call("session/new", {"cwd": project, "mcpServers": []})
    opened = response.get("result", {})
    session = opened.get("sessionId")
    check(bool(session), f"session/new: {response!r}")
    modes = opened.get("modes", {})
    check(modes.get("currentModeId") == "manual" and
          [m["id"] for m in modes.get("availableModes", [])] == ["manual", "accept-edits", "plan"],
          f"modes: {modes!r}")
    options = {option["id"]: option for option in opened.get("configOptions", [])}
    model = options.get("model", {})
    values = [choice["value"] for choice in model.get("options", [])]
    check(model.get("category") == "model" and model.get("currentValue") == "main" and
          values[:1] == ["main"], f"the model option, default first: {model!r}")
    check({"second", "slow", "writer", "sub"} <= set(values),
          f"every chat backend, the subscription CLI among them: {values!r}")
    check(options.get("mode", {}).get("currentValue") == "manual", "the mode option")

    # A prompt: the opening's notices lead the thought stream, then the turn.
    response, updates = host.call("session/prompt", {
        "sessionId": session, "prompt": [{"type": "text", "text": "what are the rules?"}]})
    check(response.get("result", {}).get("stopReason") == "end_turn", f"prompt: {response!r}")
    thought = texts(updates, "agent_thought_chunk")
    for said in ("persona: coder", "project context: AGENTS.md", "attached ."):
        check(said in thought, f"the opening's '{said}' is not in the thought stream: "
                               f"{thought!r}")
    check("weighing the rules" in thought, "the reasoning is not a thought chunk")
    check("Answer in one line." in texts(updates, "agent_message_chunk"),
          "the answer is not message chunks")
    calls = {u["toolCallId"]: u for u in updates if u.get("sessionUpdate") == "tool_call"}
    check(calls.get("read-1", {}).get("kind") == "read" and
          calls["read-1"].get("locations") == [{"path": os.path.join(project, "AGENTS.md")}],
          f"read_file's tool_call: {calls.get('read-1')!r}")
    check(calls.get("todo-1", {}).get("kind") == "think", "todo_write's kind")
    statuses = [u["status"] for u in updates
                if u.get("sessionUpdate") == "tool_call_update" and u["toolCallId"] == "read-1"]
    check(statuses == ["in_progress", "completed"], f"read_file's updates: {statuses!r}")
    plans = [u for u in updates if u.get("sessionUpdate") == "plan"]
    check(bool(plans) and plans[-1]["entries"][0] ==
          {"content": "Read the rules", "priority": "medium", "status": "in_progress"},
          f"the plan: {plans!r}")

    # Switching models: the next turn is the second's.
    response, _ = host.call("session/set_config_option",
                            {"sessionId": session, "configId": "model", "value": "second"})
    current = {o["id"]: o["currentValue"] for o in response.get("result", {}).get(
        "configOptions", [])}
    check(current.get("model") == "second", f"set_config_option: {response!r}")
    response, updates = host.call("session/prompt", {
        "sessionId": session, "prompt": [{"type": "text", "text": "and you?"}]})
    check("second answers" in texts(updates, "agent_message_chunk"),
          "the turn after switching is not the second backend's")
    response, _ = host.call("session/set_config_option",
                            {"sessionId": session, "configId": "model", "value": "nobody"})
    check(response.get("error", {}).get("code") == -32602, f"an unknown model: {response!r}")
    response, _ = host.call("session/set_mode", {"sessionId": session, "modeId": "reckless"})
    check(response.get("error", {}).get("code") == -32602, f"an unknown mode: {response!r}")

    # A second session ends the first.
    first_children = host.children()
    check(len(first_children) == 1, f"one child for one session: {first_children!r}")
    response, _ = host.call("session/new", {"cwd": project, "mcpServers": []})
    second = response.get("result", {}).get("sessionId")
    check(bool(second) and second != session, f"a second session/new: {response!r}")
    response, _ = host.call("session/prompt", {
        "sessionId": session, "prompt": [{"type": "text", "text": "still there?"}]})
    check("replaced" in response.get("error", {}).get("message", ""),
          f"a prompt to a replaced session: {response!r}")
    deadline = time.time() + 20
    while time.time() < deadline and any(alive(pid) for pid in first_children):
        time.sleep(0.1)
    check(not any(alive(pid) for pid in first_children), "the replaced session's child lives on")

    children = host.children()
    code = host.close()
    check(code == 0, f"closing stdin exits {code}")
    check(not any(alive(pid) for pid in children), "a child outlived apogee acp")


def cancel(start, project, check):
    host = start(["-m", "slow"])
    initialize(host)
    response, _ = host.call("session/new", {"cwd": project, "mcpServers": []})
    session = response["result"]["sessionId"]
    check(response["result"]["configOptions"][0]["currentValue"] == "slow",
          "-m on the acp line is the session's model")
    cancelled = []

    def on_update(update):
        if update.get("sessionUpdate") == "agent_message_chunk" and not cancelled:
            cancelled.append(True)
            host.send({"jsonrpc": "2.0", "method": "session/cancel",
                       "params": {"sessionId": session}})

    response, _ = host.call("session/prompt", {
        "sessionId": session, "prompt": [{"type": "text", "text": "take your time"}]},
        on_update=on_update)
    check(cancelled and response.get("result", {}).get("stopReason") == "cancelled",
          f"a cancelled turn: {response!r}")
    response, _ = host.call("session/prompt", {
        "sessionId": session, "prompt": [{"type": "text", "text": "again"}]})
    check(response.get("result", {}).get("stopReason") == "end_turn",
          f"the turn after a cancel: {response!r}")
    check(host.close() == 0, "the cancel host's acp did not exit 0")


def permissions(start, project, check):
    target = os.path.join(project, "out.txt")
    host = start(["-m", "writer"])
    initialize(host)
    response, _ = host.call("session/new", {"cwd": project, "mcpServers": []})
    session = response["result"]["sessionId"]
    asked = []

    def answering(outcome):
        def answer(frame):
            if frame["method"] != "session/request_permission":
                return None
            asked.append(frame["params"])
            return {"outcome": outcome}
        return answer

    def prompt(outcome, text):
        with open(target, "w", encoding="utf-8") as handle:
            handle.write("original\n")
        return host.call("session/prompt", {
            "sessionId": session, "prompt": [{"type": "text", "text": text}]},
            answer=answering(outcome))

    response, updates = prompt({"outcome": "selected", "optionId": "yes"}, "write it")
    with open(target, encoding="utf-8") as handle:
        check(handle.read() == "new\n", "allow_once did not run the write")
    check(response.get("result", {}).get("stopReason") == "end_turn", f"allowed: {response!r}")
    if check(len(asked) == 1, f"one permission request: {len(asked)}"):
        sheet = asked[0]
        check(sheet["toolCall"]["toolCallId"] == "w-1" and sheet["toolCall"]["kind"] == "edit",
              f"the sheet names the call: {sheet['toolCall']!r}")
        check([o["kind"] for o in sheet["options"]] ==
              ["allow_once", "allow_always", "allow_always", "reject_once"],
              f"the sheet's options: {sheet['options']!r}")
        check("+new" in json.dumps(sheet["toolCall"].get("content", [])),
              "the sheet does not carry the diff")
    check(any(u.get("sessionUpdate") == "tool_call" and u["toolCallId"] == "w-1"
              for u in updates), "the write is not drawn as a tool_call")

    response, updates = prompt({"outcome": "selected", "optionId": "no"}, "write it again")
    with open(target, encoding="utf-8") as handle:
        check(handle.read() == "original\n", "reject_once changed the file")
    check(["failed"] == [u["status"] for u in updates
                         if u.get("sessionUpdate") == "tool_call_update"],
          f"a refused write is not drawn as failed: {updates!r}")
    response, _ = prompt({"outcome": "cancelled"}, "and again")
    with open(target, encoding="utf-8") as handle:
        check(handle.read() == "original\n", "a cancelled sheet changed the file")
    check(response.get("result", {}).get("stopReason") == "end_turn",
          f"after a cancelled sheet: {response!r}")

    # Plan mode, as the host's mode menu sets it: the write is refused in
    # plan's words, and nobody is asked.
    asked.clear()
    response, updates = host.call("session/set_mode", {"sessionId": session, "modeId": "plan"})
    check(response.get("result") == {}, f"set_mode: {response!r}")
    response, updates = prompt({"outcome": "selected", "optionId": "yes"}, "plan it")
    with open(target, encoding="utf-8") as handle:
        check(handle.read() == "original\n", "plan mode let the write run")
    check(not asked, "plan mode asked the host")
    check("plan mode is on" in texts(updates, "agent_message_chunk"),
          f"the refusal is not in plan's words: {texts(updates, 'agent_message_chunk')!r}")
    check(host.close() == 0, "the permission host's acp did not exit 0")


def cancel_at_sheet(start, project, check):
    """The host cancels the turn while its permission sheet is open, then
    answers the sheet `cancelled`, as ACP's cancellation asks of a client."""
    target = os.path.join(project, "out.txt")
    with open(target, "w", encoding="utf-8") as handle:
        handle.write("original\n")
    host = start(["-m", "writer"])
    initialize(host)
    response, _ = host.call("session/new", {"cwd": project, "mcpServers": []})
    session = response["result"]["sessionId"]
    asked = []

    def cancel_then_answer(frame):
        if frame["method"] != "session/request_permission":
            return None
        asked.append(frame["params"]["toolCall"]["toolCallId"])
        host.send({"jsonrpc": "2.0", "method": "session/cancel",
                   "params": {"sessionId": session}})
        time.sleep(0.3)
        return {"outcome": {"outcome": "cancelled"}}

    response, updates = host.call("session/prompt", {
        "sessionId": session, "prompt": [{"type": "text", "text": "write it"}]},
        answer=cancel_then_answer)
    check(response.get("result", {}).get("stopReason") == "cancelled",
          f"a turn cancelled at its sheet: {response!r}")
    check(asked == ["w-1"], f"the sheet cancelled: {asked!r}")
    with open(target, encoding="utf-8") as handle:
        check(handle.read() == "original\n", "a turn cancelled at its sheet changed the file")
    card = [u.get("status") for u in updates
            if u.get("sessionUpdate") in ("tool_call", "tool_call_update")
            and u.get("toolCallId") == "w-1"]
    check(card == ["pending", "failed"],
          f"the card of a call cancelled at its sheet did not end failed: {card!r}")
    response, _ = host.call("session/prompt", {
        "sessionId": session, "prompt": [{"type": "text", "text": "again"}]},
        answer=lambda frame: {"outcome": {"outcome": "selected", "optionId": "no"}})
    check("result" in response, f"the turn after a cancel at the sheet: {response!r}")
    check(host.close() == 0, "the cancel-at-sheet host's acp did not exit 0")


def repeated_ids(start, project, check):
    # Gemini's ids repeat: three writes, all `write_file`, two in one turn.
    # Each is its own card, and each sheet names its own file and card.
    for name in ("a.txt", "b.txt", "c.txt"):
        if os.path.exists(os.path.join(project, name)):
            os.remove(os.path.join(project, name))
    host = start(["-m", "repeat"])
    initialize(host)
    response, _ = host.call("session/new", {"cwd": project, "mcpServers": []})
    session = response["result"]["sessionId"]
    sheets = []

    def allow(frame):
        if frame["method"] != "session/request_permission":
            return None
        sheets.append(frame["params"]["toolCall"])
        return {"outcome": {"outcome": "selected", "optionId": "yes"}}

    updates = []
    for text in ("two writes", "one more"):
        response, said = host.call("session/prompt", {
            "sessionId": session, "prompt": [{"type": "text", "text": text}]}, answer=allow)
        check(response.get("result", {}).get("stopReason") == "end_turn",
              f"repeated ids, '{text}': {response!r}")
        updates += said
    cards = [u for u in updates if u.get("sessionUpdate") == "tool_call"]
    ids = [card["toolCallId"] for card in cards]
    check([card["title"] for card in cards] ==
          ["write_file a.txt", "write_file b.txt", "write_file c.txt"] and len(set(ids)) == 3,
          f"three calls under one id are not three cards: {cards!r}")
    check([(sheet["toolCallId"], sheet["title"], sheet.get("locations")) for sheet in sheets] ==
          [(card["toolCallId"], card["title"], [{"path": os.path.join(project, path)}])
           for card, path in zip(cards, ("a.txt", "b.txt", "c.txt"))],
          f"a sheet names another call's file or card: {sheets!r} against {cards!r}")
    for card in cards:
        statuses = [u["status"] for u in updates if u.get("sessionUpdate") == "tool_call_update"
                    and u["toolCallId"] == card["toolCallId"]]
        check(statuses == ["in_progress", "completed"],
              f"{card['title']}'s card moved {statuses!r}")
    for name in ("a.txt", "b.txt", "c.txt"):
        path = os.path.join(project, name)
        check(os.path.exists(path), f"{name} was not written")
        if os.path.exists(path):
            os.remove(path)
    check(host.close() == 0, "the repeated-ids host's acp did not exit 0")


def mode_mid_sheet(start, project, check):
    # The host's mode menu moved to plan while the first write's sheet is
    # open: the sheet's own answer still runs that write, the menu says plan
    # at once, and the next turn's write is refused in plan's words, nobody
    # asked -- the choice is never dropped by the question waiting.
    for name in ("first.txt", "second.txt"):
        if os.path.exists(os.path.join(project, name)):
            os.remove(os.path.join(project, name))
    host = start(["-m", "midsheet"])
    initialize(host)
    response, _ = host.call("session/new", {"cwd": project, "mcpServers": []})
    session = response["result"]["sessionId"]
    asked = []
    set_id = 9001

    def allow_and_switch(frame):
        if frame["method"] != "session/request_permission":
            return None
        asked.append(frame["params"]["toolCall"]["title"])
        if len(asked) == 1:
            host.send({"jsonrpc": "2.0", "id": set_id, "method": "session/set_mode",
                       "params": {"sessionId": session, "modeId": "plan"}})
        return {"outcome": {"outcome": "selected", "optionId": "yes"}}

    response, updates = host.call("session/prompt", {
        "sessionId": session, "prompt": [{"type": "text", "text": "write the first"}]},
        answer=allow_and_switch)
    check(response.get("result", {}).get("stopReason") == "end_turn",
          f"the turn a mode was chosen in: {response!r}")
    check(os.path.exists(os.path.join(project, "first.txt")),
          "the sheet's own answer did not run the first write")
    answered = [frame for frame in host.others if frame.get("id") == set_id]
    check(len(answered) == 1 and answered[0].get("result") == {},
          f"set_mode mid-sheet was not answered {{}}: {answered!r}")
    moved = [u for u in updates if u.get("sessionUpdate") == "config_option_update"]
    check(bool(moved) and {o["id"]: o["currentValue"] for o in
                           moved[-1]["configOptions"]}.get("mode") == "plan",
          f"the menu did not say plan at once: {moved!r}")

    asked.clear()
    response, updates = host.call("session/prompt", {
        "sessionId": session, "prompt": [{"type": "text", "text": "write the second"}]},
        answer=allow_and_switch)
    check(not asked, f"the turn after plan was chosen mid-sheet asked: {asked!r}")
    check(not os.path.exists(os.path.join(project, "second.txt")),
          "the turn after plan was chosen mid-sheet ran the write")
    check("plan mode is on" in texts(updates, "agent_message_chunk"),
          f"the mode chosen mid-sheet was lost: {texts(updates, 'agent_message_chunk')!r}")
    for name in ("first.txt", "second.txt"):
        if os.path.exists(os.path.join(project, name)):
            os.remove(os.path.join(project, name))
    check(host.close() == 0, "the mid-sheet host's acp did not exit 0")


def unstarted_switch(start, project, check):
    # A backend the menu lists that did not start -- its CLI missing, as
    # under Xcode's launchd PATH: the choice is refused in M26's words, the
    # menu goes back to what runs, and the next turn is that backend's --
    # never the default's under the chosen name.
    host = start(["-m", "second"])
    initialize(host)
    response, _ = host.call("session/new", {"cwd": project, "mcpServers": []})
    session = response["result"]["sessionId"]
    values = [choice["value"] for option in response["result"]["configOptions"]
              if option["id"] == "model" for choice in option["options"]]
    check("gone" in values, f"a backend that did not start is not on the menu: {values!r}")
    response, said = host.call("session/set_config_option",
                               {"sessionId": session, "configId": "model", "value": "gone"})
    check("result" in response, f"choosing a backend that did not start: {response!r}")
    response, updates = host.call("session/prompt", {
        "sessionId": session, "prompt": [{"type": "text", "text": "who answers?"}]})
    updates = said + updates
    check(response.get("result", {}).get("stopReason") == "end_turn",
          f"the turn after a refused switch: {response!r}")
    moved = [{o["id"]: o["currentValue"] for o in u["configOptions"]}.get("model")
             for u in updates if u.get("sessionUpdate") == "config_option_update"]
    check(bool(moved) and moved[-1] == "second",
          f"the menu was not set back to the backend that runs: {moved!r}")
    thought = texts(updates, "agent_thought_chunk")
    check("set model: backend 'gone' is configured but could not be started: " in thought,
          f"the refusal is not in M26's words: {thought!r}")
    answer = texts(updates, "agent_message_chunk")
    check("second answers" in answer,
          f"the turn after a refused switch is not the previous backend's: {answer!r}")
    check(host.close() == 0, "the unstarted-switch host's acp did not exit 0")


def delegated_calls(start, project, check):
    # A gated call a `delegate` sub-session makes asks the host's sheet
    # under its own id: that call is drawn as a card of its own, the sheet
    # names it, and it ends -- failed when denied, completed when allowed --
    # beside the delegation's own card.
    target = os.path.join(project, "sub.txt")
    if os.path.exists(target):
        os.remove(target)
    host = start(["-m", "delegator"])
    initialize(host)
    response, _ = host.call("session/new", {"cwd": project, "mcpServers": []})
    session = response["result"]["sessionId"]
    for n, option, ended in ((1, "no", "failed"), (2, "yes", "completed")):
        seen = []
        sheets = []

        def answer(frame, option=option, seen=seen, sheets=sheets):
            if frame["method"] != "session/request_permission":
                return None
            sheets.append((frame["params"]["toolCall"], len(seen)))
            return {"outcome": {"outcome": "selected", "optionId": option}}

        response, updates = host.call("session/prompt", {
            "sessionId": session, "prompt": [{"type": "text", "text": f"delegate {n}"}]},
            answer=answer, on_update=seen.append)
        check(response.get("result", {}).get("stopReason") == "end_turn",
              f"delegation {n}: {response!r}")
        if not check(len(sheets) == 1, f"delegation {n}: one sheet, not {sheets!r}"):
            continue
        sheet, at = sheets[0]
        card = sheet["toolCallId"]
        check(card == f"s-{n}" and sheet["title"] == "write_file sub.txt",
              f"delegation {n}: the sheet names {sheet!r}")
        drawn = [u for u in updates[:at] if u.get("sessionUpdate") == "tool_call"
                 and u["toolCallId"] == card]
        check(len(drawn) == 1 and drawn[0]["status"] == "pending",
              f"delegation {n}: the sheet's call was not drawn before it: {updates[:at]!r}")
        states = [u["status"] for u in updates
                  if u.get("sessionUpdate") in ("tool_call", "tool_call_update")
                  and u.get("toolCallId") == card]
        check(states[-1:] == [ended],
              f"delegation {n}: the delegated call's card moved {states!r}, not to {ended}")
        parent = [u["status"] for u in updates
                  if u.get("sessionUpdate") in ("tool_call", "tool_call_update")
                  and u.get("toolCallId") == f"d-{n}"]
        check(parent == ["pending", "in_progress", "completed"],
              f"delegation {n}: the delegation's own card moved {parent!r}")
        written = os.path.exists(target)
        check(written == (option == "yes"),
              f"delegation {n}: sub.txt {'was' if written else 'was not'} written")
    if os.path.exists(target):
        os.remove(target)
    check(host.close() == 0, "the delegation host's acp did not exit 0")


def dying_child(start, project, check):
    # --verbose: each child's raw stderr reaches acp's, never its stdout.
    host = start(["-m", "slow", "--verbose"])
    initialize(host)
    response, _ = host.call("session/new", {"cwd": project, "mcpServers": []})
    session = response["result"]["sessionId"]
    killed = []

    def on_update(update):
        if update.get("sessionUpdate") == "agent_message_chunk" and not killed:
            for pid in host.children():
                os.kill(pid, signal.SIGTERM)
                killed.append(pid)

    response, _ = host.call("session/prompt", {
        "sessionId": session, "prompt": [{"type": "text", "text": "keep going"}]},
        on_update=on_update)
    message = response.get("error", {}).get("message", "")
    time.sleep(0.5)  # the tee reaches acp's stderr pipe as the error is sent
    check(bool(killed) and message.startswith("apogee's session ended"),
          f"a dying child's prompt: {response!r}")
    told = [line.rstrip("\n") for line in host.stderr
            if line.strip() and not line.startswith("apogee acp: ")]
    check(bool(told) and message.endswith(told[-1]),
          f"the error does not carry the child's last stderr line {told[-1:]!r}: {message!r}")
    # A leaked line would be a line of its own: none of stdout's is one of
    # the child's (and every one of them parsed as JSON-RPC, checked at the
    # end).
    raw_out = {line.rstrip("\n") for line in host.stdout_lines}
    check(bool(told) and not raw_out & set(told), "the child's stderr reached stdout")
    response, _ = host.call("session/prompt", {
        "sessionId": session, "prompt": [{"type": "text", "text": "hello?"}]})
    check("ended" in response.get("error", {}).get("message", ""),
          f"a prompt to an ended session: {response!r}")
    response, _ = host.call("session/new", {"cwd": project, "mcpServers": []})
    check(bool(response.get("result", {}).get("sessionId")), "acp stopped answering")
    check(host.close() == 0, "acp did not exit 0 after its child died")


def notices(start, project, check):
    host = start(notices=True)
    initialize(host)
    response, _ = host.call("session/new", {"cwd": project, "mcpServers": []})
    session = response["result"]["sessionId"]
    response, updates = host.call("session/prompt", {
        "sessionId": session, "prompt": [{"type": "text", "text": "hi"}]})
    titles = [u["title"] for u in updates if u.get("sessionUpdate") == "notice"]
    # The opening's notices may land before the prompt: read what came.
    while True:
        try:
            frame = host.frames.get(timeout=0.5)
        except queue.Empty:
            break
        if frame and frame.get("method") == "session/update" and \
                frame["params"]["update"].get("sessionUpdate") == "notice":
            titles.append(frame["params"]["update"]["title"])
    check(any(t.startswith("persona: coder") for t in titles),
          f"a host that shows notices did not get the opening's: {titles!r}")
    check("persona: coder" not in texts(updates, "agent_thought_chunk"),
          "a notice reached a host that shows them as a thought too")
    check(host.close() == 0, "the notices host's acp did not exit 0")


def descendants(pid):
    """Every process under `pid`, with its command line."""
    listed = subprocess.run(["ps", "-axww", "-o", "pid=,ppid=,args="], capture_output=True,
                            text=True).stdout
    rows = []
    for line in listed.splitlines():
        parts = line.split(None, 2)
        if len(parts) >= 2:
            rows.append((int(parts[0]), int(parts[1]), parts[2] if len(parts) > 2 else ""))
    found, frontier = [], [pid]
    while frontier:
        parent = frontier.pop()
        for child, ppid, args in rows:
            if ppid == parent:
                found.append((child, args))
                frontier.append(child)
    return found


def gone_within(pid, seconds=10):
    deadline = time.time() + seconds
    while time.time() < deadline:
        if not alive(pid):
            return True
        time.sleep(0.1)
    return not alive(pid)


def mcp_home(apogee, work, name, env, entry=None):
    """A home of its own for the host-server phases: a mock that calls the
    `self` server's list_notes, then says what it got -- and, given `entry`,
    a config MCP server registered through the one editor."""
    home = os.path.join(work, name)
    os.makedirs(home)
    home_env = dict(env, APOGEE_HOME=home, TMPDIR=os.path.join(work, name + "-tmp"))
    os.makedirs(home_env["TMPDIR"])
    script = os.path.join(work, name + "-caller.json")
    write_script(script, [
        {"tool_calls": [{"id": "n-1", "name": "mcp__self__list_notes", "arguments": {}}]},
        {"text": "notes: {{last_tool_result}}"},
    ])
    helper = os.path.join(work, name + "-helper.json")
    write_script(helper, [{"text": "the search query"}])
    commands = [["config", "init"],
                ["config", "add-backend", "caller", "--type", "mock", "--model", "mock-c",
                 "--model-path", script],
                ["config", "add-backend", "helper", "--type", "mock", "--model", "mock-h",
                 "--model-path", helper],
                ["config", "set-default", "caller"],
                ["config", "set-default-utility", "helper"]]
    if entry:
        commands.append(["mcp", "create", *entry])
    for args in commands:
        subprocess.run([apogee, *args], env=home_env, check=True, stdout=subprocess.DEVNULL,
                       stdin=subprocess.DEVNULL)
    return home, home_env


def sha256_of(path):
    with open(path, "rb") as handle:
        return hashlib.sha256(handle.read()).hexdigest()


def everything_under(folder):
    """Every file's bytes under `folder`, by path."""
    found = {}
    for root, _, files in os.walk(folder):
        for name in files:
            path = os.path.join(root, name)
            try:
                with open(path, "rb") as handle:
                    found[path] = handle.read()
            except OSError:
                continue
    return found


def host_servers(apogee, work, env, start, project, check):
    """56b: the stdio MCP servers a host names in session/new."""
    home, home_env = mcp_home(apogee, work, "mcp-home", env)
    config = os.path.join(home, "config", "config.json")
    before = sha256_of(config)
    sentinel = "apogee-sentinel-" + os.urandom(8).hex()
    probe = os.path.join(work, "env-probe.txt")
    token = [{"name": "APOGEE_TEST_TOKEN", "value": sentinel}]
    servers = [
        {"name": "self", "command": apogee, "args": ["mcp", "serve"], "env": token},
        {"type": "http", "name": "remote", "url": "http://127.0.0.1:9/mcp",
         "headers": [{"name": "Authorization", "value": "Bearer " + sentinel}]},
        {"name": "envprobe", "command": "/bin/sh",
         "args": ["-c", 'printf %s "$APOGEE_TEST_TOKEN" > "$0"; exit 1', probe], "env": token},
        {"name": "dying", "command": "/bin/sh",
         "args": ["-c", 'echo "fatal: no upstream" >&2; exit 1']},
    ]
    mine = []
    host = start(home_env=home_env)
    mine.append(host)
    initialize(host)
    began = time.time()
    response, _ = host.call("session/new", {"cwd": project, "mcpServers": servers})
    took = time.time() - began
    first = response.get("result", {}).get("sessionId")
    check(bool(first), f"session/new with the host's servers: {response!r}")
    print(f"acp conformance: session/new with four host MCP servers took {took:.2f} s")
    tree = descendants(host.process.pid)
    served = [pid for pid, args in tree if args.endswith("mcp serve")]
    check(len(served) == 1, f"one `self` server for the session, not {tree!r}")
    check(not any(sentinel in args for _, args in tree),
          "an env value is on a process's argv")
    check(not any(name.startswith("apogee-acp-") for name in os.listdir(home_env["TMPDIR"])),
          "the handover was left behind once the session opened")
    try:
        with open(probe, encoding="utf-8") as handle:
            check(handle.read() == sentinel, "the env value did not reach the server as given")
    except OSError:
        check(False, "the env probe server never wrote its file")

    asked = []

    def answer(frame):
        asked.append(frame["method"])
        return {"outcome": {"outcome": "selected", "optionId": "no"}}

    response, updates = host.call("session/prompt", {
        "sessionId": first, "prompt": [{"type": "text", "text": "what notes are there?"}]},
        answer=answer)
    message = texts(updates, "agent_message_chunk")
    check(response.get("result", {}).get("stopReason") == "end_turn" and
          "No notes yet" in message, f"the host's server answered nothing: {message!r}")
    check(asked == [], f"a read-only host tool asked the gate: {asked!r}")
    cards = [u for u in updates if u.get("sessionUpdate") == "tool_call"
             and u.get("toolCallId") == "n-1"]
    check(len(cards) == 1 and cards[0].get("kind") == "other",
          f"the host tool's call was not drawn as its card: {cards!r}")
    thought = texts(updates, "agent_thought_chunk")
    check("Apogee connects stdio MCP servers only; skipped: remote" in thought,
          f"the http server is not named in the notice: {thought!r}")
    check(thought.count("fatal: no upstream") == 1,
          f"the dying server's last line, folded once: {thought!r}")
    check("envprobe" in thought and "dying" in thought,
          f"the failing servers are not named in the notice: {thought!r}")
    said = [line for line in host.stderr
            if line.startswith("apogee acp: session") and "[mcp] warning:" in line]
    check(len(said) == 1,
          f"the session's MCP warnings are not one notice on acp's stderr: {said!r}")

    # A second session, with no servers: the first's server ends with it, and
    # its tool is unknown here.
    response, _ = host.call("session/new", {"cwd": project, "mcpServers": []})
    second = response.get("result", {}).get("sessionId")
    check(bool(second), f"a second session/new: {response!r}")
    check(all(gone_within(pid) for pid in served),
          "the first session's MCP server outlived its session")
    response, updates = host.call("session/prompt", {
        "sessionId": second, "prompt": [{"type": "text", "text": "and here?"}]})
    message = texts(updates, "agent_message_chunk")
    check("no tool named 'mcp__self__list_notes'" in message and "No notes yet" not in message,
          f"the second session reached the first's server: {message!r}")
    check(host.close() == 0, "the host-servers acp did not exit 0")

    # A third: its server is gone once stdin ends.
    host = start(home_env=home_env)
    mine.append(host)
    initialize(host)
    response, _ = host.call("session/new", {"cwd": project, "mcpServers": servers[:1]})
    check(bool(response.get("result", {}).get("sessionId")), f"session/new: {response!r}")
    served = [pid for pid, args in descendants(host.process.pid) if args.endswith("mcp serve")]
    check(len(served) == 1, "no `self` server for the third session")
    check(host.close() == 0, "the third acp did not exit 0")
    check(all(gone_within(pid) for pid in served), "an MCP server outlived acp's stdin")

    # The config is the user's: untouched, and no other process sees `self`.
    check(sha256_of(config) == before, "the config file changed")
    listed = subprocess.run([apogee, "mcp", "list", "--output-format", "json"], env=home_env,
                            capture_output=True, text=True, stdin=subprocess.DEVNULL)
    names = [entry.get("name") for entry in json.loads(listed.stdout or "{}").get("data", [])]
    check("self" not in names, f"`mcp list` names the host's server: {names!r}")
    for path, data in everything_under(home).items():
        check(sentinel.encode() not in data, f"an env value was written to {path}")
    for line in [line for each in mine for line in (*each.stderr, *each.stdout_lines)]:
        check(sentinel not in line, "an env value reached acp's stderr or stdout")

    # A config entry of the host's name: set aside for the session, never
    # spawned; one connection, the host's, said once.
    marker = os.path.join(work, "config-self-ran")
    config_self = os.path.join(work, "config-self.sh")
    with open(config_self, "w", encoding="utf-8") as handle:
        handle.write(f'#!/bin/sh\ntouch "{marker}"\nexec "{apogee}" mcp serve\n')
    os.chmod(config_self, 0o755)
    _, collide_env = mcp_home(apogee, work, "mcp-collide", env,
                              entry=["self", "--command", config_self])
    host = start(home_env=collide_env)
    initialize(host)
    response, _ = host.call("session/new", {
        "cwd": project,
        "mcpServers": [{"name": "self", "command": apogee, "args": ["mcp", "serve"]}]})
    session = response.get("result", {}).get("sessionId")
    check(bool(session), f"a session whose host server collides: {response!r}")
    response, updates = host.call("session/prompt", {
        "sessionId": session, "prompt": [{"type": "text", "text": "notes?"}]})
    check("No notes yet" in texts(updates, "agent_message_chunk"),
          f"the host's `self` did not answer: {response!r}")
    check(not os.path.exists(marker), "the config's `self` was spawned beside the host's")
    check("the config's server is set aside for this session" in
          texts(updates, "agent_thought_chunk"), "the displacement is not said")
    check(len([pid for pid, args in descendants(host.process.pid)
               if args.endswith("mcp serve")]) == 1, "not one `self` connection")
    check(host.close() == 0, "the colliding acp did not exit 0")


def replay(start, project, fixtures, check):
    for name in ("schema_examples.jsonl", "xcode-27.1.jsonl"):
        path = os.path.join(fixtures, name)
        if not os.path.exists(path):
            print(f"acp conformance: {name} not captured yet -- the protocol's examples stand "
                  f"in for it")
            continue
        host = start()
        session = None
        with open(path, encoding="utf-8") as handle:
            frames = [line for line in handle.read().splitlines() if line.strip()]
        for text in frames:
            text = text.replace("{{cwd}}", project).replace("{{sessionId}}", session or "")
            frame = json.loads(text)
            if "id" not in frame:
                host.send(frame)
                continue
            response, _ = host.call(frame["method"], frame.get("params", {}),
                                    answer=lambda f: {"outcome": {"outcome": "cancelled"}})
            check("result" in response, f"{name}: {frame['method']} answered {response!r}")
            if frame["method"] == "session/new":
                session = response.get("result", {}).get("sessionId")
        check(host.close() == 0, f"{name}: acp did not exit 0")
        if name == "schema_examples.jsonl":
            # Its stdio server is a placeholder path: handed over, dialled,
            # skipped with a warning -- and the session answered all the same.
            check(any("the host's MCP servers: filesystem (/path/to/mcp-server)" in line
                      for line in host.stderr),
                  "the host's MCP server is not named on stderr")
            check(any("[mcp] warning: filesystem" in line for line in host.stderr),
                  "the placeholder MCP server's skip is not said")


if __name__ == "__main__":
    sys.exit(main())
