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
# runtime refused naming the command that installs it.
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

echo "video generate: a missing runtime refused on a pipe - OK"
