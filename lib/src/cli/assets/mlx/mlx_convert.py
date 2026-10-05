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

    mlx_convert.py --hf-path <snapshot> --mlx-path <out>
                   [--q-bits N [--q-group-size N] [--q-mode M] | --dtype T]

`--q-bits` quantizes (mlx-lm's own scheme: N bits a weight in groups, a scale
and bias each); without it the weights are written at `--dtype` (`bfloat16`,
`float16`, `float32`). `<out>` must not exist: mlx-lm refuses to write into a
path that does, and so does this.

The training drivers' protocol, one JSON object per line on stdout:
  {"message": "..."}                       as it goes
  {"error": "..."}                         fatal; a non-zero exit follows
  {"converted": "<out>", "quantized": B, "mlx_lm": "..."}   at the end

Everything a library prints goes to stderr, which Apogee captures: the
protocol's stdout is a duplicate of the original descriptor, and descriptor 1
itself is pointed at stderr before anything is imported.
"""

import argparse
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


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
