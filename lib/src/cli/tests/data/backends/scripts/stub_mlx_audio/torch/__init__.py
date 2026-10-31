"""A stub `torch` beside the stub `mlx_audio` (53m): no tensors, only what the
seeded converter asks of it -- `load`, handing back Kokoro's checkpoint as
the real one is shaped (one state dict a module, its names under
DataParallel's `module.`) and a voice pack as one tensor. Steered by the
environment:

  STUB_MLX_AUDIO_RECORD    a file each load is appended to, as JSON
  STUB_TORCH_LOAD_ERROR    load() raises RuntimeError with this message
"""

import json
import os

__version__ = "0.0-stub"


class Tensor:
    """What the driver asks of a tensor: detached, on the CPU, as 32-bit
    floats, as numbers."""

    def __init__(self, values, floating=True):
        self.values = list(values)
        self.floating = floating

    def detach(self):
        return self

    def cpu(self):
        return self

    def float(self):
        return Tensor([float(value) for value in self.values], True)

    def is_floating_point(self):
        return self.floating

    def numpy(self):
        return list(self.values)


def load(path, map_location=None, weights_only=False):
    record = os.environ.get("STUB_MLX_AUDIO_RECORD")
    if record:
        with open(record, "a", encoding="utf-8") as out:
            out.write(json.dumps({"torch_load": os.path.basename(path),
                                  "map_location": map_location,
                                  "weights_only": weights_only}) + "\n")
    error = os.environ.get("STUB_TORCH_LOAD_ERROR")
    if error:
        raise RuntimeError(error)
    if str(path).endswith(".pth"):
        return {
            "bert": {"module.embeddings.word_embeddings.weight": Tensor([0.5] * 8),
                     "module.embeddings.position_ids": Tensor([0, 1, 2], floating=False)},
            "predictor": {"module.lstm.weight_ih_l0": Tensor([0.25] * 4)},
            "decoder": {"module.generator.conv_post.weight_v": Tensor([0.125] * 4)},
        }
    return Tensor([0.5] * 6)
