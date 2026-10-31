"""A stub `mlx_audio` for a voice model on MLX (53m): no Metal, no weights, no
network. Laid beside the stub `mlx_lm` only where a test asks for it, so the
same runtime can be had with mlx-audio and without.

Its converter sits where the real one does, `mlx_audio/convert.py` -- the
module `models convert --mlx` reads for "installed" and the seeded driver
imports -- and writes what the real one writes for Kokoro (`convert.py`).
Its Kokoro sits where the real one does too, `mlx_audio/tts/models/kokoro/`,
loaded by `mlx_audio.tts.utils.load_model` and speaking through its
`generate` (`tts/models/kokoro/__init__.py`). Beside it lie a stub `torch`,
whose `load` hands the seeded driver Kokoro's checkpoint and voice packs, and
a stub `misaki`, the phonemizer the real Kokoro reads text through: the set
installs both, and "installed" asks for them.
"""

from mlx_audio.version import __version__  # noqa: F401
