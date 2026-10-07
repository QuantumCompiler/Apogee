#!/usr/bin/env bash
# Suites (27d) on the real binary: the config unit written through the one
# editor, a chat run under one, the bundle switched mid-chat, and both
# surviving a resume -- and, since 27e, the set's footprint stated, a suite
# that cannot fit refused until --force, and --warm silent on a pipe; since
# 27f, the root consulting a member on exactly the brief, a metered member
# refused at config time, and machine mode carrying the consult as any tool
# call; since 27g, validation -- a tool call objected to as its round-one
# result and its revision run, /check, and a capture's planted wrong field
# disputed -- with the run's own records as the evidence:
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
echo "$OFFERED" | grep -qx "off" || fail "chat --suite does not complete off, which it takes: $OFFERED"

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

# --- validation (27g): members checking each other's work -------------------
# The tool-argument seam on the real filesystem tools: the root deletes the
# wrong note first; the verifier objects, the objection comes back as the
# call's result and the file survives; the revised call is checked again and
# runs. The verifier's script is the evidence of each check.
cat >"$WORK_DIR/scripts/deleter.json" <<'JSON'
{"turns": [{"text": "", "tool_calls": [{"name": "delete_file", "arguments": {"path": "notes/final.md"}}]},
           {"text": "", "tool_calls": [{"name": "delete_file", "arguments": {"path": "notes/draft.md"}}]},
           {"text": "ROOT-DELETED {{last_tool_result}}"}]}
JSON
cat >"$WORK_DIR/scripts/verifier.json" <<'JSON'
{"turns": [{"text": "OBJECT: the user asked to delete the draft; notes/final.md is the final copy"},
           {"text": "AGREE"}]}
JSON
for name in deleter verifier; do
    "$APOGEE_BIN" config add-backend "$name" --type mock \
        --model-path "$WORK_DIR/scripts/$name.json" >/dev/null || fail "add-backend $name"
done
# A verifier billed per call is refused at config time, the file untouched.
BEFORE_VALIDATE=$(cat "$CONFIG")
"$APOGEE_BIN" config add-suite paidcheck --chat deleter --utility paid --validate tool_args=on \
    >"$WORK_DIR/paidcheck.txt" 2>&1 && fail "a metered verifier was written"
grep -q "validate: 'paid' is billed per call" "$WORK_DIR/paidcheck.txt" \
    || fail "the metered verifier was not refused with the reason: $(cat "$WORK_DIR/paidcheck.txt")"
[ "$(cat "$CONFIG")" = "$BEFORE_VALIDATE" ] || fail "a refused validate edit changed the file"
# --validate is repeatable.
"$APOGEE_BIN" config add-suite checked --chat deleter --utility verifier \
    --validate tool_args=on --validate extraction=off >/dev/null || fail "add-suite checked"
grep -q "^    validate:$" "$CONFIG" || fail "validate was not written: $(cat "$CONFIG")"
"$APOGEE_BIN" config get suites.checked.validate >"$WORK_DIR/validate-get.txt" 2>&1 \
    || fail "config get validate"
grep -qx "verifier utility (default), tool_args on, extraction off, answers request (default)" \
    "$WORK_DIR/validate-get.txt" || fail "config get validate: $(cat "$WORK_DIR/validate-get.txt")"

mkdir -p "$WORK_DIR/ws/notes"
echo draft >"$WORK_DIR/ws/notes/draft.md"
echo final >"$WORK_DIR/ws/notes/final.md"
(cd "$WORK_DIR/ws" && echo "Delete the draft notes." \
    | "$APOGEE_BIN" chat --suite checked --tools --allow delete_file) >"$WORK_DIR/validate.txt" 2>&1 \
    || fail "the validated chat failed: $(cat "$WORK_DIR/validate.txt")"
[ -f "$WORK_DIR/ws/notes/final.md" ] || fail "the call objected to ran: final.md is gone"
[ -f "$WORK_DIR/ws/notes/draft.md" ] && fail "the revised call did not run: draft.md is still there"
grep -q "validate: utility (verifier) objected to delete_file {\"path\":\"notes/final.md\"}" \
    "$WORK_DIR/validate.txt" || fail "the objection was not said: $(cat "$WORK_DIR/validate.txt")"
grep -q "ROOT-DELETED Deleted" "$WORK_DIR/validate.txt" \
    || fail "the revision's result did not reach the root: $(cat "$WORK_DIR/validate.txt")"
VALIDATED=$(session_of "Delete the draft notes.")
[ -n "$VALIDATED" ] || fail "the validated chat was not saved"
python3 - "$VALIDATED" <<'PY' || fail "the objection is not round one's tool result: $(cat "$VALIDATED")"
import json, sys
messages = json.load(open(sys.argv[1]))["messages"]
results = [m["content"] if isinstance(m["content"], str) else json.dumps(m["content"])
           for m in messages if m.get("role") == "tool"]
assert len(results) == 2, results
assert results[0].startswith("Not run: before delete_file ran, utility (verifier) checked it and objected"), results
assert "Deleted" in results[1], results
PY

# Machine mode: the same rounds in events a driver already reads.
echo draft >"$WORK_DIR/ws/notes/draft.md"
(cd "$WORK_DIR/ws" && printf '{"type":"user","text":"Delete the draft notes, driven."}\n' \
    | "$APOGEE_BIN" chat --suite checked --tools --allow delete_file --output-format stream-json) \
    >"$WORK_DIR/validate.jsonl" 2>"$WORK_DIR/validate-machine.err" \
    || fail "the driven validated chat failed: $(cat "$WORK_DIR/validate-machine.err")"
python3 - "$WORK_DIR/validate.jsonl" <<'PY' || fail "machine mode carried validation in an unknown shape: $(cat "$WORK_DIR/validate.jsonl")"
import json, sys
known = {"session", "thinking", "thinking_delta", "memory", "tool_status", "notice",
         "answer_start", "answer_delta", "answer_end", "result", "question", "error"}
events = [json.loads(line) for line in open(sys.argv[1]) if line.strip()]
assert all(event["type"] in known for event in events), [e["type"] for e in events]
notices = [e["text"] for e in events if e["type"] == "notice"]
assert any(n.startswith("validate: utility (verifier) objected to delete_file") for n in notices), notices
statuses = [e["text"] for e in events if e["type"] == "tool_status"]
assert any(s.startswith("validate — asking utility (verifier): Check a tool call before it runs") for s in statuses), statuses
PY
[ -f "$WORK_DIR/ws/notes/final.md" ] || fail "the driven call objected to ran"

# /check on the real binary: the verifier once, the model's reply beside it.
# The utility member titles the chat first, so its script's first turn is
# the title.
printf '{"turns": [{"text": "381"}, {"text": "You are right: 391."}]}\n' \
    >"$WORK_DIR/scripts/answerer.json"
printf '{"turns": [{"text": "A Title"}, {"text": "OBJECT: 17 x 23 is 391, not 381."}]}\n' \
    >"$WORK_DIR/scripts/checker.json"
for name in answerer checker; do
    "$APOGEE_BIN" config add-backend "$name" --type mock \
        --model-path "$WORK_DIR/scripts/$name.json" >/dev/null || fail "add-backend $name"
done
"$APOGEE_BIN" config add-suite asking --chat answerer --utility checker >/dev/null \
    || fail "add-suite asking"
printf 'what is 17 x 23?\n/check\n' | "$APOGEE_BIN" chat --suite asking >"$WORK_DIR/check-answer.txt" 2>&1 \
    || fail "/check failed: $(cat "$WORK_DIR/check-answer.txt")"
grep -q 'check: utility (checker) objects to the answer -- "17 x 23 is 391, not 381."' \
    "$WORK_DIR/check-answer.txt" || fail "/check did not say the objection: $(cat "$WORK_DIR/check-answer.txt")"
grep -q 'check: shown the objection, the model answered -- "You are right: 391."' \
    "$WORK_DIR/check-answer.txt" || fail "/check did not say the model's reply: $(cat "$WORK_DIR/check-answer.txt")"
CHECKED=$(session_of "what is 17 x 23?")
python3 - "$CHECKED" <<'PY' || fail "/check changed the conversation: $(cat "$CHECKED")"
import json, sys
messages = json.load(open(sys.argv[1]))["messages"]
assert [m["role"] for m in messages] == ["user", "assistant"], messages
PY

# The extraction seam: a capture whose clerk plants one wrong field -- the
# link -- and stands by it; the verifier objects, the clerk is asked once
# more, and the dispute is surfaced with the record kept as the clerk wrote it.
cat >"$WORK_DIR/scripts/clerk.json" <<'JSON'
{"turns": [{"text": "{\"intent\": \"Testers kept mistaking the cancel button for back.\", \"decision\": \"Remove the cancel button.\", \"status\": \"shipped\", \"discipline\": \"ux\", \"downstream_link\": \"PROJ-24\", \"provenance\": {\"source\": \"meeting\"}}"}]}
JSON
printf '{"turns": [{"text": "OBJECT: downstream_link is PROJ-24; the source says PROJ-42."}]}\n' \
    >"$WORK_DIR/scripts/linkcheck.json"
for name in clerk linkcheck; do
    "$APOGEE_BIN" config add-backend "$name" --type mock \
        --model-path "$WORK_DIR/scripts/$name.json" >/dev/null || fail "add-backend $name"
done
"$APOGEE_BIN" config add-suite capturing --chat root --extraction clerk --utility linkcheck \
    --validate extraction=on >/dev/null || fail "add-suite capturing"
"$APOGEE_BIN" config set-default-suite capturing >/dev/null || fail "set-default-suite capturing"
echo "Ada: testers kept mistaking the cancel button for back, so we removed it. Shipped in PROJ-42." \
    | "$APOGEE_BIN" knowledge capture --dry-run --json >"$WORK_DIR/capture.json" 2>"$WORK_DIR/capture.err" \
    || fail "the validated capture failed: $(cat "$WORK_DIR/capture.err")"
python3 - "$WORK_DIR/capture.json" <<'PY' || fail "the capture's validation is not the dispute: $(cat "$WORK_DIR/capture.json")"
import json, sys
out = json.load(open(sys.argv[1]))
v = out["validation"]
assert v["result"] == "disputed", v
assert v["verifier"] == "utility (linkcheck)", v
assert v["objection"] == "downstream_link is PROJ-24; the source says PROJ-42.", v
assert v["verifier_calls"] == 1 and v["revisions"] == 1 and v.get("insisted") is True, v
assert out["record"]["downstream_link"] == "PROJ-24", out["record"]
PY
# And in words, on the human path.
echo "Ada: we removed the cancel button. Shipped in PROJ-42." \
    | "$APOGEE_BIN" knowledge capture --dry-run >"$WORK_DIR/capture.txt" 2>&1 \
    || fail "the validated capture (text) failed: $(cat "$WORK_DIR/capture.txt")"
grep -q 'apogee knowledge: disputed by utility (linkcheck): "downstream_link is PROJ-24; the source says PROJ-42." -- the clerk returned the same record' \
    "$WORK_DIR/capture.txt" || fail "the dispute was not said: $(cat "$WORK_DIR/capture.txt")"
"$APOGEE_BIN" config set-default-suite off >/dev/null || fail "set-default-suite off"

echo "suites: OK"
