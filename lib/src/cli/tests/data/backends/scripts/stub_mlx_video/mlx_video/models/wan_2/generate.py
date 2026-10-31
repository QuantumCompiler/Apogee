"""A stub of mlx-video's Wan generator (53l), `mlx_video.models.wan_2.generate`
at the commit Apogee pins: no Metal, no weights, no network. Its entry is the
real one's, `generate_video`, with the real signature, and it does what the
real one does around the denoising: reads the model's `config.json`, refuses
a frame count that is not 4n+1 with the real assertion, asks
`AutoTokenizer.from_pretrained("google/umt5-xxl")` of transformers from
inside the call, walks its steps under the module's own `tqdm` bar
(`desc="Diffusion"`), and hands `save_video` -- imported into this module, as
the real one is -- its frames, `num_frames` of them, each a (height, width,
3) block of 8-bit RGB. Like the real one it prints its progress on stdout,
which the driver must keep off the protocol, and notes on stderr. Steered by
the environment:

  STUB_MLX_VIDEO_RECORD   a file each call appends to, one JSON object a
                          line: the call (`"generate"`), then the tokenizer
                          it was handed
  STUB_MLX_VIDEO_ERROR    the call raises RuntimeError with this message,
                          after its first step
  STUB_MLX_VIDEO_HANG     the call never returns after its first step
  STUB_MLX_VIDEO_NO_LORA  the entry is an older one, taking no LoRA
"""

from __future__ import annotations

import json
import os
import random
import sys
import time
from pathlib import Path

from tqdm import tqdm

from mlx_video.models.wan_2.postprocess import save_video


def _record(entry):
    path = os.environ.get("STUB_MLX_VIDEO_RECORD")
    if path:
        with open(path, "a", encoding="utf-8") as out:
            out.write(json.dumps(entry) + "\n")


class _Frame:
    """One decoded frame: what the real one's uint8 array offers the driver."""

    def __init__(self, height, width, index):
        self.shape = (height, width, 3)
        self._bytes = bytes([(index * 40) % 256, 64, 128]) * (height * width)

    def tobytes(self):
        return self._bytes


def _generate(model_dir, prompt, negative_prompt, image, width, height, num_frames, steps,
              guide_scale, shift, seed, output_path, loras, loras_high, loras_low):
    model_dir = Path(model_dir)
    with open(model_dir / "config.json") as handle:
        config = json.load(handle)
    assert (num_frames - 1) % 4 == 0, f"num_frames must be 4n+1, got {num_frames}"
    if steps is None:
        steps = config.get("sample_steps", 50)
    if seed < 0:
        seed = random.randint(0, 2**32 - 1)
    # The real one's banner, on stdout; a note on stderr.
    print(f"Wan2.2 {config.get('model_type', 't2v').upper()} Generation (MLX)")
    print(f"  Prompt: {prompt}")
    sys.stderr.write("stub mlx-video: noise on stderr\n")

    from transformers import AutoTokenizer

    tokenizer = AutoTokenizer.from_pretrained("google/umt5-xxl")
    _record({
        "generate": str(model_dir),
        "prompt": prompt,
        "negative_prompt": negative_prompt,
        "image": image,
        "width": width,
        "height": height,
        "num_frames": num_frames,
        "steps": steps,
        "guide_scale": guide_scale,
        "shift": shift,
        "seed": seed,
        "output_path": output_path,
        "loras": [list(lora) for lora in loras] if loras else None,
        "loras_high": loras_high,
        "loras_low": loras_low,
    })
    _record({"tokenizer": tokenizer.path, "local_files_only": tokenizer.local_files_only})

    print(f"Denoising ({steps} steps)...")
    for i, _t in enumerate(tqdm(range(steps), desc="Diffusion")):
        if i == 1 and os.environ.get("STUB_MLX_VIDEO_HANG"):
            while True:
                time.sleep(1)
        if i == 1 and os.environ.get("STUB_MLX_VIDEO_ERROR"):
            raise RuntimeError(os.environ["STUB_MLX_VIDEO_ERROR"])
    if steps == 1 and os.environ.get("STUB_MLX_VIDEO_ERROR"):
        raise RuntimeError(os.environ["STUB_MLX_VIDEO_ERROR"])
    if steps == 1 and os.environ.get("STUB_MLX_VIDEO_HANG"):
        while True:
            time.sleep(1)

    video = [_Frame(height, width, index) for index in range(num_frames)]
    save_video(video, output_path, fps=config.get("sample_fps", 16))
    print(f"Video saved to {output_path}")


if os.environ.get("STUB_MLX_VIDEO_NO_LORA"):
    def generate_video(
        model_dir: str,
        prompt: str,
        negative_prompt: str | None = None,
        image: str | None = None,
        width: int = 1280,
        height: int = 704,
        num_frames: int = 81,
        steps: int = None,
        guide_scale: str | float | tuple = None,
        shift: float = None,
        seed: int = -1,
        output_path: str = "output.mp4",
        scheduler: str = "unipc",
        tiling: str = "auto",
    ):
        _generate(model_dir, prompt, negative_prompt, image, width, height, num_frames, steps,
                  guide_scale, shift, seed, output_path, None, None, None)
else:
    def generate_video(
        model_dir: str,
        prompt: str,
        negative_prompt: str | None = None,
        image: str | None = None,
        width: int = 1280,
        height: int = 704,
        num_frames: int = 81,
        steps: int = None,
        guide_scale: str | float | tuple = None,
        shift: float = None,
        seed: int = -1,
        output_path: str = "output.mp4",
        scheduler: str = "unipc",
        loras: list | None = None,
        loras_high: list | None = None,
        loras_low: list | None = None,
        tiling: str = "auto",
        no_compile: bool = False,
        trim_first_frames: int = 0,
        debug_latents: bool = False,
    ):
        _generate(model_dir, prompt, negative_prompt, image, width, height, num_frames, steps,
                  guide_scale, shift, seed, output_path, loras, loras_high, loras_low)
