#!/usr/bin/env python3
"""Speech driver for `apogee voice say` and the composer's narration (50a).

Speaks text segments with ONE voice model from Apogee's model store -- a
Kokoro directory: config.json, its checkpoint (*.pth) and its voices
(voices/*.pt) -- and writes each segment as a 16-bit mono WAV, reporting its
duration measured from the samples it wrote, sentence by sentence. It never
downloads a model, never reads Apogee's config and never touches the store:
Apogee names the model, the voice, the text and a working directory, and
measures the files again itself.

Runs under the Python environment Apogee owns, never the system Python.
Requires the voice set there:  apogee train setup --with voice
(torch>=2.6, kokoro>=0.9.4, misaki[en]>=0.9.4 -- floors; the versions that
ran are reported on the "ready" line and kept in the run's document).

    voice_say.py --request <request.json>

The request, written by Apogee as UTF-8 JSON so no text is ever an argument:

  {"model_dir": "<dir>", "voice": "af_heart", "speed": 1.0,
   "segments": ["...", "..."], "out_dir": "<dir>"}

Each segment is split into sentences by a plain rule -- an end mark (. ! ?
or an ellipsis) followed by space -- never by a model; each sentence is
spoken on its own and the segment's file is the sentences in order.

The training drivers' protocol, one JSON object per line on stdout:
  {"message": "..."}                                       as it goes
  {"error": "..."}                                         fatal; a non-zero exit follows
  {"event": "ready", "device", "torch", "kokoro", "sample_rate"}
  {"event": "segment", "index": N, "of": N, "file": "...", "seconds": S,
   "sentences": [{"text": "...", "seconds": S}, ...]}      one per segment
  {"event": "done", "segments": N, "seconds": S}           at the end

Everything a library prints goes to stderr, which Apogee captures: the
protocol's stdout is a duplicate of the original descriptor, and descriptor 1
itself is pointed at stderr before anything is imported.
"""

import argparse
import array
import json
import os
import re
import signal
import sys
import wave

# ── Protective environment, before any ML import ─────────────────────────────
os.environ.setdefault("KMP_DUPLICATE_LIB_OK", "TRUE")
os.environ.setdefault("TOKENIZERS_PARALLELISM", "false")
# The model is a local directory. These make anything else fail loudly
# rather than download quietly.
os.environ["HF_HUB_OFFLINE"] = "1"
os.environ["TRANSFORMERS_OFFLINE"] = "1"
os.environ["HF_HUB_DISABLE_TELEMETRY"] = "1"

# Ctrl-C at a terminal reaches the whole foreground process group. Apogee
# ends this child itself and removes what it wrote.
signal.signal(signal.SIGINT, signal.SIG_IGN)

_OUT = os.fdopen(os.dup(1), "w", encoding="utf-8", newline="\n")
os.dup2(2, 1)
sys.stdout = sys.stderr

SAMPLE_RATE = 24000
SENTENCE_END = re.compile(r"(?<=[.!?…])\s+")


def emit(obj):
    _OUT.write(json.dumps(obj) + "\n")
    _OUT.flush()


def fail(message, code=1):
    emit({"error": message})
    return code


def arguments(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--request", required=True)
    return parser.parse_args(argv)


def read_request(path):
    with open(path, encoding="utf-8") as handle:
        request = json.load(handle)
    for key in ("model_dir", "voice", "segments", "out_dir"):
        if key not in request:
            raise ValueError(f"the request names no {key}")
    return request


def sentences_of(text):
    """The segment's sentences, by the plain rule; the text whole if none."""
    parts = [part.strip() for part in SENTENCE_END.split(text.strip())]
    return [part for part in parts if part] or ([text.strip()] if text.strip() else [])


def pcm16(audio):
    """Float samples in [-1, 1] as 16-bit little-endian PCM."""
    if hasattr(audio, "detach"):
        audio = audio.detach().cpu().numpy()
    try:
        import numpy

        return (numpy.clip(numpy.asarray(audio, dtype="float32"), -1.0, 1.0) * 32767).astype(
            "<i2"
        ).tobytes()
    except ImportError:
        samples = array.array("h", (int(max(-1.0, min(1.0, float(x))) * 32767) for x in audio))
        if sys.byteorder == "big":
            samples.byteswap()
        return samples.tobytes()


def checkpoint_of(model_dir):
    names = sorted(name for name in os.listdir(model_dir) if name.endswith(".pth"))
    if len(names) != 1:
        raise ValueError(f"{model_dir} holds {len(names)} checkpoints -- a voice model is one")
    return os.path.join(model_dir, names[0])


def main(argv):
    args = arguments(argv)
    try:
        request = read_request(args.request)
    except (OSError, ValueError) as exc:
        return fail(f"cannot read the request {args.request}: {exc}", 2)
    model_dir = request["model_dir"]
    voice_file = os.path.join(model_dir, "voices", request["voice"] + ".pt")
    if not os.path.isfile(os.path.join(model_dir, "config.json")):
        return fail(f"{model_dir} is not a voice model (no config.json)", 2)
    if not os.path.isfile(voice_file):
        return fail(f"{model_dir} has no voice {request['voice']} (no voices/{request['voice']}.pt)", 2)
    try:
        checkpoint = checkpoint_of(model_dir)
    except (OSError, ValueError) as exc:
        return fail(str(exc), 2)
    os.makedirs(request["out_dir"], exist_ok=True)

    emit({"message": "loading torch and kokoro"})
    try:
        import torch
        import kokoro
        from kokoro import KModel, KPipeline
    except ImportError as exc:
        return fail(
            f"the voice runtime is not installed in Apogee's Python environment ({exc}) -- "
            "apogee train setup --with voice",
            3,
        )

    # An 82M-parameter model speaks faster than real time on a CPU; a GPU
    # when there is one, never Apple's MPS, whose operator gaps it would meet.
    device = "cuda" if torch.cuda.is_available() else "cpu"
    emit({"message": "loading the voice model"})
    try:
        model = KModel(config=os.path.join(model_dir, "config.json"), model=checkpoint)
        model = model.to(device).eval()
        pipeline = KPipeline(lang_code=request["voice"][0], model=model)
    except Exception as exc:  # the library's own words
        return fail(f"could not load the voice model: {type(exc).__name__}: {exc}", 4)

    emit(
        {
            "event": "ready",
            "device": device,
            "torch": getattr(torch, "__version__", ""),
            "kokoro": getattr(kokoro, "__version__", ""),
            "sample_rate": SAMPLE_RATE,
        }
    )

    speed = float(request.get("speed", 1.0))
    segments = request["segments"]
    total = 0.0
    for index, text in enumerate(segments, start=1):
        path = os.path.join(request["out_dir"], f"s{index:05d}.wav")
        spoken = []
        pcm = b""
        try:
            for sentence in sentences_of(text):
                sentence_pcm = b""
                for result in pipeline(sentence, voice=voice_file, speed=speed):
                    audio = getattr(result, "audio", None)
                    if audio is None:
                        audio = result[2]
                    if audio is not None:
                        sentence_pcm += pcm16(audio)
                spoken.append({"text": sentence, "seconds": len(sentence_pcm) / 2 / SAMPLE_RATE})
                pcm += sentence_pcm
        except Exception as exc:
            return fail(f"speech failed: {type(exc).__name__}: {exc}", 5)
        with wave.open(path, "wb") as out:
            out.setnchannels(1)
            out.setsampwidth(2)
            out.setframerate(SAMPLE_RATE)
            out.writeframes(pcm)
        seconds = len(pcm) / 2 / SAMPLE_RATE
        total += seconds
        emit(
            {
                "event": "segment",
                "index": index,
                "of": len(segments),
                "file": path,
                "seconds": seconds,
                "sentences": spoken,
            }
        )
    emit({"event": "done", "segments": len(segments), "seconds": total})
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
