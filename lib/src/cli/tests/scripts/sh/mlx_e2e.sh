#!/usr/bin/env bash
# The mlx backend (27a) on the real binary, with its REAL driver -- the seeded
# mlx_generate.py, run as the backend's child -- under stub mlx packages, so
# nothing is installed and no model is loaded.
#
# What only the process can show: `check` reading the runtime honestly,
# `complete` byte-clean on a pipe with the library's noise kept off both
# streams, a tool-using chat whose call the driver parses in the model's own
# format and the one loop dispatches, both turns served by ONE driver that
# kept its cache, 27a's own driver brought up to this build's by check --fix
# and an edit of it kept, a plain chat ended by /exit taking its driver with it, a
# vision model's picture read as it is through mlx-vlm -- and, without it,
# the doctor saying so and the picture refused naming the way (27c) -- a
# text model's picture described by the vision role instead, a run promoted
# with --target mlx registered as an mlx entry with no conversion and then
# answered through the driver from the store (27c), and a runtime taken away
# refused at construction with the fix named.
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

# --- an upgrade reaches the driver (27c) -------------------------------------
# An install that ran 27a holds its mlx_generate.py: seeding is skip-if-present,
# so only the retired-digest refresh brings it up to this build's -- and an
# edit is the user's, kept.
cp "$(dirname "$MLX_MODEL")/mlx_generate-27a.py" "$WORK_DIR/training/scripts/mlx_generate.py" \
    || fail "could not lay 27a's driver in"
"$APOGEE_BIN" check --no-color >"$WORK_DIR/check-stale.txt" 2>&1
grep -q "mlx_generate.py is an earlier Apogee's copy, unedited" "$WORK_DIR/check-stale.txt" \
    || fail "check did not call 27a's driver an earlier copy: $(grep driver "$WORK_DIR/check-stale.txt")"
"$APOGEE_BIN" check --fix >"$WORK_DIR/fix.txt" 2>&1
grep -q "updated $WORK_DIR/training/scripts/mlx_generate.py" "$WORK_DIR/fix.txt" \
    || fail "check --fix did not update the driver: $(cat "$WORK_DIR/fix.txt")"
"$APOGEE_BIN" check --no-color >"$WORK_DIR/check-stale.txt" 2>&1
grep -q "mlx_generate.py matches the shipped copy" "$WORK_DIR/check-stale.txt" \
    || fail "the refreshed driver is not this build's"
printf '\n# mine\n' >>"$WORK_DIR/training/scripts/mlx_generate.py"
"$APOGEE_BIN" check --fix >"$WORK_DIR/fix.txt" 2>&1
grep -q "# mine" "$WORK_DIR/training/scripts/mlx_generate.py" || fail "check --fix overwrote an edit"
"$APOGEE_BIN" check --no-color 2>&1 | grep -q "mlx_generate.py differs from the shipped copy -- your edit is kept" \
    || fail "an edited driver was not reported as the user's"
rm -f "$WORK_DIR/training/scripts/mlx_generate.py"
"$APOGEE_BIN" check --fix >/dev/null 2>&1 || true

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

# --- vision (27c): a picture read as it is, or honestly not --------------------
# The fixture made a vision model: a vision tower in its configuration and an
# image processor beside it. A 1x1 PNG, then another, to attach.
cp -R "$WORK_DIR/model" "$WORK_DIR/eyes" || fail "could not copy the vision fixture"
python3 - "$WORK_DIR/eyes/config.json" <<'EOF' || fail "could not make the vision fixture"
import json, sys
config = json.load(open(sys.argv[1]))
config["vision_config"] = {"depth": 2}
json.dump(config, open(sys.argv[1], "w"))
EOF
echo '{}' >"$WORK_DIR/eyes/preprocessor_config.json"
python3 - "$WORK_DIR/work" <<'EOF' || fail "could not write the pictures"
import base64, os, sys
png = base64.b64decode("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQ"
                       "DwAEhQGAhKmMIQAAAABJRU5ErkJggg==")
open(os.path.join(sys.argv[1], "photo.png"), "wb").write(png)
open(os.path.join(sys.argv[1], "chart.png"), "wb").write(png + b"\0\0\0")
EOF
PHOTO_BYTES="$(wc -c <"$WORK_DIR/work/photo.png" | tr -d ' ')"
"$APOGEE_BIN" config add-backend eyes --type mlx --model-path "$WORK_DIR/eyes" >/dev/null \
    || fail "add-backend eyes"
cat >"$STUB_MLX_REPLIES" <<'EOF'
{"rules": [["<|image|>", "I see {images}."],
           ["picture", "From its description: a small picture."]],
 "default": "A title"}
EOF
: >"$STUB_MLX_RECORD"

# Without mlx-vlm: the doctor says the vision model cannot see and names the
# fix, and a picture attached to it is refused naming the ways that would.
"$APOGEE_BIN" check --no-color >"$WORK_DIR/check-vision.txt" 2>&1
perl -0pe 's/\n {6,}(?!run:)/ /g' "$WORK_DIR/check-vision.txt" >"$WORK_DIR/check-vision-joined.txt"
grep -q "warn  vision  mlx-vlm is not installed in .*, so eyes cannot read images" \
    "$WORK_DIR/check-vision-joined.txt" \
    || fail "check did not warn about mlx-vlm: $(cat "$WORK_DIR/check-vision.txt")"
grep -q "apogee train setup --with mlx-vlm" "$WORK_DIR/check-vision.txt" \
    || fail "check did not name the vision fix"
if (cd "$WORK_DIR/work" && "$APOGEE_BIN" complete -m eyes --image photo.png "What is it?" \
        >"$WORK_DIR/out.txt" 2>"$WORK_DIR/err.txt"); then
    fail "a picture went to a vision model with no mlx-vlm: $(cat "$WORK_DIR/out.txt")"
fi
grep -q "an mlx backend over a vision model with mlx-vlm installed" "$WORK_DIR/err.txt" \
    || fail "the refusal did not name the mlx way: $(cat "$WORK_DIR/err.txt")"
grep -q "^vlm " "$STUB_MLX_RECORD" && fail "mlx-vlm was asked for while not installed"

# With mlx-vlm: the doctor passes on its files, and a chat on the vision
# model reads the picture as it is -- one load, through mlx-vlm.
mlx_fake_vlm "$WORK_DIR" "$MLX_STUBS"
"$APOGEE_BIN" check --no-color >"$WORK_DIR/check-vision.txt" 2>&1
grep -q "ok   vision  mlx-vlm 0.0-vlm-stub is present" "$WORK_DIR/check-vision.txt" \
    || fail "check did not report mlx-vlm: $(cat "$WORK_DIR/check-vision.txt")"
"$APOGEE_BIN" models info eyes >"$WORK_DIR/info.txt" 2>&1
grep -q "vision:       reads images as they are -- mlx-vlm 0.0-vlm-stub" "$WORK_DIR/info.txt" \
    || fail "models info did not say the model reads images: $(cat "$WORK_DIR/info.txt")"
(cd "$WORK_DIR/work" && printf 'What is in the picture?\n/exit\n' \
    | "$APOGEE_BIN" chat -m eyes --image photo.png >"$WORK_DIR/vision.txt" 2>&1) \
    || fail "the vision chat failed: $(cat "$WORK_DIR/vision.txt")"
grep -q "I see 1 image: $PHOTO_BYTES bytes .png." "$WORK_DIR/vision.txt" \
    || fail "the picture did not reach the model as it is: $(cat "$WORK_DIR/vision.txt")"
grep -q "read as it is with your next message" "$WORK_DIR/vision.txt" \
    || fail "the attachment did not say it is read as it is: $(cat "$WORK_DIR/vision.txt")"
[ "$(grep -c '^vlm load ' "$STUB_MLX_RECORD")" -eq 1 ] \
    || fail "expected one load through mlx-vlm, got: $(grep 'load ' "$STUB_MLX_RECORD")"
grep -q "^vlm generate images=1 " "$STUB_MLX_RECORD" \
    || fail "no turn carried the picture: $(cat "$STUB_MLX_RECORD")"
no_driver_left

# A text model with the vision model as its vision role: the picture is
# described there, and the chat model reads the description, honestly said.
"$APOGEE_BIN" config set-default-vision eyes >/dev/null || fail "set-default-vision"
: >"$STUB_MLX_RECORD"
(cd "$WORK_DIR/work" && printf 'What is in the picture?\n/exit\n' \
    | "$APOGEE_BIN" chat -m mlx --image chart.png >"$WORK_DIR/helper.txt" 2>&1) \
    || fail "the helper chat failed: $(cat "$WORK_DIR/helper.txt")"
grep -q "described by eyes" "$WORK_DIR/helper.txt" \
    || fail "the vision role did not describe the picture: $(cat "$WORK_DIR/helper.txt")"
grep -q "read as it is" "$WORK_DIR/helper.txt" \
    && fail "a text model was said to read the picture as it is"
grep -q "From its description: a small picture." "$WORK_DIR/helper.txt" \
    || fail "the text model never answered: $(cat "$WORK_DIR/helper.txt")"
grep -q "^vlm generate images=1 " "$STUB_MLX_RECORD" \
    || fail "the vision role was never shown the picture: $(cat "$STUB_MLX_RECORD")"
no_driver_left
unset STUB_MLX_RECORD

# --- the training shortcut (27c): promoted with --target mlx, then asked ------
# The mock trainer (no Python) fuses a whole model directory; promotion
# registers it as it is -- no GGUF made -- and the driver loads it from the
# store's mlx/ row.
STUDENT="$WORK_DIR/models/tiny/safetensors/aaaaaaaaaaaa"
mkdir -p "$STUDENT"
cp "$WORK_DIR/model/config.json" "$WORK_DIR/model/tokenizer_config.json" "$STUDENT/" \
    || fail "could not lay out the student"
printf 'weights' >"$STUDENT/model.safetensors"
"$APOGEE_BIN" datasets create starter >/dev/null 2>&1 || fail "datasets create"
"$APOGEE_BIN" train run tiny --dataset starter --trainer mock --iters 3 </dev/null \
    >/dev/null 2>"$WORK_DIR/run.err" || fail "train run: $(cat "$WORK_DIR/run.err")"
RUN="$(ls "$WORK_DIR/training/runs" | head -1)"
printf '{"prompt": "say hello", "expected": "hello"}\n' >"$WORK_DIR/suite.jsonl"
"$APOGEE_BIN" train eval "$RUN" --suite "$WORK_DIR/suite.jsonl" </dev/null >/dev/null 2>&1 \
    || fail "train eval"
"$APOGEE_BIN" train promote "$RUN" --as tuned --target mlx </dev/null >"$WORK_DIR/promote.txt" 2>&1 \
    || fail "train promote --target mlx: $(cat "$WORK_DIR/promote.txt")"
grep -q "promoted to new backend tuned -> v1" "$WORK_DIR/promote.txt" \
    || fail "the promote said: $(cat "$WORK_DIR/promote.txt")"
grep -q "no GGUF conversion ran" "$WORK_DIR/promote.txt" \
    || fail "the promote did not say no conversion ran"
TUNED="$("$APOGEE_BIN" config get backends.tuned.model_path)"
case "$TUNED" in
    "$WORK_DIR/models/tiny/mlx/"*) ;;
    *) fail "the tuned model is not in the store's mlx/ row: $TUNED" ;;
esac
[ "$("$APOGEE_BIN" config get backends.tuned.type)" = "mlx" ] || fail "tuned is not an mlx entry"
[ ! -d "$WORK_DIR/models/tiny/gguf" ] || fail "a GGUF was made: $(ls -R "$WORK_DIR/models/tiny/gguf")"
export STUB_MLX_RECORD="$WORK_DIR/record.txt"
: >"$STUB_MLX_RECORD"
printf '["tuned answer"]' >"$STUB_MLX_REPLIES"
"$APOGEE_BIN" complete -m tuned "hello" >"$WORK_DIR/out.txt" 2>"$WORK_DIR/err.txt" \
    || fail "the promoted model did not answer: $(cat "$WORK_DIR/err.txt")"
[ "$(cat "$WORK_DIR/out.txt")" = "tuned answer" ] \
    || fail "the promoted model said: $(cat "$WORK_DIR/out.txt")"
grep -q "^load $(basename "$TUNED")\$" "$STUB_MLX_RECORD" \
    || fail "the driver did not load the stored version: $(cat "$STUB_MLX_RECORD")"
no_driver_left

# Again through the mlx trainer's own fuse (train_mlx.py over a stub
# mlx_lm.fuse), in an environment holding the mlx set and NOT the convert
# set: an MLX promotion never asks for the GGUF converter.
printf '{"base_python": "python3", "created_at": "x", "sets": ["mlx"]}\n' \
    >"$WORK_DIR/training/venv/apogee.json"
"$APOGEE_BIN" train promote "$RUN" --as tuned --trainer mlx </dev/null >"$WORK_DIR/promote.txt" 2>&1 \
    || fail "train promote --trainer mlx: $(cat "$WORK_DIR/promote.txt")"
grep -q "updated backend tuned -> v2" "$WORK_DIR/promote.txt" \
    || fail "the second promote said: $(cat "$WORK_DIR/promote.txt")"
grep -q "Saved fused model" "$WORK_DIR/promote.txt" \
    || fail "the mlx trainer's fuse never ran: $(cat "$WORK_DIR/promote.txt")"
TUNED2="$("$APOGEE_BIN" config get backends.tuned.model_path)"
[ "$TUNED2" != "$TUNED" ] || fail "the second version did not repoint the entry"
[ -d "$TUNED" ] || fail "v1 was removed by a promote under the default retention"
: >"$STUB_MLX_RECORD"
"$APOGEE_BIN" complete -m tuned "hello" >"$WORK_DIR/out.txt" 2>"$WORK_DIR/err.txt" \
    || fail "v2 did not answer: $(cat "$WORK_DIR/err.txt")"
grep -q "^load $(basename "$TUNED2")\$" "$STUB_MLX_RECORD" \
    || fail "the driver did not load v2: $(cat "$STUB_MLX_RECORD")"
"$APOGEE_BIN" train rollback tuned >"$WORK_DIR/rollback.txt" 2>&1 \
    || fail "rollback: $(cat "$WORK_DIR/rollback.txt")"
[ "$("$APOGEE_BIN" config get backends.tuned.model_path)" = "$TUNED" ] \
    || fail "rollback did not repoint at v1"
[ ! -d "$WORK_DIR/models/tiny/gguf" ] || fail "a GGUF was made"
rm -f "$WORK_DIR/training/venv/apogee.json"
no_driver_left
unset STUB_MLX_RECORD

# --- the runtime taken away: refused at construction, the fix named -----------
mv "$WORK_DIR/training/venv" "$WORK_DIR/training/venv.away"
if "$APOGEE_BIN" complete -m mlx "hi" >"$WORK_DIR/out.txt" 2>"$WORK_DIR/err.txt"; then
    fail "an mlx entry with no environment answered"
fi
grep -q "apogee train setup --with mlx" "$WORK_DIR/err.txt" \
    || fail "the refusal did not name the fix: $(cat "$WORK_DIR/err.txt")"
mv "$WORK_DIR/training/venv.away" "$WORK_DIR/training/venv"

echo "mlx backend over its real driver - OK"
