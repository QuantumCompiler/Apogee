"""A stand-in for mlx-audio's Kokoro (53m): a model called as the real one is
-- `generate(text, voice, speed, lang_code)` -- answering each call with
silence of a length its text decides -- 100 samples a character at 24 kHz,
divided by the speed -- so durations are known before the test runs, as the
PyTorch suite's stub kokoro does. A voice is a path to its own SafeTensors
pack, as Apogee hands it over; anything else is refused as the real one,
offline, would fail to find it. Never a model.

Steered by the environment:
  STUB_MLX_AUDIO_RECORD  a file each load and each call is appended to, as JSON
  STUB_MLX_AUDIO_ERROR   generate raises this
  STUB_MLX_AUDIO_HANG    generate never returns
"""

import json
import os
import sys
import time

import mlx.core as mx


def _record(entry):
    path = os.environ.get("STUB_MLX_AUDIO_RECORD")
    if path:
        with open(path, "a", encoding="utf-8") as out:
            out.write(json.dumps(entry) + "\n")


class GenerationResult:
    def __init__(self, audio, sample_rate):
        self.audio = audio
        self.sample_rate = sample_rate


class Model:
    def __init__(self, config):
        self.config = config

    @property
    def sample_rate(self):
        return int(self.config.get("sample_rate", 24000))

    def generate(self, text, voice=None, speed=1.0, lang_code="a", split_pattern=r"\n+",
                 **kwargs):
        # A library's chatter on both streams: neither may reach the protocol.
        print("stub mlx-audio: a library's chatter on stdout")
        sys.stderr.write("stub mlx-audio: noise on stderr\n")
        _record({"say": text, "voice": voice, "speed": speed, "lang_code": lang_code})
        if os.environ.get("STUB_MLX_AUDIO_ERROR"):
            raise RuntimeError(os.environ["STUB_MLX_AUDIO_ERROR"])
        if os.environ.get("STUB_MLX_AUDIO_HANG"):
            while True:
                time.sleep(0.1)
        if not (voice or "").endswith(".safetensors") or not os.path.isfile(voice):
            raise FileNotFoundError(f"no voice pack {voice} (offline: the hub is never asked)")
        yield GenerationResult(mx.array([0.25] * int(len(text) * 100 / speed)), self.sample_rate)
