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

echo "machine mode: stdout is pure JSONL, one child served 2 turns, no listening socket - OK"
