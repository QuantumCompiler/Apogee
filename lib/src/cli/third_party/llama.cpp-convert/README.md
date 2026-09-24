# llama.cpp-convert

llama.cpp's HuggingFace → GGUF converter, vendored **verbatim** from the same
pinned revision the in-process backend builds against (`549b9d84`, see
[`../CMakeLists.txt`](../CMakeLists.txt)):

| Path | Upstream | Why |
|---|---|---|
| `convert_hf_to_gguf.py` | `/convert_hf_to_gguf.py` | the entry point `apogee train promote` runs |
| `conversion/` | `/conversion/` | the converter itself -- one module per model family, refactored upstream out of the old single file |
| `gguf-py/gguf/*.py`, `gguf-py/LICENSE` | `/gguf-py/gguf/`, `/gguf-py/LICENSE` | the `gguf` package the converter imports: tensor names, metadata keys, the writer. Its `scripts/` (the `gguf-dump`-style tools) are left out; the converter never imports them. |
| `models/templates/*.jinja` | `/models/templates/` | the three templates `conversion/base.py` reads by path: two Mistral community chat templates and the RWKV world template. The rest of that directory is never consulted by the converter. |
| `LICENSE` | `/LICENSE` | MIT |

**Nothing here is edited** (the rule this directory's parent README states): a
fix goes upstream, and a pin bump is its own commit that moves *both* pins --
copying `/conversion/`, the entry script and `/gguf-py/gguf/*.py` from the new
revision, then regenerating.

**Why `gguf-py/` is vendored.** The converter and its `gguf` package change
together upstream: a new model's tensors are named in `gguf/constants.py` and
`gguf/tensor_mapping.py` in the same commit that teaches `conversion/` about
them. PyPI's `gguf` is published far less often and does not bump its version
with every such change -- PyPI's 0.19.0 and this pin's 0.19.0 differ, and the
PyPI one cannot name Qwen3.5's MTP layer (`Can not map tensor
'model.layers.64.eh_proj.weight'`). `convert_hf_to_gguf.py`'s own `sys.path`
tweak puts `gguf-py/` beside it ahead of site-packages, which is how upstream
runs it from a checkout, so the seeded copy is the one imported. The `convert`
requirement set still installs PyPI's `gguf`, for the dependencies it brings
(numpy, tqdm, PyYAML, requests); it is shadowed, never imported.

**How it reaches the user.** `scripts/generate_training_assets.py` compiles
every file here into `source/harness/assets_converter.cpp` (chunked raw
string literals, under MSVC's limit), and the one seeding path materialises
them under `~/.apogee/training/scripts/convert/` skip-if-present -- the same
contract the bundled agents, kits and drivers keep. `apogee check` reports
the tree's drift on one `converter` row. It runs under the environment's
interpreter, never the system Python:

    <venv>/bin/python training/scripts/convert/convert_hf_to_gguf.py --outtype f16 --outfile <out.gguf> <fused snapshot>
