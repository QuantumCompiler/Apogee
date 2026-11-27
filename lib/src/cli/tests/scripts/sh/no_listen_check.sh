#!/usr/bin/env bash
# The interactive-never-listens invariant, test-locked.
#
# SPEC.md -> Principles: "No interactive turn on any backend opens a listening
# socket; only `serve` owns a port." Apogee locks it the day the first
# interactive command exists rather than retrofitting it after the fact,
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
#
# The phases: a mock turn, a code-exec turn (47b), an ACP turn across
# `apogee acp`'s process tree (56a), a turn answered by the mlx backend's real
# driver (27a), and a turn that driver answers under `serve` (27c) -- where
# serve's own process is the one allowed a port, and its driver child never
# is.

set -uo pipefail

APOGEE_BIN="${1:?usage: no_listen_check.sh <apogee-binary> <work-dir> [<mlx-stubs> <mlx-model>]}"
WORK_DIR="${2:?usage: no_listen_check.sh <apogee-binary> <work-dir> [<mlx-stubs> <mlx-model>]}"
# The mlx backend's phase (27a): the stub packages and a model directory.
MLX_STUBS="${3:-}"
MLX_MODEL="${4:-}"

if ! command -v lsof >/dev/null 2>&1; then
    echo "no_listen_check: lsof not found; skipping"
    exit 0
fi

. "$(dirname "$0")/sandbox.sh"
WORK_DIR="$(sandbox_dir "$WORK_DIR")"
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

# --- the code-execution server: a child over pipes, never a port (47b) --------
#
# The same continuous sample over a turn whose tool call runs Python through
# the seeded code_exec_server.py -- an MCP child of apogee, and the code a
# child of that -- across the whole process tree, which must SEE the server
# at least once or it proved nothing. Skipped without python3.
code_descendants() {
    for child in $(pgrep -P "$1" 2>/dev/null); do
        echo "$child"
        code_descendants "$child"
    done
}
if command -v python3 >/dev/null 2>&1; then
    "$APOGEE_BIN" check --fix >/dev/null 2>&1 || true   # seeds the server
    "$APOGEE_BIN" mcp create code --command python3 \
        --args "$WORK_DIR/training/scripts/code_exec_server.py" </dev/null >/dev/null || exit 1
    printf '%s' '{"turns":[{"text":"","tool_calls":[{"name":"mcp__code__run_python","arguments":{"code":"import time\ntime.sleep(1.5)\nprint(\"slept\")"}}]},{"text":"code said: {{last_tool_result}}"}]}' \
        >"$WORK_DIR/code.json"
    "$APOGEE_BIN" config add-backend code-runner --type mock --model-path "$WORK_DIR/code.json" \
        >/dev/null || exit 1

    ( sleep 1; printf 'hello\n' ) | "$APOGEE_BIN" complete -m code-runner --tools \
        --allow mcp__code__run_python >"$WORK_DIR/code-out.txt" 2>"$WORK_DIR/code-err.txt" &
    CHILD=$!
    LISTENING=""
    SERVER_SEEN=""
    while kill -0 "$CHILD" 2>/dev/null; do
        PIDS="$CHILD"
        for pid in $(code_descendants "$CHILD"); do
            PIDS="$PIDS,$pid"
            if ps -o command= -p "$pid" 2>/dev/null | grep code_exec_server.py >/dev/null; then
                SERVER_SEEN=1
            fi
        done
        HIT="$(lsof -a -p "$PIDS" -i -sTCP:LISTEN -Fn 2>/dev/null)"
        if [ -n "$HIT" ]; then
            LISTENING="$HIT"
            break
        fi
    done
    wait "$CHILD"
    STATUS=$?
    if [ -n "$LISTENING" ]; then
        echo "INVARIANT VIOLATED: a code-exec turn's process tree opened a listening socket:" >&2
        echo "$LISTENING" >&2
        exit 1
    fi
    if [ "$STATUS" -ne 0 ]; then
        echo "no_listen_check: the code-exec turn failed (exit $STATUS)" >&2
        cat "$WORK_DIR/code-err.txt" >&2
        exit 1
    fi
    if [ -z "$SERVER_SEEN" ]; then
        echo "no_listen_check: the sample never saw the code-exec server, so it proved nothing" >&2
        exit 1
    fi
    if ! grep -q "code said: slept" "$WORK_DIR/code-out.txt"; then
        echo "no_listen_check: the code-exec turn produced no answer" >&2
        cat "$WORK_DIR/code-out.txt" "$WORK_DIR/code-err.txt" >&2
        exit 1
    fi
    "$APOGEE_BIN" config delete-mcp-server code >/dev/null || exit 1
    echo "no listening sockets across a code-exec turn and its server - OK"
else
    echo "no_listen_check: the code-exec phase needs python3; skipping it"
fi

# --- apogee acp: an agent host's stdio, each session a child (56a) -----------
#
# The same continuous sample over an ACP turn: a scripted host drives
# `apogee acp` over pipes, as Xcode would, through initialize, session/new and
# a slow prompt, while the sample walks acp's whole process tree -- which must
# SEE the session's child, apogee's own execute, or it proved nothing.
# Skipped without python3, which plays the host.
if command -v python3 >/dev/null 2>&1; then
    mkdir -p "$WORK_DIR/acp-project"
    printf '# Rules\n' >"$WORK_DIR/acp-project/AGENTS.md"
    python3 -c 'import json; print(json.dumps({"turns": [{"text": "slowly " * 40, "delay_ms": 50}]}))' \
        >"$WORK_DIR/acp.json"
    "$APOGEE_BIN" config add-backend acp-slow --type mock --model-path "$WORK_DIR/acp.json" \
        >/dev/null || exit 1
    rm -f "$WORK_DIR/acp.pid"
    python3 - "$APOGEE_BIN" "$WORK_DIR/acp-project" "$WORK_DIR/acp.pid" \
        >"$WORK_DIR/acp-out.txt" 2>"$WORK_DIR/acp-err.txt" <<'HOST' &
import json, subprocess, sys
apogee, project, pidfile = sys.argv[1:4]
acp = subprocess.Popen([apogee, "acp", "-m", "acp-slow"], stdin=subprocess.PIPE,
                       stdout=subprocess.PIPE, text=True, bufsize=1)
with open(pidfile, "w") as handle:
    handle.write(str(acp.pid))
def call(i, method, params):
    acp.stdin.write(json.dumps({"jsonrpc": "2.0", "id": i, "method": method,
                                "params": params}) + "\n")
    acp.stdin.flush()
    for line in acp.stdout:
        frame = json.loads(line)
        if frame.get("id") == i:
            return frame
call(1, "initialize", {"protocolVersion": 1})
session = call(2, "session/new", {"cwd": project, "mcpServers": []})["result"]["sessionId"]
done = call(3, "session/prompt", {"sessionId": session,
                                  "prompt": [{"type": "text", "text": "go"}]})
print(done["result"]["stopReason"])
acp.stdin.close()
sys.exit(acp.wait(timeout=60))
HOST
    HOST_PID=$!
    for _ in $(seq 1 100); do
        [ -s "$WORK_DIR/acp.pid" ] && break
        sleep 0.05
    done
    ACP_PID="$(cat "$WORK_DIR/acp.pid" 2>/dev/null)"
    LISTENING=""
    SESSION_SEEN=""
    while [ -n "$ACP_PID" ] && kill -0 "$ACP_PID" 2>/dev/null; do
        PIDS="$ACP_PID"
        for pid in $(code_descendants "$ACP_PID"); do
            PIDS="$PIDS,$pid"
            if ps -o command= -p "$pid" 2>/dev/null | grep ' execute ' >/dev/null; then
                SESSION_SEEN=1
            fi
        done
        HIT="$(lsof -a -p "$PIDS" -i -sTCP:LISTEN -Fn 2>/dev/null)"
        if [ -n "$HIT" ]; then
            LISTENING="$HIT"
            break
        fi
    done
    wait "$HOST_PID"
    STATUS=$?
    if [ -n "$LISTENING" ]; then
        echo "INVARIANT VIOLATED: an ACP turn's process tree opened a listening socket:" >&2
        echo "$LISTENING" >&2
        exit 1
    fi
    if [ "$STATUS" -ne 0 ] || ! grep -q "end_turn" "$WORK_DIR/acp-out.txt"; then
        echo "no_listen_check: the ACP turn failed (exit $STATUS)" >&2
        cat "$WORK_DIR/acp-out.txt" "$WORK_DIR/acp-err.txt" >&2
        exit 1
    fi
    if [ -z "$SESSION_SEEN" ]; then
        echo "no_listen_check: the sample never saw the ACP session's child, so it proved nothing" >&2
        exit 1
    fi
    echo "no listening sockets across an ACP turn and its session's child - OK"
else
    echo "no_listen_check: the ACP phase needs python3; skipping it"
fi

# --- the mlx backend: a child over pipes, never a port (27a) -------------------
#
# The same continuous sample over a turn answered by the REAL driver -- the
# seeded mlx_generate.py, run as the backend's child under stub mlx packages
# -- and this time over the whole process tree, since the driver is a child
# and lsof -p on apogee alone would never see a port it held. The sample must
# also SEE the driver at least once, or it proved nothing about it.
if [ -z "$MLX_STUBS" ]; then
    exit 0
fi
. "$(dirname "$0")/mlx_fake_runtime.sh"
if ! mlx_fake_runtime_supported; then
    echo "no_listen_check: the mlx phase needs Apple silicon and python3; skipping it"
    exit 0
fi
mlx_fake_runtime "$WORK_DIR" "$MLX_STUBS"
"$APOGEE_BIN" check --fix >/dev/null 2>&1 || true   # seeds the driver
"$APOGEE_BIN" config add-backend mlx --type mlx --model-path "$MLX_MODEL" >/dev/null || exit 1
printf '["mlx answer"]' >"$WORK_DIR/replies.json"
export STUB_MLX_REPLIES="$WORK_DIR/replies.json"
# Slow enough that the generation is sampled many times over.
export STUB_MLX_DELAY_MS=40

( sleep 1; printf 'hello\n' ) | "$APOGEE_BIN" complete -m mlx \
    >"$WORK_DIR/mlx-out.txt" 2>"$WORK_DIR/mlx-err.txt" &
CHILD=$!

LISTENING=""
DRIVER_SEEN=""
while kill -0 "$CHILD" 2>/dev/null; do
    PIDS="$CHILD"
    for pid in $(mlx_descendants "$CHILD"); do
        PIDS="$PIDS,$pid"
        if ps -o command= -p "$pid" 2>/dev/null | grep mlx_generate.py >/dev/null; then
            DRIVER_SEEN=1
        fi
    done
    HIT="$(lsof -a -p "$PIDS" -i -sTCP:LISTEN -Fn 2>/dev/null)"
    if [ -n "$HIT" ]; then
        LISTENING="$HIT"
        break
    fi
done

wait "$CHILD"
STATUS=$?

if [ -n "$LISTENING" ]; then
    echo "INVARIANT VIOLATED: an mlx turn's process tree opened a listening socket:" >&2
    echo "$LISTENING" >&2
    exit 1
fi
if [ "$STATUS" -ne 0 ]; then
    echo "no_listen_check: the mlx turn failed (exit $STATUS)" >&2
    cat "$WORK_DIR/mlx-err.txt" >&2
    exit 1
fi
if [ -z "$DRIVER_SEEN" ]; then
    echo "no_listen_check: the sample never saw the mlx driver, so it proved nothing" >&2
    exit 1
fi
if ! grep -q "mlx answer" "$WORK_DIR/mlx-out.txt"; then
    echo "no_listen_check: the mlx turn produced no answer" >&2
    cat "$WORK_DIR/mlx-err.txt" >&2
    exit 1
fi

echo "no listening sockets across an mlx turn and its driver - OK"

# --- a served mlx turn: serve listens, its driver never does (27c) ------------
#
# `serve` routes an mlx entry like any local backend -- the vendor-CLI
# refusal is about credentials, not runtimes -- and is the one process that
# may own a port. Sampled across a streamed turn a stock OpenAI-style client
# asks for: the listening socket must be serve's own, no descendant may hold
# one, and the driver must be seen, or the sample proved nothing.
if ! command -v curl >/dev/null 2>&1; then
    echo "no_listen_check: the served mlx phase needs curl; skipping it"
    exit 0
fi
"$APOGEE_BIN" config set-default mlx >/dev/null || exit 1
printf '["served from mlx"]' >"$WORK_DIR/replies.json"
"$APOGEE_BIN" serve --port 0 >"$WORK_DIR/serve.out" 2>"$WORK_DIR/serve.err" </dev/null &
SERVER=$!
trap 'kill -KILL "$SERVER" 2>/dev/null; rm -rf "$WORK_DIR"' EXIT
PORT=""
for _ in $(seq 1 100); do
    PORT="$(grep -o 'listening on http://127.0.0.1:[0-9]*' "$WORK_DIR/serve.err" 2>/dev/null | grep -o '[0-9]*$' || true)"
    [ -n "$PORT" ] && break
    if ! kill -0 "$SERVER" 2>/dev/null; then
        echo "no_listen_check: serve exited before listening" >&2
        cat "$WORK_DIR/serve.err" >&2
        exit 1
    fi
    sleep 0.1
done
if [ -z "$PORT" ]; then
    echo "no_listen_check: serve never reported a port" >&2
    exit 1
fi

curl -s -N -H 'Content-Type: application/json' \
    -d '{"model":"mlx","messages":[{"role":"user","content":"hello"}],"stream":true}' \
    "http://127.0.0.1:$PORT/v1/chat/completions" >"$WORK_DIR/served.txt" &
CLIENT=$!

CHILD_LISTENING=""
CHILD_SOCKET=""
DRIVER_SEEN=""
SERVE_LISTENS=""
while kill -0 "$CLIENT" 2>/dev/null; do
    if [ -n "$(lsof -a -p "$SERVER" -i -sTCP:LISTEN -Fn 2>/dev/null)" ]; then
        SERVE_LISTENS=1
    fi
    for pid in $(mlx_descendants "$SERVER"); do
        if ps -o command= -p "$pid" 2>/dev/null | grep mlx_generate.py >/dev/null; then
            DRIVER_SEEN=1
        fi
        HIT="$(lsof -a -p "$pid" -i -sTCP:LISTEN -Fn 2>/dev/null)"
        if [ -n "$HIT" ]; then
            CHILD_LISTENING="$pid $HIT"
            break 2
        fi
        # Nor any socket at all: a driver spawned while serve held a client's
        # connection once inherited it (27c), keeping it open for its life.
        HELD="$(lsof -a -p "$pid" -i -Fn 2>/dev/null)"
        if [ -n "$HELD" ]; then
            CHILD_SOCKET="$pid $HELD"
        fi
    done
done
wait "$CLIENT"

kill -TERM "$SERVER"
for _ in $(seq 1 100); do
    kill -0 "$SERVER" 2>/dev/null || break
    sleep 0.1
done
wait "$SERVER" 2>/dev/null

if [ -n "$CHILD_LISTENING" ]; then
    echo "INVARIANT VIOLATED: a served mlx turn's driver held a listening socket:" >&2
    echo "$CHILD_LISTENING" >&2
    exit 1
fi
if [ -n "$CHILD_SOCKET" ]; then
    echo "no_listen_check: a served mlx turn's driver held a socket it was handed:" >&2
    echo "$CHILD_SOCKET" >&2
    exit 1
fi
if [ -z "$DRIVER_SEEN" ]; then
    echo "no_listen_check: the served sample never saw the mlx driver, so it proved nothing" >&2
    cat "$WORK_DIR/serve.err" >&2
    exit 1
fi
if [ -z "$SERVE_LISTENS" ]; then
    echo "no_listen_check: serve held no listening socket while it answered" >&2
    exit 1
fi
# What a stock client reads: content deltas that join to the answer, [DONE] last.
python3 - "$WORK_DIR/served.txt" <<'EOF' || exit 1
import json, sys
frames = [block[len("data: "):] for block in open(sys.argv[1]).read().split("\n\n") if block]
if not frames or frames[-1] != "[DONE]":
    sys.exit(f"no_listen_check: the served stream did not end with [DONE]: {frames[-3:]}")
text = ""
for frame in frames[:-1]:
    delta = json.loads(frame)["choices"][0]["delta"]
    text += delta.get("content") or ""
if text != "served from mlx":
    sys.exit(f"no_listen_check: the served mlx stream said {text!r}")
EOF
if pgrep -f "$WORK_DIR/training/scripts/mlx_generate.py" >/dev/null 2>&1; then
    echo "no_listen_check: a driver outlived serve" >&2
    exit 1
fi

echo "a served mlx turn: serve listens, its driver never does - OK"
