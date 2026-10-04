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
# A developer's own keys stay out of the suite (the resolver reads the environment).
unset ANTHROPIC_API_KEY OPENAI_API_KEY GEMINI_API_KEY GOOGLE_API_KEY
SERVER=""
cleanup() {
    if [ -n "$SERVER" ] && kill -0 "$SERVER" 2>/dev/null; then
        kill -KILL "$SERVER" 2>/dev/null
    fi
    rm -rf "$WORK_DIR"
}
trap cleanup EXIT

fail() { echo "serve_e2e: $*" >&2; exit 1; }

# The mode of a path as three octal digits, on either stat flavour: GNU stat
# has no -f mode format (its -f is file-system status, which prints), BSD stat
# has no -c.
mode_of() {
    if stat --version >/dev/null 2>&1; then stat -c '%a' "$1"; else stat -f '%Lp' "$1"; fi
}

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

# --- the control plane: gated, and byte-identical to the CLI -----------------
TOKEN="$("$APOGEE_BIN" serve --print-admin-token </dev/null 2>/dev/null)"
[ -n "$TOKEN" ] || fail "--print-admin-token printed nothing"
[ "$(mode_of "$WORK_DIR/config/admin-token")" = "600" ] \
    || fail "the admin token is not 0600"

CODE="$(curl -s -o "$WORK_DIR/noauth.json" -w '%{http_code}' "$BASE/v1/admin/backends")"
[ "$CODE" = "401" ] || fail "an unauthenticated admin request was not a 401 (got $CODE)"
grep -q '"authentication_error"' "$WORK_DIR/noauth.json" || fail "the 401 was not in the error shape"
CODE="$(curl -s -o /dev/null -w '%{http_code}' "$BASE/v1/admin/backends?token=$TOKEN")"
[ "$CODE" = "401" ] || fail "a query-string token was accepted (got $CODE)"
CODE="$(curl -s -o /dev/null -w '%{http_code}' -H "Authorization: Bearer wrong" "$BASE/v1/admin/backends")"
[ "$CODE" = "401" ] || fail "a wrong bearer was accepted (got $CODE)"

CODE="$(curl -s -o "$WORK_DIR/admin-list.json" -w '%{http_code}' -H "Authorization: Bearer $TOKEN" "$BASE/v1/admin/backends")"
[ "$CODE" = "200" ] || fail "the bearer was refused (got $CODE): $(cat "$WORK_DIR/admin-list.json")"
grep -q '"name":"mock"' "$WORK_DIR/admin-list.json" || fail "the backend list did not name mock"
grep -q 'api_key' "$WORK_DIR/admin-list.json" && ! grep -q '"api_key":' "$WORK_DIR/admin-list.json" \
    || fail "the backend view carried a key field: $(cat "$WORK_DIR/admin-list.json")"

# An HTTP add-backend the CLI then reads back -- and the file it wrote is the
# one the CLI would have written.
cp "$WORK_DIR/config/config.yaml" "$WORK_DIR/before.yaml"
CODE="$(curl -s -o "$WORK_DIR/added.json" -w '%{http_code}' -H "Authorization: Bearer $TOKEN" \
    -H 'Content-Type: application/json' \
    -d '{"name":"second","type":"mock","model":"mock-2"}' "$BASE/v1/admin/backends")"
[ "$CODE" = "201" ] || fail "add-backend over HTTP failed (got $CODE): $(cat "$WORK_DIR/added.json")"
grep -q '"restart_required":true' "$WORK_DIR/added.json" || fail "a new backend did not report restart_required"
[ "$("$APOGEE_BIN" config get backends.second.type </dev/null)" = "mock" ] || fail "the CLI does not see the backend added over HTTP"
cp "$WORK_DIR/config/config.yaml" "$WORK_DIR/http.yaml"
cp "$WORK_DIR/before.yaml" "$WORK_DIR/config/config.yaml"
"$APOGEE_BIN" config add-backend second --type mock --model mock-2 >/dev/null </dev/null || fail "CLI add-backend"
cmp -s "$WORK_DIR/config/config.yaml" "$WORK_DIR/http.yaml" || fail "the HTTP edit and the CLI edit produced different bytes"

# The lifecycle stream: a session minted over the public plane shows up on it.
curl -s -N --max-time 3 -H "Authorization: Bearer $TOKEN" "$BASE/v1/admin/events" >"$WORK_DIR/events.txt" 2>/dev/null &
EVENTS=$!
sleep 0.3
curl -s -o /dev/null -H 'Content-Type: application/json' \
    -d '{"messages":[{"role":"user","content":"ping"}],"session_id":"new"}' "$BASE/v1/chat/completions"
wait $EVENTS 2>/dev/null
grep -q '^event: session.created' "$WORK_DIR/events.txt" || fail "the events stream did not carry session.created: $(cat "$WORK_DIR/events.txt")"
grep -q '^event: agent.run.completed' "$WORK_DIR/events.txt" || fail "the events stream did not carry agent.run.completed"
grep -q "$TOKEN" "$WORK_DIR/events.txt" && fail "the token appeared in the event stream"

# --- the credential store: private, keyed by type, never echoed anywhere -----
SECRET="sk-e2e-LEAKPROBE-$$-$(date +%s)"
printf '%s\n' "$SECRET" | "$APOGEE_BIN" auth add openai --stdin >"$WORK_DIR/auth-add.txt" 2>&1 || fail "auth add openai --stdin: $(cat "$WORK_DIR/auth-add.txt")"
[ "$(mode_of "$WORK_DIR/config/credentials.json")" = "600" ] \
    || fail "the credential store is not 0600"
"$APOGEE_BIN" auth list </dev/null >"$WORK_DIR/auth-list.txt" 2>&1 || fail "auth list: $(cat "$WORK_DIR/auth-list.txt")"
grep -q 'openai' "$WORK_DIR/auth-list.txt" || fail "auth list did not show the openai slot: $(cat "$WORK_DIR/auth-list.txt")"
# A keyless cloud entry now resolves from the store, and the doctor says so.
"$APOGEE_BIN" config add-backend gpt --type openai --model gpt-e2e >/dev/null </dev/null || fail "add-backend gpt"
# (The throwaway home has no models/ or cache/ and umask-mode directories, so
# the doctor fails rows this test is not about; its exit status is not asserted.)
"$APOGEE_BIN" check </dev/null >"$WORK_DIR/check.txt" 2>&1 || true
grep -q 'from the credential store' "$WORK_DIR/check.txt" || fail "check did not attribute the key to the store: $(cat "$WORK_DIR/check.txt")"
# The same over HTTP: metadata only, a PUT from loopback, a DELETE.
CODE="$(curl -s -o "$WORK_DIR/auth-get.json" -w '%{http_code}' -H "Authorization: Bearer $TOKEN" "$BASE/v1/admin/auth")"
[ "$CODE" = "200" ] || fail "GET /v1/admin/auth failed (got $CODE): $(cat "$WORK_DIR/auth-get.json")"
grep -q '"provider":"openai"' "$WORK_DIR/auth-get.json" || fail "the credential listing did not name the openai slot: $(cat "$WORK_DIR/auth-get.json")"
grep -q '"stored_at"' "$WORK_DIR/auth-get.json" || fail "the credential listing carried no stored_at"
CODE="$(curl -s -o /dev/null -w '%{http_code}' "$BASE/v1/admin/auth")"
[ "$CODE" = "401" ] || fail "an unauthenticated credential listing was not a 401 (got $CODE)"
CODE="$(curl -s -o "$WORK_DIR/auth-put.json" -w '%{http_code}' -X PUT -H "Authorization: Bearer $TOKEN" \
    -H 'Content-Type: application/json' -d "{\"key\":\"$SECRET-anthropic\"}" "$BASE/v1/admin/auth/anthropic")"
[ "$CODE" = "200" ] || fail "PUT /v1/admin/auth/anthropic from loopback failed (got $CODE): $(cat "$WORK_DIR/auth-put.json")"
CODE="$(curl -s -o /dev/null -w '%{http_code}' -X PUT -H "Authorization: Bearer $TOKEN" \
    -H 'Content-Type: application/json' -d '{"key":"x"}' "$BASE/v1/admin/auth/claude-cli")"
[ "$CODE" = "400" ] || fail "a vendor-CLI credential slot was accepted (got $CODE)"
CODE="$(curl -s -o "$WORK_DIR/auth-del.json" -w '%{http_code}' -X DELETE -H "Authorization: Bearer $TOKEN" "$BASE/v1/admin/auth/anthropic")"
[ "$CODE" = "200" ] || fail "DELETE /v1/admin/auth/anthropic failed (got $CODE): $(cat "$WORK_DIR/auth-del.json")"
CODE="$(curl -s -o /dev/null -w '%{http_code}' -X DELETE -H "Authorization: Bearer $TOKEN" "$BASE/v1/admin/auth/anthropic")"
[ "$CODE" = "404" ] || fail "clearing an empty slot was not a 404 (got $CODE)"
# The secret is in exactly one file. Nothing else that was written mentions it.
grep -q "$SECRET" "$WORK_DIR/config/credentials.json" || fail "the store does not hold the key"
for f in "$WORK_DIR"/*.txt "$WORK_DIR"/*.json "$WORK_DIR"/serve.err "$WORK_DIR"/config/config.yaml "$WORK_DIR"/config/admin-token; do
    case "$f" in */credentials.json) continue ;; esac
    [ -f "$f" ] || continue
    grep -q "LEAKPROBE" "$f" && fail "the secret leaked into $f"
done
if [ -d "$WORK_DIR/logs" ]; then
    grep -rq "LEAKPROBE" "$WORK_DIR/logs" && fail "the secret leaked into the operational log"
fi
"$APOGEE_BIN" auth clear openai </dev/null >/dev/null || fail "auth clear openai"
# A vendor CLI has no slot -- refused with the principle -- and a key is never
# taken on the command line.
if printf 'x\n' | "$APOGEE_BIN" auth add claude-cli --stdin >"$WORK_DIR/auth-cli.txt" 2>&1; then
    fail "auth add accepted a vendor-CLI type"
fi
grep -qi 'vendor CLI' "$WORK_DIR/auth-cli.txt" || fail "the vendor-CLI refusal did not name the principle: $(cat "$WORK_DIR/auth-cli.txt")"
if "$APOGEE_BIN" auth add openai sk-on-the-command-line </dev/null >/dev/null 2>&1; then
    fail "auth add accepted a key as a command-line argument"
fi
grep -q "LEAKPROBE" "$WORK_DIR/config/credentials.json" && fail "auth clear left the key in the store"

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
