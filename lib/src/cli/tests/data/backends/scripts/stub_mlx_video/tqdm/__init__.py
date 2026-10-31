"""A stub `tqdm` beside the stub `mlx_video` (53l): mlx-video's generator walks
its denoising steps under `tqdm(range(steps), desc="Diffusion")`, the real
bar drawn on stderr. This one draws a line there per step, and yields."""

import sys


def tqdm(iterable=None, desc=None, **_):
    total = len(iterable) if hasattr(iterable, "__len__") else "?"
    for index, item in enumerate(iterable, start=1):
        yield item
        sys.stderr.write(f"{desc}: {index}/{total}\n")
