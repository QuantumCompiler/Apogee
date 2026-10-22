"""The stub's `mlx_embeddings.utils`: `load`, and the remap table a real
mlx-embeddings resolves a model's module through (empty, as 0.1.0's is)."""

import math
import os
import sys

from mlx_lm._stub import record

MODEL_REMAPPING = {}

WIDTH = 768


class Embeds(list):
    """What `text_embeds` is: rows, read back as an mlx array's are."""

    def tolist(self):
        return [list(row) for row in self]


class Output:
    def __init__(self, embeds):
        self.text_embeds = embeds


class Model:
    def __init__(self, width):
        self.width = width

    def vector(self, text):
        values = [0.0] * self.width
        for c in text.lower():
            if c.isalnum():
                values[ord(c) % self.width] += 1.0
        if not any(values):
            values[0] = 1.0
        norm = math.sqrt(sum(v * v for v in values))
        return [v / norm for v in values]

    def __call__(self, input_ids, attention_mask=None):
        rows = [list(row) for row in input_ids]
        masks = [list(row) for row in attention_mask] if attention_mask is not None else None
        texts = []
        for i, row in enumerate(rows):
            kept = [token for j, token in enumerate(row) if masks is None or masks[i][j]]
            texts.append("".join(chr(token) for token in kept))
        failing = os.environ.get("STUB_MLX_EMBEDDINGS_FAIL")
        if failing and any(failing in text for text in texts):
            raise RuntimeError(f"the stub model refused a text containing {failing!r}")
        return Output(Embeds(self.vector(text) for text in texts))


class Tokenizer:
    """One token per character, its id the code point; padding with 0."""

    def __call__(self, texts, padding=False, truncation=False, max_length=None, **_):
        record(f"embeddings batch {len(texts)} max_length={max_length}")
        ids = [[ord(c) for c in text] for text in texts]
        if truncation and max_length:
            ids = [row[:max_length] for row in ids]
        longest = max((len(row) for row in ids), default=0)
        mask = [[1] * len(row) + [0] * (longest - len(row)) for row in ids]
        if padding:
            ids = [row + [0] * (longest - len(row)) for row in ids]
        return {"input_ids": ids, "attention_mask": mask}


def load(path_or_hf_repo, tokenizer_config=None, model_config=None, adapter_path=None,
         lazy=False):
    record(f"embeddings load {os.path.basename(os.path.normpath(path_or_hf_repo))}")
    noise = os.environ.get("STUB_MLX_NOISE")
    if noise:
        record("embeddings noise")  # so a test knows the narration was made
        print(noise)
        os.write(1, (noise + "\n").encode())
        sys.stderr.write(noise + "\n")
    error = os.environ.get("STUB_MLX_EMBEDDINGS_LOAD_ERROR")
    if error:
        raise ValueError(error)
    width = int(os.environ.get("STUB_MLX_EMBEDDINGS_WIDTH") or WIDTH)
    return Model(width), Tokenizer()
