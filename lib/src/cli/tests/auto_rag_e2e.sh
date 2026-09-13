#!/usr/bin/env bash
# auto_rag, the two things about it only a real shell can show.
#
# 1. `--rag ""` switches it off for one run. This cannot live in
#    config_e2e.cmake: CMake's execute_process drops an empty list element, so
#    there is no way to hand the binary an EMPTY argument from a cmake -P
#    script at all. The precedence itself is table-tested in
#    tests/commands/helpers_test.cpp; this proves the empty value survives the
#    trip from a real command line through CLI11 to that table.
#
# 2. The key is read at TURN BUILD, not at startup, so an edit mid-session
#    takes effect on the next question. Shown by feeding chat two questions
#    with a config edit between them and counting how many turns announced
#    auto_rag: exactly one.
#
# POSIX only, like the other .sh checks here (CLAUDE.md -> Platforms).

set -uo pipefail

APOGEE_BIN="${1:?usage: auto_rag_e2e.sh <apogee-binary> <work-dir>}"
WORK_DIR="${2:?usage: auto_rag_e2e.sh <apogee-binary> <work-dir>}"

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR/corpus"
export APOGEE_HOME="$WORK_DIR"
trap 'rm -rf "$WORK_DIR"' EXIT

fail() { echo "auto_rag: $*" >&2; exit 1; }

"$APOGEE_BIN" config init >/dev/null || fail "config init"
"$APOGEE_BIN" config add-backend mock --type mock --model mock-1 >/dev/null || fail "add-backend"
echo "the zarquon protocol requires seventeen widgets" > "$WORK_DIR/corpus/notes.md"
"$APOGEE_BIN" embed ingest notes "$WORK_DIR/corpus" >/dev/null || fail "ingest"
printf '\nauto_rag: notes\n' >> "$WORK_DIR/config/config.yaml"

QUESTION="what does the zarquon protocol require?"

# --- the positive control: with no flag, the key injects and says so --------
# Without this the check below could pass on a build where retrieval was
# simply broken, which is a very different thing from "switched off".
ON=$("$APOGEE_BIN" complete -m mock -v "$QUESTION" 2>&1)
echo "$ON" | grep -q "chunk(s) from 'notes'" || fail "auto_rag did not inject with no flag: $ON"
echo "$ON" | grep -q "(auto_rag)" || fail "injection from config was not announced: $ON"

# --- the switch: an EMPTY flag means no retrieval this run ------------------
OFF=$("$APOGEE_BIN" complete -m mock -v --rag "" "$QUESTION" 2>&1) || fail "complete --rag \"\" failed: $OFF"
if echo "$OFF" | grep -q "chunk(s) from"; then
    fail "--rag \"\" did not switch auto_rag off: $OFF"
fi
if echo "$OFF" | grep -q "auto_rag"; then
    fail "--rag \"\" still reported the key: $OFF"
fi
# The empty value was consumed AS the flag's value: the positional prompt was
# not swallowed, so the turn still ran and answered.
echo "$OFF" | grep -q "piped prompt was empty" && fail "--rag \"\" swallowed the prompt"

# --- the same switch on the chat surface -------------------------------------
# One shared decision behind both surfaces, but the PLUMBING of "was the flag
# present" is per surface, so each is checked.
CHAT_OFF=$(echo "$QUESTION" | "$APOGEE_BIN" chat -m mock --rag "" 2>&1) || fail "chat --rag \"\" failed: $CHAT_OFF"
if echo "$CHAT_OFF" | grep -q "auto_rag"; then
    fail "chat --rag \"\" did not switch auto_rag off: $CHAT_OFF"
fi

# --- read at turn build: an edit between two questions is honoured ----------
# The feeder waits two seconds after the first question -- a mock turn takes
# milliseconds -- then switches the key off in the file, then asks again.
# A chat that read auto_rag once at startup would announce it on both turns.
CONFIG="$WORK_DIR/config/config.yaml"
TWO_TURNS=$( { echo "$QUESTION"; sleep 2; sed -i.bak 's/^auto_rag: notes$/auto_rag: ""/' "$CONFIG"; echo "$QUESTION"; } \
    | "$APOGEE_BIN" chat -m mock 2>&1 ) || fail "two-turn chat failed: $TWO_TURNS"
ANNOUNCED=$(echo "$TWO_TURNS" | grep -c "(auto_rag)")
[ "$ANNOUNCED" -eq 1 ] || fail "expected auto_rag on exactly one of two turns, saw $ANNOUNCED:
$TWO_TURNS"

echo "auto_rag: OK"
