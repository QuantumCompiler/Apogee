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
# frame, a clip with no record refused; since 52b each verb's progress as
# `--output-format stream-json` events, the document last and a Ctrl-C'd
# stream ending on an error; `voice say` (50a) -- one WAV with its
# sentences timed and its record, an unknown voice refused, no listener while
# the speech driver runs, Ctrl-C leaving nothing; and on a pipe, a missing
# runtime refused naming the command that installs it; and since 53l, on
# Apple silicon, the same generation on MLX -- mlx-video's build of a Wan set
# beside the set in the store, the REAL seeded MLX driver over the stub
# `mlx_video` (tests/data/backends/scripts) -- recorded with its engine, its
# tokenizer the set's own, an engine it does not run on refused, `--from`
# replaying MLX, the MLX driver's whole tree sampled for a socket, and a
# Ctrl-C leaving nothing; and since 53m, `voice say` on MLX -- mlx-audio's
# build of Kokoro in the store, the REAL seeded MLX speech driver over the
# stub `mlx_audio` -- recorded with its engine, its timings the WAV's own, the
# library's prints on neither stream, an engine it does not speak on refused,
# the MLX speech driver's whole tree sampled for a socket, a Ctrl-C leaving
# nothing, and mlx-audio missing refused naming its setup.
#
# POSIX only. Skips (77) without python3.
set -eu

USAGE="usage: video_e2e.sh <apogee-binary> <work-dir> <stubs-dir> [<mlx-stubs-dir>]"
APOGEE_BIN="${1:?$USAGE}"
WORK_DIR="${2:?$USAGE}"
STUBS="${3:?$USAGE}"
MLX_STUBS="${4:-}"

if ! command -v python3 >/dev/null 2>&1; then
    echo "video_e2e: python3 not found (the driver runs under it); skipping"
    exit 77
fi

. "$(dirname "$0")/sandbox.sh"
WORK_DIR="$(sandbox_dir "$WORK_DIR")"
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
# A query (`-filters`) lists nothing: this ffmpeg has no libass.
case "$out" in -*) exit 0 ;; esac
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
[ "$(field "$WORK_DIR/first.out" '["engine"]')" = "torch" ] || fail "a pipeline's engine is not torch (53l)"
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

# --- the same run as events (52b): the driver's own, the document last ------
generate streamed -o "$WORK_DIR/out/streamed.mp4" --output-format stream-json
[ "$STATUS" -eq 0 ] || fail "the streamed generation failed ($STATUS): $(cat "$WORK_DIR/streamed.err")"
python3 - "$WORK_DIR/streamed.out" "$WORK_DIR/out/streamed.mp4.json" <<'PY' \
    || fail "the generation's stream: $(cat "$WORK_DIR/streamed.out")"
import json, sys
events = [json.loads(line) for line in open(sys.argv[1])]
types = [e["type"] for e in events]
assert types[0] == "job" and events[0]["verb"] == "video generate", types
assert types[-1] == "document", types
ready = [e for e in events if e["type"] == "ready"]
assert ready and ready[0]["device"] == "cpu" and ready[0]["dtype"] == "float32", ready
# The engine the run takes, said as it runs (M24): the record's own word.
assert ready[0]["engine"] == "torch" == events[-1]["document"]["engine"], ready
steps = [(e["step"], e["steps"]) for e in events if e["type"] == "step"]
assert steps == [(1, 2), (2, 2)], steps
assert types.index("ready") < types.index("step") < types.index("frames") < types.index("assembling"), types
assert all("scene" not in e for e in events if e["type"] != "document"), "a scene outside a composition"
assert events[-1]["document"] == json.load(open(sys.argv[2])), "the document is not the record"
PY
if grep -q "\[video\]" "$WORK_DIR/streamed.err"; then
    fail "the stream's narration also reached stderr: $(cat "$WORK_DIR/streamed.err")"
fi

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

# --- voice say (50a): a voice model, the stub kokoro, one WAV, its record ----
cp -R "$STUBS/stub_voice/." "$SITE/"
VOICE_MODEL="$APOGEE_HOME/models/hexgrad--Kokoro-82M/voice/cccccccccccc"
python3 - "$VOICE_MODEL" <<'PY'
import os, sys
root = sys.argv[1]
def save(path, size=96):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as out:
        out.write(b"PK\x03\x04" + b"w" * size + b"PK\x05\x06" + b"\0" * 22)
os.makedirs(root, exist_ok=True)
with open(os.path.join(root, "config.json"), "w") as out:
    out.write('{"plbert": {}}')
save(os.path.join(root, "kokoro-v1_0.pth"), 256)
for voice in ("af_heart", "bm_george"):
    save(os.path.join(root, "voices", voice + ".pt"))
PY
set +e
"$APOGEE_BIN" voice say "Hello there. How are you?" -o "$WORK_DIR/out/hello.wav" --output-format json \
    </dev/null >"$WORK_DIR/voice.out" 2>"$WORK_DIR/voice.err"
STATUS=$?
set -e
[ "$STATUS" -eq 0 ] || fail "voice say failed ($STATUS): $(cat "$WORK_DIR/voice.err")"
[ "$(field "$WORK_DIR/voice.out" '["object"]')" = "voice.speech" ] || fail "not a voice.speech document"
[ "$(field "$WORK_DIR/voice.out" '["voice"]')" = "af_heart" ] || fail "the default voice"
[ "$(field "$WORK_DIR/voice.out" '["segments"][0]["sentences"][1]["text"]')" = "How are you?" ] \
    || fail "the sentences were not timed: $(cat "$WORK_DIR/voice.out")"
python3 -c 'import sys, wave; w = wave.open(sys.argv[1]); assert w.getframerate() == 24000 and w.getnframes() > 0' \
    "$WORK_DIR/out/hello.wav" || fail "the WAV is not one"
[ -f "$WORK_DIR/out/hello.wav.json" ] || fail "no record beside the WAV"
for stream in out err; do
    if grep -q "chatter\|noise on stderr" "$WORK_DIR/voice.$stream"; then
        fail "a library's output reached voice say's std$stream"
    fi
done
set +e
"$APOGEE_BIN" voice say "Hi." --voice zf_nobody -o "$WORK_DIR/out/nobody.wav" </dev/null >/dev/null 2>"$WORK_DIR/nobody.err"
set -e
grep -q "this model has no voice 'zf_nobody' -- its voices: af_heart, bm_george" "$WORK_DIR/nobody.err" \
    || fail "an unknown voice was not refused naming the set: $(cat "$WORK_DIR/nobody.err")"
# The tree sampled for a listener while the speech driver runs; then Ctrl-C.
VOICE_RECORD="$WORK_DIR/voice-record.jsonl"
STUB_VOICE_HANG=1 STUB_VOICE_RECORD="$VOICE_RECORD" "$APOGEE_BIN" voice say "A long one." \
    -o "$WORK_DIR/out/stopped.wav" </dev/null >/dev/null 2>"$WORK_DIR/voice-stopped.err" &
CHILD=$!
tries=0
until [ -s "$VOICE_RECORD" ] && grep -q '"say"' "$VOICE_RECORD"; do
    tries=$((tries + 1))
    [ "$tries" -lt 300 ] || { kill -9 "$CHILD" 2>/dev/null || true; fail "the speech driver never started"; }
    sleep 0.1
done
if command -v lsof >/dev/null 2>&1; then
    SEEN=0
    PIDS="$CHILD"
    for pid in $(descendants "$CHILD"); do
        PIDS="$PIDS,$pid"
        if ps -o command= -p "$pid" 2>/dev/null | grep -q voice_say.py; then SEEN=1; fi
    done
    HIT="$(lsof -a -p "$PIDS" -i -sTCP:LISTEN -Fn 2>/dev/null || true)"
    [ -z "$HIT" ] || { kill -9 "$CHILD" 2>/dev/null || true; fail "INVARIANT VIOLATED: speech's tree listens: $HIT"; }
    [ "$SEEN" = 1 ] || { kill -9 "$CHILD" 2>/dev/null || true; fail "the sample never saw the speech driver"; }
fi
kill -INT "$CHILD"
set +e
wait "$CHILD"
STATUS=$?
set -e
[ "$STATUS" -eq 130 ] || fail "Ctrl-C mid-speech exited $STATUS: $(cat "$WORK_DIR/voice-stopped.err")"
[ ! -e "$WORK_DIR/out/stopped.wav" ] || fail "a cancelled speech left a file"

# The same speech as events (52b): ready, each segment, the record last.
set +e
"$APOGEE_BIN" voice say "Hello there. How are you?" -o "$WORK_DIR/out/streamed.wav" \
    --output-format stream-json </dev/null >"$WORK_DIR/voice-stream.out" 2>"$WORK_DIR/voice-stream.err"
STATUS=$?
set -e
[ "$STATUS" -eq 0 ] || fail "the streamed speech failed ($STATUS): $(cat "$WORK_DIR/voice-stream.err")"
python3 - "$WORK_DIR/voice-stream.out" "$WORK_DIR/out/streamed.wav.json" <<'PY' \
    || fail "the speech's stream: $(cat "$WORK_DIR/voice-stream.out")"
import json, sys
events = [json.loads(line) for line in open(sys.argv[1])]
types = [e["type"] for e in events]
assert types[0] == "job" and events[0]["verb"] == "voice say", types
assert "ready" in types and "segment" in types and types[-1] == "document", types
segment = next(e for e in events if e["type"] == "segment")
assert segment["index"] == 1 and segment["of"] >= 1 and segment["seconds"] > 0, segment
assert events[-1]["document"] == json.load(open(sys.argv[2])), "the document is not the record"
ready = next(e for e in events if e["type"] == "ready")
assert ready["engine"] == "torch" == events[-1]["document"]["engine"], ready
PY
# Ctrl-C mid-stream: 130, every line whole, the last an error.
rm -f "$VOICE_RECORD"
STUB_VOICE_HANG=1 STUB_VOICE_RECORD="$VOICE_RECORD" "$APOGEE_BIN" voice say "A long one." \
    -o "$WORK_DIR/out/stopped2.wav" --output-format stream-json </dev/null \
    >"$WORK_DIR/voice-cancel.out" 2>"$WORK_DIR/voice-cancel.err" &
CHILD=$!
tries=0
until [ -s "$VOICE_RECORD" ] && grep -q '"say"' "$VOICE_RECORD"; do
    tries=$((tries + 1))
    [ "$tries" -lt 300 ] || { kill -9 "$CHILD" 2>/dev/null || true; fail "the streamed speech never started"; }
    sleep 0.1
done
kill -INT "$CHILD"
set +e
wait "$CHILD"
STATUS=$?
set -e
[ "$STATUS" -eq 130 ] || fail "Ctrl-C mid-stream exited $STATUS: $(cat "$WORK_DIR/voice-cancel.err")"
python3 - "$WORK_DIR/voice-cancel.out" <<'PY' || fail "the cancelled stream: $(cat "$WORK_DIR/voice-cancel.out")"
import json, sys
events = [json.loads(line) for line in open(sys.argv[1])]
assert events[0]["type"] == "job" and events[-1]["type"] == "error", [e["type"] for e in events]
# The document --output-format json prints for a cancelled run, then the error.
documents = [e["document"] for e in events if e["type"] == "document"]
assert all(d["status"] == "cancelled" for d in documents), documents
PY
[ ! -e "$WORK_DIR/out/stopped2.wav" ] || fail "a cancelled streamed speech left a file"

echo "voice say: one WAV, its sentences timed, its record; no listener; Ctrl-C - OK"

# --- video generate --lora (49e): applied by the driver, hashed in the record ---
mkdir -p "$WORK_DIR/loras"
printf 'weights' >"$WORK_DIR/loras/style.safetensors"
LORA_HASH="$(python3 -c 'import hashlib,sys; print(hashlib.sha256(open(sys.argv[1],"rb").read()).hexdigest())' "$WORK_DIR/loras/style.safetensors")"
export STUB_VIDEO_RECORD="$WORK_DIR/lora-record.jsonl"
generate lora -o "$WORK_DIR/out/lora.mp4" --lora "$WORK_DIR/loras/style.safetensors:0.8" --output-format json
unset STUB_VIDEO_RECORD
[ "$STATUS" -eq 0 ] || fail "the LoRA run failed ($STATUS): $(cat "$WORK_DIR/lora.err")"
[ "$(field "$WORK_DIR/lora.out" '["loras"][0]["sha256"]')" = "$LORA_HASH" ] || fail "the LoRA's hash was not recorded"
[ "$(field "$WORK_DIR/lora.out" '["loras"][0]["scale"]')" = "0.8" ] || fail "the LoRA's strength was not recorded"
grep -q '"adapters": \["lora1"\], "weights": \[0.8\]' "$WORK_DIR/lora-record.jsonl" \
    || fail "the driver did not set the LoRA's strength: $(cat "$WORK_DIR/lora-record.jsonl")"
grep -q "LoRAs: style.safetensors x0.8" "$WORK_DIR/lora.err" || fail "the LoRA was not said"
# Run again from its record: held and verified; changed under it, refused by name.
set +e
"$APOGEE_BIN" video generate --from "$WORK_DIR/out/lora.mp4" -o "$WORK_DIR/out/lora2.mp4" \
    </dev/null >/dev/null 2>"$WORK_DIR/lora2.err"
STATUS=$?
set -e
[ "$STATUS" -eq 0 ] || fail "the LoRA rerun failed: $(cat "$WORK_DIR/lora2.err")"
grep -q "1 LoRA (held)" "$WORK_DIR/lora2.err" || fail "the LoRA was not held: $(cat "$WORK_DIR/lora2.err")"
printf 'retrained' >"$WORK_DIR/loras/style.safetensors"
set +e
"$APOGEE_BIN" video generate --from "$WORK_DIR/out/lora.mp4" -o "$WORK_DIR/out/lora3.mp4" \
    </dev/null >/dev/null 2>"$WORK_DIR/lora3.err"
STATUS=$?
set -e
[ "$STATUS" -ne 0 ] || fail "a changed LoRA ran"
grep -q "has changed since that run" "$WORK_DIR/lora3.err" || fail "the change was not named: $(cat "$WORK_DIR/lora3.err")"
[ ! -e "$WORK_DIR/out/lora3.mp4" ] || fail "a refused run wrote a clip"

echo "video generate --lora: applied, hashed, held on a rerun, a changed file refused - OK"

# --- video compose (50c): planned on a mock, spoken, rendered, assembled -----
# The planner's first answer is not JSON: the one correction is what lands.
cat >"$WORK_DIR/plan.json" <<'JSON'
{"turns": [{"text": "Here is a plan."},
           {"text": "{\"title\": \"Tides\", \"scenes\": [{\"narration\": \"The sea breathes.\", \"visual\": \"a shore at dawn\", \"seconds\": 3}, {\"narration\": \"Twice a day.\", \"visual\": \"the moon over water\", \"seconds\": 3}]}"}]}
JSON
"$APOGEE_BIN" config add-backend planner --type mock --model-path "$WORK_DIR/plan.json" \
    </dev/null >/dev/null || fail "add-backend planner"
# ffprobe answers in stream blocks, as the real one does.
cat >"$WORK_DIR/bin/ffprobe" <<'PROBE'
#!/bin/sh
case "$*" in
  *.wav) printf '[STREAM]\ncodec_name=pcm_s16le\ncodec_type=audio\nsample_rate=24000\nchannels=1\n[/STREAM]\n[FORMAT]\nduration=1.0\n[/FORMAT]\n' ;;
  *) printf '[STREAM]\ncodec_name=h264\ncodec_type=video\nwidth=128\nheight=128\nr_frame_rate=24/1\n[/STREAM]\n[FORMAT]\nduration=3.04\n[/FORMAT]\n' ;;
esac
PROBE
chmod +x "$WORK_DIR/bin/ffprobe"
compose() {
    set +e
    "$APOGEE_BIN" video compose "the tides, twice a day" -o "$WORK_DIR/out/tides.mp4" \
        --planner planner --output-format json </dev/null >"$WORK_DIR/$1.out" 2>"$WORK_DIR/$1.err"
    STATUS=$?
    set -e
}
compose compose
[ "$STATUS" -eq 0 ] || fail "video compose failed ($STATUS): $(cat "$WORK_DIR/compose.err")"
[ "$(field "$WORK_DIR/compose.out" '["object"]')" = "video.project" ] || fail "not a video.project document"
[ "$(field "$WORK_DIR/compose.out" '["status"]')" = "done" ] || fail "not done: $(cat "$WORK_DIR/compose.out")"
[ "$(field "$WORK_DIR/compose.out" '["plan"]["attempts"]')" = "2" ] || fail "the correction did not land"
[ "$(field "$WORK_DIR/compose.out" '["settings"]["voice"]')" = "af_heart" ] || fail "the voice"
[ "$(field "$WORK_DIR/compose.out" '["captions"]["burned"]')" = "False" ] || fail "captions burned without libass"
[ "$(cat "$WORK_DIR/out/tides.mp4")" = "MP4 clip" ] || fail "the video did not land"
[ -f "$WORK_DIR/out/tides.srt" ] || fail "the captions were not kept beside the video"
grep -q "The sea breathes." "$WORK_DIR/out/tides.srt" || fail "the captions: $(cat "$WORK_DIR/out/tides.srt")"
python3 -c 'import json,sys; a=json.load(open(sys.argv[1])); b=json.load(open(sys.argv[2])); sys.exit(a != b)' \
    "$WORK_DIR/out/tides.mp4.json" "$WORK_DIR/compose.out" || fail "the project beside it is not the document"
SCENES="$WORK_DIR/out/tides.scenes"
for file in s01.mp4 s01.wav s02.mp4 s02.wav captions.srt; do
    [ -f "$SCENES/$file" ] || fail "the scenes directory lacks $file"
done
[ "$(field "$SCENES/s01.mp4.json" '["object"]')" = "video.generation" ] || fail "a clip without its record"
[ "$(field "$SCENES/s02.wav.json" '["object"]')" = "voice.speech" ] || fail "a narration without its record"
# The plan and its spend said before the first scene.
PLAN_AT="$(grep -n '\[compose\] plan: "Tides" -- 2 scenes' "$WORK_DIR/compose.err" | cut -d: -f1)"
SPEND_AT="$(grep -n '\[compose\] spend: 2 narration runs' "$WORK_DIR/compose.err" | cut -d: -f1)"
FIRST_AT="$(grep -n '\[compose\] scene 1/2: speaking its narration' "$WORK_DIR/compose.err" | cut -d: -f1)"
[ -n "$PLAN_AT" ] && [ -n "$SPEND_AT" ] && [ -n "$FIRST_AT" ] || fail "the plan was not said: $(cat "$WORK_DIR/compose.err")"
[ "$PLAN_AT" -lt "$FIRST_AT" ] && [ "$SPEND_AT" -lt "$FIRST_AT" ] || fail "the plan was said after the work began"
grep -q "captions: kept beside the video as tides.srt" "$WORK_DIR/compose.err" || fail "the captions' fate was not said"
for stream in out err; do
    if grep -q "chatter\|noise on stderr\|nobody should see" "$WORK_DIR/compose.$stream"; then
        fail "a library's output reached compose's std$stream"
    fi
done
[ -z "$(ls -A "$APOGEE_HOME/cache/video")" ] || fail "a composition left a working directory"
# The same command again resumes: no plan (the mock has no third answer), one clip rendered.
rm "$WORK_DIR/out/tides.mp4" "$SCENES/s02.mp4" "$SCENES/s02.mp4.json"
compose resumed
[ "$STATUS" -eq 0 ] || fail "the resume failed ($STATUS): $(cat "$WORK_DIR/resumed.err")"
grep -q "\[compose\] resuming .*tides.mp4.json: 2 of 2 narrations and 1 clips already there" \
    "$WORK_DIR/resumed.err" || fail "the resume was not said: $(cat "$WORK_DIR/resumed.err")"
if grep -q "scene 1/2: rendering" "$WORK_DIR/resumed.err"; then fail "a kept clip rendered again"; fi
grep -q "scene 2/2: rendering its clip" "$WORK_DIR/resumed.err" || fail "the missing clip was not rendered"
[ -f "$WORK_DIR/out/tides.mp4" ] || fail "the resumed video did not land"

echo "video compose: planned with one correction, said first, spoken, rendered, kept, resumed - OK"

# --- a composition as events (52b): each scene begun, its drivers' events --
set +e
"$APOGEE_BIN" video compose "the tides, twice a day" -o "$WORK_DIR/out/tides-stream.mp4" \
    --planner planner --output-format stream-json </dev/null \
    >"$WORK_DIR/compose-stream.out" 2>"$WORK_DIR/compose-stream.err"
STATUS=$?
set -e
[ "$STATUS" -eq 0 ] || fail "the streamed composition failed ($STATUS): $(cat "$WORK_DIR/compose-stream.err")"
python3 - "$WORK_DIR/compose-stream.out" "$WORK_DIR/out/tides-stream.mp4.json" <<'PY' \
    || fail "the composition's stream: $(cat "$WORK_DIR/compose-stream.out")"
import json, sys
events = [json.loads(line) for line in open(sys.argv[1])]
types = [e["type"] for e in events]
assert types[0] == "job" and events[0]["verb"] == "video compose", types
assert types[-1] == "document", types
scenes = [(e["scene"], e["scenes"], e["state"]) for e in events if e["type"] == "scene"]
assert scenes == [(1, 2, "speaking"), (2, 2, "speaking"), (1, 2, "rendering"), (2, 2, "rendering")], scenes
# Each driver event carries the scene it belongs to, in the order scenes ran.
assert [e["scene"] for e in events if e["type"] == "segment"] == [1, 2], "segments"
steps = [e["scene"] for e in events if e["type"] == "step"]
assert steps and steps == sorted(steps) and set(steps) == {1, 2}, steps
assert any(e["type"] == "notice" and "plan:" in e["text"] for e in events), "the plan was not said"
assert events[-1]["document"] == json.load(open(sys.argv[2])), "the document is not the project"
PY

# --- a brief planned on paper (52c), then rendered as planned ---------------
set +e
"$APOGEE_BIN" video compose "the tides, twice a day" -o "$WORK_DIR/out/paper.mp4" \
    --planner planner --dry-run --output-format json </dev/null \
    >"$WORK_DIR/paper.out" 2>"$WORK_DIR/paper.err"
STATUS=$?
set -e
[ "$STATUS" -eq 0 ] || fail "the dry run failed ($STATUS): $(cat "$WORK_DIR/paper.err")"
[ "$(field "$WORK_DIR/paper.out" '["status"]')" = "planned" ] || fail "the dry run's project is not planned"
[ ! -e "$WORK_DIR/out/paper.mp4" ] && [ ! -e "$WORK_DIR/out/paper.scenes" ] || fail "a dry run rendered"
grep -q "\[compose\] spend: 2 narration runs" "$WORK_DIR/paper.err" || fail "the dry run's spend was not said"
python3 -c 'import json,sys; a=json.load(open(sys.argv[1])); b=json.load(open(sys.argv[2])); sys.exit(a != b)' \
    "$WORK_DIR/out/paper.mp4.json" "$WORK_DIR/paper.out" || fail "the plan kept is not the document printed"
SEEDS="$(field "$WORK_DIR/paper.out" '["scenes"][0]["seed"]')"
set +e
"$APOGEE_BIN" video compose "the tides, twice a day" -o "$WORK_DIR/out/paper.mp4" \
    --output-format json </dev/null >"$WORK_DIR/paper2.out" 2>"$WORK_DIR/paper2.err"
STATUS=$?
set -e
[ "$STATUS" -eq 0 ] || fail "rendering the kept plan failed ($STATUS): $(cat "$WORK_DIR/paper2.err")"
grep -q "\[compose\] rendering the plan a dry run kept at" "$WORK_DIR/paper2.err" \
    || fail "the kept plan was not said: $(cat "$WORK_DIR/paper2.err")"
if grep -q "planning the scenes" "$WORK_DIR/paper2.err"; then fail "the kept plan was planned again"; fi
[ "$(field "$WORK_DIR/paper2.out" '["status"]')" = "done" ] || fail "the kept plan was not rendered"
[ "$(field "$WORK_DIR/paper2.out" '["scenes"][0]["seed"]')" = "$SEEDS" ] || fail "the plan's seed was not held"
echo "video compose --dry-run: a brief planned on paper, kept, then rendered as planned - OK"

# --- video compose --edit (50d): the diff said, only the changed scene rendered ---
cat >"$WORK_DIR/edit.json" <<'JSON'
{"turns": [{"text": "{\"title\": \"Tides\", \"scenes\": [{\"from\": 1, \"narration\": \"The sea breathes.\", \"visual\": \"a shore at dawn\", \"seconds\": 3}, {\"from\": 2, \"narration\": \"Twice a day.\", \"visual\": \"a full moon rising\", \"seconds\": 3}]}"}]}
JSON
"$APOGEE_BIN" config add-backend editor --type mock --model-path "$WORK_DIR/edit.json" \
    </dev/null >/dev/null || fail "add-backend editor"
edit() {
    out="$1"; shift
    set +e
    "$APOGEE_BIN" video compose --from "$WORK_DIR/out/tides.mp4" --edit "make the second scene the moon" \
        --planner editor -o "$WORK_DIR/out/$out.mp4" --output-format json "$@" \
        </dev/null >"$WORK_DIR/$out.out" 2>"$WORK_DIR/$out.err"
    STATUS=$?
    set -e
}
edit dry --dry-run
[ "$STATUS" -eq 0 ] || fail "the dry run failed ($STATUS): $(cat "$WORK_DIR/dry.err")"
[ "$(field "$WORK_DIR/dry.out" '["status"]')" = "planned" ] || fail "a dry run's project is not planned"
[ ! -e "$WORK_DIR/out/dry.mp4.json" ] && [ ! -e "$WORK_DIR/out/dry.scenes" ] || fail "a dry run wrote something"
edit edited
[ "$STATUS" -eq 0 ] || fail "the edit failed ($STATUS): $(cat "$WORK_DIR/edited.err")"
[ "$(field "$WORK_DIR/edited.out" '["status"]')" = "done" ] || fail "the edit is not done: $(cat "$WORK_DIR/edited.out")"
[ "$(field "$WORK_DIR/edited.out" '["parent"]')" = "$WORK_DIR/out/tides.mp4.json" ] || fail "the edit does not chain to its parent"
[ "$(field "$WORK_DIR/edited.out" '["edits"][0]["instruction"]')" = "make the second scene the moon" ] || fail "the edit's instruction"
grep -q "\[compose\] the edit: 2 scenes, were 2" "$WORK_DIR/edited.err" || fail "the diff was not said: $(cat "$WORK_DIR/edited.err")"
grep -q "\[compose\]   2. scene 2; its prompt changed" "$WORK_DIR/edited.err" || fail "the changed scene was not said"
grep -q "scene 2/2: rendering its clip" "$WORK_DIR/edited.err" || fail "the changed scene was not rendered"
if grep -q "scene 1/2: rendering\|speaking its narration" "$WORK_DIR/edited.err"; then
    fail "an untouched scene rendered again: $(cat "$WORK_DIR/edited.err")"
fi
cmp -s "$WORK_DIR/out/tides.scenes/s01.mp4" "$WORK_DIR/out/edited.scenes/s01.mp4" || fail "scene 1's clip was not carried byte for byte"
cmp -s "$WORK_DIR/out/tides.scenes/s02.wav" "$WORK_DIR/out/edited.scenes/s02.wav" || fail "scene 2's narration was not carried byte for byte"
[ -f "$WORK_DIR/out/edited.mp4" ] || fail "the edited video did not land"

echo "video compose --edit: a dry run writes nothing; the diff said; only the changed scene rendered; chained - OK"

# --- on a pipe, a missing runtime is refused naming its installer -----------
rm -rf "$SITE/diffusers"
generate missing -o "$WORK_DIR/out/missing.mp4"
[ "$STATUS" -ne 0 ] || fail "a run without diffusers succeeded"
grep -q "torch and diffusers are not installed in Apogee's Python environment -- apogee train setup --with video" \
    "$WORK_DIR/missing.err" || fail "the refusal: $(cat "$WORK_DIR/missing.err")"
# Streamed (M29), the stream ends on its error -- the words stderr gets --
# where it ended on its `job` line alone.
generate missing_stream -o "$WORK_DIR/out/missing.mp4" --output-format stream-json
[ "$STATUS" -eq 1 ] || fail "a streamed run without diffusers: exit $STATUS"
python3 - "$WORK_DIR/missing_stream.out" <<'EOF2' || fail "the stream: $(cat "$WORK_DIR/missing_stream.out")"
import json, sys
events = [json.loads(line) for line in open(sys.argv[1])]
assert [e["type"] for e in events] == ["job", "error"], events
assert events[1]["message"] == ("torch and diffusers are not installed in Apogee's Python "
                                "environment -- apogee train setup --with video"), events[1]
EOF2
grep -q "^apogee video generate: torch and diffusers are not installed" "$WORK_DIR/missing_stream.err" \
    || fail "the streamed refusal left stderr: $(cat "$WORK_DIR/missing_stream.err")"

echo "video generate: a missing runtime refused on a pipe, and as a stream's error - OK"

# --- on MLX (53l): mlx-video's build of a Wan set, the MLX driver ------------
if [ -z "$MLX_STUBS" ] || [ "$(uname -s)" != "Darwin" ] || [ "$(uname -m)" != "arm64" ]; then
    echo "video generate on MLX: skipped -- MLX runs on Apple silicon alone"
    exit 0
fi
[ -f "$APOGEE_HOME/training/scripts/video_generate_mlx.py" ] || fail "check --fix did not seed the MLX driver"
# What `train setup --with mlx-video` would install: mlx, mlx-video's
# generator and the torch its set brings, the tqdm and transformers it uses.
cp -R "$MLX_STUBS/stub_mlx/." "$SITE/"
cp -R "$MLX_STUBS/stub_mlx_video/." "$SITE/"
# The speech driver keeps the stub torch it runs on: the MLX ladder asks only
# that a torch is there.
cp -R "$STUBS/stub_video/torch/." "$SITE/torch/"
SET="$APOGEE_HOME/models/Wan-AI--Wan2.2-TI2V-5B/safetensors/dddddddddddd"
MLX_MODEL="$APOGEE_HOME/models/Wan-AI--Wan2.2-TI2V-5B/mlx/eeeeeeeeeeee"
MLX_HANDLE="Wan-AI--Wan2.2-TI2V-5B/mlx/eeeeeeeeeeee"
python3 - "$SET" "$MLX_MODEL" <<'PY'
import hashlib, json, os, struct, sys
source, model = sys.argv[1], sys.argv[2]
def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as out:
        out.write(data if isinstance(data, bytes) else data.encode())
    return data if isinstance(data, bytes) else data.encode()
def shard(name):
    header = json.dumps({"__metadata__": {"format": "mlx"},
                         name: {"dtype": "F16", "shape": [1], "data_offsets": [0, 2]}}).encode()
    return struct.pack("<Q", len(header)) + header + b"\0\0"
# The Wan set as 53j pulls it: its config, its tokenizer under google/umt5-xxl/.
files = []
for path, data in (("config.json", json.dumps({"_class_name": "WanModel",
                                               "_diffusers_version": "0.33.0",
                                               "model_type": "ti2v", "out_dim": 48})),
                   ("diffusion_pytorch_model.safetensors", shard("w")),
                   ("google/umt5-xxl/tokenizer_config.json", "{}"),
                   ("google/umt5-xxl/tokenizer.json", "{}"),
                   ("google/umt5-xxl/spiece.model", "a sentencepiece model, never one")):
    data = write(f"{source}/{path}", data)
    files.append({"path": path, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()})
write(f"{source}/apogee-snapshot.json", json.dumps({
    "ref": "Wan-AI/Wan2.2-TI2V-5B", "revision": "main", "source": "huggingface",
    "pulled_at": "2026-10-21T00:00:00Z", "files": files}))
# mlx-video's build of it, as `models convert --mlx` commits it (53k).
write(f"{model}/config.json", json.dumps({"model_type": "ti2v", "dim": 3072, "sample_fps": 24,
                                          "quantization": {"group_size": 64, "bits": 4}}))
for name in ("model", "t5_encoder", "vae"):
    write(f"{model}/{name}.safetensors", shard(f"{name}.weight"))
write(f"{model}/apogee-snapshot.json", json.dumps({
    "ref": "Wan-AI--Wan2.2-TI2V-5B/safetensors/dddddddddddd", "source": "convert",
    "transform": "mlx_video.models.wan_2.convert 4bit", "kind": "video", "files": []}))
PY

MLX_RECORD="$WORK_DIR/mlx-record.jsonl"
export STUB_MLX_VIDEO_RECORD="$MLX_RECORD"
generate mlx -o "$WORK_DIR/out/mlx.mp4" --model "$MLX_HANDLE" --output-format json
[ "$STATUS" -eq 0 ] || fail "the MLX generation failed ($STATUS): $(cat "$WORK_DIR/mlx.err")"
[ "$(field "$WORK_DIR/mlx.out" '["engine"]')" = "mlx" ] || fail "the record's engine: $(cat "$WORK_DIR/mlx.out")"
[ "$(field "$WORK_DIR/mlx.out" '["status"]')" = "done" ] || fail "not done: $(cat "$WORK_DIR/mlx.out")"
[ "$(field "$WORK_DIR/mlx.out" '["model"]["name"]')" = "$MLX_HANDLE" ] || fail "the model"
[ "$(field "$WORK_DIR/mlx.out" '["model"]["class"]')" = "Wan ti2v" ] || fail "the model's class"
[ "$(field "$WORK_DIR/mlx.out" '["versions"]["mlx_video"]')" = "0.0-video-stub" ] || fail "mlx-video's version"
[ "$(field "$WORK_DIR/mlx.out" '["device"]["device"]')" = "gpu" ] || fail "MLX's device"
[ "$(field "$WORK_DIR/mlx.out" '["settings"]["frames"]')" = "9" ] || fail "1 s at 8 fps is not 9 frames"
[ "$(cat "$WORK_DIR/out/mlx.mp4")" = "MP4 clip" ] || fail "the MLX clip did not land"
python3 -c 'import json,sys; a=json.load(open(sys.argv[1])); b=json.load(open(sys.argv[2])); sys.exit(a != b)' \
    "$WORK_DIR/out/mlx.mp4.json" "$WORK_DIR/mlx.out" || fail "the MLX sidecar is not the document"
# The generator's tokenizer is the set's own, from its files; the hub never asked.
python3 - "$MLX_RECORD" "$SET/google/umt5-xxl" <<'PY' || fail "the tokenizer: $(cat "$MLX_RECORD")"
import json, sys
entries = [json.loads(line) for line in open(sys.argv[1])]
tokenizers = [e for e in entries if "tokenizer" in e]
assert tokenizers == [{"tokenizer": sys.argv[2], "local_files_only": True}], tokenizers
call = next(e for e in entries if "generate" in e)
assert call["num_frames"] == 9 and call["steps"] == 2 and call["width"] == 128, call
PY
grep -q "\[video\] running on MLX (gpu, 4-bit) -- expect minutes per clip on Apple silicon" "$WORK_DIR/mlx.err" \
    || fail "MLX and the scale were not said: $(cat "$WORK_DIR/mlx.err")"
grep -q "on MLX (mlx-video)" "$WORK_DIR/mlx.err" || fail "the engine was not said before the run"
grep -q "\[video\] step 2/2" "$WORK_DIR/mlx.err" || fail "the MLX steps were not said"
for stream in out err; do
    if grep -q "Generation (MLX)\|noise on stderr\|nobody should see\|Diffusion: " "$WORK_DIR/mlx.$stream"; then
        fail "mlx-video's output reached apogee's std$stream"
    fi
done
[ -z "$(ls -A "$APOGEE_HOME/cache/video")" ] || fail "an MLX run left its working directory"

# The same run as events (M24): its ready says MLX while it runs.
generate mlx_stream -o "$WORK_DIR/out/mlx-stream.mp4" --model "$MLX_HANDLE" --output-format stream-json
[ "$STATUS" -eq 0 ] || fail "the streamed MLX generation failed ($STATUS): $(cat "$WORK_DIR/mlx_stream.err")"
python3 - "$WORK_DIR/mlx_stream.out" <<'PY' || fail "the MLX generation's stream: $(cat "$WORK_DIR/mlx_stream.out")"
import json, sys
events = [json.loads(line) for line in open(sys.argv[1])]
types = [e["type"] for e in events]
ready = [e for e in events if e["type"] == "ready"]
assert len(ready) == 1 and ready[0]["engine"] == "mlx" and ready[0]["device"] == "gpu", ready
assert types.index("ready") < types.index("step") and types[-1] == "document", types
assert events[-1]["document"]["engine"] == "mlx", events[-1]
PY

# The Wan set itself is no model a run takes (M24): the refusal names the
# MLX conversion made of it, by its handle, and the pipeline it could pull.
generate wan_set -o "$WORK_DIR/out/wan-set.mp4" --model "Wan-AI--Wan2.2-TI2V-5B/safetensors/dddddddddddd"
[ "$STATUS" -eq 1 ] || fail "the Wan set itself ran ($STATUS)"
grep -qF "'Wan-AI--Wan2.2-TI2V-5B/safetensors/dddddddddddd' is Wan's original layout (WanModel), not a pipeline or an MLX video model -- what runs it is its Diffusers pipeline on PyTorch, pulled whole with 'apogee models pull Wan-AI/Wan2.2-TI2V-5B-Diffusers', or its MLX conversion on MLX through mlx-video, made as $MLX_HANDLE -- name it with --model" \
    "$WORK_DIR/wan_set.err" || fail "the Wan set's refusal: $(cat "$WORK_DIR/wan_set.err")"
[ ! -e "$WORK_DIR/out/wan-set.mp4" ] || fail "a refused run wrote a clip"

# An engine its model does not run on: refused before anything runs.
generate mlx_torch -o "$WORK_DIR/out/mlx-torch.mp4" --model "$MLX_HANDLE" --engine torch
[ "$STATUS" -eq 1 ] || fail "an MLX video model ran on --engine torch ($STATUS)"
grep -q -- "--engine torch: '$MLX_HANDLE' is an MLX video model, which runs on MLX (mlx-video), not PyTorch" \
    "$WORK_DIR/mlx_torch.err" || fail "the engine refusal: $(cat "$WORK_DIR/mlx_torch.err")"
[ ! -e "$WORK_DIR/out/mlx-torch.mp4" ] || fail "a refused run wrote a clip"

# A run again from its record replays MLX.
run_video mlx_again --from "$WORK_DIR/out/mlx.mp4" -o "$WORK_DIR/out/mlx-again.mp4"
[ "$(field "$WORK_DIR/mlx_again.out" '["engine"]')" = "mlx" ] || fail "--from did not replay MLX"
grep -q "seed .* (held), engine mlx" "$WORK_DIR/mlx_again.err" || fail "the held engine was not said: $(cat "$WORK_DIR/mlx_again.err")"
unset STUB_MLX_VIDEO_RECORD

# The composer runs each clip on whichever engine its model's directory says.
set +e
"$APOGEE_BIN" video compose "the tides, twice a day" -m "$MLX_HANDLE" -o "$WORK_DIR/out/tides-mlx.mp4" \
    --planner planner --output-format json </dev/null >"$WORK_DIR/compose-mlx.out" 2>"$WORK_DIR/compose-mlx.err"
STATUS=$?
set -e
[ "$STATUS" -eq 0 ] || fail "video compose on MLX failed ($STATUS): $(cat "$WORK_DIR/compose-mlx.err")"
[ "$(field "$WORK_DIR/compose-mlx.out" '["status"]')" = "done" ] || fail "the MLX composition is not done"
for scene in s01 s02; do
    [ "$(field "$WORK_DIR/out/tides-mlx.scenes/$scene.mp4.json" '["engine"]')" = "mlx" ] \
        || fail "the composition's $scene did not run on MLX"
done
if grep -q "Generation (MLX)\|noise on stderr" "$WORK_DIR/compose-mlx.out" "$WORK_DIR/compose-mlx.err"; then
    fail "mlx-video's output reached compose's streams"
fi

echo "video generate on MLX: recorded with its engine, the set's tokenizer, --from replays it, composed - OK"

# --- Ctrl-C mid-denoise on MLX, the MLX driver's whole tree sampled -----------
rm -f "$MLX_RECORD"
STUB_MLX_VIDEO_HANG=1 STUB_MLX_VIDEO_RECORD="$MLX_RECORD" "$APOGEE_BIN" video generate "a heron" \
    --model "$MLX_HANDLE" --duration 1 --resolution 128x128 --steps 2 --fps 8 \
    -o "$WORK_DIR/out/mlx-stopped.mp4" </dev/null >"$WORK_DIR/mlx-stopped.out" 2>"$WORK_DIR/mlx-stopped.err" &
CHILD=$!
tries=0
until [ -s "$MLX_RECORD" ] && grep -q '"generate"' "$MLX_RECORD"; do
    tries=$((tries + 1))
    [ "$tries" -lt 300 ] || { kill -9 "$CHILD" 2>/dev/null; fail "the MLX driver never started denoising"; }
    sleep 0.1
done
if command -v lsof >/dev/null 2>&1; then
    SEEN=0
    for _ in 1 2 3 4 5; do
        PIDS="$CHILD"
        for pid in $(descendants "$CHILD"); do
            PIDS="$PIDS,$pid"
            ps -o command= -p "$pid" 2>/dev/null | grep -q video_generate_mlx.py && SEEN=1
        done
        # No socket of any kind anywhere below apogee, and no listener in its tree.
        HIT="$(lsof -a -p "$PIDS" -i -sTCP:LISTEN -Fn 2>/dev/null || true)"
        [ -z "$HIT" ] || { kill -9 "$CHILD" 2>/dev/null; fail "INVARIANT VIOLATED: an MLX generation's tree listens: $HIT"; }
        for pid in $(descendants "$CHILD"); do
            SOCKETS="$(lsof -a -p "$pid" -i -Fn 2>/dev/null || true)"
            [ -z "$SOCKETS" ] || { kill -9 "$CHILD" 2>/dev/null; fail "INVARIANT VIOLATED: the MLX driver's tree holds a socket: $SOCKETS"; }
        done
        sleep 0.1
    done
    [ "$SEEN" = 1 ] || { kill -9 "$CHILD" 2>/dev/null; fail "the sample never saw the MLX driver, so it proved nothing"; }
fi
kill -INT "$CHILD"
set +e
wait "$CHILD"
STATUS=$?
set -e
[ "$STATUS" -eq 130 ] || fail "Ctrl-C on MLX exited $STATUS, not 130: $(cat "$WORK_DIR/mlx-stopped.err")"
grep -q "cancelled -- nothing was written" "$WORK_DIR/mlx-stopped.err" || fail "the MLX cancel was not said"
[ ! -e "$WORK_DIR/out/mlx-stopped.mp4" ] || fail "a cancelled MLX run left a clip"
[ ! -e "$WORK_DIR/out/mlx-stopped.mp4.json" ] || fail "a cancelled MLX run left a sidecar"
[ -z "$(ls -A "$APOGEE_HOME/cache/video")" ] || fail "a cancelled MLX run left its working directory"
if pgrep -f "$APOGEE_HOME/training/scripts/video_generate_mlx.py" >/dev/null 2>&1; then
    fail "the MLX driver outlived the cancelled run"
fi

echo "video generate on MLX: Ctrl-C mid-denoise leaves nothing; no socket in the MLX driver's tree - OK"

# --- voice say on MLX (53m): mlx-audio's build of Kokoro, the MLX speech driver ---
[ -f "$APOGEE_HOME/training/scripts/voice_say_mlx.py" ] || fail "check --fix did not seed the MLX speech driver"
# What `train setup --with mlx-audio` would install for speech: mlx-audio's
# Kokoro and the misaki it reads text through, beside the stub mlx.
cp -R "$MLX_STUBS/stub_mlx_audio/mlx_audio" "$SITE/"
cp -R "$MLX_STUBS/stub_mlx_audio/misaki" "$SITE/"
VOICE_MLX="$APOGEE_HOME/models/hexgrad--Kokoro-82M/mlx/ffffffffffff"
VOICE_MLX_HANDLE="hexgrad--Kokoro-82M/mlx/ffffffffffff"
python3 - "$VOICE_MLX" <<'PY'
import json, os, struct, sys
model = sys.argv[1]
def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as out:
        out.write(data if isinstance(data, bytes) else data.encode())
def shard(name, elements):
    header = json.dumps({"__metadata__": {"format": "mlx"},
                         name: {"dtype": "F16", "shape": [elements],
                                "data_offsets": [0, 2 * elements]}}).encode()
    return struct.pack("<Q", len(header)) + header + b"\0" * (2 * elements)
# mlx-audio's build of Kokoro, as `models convert --mlx` commits it (53m).
write(f"{model}/config.json", json.dumps({"model_type": "kokoro", "istftnet": {}, "plbert": {}}))
write(f"{model}/model.safetensors", shard("bert.weight", 32))
for voice in ("af_heart", "bm_george"):
    write(f"{model}/voices/{voice}.safetensors", shard("voice", 16))
write(f"{model}/apogee-snapshot.json", json.dumps({
    "ref": "hexgrad--Kokoro-82M/voice/cccccccccccc", "source": "convert",
    "transform": "mlx_audio.convert bf16", "kind": "voice", "files": []}))
PY

VOICE_MLX_RECORD="$WORK_DIR/voice-mlx-record.jsonl"
set +e
STUB_MLX_AUDIO_RECORD="$VOICE_MLX_RECORD" "$APOGEE_BIN" voice say "Hello there. How are you?" \
    --model "$VOICE_MLX_HANDLE" -o "$WORK_DIR/out/hello-mlx.wav" --output-format json \
    </dev/null >"$WORK_DIR/voice-mlx.out" 2>"$WORK_DIR/voice-mlx.err"
STATUS=$?
set -e
[ "$STATUS" -eq 0 ] || fail "voice say on MLX failed ($STATUS): $(cat "$WORK_DIR/voice-mlx.err")"
[ "$(field "$WORK_DIR/voice-mlx.out" '["engine"]')" = "mlx" ] || fail "the speech's engine: $(cat "$WORK_DIR/voice-mlx.out")"
[ "$(field "$WORK_DIR/voice-mlx.out" '["voice"]')" = "af_heart" ] || fail "the default voice on MLX"
[ "$(field "$WORK_DIR/voice-mlx.out" '["versions"]["mlx_audio"]')" = "0.0-audio-stub" ] || fail "mlx-audio's version"
[ "$(field "$WORK_DIR/voice-mlx.out" '["device"]["device"]')" = "gpu" ] || fail "MLX's device"
# Each segment's seconds the WAV's own header's, the record beside it.
python3 - "$WORK_DIR/voice-mlx.out" "$WORK_DIR/out/hello-mlx.wav" <<'PY' || fail "the timings: $(cat "$WORK_DIR/voice-mlx.out")"
import json, sys, wave
document = json.load(open(sys.argv[1]))
w = wave.open(sys.argv[2])
seconds = w.getnframes() / w.getframerate()
assert w.getframerate() == 24000 and w.getnframes() == 2400, (w.getframerate(), w.getnframes())
assert document["segments"][0]["seconds"] == seconds == document["seconds"], document
assert [s["text"] for s in document["segments"][0]["sentences"]] == ["Hello there.", "How are you?"]
PY
python3 -c 'import json,sys; a=json.load(open(sys.argv[1])); b=json.load(open(sys.argv[2])); sys.exit(a != b)' \
    "$WORK_DIR/out/hello-mlx.wav.json" "$WORK_DIR/voice-mlx.out" || fail "the MLX speech's sidecar is not the document"
# Spoken with the build's own voice pack, by path -- the hub never asked.
grep -q "\"voice\": \"$VOICE_MLX/voices/af_heart.safetensors\"" "$VOICE_MLX_RECORD" \
    || fail "the voice was not the build's own pack: $(cat "$VOICE_MLX_RECORD")"
grep -q "\[voice\] speaking on MLX (gpu)" "$WORK_DIR/voice-mlx.err" || fail "MLX was not said: $(cat "$WORK_DIR/voice-mlx.err")"
for stream in out err; do
    if grep -q "chatter\|noise on stderr" "$WORK_DIR/voice-mlx.$stream"; then
        fail "mlx-audio's output reached voice say's std$stream"
    fi
done
[ -z "$(ls -A "$APOGEE_HOME/cache/video")" ] || fail "an MLX speech left its working directory"

# The same speech as events (M24): its ready says MLX while it speaks.
set +e
"$APOGEE_BIN" voice say "Hello there." --model "$VOICE_MLX_HANDLE" -o "$WORK_DIR/out/hello-mlx-stream.wav" \
    --output-format stream-json </dev/null >"$WORK_DIR/voice-mlx-stream.out" 2>"$WORK_DIR/voice-mlx-stream.err"
STATUS=$?
set -e
[ "$STATUS" -eq 0 ] || fail "the streamed MLX speech failed ($STATUS): $(cat "$WORK_DIR/voice-mlx-stream.err")"
python3 - "$WORK_DIR/voice-mlx-stream.out" <<'PY' || fail "the MLX speech's stream: $(cat "$WORK_DIR/voice-mlx-stream.out")"
import json, sys
events = [json.loads(line) for line in open(sys.argv[1])]
ready = [e for e in events if e["type"] == "ready"]
assert len(ready) == 1 and ready[0] == {"type": "ready", "device": "gpu", "engine": "mlx"}, ready
assert events[-1]["type"] == "document" and events[-1]["document"]["engine"] == "mlx", events[-1]
PY

# An engine its model does not speak on: refused before anything runs.
set +e
"$APOGEE_BIN" voice say "Hi." --model "$VOICE_MLX_HANDLE" --engine torch -o "$WORK_DIR/out/voice-torch.wav" \
    </dev/null >/dev/null 2>"$WORK_DIR/voice-torch.err"
STATUS=$?
set -e
[ "$STATUS" -eq 1 ] || fail "an MLX voice model spoke on --engine torch ($STATUS)"
grep -q -- "--engine torch: '$VOICE_MLX_HANDLE' is an MLX voice model, which speaks on MLX (mlx-audio), not PyTorch" \
    "$WORK_DIR/voice-torch.err" || fail "the engine refusal: $(cat "$WORK_DIR/voice-torch.err")"
[ ! -e "$WORK_DIR/out/voice-torch.wav" ] || fail "a refused speech wrote a file"

echo "voice say on MLX: recorded with its engine, its timings the WAV's, the build's own voice - OK"

# --- Ctrl-C mid-speech on MLX, the MLX speech driver's whole tree sampled ------
rm -f "$VOICE_MLX_RECORD"
STUB_MLX_AUDIO_HANG=1 STUB_MLX_AUDIO_RECORD="$VOICE_MLX_RECORD" "$APOGEE_BIN" voice say "A long one." \
    --model "$VOICE_MLX_HANDLE" -o "$WORK_DIR/out/voice-mlx-stopped.wav" \
    </dev/null >/dev/null 2>"$WORK_DIR/voice-mlx-stopped.err" &
CHILD=$!
tries=0
until [ -s "$VOICE_MLX_RECORD" ] && grep -q '"say"' "$VOICE_MLX_RECORD"; do
    tries=$((tries + 1))
    [ "$tries" -lt 300 ] || { kill -9 "$CHILD" 2>/dev/null || true; fail "the MLX speech driver never started"; }
    sleep 0.1
done
if command -v lsof >/dev/null 2>&1; then
    SEEN=0
    for _ in 1 2 3 4 5; do
        PIDS="$CHILD"
        for pid in $(descendants "$CHILD"); do
            PIDS="$PIDS,$pid"
            ps -o command= -p "$pid" 2>/dev/null | grep -q voice_say_mlx.py && SEEN=1
        done
        # No socket of any kind anywhere below apogee, and no listener in its tree.
        HIT="$(lsof -a -p "$PIDS" -i -sTCP:LISTEN -Fn 2>/dev/null || true)"
        [ -z "$HIT" ] || { kill -9 "$CHILD" 2>/dev/null || true; fail "INVARIANT VIOLATED: an MLX speech's tree listens: $HIT"; }
        for pid in $(descendants "$CHILD"); do
            SOCKETS="$(lsof -a -p "$pid" -i -Fn 2>/dev/null || true)"
            [ -z "$SOCKETS" ] || { kill -9 "$CHILD" 2>/dev/null || true; fail "INVARIANT VIOLATED: the MLX speech driver's tree holds a socket: $SOCKETS"; }
        done
        sleep 0.1
    done
    [ "$SEEN" = 1 ] || { kill -9 "$CHILD" 2>/dev/null || true; fail "the sample never saw the MLX speech driver, so it proved nothing"; }
fi
kill -INT "$CHILD"
set +e
wait "$CHILD"
STATUS=$?
set -e
[ "$STATUS" -eq 130 ] || fail "Ctrl-C mid-speech on MLX exited $STATUS: $(cat "$WORK_DIR/voice-mlx-stopped.err")"
[ ! -e "$WORK_DIR/out/voice-mlx-stopped.wav" ] || fail "a cancelled MLX speech left a file"
[ -z "$(ls -A "$APOGEE_HOME/cache/video")" ] || fail "a cancelled MLX speech left its working directory"
if pgrep -f "$APOGEE_HOME/training/scripts/voice_say_mlx.py" >/dev/null 2>&1; then
    fail "the MLX speech driver outlived the cancelled run"
fi

echo "voice say on MLX: Ctrl-C mid-speech leaves nothing; no socket in the MLX speech driver's tree - OK"

# The composer narrates on whichever engine its voice model's directory says,
# as each clip runs on its own model's (53m).
set +e
"$APOGEE_BIN" video compose "the tides, twice a day" -m "$MLX_HANDLE" --voice-model "$VOICE_MLX_HANDLE" \
    -o "$WORK_DIR/out/tides-voice-mlx.mp4" --planner planner --output-format json \
    </dev/null >"$WORK_DIR/compose-voice-mlx.out" 2>"$WORK_DIR/compose-voice-mlx.err"
STATUS=$?
set -e
[ "$STATUS" -eq 0 ] || fail "video compose with an MLX voice failed ($STATUS): $(cat "$WORK_DIR/compose-voice-mlx.err")"
for scene in s01 s02; do
    [ "$(field "$WORK_DIR/out/tides-voice-mlx.scenes/$scene.wav.json" '["engine"]')" = "mlx" ] \
        || fail "the composition's $scene narration did not speak on MLX"
done
if grep -q "chatter\|noise on stderr" "$WORK_DIR/compose-voice-mlx.out" "$WORK_DIR/compose-voice-mlx.err"; then
    fail "mlx-audio's output reached compose's streams"
fi

echo "video compose: narrated on MLX through the voice model's own engine - OK"

# --- mlx-video not installed: refused on a pipe, naming its setup -------------
rm -rf "$SITE/mlx_video"
generate mlx_missing -o "$WORK_DIR/out/mlx-missing.mp4" --model "$MLX_HANDLE"
[ "$STATUS" -ne 0 ] || fail "a run without mlx-video succeeded"
grep -q "mlx-video is not installed in the Python environment at .* -- apogee train setup --with mlx-video" \
    "$WORK_DIR/mlx_missing.err" || fail "the refusal: $(cat "$WORK_DIR/mlx_missing.err")"

echo "video generate on MLX: mlx-video missing refused naming its setup - OK"

# --- mlx-audio not installed: refused on a pipe, naming its setup -------------
rm -rf "$SITE/mlx_audio"
set +e
"$APOGEE_BIN" voice say "Hi." --model "$VOICE_MLX_HANDLE" -o "$WORK_DIR/out/voice-missing.wav" \
    </dev/null >/dev/null 2>"$WORK_DIR/voice-missing.err"
STATUS=$?
set -e
[ "$STATUS" -ne 0 ] || fail "a speech without mlx-audio succeeded"
grep -q "mlx-audio is not installed in the Python environment at .* -- apogee train setup --with mlx-audio" \
    "$WORK_DIR/voice-missing.err" || fail "the refusal: $(cat "$WORK_DIR/voice-missing.err")"

echo "voice say on MLX: mlx-audio missing refused naming its setup - OK"
