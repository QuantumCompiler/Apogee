"""A stub `mlx_vlm` for the MLX driver's vision suite (27c): no Metal, no
weights, no network. Laid beside the stub `mlx_lm` only where a test asks for
it, so the same runtime can be had with mlx-vlm and without.

  STUB_MLX_VLM_LOAD_ERROR  load() raises ValueError with this message
  STUB_MLX_VLM_LEGACY      "1": the step takes mlx-vlm's own knobs
                           (`temperature`, `top_p`) and no sampler

The reply is the scripted one `mlx_lm`'s stub hands out (STUB_MLX_REPLIES),
with `{images}` replaced by what was seen: "2 images: 1234 bytes .png, 99
bytes .jpg" -- so a test can tell the pictures reached the model.
"""

import os

from mlx_lm._stub import Model, record
from mlx_vlm.generate import stream_generate  # noqa: F401 -- where mlx-vlm exports it
from mlx_vlm.version import __version__  # noqa: F401


class Processor:
    """What load() hands back beside the model: the image processor."""


def load(path, **_):
    record(f"vlm load {os.path.basename(os.path.normpath(path))}")
    error = os.environ.get("STUB_MLX_VLM_LOAD_ERROR")
    if error:
        raise ValueError(error)
    return Model(), Processor()
