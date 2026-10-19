"""The stub's `mlx_lm.utils`: the tokenizer alone, as the vision driver asks
for it (27c) -- the same stub tokenizer `load` hands back."""

import os

from mlx_lm._stub import record
from mlx_lm.tokenizer import Tokenizer

# The remap table a real mlx-lm resolves a converter class through (M15):
# a type it knows by another's module.
MODEL_REMAPPING = {
    "gemma4_unified": "gemma4",
    "mistral": "llama",
}


def load_tokenizer(model_path, tokenizer_config_extra=None, eos_token_ids=None):
    record(f"tokenizer {os.path.basename(os.path.normpath(str(model_path)))}")
    return Tokenizer()
