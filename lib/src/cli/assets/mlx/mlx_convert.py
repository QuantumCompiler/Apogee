#!/usr/bin/env python3
"""MLX conversion driver for `apogee models convert --mlx` (Apple silicon).

Makes ONE full-weight SafeTensors snapshot (a Hugging Face layout --
config.json, the tokenizer files, the shards) into an MLX model directory with
`mlx_lm`'s own `convert`, quantized or at a floating-point type, and exits. It
never downloads, never reads Apogee's config, and never touches the model
store: Apogee names a local snapshot and a staging path, verifies what lands
there, and commits it by the store's own rule.

Runs under the Python environment Apogee owns, never the system Python.
Requires `mlx-lm` there:  apogee train setup --with mlx

    mlx_convert.py [--with mlx-vlm|mlx-embeddings] --hf-path <snapshot> --mlx-path <out>
                   [--q-bits N [--q-group-size N] [--q-mode M] | --dtype T]

`--with mlx-vlm` converts with `mlx_vlm`'s own `convert` instead (53f): a
model that sees or hears -- its config declares a vision, audio or omni
tower -- built whole, its towers kept, where mlx-lm would build its language
model alone. The same precisions, mapped onto mlx-vlm's own flags as its
command line passes them, and remote code never trusted, as its command line
defaults. Requires `mlx-vlm` too:  apogee train setup --with mlx-vlm

`--with mlx-embeddings` converts with `mlx_embeddings`' own `convert` (53h):
an embedding model sentence-transformers publishes (its `modules.json`
naming a Pooling module) built whole -- its encoder, and the dense layers
the pooled vector passes through -- where mlx-lm builds language models
alone. The same precisions, mapped onto its own flags. Requires
`mlx-embeddings`:  apogee train setup --with mlx-embeddings

`--q-bits` quantizes (the converter's own scheme: N bits a weight in groups, a
scale and bias each); without it the weights are written at `--dtype`
(`bfloat16`, `float16`, `float32`). `<out>` must not exist: both converters
refuse to write into a path that does, and so does this.

The training drivers' protocol, one JSON object per line on stdout:
  {"message": "..."}                       as it goes
  {"error": "..."}                         fatal; a non-zero exit follows
  {"converted": "<out>", "quantized": B, "mlx_lm": "..."}   at the end
      -- with `--with mlx-vlm`, "mlx_vlm": "..." in place of "mlx_lm"; with
      `--with mlx-embeddings`, "mlx_embeddings": "..."

Everything a library prints goes to stderr, which Apogee captures: the
protocol's stdout is a duplicate of the original descriptor, and descriptor 1
itself is pointed at stderr before anything is imported.
"""

import argparse
import inspect
import json
import os
import signal
import sys

# ── Protective environment, before any ML import ─────────────────────────────
os.environ.setdefault("KMP_DUPLICATE_LIB_OK", "TRUE")
os.environ.setdefault("TOKENIZERS_PARALLELISM", "false")
# The snapshot is a local directory. These make anything else fail loudly
# rather than download quietly -- the converter's model card included.
os.environ["HF_HUB_OFFLINE"] = "1"
os.environ["TRANSFORMERS_OFFLINE"] = "1"
os.environ["HF_HUB_DISABLE_TELEMETRY"] = "1"

# Ctrl-C at a terminal reaches the whole foreground process group. Apogee
# ends this child itself and removes what it wrote; a KeyboardInterrupt here
# would race it with a traceback.
signal.signal(signal.SIGINT, signal.SIG_IGN)

_OUT = os.fdopen(os.dup(1), "w", encoding="utf-8", newline="\n")
os.dup2(2, 1)
sys.stdout = sys.stderr


def emit(obj):
    _OUT.write(json.dumps(obj) + "\n")
    _OUT.flush()


def fail(message, code=1):
    emit({"error": message})
    return code


def arguments(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--with", dest="converter",
                        choices=["mlx-lm", "mlx-vlm", "mlx-embeddings"], default="mlx-lm")
    parser.add_argument("--hf-path", required=True)
    parser.add_argument("--mlx-path", required=True)
    parser.add_argument("--q-bits", type=int, default=0)
    parser.add_argument("--q-group-size", type=int, default=0)
    parser.add_argument("--q-mode", default="")
    parser.add_argument("--dtype", default="")
    return parser.parse_args(argv)


def main(argv):
    args = arguments(argv)
    if not os.path.isdir(args.hf_path):
        return fail(f"{args.hf_path} is not a local snapshot directory", 2)
    if os.path.exists(args.mlx_path):
        return fail(f"{args.mlx_path} already exists -- a conversion never writes over one", 2)
    if args.converter == "mlx-vlm":
        return convert_with_vlm(args)
    if args.converter == "mlx-embeddings":
        return convert_with_embeddings(args)

    emit({"message": "loading mlx-lm"})
    try:
        import mlx_lm
        from mlx_lm.convert import convert
    except ImportError as exc:
        return fail(
            f"mlx-lm is not installed in Apogee's Python environment ({exc}) -- "
            "apogee train setup --with mlx",
            3,
        )

    options = {"hf_path": args.hf_path, "mlx_path": args.mlx_path}
    if args.q_bits > 0:
        options["quantize"] = True
        options["q_bits"] = args.q_bits
        if args.q_group_size > 0:
            options["q_group_size"] = args.q_group_size
        if args.q_mode and args.q_mode != "affine":
            # Only an older mlx-lm lacks the keyword; affine is its default.
            options["q_mode"] = args.q_mode
    elif args.dtype:
        options["dtype"] = args.dtype

    emit({"message": "quantizing" if args.q_bits > 0 else "converting"})
    try:
        convert(**options)
    except TypeError as exc:
        if "q_mode" in str(exc) or "dtype" in str(exc):
            version = getattr(mlx_lm, "__version__", "unknown")
            return fail(f"this mlx-lm ({version}) cannot write that precision: {exc}")
        return fail(f"TypeError: {exc}")
    except Exception as exc:  # the converter's own words, whatever they are
        return fail(f"{type(exc).__name__}: {exc}")

    emit(
        {
            "converted": args.mlx_path,
            "quantized": args.q_bits > 0,
            "mlx_lm": getattr(mlx_lm, "__version__", ""),
        }
    )
    return 0


def convert_with_vlm(args):
    """The snapshot built whole through mlx-vlm's own `convert`, its towers
    kept (53f): the precision as mlx-vlm's command line hands it over."""
    emit({"message": "loading mlx-vlm"})
    try:
        import mlx_vlm
        from mlx_vlm.convert import convert
    except ImportError as exc:
        return fail(
            f"mlx-vlm is not installed in Apogee's Python environment ({exc}) -- "
            "apogee train setup --with mlx-vlm",
            3,
        )
    version = vlm_version(mlx_vlm)

    options = {"hf_path": args.hf_path, "mlx_path": args.mlx_path}
    if args.q_bits > 0:
        options["quantize"] = True
        options["q_bits"] = args.q_bits
        # Unnamed, the group is the mode's own, as mlx-vlm's command line
        # leaves it: its function's default (64) would not fit mxfp4's 32.
        options["q_group_size"] = args.q_group_size if args.q_group_size > 0 else None
        if args.q_mode and args.q_mode != "affine":
            options["q_mode"] = args.q_mode
    elif args.dtype:
        options["dtype"] = args.dtype
    try:
        accepted = set(inspect.signature(convert).parameters)
    except (TypeError, ValueError):
        accepted = set()
    if "trust_remote_code" in accepted:
        # Its command line's default; its function's is to trust. A snapshot's
        # own code never runs here.
        options["trust_remote_code"] = False

    emit({"message": "quantizing" if args.q_bits > 0 else "converting"})
    try:
        convert(**options)
    except TypeError as exc:
        if "q_mode" in str(exc) or "dtype" in str(exc):
            return fail(f"this mlx-vlm ({version}) cannot write that precision: {exc}")
        return fail(f"TypeError: {exc}")
    except Exception as exc:  # the converter's own words, whatever they are
        return fail(f"{type(exc).__name__}: {exc}")

    emit({"converted": args.mlx_path, "quantized": args.q_bits > 0, "mlx_vlm": version})
    return 0


def convert_with_embeddings(args):
    """An embedding model built whole through mlx-embeddings' own `convert`
    (53h): the precision as its command line hands it over."""
    emit({"message": "loading mlx-embeddings"})
    try:
        import mlx_embeddings
        from mlx_embeddings.convert import convert
    except ImportError as exc:
        return fail(
            f"mlx-embeddings is not installed in Apogee's Python environment ({exc}) -- "
            "apogee train setup --with mlx-embeddings",
            3,
        )
    version = embeddings_version(mlx_embeddings)

    options = {"hf_path": args.hf_path, "mlx_path": args.mlx_path}
    if args.q_bits > 0:
        options["quantize"] = True
        options["q_bits"] = args.q_bits
        # Unnamed, the group is the mode's own: its quantizer takes a missing
        # group as the mode's default (32 for mxfp4), where its function's
        # default 64 would not fit.
        options["q_group_size"] = args.q_group_size if args.q_group_size > 0 else None
        if args.q_mode and args.q_mode != "affine":
            options["q_mode"] = args.q_mode
    elif args.dtype:
        options["dtype"] = args.dtype

    emit({"message": "quantizing" if args.q_bits > 0 else "converting"})
    try:
        convert(**options)
    except TypeError as exc:
        if "q_mode" in str(exc) or "dtype" in str(exc):
            return fail(f"this mlx-embeddings ({version}) cannot write that precision: {exc}")
        return fail(f"TypeError: {exc}")
    except Exception as exc:  # the converter's own words, whatever they are
        return fail(f"{type(exc).__name__}: {exc}")

    emit({"converted": args.mlx_path, "quantized": args.q_bits > 0, "mlx_embeddings": version})
    return 0


def embeddings_version(module):
    version = getattr(module, "__version__", None)
    if version is None:
        try:
            from mlx_embeddings.version import __version__ as version
        except ImportError:
            version = ""
    return str(version)


def vlm_version(module):
    version = getattr(module, "__version__", None)
    if version is None:
        try:
            from mlx_vlm.version import __version__ as version
        except ImportError:
            version = ""
    return str(version)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
