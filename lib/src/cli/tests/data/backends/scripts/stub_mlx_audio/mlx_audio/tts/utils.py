"""The stub's `mlx_audio.tts.utils`: `load_model`, as the real one is called --
a local directory in, its Kokoro out -- recorded, and its library's chatter
on both streams. Steered by the environment, beside the Kokoro's own:

  STUB_MLX_AUDIO_RECORD      a file each load and each call is appended to
  STUB_MLX_AUDIO_LOAD_ERROR  load_model() raises RuntimeError with this message
"""

import json
import os
import sys

from mlx_audio.tts.models.kokoro import Model, _record


def load_model(model_path, lazy=False, strict=True, **kwargs):
    # A library's chatter on both streams -- and straight onto descriptor 1,
    # as a C extension would write: none of it may reach the protocol.
    print("stub mlx-audio: loading chatter on stdout")
    os.write(1, b"stub mlx-audio: chatter straight onto descriptor 1\n")
    sys.stderr.write("stub mlx-audio: loading noise on stderr\n")
    _record({"load": str(model_path)})
    error = os.environ.get("STUB_MLX_AUDIO_LOAD_ERROR")
    if error:
        raise RuntimeError(error)
    with open(os.path.join(str(model_path), "config.json"), encoding="utf-8") as handle:
        config = json.load(handle)
    return Model(config)
