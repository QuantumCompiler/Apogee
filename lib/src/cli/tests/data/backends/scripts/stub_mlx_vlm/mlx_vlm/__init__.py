"""A stub `mlx_vlm` for the MLX driver's vision suite (27c): no Metal, no
weights, no network. Laid beside the stub `mlx_lm` only where a test asks for
it, so the same runtime can be had with mlx-vlm and without.

  STUB_MLX_VLM_LOAD_ERROR  load() raises ValueError with this message
  STUB_MLX_VLM_LEGACY      "1": the step takes mlx-vlm's own knobs
                           (`temperature`, `top_p`) and no sampler

The reply is the scripted one `mlx_lm`'s stub hands out (STUB_MLX_REPLIES),
with `{images}` replaced by what was seen: "2 images: 1234 bytes .png, 99
bytes .jpg" -- so a test can tell the pictures reached the model -- and,
since 53g, `{audio}` by what was heard through `audio=`: "1 recording: 32044
bytes .wav, a WAV". Each recording's path is recorded as `vlm audio <path>`.

Since 53f it converts too (`convert.py`), and says which types it builds the
way a real mlx-vlm does: a package under `models/` naming its `Model`, and
`utils.py`'s remap table -- `gemma4`, `gemma4_unified`, `qwen3_5` and
`qwen3_vl`, never `qwen3_omni_moe`, which a test adds to its own copy.

Since 53i it drafts (`speculative/drafters`): `load_drafter` loads Gemma 4's
assistant, `gemma4_assistant`, and `stream_generate` records the pairing and
answers through the speculative path with the model's own text. The unified
12B's `gemma4_unified_assistant` is not here; a test adds it to its copy.
"""

import os

from mlx_lm._stub import Model, record
from mlx_vlm.generate import stream_generate  # noqa: F401 -- where mlx-vlm exports it
from mlx_vlm.version import __version__  # noqa: F401


class Processor:
    """What load() hands back beside the model: the image processor."""


def load(path, adapter_path=None, **_):
    record(f"vlm load {os.path.basename(os.path.normpath(path))}")
    if adapter_path is not None:
        record(f"vlm adapter {os.path.basename(os.path.normpath(adapter_path))}")
    error = os.environ.get("STUB_MLX_VLM_LOAD_ERROR")
    if error:
        raise ValueError(error)
    return Model(), Processor()
