"""A stub `mlx_vlm.speculative.drafters` (53i): no Metal, no weights.

`load_drafter(path, kind=None)` loads a drafter as mlx-vlm's does -- a
directory with a `config.json` whose `model_type` is a package here naming
its `Model` -- and hands back the drafter and the kind it resolved, recording
`vlm drafter <directory> kind=<kind asked> type=<model_type>`. Gemma 4's
assistant (`gemma4_assistant`) is the one package here; `gemma4_unified_
assistant`, the 12B's, a test adds to its own copy.

`validate_drafter_compatibility(target, drafter, kind)` checks what mlx-vlm's
does for an MTP drafter: the drafter is one this module loaded, of that kind.
Steered by the environment:

  STUB_MLX_VLM_DRAFT_MISMATCH  it raises ValueError with this message
"""

import json
import os

from mlx_lm._stub import record

KINDS = ("dflash", "mtp", "eagle3")


class Drafter:
    """What load_drafter hands back: the directory it read and its type."""

    def __init__(self, path, model_type, block_size):
        self.path = path
        self.model_type = model_type
        self.block_size = block_size


def load_drafter(path_or_repo, kind=None, **_):
    if kind is not None and kind not in KINDS:
        raise ValueError(f"unknown drafter kind {kind!r}")
    with open(os.path.join(path_or_repo, "config.json"), encoding="utf-8") as f:
        config = json.load(f)
    model_type = config.get("model_type")
    here = os.path.join(os.path.dirname(__file__), str(model_type), "__init__.py")
    if not os.path.isfile(here):
        raise ValueError(f"Model type {model_type} not supported as a drafter.")
    record(f"vlm drafter {os.path.basename(os.path.normpath(path_or_repo))} "
           f"kind={kind} type={model_type}")
    return Drafter(path_or_repo, model_type, int(config.get("block_size", 4))), "mtp"


def validate_drafter_compatibility(target_model, draft_model, draft_kind):
    record(f"vlm drafter check kind={draft_kind}")
    mismatch = os.environ.get("STUB_MLX_VLM_DRAFT_MISMATCH")
    if mismatch:
        raise ValueError(mismatch)
    if not isinstance(draft_model, Drafter) or draft_kind != "mtp":
        raise ValueError("the drafter is not an MTP drafter this mlx-vlm loaded")
