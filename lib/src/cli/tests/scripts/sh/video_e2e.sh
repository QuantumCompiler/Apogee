#!/bin/sh
# `apogee video generate` (49c) on the real binary, with nothing installed:
# a fake runtime -- the venv's interpreter a shim exec'ing the host's
# python3 with stub `torch` and `diffusers` (tests/business/video/scripts/
# stub_video) in its site-packages, where the ladder looks for them -- so the
# REAL seeded driver runs; a fake `ffmpeg` first on PATH that writes the
# clip; and a pipeline in the store built here, never weights.
#
# Phases: the pointer refused until set, then set; a generation as a JSON
# document on stdout alone, its narration on stderr, its sidecar beside the
# clip and equal to the document; a given seed kept; the caps and an
# existing output refused with the output untouched; a failing generation
# and a real Ctrl-C leaving no clip, no sidecar and no working directory; no
# listening socket anywhere in the tree while the driver runs; a library's
# print on neither of apogee's streams; the loop (49d) -- a draft, the final
# from it over the same seed and frame, a continuation from the final's last
# frame, a clip with no record refused; and on a pipe, a missing runtime
# refused naming the command that installs it.
#
# POSIX only. Skips (77) without python3.
set -eu

APOGEE_BIN="${1:?usage: video_e2e.sh <apogee-binary> <work-dir> <stubs-dir>}"
WORK_DIR="${2:?usage: video_e2e.sh <apogee-binary> <work-dir> <stubs-dir>}"
STUBS="${3:?usage: video_e2e.sh <apogee-binary> <work-dir> <stubs-dir>}"

if ! command -v python3 >/dev/null 2>&1; then
    echo "video_e2e: python3 not found (the driver runs under it); skipping"
    exit 77
fi

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR/bin" "$WORK_DIR/out"
export APOGEE_HOME="$WORK_DIR/home"
trap 'rm -rf "$WORK_DIR"' EXIT

fail() { echo "video_e2e: $*" >&2; exit 1; }
field() { python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); print(eval("d" + sys.argv[2]))' "$1" "$2"; }

# --- the install, the fake runtime, the fake ffmpeg, a pipeline -------------
"$APOGEE_BIN" config init </dev/null >/dev/null || fail "config init"
"$APOGEE_BIN" check --fix </dev/null >/dev/null 2>&1 || true
[ -f "$APOGEE_HOME/training/scripts/video_generate.py" ] || fail "check --fix did not seed the driver"

SITE="$APOGEE_HOME/training/venv/lib/python3.99/site-packages"
mkdir -p "$APOGEE_HOME/training/venv/bin" "$SITE"
cp -R "$STUBS/stub_video/." "$SITE/"
cat >"$APOGEE_HOME/training/venv/bin/python" <<EOF
#!/bin/sh
PYTHONPATH="$SITE" PYTHONDONTWRITEBYTECODE=1 exec python3 "\$@"
EOF
chmod +x "$APOGEE_HOME/training/venv/bin/python"

cat >"$WORK_DIR/bin/ffmpeg" <<'EOF'
#!/bin/sh
echo "ffmpeg: a banner nobody should see" >&2
out=''; for a in "$@"; do out="$a"; done
printf 'MP4 clip' > "$out"
EOF
cp "$WORK_DIR/bin/ffmpeg" "$WORK_DIR/bin/ffprobe"
chmod +x "$WORK_DIR/bin/ffmpeg" "$WORK_DIR/bin/ffprobe"
PATH="$WORK_DIR/bin:$PATH"
export PATH

PIPELINE="$APOGEE_HOME/models/org--clip/pipeline/aaaaaaaaaaaa"
python3 - "$PIPELINE" <<'EOF'
import json, os, struct, sys
root = sys.argv[1]
def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as out:
        out.write(data if isinstance(data, bytes) else data.encode())
def shard():
    header = json.dumps({"w": {"dtype": "F16", "shape": [1], "data_offsets": [0, 2]}}).encode()
    return struct.pack("<Q", len(header)) + header + b"\0\0"
write(f"{root}/model_index.json", json.dumps({
    "_class_name": "WanPipeline", "_diffusers_version": "0.35.0",
    "scheduler": ["diffusers", "UniPCMultistepScheduler"],
    "tokenizer": ["transformers", "T5TokenizerFast"],
    "transformer": ["diffusers", "WanTransformer3DModel"],
    "vae": ["diffusers", "AutoencoderKLWan"]}))
write(f"{root}/scheduler/scheduler_config.json", "{}")
write(f"{root}/tokenizer/tokenizer.json", "{}")
for component in ("transformer", "vae"):
    write(f"{root}/{component}/config.json", "{}")
    write(f"{root}/{component}/diffusion_pytorch_model.safetensors", shard())
EOF

generate() {
    # generate <name> [args...]: one run, stdout and stderr kept apart.
    name="$1"; shift
    set +e
    "$APOGEE_BIN" video generate "a heron lands on a pier at dusk" --duration 1 \
        --resolution 128x128 --steps 2 --fps 8 "$@" \
        </dev/null >"$WORK_DIR/$name.out" 2>"$WORK_DIR/$name.err"
    STATUS=$?
    set -e
}

# --- no pipeline named: refused, naming the pointer ------------------------
generate unset -o "$WORK_DIR/out/unset.mp4"
[ "$STATUS" -ne 0 ] || fail "a run with no pipeline named succeeded"
grep -q "apogee config set-default-video <pipeline>" "$WORK_DIR/unset.err" \
    || fail "the refusal did not name the pointer: $(cat "$WORK_DIR/unset.err")"
"$APOGEE_BIN" config set-default-video org--clip </dev/null >"$WORK_DIR/set.out" 2>&1 \
    || fail "set-default-video: $(cat "$WORK_DIR/set.out")"
grep -q "models.default_video = org--clip" "$WORK_DIR/set.out" || fail "the pointer was not said"

# --- a generation: the document on stdout alone, the narration on stderr ---
generate first -o "$WORK_DIR/out/first.mp4" --output-format json
[ "$STATUS" -eq 0 ] || fail "the generation failed ($STATUS): $(cat "$WORK_DIR/first.err")"
python3 -c 'import json,sys; json.load(open(sys.argv[1]))' "$WORK_DIR/first.out" \
    || fail "stdout is not one JSON document: $(cat "$WORK_DIR/first.out")"
[ "$(field "$WORK_DIR/first.out" '["object"]')" = "video.generation" ] || fail "not a video.generation document"
[ "$(field "$WORK_DIR/first.out" '["status"]')" = "done" ] || fail "not done: $(cat "$WORK_DIR/first.out")"
[ "$(field "$WORK_DIR/first.out" '["model"]["name"]')" = "org--clip/pipeline/aaaaaaaaaaaa" ] || fail "the pipeline"
[ "$(field "$WORK_DIR/first.out" '["seed_source"]')" = "generated" ] || fail "the seed was not drawn"
[ "$(field "$WORK_DIR/first.out" '["settings"]["frames"]')" = "9" ] || fail "1 s at 8 fps is not 9 frames"
[ "$(field "$WORK_DIR/first.out" '["device"]["device"]')" = "cpu" ] || fail "the device"
[ "$(cat "$WORK_DIR/out/first.mp4")" = "MP4 clip" ] || fail "the clip did not land"
SIDECAR="$WORK_DIR/out/first.mp4.json"
[ -f "$SIDECAR" ] || fail "no sidecar beside the clip"
python3 -c 'import json,sys; a=json.load(open(sys.argv[1])); b=json.load(open(sys.argv[2])); sys.exit(a != b)' \
    "$SIDECAR" "$WORK_DIR/first.out" || fail "the sidecar is not the document"
grep -q "\[video\] running on cpu (float32) -- expect hours per clip on the CPU" "$WORK_DIR/first.err" \
    || fail "the device and the scale were not said: $(cat "$WORK_DIR/first.err")"
grep -q "\[video\] step 2/2" "$WORK_DIR/first.err" || fail "the steps were not said"
for stream in out err; do
    if grep -q "a library's chatter\|noise on stderr\|nobody should see" "$WORK_DIR/first.$stream"; then
        fail "a library's output reached apogee's std$stream"
    fi
done
[ -z "$(ls -A "$APOGEE_HOME/cache/video")" ] || fail "a working directory was left"

# --- a given seed is the record's; the text form names the clip -------------
generate seeded -o "$WORK_DIR/out/seeded.mp4" --seed 7
[ "$STATUS" -eq 0 ] || fail "the seeded run failed: $(cat "$WORK_DIR/seeded.err")"
head -1 "$WORK_DIR/seeded.out" | grep -q "seeded.mp4$" || fail "the text form: $(cat "$WORK_DIR/seeded.out")"
grep -q "seed 7 -- " "$WORK_DIR/seeded.out" || fail "the seed was not said"
[ "$(field "$WORK_DIR/out/seeded.mp4.json" '["seed"]')" = "7" ] || fail "the given seed was not kept"

# --- refused before anything runs, the output untouched ---------------------
generate exists -o "$WORK_DIR/out/first.mp4"
[ "$STATUS" -ne 0 ] || fail "a run wrote over an existing clip"
grep -q "a generation never writes over a file" "$WORK_DIR/exists.err" || fail "the refusal: $(cat "$WORK_DIR/exists.err")"
[ "$(cat "$WORK_DIR/out/first.mp4")" = "MP4 clip" ] || fail "the existing clip was touched"
set +e
"$APOGEE_BIN" video generate "a heron" --duration 30 -o "$WORK_DIR/out/long.mp4" \
    </dev/null >/dev/null 2>"$WORK_DIR/long.err"
set -e
grep -q "is over the cap of 10 seconds a clip" "$WORK_DIR/long.err" || fail "the cap was not quoted: $(cat "$WORK_DIR/long.err")"
[ ! -e "$WORK_DIR/out/long.mp4" ] || fail "a refused run wrote a clip"

# --- a failing generation: no clip, no sidecar, the driver's words ----------
# Exported and unset around the call: an assignment before a function call
# outlives the call in a POSIX shell.
export STUB_VIDEO_ERROR="MPS backend out of memory"
generate failing -o "$WORK_DIR/out/failing.mp4"
unset STUB_VIDEO_ERROR
[ "$STATUS" -eq 1 ] || fail "a failed generation exited $STATUS"
grep -q "generation failed: RuntimeError: MPS backend out of memory" "$WORK_DIR/failing.err" \
    || fail "the failure: $(cat "$WORK_DIR/failing.err")"
grep -q "was not written" "$WORK_DIR/failing.err" || fail "the output's fate was not said"
[ ! -e "$WORK_DIR/out/failing.mp4" ] || fail "a failed run left a clip"
[ ! -e "$WORK_DIR/out/failing.mp4.json" ] || fail "a failed run left a sidecar"

echo "video generate: the pointer, the document, the sidecar, the seed, the refusals - OK"

# --- the loop (49d): a draft, its final, a continuation, a bare clip -------
run_video() {
    # run_video <name> [args...]: apogee video generate as given, JSON on stdout.
    name="$1"; shift
    set +e
    "$APOGEE_BIN" video generate "$@" --output-format json \
        </dev/null >"$WORK_DIR/$name.out" 2>"$WORK_DIR/$name.err"
    STATUS=$?
    set -e
    [ "$STATUS" -eq 0 ] || fail "the $name run failed ($STATUS): $(cat "$WORK_DIR/$name.err")"
}
run_video draft "a heron lands" --duration 1 --resolution 128x128 --fps 8 --draft \
    -o "$WORK_DIR/out/draft.mp4"
[ "$(field "$WORK_DIR/draft.out" '["preset"]')" = "draft" ] || fail "the draft's preset"
[ "$(field "$WORK_DIR/draft.out" '["settings"]["steps"]')" = "16" ] || fail "the draft's steps"
run_video final --from "$WORK_DIR/out/draft.mp4" --final -o "$WORK_DIR/out/final.mp4"
[ "$(field "$WORK_DIR/final.out" '["seed"]')" = "$(field "$WORK_DIR/draft.out" '["seed"]')" ] \
    || fail "the final did not hold the draft's seed"
[ "$(field "$WORK_DIR/final.out" '["seed_source"]')" = "held" ] || fail "the held seed was not said"
[ "$(field "$WORK_DIR/final.out" '["settings"]["steps"]')" = "50" ] || fail "the final's steps"
[ "$(field "$WORK_DIR/final.out" '["settings"]["frames"]')" = "$(field "$WORK_DIR/draft.out" '["settings"]["frames"]')" ] \
    || fail "the final is not the draft's frames"
[ "$(field "$WORK_DIR/final.out" '["parent"]["document"]')" = "$WORK_DIR/out/draft.mp4.json" ] \
    || fail "the final does not chain to the draft: $(cat "$WORK_DIR/final.out")"
grep -q "\[video\] changed: 50 steps (--final)" "$WORK_DIR/final.err" \
    || fail "what changed was not said: $(cat "$WORK_DIR/final.err")"
run_video shot2 "the heron takes off" --extend "$WORK_DIR/out/final.mp4" -o "$WORK_DIR/out/shot2.mp4"
[ "$(field "$WORK_DIR/shot2.out" '["mode"]')" = "i2v" ] || fail "the continuation is not image-to-video"
[ "$(field "$WORK_DIR/shot2.out" '["parent"]["continued"]')" = "$WORK_DIR/out/final.mp4" ] \
    || fail "the continuation does not name its clip"
grep -q "\[video\] extending $WORK_DIR/out/final.mp4 from its last frame" "$WORK_DIR/shot2.err" \
    || fail "the continuation was not said"
[ -z "$(ls -A "$APOGEE_HOME/cache/video")" ] || fail "the continuation left its frame behind"
set +e
"$APOGEE_BIN" video generate --from "$WORK_DIR/out/missing.mp4" -o "$WORK_DIR/out/never.mp4" \
    </dev/null >/dev/null 2>"$WORK_DIR/bare.err"
set -e
grep -q "settings are never guessed from pixels" "$WORK_DIR/bare.err" \
    || fail "a clip with no record was not refused: $(cat "$WORK_DIR/bare.err")"
[ ! -e "$WORK_DIR/out/never.mp4" ] || fail "a refused run wrote a clip"

echo "video generate: a draft, its final over the same seed, a continuation - OK"

# --- Ctrl-C mid-denoise, the tree sampled for a listener while it runs -----
descendants() {
    for child in $(pgrep -P "$1" 2>/dev/null); do
        echo "$child"
        descendants "$child"
    done
}
RECORD="$WORK_DIR/record.jsonl"
STUB_VIDEO_HANG=1 STUB_VIDEO_RECORD="$RECORD" "$APOGEE_BIN" video generate "a heron" \
    --duration 1 --resolution 128x128 --steps 2 --fps 8 -o "$WORK_DIR/out/stopped.mp4" \
    </dev/null >"$WORK_DIR/stopped.out" 2>"$WORK_DIR/stopped.err" &
CHILD=$!
tries=0
until [ -s "$RECORD" ] && grep -q '"call"' "$RECORD"; do
    tries=$((tries + 1))
    [ "$tries" -lt 300 ] || { kill -9 "$CHILD" 2>/dev/null; fail "the driver never started denoising"; }
    sleep 0.1
done
SEEN=0
if command -v lsof >/dev/null 2>&1; then
    for _ in 1 2 3 4 5; do
        PIDS="$CHILD"
        for pid in $(descendants "$CHILD"); do
            PIDS="$PIDS,$pid"
            ps -o command= -p "$pid" 2>/dev/null | grep -q video_generate.py && SEEN=1
        done
        HIT="$(lsof -a -p "$PIDS" -i -sTCP:LISTEN -Fn 2>/dev/null || true)"
        [ -z "$HIT" ] || { kill -9 "$CHILD" 2>/dev/null; fail "INVARIANT VIOLATED: a generation's tree listens: $HIT"; }
        sleep 0.1
    done
    [ "$SEEN" = 1 ] || { kill -9 "$CHILD" 2>/dev/null; fail "the sample never saw the driver, so it proved nothing"; }
fi
kill -INT "$CHILD"
set +e
wait "$CHILD"
STATUS=$?
set -e
[ "$STATUS" -eq 130 ] || fail "Ctrl-C exited $STATUS, not 130: $(cat "$WORK_DIR/stopped.err")"
grep -q "cancelled -- nothing was written" "$WORK_DIR/stopped.err" || fail "the cancel was not said"
[ ! -e "$WORK_DIR/out/stopped.mp4" ] || fail "a cancelled run left a clip"
[ -z "$(ls -A "$APOGEE_HOME/cache/video")" ] || fail "a cancelled run left its working directory"
if pgrep -f "$APOGEE_HOME/training/scripts/video_generate.py" >/dev/null 2>&1; then
    fail "the driver outlived the cancelled run"
fi

echo "video generate: Ctrl-C mid-denoise leaves nothing; no listener in the tree - OK"

# --- on a pipe, a missing runtime is refused naming its installer -----------
rm -rf "$SITE/diffusers"
generate missing -o "$WORK_DIR/out/missing.mp4"
[ "$STATUS" -ne 0 ] || fail "a run without diffusers succeeded"
grep -q "torch and diffusers are not installed in Apogee's Python environment -- apogee train setup --with video" \
    "$WORK_DIR/missing.err" || fail "the refusal: $(cat "$WORK_DIR/missing.err")"

echo "video generate: a missing runtime refused on a pipe - OK"
