"""A stub `mlx_embeddings` for the embedding route (53h): no Metal, no
weights, no network. Laid beside the stub `mlx_lm` only where a test asks for
it, so the same runtime can be had with mlx-embeddings and without.

  STUB_MLX_EMBEDDINGS_LOAD_ERROR  load() raises ValueError with this message
  STUB_MLX_EMBEDDINGS_WIDTH       the vectors' width (default 768, the user's
                                  Gemma-Embedding-300M's)
  STUB_MLX_EMBEDDINGS_FAIL        a text containing it makes the model raise
  STUB_MLX_NOISE                  printed at load, through print(), onto
                                  descriptor 1 and onto stderr (and recorded
                                  as `embeddings noise`): none of it may reach
                                  the protocol channel

`load` hands back a model whose vectors are fixed by each text alone -- its
characters counted into WIDTH slots, normalized -- so the same text is the
same vector, a different one another, and a vector search finds the chunk
that shares its words. Each load is recorded as `embeddings load <dir>` and
each batch as `embeddings batch <n> max_length=<m>` (STUB_MLX_RECORD).

It converts too (`convert.py`), and says which types it builds the way a real
mlx-embeddings does: a module under `models/` defining its `Model` --
`gemma3_text` and `bert`, never `qwen3`, which the real one has and a test
uses as a type this stub lacks.
"""

from mlx_embeddings.utils import load  # noqa: F401 -- where mlx-embeddings exports it
from mlx_embeddings.version import __version__  # noqa: F401
