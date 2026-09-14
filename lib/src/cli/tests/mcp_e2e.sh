#!/bin/sh
# The MCP client end to end, on the real binary: a scaffolded server created,
# listed, tested through the production client, connected by a tool-using
# run, disabled and deleted -- and the in-binary server driven by the client
# as one more server, so the fixture is Apogee itself with nothing installed.
#
# POSIX only, like the other shell checks. Needs python3 for the scaffolded
# server (the same interpreter the PTY checks already need).
set -eu

APOGEE_BIN="${1:?usage: mcp_e2e.sh <apogee-binary> <work-dir>}"
WORK_DIR="${2:?usage: mcp_e2e.sh <apogee-binary> <work-dir>}"

if ! command -v python3 >/dev/null 2>&1; then
    echo "mcp_e2e: python3 not found; skipping"
    exit 0
fi

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR"
export APOGEE_HOME="$WORK_DIR"
unset ANTHROPIC_API_KEY OPENAI_API_KEY GEMINI_API_KEY GOOGLE_API_KEY
trap 'rm -rf "$WORK_DIR"' EXIT

fail() { echo "mcp_e2e: $*" >&2; exit 1; }

"$APOGEE_BIN" config init >/dev/null || fail "config init"
"$APOGEE_BIN" config add-backend mock --type mock --model mock-1 >/dev/null || fail "add-backend"
"$APOGEE_BIN" config set-default mock >/dev/null || fail "set-default"

# --- create: a scaffolded server, runnable, registered ----------------------
"$APOGEE_BIN" mcp create greeter </dev/null >"$WORK_DIR/create.txt" 2>&1 || fail "mcp create: $(cat "$WORK_DIR/create.txt")"
[ -x "$WORK_DIR/mcp/greeter/server.py" ] || fail "server.py was not written executable"
[ -f "$WORK_DIR/mcp/greeter/README.md" ] || fail "README.md was not written"
[ "$("$APOGEE_BIN" config get mcp_servers.greeter.enabled </dev/null)" = "true" ] || fail "the entry is not enabled"
# The scaffold's own smoke test passes.
( cd "$WORK_DIR/mcp/greeter" && python3 test_server.py >"$WORK_DIR/pytest.txt" 2>&1 ) || fail "the scaffold's test_server.py failed: $(cat "$WORK_DIR/pytest.txt")"

# --- test: through the production client ------------------------------------
"$APOGEE_BIN" mcp test greeter echo '{"text":"hello-mcp"}' </dev/null >"$WORK_DIR/test.txt" 2>"$WORK_DIR/test.err" || fail "mcp test: $(cat "$WORK_DIR/test.err")"
grep -q "echo: hello-mcp" "$WORK_DIR/test.txt" || fail "mcp test did not echo: $(cat "$WORK_DIR/test.txt")"
grep -q "connecting: greeter" "$WORK_DIR/test.err" || fail "progress did not precede the dial"
if "$APOGEE_BIN" mcp test greeter echo 'not json' </dev/null >/dev/null 2>"$WORK_DIR/badjson.err"; then
    fail "invalid JSON arguments were accepted"
fi
grep -q "JSON" "$WORK_DIR/badjson.err" || fail "the JSON refusal did not say why"

# --- list: live state -------------------------------------------------------
"$APOGEE_BIN" mcp list </dev/null >"$WORK_DIR/list.txt" 2>&1 || fail "mcp list"
grep -q "greeter" "$WORK_DIR/list.txt" || fail "list did not name greeter"
grep -q "connected" "$WORK_DIR/list.txt" || fail "list did not show connected"
grep -q "2025-03-26" "$WORK_DIR/list.txt" || fail "list did not record the negotiated protocol"

# --- the in-binary server, as one more server -------------------------------
"$APOGEE_BIN" mcp create self --command "$APOGEE_BIN" --args __mcp-tools </dev/null >/dev/null || fail "register the in-binary server"
"$APOGEE_BIN" mcp test self list_notes </dev/null >"$WORK_DIR/self.txt" 2>/dev/null || fail "mcp test against __mcp-tools"
grep -q "No notes yet" "$WORK_DIR/self.txt" || fail "__mcp-tools did not answer list_notes: $(cat "$WORK_DIR/self.txt")"
# Only read-only tools are served: the writers are not reachable this way.
if "$APOGEE_BIN" mcp test self write_note '{"key":"k","content":"x"}' </dev/null >"$WORK_DIR/write.txt" 2>/dev/null; then
    fail "a writing tool was reachable over stdio: $(cat "$WORK_DIR/write.txt")"
fi

# --- a tool-using run connects at startup, and a dead server is skipped ------
"$APOGEE_BIN" mcp create dead --command /bin/sh --args -c 'echo "fatal: no upstream" >&2; exit 1' </dev/null >/dev/null || fail "register the dying server"
"$APOGEE_BIN" complete --tools "hi" </dev/null >"$WORK_DIR/run.txt" 2>"$WORK_DIR/run.err" || fail "complete --tools with servers configured: $(cat "$WORK_DIR/run.err")"
[ "$(cat "$WORK_DIR/run.txt")" = "mock response" ] || fail "the answer was disturbed: $(cat "$WORK_DIR/run.txt")"
grep -q "dead: connect failed" "$WORK_DIR/run.err" || fail "the dead server was not reported: $(cat "$WORK_DIR/run.err")"
grep -q "fatal: no upstream" "$WORK_DIR/run.err" || fail "the dead server's stderr tail was lost: $(cat "$WORK_DIR/run.err")"
# The raw stderr line itself never reached the terminal: only the folded note did.
[ "$(grep -c "fatal: no upstream" "$WORK_DIR/run.err")" = "1" ] || fail "the server's stderr reached the terminal raw"

# --- disable, doctor, delete ---------------------------------------------
"$APOGEE_BIN" mcp disable dead </dev/null >/dev/null || fail "mcp disable"
[ "$("$APOGEE_BIN" config get mcp_servers.dead.enabled </dev/null)" = "false" ] || fail "disable did not flip the key"
"$APOGEE_BIN" check </dev/null >"$WORK_DIR/check.txt" 2>&1 || true
grep -q "server: greeter" "$WORK_DIR/check.txt" || fail "the doctor has no MCP section"
"$APOGEE_BIN" mcp enable dead </dev/null >/dev/null || fail "mcp enable"
"$APOGEE_BIN" config delete-mcp-server dead </dev/null >/dev/null || fail "delete-mcp-server"
if "$APOGEE_BIN" config get mcp_servers.dead </dev/null >/dev/null 2>&1; then
    fail "the deleted server is still in the config"
fi

echo "apogee mcp end-to-end: OK"
