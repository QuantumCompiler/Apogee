#!/usr/bin/env python3
"""MLX embedding driver for Apogee's `mlx` backend over an embedding model
(Apple silicon, 53h).

One process per embedding provider. It loads ONE model directory that
`mlx_embeddings.convert` made (`apogee models convert <set> --mlx` records it
as kind `embedding`) through `mlx_embeddings`, holds the model in memory, and
answers batches of texts with their vectors until its stdin closes, then
exits. It never listens on a port, never reads Apogee's config, and never
touches the model store: the backend tells it everything it needs, one batch
at a time.

Runs under the Python environment Apogee owns, never the system Python.
Requires `mlx-embeddings` there:  apogee train setup --with mlx-embeddings

    mlx_embed.py --model <model directory> [--adapter <directory>]

`--adapter` applies a LoRA adapter over the model at load, never fused into
it (55e, as the generation driver applies one): the directory mlx-lm's
training wrote -- adapter_config.json and adapters.safetensors -- handed to
mlx-embeddings' loader as its own `adapter_path`. The backend has checked it
is pinned to these weights before starting the driver.

A batch is tokenized together, padded to its longest text and cut at the
model's own `max_seq_length` (its `sentence_bert_config.json`, else 512), and
run through the model once; each vector is the model's `text_embeds`, pooled
and normalized as its sentence-transformers pipeline does.

The protocol, one JSON object per line.

In, on stdin:
  {"type": "embed", "id": N, "texts": ["...", ...]}
  Closing stdin ends the driver, and it exits 0.

Out, on stdout:
  {"type": "ready", "protocol": 1, "embedding": true, "model_type": "...",
   "mlx_embeddings": "...", "max_length": N, "adapter": "..."}
      Once, after the model loads. `adapter` names the adapter applied over
      it, and is there only when one was (55e): the backend refuses a driver
      that was asked for one and never names it -- one older than adapters.
  {"type": "vectors", "id": N, "vectors": [[F, ...], ...]}
      One vector per text, in the order sent.
  {"type": "error", "id": N|null, "kind": "missing_dependency"|"load"|
   "request"|"protocol", "message": "..."}
      A missing dependency or a failed load is fatal: the driver exits
      non-zero after saying so. A bad request is not: the model stays loaded.

Everything a library prints goes to stderr, which Apogee captures: the
protocol's stdout is a duplicate of the original descriptor, and descriptor 1
itself is pointed at stderr before anything is imported.
"""

import inspect
import json
import math
import os
import signal
import sys

# ── Protective environment, before any ML import ─────────────────────────────
# KMP_DUPLICATE_LIB_OK: two bundled libomp copies in one process abort without it.
# TOKENIZERS_PARALLELISM: forked tokenizer workers deadlock on macOS.
os.environ.setdefault("KMP_DUPLICATE_LIB_OK", "TRUE")
os.environ.setdefault("TOKENIZERS_PARALLELISM", "false")
# The model is a local directory. These make anything else fail loudly rather
# than download quietly.
os.environ["HF_HUB_OFFLINE"] = "1"
os.environ["TRANSFORMERS_OFFLINE"] = "1"
os.environ["HF_HUB_DISABLE_TELEMETRY"] = "1"

PROTOCOL = 1

# The longest a text is read when the model states no length of its own: what
# mlx-embeddings' own examples cut at.
DEFAULT_MAX_LENGTH = 512

# Ctrl-C at a terminal reaches the whole foreground process group, this child
# with it. The backend ends a batch itself; a driver killed by the same
# keystroke would cost a reload.
signal.signal(signal.SIGINT, signal.SIG_IGN)

# The protocol channel, then every stray write to descriptor 1 sent to stderr:
# a library's print() must never land between two protocol lines.
_OUT = os.fdopen(os.dup(1), "w", encoding="utf-8", newline="\n")
os.dup2(2, 1)
sys.stdout = sys.stderr


def emit(obj):
    _OUT.write(json.dumps(obj) + "\n")
    _OUT.flush()


def error(kind, message, rid=None):
    emit({"type": "error", "id": rid, "kind": kind, "message": message})


def read_json(path):
    try:
        with open(path, encoding="utf-8") as f:
            value = json.load(f)
    except (OSError, ValueError):
        return None
    return value if isinstance(value, dict) else None


def max_length_of(model_dir):
    """The longest text the model reads, as its sentence-transformers
    configuration states it; the default when it states none."""
    config = read_json(os.path.join(model_dir, "sentence_bert_config.json")) or {}
    length = config.get("max_seq_length")
    if isinstance(length, int) and length > 0:
        return length
    return DEFAULT_MAX_LENGTH


def rows_of(embeds):
    """`text_embeds` as plain lists of floats: an mlx array's own `tolist`,
    or the rows as they are."""
    rows = embeds.tolist() if hasattr(embeds, "tolist") else embeds
    return [[float(value) for value in row] for row in rows]


def texts_of(request):
    texts = request.get("texts")
    if not isinstance(texts, list) or not all(isinstance(text, str) for text in texts):
        raise ValueError("an embed request carries its texts as a list of strings")
    return texts


class Embedder:
    def __init__(self, mx, model, tokenizer, max_length):
        self.mx = mx
        self.model = model
        self.tokenizer = tokenizer
        self.max_length = max_length

    def vectors(self, texts):
        """One vector per text: the batch tokenized together, padded to its
        longest, run once, and the model's pooled, normalized embeddings."""
        if not texts:
            return []
        encoded = self.tokenizer(texts, padding=True, truncation=True,
                                 max_length=self.max_length)
        input_ids = self.mx.array(encoded["input_ids"])
        mask = encoded.get("attention_mask") if hasattr(encoded, "get") else None
        if mask is None:
            outputs = self.model(input_ids)
        else:
            outputs = self.model(input_ids, attention_mask=self.mx.array(mask))
        embeds = getattr(outputs, "text_embeds", None)
        if embeds is None:
            raise ValueError("the model gives no text_embeds -- it is not one mlx-embeddings "
                             "pools into a sentence vector")
        self.mx.eval(embeds)
        rows = rows_of(embeds)
        if len(rows) != len(texts):
            raise ValueError(f"the model gave {len(rows)} vector(s) for {len(texts)} text(s)")
        for row in rows:
            if not all(math.isfinite(value) for value in row):
                raise ValueError("the model gave a vector that is not finite")
        return rows

    def answer(self, request):
        rid = request.get("id")
        try:
            texts = texts_of(request)
            vectors = self.vectors(texts)
        except Exception as e:  # noqa: BLE001 -- the reason is the message
            error("request", f"{type(e).__name__}: {e}", rid)
            return
        emit({"type": "vectors", "id": rid, "vectors": vectors})

    def serve(self):
        for raw in sys.stdin:
            line = raw.strip()
            if not line:
                continue
            try:
                request = json.loads(line)
            except ValueError:
                error("protocol", f"not a JSON line: {line[:200]}")
                continue
            if not isinstance(request, dict):
                error("protocol", f"not a JSON object: {line[:200]}")
                continue
            if request.get("type") != "embed":
                error("protocol", f"unknown request type {request.get('type')!r}",
                      request.get("id"))
                continue
            self.answer(request)
        return 0


def version_of(module):
    version = getattr(module, "__version__", None)
    if version is None:
        try:
            from mlx_embeddings.version import __version__ as version
        except ImportError:
            version = "unknown"
    return str(version)


USAGE = "usage: mlx_embed.py --model <model directory> [--adapter <directory>]"

# What mlx-lm's training writes into an adapter directory, both read to apply
# it: one missing is refused by name here, never left to the loader's raise.
ADAPTER_FILES = ("adapter_config.json", "adapters.safetensors")


def takes_adapters(load):
    """Whether this mlx-embeddings' loader takes an `adapter_path`."""
    try:
        return "adapter_path" in inspect.signature(load).parameters
    except (TypeError, ValueError):
        return False


def with_adapter(adapter):
    """How a load failure names the adapter it was asked to apply."""
    return f" with the adapter {adapter}" if adapter else ""


def main(argv):
    if (len(argv) not in (3, 5) or argv[1] != "--model" or not argv[2]
            or (len(argv) == 5 and (argv[3] != "--adapter" or not argv[4]))):
        error("protocol", USAGE)
        return 2
    model_dir = argv[2]
    adapter = argv[4] if len(argv) == 5 else None

    try:
        import mlx.core as mx
        import mlx_embeddings
        from mlx_embeddings.utils import load
    except ImportError as e:
        error("missing_dependency",
              f"mlx-embeddings is not installed in the Python environment ({e}). "
              "Run: apogee train setup --with mlx-embeddings")
        return 3

    config = read_json(os.path.join(model_dir, "config.json"))
    if config is None:
        error("load", f"{model_dir} is not a model directory: it has no config.json")
        return 4
    if adapter:
        for name in ADAPTER_FILES:
            if not os.path.isfile(os.path.join(adapter, name)):
                error("load", f"{adapter} is not an mlx-lm adapter directory: it has no {name}")
                return 4
        if not takes_adapters(load):
            error("load", f"the installed mlx-embeddings ({version_of(mlx_embeddings)}) takes no "
                  f"adapter_path, so it cannot apply {adapter}. Run: apogee train setup --with "
                  "mlx-embeddings")
            return 4
    try:
        # The loader's own adapter_path: applied over the weights, never fused.
        model, tokenizer = load(model_dir, adapter_path=adapter) if adapter else load(model_dir)
    except Exception as e:  # noqa: BLE001 -- the reason is the message
        error("load", f"could not load {model_dir}{with_adapter(adapter)} through "
              f"mlx-embeddings: {type(e).__name__}: {e}")
        return 4

    max_length = max_length_of(model_dir)
    ready = {"type": "ready", "protocol": PROTOCOL, "embedding": True,
             "model_type": config.get("model_type"), "mlx_embeddings": version_of(mlx_embeddings),
             "max_length": max_length}
    # Said only when applied: the backend refuses a driver that was asked for
    # an adapter and never names one.
    if adapter:
        ready["adapter"] = adapter
    emit(ready)
    return Embedder(mx, model, tokenizer, max_length).serve()


if __name__ == "__main__":
    sys.exit(main(sys.argv))
