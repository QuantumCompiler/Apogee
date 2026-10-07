"""A stub `mlx_lm` for the MLX driver's protocol suite: no Metal, no weights,
no network. See `_stub.py` for the switches."""

import os
import sys

from mlx_lm._stub import Model, record
from mlx_lm._version import __version__  # noqa: F401 -- where mlx-lm keeps it
from mlx_lm.tokenizer import Tokenizer


def load(path):
    record(f"load {os.path.basename(os.path.normpath(path))}")
    noise = os.environ.get("STUB_MLX_NOISE")
    if noise:
        print(noise)
        os.write(1, (noise + "\n").encode())
        print(noise, file=sys.stderr, flush=True)
    error = os.environ.get("STUB_MLX_LOAD_ERROR")
    if error:
        raise ValueError(error)
    return Model(), Tokenizer()
