# Embeddings on MLX

**What / why.** Since M21, an embedding model (sentence-transformers, by its `modules.json`) converts to GGUF alone, and MLX is refused. The reason was true: mlx-lm builds language models, and the `mlx` backend does not embed. Only `openai`, `google`, `llamacpp` and `mock` implement `EmbeddingCapable` (`source/data/contracts/provider.h:74`). This item removes both halves of that reason. Wall **W4**. Measured, not guessed (2026-10-21, the MLX-for-every-kind spike: mlx-embeddings' README and source read on GitHub, the user's set read in the mirror of their store).

- **mlx-embeddings builds the user's set whole.** Its `models/gemma3_text.py`, read 2026-10-21:
  - it wraps mlx-lm's Gemma 3 text blocks for embeddings;
  - it carries the two bias-free dense layers (`hidden_size` → `hidden_size * 4` → back), which `sanitize` maps from `N_Dense.linear`;
  - it adds the `model.` prefix the user's weight names lack (the names mlx-lm's strict load refused, M21);
  - it builds a bidirectional mask;
  - it pools first, then dense, then normalizes ("matches SentenceTransformers pipeline", its comment).

  Its README's list of architectures does not name Gemma 3 yet, though the module ships. Its `models/` also holds `qwen3`, `qwen3_vl`, `bert`, `modernbert` and `xlm_roberta`.
- **It converts:** `python -m mlx_embeddings.convert --hf-path … --mlx-path … [--quantize --q-bits N --q-mode affine|mxfp4|…] [--dtype …]`.
- **It runs only as a library:** `load(path)` returns the model and its tokenizer, and `model(input_ids).text_embeds` is the pooled, normalized vector. It has no server and no CLI for embedding, so Apogee needs a driver, as it has for generation (`assets/mlx/mlx_generate.py`).
- **The user's set:** `Gemma-Embedding-300M/safetensors/b6845e67fafb`, a `Gemma3TextModel` with a 768-wide output and `Transformer → Pooling → Dense (768→3072) → Dense (3072→768) → Normalize` (its own `modules.json`).

**Core constraint(s).**
- **Capability probes never leak a cast:** an `mlx` entry over an `embedding`-kind directory (53e) implements `EmbeddingCapable`, and the Harness discovers it. The embedding role, retrieval and the vector store ask it like any embedder.
- **One retriever, resolved once** (CLAUDE.md → ⚠): an MLX embedder joins that rule unchanged. A collection's vectors stay bound to the model that made them, and a GGUF and an MLX build of one set are different models to it.
- **Offered only where it works** (M21): `converts_to` gains `mlx` for an embedding model exactly when the installed mlx-embeddings has its type's module. Otherwise GGUF alone, as now.
- **Spend:** a local embedder is unmetered, so it may vectorize a collection on Apogee's initiative, as llama.cpp's does.

**Seam + files.**
- **`source/business/training/python_env.cpp`:** an `mlx-embeddings` requirement set, its Python range and its reviewed date.
- **`assets/mlx/mlx_convert.py`:** the `mlx_embeddings.convert` route. **`assets/mlx/mlx_embed.py`:** a new seeded driver, one process per provider. It holds the model, reads JSONL batches of texts and writes their vectors, with its descriptor 1 on stderr before importing, as `mlx_generate.py` does.
- **`source/data/backends/mlx_local.h/.cpp`, `mlx_protocol.*`:** `EmbeddingCapable` over the driver for an `embedding` directory.
- **`source/data/modelstore/snapshot.cpp`:** M21's decision gains `mlx` for an embedding model the installed mlx-embeddings builds. **`source/business/models/convert.cpp`:** M21's MLX refusal lifted for such a set and kept otherwise, said as now.
- **`source/presentation/cli/config_cmd.cpp`:** `config scan --register` registers an `embedding` directory as an `mlx` entry the embedding role can name (53e's default).
- **Tests:** a stub `mlx_embeddings` (a `convert` and a `load` returning fixed vectors). The route, the driver's protocol, `EmbeddingCapable` discovered, the role resolved to an MLX entry, and M21's refusal kept where the stub lacks the type.

**Reference.** M21 (the embedding model's reader, its GGUF made whole with `--sentence-transformers-dense-modules`). 27a and 27c (the MLX child over pipes). [mlx-embeddings](https://github.com/Blaizzy/mlx-embeddings), its [`gemma3_text.py`](https://github.com/Blaizzy/mlx-embeddings/blob/main/mlx_embeddings/models/gemma3_text.py).

**Decisions made** (dated):
- 2026-10-21 — **MLX embeddings join, never replace:** the GGUF stays the default offered first, since the `llamacpp` backend embeds with it today. MLX is the second offer where mlx-embeddings builds the type.

**Open calls:**
- [default: the driver batches texts as the embedding seam already batches them (`backends/embedding_batch`), its batch size the seam's.]
- [default: the vector width comes from the converted directory's own config (`hidden_size`, or the last Dense's `out_features`) and is said in `models info`. The vector store's model binding, keyed by the backend, holds GGUF and MLX builds apart.]

**Guardrail(s).**
- **The user's set's own files,** with M21's fixture: converted through the stub, recorded `embedding`, registered, and asked by the embedding role for vectors of the stub's width.
- **A collection built by it is bound to it,** and asking it under another embedder demotes it to lexical (the one-retriever rule's tests extended).
- **M21's refusal still holds** for an embedding type the stub mlx-embeddings lacks.

**Acceptance criteria:**
- [ ] `models convert Gemma-Embedding-300M/... --mlx` converts through mlx-embeddings where it builds `gemma3_text`, and `converts_to` is then `["gguf","mlx"]`.
- [ ] An `mlx` backend over it serves as the embedding role: `embed ingest` and a retrieval turn run on it. The real vectors are the user's run.
- [ ] Without mlx-embeddings, everything reads as M21 left it.
- [ ] `lib/scripts/cicd.sh --test` is green.

**Scope note.** v0.2.0, its gate, [53e](../../assistant/MILESTONES.md#milestone-aq--mlx-for-every-kind) (the `embedding` kind), shipped 2026-10-21. Vision embeddings (`qwen3_vl` in mlx-embeddings) and rerankers are out of scope and unasked.
