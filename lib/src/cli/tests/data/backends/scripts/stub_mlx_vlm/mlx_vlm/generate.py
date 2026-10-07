"""The stub's generation: the prompt checked against the images -- one image
marker per picture, as a real processor insists -- then the scripted reply a
character at a time, each a result carrying the counts mlx-vlm's do."""

import os
import time

from mlx_lm._stub import record

IMAGE_MARKER = "<|image|>"


class GenerationResult:
    def __init__(self, text, prompt_tokens, generation_tokens):
        self.text = text
        self.prompt_tokens = prompt_tokens
        self.generation_tokens = generation_tokens


if os.environ.get("STUB_MLX_VLM_LEGACY") == "1":

    def generate_step(input_ids, model, pixel_values, mask, *, max_tokens=256,
                      temperature=0.0, top_p=1.0, repetition_penalty=None, **kwargs):
        raise NotImplementedError

else:

    def generate_step(input_ids, model, pixel_values, mask, *, max_tokens=256,
                      temperature=0.0, top_p=1.0, repetition_penalty=None, sampler=None,
                      logits_processors=None, prompt_cache=None, **kwargs):
        raise NotImplementedError


def seen(images):
    parts = []
    for path in images:
        with open(path, "rb") as f:
            size = len(f.read())
        parts.append(f"{size} bytes {os.path.splitext(path)[1]}")
    noun = "image" if len(images) == 1 else "images"
    return f"{len(images)} {noun}: " + ", ".join(parts)


def stream_generate(model, processor, prompt, image=None, audio=None, **kwargs):
    images = list(image or [])
    record(f"vlm generate images={len(images)} options={','.join(sorted(kwargs))}")
    for path in images:
        record(f"vlm image {path}")
    if prompt.count(IMAGE_MARKER) != len(images):
        raise ValueError(f"the prompt places {prompt.count(IMAGE_MARKER)} image(s) "
                         f"and {len(images)} were given")
    for path in images:
        if not os.path.isfile(path):
            raise ValueError(f"no image at {path}")
    reply = model.next_reply(prompt).replace("{images}", seen(images))
    max_tokens = kwargs.get("max_tokens", 256)
    delay = float(os.environ.get("STUB_MLX_DELAY_MS", "0")) / 1000.0
    for produced, character in enumerate(reply[:max_tokens], start=1):
        if delay:
            time.sleep(delay)
        yield GenerationResult(character, len(prompt), produced)
