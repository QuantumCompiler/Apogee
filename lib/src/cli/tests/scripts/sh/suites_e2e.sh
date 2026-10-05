#!/usr/bin/env bash
# Suites (27d) on the real binary: the config unit written through the one
# editor, a chat run under one, the bundle switched mid-chat, and both
# surviving a resume -- and, since 27e, the set's footprint stated, a suite
# that cannot fit refused until --force, and --warm silent on a pipe; since
# 27f, the root consulting a member on exactly the brief, a metered member
# refused at config time, and machine mode carrying the consult as any tool
# call -- with the run's own records as the evidence:
#
#   * who titled the chat: the utility role does, so a title from the helper's
#     script proves `--suite research` resolved utility to `helper` (and the
#     control, a chat with no suite, is titled by its own backend);
#   * which backend answered: each mock answers with its own name;
#   * what the chat saved: its `suite`, and nothing at all with none.
#
# The resolution itself is table-tested in tests/business/harness/roles_test.cpp;
# this proves the plumbing from a command line to that table and back to disk.
# POSIX only, like the other .sh checks here (CLAUDE.md -> Platforms).

set -uo pipefail

APOGEE_BIN="${1:?usage: suites_e2e.sh <apogee-binary> <work-dir>}"
WORK_DIR="${2:?usage: suites_e2e.sh <apogee-binary> <work-dir>}"

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR/scripts"
export APOGEE_HOME="$WORK_DIR/home"
export HOME="$WORK_DIR"
trap 'rm -rf "$WORK_DIR"' EXIT

fail() { echo "suites: $*" >&2; exit 1; }

# One script per backend, each answering with its own name -- so every answer
# and every title says which backend produced it.
for name in root helper fastroot embedder; do
    upper=$(echo "$name" | tr '[:lower:]' '[:upper:]')
    printf '{"turns": [{"text": "%s-SAYS"}]}\n' "$upper" > "$WORK_DIR/scripts/$name.json"
done

"$APOGEE_BIN" config init >/dev/null || fail "config init"
for name in root helper fastroot embedder; do
    "$APOGEE_BIN" config add-backend "$name" --type mock \
        --model-path "$WORK_DIR/scripts/$name.json" >/dev/null || fail "add-backend $name"
done
"$APOGEE_BIN" config set-default root >/dev/null || fail "set-default"
CONFIG="$APOGEE_HOME/config/config.yaml"
BEFORE=$(cat "$CONFIG")

# --- the config unit, through the one editor ---------------------------------
"$APOGEE_BIN" config add-suite research --chat root --utility helper --embedding embedder \
    >/dev/null || fail "add-suite research"
"$APOGEE_BIN" config add-suite fast --chat fastroot >/dev/null || fail "add-suite fast"
# Exactly the old bytes and the new block: nothing else in the file moved.
AFTER=$(cat "$CONFIG")
EXPECTED="$BEFORE

suites:
  research:
    members:
      chat: root
      embedding: embedder
      utility: helper

  fast:
    members:
      chat: fastroot"
[ "$AFTER" = "$EXPECTED" ] || fail "add-suite wrote more than its block:
$AFTER"
"$APOGEE_BIN" check >"$WORK_DIR/check.txt" 2>&1
grep -q "suite: research" "$WORK_DIR/check.txt" || fail "check has no row for the suite"

# --- models status: the suite rung, where it answered -----------------------
STATUS=$("$APOGEE_BIN" models status --suite research -q 2>&1) || fail "models status --suite: $STATUS"
echo "$STATUS" | grep -qx "suite: research" || fail "status does not name the suite: $STATUS"
echo "$STATUS" | grep -qx "utility: helper   (via suite research)" \
    || fail "status does not answer utility through the suite rung: $STATUS"
PLAIN=$("$APOGEE_BIN" models status -q 2>&1) || fail "models status: $PLAIN"
echo "$PLAIN" | grep -q "suite" && fail "status names a suite with none active: $PLAIN"
echo "$PLAIN" | grep -qx "utility: (unset -- the chat's own backend)" \
    || fail "status without a suite changed: $PLAIN"

# --- completion: the suites, from the live config ---------------------------
OFFERED=$("$APOGEE_BIN" __complete chat --suite "" 2>/dev/null)
echo "$OFFERED" | grep -q "research" || fail "--suite does not complete the suites: $OFFERED"
echo "$OFFERED" | grep -q "fast" || fail "--suite does not complete every suite: $OFFERED"

session_of() {  # the one session file whose JSON carries $1
    grep -l "$1" "$APOGEE_HOME"/sessions/*.json 2>/dev/null | head -n 1
}

# --- the control: no suite, the chat titles itself ---------------------------
echo "control question" | "$APOGEE_BIN" chat >"$WORK_DIR/control.txt" 2>&1 \
    || fail "chat with no suite failed: $(cat "$WORK_DIR/control.txt")"
grep -q "ROOT-SAYS" "$WORK_DIR/control.txt" || fail "the control chat did not answer from root"
CONTROL=$(session_of "control question")
[ -n "$CONTROL" ] || fail "the control chat was not saved"
grep -q '"suite"' "$CONTROL" && fail "a chat with no suite saved one: $(cat "$CONTROL")"
grep -q '"title": "ROOT-SAYS"' "$CONTROL" || fail "the control chat was not titled by root: $(cat "$CONTROL")"

# --- chat --suite research: utility resolves to helper ----------------------
echo "research question" | "$APOGEE_BIN" chat --suite research >"$WORK_DIR/research.txt" 2>&1 \
    || fail "chat --suite research failed: $(cat "$WORK_DIR/research.txt")"
grep -q "ROOT-SAYS" "$WORK_DIR/research.txt" || fail "the suite's chat member did not answer"
RESEARCH=$(session_of "research question")
[ -n "$RESEARCH" ] || fail "the suite chat was not saved"
grep -q '"suite": "research"' "$RESEARCH" || fail "the suite was not saved: $(cat "$RESEARCH")"
grep -q '"title": "HELPER-SAYS"' "$RESEARCH" \
    || fail "utility did not resolve to helper -- the title came from elsewhere: $(cat "$RESEARCH")"
"$APOGEE_BIN" chat --suite nope </dev/null >"$WORK_DIR/nope.txt" 2>&1 \
    && fail "an unknown suite was accepted"
grep -q "no suite named 'nope' (configured: fast, research)" "$WORK_DIR/nope.txt" \
    || fail "an unknown suite was not refused by name: $(cat "$WORK_DIR/nope.txt")"

# --- a member naming nothing: said by check, refused at use ----------------
# The resolver returns the name as it returns any; a chat must not let the
# router fall back to models.default and answer from a model nobody chose.
"$APOGEE_BIN" config add-backend gone --type mock >/dev/null || fail "add-backend gone"
"$APOGEE_BIN" config add-suite broken --utility gone >/dev/null || fail "add-suite broken"
"$APOGEE_BIN" config delete-backend gone >/dev/null || fail "delete-backend gone"
"$APOGEE_BIN" check >"$WORK_DIR/check-broken.txt" 2>&1
grep -q "suite: broken.*utility names a backend that is not configured: 'gone'" \
    "$WORK_DIR/check-broken.txt" || fail "check does not fail the broken suite: $(cat "$WORK_DIR/check-broken.txt")"
echo "question" | "$APOGEE_BIN" chat --suite broken >"$WORK_DIR/broken.txt" 2>&1 \
    && fail "a chat ran under a suite whose member names nothing"
grep -q "no backend named 'gone'" "$WORK_DIR/broken.txt" \
    || fail "the broken suite was not refused in the existing words: $(cat "$WORK_DIR/broken.txt")"
grep -q "SAYS" "$WORK_DIR/broken.txt" && fail "a model answered under the broken suite"
"$APOGEE_BIN" config delete-suite broken >/dev/null || fail "delete-suite broken"

# --- /suite mid-chat: the bundle switches, and off restores the pointers ----
printf 'switch question\n/suite fast\nsecond question\n/suite\n' \
    | "$APOGEE_BIN" chat --suite research >"$WORK_DIR/switch.txt" 2>&1 \
    || fail "the switching chat failed: $(cat "$WORK_DIR/switch.txt")"
grep -q "suite fast -- chat fastroot" "$WORK_DIR/switch.txt" || fail "/suite fast was not said: $(cat "$WORK_DIR/switch.txt")"
grep -q "switched to fastroot" "$WORK_DIR/switch.txt" || fail "/suite fast did not move the chat"
grep -q "FASTROOT-SAYS" "$WORK_DIR/switch.txt" || fail "the next turn did not run on fastroot"
grep -q "suite: fast -- chat fastroot" "$WORK_DIR/switch.txt" || fail "/suite does not show the suite"
SWITCHED=$(session_of "switch question")
grep -q '"suite": "fast"' "$SWITCHED" || fail "the switch was not saved: $(cat "$SWITCHED")"
ID=$(basename "$SWITCHED" .json)

# Survives resume: the chat comes back under fast.
printf '/suite\n' | "$APOGEE_BIN" chat --resume "$ID" >"$WORK_DIR/resumed.txt" 2>&1 \
    || fail "resume failed: $(cat "$WORK_DIR/resumed.txt")"
grep -q "suite: fast -- chat fastroot" "$WORK_DIR/resumed.txt" \
    || fail "the suite did not survive resume: $(cat "$WORK_DIR/resumed.txt")"

# /suite off: the global pointers, the conversation where it is -- and kept
# off on resume even once the config has a default suite.
printf '/suite off\nthird question\n' | "$APOGEE_BIN" chat --resume "$ID" >"$WORK_DIR/off.txt" 2>&1 \
    || fail "/suite off failed: $(cat "$WORK_DIR/off.txt")"
grep -q "suite off -- the roles follow the global pointers; the conversation stays on fastroot" \
    "$WORK_DIR/off.txt" || fail "/suite off was not said: $(cat "$WORK_DIR/off.txt")"
grep -q "FASTROOT-SAYS" "$WORK_DIR/off.txt" || fail "/suite off moved the conversation"
grep -q '"suite": ""' "$SWITCHED" || fail "/suite off was not saved: $(cat "$SWITCHED")"
"$APOGEE_BIN" config set-default-suite research >/dev/null || fail "set-default-suite"
printf '/suite\n' | "$APOGEE_BIN" chat --resume "$ID" >"$WORK_DIR/still-off.txt" 2>&1 \
    || fail "resume after off failed"
grep -q "suite: none" "$WORK_DIR/still-off.txt" \
    || fail "an off chat resumed under the default suite: $(cat "$WORK_DIR/still-off.txt")"

# A new chat takes the default suite, says so, and saves it.
echo "default question" | "$APOGEE_BIN" chat >"$WORK_DIR/default.txt" 2>&1 \
    || fail "chat under the default suite failed"
DEFAULTED=$(session_of "default question")
grep -q '"suite": "research"' "$DEFAULTED" || fail "the default suite was not used: $(cat "$DEFAULTED")"
grep -q '"title": "HELPER-SAYS"' "$DEFAULTED" || fail "the default suite's utility did not title"
# The default cannot be deleted from under the config.
"$APOGEE_BIN" config delete-suite research >/dev/null 2>&1 && fail "the default suite was deleted"

# --- residency (27e): the set stated, refused when it cannot fit, warmed ----
# Mock members hold nothing on this machine: said, and the chat went ahead.
grep -q "suite research: no member holds memory on this machine" "$WORK_DIR/research.txt" \
    || fail "selecting a suite did not state its footprint: $(cat "$WORK_DIR/research.txt")"
PRICED=$("$APOGEE_BIN" models status --suite research -q 2>&1) || fail "models status: $PRICED"
echo "$PRICED" | grep -qx "footprint: suite research" || fail "status has no footprint: $PRICED"
echo "$PRICED" | grep -qx "  helper (utility): nothing held here" \
    || fail "status does not price the members: $PRICED"
"$APOGEE_BIN" models status -q --suite off 2>&1 | grep -q "footprint" \
    && fail "status priced a suite with none active"

# Warming: refused with no suite to warm; on a pipe it paints nothing.
"$APOGEE_BIN" chat --suite off --warm </dev/null >"$WORK_DIR/warm-none.txt" 2>&1 \
    && fail "--warm ran with no suite"
grep -q "warm loads a suite's members, and this chat runs under none" "$WORK_DIR/warm-none.txt" \
    || fail "--warm with no suite was not refused: $(cat "$WORK_DIR/warm-none.txt")"
echo "warm question" | "$APOGEE_BIN" chat --suite research --warm >"$WORK_DIR/warm.txt" 2>&1 \
    || fail "chat --warm failed: $(cat "$WORK_DIR/warm.txt")"
grep -q "ROOT-SAYS" "$WORK_DIR/warm.txt" || fail "the warmed chat did not answer"
LC_ALL=C grep -q "$(printf '\033')" "$WORK_DIR/warm.txt" && fail "--warm painted on a pipe"

# A local member whose record claims a pebibyte: no machine fits it. With
# llama.cpp built in, the machine's budget is known and the suite is refused
# with its numbers until --force; without it the budget is unknown, said so,
# and the chat runs. Its role (extraction) is never asked in a chat, so no
# model is loaded either way.
python3 - "$WORK_DIR/vast.gguf" <<'PY'
import struct, sys
def text(value):
    raw = value.encode()
    return struct.pack("<Q", len(raw)) + raw
out = b"GGUF" + struct.pack("<IQQ", 3, 1, 6)
out += text("general.architecture") + struct.pack("<I", 8) + text("llama")
for key, value in [("llama.block_count", 16), ("llama.context_length", 131072),
                   ("llama.embedding_length", 2048), ("llama.attention.head_count", 32),
                   ("llama.attention.head_count_kv", 8)]:
    out += text(key) + struct.pack("<II", 4, value)
out += text("token_embd.weight") + struct.pack("<IQQIQ", 2, 16, 16, 0, 0)
open(sys.argv[1], "wb").write(out)
PY
printf '{"file": "vast.gguf", "file_size": 1125899906842624}\n' >"$WORK_DIR/vast.json"
"$APOGEE_BIN" config add-backend vast --type llamacpp --model-path "$WORK_DIR/vast.gguf" \
    >/dev/null || fail "add-backend vast"
"$APOGEE_BIN" config add-suite vast --chat root --extraction vast >/dev/null || fail "add-suite vast"
VAST="vast 1073742368 + 1024 margin"
if echo "vast question" | "$APOGEE_BIN" chat --suite vast >"$WORK_DIR/vast.txt" 2>&1; then
    grep -q "suite vast needs 1073743392 MiB: $VAST -- this machine's budget is not known here" \
        "$WORK_DIR/vast.txt" || fail "an unknown budget was not said: $(cat "$WORK_DIR/vast.txt")"
else
    grep -q "suite vast needs 1073743392 MiB of this machine's [0-9]* MiB: $VAST -- it does not fit; --force runs it anyway" \
        "$WORK_DIR/vast.txt" || fail "the refusal does not name the numbers: $(cat "$WORK_DIR/vast.txt")"
    grep -q "SAYS" "$WORK_DIR/vast.txt" && fail "a model answered under a refused suite"
    echo "vast question" | "$APOGEE_BIN" chat --suite vast --force >"$WORK_DIR/forced.txt" 2>&1 \
        || fail "--force did not run the suite: $(cat "$WORK_DIR/forced.txt")"
    grep -q "$VAST -- over budget, run anyway (--force)" "$WORK_DIR/forced.txt" \
        || fail "the forced run does not say so: $(cat "$WORK_DIR/forced.txt")"
    grep -q "ROOT-SAYS" "$WORK_DIR/forced.txt" || fail "the forced chat did not answer"
fi

# --- consult (27f): the root delegates a brief to a member -------------------
# The asking root calls consult once, then relays what came back; the oracle
# answers with the brief it was sent and the system prompt it saw, so its
# answer is the wire's own record: exactly the question, no system prompt.
cat >"$WORK_DIR/scripts/asker.json" <<'JSON'
{"turns": [{"text": "", "tool_calls": [{"name": "consult", "arguments": {"member": "utility", "question": "What is the codeword?"}}]},
           {"text": "ROOT-RELAYS {{last_tool_result}}"}]}
JSON
printf '{"turns": [{"text": "CODEWORD-MARMALADE heard [{{last_user}}] system [{{system}}]"}]}\n' \
    >"$WORK_DIR/scripts/oracle.json"
printf '{"metered": true, "turns": [{"text": "PAID-SAYS"}]}\n' >"$WORK_DIR/scripts/paid.json"
for name in asker oracle paid; do
    "$APOGEE_BIN" config add-backend "$name" --type mock \
        --model-path "$WORK_DIR/scripts/$name.json" >/dev/null || fail "add-backend $name"
done
# A member billed per call is refused at config time, by its provider's word.
BEFORE_CONSULT=$(cat "$CONFIG")
"$APOGEE_BIN" config add-suite billed --chat asker --utility paid --consultable utility \
    >"$WORK_DIR/billed.txt" 2>&1 && fail "a metered consultable member was written"
grep -q "consultable utility: 'paid' is billed per call" "$WORK_DIR/billed.txt" \
    || fail "the metered member was not refused with the reason: $(cat "$WORK_DIR/billed.txt")"
[ "$(cat "$CONFIG")" = "$BEFORE_CONSULT" ] || fail "a refused consult edit changed the file"
"$APOGEE_BIN" config add-suite consult --chat asker --utility oracle --consultable utility \
    >/dev/null || fail "add-suite consult"
grep -q "    consultable: \[utility\]" "$CONFIG" || fail "consultable was not written: $(cat "$CONFIG")"

echo "what is the codeword?" | "$APOGEE_BIN" chat --suite consult --tools >"$WORK_DIR/consult.txt" 2>&1 \
    || fail "the consulting chat failed: $(cat "$WORK_DIR/consult.txt")"
grep -q "ROOT-RELAYS utility (oracle) answered:" "$WORK_DIR/consult.txt" \
    || fail "the root did not get the member's answer: $(cat "$WORK_DIR/consult.txt")"
grep -q "CODEWORD-MARMALADE heard \[What is the codeword?\] system \[\]" "$WORK_DIR/consult.txt" \
    || fail "the member saw more than the brief: $(cat "$WORK_DIR/consult.txt")"
# The call and its result in history, as any tool's.
CONSULTED=$(session_of "what is the codeword?")
[ -n "$CONSULTED" ] || fail "the consulting chat was not saved"
python3 - "$CONSULTED" <<'PY' || fail "the session does not hold the consult as a tool call and result"
import json, sys
messages = json.load(open(sys.argv[1]))["messages"]
calls = [c for m in messages for c in (m.get("tool_calls") or [])
         if c.get("function", {}).get("name") == "consult"]
results = [m for m in messages if m.get("role") == "tool" and "CODEWORD-MARMALADE" in json.dumps(m)]
sys.exit(0 if len(calls) == 1 and len(results) == 1 else 1)
PY

# Machine mode: the same exchange in the events a driver already reads --
# no new type, the member's call said as a tool status.
printf '{"type":"user","text":"what is the codeword?"}\n' \
    | "$APOGEE_BIN" chat --suite consult --tools --output-format stream-json \
    >"$WORK_DIR/consult.jsonl" 2>"$WORK_DIR/consult-machine.err" \
    || fail "the driven consulting chat failed: $(cat "$WORK_DIR/consult-machine.err")"
python3 - "$WORK_DIR/consult.jsonl" <<'PY' || fail "machine mode carried the consult unlike a tool call: $(cat "$WORK_DIR/consult.jsonl")"
import json, sys
known = {"session", "thinking", "thinking_delta", "memory", "tool_status", "notice",
         "answer_start", "answer_delta", "answer_end", "result", "question", "error"}
events = [json.loads(line) for line in open(sys.argv[1]) if line.strip()]
assert all(event["type"] in known for event in events), [e["type"] for e in events]
statuses = [e["text"] for e in events if e["type"] == "tool_status"]
assert "[tool] consult" in statuses, statuses
assert "consult \u2014 asking utility (oracle): What is the codeword?" in statuses, statuses
result = [e for e in events if e["type"] == "result"][-1]
assert "CODEWORD-MARMALADE heard [What is the codeword?]" in result["text"], result
PY

# /suite moves the offer: under fast there is no consult; switched to the
# consulting suite, its chat member finds the tool.
printf 'first question\n/suite consult\nwhat is the codeword now?\n' \
    | "$APOGEE_BIN" chat --suite fast --tools >"$WORK_DIR/consult-switch.txt" 2>&1 \
    || fail "the switching consult chat failed: $(cat "$WORK_DIR/consult-switch.txt")"
grep -q "FASTROOT-SAYS" "$WORK_DIR/consult-switch.txt" || fail "fast did not answer first"
grep -q "ROOT-RELAYS utility (oracle) answered:" "$WORK_DIR/consult-switch.txt" \
    || fail "/suite did not offer consult: $(cat "$WORK_DIR/consult-switch.txt")"

echo "suites: OK"
