#!/usr/bin/env python3
"""Speech driver for `apogee voice say` on MLX (53m).

Speaks text segments with ONE MLX voice model from Apogee's model store --
mlx-audio's build of Kokoro (`apogee models convert <voice model> --mlx`):
its config.json, the weights and its voice packs (voices/*.safetensors) --
through mlx-audio's own Kokoro, and writes each segment as a 16-bit mono WAV,
reporting its duration counted from the samples it wrote, sentence by
sentence. Like voice_say.py, the PyTorch engine's driver, it never downloads
a model, never reads Apogee's config and never touches the store: Apogee names
the model, the voice, the text and a working directory, and measures the files
again itself.

Runs under the Python environment Apogee owns, never the system Python.
Requires the mlx-audio set there:  apogee train setup --with mlx-audio
(mlx-audio>=0.4.0, misaki[en]>=0.9.4 and torch>=2.6 -- floors; the versions
that ran are reported on the "ready" line and kept in the run's document).

    voice_say_mlx.py --request <request.json>

The request, written by Apogee as UTF-8 JSON so no text is ever an argument
(the PyTorch driver's, its engine named):

  {"engine": "mlx", "model_dir": "<dir>", "voice": "af_heart", "speed": 1.0,
   "segments": ["...", "..."], "out_dir": "<dir>"}

The model is loaded with mlx_audio.tts.utils.load_model(<dir>) and spoken
with its generate(text, voice=..., speed=..., lang_code=...), the voice
handed over as the path to its own pack -- `voices/<voice>.safetensors` in
the model's directory -- which Kokoro on MLX opens as it is, never asking
the Hugging Face hub for one; the language is the voice's first letter, as
Kokoro's voices are named. Each segment is split into sentences by the PyTorch
driver's plain rule -- an end mark (. ! ? or an ellipsis) followed by space --
never by a model; each sentence is spoken on its own and the segment's file is
the sentences in order.

The training drivers' protocol, one JSON object per line on stdout (the
PyTorch driver's, its engine and MLX's versions in its "ready"):
  {"message": "..."}                                       as it goes
  {"error": "..."}                                         fatal; a non-zero exit follows
  {"event": "ready", "device", "engine": "mlx", "mlx", "mlx_audio", "sample_rate"}
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
os.environ.setdefault("TOKENIZERS_PARALLELISM", "false")
# The model and its voices are local files. These make anything else fail
# loudly rather than download quietly.
os.environ["HF_HUB_OFFLINE"] = "1"
os.environ["TRANSFORMERS_OFFLINE"] = "1"
os.environ["HF_HUB_DISABLE_TELEMETRY"] = "1"
os.environ["HF_DATASETS_OFFLINE"] = "1"

# Ctrl-C at a terminal reaches the whole foreground process group. Apogee
# ends this child itself and removes what it wrote.
signal.signal(signal.SIGINT, signal.SIG_IGN)

_OUT = os.fdopen(os.dup(1), "w", encoding="utf-8", newline="\n")
os.dup2(2, 1)
sys.stdout = sys.stderr

# Kokoro's rate, when the model does not say its own.
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


def flat(values):
    """Nested lists of samples, one after another."""
    for value in values:
        if isinstance(value, (list, tuple)):
            yield from flat(value)
        else:
            yield value


def pcm16(audio, mx):
    """Float samples in [-1, 1] -- an MLX array, as Kokoro on MLX yields
    them, of any shape -- as 16-bit little-endian PCM."""
    if hasattr(audio, "astype") and hasattr(mx, "float32"):
        audio = audio.astype(mx.float32)
    try:
        import numpy

        samples = numpy.asarray(audio, dtype="float32").reshape(-1)
        return (numpy.clip(samples, -1.0, 1.0) * 32767).astype("<i2").tobytes()
    except ImportError:
        values = audio.tolist() if hasattr(audio, "tolist") else audio
        samples = array.array("h", (int(max(-1.0, min(1.0, float(x))) * 32767)
                                    for x in flat(values)))
        if sys.byteorder == "big":
            samples.byteswap()
        return samples.tobytes()


def version_of(module, distribution):
    """The version of the package that ran: its own word, else its
    distribution record's, else unstated."""
    own = getattr(module, "__version__", None)
    if own:
        return str(own)
    try:
        from importlib import metadata

        return metadata.version(distribution)
    except Exception:  # no record: a checkout, a copy
        return ""


def main(argv):
    args = arguments(argv)
    try:
        request = read_request(args.request)
    except (OSError, ValueError) as exc:
        return fail(f"cannot read the request {args.request}: {exc}", 2)
    model_dir = request["model_dir"]
    voice = request["voice"]
    voice_file = os.path.join(model_dir, "voices", voice + ".safetensors")
    if not os.path.isfile(os.path.join(model_dir, "config.json")):
        return fail(f"{model_dir} is not an MLX voice model (no config.json)", 2)
    if not os.path.isfile(voice_file):
        return fail(f"{model_dir} has no voice {voice} (no voices/{voice}.safetensors)", 2)
    if "," in voice_file:
        # Kokoro on MLX reads a voice as a list of voices at each comma.
        return fail(f"the voice's path {voice_file} holds a comma, which Kokoro on MLX reads "
                    "between voices -- move the model where its path has none", 2)
    os.makedirs(request["out_dir"], exist_ok=True)

    emit({"message": "loading mlx-audio"})
    try:
        import mlx.core as mx
        import misaki  # noqa: F401 -- Kokoro's phonemizer, asked for before the model loads
        import mlx_audio
        from mlx_audio.tts.utils import load_model
    except ImportError as exc:
        return fail(
            f"the voice runtime on MLX is not installed in Apogee's Python environment ({exc}) "
            "-- apogee train setup --with mlx-audio",
            3,
        )

    emit({"message": "loading the voice model"})
    try:
        model = load_model(model_dir)
    except Exception as exc:  # the library's own words
        return fail(f"could not load the voice model: {type(exc).__name__}: {exc}", 4)
    generate = getattr(model, "generate", None)
    if not callable(generate):
        return fail(f"mlx-audio built {model_dir} as no model it speaks with (no generate) -- "
                    "apogee train setup --with mlx-audio", 4)

    rate = int(getattr(model, "sample_rate", 0) or SAMPLE_RATE)
    default = getattr(mx, "default_device", None)
    device = str(default()) if callable(default) else ""
    device = "gpu" if "gpu" in device else ("cpu" if "cpu" in device else device)
    emit(
        {
            "event": "ready",
            "device": device,
            "engine": "mlx",
            "mlx": version_of(mx, "mlx"),
            "mlx_audio": version_of(mlx_audio, "mlx-audio"),
            "sample_rate": rate,
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
                for result in generate(text=sentence, voice=voice_file, speed=speed,
                                       lang_code=voice[0]):
                    audio = getattr(result, "audio", None)
                    if audio is not None:
                        sentence_pcm += pcm16(audio, mx)
                spoken.append({"text": sentence, "seconds": len(sentence_pcm) / 2 / rate})
                pcm += sentence_pcm
        except Exception as exc:
            return fail(f"speech failed: {type(exc).__name__}: {exc}", 5)
        with wave.open(path, "wb") as out:
            out.setnchannels(1)
            out.setsampwidth(2)
            out.setframerate(rate)
            out.writeframes(pcm)
        seconds = len(pcm) / 2 / rate
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
