"""The stub's shared state, steered by the environment:

  STUB_MLX_REPLIES     a JSON file holding a list of reply strings, one per
                       generation, in order (the last repeats) -- or
                       {"rules": [[needle, reply], ...], "default": reply}:
                       the first rule whose needle is in the conversation
                       after its last user turn answers, else the default
                       (what a chat's title and recall calls get)
  STUB_MLX_RECORD      a file each call appends a line to (what was fed, the
                       sampler, the template's arguments)
  STUB_MLX_TOOLS       "json_tools": the tokenizer declares Qwen's call
                       markers and mlx_lm's json_tools parser
  STUB_MLX_THINK       "1": the tokenizer declares <think> markers
  STUB_MLX_OPEN_THINK  "1": the rendered prompt ends inside a <think> block
  STUB_MLX_TEMPLATE    "0": the model ships no chat template
  STUB_MLX_NOTRIM      "1": the cache cannot be trimmed
  STUB_MLX_DELAY_MS    a pause before each generated token
  STUB_MLX_LOAD_ERROR  load() raises ValueError with this message
  STUB_MLX_NOISE       printed at load, through print() and straight onto
                       descriptor 1 and stderr: none of it may reach the
                       protocol channel

A token is one character, its id the code point plus OFFSET; EOS is 1.
"""

import json
import os

EOS = 1
OFFSET = 10


def record(line):
    path = os.environ.get("STUB_MLX_RECORD")
    if path:
        with open(path, "a", encoding="utf-8") as f:
            f.write(line + "\n")


class Model:
    def __init__(self):
        self.calls = 0
        path = os.environ.get("STUB_MLX_REPLIES")
        self.replies = ["stub reply"]
        if path:
            with open(path, encoding="utf-8") as f:
                self.replies = json.load(f)

    def __call__(self, batch, cache=None):
        """A forward pass: the tokens enter the cache, nothing is returned
        that the driver reads."""
        tokens = list(batch[0])
        record(f"prefill {len(tokens)} cached={cache[0].offset}")
        cache[0].offset += len(tokens)
        cache[0].tokens.extend(tokens)

    def next_reply(self, conversation):
        if isinstance(self.replies, dict):
            tail = conversation.rsplit("<|user|>", 1)[-1]
            for needle, reply in self.replies.get("rules", []):
                if needle in tail:
                    return reply
            return self.replies.get("default", "stub reply")
        reply = self.replies[min(self.calls, len(self.replies) - 1)]
        self.calls += 1
        return reply
