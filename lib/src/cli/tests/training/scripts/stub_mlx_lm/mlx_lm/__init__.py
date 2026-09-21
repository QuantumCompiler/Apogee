"""A stub `mlx_lm` for the protocol tests: no Metal, no weights, no network.

`load` hands back a tokenizer with (or without) a chat template, `generate`
echoes; `python -m mlx_lm.lora` / `mlx_lm.fuse` print what the environment
tells them to. train_mlx.py is run against this package on bare python3 to
prove it emits the JSONL protocol the C++ parser is pinned to.
"""

import os


class _Tokenizer:
    chat_template = "{{ messages }}" if os.environ.get("STUB_CHAT_TEMPLATE") else None

    def apply_chat_template(self, messages, add_generation_prompt=False, tokenize=False):
        text = "".join(f"<{m['role']}>{m['content']}" for m in messages)
        return text + ("<assistant>" if add_generation_prompt else "")


def load(model, adapter_path=None):
    _record("load", f"{model} adapter={adapter_path}")
    return object(), _Tokenizer()


def generate(model, tokenizer, prompt, max_tokens=256, verbose=False):
    return f"stub answer to [{prompt}] max={max_tokens}"


def _record(kind, text):
    path = os.environ.get("STUB_RECORD")
    if path:
        with open(path, "a", encoding="utf-8") as f:
            f.write(f"{kind}: {text}\n")
