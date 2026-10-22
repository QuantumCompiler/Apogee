"""A stub `mlx_video` for Wan's original layout converted to MLX (53k): no
Metal, no weights, no network. Laid beside the stub `mlx_lm` only where a test
asks for it, so the same runtime can be had with mlx-video and without.

Its Wan converter sits where the commit Apogee pins holds it,
`mlx_video/models/wan_2/convert.py` -- the module `models convert --mlx`
reads for "installed" and the seeded driver imports -- and writes what the
real one writes (`convert.py`). A stub `torch` lies beside it: the real
converter loads Wan's `.pth` checkpoints through torch, so the set installs
it, and "installed" asks for both.

Since 53l its Wan generator sits beside the converter, where the pinned
commit holds it too (`mlx_video/models/wan_2/generate.py`, with the
`save_video` it imports from `postprocess.py`), and with it the two packages
it reaches for: a stub `tqdm`, whose bar it walks its steps under, and a stub
`transformers`, whose `AutoTokenizer` it asks for its tokenizer -- refusing a
hub name, as the real one does offline.
"""

from mlx_video.version import __version__  # noqa: F401
