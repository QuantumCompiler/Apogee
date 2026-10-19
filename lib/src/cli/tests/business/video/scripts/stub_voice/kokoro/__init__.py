"""A stand-in kokoro for the speech driver's suite (50a): a model and a
pipeline called as kokoro's are, answering each sentence with silence of a
length its text decides -- 100 samples a character at 24 kHz, divided by the
speed -- so durations are known before the test runs. Never a model.

Steered by the environment:
  STUB_VOICE_RECORD  a file each load and each call is appended to, as JSON
  STUB_VOICE_ERROR   the pipeline raises this
  STUB_VOICE_HANG    the pipeline never returns
"""

import json
import os
import sys
import time

__version__ = "0.0-stub"


def _record(entry):
    path = os.environ.get("STUB_VOICE_RECORD")
    if path:
        with open(path, "a", encoding="utf-8") as out:
            out.write(json.dumps(entry) + "\n")


class KModel:
    def __init__(self, repo_id=None, config=None, model=None, disable_complex=False):
        _record({"model": model, "config": config, "repo_id": repo_id})
        self.device = None

    def to(self, device):
        self.device = device
        return self

    def eval(self):
        return self


class _Result:
    def __init__(self, graphemes, audio):
        self.graphemes = graphemes
        self.phonemes = ""
        self.audio = audio


class KPipeline:
    def __init__(self, lang_code, repo_id=None, model=True, **kwargs):
        _record({"pipeline": lang_code, "has_model": isinstance(model, KModel)})
        self.model = model

    def __call__(self, text, voice=None, speed=1, **kwargs):
        # A library's chatter on both streams: neither may reach the protocol.
        print("stub kokoro: a library's chatter on stdout")
        sys.stderr.write("stub kokoro: noise on stderr\n")
        _record({"say": text, "voice": voice, "speed": speed,
                 "device": getattr(self.model, "device", None)})
        if os.environ.get("STUB_VOICE_ERROR"):
            raise RuntimeError(os.environ["STUB_VOICE_ERROR"])
        if os.environ.get("STUB_VOICE_HANG"):
            while True:
                time.sleep(0.1)
        yield _Result(text, [0.25] * int(len(text) * 100 / speed))
