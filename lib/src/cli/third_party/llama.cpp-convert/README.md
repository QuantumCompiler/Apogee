# llama.cpp-convert

llama.cpp's HuggingFace → GGUF converter, vendored **verbatim** from the same
pinned revision the in-process backend builds against (`549b9d84`, see
[`../CMakeLists.txt`](../CMakeLists.txt)):

| Path | Upstream | Why |
|---|---|---|
| `convert_hf_to_gguf.py` | `/convert_hf_to_gguf.py` | the entry point `apogee train promote` runs |
| `conversion/` | `/conversion/` | the converter itself -- one module per model family, refactored upstream out of the old single file |
| `models/templates/*.jinja` | `/models/templates/` | the three templates `conversion/base.py` reads by path: two Mistral community chat templates and the RWKV world template. The rest of that directory is never consulted by the converter. |
| `LICENSE` | `/LICENSE` | MIT |

**Nothing here is edited** (the rule this directory's parent README states): a
fix goes upstream, and a pin bump is its own commit that moves *both* pins.
The `gguf` Python package the converter imports comes from PyPI through the
`convert` requirement set (`apogee train setup --with convert`), so
`gguf-py/` is not vendored; `convert_hf_to_gguf.py`'s `sys.path` tweak finds
no local copy and falls through to the installed one, as upstream intends
(`NO_LOCAL_GGUF`).

**How it reaches the user.** `scripts/generate_training_assets.py` compiles
every file here into `source/harness/assets_converter.cpp` (chunked raw
string literals, under MSVC's limit), and the one seeding path materialises
them under `~/.apogee/training/scripts/convert/` skip-if-present -- the same
contract the bundled agents, kits and drivers keep. `apogee check` reports
the tree's drift on one `converter` row. It runs under the environment's
interpreter, never the system Python:

    <venv>/bin/python training/scripts/convert/convert_hf_to_gguf.py --outtype f16 --outfile <out.gguf> <fused snapshot>
