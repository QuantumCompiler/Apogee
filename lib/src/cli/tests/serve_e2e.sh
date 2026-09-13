#!/usr/bin/env bash
# `apogee serve`, driven the way a remote client meets it: the real binary, a
# real socket, curl.
#
# The unit suite drives every route through the listener-free mux. What only a
# process can show is the other half of the listening-socket boundary -- that
# serve DOES hold a port (the sibling check proves `complete` does not), that
# the bind policy fails closed on the real command line, that the streamed
# body reaches a real client whole and ends with [DONE], that a session minted
# over HTTP is resumable from the terminal, and that SIGTERM stops the server
# cleanly. POSIX only, like the other shell checks (CLAUDE.md -> Platforms).

set -uo pipefail

APOGEE_BIN="${1:?usage: serve_e2e.sh <apogee-binary> <work-dir>}"
WORK_DIR="${2:?usage: serve_e2e.sh <apogee-binary> <work-dir>}"

if ! command -v curl >/dev/null 2>&1; then
    echo "serve_e2e: curl not found; skipping"
    exit 0
fi

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR"
export APOGEE_HOME="$WORK_DIR"
SERVER=""
cleanup() {
    if [ -n "$SERVER" ] && kill -0 "$SERVER" 2>/dev/null; then
        kill -KILL "$SERVER" 2>/dev/null
    fi
    rm -rf "$WORK_DIR"
}
trap cleanup EXIT

fail() { echo "serve_e2e: $*" >&2; exit 1; }

"$APOGEE_BIN" config init >/dev/null || fail "config init"
"$APOGEE_BIN" config add-backend mock --type mock --model mock-1 >/dev/null || fail "add-backend"
"$APOGEE_BIN" config set-default mock >/dev/null || fail "set-default"
# A vendor-CLI entry beside it: refused by type, whether or not it builds.
"$APOGEE_BIN" config add-backend claude-sub --type claude-cli >/dev/null || fail "add-backend claude-sub"

# --- fail-closed: a non-loopback bind needs --allow-remote ------------------
if "$APOGEE_BIN" serve --bind 0.0.0.0 --port 0 >"$WORK_DIR/closed.out" 2>"$WORK_DIR/closed.err" </dev/null; then
    fail "a non-loopback bind without --allow-remote was accepted"
fi
grep -q -- "--allow-remote" "$WORK_DIR/closed.err" || fail "the refusal did not name --allow-remote: $(cat "$WORK_DIR/closed.err")"

# --- start on a free port ----------------------------------------------------
"$APOGEE_BIN" serve --port 0 --all-backends --session-ttl 5 \
    >"$WORK_DIR/serve.out" 2>"$WORK_DIR/serve.err" </dev/null &
SERVER=$!

PORT=""
for _ in $(seq 1 100); do
    PORT="$(grep -o 'listening on http://127.0.0.1:[0-9]*' "$WORK_DIR/serve.err" 2>/dev/null | grep -o '[0-9]*$' || true)"
    [ -n "$PORT" ] && break
    kill -0 "$SERVER" 2>/dev/null || fail "serve exited before listening: $(cat "$WORK_DIR/serve.err")"
    sleep 0.1
done
[ -n "$PORT" ] || fail "serve never reported a port: $(cat "$WORK_DIR/serve.err")"
BASE="http://127.0.0.1:$PORT"

grep -q "skipping claude-sub" "$WORK_DIR/serve.err" || fail "the vendor-CLI entry was not announced as skipped"

# --- the positive half of the listening-socket boundary ---------------------
if command -v lsof >/dev/null 2>&1; then
    HIT="$(lsof -a -p "$SERVER" -i -sTCP:LISTEN -Fn 2>/dev/null)"
    [ -n "$HIT" ] || fail "serve holds no listening socket, but it answered on $PORT"
else
    echo "serve_e2e: lsof not found; the socket assertion is skipped"
fi

# --- health ------------------------------------------------------------------
curl -s "$BASE/health" | grep -q '"status":"ok"' || fail "/health did not answer ok"

# --- a session over HTTP, continued, then resumed from the terminal ----------
curl -s -D "$WORK_DIR/headers.txt" -o "$WORK_DIR/turn1.json" \
    -H 'Content-Type: application/json' \
    -d '{"messages":[{"role":"user","content":"one"}],"session_id":"new"}' \
    "$BASE/v1/chat/completions" || fail "turn 1 failed"
grep -q '"content":"mock response"' "$WORK_DIR/turn1.json" || fail "turn 1 carried no answer: $(cat "$WORK_DIR/turn1.json")"
SID="$(grep -i '^X-Apogee-Session-Id:' "$WORK_DIR/headers.txt" | tr -d '\r' | awk '{print $2}')"
[ -n "$SID" ] || fail "no X-Apogee-Session-Id header: $(cat "$WORK_DIR/headers.txt")"

curl -s -o "$WORK_DIR/turn2.json" -H 'Content-Type: application/json' \
    -d "{\"messages\":[{\"role\":\"user\",\"content\":\"two\"}],\"session_id\":\"$SID\"}" \
    "$BASE/v1/chat/completions" || fail "turn 2 failed"
grep -q "\"session_id\":\"$SID\"" "$WORK_DIR/turn2.json" || fail "turn 2 did not echo the session id"

curl -s "$BASE/v1/sessions/$SID" | grep -q '"turn_count":2' || fail "the session did not record two turns"

# The same conversation, continued from the terminal: a served session is a
# chat session. Three user turns end up on disk.
printf 'three\n' | "$APOGEE_BIN" chat --resume "$SID" >"$WORK_DIR/resume.out" 2>"$WORK_DIR/resume.err" \
    || fail "chat --resume failed: $(cat "$WORK_DIR/resume.err")"
grep -q "mock response" "$WORK_DIR/resume.out" || fail "the resumed chat produced no answer"
USER_TURNS="$(grep -o '"role": *"user"' "$WORK_DIR/sessions/$SID.json" | wc -l | tr -d ' ')"
[ "$USER_TURNS" = "3" ] || fail "expected 3 user turns on disk after the resume, found $USER_TURNS"

# --- streaming reaches a real client whole and ends with [DONE] ------------
curl -s -N -H 'Content-Type: application/json' \
    -d '{"messages":[{"role":"user","content":"hello"}],"stream":true,"apogee_events":true}' \
    "$BASE/v1/chat/completions" >"$WORK_DIR/stream.txt" || fail "the streamed request failed"
grep -q '"content":"mock res"' "$WORK_DIR/stream.txt" || fail "no content chunk in the stream"
grep -q '"meta":{' "$WORK_DIR/stream.txt" || fail "no meta-frame with apogee_events on"
LAST="$(grep -v '^$' "$WORK_DIR/stream.txt" | tail -1)"
[ "$LAST" = "data: [DONE]" ] || fail "the stream did not end with [DONE]: $LAST"

# --- refusals take the error shape ------------------------------------------
CODE="$(curl -s -o "$WORK_DIR/cli.json" -w '%{http_code}' -H 'Content-Type: application/json' \
    -d '{"model":"claude-sub","messages":[{"role":"user","content":"x"}]}' "$BASE/v1/chat/completions")"
[ "$CODE" = "400" ] || fail "a vendor-CLI backend was not refused (got $CODE)"
grep -q "vendor CLI" "$WORK_DIR/cli.json" || fail "the refusal did not say why: $(cat "$WORK_DIR/cli.json")"

CODE="$(curl -s -o "$WORK_DIR/404.json" -w '%{http_code}' "$BASE/nope")"
[ "$CODE" = "404" ] || fail "an unknown route was not a 404 (got $CODE)"
grep -q '"error":{' "$WORK_DIR/404.json" || fail "the 404 was not in the error shape"

# --- SIGTERM stops it cleanly ------------------------------------------------
kill -TERM "$SERVER"
for _ in $(seq 1 100); do
    kill -0 "$SERVER" 2>/dev/null || break
    sleep 0.1
done
if kill -0 "$SERVER" 2>/dev/null; then
    fail "serve did not stop within 10s of SIGTERM"
fi
wait "$SERVER"
STATUS=$?
SERVER=""
[ "$STATUS" -eq 0 ] || fail "serve exited $STATUS after SIGTERM: $(cat "$WORK_DIR/serve.err")"
grep -q "stopped" "$WORK_DIR/serve.err" || fail "serve did not announce that it stopped"

echo "apogee serve end-to-end: OK"
