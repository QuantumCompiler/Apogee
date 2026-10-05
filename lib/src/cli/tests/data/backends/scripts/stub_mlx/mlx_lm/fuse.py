"""A stub `python -m mlx_lm.fuse` (27c): what the mlx trainer's fuse runs.
No Metal, no weights: it writes what the real one leaves -- the base's
configuration and tokenizer beside a shard saved with `format: mlx`, the
adapter named in its metadata so two fuses differ -- so a promotion with
`--target mlx` can register it and the driver can load it."""

import argparse
import json
import os
import shutil
import struct

parser = argparse.ArgumentParser()
parser.add_argument("--model", required=True)
parser.add_argument("--adapter-path", required=True)
parser.add_argument("--save-path", required=True)
parser.add_argument("--dequantize", action="store_true")
args = parser.parse_args()

print(f"Loading pretrained model {args.model}")
os.makedirs(args.save_path, exist_ok=True)
for name in ("config.json", "tokenizer.json", "tokenizer_config.json", "chat_template.jinja"):
    source = os.path.join(args.model, name)
    if os.path.exists(source):
        shutil.copy(source, args.save_path)
header = json.dumps({"__metadata__": {"format": "mlx", "adapter": args.adapter_path},
                     "model.embed_tokens.weight": {"dtype": "F16", "shape": [4],
                                                   "data_offsets": [0, 8]}}).encode()
with open(os.path.join(args.save_path, "model.safetensors"), "wb") as out:
    out.write(struct.pack("<Q", len(header)) + header + b"\0" * 8)
print(f"Saved fused model to {args.save_path}")
