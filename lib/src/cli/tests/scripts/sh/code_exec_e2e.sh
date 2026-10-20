#!/bin/sh
# The code-execution server (47b) on the real binary: seeded by the one
# seeding path byte for byte as shipped, its registration line said by the
# doctor, registered through `mcp create --command` like any server, its two
# tools connected -- and a model's call to it through the permission gate on
# a real turn: refused with nobody to ask, run under --allow (its answer and
# the file it wrote, as a path), refused by plan and dont-ask over or without
# an allow, never passed by accept-edits. The session directory is gone once
# the run ends.
#
# POSIX only, like the other shell checks. Skips (77) where there is no
# python3 to run the server with.
set -eu

APOGEE_BIN="${1:?usage: code_exec_e2e.sh <apogee-binary> <work-dir> <assets-dir>}"
WORK_DIR="${2:?usage: code_exec_e2e.sh <apogee-binary> <work-dir> <assets-dir>}"
ASSETS_DIR="${3:?usage: code_exec_e2e.sh <apogee-binary> <work-dir> <assets-dir>}"

if ! command -v python3 >/dev/null 2>&1; then
    echo "code_exec_e2e: python3 not found; skipping"
    exit 77
fi

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR"
export APOGEE_HOME="$WORK_DIR"
unset ANTHROPIC_API_KEY OPENAI_API_KEY GEMINI_API_KEY GOOGLE_API_KEY
trap 'rm -rf "$WORK_DIR"' EXIT

fail() { echo "code_exec_e2e: $*" >&2; exit 1; }

"$APOGEE_BIN" config init </dev/null >/dev/null || fail "config init"

# --- seeded, byte for byte ---------------------------------------------------
"$APOGEE_BIN" check --fix </dev/null >/dev/null 2>&1 || true
SCRIPT="$WORK_DIR/training/scripts/code_exec_server.py"
[ -f "$SCRIPT" ] || fail "check --fix did not seed $SCRIPT"
cmp -s "$SCRIPT" "$ASSETS_DIR/mcp/code_exec_server.py" || fail "the seeded server differs from the shipped file"

# --- the doctor says the line that registers it ------------------------------
"$APOGEE_BIN" check </dev/null >"$WORK_DIR/check.txt" 2>&1 || true
grep -q "code-exec.*not registered -- it would run under" "$WORK_DIR/check.txt" || fail "the doctor did not say where the server stands: $(grep code-exec "$WORK_DIR/check.txt")"
grep -q "apogee mcp create code --command .* --args $SCRIPT" "$WORK_DIR/check.txt" || fail "the doctor did not give the registration line: $(cat "$WORK_DIR/check.txt")"

# --- registered by hand, like any server -------------------------------------
"$APOGEE_BIN" mcp create code --command python3 --args "$SCRIPT" </dev/null >/dev/null || fail "mcp create"
"$APOGEE_BIN" mcp list </dev/null >"$WORK_DIR/list.txt" 2>&1 || fail "mcp list"
grep -q "code .*connected .*run_python, write_input" "$WORK_DIR/list.txt" || fail "the server did not connect with its two tools: $(cat "$WORK_DIR/list.txt")"
"$APOGEE_BIN" check </dev/null >"$WORK_DIR/check.txt" 2>&1 || true
grep -q "code-exec.*registered as 'code'" "$WORK_DIR/check.txt" || fail "the doctor did not see the registration"

# --- a model calls it, through the gate --------------------------------------
cat >"$WORK_DIR/runner.json" <<'EOF'
{"turns":[{"text":"","tool_calls":[{"name":"mcp__code__run_python","arguments":{"code":"open('answer.txt', 'w').write('42')\nprint('computed', 6 * 7)"}}]},{"text":"ran: {{last_tool_result}}"}]}
EOF
"$APOGEE_BIN" config add-backend runner --type mock --model-path "$WORK_DIR/runner.json" </dev/null >/dev/null || fail "add-backend runner"

turn() {
    # turn <name> [flags...]: one piped tool-using turn on the runner.
    name="$1"; shift
    echo go | "$APOGEE_BIN" chat --tools -m runner "$@" >"$WORK_DIR/$name.txt" 2>"$WORK_DIR/$name.err" || fail "the $name turn failed: $(cat "$WORK_DIR/$name.err")"
}

# Asked with nobody to answer: refused, the code never run.
turn asked
grep -q "the user denied permission to run 'mcp__code__run_python'" "$WORK_DIR/asked.txt" || fail "a piped run was not refused: $(cat "$WORK_DIR/asked.txt")"

# Allowed: the answer, and the file it wrote as a path under the session.
turn allowed --allow mcp__code__run_python
grep -q "ran: computed 42" "$WORK_DIR/allowed.txt" || fail "the allowed run did not answer: $(cat "$WORK_DIR/allowed.txt")"
grep -q "^\[files written\]$" "$WORK_DIR/allowed.txt" || fail "no files were listed: $(cat "$WORK_DIR/allowed.txt")"
grep -q "/cache/code-exec/session-[0-9a-f]*/answer.txt (2 bytes)$" "$WORK_DIR/allowed.txt" || fail "the file was not returned as a path: $(cat "$WORK_DIR/allowed.txt")"

# The modes govern it like any writer (39b).
turn plan --allow mcp__code__run_python --permission-mode plan
grep -q "'mcp__code__run_python' was not run: plan mode is on" "$WORK_DIR/plan.txt" || fail "plan mode let it run: $(cat "$WORK_DIR/plan.txt")"
turn dontask --permission-mode dont-ask
grep -q "'mcp__code__run_python' was not run: this session refuses whatever it would ask" "$WORK_DIR/dontask.txt" || fail "dont-ask let it run: $(cat "$WORK_DIR/dontask.txt")"
turn edits --permission-mode accept-edits
grep -q "the user denied permission to run 'mcp__code__run_python'" "$WORK_DIR/edits.txt" || fail "accept-edits passed a code run unasked: $(cat "$WORK_DIR/edits.txt")"

# --- the session directory goes with the session -----------------------------
# Each run's server has exited by now: nothing is left under cache/code-exec.
LEFT=""
for _ in 1 2 3 4 5 6 7 8 9 10; do
    LEFT="$(find "$WORK_DIR/cache/code-exec" -mindepth 1 -maxdepth 1 2>/dev/null)"
    [ -z "$LEFT" ] && break
    sleep 0.2
done
[ -z "$LEFT" ] || fail "a session directory outlived its session: $LEFT"

echo "the code-execution server: seeded, said, registered, gated, cleaned up - OK"
