"""mlx_lm's json_tools parser, as mlx-lm 0.32 ships it: Qwen's format."""

import json

tool_call_start = "<tool_call>"

tool_call_end = "</tool_call>"


def parse_tool_call(text, tools=None):
    return json.loads(text.strip())
