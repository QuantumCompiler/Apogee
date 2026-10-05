#!/usr/bin/env bash
# The mlx backend (27a) on the real binary, with its REAL driver -- the seeded
# mlx_generate.py, run as the backend's child -- under stub mlx packages, so
# nothing is installed and no model is loaded.
#
# What only the process can show: `check` reading the runtime honestly,
# `complete` byte-clean on a pipe with the library's noise kept off both
# streams, a tool-using chat whose call the driver parses in the model's own
# format and the one loop dispatches, both turns served by ONE driver that
# kept its cache, a plain chat ended by /exit taking its driver with it, and
# a runtime taken away refused at construction with the fix named.
#
# Apple silicon only: the backend runs nowhere else, and says so (that half is
# the unit suite's, which drives the ladder with any target).

set -uo pipefail

APOGEE_BIN="${1:?usage: mlx_e2e.sh <apogee-binary> <work-dir> <mlx-stubs> <mlx-model>}"
WORK_DIR="${2:?usage: mlx_e2e.sh <apogee-binary> <work-dir> <mlx-stubs> <mlx-model>}"
MLX_STUBS="${3:?usage: mlx_e2e.sh <apogee-binary> <work-dir> <mlx-stubs> <mlx-model>}"
MLX_MODEL="${4:?usage: mlx_e2e.sh <apogee-binary> <work-dir> <mlx-stubs> <mlx-model>}"

. "$(dirname "$0")/mlx_fake_runtime.sh"
if ! mlx_fake_runtime_supported; then
    echo "mlx_e2e: needs Apple silicon and python3; skipping"
    exit 0
fi

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR/work"
export APOGEE_HOME="$WORK_DIR"
unset ANTHROPIC_API_KEY OPENAI_API_KEY GEMINI_API_KEY GOOGLE_API_KEY
trap 'rm -rf "$WORK_DIR"' EXIT

fail() { echo "mlx_e2e: $*" >&2; exit 1; }

no_driver_left() {
    if pgrep -f "$WORK_DIR/training/scripts/mlx_generate.py" >/dev/null 2>&1; then
        fail "a driver outlived its apogee: $(pgrep -fl "$WORK_DIR/training/scripts/mlx_generate.py")"
    fi
}

"$APOGEE_BIN" config init >/dev/null || fail "config init"
mlx_fake_runtime "$WORK_DIR" "$MLX_STUBS"
"$APOGEE_BIN" check --fix >/dev/null 2>&1 || true
[ -f "$WORK_DIR/training/scripts/mlx_generate.py" ] || fail "check --fix did not seed the driver"
# The fixture directory, given the one shard a whole model has: since 27b
# the doctor reads an entry's files whole, and the stub never loads it.
cp -R "$MLX_MODEL" "$WORK_DIR/model" || fail "could not copy the fixture model"
python3 - "$WORK_DIR/model/model.safetensors" <<'EOF' || fail "could not write the fixture shard"
import json, struct, sys
header = json.dumps({"__metadata__": {"format": "mlx"},
                     "w": {"dtype": "F16", "shape": [8], "data_offsets": [0, 16]}}).encode()
open(sys.argv[1], "wb").write(struct.pack("<Q", len(header)) + header + b"\0" * 16)
EOF
"$APOGEE_BIN" config add-backend mlx --type mlx --model-path "$WORK_DIR/model" >/dev/null \
    || fail "add-backend"
"$APOGEE_BIN" config set-default mlx >/dev/null || fail "set-default"

# --- check: what the files show, and nothing more -----------------------------
"$APOGEE_BIN" check --no-color >"$WORK_DIR/check.txt" 2>&1
grep -q "mlx-lm 0.0-stub is present" "$WORK_DIR/check.txt" \
    || fail "check did not report the runtime: $(cat "$WORK_DIR/check.txt")"
grep -q "mlx_generate.py matches the shipped copy" "$WORK_DIR/check.txt" \
    || fail "check did not report the driver"
grep -q "backend: mlx.*llama model directory" "$WORK_DIR/check.txt" \
    || fail "check did not report the entry: $(cat "$WORK_DIR/check.txt")"
# Its window, read from config.json under 26a's default (27b).
perl -0pe 's/\n  (?! )//g' "$WORK_DIR/check.txt" \
    | grep -q "32768-token window (the default; trained for 131072)" \
    || fail "check did not report the entry's window"

# --- complete: one shot, byte-clean, the library's noise on neither stream ----
export STUB_MLX_REPLIES="$WORK_DIR/replies.json"
export STUB_MLX_NOISE="NOISY-MLX-LIBRARY-LINE"
printf '["4"]' >"$STUB_MLX_REPLIES"
"$APOGEE_BIN" complete -m mlx "What is 2+2?" >"$WORK_DIR/out.txt" 2>"$WORK_DIR/err.txt" \
    || fail "complete failed: $(cat "$WORK_DIR/err.txt")"
[ "$(od -An -c "$WORK_DIR/out.txt" | tr -d ' \n')" = '4\n' ] \
    || fail "stdout was not exactly the answer: $(od -c "$WORK_DIR/out.txt")"
if grep -q NOISY "$WORK_DIR/out.txt" "$WORK_DIR/err.txt"; then
    fail "the driver's stderr reached the terminal"
fi
no_driver_left

# --- a tool-using chat, driven: the call parsed in the model's own format -----
export STUB_MLX_TOOLS=json_tools
export STUB_MLX_RECORD="$WORK_DIR/record.txt"
echo "The secret word is: pineapple." >"$WORK_DIR/work/notes.txt"
cat >"$STUB_MLX_REPLIES" <<'EOF'
{"rules": [["<|tool|>", "The secret is pineapple."],
           ["notes.txt", "<tool_call>{\"name\": \"read_file\", \"arguments\": {\"path\": \"notes.txt\"}}</tool_call>"],
           ["again", "Still pineapple."]],
 "default": "A title"}
EOF
(cd "$WORK_DIR/work" && printf '%s\n' \
    '{"type":"user","text":"Read notes.txt and tell me the secret word."}' \
    '{"type":"user","text":"What was it again?"}' \
    | "$APOGEE_BIN" chat --tools --output-format stream-json --input-format stream-json \
        >"$WORK_DIR/chat.jsonl" 2>"$WORK_DIR/chat-err.txt") \
    || fail "the driven chat failed: $(cat "$WORK_DIR/chat-err.txt")"

grep -q '"text":"\[tool\] read_file"' "$WORK_DIR/chat.jsonl" \
    || fail "the call was not dispatched: $(cat "$WORK_DIR/chat.jsonl")"
grep -q '"text":"The secret is pineapple.","type":"result"' "$WORK_DIR/chat.jsonl" \
    || fail "the tool's result never reached the answer: $(grep result "$WORK_DIR/chat.jsonl")"
grep -q '"text":"Still pineapple.","type":"result"' "$WORK_DIR/chat.jsonl" \
    || fail "the second turn was not answered"
if grep -q NOISY "$WORK_DIR/chat.jsonl" "$WORK_DIR/chat-err.txt"; then
    fail "the driver's stderr reached the terminal in a chat"
fi
# ONE driver for the session: one load, and the second turn read from a cache
# the first had filled.
[ "$(grep -c '^load ' "$STUB_MLX_RECORD")" -eq 1 ] \
    || fail "expected one driver load, got: $(grep '^load ' "$STUB_MLX_RECORD")"
grep -q '^step fed=[0-9]* cached=[1-9]' "$STUB_MLX_RECORD" \
    || fail "no turn reused the session's cache: $(grep '^step' "$STUB_MLX_RECORD")"
no_driver_left

# --- /exit ends a chat, and its driver with it ---------------------------------
unset STUB_MLX_TOOLS
printf '["hello back"]' >"$STUB_MLX_REPLIES"
printf 'hello\n/exit\n' | "$APOGEE_BIN" chat -m mlx >"$WORK_DIR/plain.txt" 2>&1 \
    || fail "a plain chat failed: $(cat "$WORK_DIR/plain.txt")"
grep -q "hello back" "$WORK_DIR/plain.txt" || fail "the plain chat never answered"
no_driver_left

# --- the runtime taken away: refused at construction, the fix named -----------
mv "$WORK_DIR/training/venv" "$WORK_DIR/training/venv.away"
if "$APOGEE_BIN" complete -m mlx "hi" >"$WORK_DIR/out.txt" 2>"$WORK_DIR/err.txt"; then
    fail "an mlx entry with no environment answered"
fi
grep -q "apogee train setup --with mlx" "$WORK_DIR/err.txt" \
    || fail "the refusal did not name the fix: $(cat "$WORK_DIR/err.txt")"
mv "$WORK_DIR/training/venv.away" "$WORK_DIR/training/venv"

echo "mlx backend over its real driver - OK"
