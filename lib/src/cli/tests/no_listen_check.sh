#!/usr/bin/env bash
# The interactive-never-listens invariant, test-locked.
#
# SPEC.md -> Principles: "No interactive turn on any backend opens a listening
# socket; only `serve` owns a port." Ommi retrofitted this check after the fact
# (OMMI-11); Apogee locks it the day the first interactive command exists,
# because the failure it prevents is silent -- a backend that quietly starts a
# local server still answers correctly, so nothing looks wrong until someone
# notices a port open on a shared machine.
#
# The process has to be ALIVE to be inspected, and a mock-backed turn finishes
# in milliseconds. So stdin is a pipe from a writer that pauses first: apogee
# blocks at its read, we check for listening sockets, then the prompt arrives
# and the turn completes normally. That exercises the piped-stdin path too.
#
# POSIX only -- it needs lsof. Windows has no equivalent here; that is a
# recorded per-item skip (CLAUDE.md -> Platforms), and the script also skips
# itself cleanly wherever lsof is simply absent.

set -uo pipefail

APOGEE_BIN="${1:?usage: no_listen_check.sh <apogee-binary> <work-dir>}"
WORK_DIR="${2:?usage: no_listen_check.sh <apogee-binary> <work-dir>}"

if ! command -v lsof >/dev/null 2>&1; then
    echo "no_listen_check: lsof not found; skipping"
    exit 0
fi

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR"
export APOGEE_HOME="$WORK_DIR"
trap 'rm -rf "$WORK_DIR"' EXIT

"$APOGEE_BIN" config init >/dev/null || exit 1
"$APOGEE_BIN" config add-backend mock --type mock --model mock-1 >/dev/null || exit 1
"$APOGEE_BIN" config set-default mock >/dev/null || exit 1

# The writer pauses, so apogee sits at its read while we inspect it.
# `$!` on a pipeline is the LAST element -- apogee itself.
( sleep 3; printf 'hello\n' ) | "$APOGEE_BIN" complete \
    >"$WORK_DIR/out.txt" 2>"$WORK_DIR/err.txt" &
CHILD=$!

# Sample CONTINUOUSLY from now until the process exits, rather than once.
#
# A single sample during the stdin block proves nothing: it happens BEFORE the
# turn runs, so a socket opened while talking to the backend -- exactly the
# failure this guards against -- would go unseen. (That gap was real, and a
# deliberately-violating build passed the one-shot version of this check.)
#
# The window this catches is "held for more than roughly a millisecond", which
# is the honest bound of sampling from outside the process. Every realistic
# violation is far wider than that: a local inference server or proxy holds its
# port for the whole turn and usually the whole process lifetime.
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
    echo "INVARIANT VIOLATED: apogee opened a listening socket during an interactive turn:" >&2
    echo "$LISTENING" >&2
    echo "Only 'apogee serve' may own a port. See SPEC.md -> Principles." >&2
    exit 1
fi

if [ "$STATUS" -ne 0 ]; then
    echo "no_listen_check: the turn failed (exit $STATUS)" >&2
    cat "$WORK_DIR/err.txt" >&2
    exit 1
fi

if ! grep -q "mock response" "$WORK_DIR/out.txt"; then
    echo "no_listen_check: the turn produced no answer" >&2
    cat "$WORK_DIR/err.txt" >&2
    exit 1
fi

echo "no listening sockets during an interactive turn - OK"
