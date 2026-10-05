"""The stub's `generate_step`: feeds the prompt into the cache, then yields
the next scripted reply a character at a time and EOS, each token entering
the cache before it is handed over -- the real step's pipelining."""

import os
import time

from mlx_lm._stub import EOS, OFFSET, record


def generate_step(prompt, model, max_tokens=256, sampler=None, logits_processors=None,
                  prompt_cache=None, **_):
    cache = prompt_cache[0]
    record(f"step fed={len(prompt)} cached={cache.offset}")
    cache.offset += len(prompt)
    cache.tokens.extend(prompt)
    reply = model.next_reply("".join(chr(t - OFFSET) for t in cache.tokens if t >= OFFSET))
    tokens = [ord(c) + OFFSET for c in reply] + [EOS]
    delay = float(os.environ.get("STUB_MLX_DELAY_MS", "0")) / 1000.0
    for produced, token in enumerate(tokens):
        if produced >= max_tokens:
            return
        if delay:
            time.sleep(delay)
        cache.offset += 1
        cache.tokens.append(token)
        yield token, None
