"""`python -m mlx_lm.fuse`: records its argv, creates --save-path with a
marker, prints one line."""

import os
import sys

from . import _record

_record("fuse", " ".join(sys.argv[1:]))
args = sys.argv[1:]
save = args[args.index("--save-path") + 1]
os.makedirs(save, exist_ok=True)
with open(os.path.join(save, "model.safetensors"), "w", encoding="utf-8") as f:
    f.write("stub fused")
print("Fusing... done")
sys.exit(int(os.environ.get("STUB_EXIT", "0")))
