"""A stub `mlx_lm` for the MLX driver's protocol suite: no Metal, no weights,
no network. See `_stub.py` for the switches."""

import os
import sys

from mlx_lm._stub import Model, record
from mlx_lm._version import __version__  # noqa: F401 -- where mlx-lm keeps it
from mlx_lm.tokenizer import Tokenizer


def load(path, adapter_path=None):
    record(f"load {os.path.basename(os.path.normpath(path))}")
    if adapter_path is not None:
        # The adapter mlx-lm would apply over the weights (53a), recorded as
        # the loader was handed it.
        record(f"adapter {os.path.basename(os.path.normpath(adapter_path))}")
    noise = os.environ.get("STUB_MLX_NOISE")
    if noise:
        print(noise)
        os.write(1, (noise + "\n").encode())
        print(noise, file=sys.stderr, flush=True)
    error = os.environ.get("STUB_MLX_LOAD_ERROR")
    if error:
        raise ValueError(error)
    return Model(), Tokenizer()
