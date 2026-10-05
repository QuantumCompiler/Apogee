"""The stub's samplers: recorded, never used."""

from mlx_lm._stub import record


def make_sampler(temp=0.0, top_p=0.0, min_p=0.0, top_k=0, **_):
    record(f"sampler temp={temp} top_p={top_p} min_p={min_p} top_k={top_k}")
    return None


def make_logits_processors(repetition_penalty=None, presence_penalty=None, **_):
    record(f"processors repetition={repetition_penalty} presence={presence_penalty}")
    return []
