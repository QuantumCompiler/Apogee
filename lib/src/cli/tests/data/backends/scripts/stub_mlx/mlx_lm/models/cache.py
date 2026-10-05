"""The stub's attention cache: an offset per layer, one layer."""

import os

from mlx_lm._stub import record


class Cache:
    def __init__(self):
        self.offset = 0
        self.tokens = []

    @property
    def state(self):
        return self.offset


def make_prompt_cache(model, **_):
    record("cache new")
    return [Cache()]


def can_trim_prompt_cache(cache):
    return os.environ.get("STUB_MLX_NOTRIM") != "1"


def trim_prompt_cache(cache, num_tokens):
    trimmed = min(cache[0].offset, num_tokens)
    cache[0].offset -= trimmed
    del cache[0].tokens[len(cache[0].tokens) - trimmed:]
    record(f"trim {trimmed}")
    return trimmed
