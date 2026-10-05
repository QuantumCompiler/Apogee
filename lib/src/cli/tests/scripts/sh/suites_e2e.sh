#!/usr/bin/env bash
# Suites (27d) on the real binary: the config unit written through the one
# editor, a chat run under one, the bundle switched mid-chat, and both
# surviving a resume -- with the run's own records as the evidence:
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

echo "suites: OK"
