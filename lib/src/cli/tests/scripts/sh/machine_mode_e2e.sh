#!/usr/bin/env bash
# Machine mode, driven the way a GUI drives it: over real pipes, against the
# real binary.
#
# The unit suite covers the JSONL vocabulary. What only an exec-style test can
# show is what a driver actually depends on: that stdout carries NOTHING but
# events when the binary runs for real, that one child serves many turns, and
# that none of it opens a listening socket. Those are properties of the
# process, not of a class.
#
# POSIX only -- the socket half needs lsof and skips itself cleanly without it
# (CLAUDE.md -> Platforms).

set -uo pipefail

APOGEE_BIN="${1:?usage: machine_mode_e2e.sh <apogee-binary> <work-dir>}"
WORK_DIR="${2:?usage: machine_mode_e2e.sh <apogee-binary> <work-dir>}"

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR"
export APOGEE_HOME="$WORK_DIR"
trap 'rm -rf "$WORK_DIR"' EXIT

fail() { echo "machine_mode: $*" >&2; exit 1; }

"$APOGEE_BIN" config init >/dev/null || fail "config init"
"$APOGEE_BIN" config add-backend mock --type mock --model mock-1 >/dev/null || fail "add-backend"
"$APOGEE_BIN" config set-default mock >/dev/null || fail "set-default"

# --- complete: stdout carries only JSONL -------------------------------------
"$APOGEE_BIN" complete --output-format stream-json "hello" \
    >"$WORK_DIR/out.jsonl" 2>"$WORK_DIR/err.txt" || fail "complete failed: $(cat "$WORK_DIR/err.txt")"

[ -s "$WORK_DIR/out.jsonl" ] || fail "stream-json produced no events"

# Every line must open with '{'. A driver's parser reads line-oriented JSON, so
# a single line of prose breaks it -- the exact failure Apogee guards against
# when consuming other vendors' CLIs, owed here to its own consumers.
while IFS= read -r line; do
    [ -z "$line" ] && continue
    case "$line" in
        '{'*) ;;
        *) fail "stdout carried a non-JSON line: $line" ;;
    esac
done < "$WORK_DIR/out.jsonl"

grep -q '"type":"session"' "$WORK_DIR/out.jsonl" || fail "no session event"
grep -q '"type":"result"'  "$WORK_DIR/out.jsonl" || fail "no terminal result event"

# --- one driven child, many turns --------------------------------------------
printf '%s\n' \
    '{"type":"user","text":"first"}' \
    '{"type":"unknown_type_from_a_later_build"}' \
    '{"type":"user","text":"second"}' \
    | "$APOGEE_BIN" chat --output-format stream-json --input-format stream-json \
        >"$WORK_DIR/chat.jsonl" 2>"$WORK_DIR/chat-err.txt" \
    || fail "driven chat failed: $(cat "$WORK_DIR/chat-err.txt")"

SESSIONS=$(grep -c '"type":"session"' "$WORK_DIR/chat.jsonl")
RESULTS=$(grep -c '"type":"result"' "$WORK_DIR/chat.jsonl")

# One session event proves it was one process; two results prove it served both
# turns without respawning. The unknown input type must have changed neither --
# that is the tolerance this protocol demands of drivers, honoured inbound.
[ "$SESSIONS" -eq 1 ] || fail "expected 1 session event, got $SESSIONS (did the child respawn?)"
[ "$RESULTS" -eq 2 ] || fail "expected 2 result events, got $RESULTS"

ls "$WORK_DIR"/sessions/*.json >/dev/null 2>&1 || fail "closing stdin persisted no session"

# --- the handshake (28d): capabilities on session, an optional hello ---------
# The session announces what it can do before the first turn, a driver that
# says hello sees exactly what one that does not sees, a hello is recorded in
# the operational log, and one anywhere but the first line is ignored with a
# note -- never an error. A driver that never sends one is untouched.
HS_DIR="$WORK_DIR/handshake"
mkdir -p "$HS_DIR"
printf '{"turns":[{"text":"echo: {{last_user}}"}]}' >"$HS_DIR/echo.json"
"$APOGEE_BIN" config add-backend echo --type mock --model-path "$HS_DIR/echo.json" >/dev/null \
    || fail "add echo backend"
printf '%s\n' '{"type":"user","text":"one"}' '{"type":"user","text":"two"}' \
    | "$APOGEE_BIN" chat -m echo --no-recall --output-format stream-json --input-format stream-json \
        >"$HS_DIR/plain.jsonl" 2>"$HS_DIR/plain.err" || fail "plain driven chat failed"
printf '%s\n' '{"type":"hello","client":{"name":"e2e-host","version":"9.1"},"wants":["tools"]}' \
    '{"type":"user","text":"one"}' '{"type":"hello","client":{"name":"late"}}' \
    '{"type":"user","text":"two"}' \
    | "$APOGEE_BIN" chat -m echo --no-recall --output-format stream-json --input-format stream-json \
        >"$HS_DIR/hello.jsonl" 2>"$HS_DIR/hello.err" || fail "hello driven chat failed: $(cat "$HS_DIR/hello.err")"
python3 - "$HS_DIR/plain.jsonl" "$HS_DIR/hello.jsonl" <<'PY' || fail "the handshake streams disagree"
import json, sys
plain, hello = ([json.loads(l) for l in open(p) if l.strip()] for p in sys.argv[1:3])
for stream in (plain, hello):
    session = stream[0]
    assert session["type"] == "session", session
    caps = session["capabilities"]
    assert caps["accepts"] == ["user", "answer", "attach", "hello", "cancel"], caps
    assert "result" in caps["events"] and "question" in caps["events"], caps
    assert caps["tools"] is False and caps["ask"] is False, caps
    assert isinstance(caps["schema"], str) and caps["schema"], caps
    # Today's fields, as a v1 driver reads them.
    assert session["protocol_version"] == 1 and session["model"], session
# Identical turns, hello or not -- the session event aside.
assert [e for e in plain[1:]] == [e for e in hello[1:]], (plain, hello)
assert [e["text"] for e in hello if e["type"] == "result"] == ["echo: one", "echo: two"], hello
PY
grep -q 'a hello after the first line is ignored' "$HS_DIR/hello.err" || fail "the late hello was not noted"
grep -rq 'hello from e2e-host 9.1, wants \["tools"\]' "$APOGEE_HOME/logs" || fail "the hello was not recorded"
grep -rq 'hello from late' "$APOGEE_HOME/logs" && fail "a late hello was recorded"
# complete, its prompt piped: a hello first is recorded and never asked.
printf '%s\n%s\n' '{"type":"hello","client":{"name":"one-shot"}}' 'the real prompt' \
    | "$APOGEE_BIN" complete -m echo --output-format stream-json >"$HS_DIR/complete.jsonl" \
        2>"$HS_DIR/complete.err" || fail "complete with a hello failed: $(cat "$HS_DIR/complete.err")"
grep -q '"text":"echo: the real prompt"' "$HS_DIR/complete.jsonl" || fail "the hello reached the prompt: $(cat "$HS_DIR/complete.jsonl")"
grep -q '"accepts":\["hello"\]' "$HS_DIR/complete.jsonl" || fail "complete's capabilities were wrong"
"$APOGEE_BIN" complete -m echo --output-format stream-json "an argument" </dev/null \
    >"$HS_DIR/arg.jsonl" 2>&1 || fail "complete with an argument failed"
grep -q '"accepts":\[\]' "$HS_DIR/arg.jsonl" || fail "complete with an argument claimed to read stdin"

# --- turn ids and cancel (28f) ------------------------------------------------
# A driver over real pipes: every turn-scoped event carries its turn; a cancel
# with nothing in flight changes nothing; a cancel mid-stream ends the turn in
# one `result` with finish_reason `cancelled` and the text streamed so far, the
# half-turn kept out of the saved session, and the same child answers the next
# turn; a cancel while a question waits fails the turn the same way; a turn
# that fails ends in `error`, never a `result` carrying the user's own words.
TC_DIR="$WORK_DIR/turns"
mkdir -p "$TC_DIR/bin"
printf '%s' '{"turns":[{"text":"aaaaaaaabbbbbbbbccccccccddddddddeeeeeeeeffffffffgggggggghhhhhhhh","delay_ms":150},{"text":"echo: {{last_user}}"}]}' >"$TC_DIR/slow.json"
printf '%s' '{"turns":[{"text":"","tool_calls":[{"name":"ask_user","arguments":{"questions":[{"header":"Pick","question":"Which?","multi_select":false,"options":[{"label":"a","description":"A"},{"label":"b","description":"B"}]}]}}]},{"text":"echo: {{last_user}}"}]}' >"$TC_DIR/asks.json"
printf '#!/bin/sh\necho x\n' >"$TC_DIR/bin/ollama"
chmod +x "$TC_DIR/bin/ollama"
"$APOGEE_BIN" config add-backend slow --type mock --model-path "$TC_DIR/slow.json" >/dev/null || fail "add slow"
"$APOGEE_BIN" config add-backend asks --type mock --model-path "$TC_DIR/asks.json" >/dev/null || fail "add asks"
"$APOGEE_BIN" config add-backend down --type ollama-cli --model m >/dev/null || fail "add down"
python3 - "$APOGEE_HOME/config/config.yaml" "$TC_DIR/bin/ollama" <<'PY' || fail "could not point the down backend"
import sys
path, binary = sys.argv[1], sys.argv[2]
text = open(path).read()
text = text.replace("  down:\n    type: ollama-cli\n", "  down:\n    type: ollama-cli\n    host: 127.0.0.1:9\n    binary: " + binary + "\n", 1)
open(path, "w").write(text)
PY
python3 - "$APOGEE_BIN" "$APOGEE_HOME" <<'PY' || fail "turn ids and cancel"
import glob, json, os, subprocess, sys, time

binary, home = sys.argv[1], sys.argv[2]

class Child:
    def __init__(self, *args):
        self.p = subprocess.Popen([binary, "chat", "--no-recall", "--output-format", "stream-json",
                                   "--input-format", "stream-json", *args],
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, text=True, bufsize=1)
    def send(self, obj):
        self.p.stdin.write(json.dumps(obj) + "\n")
        self.p.stdin.flush()
    def read(self):
        line = self.p.stdout.readline()
        assert line, "the stream ended early: " + self.p.stderr.read()
        return json.loads(line)
    def until(self, kind):
        seen = []
        while True:
            event = self.read()
            seen.append(event)
            if event["type"] in kind:
                return seen
    def close(self):
        self.p.stdin.close()
        assert self.p.wait(timeout=30) == 0, self.p.stderr.read()

def fail(message):
    print("turns: " + message, file=sys.stderr)
    sys.exit(1)

# A cancel mid-stream, a cancel with nothing in flight, the next turn.
child = Child("-m", "slow")
session = child.read()
if session["type"] != "session" or session.get("next_turn") != 1 or "turn" in session:
    fail(f"session: {session}")
child.send({"type": "cancel"})  # nothing in flight: nothing happens
child.send({"type": "user", "text": "first"})
streamed = child.until({"answer_delta", "result"})
if streamed[-1]["type"] != "answer_delta":
    fail(f"no delta before the result: {streamed}")
child.send({"type": "cancel"})
ended = child.until({"result", "error"})
result = ended[-1]
for event in streamed + ended:
    if event.get("turn") != 1:
        fail(f"an event of turn 1 lacks it: {event}")
if result["type"] != "result" or result.get("finish_reason") != "cancelled":
    fail(f"the cancelled turn did not end in a cancelled result: {result}")
if not result["text"] or not "aaaaaaaabbbbbbbbccccccccddddddddeeeeeeeeffffffffgggggggghhhhhhhh".startswith(result["text"]):
    fail(f"the result is not the partial answer: {result!r}")
if result["text"] == "aaaaaaaabbbbbbbbccccccccddddddddeeeeeeeeffffffffgggggggghhhhhhhh":
    fail("the turn was not cut short")
child.send({"type": "user", "text": "second"})
second = child.until({"result", "error"})
if second[-1].get("text") != "echo: second" or any(e.get("turn") != 2 for e in second):
    fail(f"the next turn: {second}")
child.send({"type": "cancel"})  # a cancel after the result: harmless
child.close()
newest = max(glob.glob(os.path.join(home, "sessions", "*.json")), key=os.path.getmtime)
saved = json.load(open(newest))
texts = json.dumps(saved.get("messages", []))
if "first" in texts or "aaaaaaaa" in texts or "second" not in texts:
    fail(f"the saved session holds the cancelled half-turn: {texts[:400]}")

# A cancel while a question waits.
child = Child("-m", "asks", "--tools")
child.read()
child.send({"type": "user", "text": "ask me"})
asked = child.until({"question", "result", "error"})
if asked[-1]["type"] != "question" or asked[-1].get("turn") != 1:
    fail(f"no question: {asked}")
child.send({"type": "cancel"})
ended = child.until({"result", "error"})
if ended[-1]["type"] != "result" or ended[-1].get("finish_reason") != "cancelled":
    fail(f"the asked turn did not end cancelled: {ended}")
child.send({"type": "user", "text": "again"})
again = child.until({"result", "error"})
if again[-1].get("text") != "echo: again" or again[-1].get("turn") != 2:
    fail(f"the turn after a cancelled question: {again}")
child.close()

# A turn that fails ends in error, numbered -- one terminal event per line.
child = Child("-m", "down")
child.read()
child.send({"type": "user", "text": "my own words"})
failed = child.until({"result", "error"})
if failed[-1]["type"] != "error" or failed[-1].get("turn") != 1:
    fail(f"a failed turn did not end in a numbered error: {failed}")
if any(e["type"] == "result" for e in failed):
    fail(f"a failed turn emitted a result: {failed}")
child.close()
print("turns: ids on every turn event, cancel mid-stream and mid-question, a failure an error - OK")
PY

# --- the schema artifact (28g): every captured stream validates ----------------
# The real streams this script captured -- complete, driven chats, handshakes
# -- validated line by line against what this binary prints as its schema, with
# a stock validator; and one known event mangled, which must not validate.
"$APOGEE_BIN" __machine-schema >"$WORK_DIR/machine-schema.json" </dev/null \
    || fail "__machine-schema failed"
python3 - "$WORK_DIR/machine-schema.json" "$WORK_DIR" <<'PY' || fail "a captured stream does not validate"
import glob, json, os, sys
try:
    from jsonschema import Draft202012Validator
except ImportError:
    print("machine_mode: no jsonschema module -- the captured streams were NOT validated",
          file=sys.stderr)
    sys.exit(0)
schema = json.load(open(sys.argv[1]))
valid = Draft202012Validator(schema)
streams = [p for p in glob.glob(os.path.join(sys.argv[2], "**", "*.jsonl"), recursive=True)]
assert streams, "no captured stream to validate"
lines = 0
for path in streams:
    for line in open(path, encoding="utf-8", errors="replace"):
        if not line.strip():
            continue
        event = json.loads(line)
        errors = list(valid.iter_errors(event))
        assert not errors, f"{os.path.basename(path)}: {line.strip()[:200]} -- {errors[0].message}"
        lines += 1
mangled = {"type": "result", "model": "m", "finish_reason": "stop", "turn": "one"}
assert list(valid.iter_errors(mangled)), "a mangled result validated"
print(f"machine_mode: {lines} captured lines in {len(streams)} streams validate against the schema")
PY

# --- bytes that are not UTF-8 never end the session ---------------------------
# Every event, session file and request is a strict JSON dump, and each of
# these used to end the process with json type_error 316 at the first one: a
# valid é a backend splits between two streamed pieces (the mock streams eight
# bytes at a time, llama.cpp a byte-fallback token a byte at a time), a tool
# reading a Latin-1 file, and a line piped in Latin-1. The split character now
# arrives whole; the rest is U+FFFD.
UTF8_DIR="$WORK_DIR/utf8"
mkdir -p "$UTF8_DIR"
# é at bytes 8 and 9: the mock's first chunk ends inside it.
printf '{"turns":[{"text":"1234567\303\251 caf\303\251"}]}' >"$UTF8_DIR/split.json"
printf '%s' '{"turns":[{"tool_calls":[{"name":"read_file","arguments":{"path":"latin1.txt"}}]},{"text":"read: {{last_tool_result}}"}]}' \
    >"$UTF8_DIR/reader.json"
printf 'caf\351 cr\350me\n' >"$UTF8_DIR/latin1.txt"
"$APOGEE_BIN" config add-backend split --type mock --model-path "$UTF8_DIR/split.json" \
    >/dev/null || fail "add-backend split"
"$APOGEE_BIN" config add-backend reader --type mock --model-path "$UTF8_DIR/reader.json" \
    >/dev/null || fail "add-backend reader"

"$APOGEE_BIN" complete -m split --output-format stream-json "hi" </dev/null \
    >"$UTF8_DIR/split.jsonl" 2>"$UTF8_DIR/split-err.txt" \
    || fail "a split character ended complete: $(cat "$UTF8_DIR/split-err.txt")"
( cd "$UTF8_DIR" && printf '%s\n' '{"type":"user","text":"read it"}' \
    | "$APOGEE_BIN" chat -m reader --tools --output-format stream-json --input-format stream-json ) \
    >"$UTF8_DIR/reader.jsonl" 2>"$UTF8_DIR/reader-err.txt" \
    || fail "a Latin-1 file read ended a driven chat: $(cat "$UTF8_DIR/reader-err.txt")"
printf 'caf\351\n' | "$APOGEE_BIN" chat --system "$(printf 'r\351sum\351')" \
    >/dev/null 2>"$UTF8_DIR/piped-err.txt" \
    || fail "a Latin-1 line ended chat: $(cat "$UTF8_DIR/piped-err.txt")"

python3 - "$UTF8_DIR" "$WORK_DIR/sessions" <<'EOF' || fail "the UTF-8 streams or sessions are wrong"
import glob, json, os, sys
utf8, sessions = sys.argv[1], sys.argv[2]
def events(name):  # every line on stdout is JSON, or this throws
    return [json.loads(line) for line in open(os.path.join(utf8, name), encoding="utf-8")]
def said(stream, kind):
    return [e["text"] for e in stream if e["type"] == kind]
split = events("split.jsonl")
assert "".join(said(split, "answer_delta")) == "1234567é café", split
assert said(split, "result") == ["1234567é café"], split
read = events("reader.jsonl")
assert said(read, "result") == ["read: caf� cr�me"], read
messages = [m for path in glob.glob(os.path.join(sessions, "*.json"))
            for m in json.load(open(path, encoding="utf-8"))["messages"]]
contents = [m["content"].strip() for m in messages if isinstance(m.get("content"), str)]
assert "caf� cr�me" in contents, contents   # the tool result, as history keeps it
assert "caf�" in contents, contents              # the piped line
assert "r�sum�" in contents, contents        # its --system
EOF

# --- a flag is never silently ignored ----------------------------------------
# Mixing the directions is refused rather than half-honoured. A driver that
# asked for JSONL and got prose would debug output it never requested.
if "$APOGEE_BIN" chat --input-format stream-json --output-format text \
        </dev/null >/dev/null 2>"$WORK_DIR/mixed.txt"; then
    fail "a contradictory format pair was accepted"
fi
grep -q "cannot be combined" "$WORK_DIR/mixed.txt" \
    || fail "the refusal did not say why: $(cat "$WORK_DIR/mixed.txt")"

# --- no listening socket ------------------------------------------------------
# Machine mode is pipes only. It is the surface a GUI drives, and the whole
# reason the GUI contract is stdio rather than localhost -- so the
# interactive-never-listens invariant matters here more than anywhere.
#
# Sampled CONTINUOUSLY while the child lives, not once: a single sample lands
# during the stdin block, BEFORE the turn runs, so a socket opened while
# talking to the backend would go unseen. A deliberately-violating build passed
# the one-shot version of this check in no_listen_check.sh.
if ! command -v lsof >/dev/null 2>&1; then
    echo "machine_mode: lsof not found; the no-listen half is skipped"
else
    ( sleep 3; printf '%s\n' '{"type":"user","text":"held"}' ) \
        | "$APOGEE_BIN" chat --output-format stream-json --input-format stream-json \
            >"$WORK_DIR/held.jsonl" 2>/dev/null &
    CHILD=$!

    LISTENING=""
    while kill -0 "$CHILD" 2>/dev/null; do
        HIT="$(lsof -a -p "$CHILD" -i -sTCP:LISTEN -Fn 2>/dev/null)"
        if [ -n "$HIT" ]; then
            LISTENING="$HIT"
            break
        fi
    done

    wait "$CHILD"
    STATUS=$?

    if [ -n "$LISTENING" ]; then
        echo "INVARIANT VIOLATED: machine mode opened a listening socket:" >&2
        echo "$LISTENING" >&2
        echo "Only 'apogee serve' may own a port. See SPEC.md -> Principles." >&2
        exit 1
    fi
    [ "$STATUS" -eq 0 ] || fail "the held turn failed (exit $STATUS)"
    grep -q '"type":"result"' "$WORK_DIR/held.jsonl" || fail "the held turn produced no result"
fi

echo "machine mode: stdout is pure JSONL, one child served 2 turns, bytes that are not UTF-8" \
    "ended nothing, no listening socket - OK"
