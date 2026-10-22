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
# loader never asked. Since 53h the user's embedding set, converted through
# the stub `mlx_embeddings.convert` and recorded `embedding`, registered by
# `config scan`, and named as the embedding role: a collection ingested and a
# retrieval turn answered on vectors from the real seeded `mlx_embed.py`, a
# chat on it refused, and another embedder's space demoting the collection to
# lexical. Since 53i the user's E4B assistant, a draft head: refused as GGUF
# and as a backend, converted through the stub `mlx_vlm.convert` to a drafter
# recorded beside its base, named as that base's entry's draft, and a turn
# answered through mlx-vlm's speculative path with the base's own answer --
# the same bytes as the turn without it. Since 53k the user's Wan2.2-TI2V-5B
# in Wan's original layout: refused while it lacks its checkpoints and while
# mlx-video is not installed, each refusal naming what is missing; then,
# whole with the stub mlx-video installed, converted through the real driver
# to an MLX video model recorded `video`, said so by `models info`,
# registered as nothing by `config scan`, the converter's prints on neither
# of apogee's streams.
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

# --- an embedding model (53h): built whole through mlx-embeddings, then the role on it ---
# M21's fixture of the user's Gemma-Embedding-300M: a bare Gemma3TextModel,
# then Pooling, two Dense layers and Normalize, by its own modules.json.
mlx_fake_embeddings "$WORK_DIR" "$MLX_STUBS"
EMB="$WORK_DIR/models/Gemma-Embedding-300M/safetensors/b6845e67fafb"
python3 - "$EMB" <<'EOF' || fail "could not write the embedding snapshot"
import json, os, struct, sys
d = sys.argv[1]
os.makedirs(os.path.join(d, "2_Dense"))
json.dump({"architectures": ["Gemma3TextModel"], "model_type": "gemma3_text",
           "hidden_size": 768}, open(os.path.join(d, "config.json"), "w"))
json.dump([{"idx": 0, "path": "", "type": "sentence_transformers.models.Transformer"},
           {"idx": 1, "path": "1_Pooling", "type": "sentence_transformers.models.Pooling"},
           {"idx": 2, "path": "2_Dense", "type": "sentence_transformers.models.Dense"},
           {"idx": 3, "path": "3_Dense", "type": "sentence_transformers.models.Dense"},
           {"idx": 4, "path": "4_Normalize", "type": "sentence_transformers.models.Normalize"}],
          open(os.path.join(d, "modules.json"), "w"))
json.dump({"model": {"type": "BPE"}}, open(os.path.join(d, "tokenizer.json"), "w"))
header = json.dumps({"__metadata__": {"format": "pt"},
                     "w": {"dtype": "F16", "shape": [64], "data_offsets": [0, 128]}}).encode()
for shard in ("model.safetensors", os.path.join("2_Dense", "model.safetensors")):
    with open(os.path.join(d, shard), "wb") as out:
        out.write(struct.pack("<Q", len(header)) + header + b"\0" * 128)
EOF
"$APOGEE_BIN" models info Gemma-Embedding-300M/safetensors/b6845e67fafb --output-format json \
    >"$WORK_DIR/emb-info.json" 2>&1 || fail "models info of the embedding set"
grep -q '"converts_to":\["gguf","mlx"\]' "$WORK_DIR/emb-info.json" \
    || fail "the embedding set was not offered MLX through mlx-embeddings: $(cat "$WORK_DIR/emb-info.json")"
"$APOGEE_BIN" models convert Gemma-Embedding-300M --mlx --type 8bit >"$WORK_DIR/emb.txt" \
    2>"$WORK_DIR/emb.err" || fail "convert --mlx of the embedding set failed: $(cat "$WORK_DIR/emb.err")"
grep -q "mlx-embeddings' converter, an embedding model built whole" "$WORK_DIR/emb.txt" \
    || fail "the conversion did not say mlx-embeddings built it: $(cat "$WORK_DIR/emb.txt")"
if grep -q "\[INFO\]" "$WORK_DIR/emb.txt" "$WORK_DIR/emb.err"; then
    fail "mlx-embeddings' own print reached a terminal stream"
fi
EMB_SETS=("$WORK_DIR"/models/Gemma-Embedding-300M/mlx/*/)
[ "${#EMB_SETS[@]}" -eq 1 ] || fail "expected one MLX build of the embedding set, found: ${EMB_SETS[*]}"
grep -q '"transform": "mlx_embeddings.convert 8bit"' "${EMB_SETS[0]}apogee-snapshot.json" \
    || fail "the record does not say mlx-embeddings made it: $(cat "${EMB_SETS[0]}apogee-snapshot.json")"
grep -q '"kind": "embedding"' "${EMB_SETS[0]}apogee-snapshot.json" \
    || fail "the record does not say embedding: $(cat "${EMB_SETS[0]}apogee-snapshot.json")"
no_driver_left

# Registered by the scan as an mlx entry, and named as the embedding role.
"$APOGEE_BIN" config scan --register >"$WORK_DIR/emb-scan.txt" 2>&1 || fail "config scan --register"
grep -q "added backend 'Gemma-Embedding-300M-8bit' -- an embedding model" "$WORK_DIR/emb-scan.txt" \
    || fail "the scan did not register the embedding model: $(cat "$WORK_DIR/emb-scan.txt")"
"$APOGEE_BIN" config set-default-embedding Gemma-Embedding-300M-8bit >/dev/null \
    || fail "set-default-embedding"
"$APOGEE_BIN" config add-backend mock --type mock >/dev/null || fail "add-backend mock"
mkdir -p "$WORK_DIR/corpus"
printf 'the zarquon widget sits on the warm mat\n' >"$WORK_DIR/corpus/widgets.txt"
printf 'dogs bark at the moon at night\n' >"$WORK_DIR/corpus/dogs.txt"
: >"$STUB_MLX_RECORD"
# A library that narrates its load: none of it may reach apogee's streams.
STUB_MLX_NOISE="embedder narrating its load" \
    "$APOGEE_BIN" embed ingest notes "$WORK_DIR/corpus" >"$WORK_DIR/ingest.txt" 2>"$WORK_DIR/ingest.err" \
    || fail "embed ingest on the MLX embedder failed: $(cat "$WORK_DIR/ingest.err")"
grep -q '^embeddings noise' "$STUB_MLX_RECORD" || fail "the stub never narrated its load"
if grep -q "embedder narrating its load" "$WORK_DIR/ingest.txt" "$WORK_DIR/ingest.err"; then
    fail "the embedding driver's stderr reached a terminal stream"
fi
grep -qF "vector(s) [mlx:${EMB_SETS[0]%/}]" "$WORK_DIR/ingest.txt" \
    || fail "the collection was not vectorised by the MLX embedder: $(cat "$WORK_DIR/ingest.txt")"
[ "$(grep -c '^embeddings load ' "$STUB_MLX_RECORD")" -eq 1 ] \
    || fail "expected one load through mlx-embeddings: $(cat "$STUB_MLX_RECORD")"
if grep -q '^load \|^vlm load ' "$STUB_MLX_RECORD"; then
    fail "a chat runtime loaded the embedding model: $(cat "$STUB_MLX_RECORD")"
fi

# A retrieval turn on it: the question embedded by the same backend.
"$APOGEE_BIN" complete -m mock -v --rag notes --retriever vector "what sits on the zarquon mat?" \
    >"$WORK_DIR/rag.txt" 2>&1 || fail "a retrieval turn on the MLX embedder failed: $(cat "$WORK_DIR/rag.txt")"
grep -q "\[vector\]" "$WORK_DIR/rag.txt" \
    || fail "the turn did not search by vector: $(cat "$WORK_DIR/rag.txt")"

# A chat on the embedding entry is refused, saying what it is.
if "$APOGEE_BIN" complete -m Gemma-Embedding-300M-8bit "hello?" >"$WORK_DIR/chat.txt" 2>&1; then
    fail "a chat on the embedding entry was answered: $(cat "$WORK_DIR/chat.txt")"
fi
grep -q "answers no chat or completion" "$WORK_DIR/chat.txt" \
    || fail "the refusal did not say what the entry is: $(cat "$WORK_DIR/chat.txt")"

# Another embedder's space: the collection is another model's to it, demoted
# to lexical with the re-ingest hint -- never queried across the two.
"$APOGEE_BIN" config add-backend other-embed --type mock --embedding-model Gemma-Embedding-300M-8bit \
    >/dev/null || fail "add-backend other-embed"
"$APOGEE_BIN" config set-default-embedding other-embed >/dev/null || fail "set-default-embedding other"
"$APOGEE_BIN" embed query notes "zarquon mat" >"$WORK_DIR/demoted.txt" 2>&1 \
    || fail "embed query under another embedder failed: $(cat "$WORK_DIR/demoted.txt")"
grep -q "\[lexical\]" "$WORK_DIR/demoted.txt" \
    || fail "another space did not fall to lexical: $(cat "$WORK_DIR/demoted.txt")"
grep -q "re-run" "$WORK_DIR/demoted.txt" \
    || fail "the lexical fallback carried no re-ingest hint: $(cat "$WORK_DIR/demoted.txt")"
if pgrep -f "$WORK_DIR/training/scripts/mlx_embed.py" >/dev/null 2>&1; then
    fail "an embedding driver outlived its apogee: $(pgrep -fl "$WORK_DIR/training/scripts/mlx_embed.py")"
fi

# --- a draft head (53i): a drafter beside its base, the answer the base's ---------
# The user's Gemma4-E4B-Instruct and its assistant, as their configs lay them
# out: the assistant reads a 2560-wide gemma4 model's hidden states.
python3 - "$WORK_DIR/models" <<'EOF' || fail "could not write the E4B sets"
import json, os, struct, sys
models = sys.argv[1]
sets = {
    "Gemma4-E4B-Instruct/safetensors/cccccccccccc": {
        "architectures": ["Gemma4ForConditionalGeneration"], "model_type": "gemma4",
        "audio_config": {"model_type": "gemma4_audio", "hidden_size": 1536},
        "vision_config": {"model_type": "gemma4_vision", "hidden_size": 768},
        "text_config": {"model_type": "gemma4_text", "hidden_size": 2560,
                        "max_position_embeddings": 131072}},
    "Gemma4-E4B-Assistant/safetensors/dddddddddddd": {
        "architectures": ["Gemma4AssistantForCausalLM"], "model_type": "gemma4_assistant",
        "backbone_hidden_size": 2560, "use_ordered_embeddings": True,
        "text_config": {"model_type": "gemma4_text", "hidden_size": 256,
                        "num_hidden_layers": 4}},
}
header = json.dumps({"__metadata__": {"format": "pt"},
                     "w": {"dtype": "F16", "shape": [64], "data_offsets": [0, 128]}}).encode()
for relative, config in sets.items():
    d = os.path.join(models, relative)
    os.makedirs(d)
    json.dump(config, open(os.path.join(d, "config.json"), "w"))
    json.dump({"model": {"type": "BPE"}}, open(os.path.join(d, "tokenizer.json"), "w"))
    json.dump({"chat_template": "{{ messages }}"},
              open(os.path.join(d, "tokenizer_config.json"), "w"))
    if "vision_config" in config:
        json.dump({}, open(os.path.join(d, "preprocessor_config.json"), "w"))
    with open(os.path.join(d, "model.safetensors"), "wb") as out:
        out.write(struct.pack("<Q", len(header)) + header + b"\0" * 128)
EOF
# Refused as before: a GGUF of it, and a backend of it.
if "$APOGEE_BIN" models convert Gemma4-E4B-Assistant >"$WORK_DIR/head.txt" 2>&1; then
    fail "a GGUF of the draft head was made: $(cat "$WORK_DIR/head.txt")"
fi
grep -q "is a draft head (Gemma4AssistantForCausalLM)" "$WORK_DIR/head.txt" \
    || fail "the GGUF refusal did not say what it is: $(cat "$WORK_DIR/head.txt")"
if "$APOGEE_BIN" models convert Gemma4-E4B-Assistant --mlx --register >"$WORK_DIR/head.txt" 2>&1; then
    fail "the draft head was registered as a backend: $(cat "$WORK_DIR/head.txt")"
fi
grep -q "which no backend runs" "$WORK_DIR/head.txt" \
    || fail "the registration refusal did not say why: $(cat "$WORK_DIR/head.txt")"
[ ! -d "$WORK_DIR/models/Gemma4-E4B-Assistant/mlx" ] || fail "a refused conversion staged something"

# Its base, mlx-vlm's build, registered; the drafter converted beside it.
"$APOGEE_BIN" models convert Gemma4-E4B-Instruct --mlx --register --base-name e4b \
    >"$WORK_DIR/e4b.txt" 2>&1 || fail "convert --mlx --register of the E4B: $(cat "$WORK_DIR/e4b.txt")"
"$APOGEE_BIN" models convert Gemma4-E4B-Assistant --mlx >"$WORK_DIR/drafter.txt" 2>&1 \
    || fail "convert --mlx of the draft head failed: $(cat "$WORK_DIR/drafter.txt")"
grep -q "kind: drafter (recorded)" "$WORK_DIR/drafter.txt" \
    || fail "the conversion did not say it made a drafter: $(cat "$WORK_DIR/drafter.txt")"
DRAFTERS=("$WORK_DIR"/models/Gemma4-E4B-Assistant/mlx/*/)
[ "${#DRAFTERS[@]}" -eq 1 ] || fail "expected one drafter, found: ${DRAFTERS[*]}"
DRAFTER="${DRAFTERS[0]%/}"
grep -q '"kind": "drafter"' "$DRAFTER/apogee-snapshot.json" \
    || fail "the record does not say drafter: $(cat "$DRAFTER/apogee-snapshot.json")"
grep -q '"Gemma4-E4B-Instruct"' "$DRAFTER/apogee-snapshot.json" \
    || fail "the record does not name its base: $(cat "$DRAFTER/apogee-snapshot.json")"
"$APOGEE_BIN" config scan --register >"$WORK_DIR/drafter-scan.txt" 2>&1 || fail "config scan"
if grep -q "added backend 'Gemma4-E4B-Assistant" "$WORK_DIR/drafter-scan.txt"; then
    fail "the scan registered the drafter: $(cat "$WORK_DIR/drafter-scan.txt")"
fi
no_driver_left

# A turn on the base alone, then beside its drafter: the same answer.
: >"$STUB_MLX_RECORD"
"$APOGEE_BIN" complete -m e4b-4bit "Explain speculative decoding." >"$WORK_DIR/alone.txt" \
    2>"$WORK_DIR/alone.err" || fail "a turn on the E4B failed: $(cat "$WORK_DIR/alone.err")"
if grep -q '^vlm draft' "$STUB_MLX_RECORD"; then
    fail "a turn with no drafter drafted: $(cat "$STUB_MLX_RECORD")"
fi
"$APOGEE_BIN" config set-backend e4b-4bit --draft Gemma4-E4B-Assistant >"$WORK_DIR/pair.txt" 2>&1 \
    || fail "set-backend --draft did not pair them: $(cat "$WORK_DIR/pair.txt")"
grep -qF "set draft; drafter $DRAFTER beside " "$WORK_DIR/pair.txt" \
    || fail "the pairing was not said: $(cat "$WORK_DIR/pair.txt")"
: >"$STUB_MLX_RECORD"
"$APOGEE_BIN" complete -m e4b-4bit "Explain speculative decoding." >"$WORK_DIR/drafted.txt" \
    2>"$WORK_DIR/drafted.err" || fail "a drafted turn failed: $(cat "$WORK_DIR/drafted.err")"
cmp -s "$WORK_DIR/alone.txt" "$WORK_DIR/drafted.txt" \
    || fail "the drafted answer differs from the base's: $(cat "$WORK_DIR/alone.txt") / $(cat "$WORK_DIR/drafted.txt")"
grep -q "^vlm drafter $(basename "$DRAFTER") kind=mtp type=gemma4_assistant" "$STUB_MLX_RECORD" \
    || fail "the drafter was not loaded as mtp: $(cat "$STUB_MLX_RECORD")"
grep -q "^vlm draft $(basename "$DRAFTER") kind=mtp " "$STUB_MLX_RECORD" \
    || fail "the turn did not run the speculative path: $(cat "$STUB_MLX_RECORD")"
if pgrep -f "$WORK_DIR/training/scripts/mlx_generate.py" >/dev/null 2>&1; then
    fail "a driver outlived its apogee: $(pgrep -fl "$WORK_DIR/training/scripts/mlx_generate.py")"
fi

# --- Wan's original layout (53k): through mlx-video to an MLX video model ------------
# The user's set as 53j names it: its own config, a transformer shard -- small
# fake bytes -- and, once whole, the two root checkpoints.
WAN="$WORK_DIR/models/Wan-AI--Wan2.2-TI2V-5B/safetensors/6ef9b00b8b83"
WAN_HANDLE="Wan-AI--Wan2.2-TI2V-5B/safetensors/6ef9b00b8b83"
python3 - "$WAN" <<'EOF' || fail "could not write the Wan set"
import json, os, sys
d = sys.argv[1]
os.makedirs(d)
json.dump({"_class_name": "WanModel", "_diffusers_version": "0.33.0", "dim": 3072,
           "eps": 1e-06, "ffn_dim": 14336, "freq_dim": 256, "in_dim": 48, "model_type": "ti2v",
           "num_heads": 24, "num_layers": 30, "out_dim": 48, "text_len": 512},
          open(os.path.join(d, "config.json"), "w"), indent=2)
with open(os.path.join(d, "diffusion_pytorch_model-00001-of-00001.safetensors"), "wb") as out:
    out.write(b"fake shard bytes, never weights\n")
EOF
# Lacking its checkpoints, and no mlx-video: both named, nothing run.
if "$APOGEE_BIN" models convert "$WAN_HANDLE" --mlx >"$WORK_DIR/wan.txt" 2>&1; then
    fail "a Wan set lacking its checkpoints converted: $(cat "$WORK_DIR/wan.txt")"
fi
grep -q "missing models_t5_umt5-xxl-enc-bf16.pth and Wan2.2_VAE.pth" "$WORK_DIR/wan.txt" \
    || fail "the refusal did not name the missing checkpoints: $(cat "$WORK_DIR/wan.txt")"
grep -q "and is not installed in Apogee's Python environment -- 'apogee train setup --with mlx-video'" \
    "$WORK_DIR/wan.txt" || fail "the refusal did not name mlx-video's setup: $(cat "$WORK_DIR/wan.txt")"
# Whole, mlx-video still not installed: mlx-video alone named, MLX not offered.
printf 'fake checkpoint bytes\n' >"$WAN/models_t5_umt5-xxl-enc-bf16.pth"
printf 'fake checkpoint bytes\n' >"$WAN/Wan2.2_VAE.pth"
if "$APOGEE_BIN" models convert "$WAN_HANDLE" --mlx >"$WORK_DIR/wan.txt" 2>&1; then
    fail "a Wan set converted with no mlx-video: $(cat "$WORK_DIR/wan.txt")"
fi
grep -q "complete -- its transformer beside models_t5_umt5-xxl-enc-bf16.pth and Wan2.2_VAE.pth; mlx-video converts it to an MLX video model (--mlx), and is not installed" \
    "$WORK_DIR/wan.txt" || fail "the refusal of the whole set did not name mlx-video alone: $(cat "$WORK_DIR/wan.txt")"
"$APOGEE_BIN" models info "$WAN_HANDLE" --output-format json >"$WORK_DIR/wan-info.json" 2>&1 \
    || fail "models info of the Wan set"
grep -q '"converts_to":\[\]' "$WORK_DIR/wan-info.json" \
    || fail "the whole set was offered a conversion with no mlx-video: $(cat "$WORK_DIR/wan-info.json")"
[ ! -e "$WORK_DIR/models/Wan-AI--Wan2.2-TI2V-5B/mlx" ] || fail "a refusal left an MLX directory"

# mlx-video installed: offered MLX, converted through the real seeded driver.
mlx_fake_video "$WORK_DIR" "$MLX_STUBS"
"$APOGEE_BIN" models info "$WAN_HANDLE" --output-format json >"$WORK_DIR/wan-info.json" 2>&1 \
    || fail "models info of the Wan set"
grep -q '"converts_to":\["mlx"\]' "$WORK_DIR/wan-info.json" \
    || fail "the whole set was not offered MLX through mlx-video: $(cat "$WORK_DIR/wan-info.json")"
"$APOGEE_BIN" models convert "$WAN_HANDLE" --mlx >"$WORK_DIR/wan.txt" 2>"$WORK_DIR/wan.err" \
    || fail "convert --mlx of the whole Wan set failed: $(cat "$WORK_DIR/wan.err")"
grep -q "mlx-video's converter, Wan's original layout (WanModel) built into an MLX video model" \
    "$WORK_DIR/wan.txt" || fail "the conversion did not say mlx-video built it: $(cat "$WORK_DIR/wan.txt")"
grep -q "kind: video (recorded)" "$WORK_DIR/wan.txt" \
    || fail "the conversion did not say it made a video model: $(cat "$WORK_DIR/wan.txt")"
if grep -q "Converting\|Auto-detected" "$WORK_DIR/wan.txt" "$WORK_DIR/wan.err"; then
    fail "mlx-video's own print reached a terminal stream"
fi
WAN_SETS=("$WORK_DIR"/models/Wan-AI--Wan2.2-TI2V-5B/mlx/*/)
[ "${#WAN_SETS[@]}" -eq 1 ] || fail "expected one MLX build of the Wan set, found: ${WAN_SETS[*]}"
for part in config.json t5_encoder.safetensors vae.safetensors model.safetensors; do
    [ -f "${WAN_SETS[0]}$part" ] || fail "the MLX video model lacks $part"
done
grep -q '"transform": "mlx_video.models.wan_2.convert 4bit"' "${WAN_SETS[0]}apogee-snapshot.json" \
    || fail "the record does not say mlx-video made it: $(cat "${WAN_SETS[0]}apogee-snapshot.json")"
grep -q '"kind": "video"' "${WAN_SETS[0]}apogee-snapshot.json" \
    || fail "the record does not say video: $(cat "${WAN_SETS[0]}apogee-snapshot.json")"
no_driver_left
WAN_MLX="Wan-AI--Wan2.2-TI2V-5B/mlx/$(basename "${WAN_SETS[0]}")"
"$APOGEE_BIN" models info "$WAN_MLX" -q >"$WORK_DIR/wan-mlx.txt" 2>&1 \
    || fail "models info of the MLX video model: $(cat "$WORK_DIR/wan-mlx.txt")"
grep -q "kind:         video (recorded) -- a video model" "$WORK_DIR/wan-mlx.txt" \
    || fail "models info did not say it is a video model: $(cat "$WORK_DIR/wan-mlx.txt")"
grep -q "files:        whole -- config.json, the T5 text encoder" "$WORK_DIR/wan-mlx.txt" \
    || fail "models info did not read it whole: $(cat "$WORK_DIR/wan-mlx.txt")"
# Never a chat backend: the scan says it apart, and registers nothing of it.
"$APOGEE_BIN" config scan --register >"$WORK_DIR/wan-scan.txt" 2>&1 || fail "config scan --register"
grep -q "not registered -- a video model" "$WORK_DIR/wan-scan.txt" \
    || fail "the scan did not say the video model apart: $(cat "$WORK_DIR/wan-scan.txt")"
if grep -rqF "$(basename "${WAN_SETS[0]}")" "$WORK_DIR/config/"; then
    fail "the scan registered the video model as a backend: $(cat "$WORK_DIR/wan-scan.txt")"
fi

echo "mlx_models_e2e: ok"
