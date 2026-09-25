# llama.cpp-convert

llama.cpp's HuggingFace → GGUF converter, vendored **verbatim** from the same
pinned revision the in-process backend builds against (release `b11151`, see
[`../CMakeLists.txt`](../CMakeLists.txt)):

| Path | Upstream | Why |
|---|---|---|
| `convert_hf_to_gguf.py` | `/convert_hf_to_gguf.py` | the entry point `apogee train promote` runs |
| `conversion/` | `/conversion/` | the converter itself -- one module per model family, refactored upstream out of the old single file |
| `gguf-py/gguf/*.py`, `gguf-py/LICENSE` | `/gguf-py/gguf/`, `/gguf-py/LICENSE` | the `gguf` package the converter imports: tensor names, metadata keys, the writer. Its `scripts/` (the `gguf-dump`-style tools) are left out; the converter never imports them. |
| `models/templates/*.jinja` | `/models/templates/` | the templates `conversion/` reads by path (Mistral community, RWKV world, DeepSeek-V4, Kimi-K3, MiniMax-M1 at this pin) -- found by the vendoring script, which copies every template a module names. The rest of that directory is never consulted by the converter. |
| `LICENSE` | `/LICENSE` | MIT |
| `retired-digests.txt` | -- | Apogee's own: `<name> <sha256>` for every file version an earlier Apogee shipped. Not seeded; compiled in as `bundled_converter_retired()`. |

**Nothing here is edited** (the rule this directory's parent README states): a
fix goes upstream, and a pin bump is its own commit that moves *both* pins.
The procedure: move `GIT_TAG` in `../CMakeLists.txt`, configure (FetchContent
checks the new revision out under `build/<preset>/_deps/llama_cpp-src`), then

    python3 scripts/vendor_llama_convert.py --from build/<preset>/_deps/llama_cpp-src

which records every file vendored now in `retired-digests.txt`, replaces the
tree from the new revision, and regenerates the compiled-in assets. Build and
fix whatever the new llama.cpp API broke; the tests say what else moved.

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
the tree on one `converter` row. It runs under the environment's
interpreter, never the system Python:

    <venv>/bin/python training/scripts/convert/convert_hf_to_gguf.py --outtype f16 --outfile <out.gguf> <fused snapshot>

**After a pin bump.** Skip-if-present alone would leave every existing install
converting with the old revision's converter -- which refuses the models the
bump was for (Gemma 4's `Gemma4UnifiedForConditionalGeneration`, 2026-09-23:
"Model … is not supported" from a binary whose runtime could run it). So
before the skip-if-present pass, seeding brings the tree up to date: a seeded
file byte-identical to a version in `retired-digests.txt` is an earlier
Apogee's copy, replaced with this build's (or removed when this build no
longer ships it); anything else is the user's edit and stays. `apogee check`
counts such files as stale, and `models convert` / `train promote` refuse a
stale tree naming `apogee check --fix`, which the installers and `make
install` run anyway.
