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

. "$(dirname "$0")/sandbox.sh"
WORK_DIR="$(sandbox_dir "$WORK_DIR")"
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
    assert caps["accepts"] == ["user", "answer", "attach", "hello", "cancel", "detach", "set",
                               "compact"], caps
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
# --- the session's own doors and a private chat (52d), on the real binary ---
BEFORE=$(ls "$WORK_DIR"/sessions/*.json 2>/dev/null | wc -l)
printf '%s\n' '{"type":"set","temperature":0.4,"max_tokens":3}' '{"type":"set","temperature":0.4}' \
    '{"type":"user","text":"one"}' \
    | "$APOGEE_BIN" chat -m echo --private --output-format stream-json --input-format stream-json \
        >"$HS_DIR/private.jsonl" 2>"$HS_DIR/private.err" || fail "private driven chat failed: $(cat "$HS_DIR/private.err")"
python3 - "$HS_DIR/private.jsonl" <<'PY' || fail "the private stream: $(cat "$HS_DIR/private.jsonl")"
import json, sys
events = [json.loads(l) for l in open(sys.argv[1]) if l.strip()]
assert events[0]["type"] == "session" and events[0]["private"] is True, events[0]
assert "chat_id" not in events[0], events[0]  # it writes no chat, so names none (M28)
notices = [e["text"] for e in events if e["type"] == "notice"]
assert "private: nothing of this chat is written -- no transcript, no recall" in notices, notices
assert any(n.startswith("set: 'max_tokens' is not a setting") for n in notices), notices
assert "temperature: 0.4 -- from the next turn" in notices, notices
assert [e["text"] for e in events if e["type"] == "result"] == ["echo: one"], events
PY
AFTER=$(ls "$WORK_DIR"/sessions/*.json 2>/dev/null | wc -l)
[ "$BEFORE" -eq "$AFTER" ] || fail "a private chat wrote a session file"
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

# --- private stays private (55a): no title asked, at either door ---------------
# The CLI/GUI audit's probe (2026-10-22), pinned: the utility model -- which
# titles a chat -- is a claude-cli entry whose binary logs every prompt it is
# handed. A private chat, driven or on a pipe, hands it nothing; the same chat
# written hands it its question, which is what the absence is measured
# against. Its own home, so no other section's config moves.
PV_HOME="$WORK_DIR/private-title"
PV_LOG="$PV_HOME/utility.log"
mkdir -p "$PV_HOME/bin"
cat >"$PV_HOME/bin/claude" <<EOF
#!/bin/sh
for last in "\$@"; do :; done
printf '%s\n' "\$last" >>"$PV_LOG"
printf '%s\n' '{"type":"result","subtype":"success","is_error":false,"result":"Acme Merger Talk"}'
EOF
chmod +x "$PV_HOME/bin/claude"
pv() { APOGEE_HOME="$PV_HOME" "$APOGEE_BIN" "$@"; }
pv config init >/dev/null || fail "private-title: config init"
pv config add-backend mock --type mock --model mock-1 >/dev/null || fail "private-title: add mock"
pv config set-default mock >/dev/null || fail "private-title: set-default"
pv config add-backend util --type claude-cli >/dev/null || fail "private-title: add util"
pv config set-backend util --binary "$PV_HOME/bin/claude" >/dev/null || fail "private-title: point util"
pv config set-default-utility util >/dev/null || fail "private-title: set-default-utility"
printf '%s\n' '{"type":"user","text":"our confidential merger with Acme closes Friday"}' \
    | pv chat --private --output-format stream-json --input-format stream-json \
        >"$PV_HOME/driven.jsonl" 2>"$PV_HOME/driven.err" \
    || fail "private-title: the driven chat failed: $(cat "$PV_HOME/driven.err")"
grep -q '"type":"result"' "$PV_HOME/driven.jsonl" || fail "private-title: the driven chat answered nothing"
[ ! -e "$PV_LOG" ] || fail "a private driven chat asked the utility model for a title: $(cat "$PV_LOG")"
printf 'our confidential merger with Acme closes Friday\n' | pv chat --private \
    >"$PV_HOME/repl.out" 2>"$PV_HOME/repl.err" \
    || fail "private-title: the chat on a pipe failed: $(cat "$PV_HOME/repl.err")"
[ ! -e "$PV_LOG" ] || fail "a private chat on a pipe asked the utility model for a title: $(cat "$PV_LOG")"
[ -z "$(ls "$PV_HOME/sessions" 2>/dev/null)" ] || fail "private-title: a private chat wrote a session"
printf '%s\n' '{"type":"user","text":"an ordinary question about lunch"}' \
    | pv chat --no-recall --output-format stream-json --input-format stream-json \
        >"$PV_HOME/written.jsonl" 2>"$PV_HOME/written.err" \
    || fail "private-title: the written chat failed: $(cat "$PV_HOME/written.err")"
grep -q "an ordinary question about lunch" "$PV_LOG" 2>/dev/null \
    || fail "private-title: the logging utility heard no title for a written chat -- the absence above proves nothing"
grep -q "Acme" "$PV_LOG" && fail "private-title: a private chat's words reached the utility model"

# --- the session names its chat; an empty one is never saved (M28) -------------
# The user's run, as the probe that found it drove it (2026-10-22): two scripted
# mocks, `claude` the default. A session closed before any turn -- with nothing,
# or with every door but a turn -- adds no row to `chats list` and leaves no
# index; one with a turn names the one row that appears, and its answers say
# the model that gave them after a `set`; resumed, it names that chat, and
# closed with no turn leaves its file as it was. Its own home, so the rows are
# only these.
ID_HOME="$WORK_DIR/chat-id"
mkdir -p "$ID_HOME"
printf '{"turns":[{"text":"claude says: {{last_user}}"}]}' >"$ID_HOME/claude.json"
printf '{"turns":[{"text":"gemma says: {{last_user}}"}]}' >"$ID_HOME/gemma.json"
printf 'The launch code is 7731.\n' >"$ID_HOME/notes.md"
APOGEE_HOME="$ID_HOME" "$APOGEE_BIN" config init >/dev/null || fail "chat-id: config init"
APOGEE_HOME="$ID_HOME" "$APOGEE_BIN" config add-backend claude --type mock \
    --model-path "$ID_HOME/claude.json" >/dev/null || fail "chat-id: add claude"
APOGEE_HOME="$ID_HOME" "$APOGEE_BIN" config add-backend Gemma4-26B-Instruct --type mock \
    --model-path "$ID_HOME/gemma.json" >/dev/null || fail "chat-id: add gemma"
APOGEE_HOME="$ID_HOME" "$APOGEE_BIN" config set-default claude >/dev/null || fail "chat-id: set-default"
python3 - "$APOGEE_BIN" "$ID_HOME" <<'PY' || fail "the session's chat id, or an empty session's save"
import hashlib, json, os, subprocess, sys

binary, home = sys.argv[1], sys.argv[2]
env = {**os.environ, "APOGEE_HOME": home}

def chats():
    got = subprocess.run([binary, "chats", "list", "--output-format", "json"], env=env,
                         capture_output=True, text=True, stdin=subprocess.DEVNULL, timeout=60)
    assert got.returncode == 0, got.stderr
    return [(row["id"], row["backend"], row["turns"]) for row in json.loads(got.stdout)["data"]]

def drive(name, lines, *flags):
    text = "".join(json.dumps(line) + "\n" for line in lines)
    got = subprocess.run([binary, "chat", "--no-recall", "--output-format", "stream-json",
                          "--input-format", "stream-json", *flags], env=env, input=text,
                         capture_output=True, text=True, timeout=60)
    assert got.returncode == 0, got.stderr
    with open(os.path.join(home, name + ".jsonl"), "w") as kept:  # validated below
        kept.write(got.stdout)
    events = [json.loads(line) for line in got.stdout.splitlines() if line.strip()]
    assert events and events[0]["type"] == "session", events
    return events

def index_files():
    folder = os.path.join(home, "attachments")
    return sorted(os.listdir(folder)) if os.path.isdir(folder) else []

# Closed before any turn: named, and nothing written.
assert chats() == [], chats()
empty = drive("empty", [])
assert isinstance(empty[0].get("chat_id"), str) and empty[0]["chat_id"], empty[0]
assert chats() == [], f"a session closed before any turn added a row: {chats()}"
doors = drive("doors", [{"type": "set", "model": "Gemma4-26B-Instruct"},
                        {"type": "attach", "path": os.path.join(home, "notes.md")},
                        {"type": "compact"}])
assert "switched to Gemma4-26B-Instruct -- from the next turn" in \
    [e["text"] for e in doors if e["type"] == "notice"], doors
assert chats() == [], f"a session of doors and no turn added a row: {chats()}"
assert index_files() == [], f"an unwritten chat left its index: {index_files()}"

# A turn: the row that appears is exactly the chat the session named.
talked = drive("talked", [{"type": "user", "text": "Hello, how are you today?"},
                          {"type": "set", "model": "Gemma4-26B-Instruct"},
                          {"type": "user", "text": "Can apogee create videos?"}])
chat_id = talked[0]["chat_id"]
assert talked[0]["model"] == "claude", talked[0]
assert [(e["turn"], e["model"]) for e in talked if e["type"] == "result"] == \
    [(1, "claude"), (2, "Gemma4-26B-Instruct")], talked
assert chats() == [(chat_id, "Gemma4-26B-Instruct", 2)], (chat_id, chats())
assert chat_id != empty[0]["chat_id"], "two sessions named one chat"

# Resumed: the chat it resumed, and its file as it was when no turn ran.
path = os.path.join(home, "sessions", chat_id + ".json")
held = hashlib.sha256(open(path, "rb").read()).hexdigest(), os.stat(path).st_mtime_ns
resumed = drive("resumed", [], "--resume", chat_id)
assert resumed[0]["chat_id"] == chat_id and resumed[0]["next_turn"] == 3, resumed[0]
assert (hashlib.sha256(open(path, "rb").read()).hexdigest(), os.stat(path).st_mtime_ns) == held, \
    "a resumed chat closed with no turn was rewritten"
again = drive("again", [{"type": "user", "text": "one more"}], "--resume", chat_id)
assert again[0]["chat_id"] == chat_id, again[0]
assert chats() == [(chat_id, "Gemma4-26B-Instruct", 3)], chats()
print(f"chat-id: named {chat_id}, listed after a turn; empty sessions wrote nothing - OK")
PY

# --- the session record tells the truth (55b) ----------------------------------
# The audit's probes, on the real binary in a home of their own: a `compact`
# with nothing to fold, and one whose utility fails, each said as it is and
# counted nowhere; a legacy chat resumed over the protocol saying what it could
# not carry over as `notice`s, not on stderr; a file with answers and no count
# resuming on the next number; and a live chat that keeps the name `chats
# title` gave it while it ran, and stays deleted after `chats delete`, saying
# so.
RT_HOME="$WORK_DIR/record-truth"
mkdir -p "$RT_HOME/bin"
printf '{"turns":[{"text":"Answer {{last_user}}"}]}' >"$RT_HOME/chat.json"
printf '#!/bin/sh\necho "the utility fell over" >&2\nexit 1\n' >"$RT_HOME/bin/claude"
chmod +x "$RT_HOME/bin/claude"
rt() { APOGEE_HOME="$RT_HOME" "$APOGEE_BIN" "$@" </dev/null >/dev/null; }
rt config init || fail "record-truth: config init"
rt config add-backend mock --type mock --model-path "$RT_HOME/chat.json" || fail "record-truth: add mock"
rt config set-default mock || fail "record-truth: set-default"
rt config add-backend broken --type claude-cli || fail "record-truth: add broken"
rt config set-backend broken --binary "$RT_HOME/bin/claude" || fail "record-truth: point broken"
python3 - "$APOGEE_BIN" "$RT_HOME" <<'PY' || fail "the session record does not tell the truth"
import json, os, subprocess, sys

binary, home = sys.argv[1], sys.argv[2]
env = {**os.environ, "APOGEE_HOME": home}

def run(*args):
    got = subprocess.run([binary, *args], env=env, capture_output=True, text=True,
                         stdin=subprocess.DEVNULL, timeout=60)
    return got

def drive(name, lines, *flags):
    text = "".join(json.dumps(line) + "\n" for line in lines)
    got = subprocess.run([binary, "chat", "--no-recall", "--output-format", "stream-json",
                          "--input-format", "stream-json", *flags], env=env, input=text,
                         capture_output=True, text=True, timeout=60)
    assert got.returncode == 0, got.stderr
    with open(os.path.join(home, name + ".jsonl"), "w") as kept:  # validated below
        kept.write(got.stdout)
    return [json.loads(line) for line in got.stdout.splitlines() if line.strip()], got.stderr

def notices(events):
    return [e["text"] for e in events if e["type"] == "notice"]

def chats():
    got = run("chats", "list", "--output-format", "json")
    assert got.returncode == 0, got.stderr
    return [(row["id"], row["name"], row["turns"]) for row in json.loads(got.stdout)["data"]]

def write(chat_id, record):
    os.makedirs(os.path.join(home, "sessions"), exist_ok=True)
    with open(os.path.join(home, "sessions", chat_id + ".json"), "w") as f:
        json.dump(record, f)

# Finding 1: nothing to fold, said and not counted -- the probe's empty compact.
events, _ = drive("rt-empty", [{"type": "compact"}])
assert notices(events) == ["nothing to compact -- no exchange to fold yet"], events

# Finding 3: a legacy chat's degradations reach the driver as notices.
messages = [{"role": "user", "content": "q1"}, {"role": "assistant", "content": "a1"},
            {"role": "user", "content": "q2"}, {"role": "assistant", "content": "a2"}]
write("legacy-chat", {"chat_id": "legacy-chat", "backend": "gone", "messages": messages})
events, err = drive("rt-legacy", [], "--resume", "legacy-chat")
assert [e["type"] for e in events] == ["session", "notice", "notice"], events
assert notices(events) == [
    "resume: this chat predates session metadata; resuming best-effort with current defaults",
    "resume: backend 'gone' is no longer configured; using the default instead"], events
assert "[resume]" not in err, err

# Finding 4: two answers and no count -- the resumed session numbers on from 2.
write("nocount", {"schema_version": 2, "chat_id": "nocount", "backend": "mock",
                  "messages": messages})
shown = json.loads(run("chats", "show", "nocount", "--output-format", "json").stdout)
assert [t["turn"] for t in shown["turns"]] == [1, 2], shown["turns"]
assert shown["warnings"] == [], shown
events, _ = drive("rt-nocount", [{"type": "user", "text": "q3"}], "--resume", "nocount")
assert events[0]["next_turn"] == 3, events[0]
assert [e["turn"] for e in events if e["type"] == "result"] == [3], events
shown = json.loads(run("chats", "show", "nocount", "--output-format", "json").stdout)
assert [t["turn"] for t in shown["turns"]] == [1, 2, 3], shown["turns"]

# Finding 5: a live chat keeps an outside rename, and an outside delete.
child = subprocess.Popen([binary, "chat", "--no-recall", "--output-format", "stream-json",
                          "--input-format", "stream-json"], env=env, stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, bufsize=1)
seen = []

def turn(text):
    child.stdin.write(json.dumps({"type": "user", "text": text}) + "\n")
    child.stdin.flush()
    while True:
        line = child.stdout.readline()
        assert line, "the live chat ended early"
        seen.append(line)
        event = json.loads(line)
        if event["type"] in ("result", "error"):
            return event

opened = json.loads(child.stdout.readline())
seen.append(json.dumps(opened) + "\n")
chat_id = opened["chat_id"]
assert turn("one")["type"] == "result"
renamed = run("chats", "title", chat_id, "Renamed By GUI")
assert renamed.returncode == 0, renamed.stderr
assert turn("two")["type"] == "result"
live = [row for row in chats() if row[0] == chat_id]
assert live == [(chat_id, "Renamed By GUI", 2)], live
deleted = run("chats", "delete", chat_id)
assert deleted.stdout.strip() == "deleted " + chat_id, deleted
assert turn("three")["type"] == "result"
child.stdin.close()
seen.extend(child.stdout.readlines())
assert child.wait(timeout=60) == 0, child.stderr.read()
said = [json.loads(l)["text"] for l in seen if json.loads(l)["type"] == "notice"]
assert said == ["chat " + chat_id + " was deleted while this session ran -- it stays deleted, "
                "and nothing more of it is saved"], said
assert all(row[0] != chat_id for row in chats()), chats()
with open(os.path.join(home, "rt-live.jsonl"), "w") as kept:
    kept.writelines(seen)

# Finding 1 again: a utility that fails is said by name, and counted nowhere.
assert run("config", "set-default-utility", "broken").returncode == 0
events, _ = drive("rt-failed", [{"type": "compact"}], "--resume", "nocount")
said = notices(events)
assert len(said) == 1 and said[0].startswith("compaction by broken failed: ") and \
    said[0].endswith(" -- the history is unchanged"), said
info = run("chats", "info", "nocount").stdout
assert "compacted:" not in info, info
refused = run("chats", "compact", "nocount", "--yes")
assert refused.returncode == 1 and "compaction by broken failed: " in refused.stderr, refused
print("record-truth: compaction, numbering, resume notices and outside edits told truly - OK")
PY

# --- a deleted chat stays deleted, a private one leaves nothing (57a) ---------
# The second bug sweep's probes, on the real binary in a home of its own and a
# temporary directory of its own: a private driven chat -- a driver's hello,
# an attachment, a turn, a compact -- leaves the data directory byte for byte
# as it was, `logs/` included; ended by SIGTERM or SIGHUP while idle, it
# still ends by that signal, and leaves no `apogee-private-*` folder behind;
# one a killed run left is swept by the next private chat -- even one whose
# pid that chat was given again, which never indexes into it; and a resumed
# chat deleted from outside, then closed with no turn, says so once.
GN_HOME="$WORK_DIR/gone"
GN_TMP="$WORK_DIR/gone-tmp"
mkdir -p "$GN_HOME" "$GN_TMP"
gn() { APOGEE_HOME="$GN_HOME" TMPDIR="$GN_TMP" "$APOGEE_BIN" "$@" </dev/null >/dev/null; }
gn config init || fail "gone: config init"
gn config add-backend mock --type mock --model mock-1 || fail "gone: add mock"
gn config set-default mock || fail "gone: set-default"
printf 'zebra private attachment text\n' >"$WORK_DIR/gone-note.txt"
python3 - "$APOGEE_BIN" "$GN_HOME" "$GN_TMP" "$WORK_DIR/gone-note.txt" <<'PY' || fail "a deleted or private chat left something behind"
import json, os, signal, subprocess, sys, time

binary, home, tmp, note = sys.argv[1:5]
env = {**os.environ, "APOGEE_HOME": home, "TMPDIR": tmp}

def tree():
    out = {}
    for root, dirs, files in os.walk(home):
        for name in dirs:
            out[os.path.relpath(os.path.join(root, name), home) + "/"] = b""
        for name in files:
            path = os.path.join(root, name)
            with open(path, "rb") as f:
                out[os.path.relpath(path, home)] = f.read()
    return out

def private_left():
    return sorted(n for n in os.listdir(tmp) if n.startswith("apogee-private-"))

opening = [{"type": "hello", "client": {"name": "e2e-host"}},
           {"type": "attach", "path": note},
           {"type": "user", "text": "first question"}]

def private_child():
    child = subprocess.Popen([binary, "chat", "--private", "--output-format", "stream-json",
                              "--input-format", "stream-json"], env=env, stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, bufsize=1)
    for line in opening:
        child.stdin.write(json.dumps(line) + "\n")
    child.stdin.flush()
    while True:
        line = child.stdout.readline()
        assert line, "the private chat ended early: " + child.stderr.read()
        if json.loads(line)["type"] in ("result", "error"):
            return child

before = tree()

# A clean close: nothing under the home, and its folder gone with it.
text = "".join(json.dumps(l) + "\n" for l in opening + [{"type": "compact"}])
got = subprocess.run([binary, "chat", "--private", "--output-format", "stream-json",
                      "--input-format", "stream-json"], env=env, input=text,
                     capture_output=True, text=True, timeout=60)
assert got.returncode == 0, got.stderr
assert private_left() == [], private_left()

# Ended by a signal while idle: still ended by that signal, and no folder.
for signum in (signal.SIGTERM, signal.SIGHUP):
    child = private_child()
    held = private_left()
    assert len(held) == 1, held  # the run's own, while it runs
    child.send_signal(signum)
    code = child.wait(timeout=30)
    assert code == -signum, (signum, code, child.stderr.read())
    assert private_left() == [], (signum, private_left())

# A run killed outright leaves its folder; the next private chat's opening
# sweeps it -- and leaves a live run's.
child = private_child()
held = private_left()
assert len(held) == 1, held
child.kill()
child.wait(timeout=30)
assert private_left() == held, private_left()
live = private_child()
got = subprocess.run([binary, "chat", "--private", "--output-format", "stream-json",
                      "--input-format", "stream-json"], env=env, input=text,
                     capture_output=True, text=True, timeout=60)
assert got.returncode == 0, got.stderr
left = private_left()
assert held[0] not in left, left
assert len(left) == 1 and left[0].startswith("apogee-private-%d-" % live.pid), left
live.stdin.close()
assert live.wait(timeout=30) == 0, live.stderr.read()
assert private_left() == [], private_left()

# A run killed outright whose id the next private chat is given again -- a
# reused pid, a restarted container's apogee always the same one: the folder
# it left under that id is no folder of the new run's. Its attachment's text
# never reaches the new conversation, and the folder does not survive it.
# `sh` renames the leftover for its own pid, then `exec`s the chat in place.
work = os.path.dirname(note)
secret = os.path.join(work, "gone-secret.txt")
with open(secret, "w") as f:
    f.write("the zebra launch code is 7741 confidential\n")
other = os.path.join(work, "gone-other.txt")
with open(other, "w") as f:
    f.write("a short note about giraffes and their long necks\n")
driven = ["chat", "--private", "--output-format", "stream-json", "--input-format", "stream-json"]
child = subprocess.Popen([binary] + driven, env=env, stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, bufsize=1)
for line in ({"type": "attach", "path": secret}, {"type": "user", "text": "keep this safe"}):
    child.stdin.write(json.dumps(line) + "\n")
child.stdin.flush()
while json.loads(child.stdout.readline() or '{"type": "error"}')["type"] not in ("result", "error"):
    pass
held = private_left()
assert len(held) == 1, held
child.kill()
child.wait(timeout=30)
assert private_left() == held, private_left()
asked = [{"type": "attach", "path": other},
         {"type": "user", "text": "what is the zebra launch code?"}]
got = subprocess.run(["sh", "-c", 't=$1 p=$2 b=$3; shift 3; '
                      'mv "$t/$p" "$t/apogee-private-$$-1" && exec "$b" "$@"', "sh",
                      tmp, held[0], binary] + driven, env=env,
                     input="".join(json.dumps(l) + "\n" for l in asked),
                     capture_output=True, text=True, timeout=60)
assert got.returncode == 0, got.stderr
reused = [json.loads(l) for l in got.stdout.splitlines() if l.strip()]
named = [s["file"] for e in reused if e["type"] == "sources" for s in e["sources"]]
assert all("gone-secret" not in n for n in named), named
assert "gone-secret" not in got.stdout and "7741" not in got.stdout, got.stdout
assert private_left() == [], private_left()

# Nothing of any of them under the home: no log line naming one, no hello.
after = tree()
assert after == before, sorted(set(after) ^ set(before))

# A resumed chat deleted from outside, closed with no turn: said once.
lines = [{"type": "user", "text": "first q"}, {"type": "user", "text": "second q"}]
got = subprocess.run([binary, "chat", "--no-recall", "--output-format", "stream-json",
                      "--input-format", "stream-json"], env=env,
                     input="".join(json.dumps(l) + "\n" for l in lines),
                     capture_output=True, text=True, timeout=60)
assert got.returncode == 0, got.stderr
chat_id = json.loads(got.stdout.splitlines()[0])["chat_id"]
child = subprocess.Popen([binary, "chat", "--output-format", "stream-json", "--input-format",
                          "stream-json", "--resume", chat_id], env=env, stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, bufsize=1)
opened = json.loads(child.stdout.readline())
assert opened["type"] == "session" and opened["chat_id"] == chat_id, opened
deleted = subprocess.run([binary, "chats", "delete", chat_id], env=env, capture_output=True,
                         text=True, stdin=subprocess.DEVNULL, timeout=60)
assert deleted.stdout.strip() == "deleted " + chat_id, deleted
child.stdin.close()
rest = [json.loads(l) for l in child.stdout.read().splitlines() if l.strip()]
assert child.wait(timeout=60) == 0, child.stderr.read()
said = [e["text"] for e in rest if e["type"] == "notice"]
assert said == ["chat " + chat_id + " was deleted while this session ran -- it stays deleted, "
                "and nothing more of it is saved"], rest
assert not os.path.exists(os.path.join(home, "sessions", chat_id + ".json"))
print("gone: deleted and private chats leave nothing, on every close - OK")
PY

# --- a session's MCP warnings reach the driver (56b) ---------------------------
# A driver reads no stderr: a configured server that will not start was said
# there alone. Now what the startup said that stays -- each warning -- is one
# `notice` after the `session` event, the dying server's last line in it once;
# stderr keeps it as before, and the server that connected is no notice.
MW_HOME="$WORK_DIR/mcp-warnings"
mkdir -p "$MW_HOME"
printf '#!/bin/sh\necho "fatal: no upstream" >&2\nexit 1\n' >"$MW_HOME/dying.sh"
chmod +x "$MW_HOME/dying.sh"
mw() { APOGEE_HOME="$MW_HOME" "$APOGEE_BIN" "$@" </dev/null >/dev/null; }
mw config init || fail "mcp-warnings: config init"
mw config add-backend mock --type mock --model mock-1 || fail "mcp-warnings: add mock"
mw config set-default mock || fail "mcp-warnings: set-default"
mw mcp create dying --command "$MW_HOME/dying.sh" || fail "mcp-warnings: register dying"
mw mcp create self --command "$APOGEE_BIN" --args mcp serve || fail "mcp-warnings: register self"
APOGEE_HOME="$MW_HOME" "$APOGEE_BIN" chat --tools --private --output-format stream-json \
    --input-format stream-json </dev/null >"$MW_HOME/out.jsonl" 2>"$MW_HOME/err.txt" \
    || fail "mcp-warnings: the driven chat failed: $(cat "$MW_HOME/err.txt")"
python3 - "$MW_HOME/out.jsonl" "$MW_HOME/err.txt" <<'PY' || fail "a session's MCP warnings did not reach the driver"
import json, sys
events = [json.loads(line) for line in open(sys.argv[1]) if line.strip()]
assert events and events[0]["type"] == "session", events
said = [e["text"] for e in events if e["type"] == "notice" and "[mcp]" in e["text"]]
assert len(said) == 1, said
assert said[0].startswith("[mcp] warning: dying: connect failed (skipped)"), said
assert said[0].count("fatal: no upstream") == 1, said
assert "connected" not in said[0] and "connecting" not in said[0], said
assert "[mcp] warning: dying: connect failed" in open(sys.argv[2]).read()
print("mcp-warnings: one notice after the session, stderr as before - OK")
PY

# --- turn ids and cancel (28f) ------------------------------------------------
# A driver over real pipes: every turn-scoped event carries its turn; a cancel
# with nothing in flight changes nothing; a cancel mid-stream ends the turn in
# one `result` with finish_reason `cancelled` and the text streamed so far, the
# half-turn kept out of the saved session, and the same child answers the next
# turn; a cancel while a question waits fails the turn the same way, and one
# at a permission question ends the call it asks about `failed` first (56a); a
# question a `delegate` sub-session asks names a call the stream reported, and
# that call ends; a turn that fails ends in `error`, never a `result` carrying
# the user's own words.
TC_DIR="$WORK_DIR/turns"
mkdir -p "$TC_DIR/bin"
printf '%s' '{"turns":[{"text":"aaaaaaaabbbbbbbbccccccccddddddddeeeeeeeeffffffffgggggggghhhhhhhh","delay_ms":150},{"text":"echo: {{last_user}}"}]}' >"$TC_DIR/slow.json"
printf '%s' '{"turns":[{"text":"","tool_calls":[{"name":"ask_user","arguments":{"questions":[{"header":"Pick","question":"Which?","multi_select":false,"options":[{"label":"a","description":"A"},{"label":"b","description":"B"}]}]}}]},{"text":"echo: {{last_user}}"}]}' >"$TC_DIR/asks.json"
printf '%s' '{"turns":[{"text":"","tool_calls":[{"id":"w-1","name":"write_file","arguments":{"path":"never.txt","content":"x"}}]},{"text":"echo: {{last_user}}"}]}' >"$TC_DIR/writes.json"
printf '%s' '{"turns":[{"text":"","tool_calls":[{"id":"d-1","name":"delegate","arguments":{"brief":"write sub.txt"}}]},{"text":"","tool_calls":[{"id":"s-1","name":"write_file","arguments":{"path":"sub.txt","content":"x"}}]},{"text":"sub: {{last_tool_result}}"},{"text":"delegated: {{last_tool_result}}"}]}' >"$TC_DIR/delegates.json"
printf '#!/bin/sh\necho x\n' >"$TC_DIR/bin/ollama"
chmod +x "$TC_DIR/bin/ollama"
"$APOGEE_BIN" config add-backend slow --type mock --model-path "$TC_DIR/slow.json" >/dev/null || fail "add slow"
"$APOGEE_BIN" config add-backend asks --type mock --model-path "$TC_DIR/asks.json" >/dev/null || fail "add asks"
"$APOGEE_BIN" config add-backend writes --type mock --model-path "$TC_DIR/writes.json" >/dev/null || fail "add writes"
"$APOGEE_BIN" config add-backend delegates --type mock --model-path "$TC_DIR/delegates.json" >/dev/null || fail "add delegates"
"$APOGEE_BIN" config add-backend down --type ollama-cli --model m >/dev/null || fail "add down"
python3 - "$APOGEE_HOME/config/config.json" "$TC_DIR/bin/ollama" <<'PY' || fail "could not point the down backend"
import json, sys
path, binary = sys.argv[1], sys.argv[2]
text = open(path).read()
entry = '    "down": {\n      "type": "ollama-cli",\n'
assert entry in text
text = text.replace(entry, entry + '      "host": "127.0.0.1:9",\n      "binary": ' + json.dumps(binary) + ',\n', 1)
open(path, "w").write(text)
PY
python3 - "$APOGEE_BIN" "$APOGEE_HOME" "$TC_DIR" <<'PY' || fail "turn ids and cancel"
import glob, json, os, subprocess, sys, time

binary, home, folder = sys.argv[1], sys.argv[2], sys.argv[3]

class Child:
    def __init__(self, *args, cwd=None):
        self.p = subprocess.Popen([binary, "chat", "--no-recall", "--output-format", "stream-json",
                                   "--input-format", "stream-json", *args],
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, text=True, bufsize=1, cwd=cwd)
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

# A cancel while a permission question waits (56a): the call it asks about
# ends `failed` before the turn's cancelled result -- no call is left
# `pending` -- and nothing is written.
child = Child("-m", "writes", "--tools", cwd=folder)
child.read()
child.send({"type": "user", "text": "write it"})
asked = child.until({"question", "result", "error"})
if asked[-1]["type"] != "question" or asked[-1].get("tool_call_id") != "w-1":
    fail(f"no permission question for the write: {asked}")
child.send({"type": "cancel"})
ended = child.until({"result", "error"})
if ended[-1]["type"] != "result" or ended[-1].get("finish_reason") != "cancelled":
    fail(f"the turn asked about a write did not end cancelled: {ended}")
states = [e["state"] for e in asked + ended if e["type"] == "tool_call" and e.get("id") == "w-1"]
if states != ["pending", "failed"]:
    fail(f"a call cancelled at its question did not end failed: {states}")
if os.path.exists(os.path.join(folder, "never.txt")):
    fail("a write cancelled at its question ran")
child.close()

# A gated call inside a `delegate` sub-session (56a's review): its question
# names a call the stream reported -- every `tool_call_id` names an id with
# `tool_call` events before it -- and that call ends, `failed` when denied.
child = Child("-m", "delegates", "--tools", cwd=folder)
child.read()
child.send({"type": "user", "text": "delegate it"})
asked = child.until({"question", "result", "error"})
if asked[-1]["type"] != "question" or asked[-1].get("tool_call_id") != "s-1":
    fail(f"no permission question for the delegated write: {asked}")
child.send({"type": "answer", "text": "no"})
ended = child.until({"result", "error"})
if ended[-1]["type"] != "result" or not ended[-1]["text"].startswith("delegated: "):
    fail(f"the delegating turn did not end in its answer: {ended}")
stream = asked + ended
for at, event in enumerate(stream):
    named = event.get("tool_call_id") if event["type"] == "question" else None
    if named and not any(e["type"] == "tool_call" and e.get("id") == named
                         for e in stream[:at]):
        fail(f"a question names a call the stream never reported: {event}")
def moves(call):
    return [e["state"] for e in stream if e["type"] == "tool_call" and e.get("id") == call]
if moves("s-1") != ["pending", "failed"]:
    fail(f"the delegated write denied did not end failed: {moves('s-1')}")
if moves("d-1") != ["pending", "running", "done"]:
    fail(f"the delegation's own call moved {moves('d-1')}")
if os.path.exists(os.path.join(folder, "sub.txt")):
    fail("a delegated write denied ran")
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
print("turns: ids on every turn event, cancel mid-stream, mid-question and at a permission question, a delegated call's question named, a failure an error - OK")
PY

# --- a default that could not start says why (M26) -----------------------------
# The app's own launch, in the user's run (2026-10-22): a chat driven with no
# -m, its default a claude-cli backend that could not start beside a local one
# that did, under the PATH a Dock-launched app has. The turn ends in an `error`
# carrying the words the command line prints -- configured, not started, the
# doctor's reason and remedy, the backends that started -- and so does a
# one-shot `complete`. A sandbox of its own through --custom, and a `binary`
# naming a path that does not exist, so no host's own `claude` can answer.
SKIP_DIR="$WORK_DIR/skipped-default"
mkdir -p "$SKIP_DIR/config"
printf '{"models":{"default":"claude"},"backends":{"claude":{"type":"claude-cli","binary":"%s"},"Local-Mock":{"type":"mock"}}}\n' \
    "$SKIP_DIR/nonexistent/claude" >"$SKIP_DIR/config/config.json"
printf '%s\n' '{"type":"user","text":"hi"}' \
    | env -i HOME="$SKIP_DIR" PATH=/usr/bin:/bin:/usr/sbin:/sbin \
        "$APOGEE_BIN" --custom "$SKIP_DIR/config/config.json" chat \
        --output-format stream-json --input-format stream-json \
        >"$SKIP_DIR/chat.jsonl" 2>"$SKIP_DIR/chat-err.txt" \
    || fail "a driven chat over a default that could not start failed: $(cat "$SKIP_DIR/chat-err.txt")"
env -i HOME="$SKIP_DIR" PATH=/usr/bin:/bin:/usr/sbin:/sbin \
    "$APOGEE_BIN" --custom "$SKIP_DIR/config/config.json" complete --output-format stream-json "hi" \
    </dev/null >"$SKIP_DIR/complete.jsonl" 2>"$SKIP_DIR/complete-err.txt" \
    && fail "complete answered over a default that could not start"
python3 - "$SKIP_DIR" <<'PY' || fail "a default that could not start: the error events are wrong"
import json, os, sys
root = sys.argv[1]
expected = ("the default backend 'claude' is configured but could not be started: '"
            + os.path.join(root, "nonexistent", "claude") + "' was not found on PATH. "
            "Install the Claude CLI and log in, or set 'binary' on this backend to its full path "
            "(started backends: Local-Mock)")
for name in ("chat.jsonl", "complete.jsonl"):
    events = [json.loads(line) for line in open(os.path.join(root, name)) if line.strip()]
    assert [e["message"] for e in events if e["type"] == "error"] == [expected], (name, events)
    assert not [e for e in events if e["type"] == "result"], (name, events)
PY

# --- a vendor CLI's failure says why (57b) ------------------------------------
# The second bug sweep's probes, on the real binary: fake vendor binaries on a
# path of their own, each saying what the real CLI says when it fails.
#  - Gemini 0.46.0 ends every fatal error in a result whose `error` object says
#    why: the `error` event carries its message, never `reported an error:
#    error`.
#  - A warning before a successful result is a notice, and the answer stands.
#  - A first turn that fails signed out leaves no session behind: signed in,
#    the same chat answers, on a fresh session told the whole history, never
#    `--resume` of one the CLI never recorded.
#  - Claude Code's expired login (is_error under subtype "success") reads in
#    its own words on the conversation's turn, and as a title is no title.
# Its own home, so no other section's config moves.
VF_HOME="$WORK_DIR/vendor-failures"
VF_STATE="$VF_HOME/state"
mkdir -p "$VF_HOME/bin" "$VF_STATE"
cat >"$VF_HOME/bin/gemini" <<EOF
#!/bin/sh
{ for arg in "\$@"; do printf '%s\n' "\$arg"; done; printf -- '--\n'; } >>"$VF_STATE/gemini-argv.log"
echo "[STARTUP] Phase 'cleanup_ops' was started but never ended." >&2
init='{"type":"init","timestamp":"2026-10-31T20:16:45.000Z","session_id":"s","model":"auto"}'
answer() {
    printf '%s\n' "\$init" \\
        "{\"type\":\"message\",\"timestamp\":\"2026-10-31T20:16:46.000Z\",\"role\":\"assistant\",\"content\":\"\$1\",\"delta\":true}" \\
        '{"type":"result","timestamp":"2026-10-31T20:16:46.100Z","status":"success","stats":{"input_tokens":3,"output_tokens":4}}'
}
case "\$(cat "$VF_STATE/gemini-mode")" in
quota)
    printf '%s\n' "\$init" \\
        '{"type":"result","timestamp":"2026-10-31T20:16:46.000Z","status":"error","error":{"type":"QuotaError","message":"You have exhausted your daily quota on this model."},"stats":{"total_tokens":0,"input_tokens":0,"output_tokens":0}}'
    exit 1 ;;
warning)
    printf '%s\n' '{"type":"error","timestamp":"2026-10-31T20:16:45.500Z","severity":"warning","message":"Loop detected, stopping execution"}'
    answer "Here is the answer."
    exit 0 ;;
login)
    resume=""; sid=""; prev=""
    for arg in "\$@"; do
        case "\$prev" in --resume) resume="\$arg" ;; --session-id) sid="\$arg" ;; esac
        prev="\$arg"
    done
    if [ ! -e "$VF_STATE/signed-in" ]; then
        printf '%s\n' "\$init" \\
            '{"type":"result","timestamp":"2026-10-31T20:16:46.000Z","status":"error","error":{"type":"FatalAuthenticationError","message":"Please sign in to continue."},"stats":{}}'
        exit 41
    fi
    if [ -n "\$resume" ] && ! grep -qx "\$resume" "$VF_STATE/gemini-sessions" 2>/dev/null; then
        echo "Error resuming session: No session found" >&2
        exit 1
    fi
    [ -n "\$sid" ] && printf '%s\n' "\$sid" >>"$VF_STATE/gemini-sessions"
    answer "Welcome back."
    exit 0 ;;
esac
EOF
cat >"$VF_HOME/bin/claude" <<EOF
#!/bin/sh
expired='{"type":"result","subtype":"success","is_error":true,"session_id":"expired-sess","result":"Failed to authenticate: OAuth session expired and could not be refreshed","num_turns":1}'
case " \$* " in
*" --input-format "*)
    # The conversation's child: hears the question, then answers as Claude
    # Code 2.1.289 does with an expired login.
    read -r question
    printf '%s\n' '{"type":"system","subtype":"init","session_id":"expired-sess","model":"claude-sonnet-5"}' \\
        '{"type":"assistant","message":{"model":"<synthetic>","role":"assistant","content":[{"type":"text","text":"Failed to authenticate: OAuth session expired and could not be refreshed"}]},"error":"authentication_failed","session_id":"expired-sess"}' \\
        "\$expired" ;;
*)
    # A one-shot -- the title: logged, so its absence proves nothing unasked.
    for last in "\$@"; do :; done
    printf '%s\n' "\$last" >>"$VF_STATE/claude-one-shots.log"
    printf '%s\n' "\$expired" ;;
esac
EOF
chmod +x "$VF_HOME/bin/gemini" "$VF_HOME/bin/claude"
vf() { APOGEE_HOME="$VF_HOME" "$APOGEE_BIN" "$@"; }
vf config init >/dev/null || fail "vendor-failures: config init"
vf config add-backend mock --type mock --model mock-1 >/dev/null || fail "vendor-failures: add mock"
vf config set-default mock >/dev/null || fail "vendor-failures: set-default"
vf config add-backend gem --type gemini-cli >/dev/null || fail "vendor-failures: add gem"
vf config set-backend gem --binary "$VF_HOME/bin/gemini" >/dev/null || fail "vendor-failures: point gem"
vf config add-backend claude --type claude-cli >/dev/null || fail "vendor-failures: add claude"
vf config set-backend claude --binary "$VF_HOME/bin/claude" >/dev/null || fail "vendor-failures: point claude"
vf config set-default-utility claude >/dev/null || fail "vendor-failures: set-default-utility"
python3 - "$APOGEE_BIN" "$VF_HOME" "$VF_STATE" <<'PY' || fail "a vendor CLI's failure does not say why"
import json, os, subprocess, sys

binary, home, state = sys.argv[1], sys.argv[2], sys.argv[3]
env = {**os.environ, "APOGEE_HOME": home}

def mode(name):
    with open(os.path.join(state, "gemini-mode"), "w") as out:
        out.write(name)

def events(text):
    return [json.loads(line) for line in text.splitlines() if line.strip()]

def complete(model, prompt):
    got = subprocess.run([binary, "complete", "-m", model, "--output-format", "stream-json", prompt],
                         env=env, capture_output=True, text=True, stdin=subprocess.DEVNULL,
                         timeout=60)
    return got.returncode, events(got.stdout)

def errors(stream):
    return [e["message"] for e in stream if e["type"] == "error"]

# Gemini's result-shaped failure, in its own words.
mode("quota")
code, stream = complete("gem", "hi")
assert code != 0, stream
said = errors(stream)
assert len(said) == 1, stream
assert "the Gemini CLI reported an error: You have exhausted your daily quota on this model." \
    in said[0], said
assert "reported an error: error" not in said[0], said
assert not [e for e in stream if e["type"] == "result"], stream

# A warning is a notice, and the answer stands.
mode("warning")
code, stream = complete("gem", "hi")
assert code == 0, stream
notices = [e["text"] for e in stream if e["type"] == "notice"]
assert "the Gemini CLI warned: Loop detected, stopping execution" in notices, stream
results = [e for e in stream if e["type"] == "result"]
assert len(results) == 1 and results[0]["text"] == "Here is the answer.", stream
assert not errors(stream), stream

# A first turn that fails signed out; signed in, the same chat answers.
mode("login")
open(os.path.join(state, "gemini-argv.log"), "w").close()
child = subprocess.Popen([binary, "chat", "-m", "gem", "--no-recall", "--output-format",
                          "stream-json", "--input-format", "stream-json"], env=env,
                         stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.DEVNULL, text=True)

def ask(text):
    child.stdin.write(json.dumps({"type": "user", "text": text}) + "\n")
    child.stdin.flush()
    seen = []
    while True:
        line = child.stdout.readline()
        assert line, f"the chat ended mid-turn: {seen}"
        event = json.loads(line)
        seen.append(event)
        if event["type"] in ("result", "error"):
            return seen

first = ask("my name is Zed")
assert first[-1]["type"] == "error", first
assert "Please sign in to continue." in first[-1]["message"], first
open(os.path.join(state, "signed-in"), "w").close()
second = ask("what is my name?")
assert second[-1]["type"] == "result" and second[-1]["text"] == "Welcome back.", second
child.stdin.close()
assert child.wait(timeout=60) == 0
runs = open(os.path.join(state, "gemini-argv.log")).read().split("--\n")
runs = [run.splitlines() for run in runs if run.strip()]
assert len(runs) == 2, runs
assert "--session-id" in runs[1] and "--resume" not in runs[1], runs[1]
asked = runs[1][runs[1].index("--prompt") + 1:]
assert any("my name is Zed" in line for line in asked), runs[1]

# Claude Code's expired login on the conversation's turn: its own words.
code, stream = complete("claude", "hi")
assert code != 0, stream
said = errors(stream)
assert len(said) == 1, stream
assert ("the Claude CLI reported an error: Failed to authenticate: OAuth session expired and "
        "could not be refreshed") in said[0], said
assert "reported success" not in said[0], said

# ...and as a title: no title, and the sentence nowhere in the chats list.
got = subprocess.run([binary, "chat", "--no-recall", "--output-format", "stream-json",
                      "--input-format", "stream-json"], env=env, capture_output=True, text=True,
                     input=json.dumps({"type": "user", "text": "an ordinary question"}) + "\n",
                     timeout=60)
assert got.returncode == 0, got.stderr
assert [e for e in events(got.stdout) if e["type"] == "result"], got.stdout
assert os.path.getsize(os.path.join(state, "claude-one-shots.log")) > 0, \
    "the utility was never asked for a title -- the absence below proves nothing"
for flags in ([], ["--output-format", "json"]):
    listed = subprocess.run([binary, "chats", "list", *flags], env=env, capture_output=True,
                            text=True, stdin=subprocess.DEVNULL, timeout=60)
    assert listed.returncode == 0, listed.stderr
    assert "Failed to authenticate" not in listed.stdout, listed.stdout
rows = json.loads(subprocess.run([binary, "chats", "list", "--output-format", "json"], env=env,
                                 capture_output=True, text=True, stdin=subprocess.DEVNULL,
                                 timeout=60).stdout)["data"]
# Untitled, each keeps the name an untitled chat has: its id.
assert len(rows) == 2 and all(row["name"] == row["id"] for row in rows), rows
print("vendor failures: Gemini's quota and login in its own words, a warning a notice, a failed "
      "first turn fresh again, Claude's expired login said and never a title - OK")
PY

# --- a direct-API failure fails its turn, never the session (57c) --------------
# The second bug sweep's probe, on the real binary: an `anthropic` entry with
# a fake key, every request sent through a proxy at a closed loopback port, so
# nothing reaches a provider and the connection fails as a dropped network
# does. The driven chat's turn ends in an `error` in curl's words, the session
# lives -- a `set` moves it to a mock and the next line is served -- and the
# child exits 0; a one-shot `complete` exits 2 on its `error` event. Before
# 57c the transport's error reached `main`: exit 1, no `error`, the next line
# never read. Its own home, so no other section's config moves.
DA_HOME="$WORK_DIR/direct-api"
mkdir -p "$DA_HOME"
printf '{"turns":[{"text":"echo: {{last_user}}"}]}' >"$DA_HOME/echo.json"
da() {
    env APOGEE_HOME="$DA_HOME" ANTHROPIC_API_KEY=sk-ant-not-a-real-key \
        https_proxy=http://127.0.0.1:9 HTTPS_PROXY=http://127.0.0.1:9 \
        all_proxy=http://127.0.0.1:9 ALL_PROXY=http://127.0.0.1:9 no_proxy= NO_PROXY= \
        "$APOGEE_BIN" "$@"
}
da config init >/dev/null || fail "direct-api: config init"
da config add-backend claude --type anthropic --model claude-sonnet-5 >/dev/null \
    || fail "direct-api: add claude"
da config add-backend echo --type mock --model-path "$DA_HOME/echo.json" >/dev/null \
    || fail "direct-api: add echo"
printf '%s\n' '{"type":"user","text":"one"}' '{"type":"set","model":"echo"}' \
    '{"type":"user","text":"two"}' \
    | da chat -m claude --no-recall --output-format stream-json --input-format stream-json \
        >"$DA_HOME/chat.jsonl" 2>"$DA_HOME/chat-err.txt" \
    || fail "direct-api: the driven chat did not exit 0 past a failed turn: $(cat "$DA_HOME/chat-err.txt")"
da complete -m claude --output-format stream-json "hi" </dev/null \
    >"$DA_HOME/complete.jsonl" 2>"$DA_HOME/complete-err.txt"
DA_CODE=$?
[ "$DA_CODE" -eq 2 ] || fail "direct-api: complete exited $DA_CODE, not 2: $(cat "$DA_HOME/complete-err.txt")"
python3 - "$DA_HOME" <<'PY' || fail "direct-api: a failed turn is not one error and a served next line"
import json, os, sys
root = sys.argv[1]
def events(name):
    return [json.loads(line) for line in open(os.path.join(root, name)) if line.strip()]
said = "claude: request failed: Couldn't connect to server"
chat = events("chat.jsonl")
ends = [e for e in chat if e["type"] in ("result", "error")]
assert [e["type"] for e in ends] == ["error", "result"], chat
assert ends[0]["message"] == said and ends[0].get("turn") == 1, ends[0]
assert ends[1]["text"] == "echo: two" and ends[1].get("turn") == 2, ends[1]
once = events("complete.jsonl")
assert [e["message"] for e in once if e["type"] == "error"] == [said], once
assert not [e for e in once if e["type"] == "result"], once
for name in ("chat-err.txt", "complete-err.txt"):
    text = open(os.path.join(root, name)).read()
    assert "sk-ant-not-a-real-key" not in text, (name, text)
for name in ("chat.jsonl", "complete.jsonl"):
    assert "sk-ant-not-a-real-key" not in open(os.path.join(root, name)).read(), name
print("direct-api: a connection that failed is one error, the next line served, complete exit 2 - OK")
PY

# --- a pull stops on Ctrl-C, its stream ending on the cancel (55c) -------------
# `models pull` from an Ollama store whose blob is a FIFO fed a MiB at a time
# -- the scripted slow source -- and a real SIGINT to the binary mid-copy, as
# a person's Ctrl-C or the GUI's stop sends it: exit 130, the stream's last
# event the cancel, every line whole, and nothing left in the store. Before
# 55c the copy ignored the signal or died of it, its stream cut on a `bytes`.
PULL_DIR="$WORK_DIR/pull"
PULL_LIB="$PULL_DIR/ollama/manifests/registry.ollama.ai/library/slow"
mkdir -p "$PULL_DIR/ollama/blobs" "$PULL_LIB"
PULL_SIZE=$((32 * 1024 * 1024))
PULL_DIGEST="$(python3 -c 'import hashlib, sys; print(hashlib.sha256(bytes(int(sys.argv[1]))).hexdigest())' "$PULL_SIZE")"
PULL_BLOB="$PULL_DIR/ollama/blobs/sha256-$PULL_DIGEST"
mkfifo "$PULL_BLOB" || fail "mkfifo"
printf '{"schemaVersion":2,"layers":[{"mediaType":"application/vnd.ollama.image.model","digest":"sha256:%s","size":%d}]}' \
    "$PULL_DIGEST" "$PULL_SIZE" >"$PULL_LIB/latest"
python3 - "$PULL_BLOB" "$PULL_SIZE" 2>/dev/null <<'PY' &
import sys, time
with open(sys.argv[1], "wb") as out:
    for _ in range(int(sys.argv[2]) >> 20):
        out.write(bytes(1 << 20))
        out.flush()
        time.sleep(0.1)
PY
PULL_WRITER=$!
OLLAMA_MODELS="$PULL_DIR/ollama" "$APOGEE_BIN" models pull slow:latest --output-format stream-json \
    </dev/null >"$PULL_DIR/pull.jsonl" 2>"$PULL_DIR/pull.err" &
PULL_CHILD=$!
tries=0
until grep -q '"done":[1-9]' "$PULL_DIR/pull.jsonl" 2>/dev/null; do
    tries=$((tries + 1))
    [ "$tries" -lt 200 ] || { kill -9 "$PULL_CHILD" "$PULL_WRITER" 2>/dev/null; fail "the copy never started: $(cat "$PULL_DIR/pull.err")"; }
    sleep 0.05
done
kill -INT "$PULL_CHILD"
wait "$PULL_CHILD"
PULL_STATUS=$?
kill "$PULL_WRITER" 2>/dev/null
wait "$PULL_WRITER" 2>/dev/null
[ "$PULL_STATUS" -eq 130 ] || fail "a Ctrl-C'd pull exited $PULL_STATUS: $(cat "$PULL_DIR/pull.err")"
grep -q "apogee models: cancelled -- nothing was written" "$PULL_DIR/pull.err" \
    || fail "the cancel was not said on stderr: $(cat "$PULL_DIR/pull.err")"
python3 - "$PULL_DIR/pull.jsonl" <<'PY' || fail "the Ctrl-C'd pull's stream: $(cat "$PULL_DIR/pull.jsonl")"
import json, sys
events = [json.loads(line) for line in open(sys.argv[1])]
types = [e["type"] for e in events]
assert types[0] == "job" and events[0]["verb"] == "models pull", types
assert events[-1] == {"type": "error", "message": "cancelled -- nothing was written"}, events[-1]
assert types.count("error") == 1 and "document" not in types, types
assert any(e["type"] == "bytes" and e["done"] > 0 for e in events), "no bytes moved before the Ctrl-C"
PY
[ ! -e "$APOGEE_HOME/models/slow-latest" ] || fail "a Ctrl-C'd pull left $(find "$APOGEE_HOME/models/slow-latest")"

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

echo "machine mode: stdout is pure JSONL, one child served 2 turns, a Ctrl-C'd pull ended on its" \
    "cancel, bytes that are not UTF-8 ended nothing, no listening socket - OK"
