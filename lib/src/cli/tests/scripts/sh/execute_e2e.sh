#!/usr/bin/env bash
# Execute mode (27s) on the real binary, against scripted mock members that
# echo the brief they were sent, with no input after the command is typed:
#
#   * suite-first: `execute` with no suite named and none the default refused
#     naming `config set-default-suite`, with no suite configured at all
#     naming `config add-suite`, `--suite off` refused -- each with nothing on
#     stdout and no conversation saved; with a default suite, that suite;
#   * bare input answered by the suite's root, exactly as `chat --suite` --
#     stdout and the saved conversation byte for byte the same;
#   * `/play <symphony> <input>`: each stage one member's call, stage two
#     given stage one's answer, the output on stdout and in the conversation
#     as one exchange beside the bare turns; a play that cannot run said and
#     nothing kept; `/symphonies` the `symphonies list` table; `/suite off`
#     refused, `/suite <name>` moving the plays to its members;
#   * the session resumed with its plays intact, by `execute -c` and by
#     `chat --resume` -- one session format -- and compacted as ordinary
#     history;
#   * machine mode: a `user` line holding `/play` carried as an ordinary
#     turn -- a `tool_status` per stage, the answer, the `result` -- every
#     event of a type the protocol already has; a play that cannot run an
#     `error`;
#   * completion: the command, its flags, the suites, and chat's table
#     without `/play`.
#
# POSIX only, like the other .sh checks here (CLAUDE.md -> Platforms).

set -uo pipefail

APOGEE_BIN="${1:?usage: execute_e2e.sh <apogee-binary> <work-dir>}"
WORK_DIR="${2:?usage: execute_e2e.sh <apogee-binary> <work-dir>}"

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR/scripts"
export APOGEE_HOME="$WORK_DIR/home"
export HOME="$WORK_DIR"
trap 'rm -rf "$WORK_DIR"' EXIT
cd "$WORK_DIR" || exit 1

fail() { echo "execute: $*" >&2; exit 1; }

printf '{"turns": [{"text": "ROOT<{{last_user}}>"}]}\n' > "$WORK_DIR/scripts/root.json"
printf '{"turns": [{"text": "HELPER<{{last_user}}>"}]}\n' > "$WORK_DIR/scripts/helper.json"

mkdir -p "$APOGEE_HOME/config"
CONFIG="$APOGEE_HOME/config/config.yaml"
write_config() {  # write_config <models: extra lines>
    {
        echo "backends:"
        for name in root helper; do
            echo "  $name:"
            echo "    type: mock"
            echo "    model_path: $WORK_DIR/scripts/$name.json"
        done
        echo "models:"
        echo "  default: root"
        printf '%s' "$1"
        echo "memory:"
        echo "  recall: false"
        echo "suites:"
        echo "  duo:"
        echo "    members:"
        echo "      chat: root"
        echo "      utility: helper"
        echo "  fast:"
        echo "    members:"
        echo "      chat: helper"
        echo "      utility: helper"
        echo "symphonies:"
        echo "  echo2:"
        echo "    description: Two stages."
        echo "    stages:"
        echo "      - {name: first, role: utility, prompt: 'One: {{input}}'}"
        echo "      - {name: second, role: chat, prompt: 'Two: {{first}}'}"
    } > "$CONFIG"
}
write_config ""
"$APOGEE_BIN" check --fix </dev/null >"$WORK_DIR/fix.txt" 2>&1 || fail "check --fix: $(cat "$WORK_DIR/fix.txt")"

count_sessions() { ls "$APOGEE_HOME/sessions" 2>/dev/null | grep -c '\.json$'; }
newest_session() { ls -t "$APOGEE_HOME"/sessions/*.json | head -n 1; }
# The saved conversation as `role: text` lines.
transcript() {
    python3 -c '
import json, sys
for m in json.load(open(sys.argv[1]))["messages"]:
    text = "".join(p.get("text", "") for p in m["content"]) if isinstance(m["content"], list) else m["content"]
    print(m["role"] + ": " + text)
' "$1"
}

# --- suite-first ---------------------------------------------------------------
OUT=$(printf 'hi\n' | "$APOGEE_BIN" execute 2>"$WORK_DIR/err.txt"); code=$?
[ "$code" = 1 ] || fail "no suite: exit $code, want 1"
[ -z "$OUT" ] || fail "no suite: stdout carried: $OUT"
grep -q 'execute opens a session with a suite, and none is named or the default' "$WORK_DIR/err.txt" &&
    grep -q 'configured: duo, fast' "$WORK_DIR/err.txt" &&
    grep -q 'apogee config set-default-suite <name>' "$WORK_DIR/err.txt" ||
    fail "no suite: no remediation: $(cat "$WORK_DIR/err.txt")"
[ "$(count_sessions)" = 0 ] || fail "a refused execute saved a conversation"

mkdir -p "$WORK_DIR/bare/config"
printf 'backends:\n  root:\n    type: mock\nmodels:\n  default: root\n' > "$WORK_DIR/bare/config/config.yaml"
OUT=$("$APOGEE_BIN" --config "$WORK_DIR/bare/config/config.yaml" execute </dev/null 2>"$WORK_DIR/err.txt"); code=$?
[ "$code" = 1 ] || fail "no suites: exit $code, want 1"
grep -q 'none is configured -- add one: apogee config add-suite <name> --chat <backend>' "$WORK_DIR/err.txt" ||
    fail "no suites: no add-suite remediation: $(cat "$WORK_DIR/err.txt")"

printf 'hi\n' | "$APOGEE_BIN" execute --suite off >/dev/null 2>"$WORK_DIR/err.txt" && fail "--suite off ran"
grep -q -- "--suite off names no suite, and execute opens a session with one" "$WORK_DIR/err.txt" ||
    fail "--suite off: $(cat "$WORK_DIR/err.txt")"
[ "$(count_sessions)" = 0 ] || fail "a refused execute saved a conversation"

# --- bare input is chat's turn ---------------------------------------------------
CHAT=$(printf 'hello\nagain\n' | "$APOGEE_BIN" chat --suite duo 2>/dev/null) || fail "chat --suite duo"
CHAT_FILE=$(newest_session)
EXEC=$(printf 'hello\nagain\n' | "$APOGEE_BIN" execute --suite duo 2>/dev/null) || fail "execute --suite duo"
EXEC_FILE=$(newest_session)
[ "$CHAT_FILE" != "$EXEC_FILE" ] || fail "execute saved no conversation of its own"
[ "$EXEC" = "$CHAT" ] || fail "execute answered other than chat: '$EXEC' vs '$CHAT'"
[ "$(transcript "$EXEC_FILE")" = "$(transcript "$CHAT_FILE")" ] ||
    fail "execute saved other than chat: $(transcript "$EXEC_FILE")"
rm -f "$CHAT_FILE" "$EXEC_FILE"

# --- /play ---------------------------------------------------------------------
OUT=$(printf '/play echo2 hello\nwhat now\n/play\n/play nope x\n/symphonies\n/help\n' |
    "$APOGEE_BIN" execute --suite duo 2>"$WORK_DIR/err.txt") || fail "execute /play: $(cat "$WORK_DIR/err.txt")"
EXPECTED="ROOT<Two: HELPER<One: hello>>
ROOT<what now>"
[ "$OUT" = "$EXPECTED" ] || fail "/play: stdout '$OUT', want '$EXPECTED'"
SESSION=$(newest_session)
EXPECTED="user: /play echo2 hello
assistant: ROOT<Two: HELPER<One: hello>>
user: what now
assistant: ROOT<what now>"
[ "$(transcript "$SESSION")" = "$EXPECTED" ] || fail "/play: saved $(transcript "$SESSION")"
grep -q '"suite": "duo"' "$SESSION" || fail "/play: the session saved no suite"
grep -q '\[error\] /play takes a symphony, then its input' "$WORK_DIR/err.txt" || fail "/play alone: $(cat "$WORK_DIR/err.txt")"
grep -q "\[error\] no symphony named 'nope'" "$WORK_DIR/err.txt" || fail "/play nope: $(cat "$WORK_DIR/err.txt")"
LIST=$("$APOGEE_BIN" symphonies list </dev/null 2>/dev/null) || fail "symphonies list"
while IFS= read -r row; do
    grep -qF "  $row" "$WORK_DIR/err.txt" || fail "/symphonies: missing '$row'"
done <<< "$LIST"
grep -q '^  /play <symphony> \[input\]' "$WORK_DIR/err.txt" || fail "/help: no /play row"
grep -q '^  /symphonies ' "$WORK_DIR/err.txt" || fail "/help: no /symphonies row"
grep -q '^  /suite \[name\] ' "$WORK_DIR/err.txt" || fail "/help: execute's /suite offers off"

# /suite off refused; /suite fast moves the plays to fast's members.
OUT=$(printf '/suite off\n/suite fast\n/play echo2 x\n' |
    "$APOGEE_BIN" execute --suite duo 2>"$WORK_DIR/err.txt") || fail "execute /suite"
[ "$OUT" = "HELPER<Two: HELPER<One: x>>" ] || fail "/suite fast: the play ran on '$OUT'"
grep -q "\[error\] execute runs under a suite" "$WORK_DIR/err.txt" || fail "/suite off: $(cat "$WORK_DIR/err.txt")"

# Chat's table has no /play: the line is an unknown command there.
printf '/play echo2 hello\n' | "$APOGEE_BIN" chat --suite duo >"$WORK_DIR/out.txt" 2>"$WORK_DIR/err.txt" || fail "chat /play"
[ ! -s "$WORK_DIR/out.txt" ] || fail "chat played: $(cat "$WORK_DIR/out.txt")"
grep -q "unknown command '/play'" "$WORK_DIR/err.txt" || fail "chat /play: $(cat "$WORK_DIR/err.txt")"

# --- the default suite, resume, one session format, compaction ---------------------
# One conversation from here on, so `-c` has exactly one most recent.
rm -f "$APOGEE_HOME"/sessions/*.json
write_config "  default_suite: duo
"
OUT=$(printf '/play echo2 one\n' | "$APOGEE_BIN" execute 2>/dev/null) || fail "execute under the default suite"
[ "$OUT" = "ROOT<Two: HELPER<One: one>>" ] || fail "default suite: '$OUT'"
SESSION=$(newest_session)
ID=$(basename "$SESSION" .json)
OUT=$(printf '/play echo2 two\nthree\n' | "$APOGEE_BIN" execute -c 2>/dev/null) || fail "execute -c"
[ "$OUT" = "ROOT<Two: HELPER<One: two>>
ROOT<three>" ] || fail "execute -c: '$OUT'"
OUT=$(printf 'four\n' | "$APOGEE_BIN" chat --resume "$ID" 2>/dev/null) || fail "chat --resume an execute session"
[ "$OUT" = "ROOT<four>" ] || fail "chat --resume: '$OUT'"
EXPECTED="user: /play echo2 one
assistant: ROOT<Two: HELPER<One: one>>
user: /play echo2 two
assistant: ROOT<Two: HELPER<One: two>>
user: three
assistant: ROOT<three>
user: four
assistant: ROOT<four>"
[ "$(transcript "$SESSION")" = "$EXPECTED" ] || fail "resume: saved $(transcript "$SESSION")"
printf '/compact\n' | "$APOGEE_BIN" execute -c >/dev/null 2>"$WORK_DIR/err.txt" || fail "execute /compact"
grep -q 'history compacted' "$WORK_DIR/err.txt" || fail "/compact: $(cat "$WORK_DIR/err.txt")"
grep -q '"compactions": 1' "$SESSION" || fail "/compact: no compaction saved"
transcript "$SESSION" | grep -q '^user: /play echo2 one$' && fail "/compact: the play was not compacted"

# --- machine mode ------------------------------------------------------------------
printf '{"type":"user","text":"/play echo2 hello"}\n{"type":"user","text":"next"}\n{"type":"user","text":"/play nope x"}\n' |
    "$APOGEE_BIN" execute --output-format stream-json >"$WORK_DIR/events.jsonl" 2>"$WORK_DIR/err.txt" ||
    fail "machine mode: $(cat "$WORK_DIR/err.txt")"
python3 - "$WORK_DIR/events.jsonl" <<'PY' || fail "machine mode: $(cat "$WORK_DIR/events.jsonl")"
import json, sys
events = [json.loads(line) for line in open(sys.argv[1])]
types = [e["type"] for e in events]
known = {"session", "thinking", "thinking_delta", "memory", "tool_status", "notice",
         "answer_start", "answer_delta", "answer_end", "result", "question", "error"}
assert set(types) <= known, f"a type the protocol does not have: {set(types) - known}"
assert types[:7] == ["session", "tool_status", "tool_status", "answer_start", "answer_delta",
                     "answer_end", "result"], types
assert events[1]["text"].startswith("stage 1/2 first — asking utility (helper): One: hello"), events[1]
assert events[2]["text"].startswith("stage 2/2 second — asking chat (root): Two: HELPER<"), events[2]
assert events[6]["text"] == "ROOT<Two: HELPER<One: hello>>", events[6]
assert types[-1] == "error" and "no symphony named 'nope'" in events[-1]["message"], events[-1]
assert types.count("result") == 2, types
PY

# --- completion ------------------------------------------------------------------------
"$APOGEE_BIN" __complete "" </dev/null | grep -qx execute || fail "completion: no execute command"
"$APOGEE_BIN" __complete execute -- </dev/null | grep -qx -- --suite || fail "completion: no --suite"
SUITES=$("$APOGEE_BIN" __complete execute --suite "" </dev/null)
[ "$SUITES" = "duo
fast" ] || fail "completion: --suite offered '$SUITES'"
"$APOGEE_BIN" __complete execute --resume "" </dev/null | grep -qx "$ID" || fail "completion: --resume lacks $ID"

echo "execute: OK"
