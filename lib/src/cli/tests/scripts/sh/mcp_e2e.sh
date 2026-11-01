#!/bin/sh
# The MCP client end to end, on the real binary: a scaffolded server created,
# listed, tested through the production client, connected by a tool-using
# run, disabled and deleted -- and the in-binary server driven by the client
# as one more server, so the fixture is Apogee itself with nothing installed:
# `mcp serve` (54a) and its hidden alias `__mcp-tools`, the status reads held
# to the CLI's own documents byte for byte by an external client.
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

. "$(dirname "$0")/sandbox.sh"
WORK_DIR="$(sandbox_dir "$WORK_DIR")"
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

# --- mcp serve (54a): the public verb, its alias, the status reads ----------
# An external client over raw JSON-RPC: both spellings list the same tools,
# all read-only, the five status reads last; each read answers with the CLI's
# own --output-format json document, byte for byte.
"$APOGEE_BIN" models list --output-format json </dev/null >"$WORK_DIR/doc-models-list.json" 2>/dev/null || fail "models list --output-format json"
"$APOGEE_BIN" models list --all --output-format json </dev/null >"$WORK_DIR/doc-models-list-all.json" 2>/dev/null || fail "models list --all --output-format json"
"$APOGEE_BIN" models status --output-format json </dev/null >"$WORK_DIR/doc-models-status.json" 2>/dev/null || fail "models status --output-format json"
"$APOGEE_BIN" train status --output-format json </dev/null >"$WORK_DIR/doc-train-status.json" 2>/dev/null || fail "train status --output-format json"
"$APOGEE_BIN" train versions --output-format json </dev/null >"$WORK_DIR/doc-train-versions.json" 2>/dev/null || fail "train versions --output-format json"
# check exits 1 on a failure, its document printed all the same.
"$APOGEE_BIN" check --output-format json </dev/null >"$WORK_DIR/doc-check.json" 2>/dev/null || true
[ -s "$WORK_DIR/doc-check.json" ] || fail "check --output-format json printed nothing"
cat >"$WORK_DIR/serve-calls.json" <<EOF
[
  {"tool": "models_list", "arguments": {}, "expect": "$WORK_DIR/doc-models-list.json"},
  {"tool": "models_list", "arguments": {"all": true}, "expect": "$WORK_DIR/doc-models-list-all.json"},
  {"tool": "models_status", "arguments": {}, "expect": "$WORK_DIR/doc-models-status.json"},
  {"tool": "train_status", "arguments": {}, "expect": "$WORK_DIR/doc-train-status.json"},
  {"tool": "train_versions", "arguments": {}, "expect": "$WORK_DIR/doc-train-versions.json"},
  {"tool": "check", "arguments": {}, "expect": "$WORK_DIR/doc-check.json"}
]
EOF
python3 "$(dirname "$0")/../py/mcp_serve_client.py" "$APOGEE_BIN" "$WORK_DIR/serve-calls.json" </dev/null >"$WORK_DIR/serve.txt" 2>&1 || fail "mcp serve's handshake: $(cat "$WORK_DIR/serve.txt")"
# The public spelling registered as one more server, through the production
# client: the entry `mcp presets` prints for another host.
[ "$("$APOGEE_BIN" mcp presets --output-format json </dev/null | python3 -c 'import json,sys; print(json.dumps(json.load(sys.stdin)["data"][1]["args"]))')" = '["mcp", "serve"]' ] || fail "mcp presets' self does not name mcp serve"
"$APOGEE_BIN" mcp create public --command "$APOGEE_BIN" --args mcp serve </dev/null >/dev/null || fail "register mcp serve"
"$APOGEE_BIN" mcp test public models_status </dev/null >"$WORK_DIR/public.txt" 2>/dev/null || fail "mcp test against mcp serve: $(cat "$WORK_DIR/public.txt")"
grep -q '"fields"' "$WORK_DIR/public.txt" || fail "mcp serve did not answer models_status: $(cat "$WORK_DIR/public.txt")"

# --- the callable set (54b): complete, listed by the person, local only ------
# Nothing listed is the read set (checked above). A mock that answers with the
# bound its request carried is listed through the one editor; one that bills
# each call is refused there, naming the rule, the file untouched; and a host
# then sees complete after the reads, not read-only, answering exactly what
# `apogee complete` answers at the served bound.
printf '%s' '{"turns": [{"text": "max_tokens={{max_tokens}} asked={{last_user}}"}]}' >"$WORK_DIR/echo.json"
printf '%s' '{"metered": true, "turns": [{"text": "paid"}]}' >"$WORK_DIR/paid.json"
"$APOGEE_BIN" config add-backend served --type mock --model-path "$WORK_DIR/echo.json" </dev/null >/dev/null || fail "add the served backend"
"$APOGEE_BIN" config add-backend paid --type mock --model-path "$WORK_DIR/paid.json" </dev/null >/dev/null || fail "add the billed backend"
"$APOGEE_BIN" config set-backend served --mcp-serve complete </dev/null >/dev/null || fail "list complete on a local backend"
cp "$APOGEE_HOME/config/config.json" "$WORK_DIR/listed.json"
if "$APOGEE_BIN" config set-backend paid --mcp-serve complete </dev/null >/dev/null 2>"$WORK_DIR/paid.err"; then
    fail "a backend that bills each call was listed"
fi
grep -q "'paid' cannot serve complete: its provider bills each call" "$WORK_DIR/paid.err" || fail "the billed backend's refusal did not name the rule: $(cat "$WORK_DIR/paid.err")"
cmp -s "$APOGEE_HOME/config/config.json" "$WORK_DIR/listed.json" || fail "the refused listing changed the config"
"$APOGEE_BIN" complete -m served -n 1024 "hello from a host" </dev/null >"$WORK_DIR/doc-complete.txt" 2>/dev/null || fail "complete -m served"
[ "$(cat "$WORK_DIR/doc-complete.txt")" = "max_tokens=1024 asked=hello from a host" ] || fail "complete did not answer at the bound: $(cat "$WORK_DIR/doc-complete.txt")"
cat >"$WORK_DIR/callable-calls.json" <<EOF2
[
  {"tool": "complete", "arguments": {"prompt": "hello from a host", "backend": "served"}, "expect": "$WORK_DIR/doc-complete.txt"},
  {"tool": "models_status", "arguments": {}, "expect": "$WORK_DIR/doc-models-status-now.json"}
]
EOF2
"$APOGEE_BIN" models status --output-format json </dev/null >"$WORK_DIR/doc-models-status-now.json" 2>/dev/null || fail "models status --output-format json"
python3 "$(dirname "$0")/../py/mcp_serve_client.py" "$APOGEE_BIN" "$WORK_DIR/callable-calls.json" --callables=complete </dev/null >"$WORK_DIR/callable.txt" 2>&1 || fail "mcp serve's callable set: $(cat "$WORK_DIR/callable.txt")"
"$APOGEE_BIN" config set-backend served --mcp-serve "" </dev/null >/dev/null || fail "take complete back"

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
