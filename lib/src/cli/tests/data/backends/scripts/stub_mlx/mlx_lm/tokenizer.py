"""The stub tokenizer: one character per token, a chat template that writes
each message on its own line, and whatever markers the environment asks for."""

import json
import os

from mlx_lm._stub import OFFSET, EOS, record
from mlx_lm.tool_parsers import json_tools


class Detokenizer:
    def __init__(self):
        self.reset()

    def reset(self):
        self.last_segment = ""

    def add_token(self, token):
        self.last_segment = chr(token - OFFSET)

    def finalize(self):
        self.last_segment = ""


class Tokenizer:
    bos_token = None

    def __init__(self):
        self.has_chat_template = os.environ.get("STUB_MLX_TEMPLATE", "1") != "0"
        tools = os.environ.get("STUB_MLX_TOOLS") == "json_tools"
        self.has_tool_calling = tools
        self.tool_call_start = json_tools.tool_call_start if tools else None
        self.tool_call_end = json_tools.tool_call_end if tools else None
        self.tool_parser = json_tools.parse_tool_call if tools else None
        think = os.environ.get("STUB_MLX_THINK") == "1"
        self.has_thinking = think
        self.think_start = "<think>" if think else None
        self.think_end = "</think>" if think else None
        self.eos_token_ids = {EOS}

    @property
    def detokenizer(self):
        return Detokenizer()

    def apply_chat_template(self, messages, tools=None, add_generation_prompt=False,
                            tokenize=True, enable_thinking=None):
        record(f"template messages={len(messages)} tools={len(tools or [])} "
               f"thinking={enable_thinking}")
        lines = []
        if tools:
            lines.append("<|tools|>" + ",".join(t["function"]["name"] for t in tools))
        for message in messages:
            if message["role"] == "refused":
                # What a real template does with a turn it cannot render.
                raise ValueError("the template refuses role 'refused'")
            content = message.get("content") or ""
            if isinstance(content, list):
                # A vision model's parts (27c): its image marker where each
                # picture sits, as a real template places them.
                content = "".join("<|image|>" if part.get("type") == "image"
                                  else part.get("text", "") for part in content)
            line = f"<|{message['role']}|>{content}"
            if message.get("tool_calls"):
                line += json.dumps(message["tool_calls"], sort_keys=True)
            if message.get("tool_call_id"):
                line += f"[{message['tool_call_id']}]"
            lines.append(line)
        text = "\n".join(lines) + "\n"
        if add_generation_prompt:
            text += "<|assistant|>"
            if os.environ.get("STUB_MLX_OPEN_THINK") == "1":
                text += "<think>"
        assert not tokenize
        return text

    def encode(self, text, add_special_tokens=True):
        return [ord(c) + OFFSET for c in text]
