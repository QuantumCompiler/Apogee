"""A stand-in diffusers for the video driver's suite (49b): a pipeline loaded
by the class its model_index.json names, called as diffusers calls one, and
answering with frames that save as tiny files -- never a model, never a
tensor.

Steered by the environment:
  STUB_VIDEO_RECORD  a file each load and each call is appended to, as JSON
  STUB_VIDEO_ERROR   the call raises this
  STUB_VIDEO_HANG    the call never returns after its first step
"""

import json
import os
import sys
import time

from . import utils

__version__ = "0.0-stub"


def _record(entry):
    path = os.environ.get("STUB_VIDEO_RECORD")
    if path:
        with open(path, "a", encoding="utf-8") as out:
            out.write(json.dumps(entry) + "\n")


class _Image:
    def __init__(self, width, height, index):
        self.size = (width, height)
        self.index = index

    def save(self, path, format=None):
        with open(path, "wb") as out:
            out.write(b"PNG frame %d" % self.index)


class _Result:
    def __init__(self, frames):
        self.frames = [frames]


class _Pipeline:
    def __init__(self, path, options):
        self.path = path
        self.options = options
        self.device = None

    def to(self, device):
        self.device = device
        return self

    @classmethod
    def from_pipe(cls, pipe):
        made = cls(pipe.path, pipe.options)
        _record({"from_pipe": type(pipe).__name__, "into": cls.__name__})
        return made

    def __call__(self, prompt, height, width, num_frames, num_inference_steps, generator=None,
                 output_type="np", negative_prompt=None, guidance_scale=None,
                 callback_on_step_end=None, image=None):
        # A library's chatter on both streams: neither may reach the protocol.
        print("stub diffusers: a library's chatter on stdout")
        sys.stderr.write("stub diffusers: noise on stderr\n")
        _record({
            "call": type(self).__name__,
            "prompt": prompt,
            "negative_prompt": negative_prompt,
            "height": height,
            "width": width,
            "frames": num_frames,
            "steps": num_inference_steps,
            "seed": generator.seed if generator is not None else None,
            "generator_device": generator.device if generator is not None else None,
            "guidance": guidance_scale,
            "image": None if image is None else image.path,
            "output_type": output_type,
            "device": self.device,
            "options": {key: str(value) for key, value in self.options.items()},
        })
        if os.environ.get("STUB_VIDEO_ERROR"):
            raise RuntimeError(os.environ["STUB_VIDEO_ERROR"])
        for step in range(num_inference_steps):
            if callback_on_step_end is not None:
                callback_on_step_end(self, step, 1000 - step, {})
            if os.environ.get("STUB_VIDEO_HANG"):
                while True:
                    time.sleep(0.1)
        return _Result([_Image(width, height, index) for index in range(num_frames)])


class WanPipeline(_Pipeline):
    pass


class WanImageToVideoPipeline(_Pipeline):
    pass


class HunyuanVideoPipeline(_Pipeline):
    """A video pipeline diffusers has no image-to-video sibling for here."""


class StableDiffusionPipeline(_Pipeline):
    """An image pipeline: it takes no num_frames."""

    def __call__(self, prompt, height, width, num_inference_steps, generator=None,
                 output_type="pil"):
        return _Result([])


class DiffusionPipeline:
    @staticmethod
    def from_pretrained(path, **options):
        with open(os.path.join(path, "model_index.json"), encoding="utf-8") as handle:
            name = json.load(handle).get("_class_name", "WanPipeline")
        _record({"load": path, "class": name,
                 "options": {key: str(value) for key, value in options.items()}})
        cls = globals().get(name)
        if cls is None:
            raise ValueError(f"stub diffusers knows no {name}")
        return cls(path, options)
