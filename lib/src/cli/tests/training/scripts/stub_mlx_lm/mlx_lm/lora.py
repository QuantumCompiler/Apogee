"""`python -m mlx_lm.lora`: prints the lines STUB_LORA_OUTPUT holds (a file),
records its argv, and exits with STUB_EXIT (default 0)."""

import os
import sys

from . import _record

_record("lora", " ".join(sys.argv[1:]))
output = os.environ.get("STUB_LORA_OUTPUT")
if output:
    with open(output, encoding="utf-8") as f:
        sys.stdout.write(f.read())
        sys.stdout.flush()
sys.exit(int(os.environ.get("STUB_EXIT", "0")))
