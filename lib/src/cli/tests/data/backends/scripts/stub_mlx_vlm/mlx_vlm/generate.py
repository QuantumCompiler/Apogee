"""The stub's generation: the prompt checked against the images -- one image
marker per picture, as a real processor insists -- and against the
recordings, one audio marker per WAV handed over as `audio=` (53g); then the
scripted reply a character at a time, each a result carrying the counts
mlx-vlm's do.

With a drafter (53i) -- `draft_model` as `load_drafter` handed it back, its
`draft_kind` and, when given, `draft_block_size` -- the pairing is recorded
(`vlm draft <directory> kind=<kind> block=<block or the drafter's>`) and the
reply comes a verified block at a time, each result several characters, as a
speculative round's accepted tokens do: the text is the model's own, the
same as without a drafter, at temperature 0 as at any."""

import os
import time

from mlx_lm._stub import record

IMAGE_MARKER = "<|image|>"
AUDIO_MARKER = "<|audio|>"


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


def heard(recordings):
    """What was heard: "1 recording: 32044 bytes .wav, a WAV" -- each file's
    size, its extension, and whether it opens as a RIFF WAVE."""
    parts = []
    for path in recordings:
        with open(path, "rb") as f:
            data = f.read()
        wav = data[:4] == b"RIFF" and data[8:12] == b"WAVE"
        parts.append(f"{len(data)} bytes {os.path.splitext(path)[1]}"
                     f"{', a WAV' if wav else ', not a WAV'}")
    noun = "recording" if len(recordings) == 1 else "recordings"
    return f"{len(recordings)} {noun}: " + "; ".join(parts)


def stream_generate(model, processor, prompt, image=None, audio=None, **kwargs):
    images = list(image or [])
    recordings = list(audio or [])
    record(f"vlm generate images={len(images)} options={','.join(sorted(kwargs))}")
    for path in images:
        record(f"vlm image {path}")
    for path in recordings:
        record(f"vlm audio {path}")
    if prompt.count(IMAGE_MARKER) != len(images):
        raise ValueError(f"the prompt places {prompt.count(IMAGE_MARKER)} image(s) "
                         f"and {len(images)} were given")
    if prompt.count(AUDIO_MARKER) != len(recordings):
        raise ValueError(f"the prompt places {prompt.count(AUDIO_MARKER)} recording(s) "
                         f"and {len(recordings)} were given")
    for path in images:
        if not os.path.isfile(path):
            raise ValueError(f"no image at {path}")
    for path in recordings:
        if not os.path.isfile(path):
            raise ValueError(f"no recording at {path}")
    block = 1
    drafter = kwargs.get("draft_model")
    if drafter is not None or "draft_kind" in kwargs or "draft_block_size" in kwargs:
        from mlx_vlm.speculative.drafters import Drafter

        if not isinstance(drafter, Drafter) or kwargs.get("draft_kind") != "mtp":
            raise ValueError("speculative decoding needs a drafter load_drafter loaded, of "
                             f"kind mtp (got {type(drafter).__name__}, "
                             f"{kwargs.get('draft_kind')!r})")
        block = int(kwargs.get("draft_block_size") or drafter.block_size)
        record(f"vlm draft {os.path.basename(os.path.normpath(drafter.path))} "
               f"kind={kwargs['draft_kind']} "
               f"block={kwargs.get('draft_block_size') or 'drafter:' + str(drafter.block_size)}")
    reply = model.next_reply(prompt).replace("{images}", seen(images))
    reply = reply.replace("{audio}", heard(recordings))
    max_tokens = kwargs.get("max_tokens", 256)
    delay = float(os.environ.get("STUB_MLX_DELAY_MS", "0")) / 1000.0
    text = reply[:max_tokens]
    for start in range(0, len(text), block):
        if delay:
            time.sleep(delay)
        piece = text[start:start + block]
        yield GenerationResult(piece, len(prompt), start + len(piece))
