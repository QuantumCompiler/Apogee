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
# directory whole.
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

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR"
export APOGEE_HOME="$WORK_DIR"
unset ANTHROPIC_API_KEY OPENAI_API_KEY GEMINI_API_KEY GOOGLE_API_KEY HF_TOKEN
trap 'rm -rf "$WORK_DIR"' EXIT

fail() { echo "mlx_models_e2e: $*" >&2; exit 1; }

# The doctor's report wraps long rows; read it joined.
joined() { perl -0pe 's/\n  (?! )//g' "$1"; }

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
joined "$WORK_DIR/check.txt" | grep -q "org--m/mlx/$ID  llama, MLX, 4-bit (affine, group 64), files whole" \
    || fail "check did not read the set whole: $(cat "$WORK_DIR/check.txt")"
joined "$WORK_DIR/check.txt" | grep -q "mlx_convert.py matches the shipped copy" \
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

echo "mlx_models_e2e: ok"
