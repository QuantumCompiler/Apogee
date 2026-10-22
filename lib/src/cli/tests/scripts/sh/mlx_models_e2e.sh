#!/usr/bin/env bash
# MLX model operations (27b) on the real binary: `models convert --mlx` with
# its REAL driver -- the seeded mlx_convert.py, run as a child of apogee --
# under stub mlx packages whose `mlx_lm.convert` writes a small MLX
# directory, so nothing is installed, downloaded or converted for real.
#
# What only the process can show: the converted model landing under the
# model's mlx/ by its hash and listed with its quantization and window;
# `check` reading it whole; a REAL Ctrl-C -- a SIGINT to apogee while its
# converter is mid-write -- ending the child and leaving the store exactly as
# it was, no staging and no driver behind; and `models delete` taking the
# directory whole. Since 53f a set that sees, converted through the stub
# `mlx_vlm.convert` with its tower kept and recorded `vision`, then a backend
# over it answering a text turn and a picture through mlx-vlm, mlx-lm's
# loader never asked.
#
# Apple silicon only: the mlx runtime runs nowhere else (the in-process
# suite, `models_mlx_test`, covers the rest on every platform).

set -uo pipefail

APOGEE_BIN="${1:?usage: mlx_models_e2e.sh <apogee-binary> <work-dir> <mlx-stubs>}"
WORK_DIR="${2:?usage: mlx_models_e2e.sh <apogee-binary> <work-dir> <mlx-stubs>}"
MLX_STUBS="${3:?usage: mlx_models_e2e.sh <apogee-binary> <work-dir> <mlx-stubs>}"

. "$(dirname "$0")/mlx_fake_runtime.sh"
if ! mlx_fake_runtime_supported; then
    echo "mlx_models_e2e: needs Apple silicon and python3; skipping"
    exit 0
fi

. "$(dirname "$0")/sandbox.sh"
WORK_DIR="$(sandbox_dir "$WORK_DIR")"
mkdir -p "$WORK_DIR"
export APOGEE_HOME="$WORK_DIR"
unset ANTHROPIC_API_KEY OPENAI_API_KEY GEMINI_API_KEY GOOGLE_API_KEY HF_TOKEN
trap 'rm -rf "$WORK_DIR"' EXIT

fail() { echo "mlx_models_e2e: $*" >&2; exit 1; }

# Whether the doctor's report in file $1 holds the text $2, its wrapped rows
# joined. One process reads the file whole, never a pipe into `grep -q`:
# under pipefail that fails whenever grep exits before its writer's last
# write -- the writer killed by SIGPIPE, status 141 -- and once `check`'s
# report grew past perl's 8 KiB output buffer, that made these checks fail
# under load with the text right there (2026-10-20).
report_has() {
    perl -0ne 'BEGIN { $want = pop @ARGV; $found = 0 }
        s/\n  (?! )//g; $found ||= index($_, $want) >= 0;
        END { $? = $found ? 0 : 1 }' "$1" "$2"
}

no_driver_left() {
    if pgrep -f "$WORK_DIR/training/scripts/mlx_convert.py" >/dev/null 2>&1; then
        fail "a converter outlived its apogee: $(pgrep -fl "$WORK_DIR/training/scripts/mlx_convert.py")"
    fi
}

"$APOGEE_BIN" config init >/dev/null || fail "config init"
mlx_fake_runtime "$WORK_DIR" "$MLX_STUBS"
"$APOGEE_BIN" check --fix >/dev/null 2>&1 || true
[ -f "$WORK_DIR/training/scripts/mlx_convert.py" ] || fail "check --fix did not seed the converter"

# --- a full-weight snapshot in the store: config, tokenizer, one shard --------
SNAPSHOT="$WORK_DIR/models/org--m/safetensors/aaaaaaaaaaaa"
python3 - "$SNAPSHOT" <<'EOF' || fail "could not write the fixture snapshot"
import json, os, struct, sys
d = sys.argv[1]
os.makedirs(d)
json.dump({"architectures": ["LlamaForCausalLM"], "model_type": "llama",
           "max_position_embeddings": 131072, "torch_dtype": "bfloat16"},
          open(os.path.join(d, "config.json"), "w"))
json.dump({"model": {"type": "BPE"}}, open(os.path.join(d, "tokenizer.json"), "w"))
json.dump({"chat_template": "{{ messages }}"}, open(os.path.join(d, "tokenizer_config.json"), "w"))
header = json.dumps({"__metadata__": {"format": "pt"},
                     "w": {"dtype": "F16", "shape": [64], "data_offsets": [0, 128]}}).encode()
with open(os.path.join(d, "model.safetensors"), "wb") as out:
    out.write(struct.pack("<Q", len(header)) + header + b"\0" * 128)
EOF

# --- convert --mlx: the real driver, the store's rule ---------------------------
"$APOGEE_BIN" models convert org--m --mlx >"$WORK_DIR/convert.txt" 2>"$WORK_DIR/convert.err" \
    || fail "convert --mlx failed: $(cat "$WORK_DIR/convert.err")"
SETS=("$WORK_DIR"/models/org--m/mlx/*/)
[ "${#SETS[@]}" -eq 1 ] || fail "expected one MLX set, found: ${SETS[*]}"
ID="$(basename "${SETS[0]}")"
case "$ID" in
    [0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]) ;;
    *) fail "the set is not named by a weight id: $ID" ;;
esac
grep -q "mlx model: llama, 4-bit (affine, group 64), 32768-token window (the default; trained for 131072)" \
    "$WORK_DIR/convert.txt" || fail "convert did not say what it made: $(cat "$WORK_DIR/convert.txt")"
if grep -q "\[INFO\]" "$WORK_DIR/convert.txt" "$WORK_DIR/convert.err"; then
    fail "the converter's own print reached a terminal stream"
fi
grep -q '"transform": "mlx_lm.convert 4bit"' "${SETS[0]}apogee-snapshot.json" \
    || fail "the set's record does not say how it was made"
no_driver_left

# --- listed and checked ---------------------------------------------------------
"$APOGEE_BIN" models list --no-color -q --all >"$WORK_DIR/list.txt" 2>&1 || fail "models list"
grep -q "org--m/mlx/$ID" "$WORK_DIR/list.txt" || fail "list does not show the set: $(cat "$WORK_DIR/list.txt")"
grep -q "llama, 4-bit (affine, group 64), 32768-token window" "$WORK_DIR/list.txt" \
    || fail "list does not show its quantization and window"
"$APOGEE_BIN" check --no-color -q >"$WORK_DIR/check.txt" 2>&1
report_has "$WORK_DIR/check.txt" "org--m/mlx/$ID  llama, MLX, 4-bit (affine, group 64), files whole" \
    || fail "check did not read the set whole: $(cat "$WORK_DIR/check.txt")"
report_has "$WORK_DIR/check.txt" "mlx_convert.py matches the shipped copy" \
    || fail "check did not report the conversion driver"

# --- a real Ctrl-C mid-conversion: nothing written, nothing left ---------------
BEFORE="$(cd "$WORK_DIR/models" && find . | sort)"
STUB_MLX_CONVERT_HANG=1 "$APOGEE_BIN" models convert org--m --mlx --type 8bit \
    >"$WORK_DIR/cancel.txt" 2>"$WORK_DIR/cancel.err" &
PID=$!
for _ in $(seq 1 200); do
    ls "$WORK_DIR"/models/org--m/mlx/.incoming-*/model-00001-of-00002.safetensors >/dev/null 2>&1 && break
    sleep 0.05
done
ls "$WORK_DIR"/models/org--m/mlx/.incoming-*/model-00001-of-00002.safetensors >/dev/null 2>&1 \
    || { kill -KILL "$PID" 2>/dev/null; fail "the converter never started writing: $(cat "$WORK_DIR/cancel.err")"; }
kill -INT "$PID"
wait "$PID"
CODE=$?
[ "$CODE" -eq 130 ] || fail "Ctrl-C exited $CODE, not 130: $(cat "$WORK_DIR/cancel.err")"
grep -q "cancelled -- nothing was written" "$WORK_DIR/cancel.err" \
    || fail "Ctrl-C was not said: $(cat "$WORK_DIR/cancel.err")"
AFTER="$(cd "$WORK_DIR/models" && find . | sort)"
[ "$BEFORE" = "$AFTER" ] || fail "Ctrl-C left the store changed:
before:
$BEFORE
after:
$AFTER"
no_driver_left

# --- deleted whole ----------------------------------------------------------------
"$APOGEE_BIN" models delete "org--m/mlx/$ID" --yes >"$WORK_DIR/delete.txt" 2>&1 \
    || fail "delete failed: $(cat "$WORK_DIR/delete.txt")"
[ ! -e "$WORK_DIR/models/org--m/mlx" ] || fail "delete left the set behind"
[ -f "$SNAPSHOT/model.safetensors" ] || fail "delete took the snapshot too"

# --- a set that sees (53f): built whole through mlx-vlm, every turn through it ---
# The stub mlx-vlm beside the stub mlx-lm (what `train setup --with mlx-vlm`
# installs), and a Qwen3-VL-shaped snapshot: a vision tower in its
# configuration, the processor that feeds it beside it.
mlx_fake_vlm "$WORK_DIR" "$MLX_STUBS"
VL="$WORK_DIR/models/org--vl/safetensors/bbbbbbbbbbbb"
python3 - "$VL" <<'EOF' || fail "could not write the towered snapshot"
import json, os, struct, sys
d = sys.argv[1]
os.makedirs(d)
json.dump({"architectures": ["Qwen3VLForConditionalGeneration"], "model_type": "qwen3_vl",
           "text_config": {"model_type": "qwen3_vl_text", "max_position_embeddings": 262144},
           "vision_config": {"model_type": "qwen3_vl", "depth": 27, "hidden_size": 1152}},
          open(os.path.join(d, "config.json"), "w"))
json.dump({}, open(os.path.join(d, "preprocessor_config.json"), "w"))
json.dump({"model": {"type": "BPE"}}, open(os.path.join(d, "tokenizer.json"), "w"))
json.dump({"chat_template": "{{ messages }}"}, open(os.path.join(d, "tokenizer_config.json"), "w"))
header = json.dumps({"__metadata__": {"format": "pt"},
                     "w": {"dtype": "F16", "shape": [64], "data_offsets": [0, 128]}}).encode()
with open(os.path.join(d, "model.safetensors"), "wb") as out:
    out.write(struct.pack("<Q", len(header)) + header + b"\0" * 128)
EOF
"$APOGEE_BIN" models convert org--vl --mlx >"$WORK_DIR/vl.txt" 2>"$WORK_DIR/vl.err" \
    || fail "convert --mlx of a set that sees failed: $(cat "$WORK_DIR/vl.err")"
grep -q "mlx-vlm's converter, its towers kept (vision_config)" "$WORK_DIR/vl.txt" \
    || fail "the conversion did not say mlx-vlm keeps the tower: $(cat "$WORK_DIR/vl.txt")"
if grep -q "\[INFO\]" "$WORK_DIR/vl.txt" "$WORK_DIR/vl.err"; then
    fail "mlx-vlm's own print reached a terminal stream"
fi
VL_SETS=("$WORK_DIR"/models/org--vl/mlx/*/)
[ "${#VL_SETS[@]}" -eq 1 ] || fail "expected one MLX build of the set that sees, found: ${VL_SETS[*]}"
grep -q '"transform": "mlx_vlm.convert 4bit"' "${VL_SETS[0]}apogee-snapshot.json" \
    || fail "the record does not say mlx-vlm made it: $(cat "${VL_SETS[0]}apogee-snapshot.json")"
grep -q '"kind": "vision"' "${VL_SETS[0]}apogee-snapshot.json" \
    || fail "the record does not say vision: $(cat "${VL_SETS[0]}apogee-snapshot.json")"
no_driver_left

"$APOGEE_BIN" config add-backend vl --type mlx --model-path "${VL_SETS[0]%/}" >/dev/null \
    || fail "add-backend vl"
"$APOGEE_BIN" models info vl >"$WORK_DIR/vl-info.txt" 2>&1 || fail "models info vl"
grep -q "kind:         vision (recorded)" "$WORK_DIR/vl-info.txt" \
    || fail "models info did not say the kind: $(cat "$WORK_DIR/vl-info.txt")"
export STUB_MLX_REPLIES="$WORK_DIR/replies.json"
export STUB_MLX_RECORD="$WORK_DIR/record.txt"
printf '{"rules": [["<|image|>", "I see {images}."]], "default": "Hello there."}' \
    >"$STUB_MLX_REPLIES"

# A text turn: answered by mlx-vlm's model; mlx-lm's loader never sees it.
: >"$STUB_MLX_RECORD"
"$APOGEE_BIN" complete -m vl "Hello?" >"$WORK_DIR/turn.txt" 2>"$WORK_DIR/turn.err" \
    || fail "a text turn on the set mlx-vlm built failed: $(cat "$WORK_DIR/turn.err")"
grep -q "Hello there." "$WORK_DIR/turn.txt" \
    || fail "the text turn was not answered: $(cat "$WORK_DIR/turn.txt")"
[ "$(grep -c '^vlm load ' "$STUB_MLX_RECORD")" -eq 1 ] \
    || fail "expected one load through mlx-vlm: $(cat "$STUB_MLX_RECORD")"
grep -q '^vlm generate images=0 ' "$STUB_MLX_RECORD" \
    || fail "the text turn did not run through mlx-vlm: $(cat "$STUB_MLX_RECORD")"
if grep -q '^load ' "$STUB_MLX_RECORD"; then
    fail "mlx-lm loaded a directory mlx-vlm built: $(cat "$STUB_MLX_RECORD")"
fi

# A picture on it: read as it is, through the same.
mkdir -p "$WORK_DIR/work"
python3 - "$WORK_DIR/work/photo.png" <<'EOF' || fail "could not write the picture"
import base64, sys
open(sys.argv[1], "wb").write(base64.b64decode(
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg=="))
EOF
PHOTO_BYTES="$(wc -c <"$WORK_DIR/work/photo.png" | tr -d ' ')"
: >"$STUB_MLX_RECORD"
(cd "$WORK_DIR/work" && "$APOGEE_BIN" complete -m vl --image photo.png "What is it?" \
    >"$WORK_DIR/look.txt" 2>"$WORK_DIR/look.err") \
    || fail "a picture on the set mlx-vlm built failed: $(cat "$WORK_DIR/look.err")"
grep -q "I see 1 image: $PHOTO_BYTES bytes .png." "$WORK_DIR/look.txt" \
    || fail "the picture did not reach the model as it is: $(cat "$WORK_DIR/look.txt")"
grep -q '^vlm generate images=1 ' "$STUB_MLX_RECORD" \
    || fail "no turn carried the picture: $(cat "$STUB_MLX_RECORD")"
if pgrep -f "$WORK_DIR/training/scripts/mlx_generate.py" >/dev/null 2>&1; then
    fail "a driver outlived its apogee: $(pgrep -fl "$WORK_DIR/training/scripts/mlx_generate.py")"
fi

echo "mlx_models_e2e: ok"
