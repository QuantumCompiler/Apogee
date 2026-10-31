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

    mlx_convert.py [--with mlx-vlm|mlx-embeddings|mlx-video|mlx-audio] --hf-path <snapshot>
                   --mlx-path <out> [--q-bits N [--q-group-size N] [--q-mode M] | --dtype T]

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

`--with mlx-video` converts Wan's original layout -- the transformer's
`diffusion_pytorch_model*.safetensors` beside its T5 text encoder and VAE
checkpoints (`models_t5_umt5-xxl-enc-bf16.pth`, `Wan2.*_VAE.pth`) -- with
mlx-video's own Wan converter (53k), `mlx_video.models.wan_2.convert`, as its
command line calls it: an MLX video model's directory, its `config.json`,
`t5_encoder.safetensors`, `vae.safetensors` and the transformer's weights.
It quantizes the transformer alone, at 4 or 8 bits in groups of 32, 64 or
128, or writes it at a floating-point type. Requires `mlx-video` and the
`torch` its converter loads the checkpoints with:
apogee train setup --with mlx-video

`--with mlx-audio` converts a voice model as Kokoro publishes it -- its
`config.json`, one PyTorch checkpoint (`*.pth`) and its voice packs
(`voices/*.pt`), the store's `voice` format -- with `mlx_audio`'s own
`convert` (53m), as its command line calls it: an MLX voice model's
directory, its `config.json`, the weights and a `voices/<name>.safetensors`
pack for every voice. mlx-audio's converter reads SafeTensors weights alone,
so the checkpoint and the packs are first read through torch and written as
SafeTensors in a source directory inside `<out>` -- the checkpoint's modules
flattened under their own names (`bert.`, `predictor.` ...), DataParallel's
`module.` prefix dropped, each pack as the `voice` tensor Kokoro on MLX loads
-- with `model_type: kokoro` in its `config.json`, and that source is removed
once the conversion is done (so a stopped one leaves it only where Apogee
removes `<out>` whole). Requires `mlx-audio`, the `torch` that reads the
checkpoint and the `misaki` Kokoro speaks through:
apogee train setup --with mlx-audio

`--q-bits` quantizes (the converter's own scheme: N bits a weight in groups, a
scale and bias each); without it the weights are written at `--dtype`
(`bfloat16`, `float16`, `float32`). `<out>` must not exist: both converters
refuse to write into a path that does, and so does this.

The training drivers' protocol, one JSON object per line on stdout:
  {"message": "..."}                       as it goes
  {"error": "..."}                         fatal; a non-zero exit follows
  {"converted": "<out>", "quantized": B, "mlx_lm": "..."}   at the end
      -- with `--with mlx-vlm`, "mlx_vlm": "..." in place of "mlx_lm"; with
      `--with mlx-embeddings`, "mlx_embeddings": "..."; with `--with
      mlx-video`, "mlx_video": "..."; with `--with mlx-audio`, "mlx_audio":
      "..." and "voices": N

Everything a library prints goes to stderr, which Apogee captures: the
protocol's stdout is a duplicate of the original descriptor, and descriptor 1
itself is pointed at stderr before anything is imported.
"""

import argparse
import inspect
import json
import os
import shutil
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
    # The converters, each by its package's name (mlx-lm's the default).
    converters = ["mlx-lm", "mlx-vlm"]
    converters += ["mlx-embeddings", "mlx-video"]
    converters += ["mlx-audio"]
    parser.add_argument("--with", dest="converter", choices=converters, default="mlx-lm")
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
    if args.converter == "mlx-video":
        return convert_with_video(args)
    if args.converter == "mlx-audio":
        return convert_with_audio(args)

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


def convert_with_video(args):
    """Wan's original layout built into an MLX video model by mlx-video's own
    Wan converter (53k): the function its command line calls, with the
    precision as that command line takes it -- `--quantize --bits N
    --group-size G` for the transformer, or `--dtype` unquantized."""
    if args.q_bits > 0:
        if args.q_mode not in ("", "affine") or args.q_bits not in (4, 8) or \
                args.q_group_size not in (0, 32, 64, 128):
            return fail("mlx-video's converter quantizes 4 or 8 bits in groups of 32, 64 or "
                        f"128, never {args.q_bits} bits ({args.q_mode or 'affine'}, group "
                        f"{args.q_group_size or 'unset'})", 2)
    elif args.dtype not in ("", "bfloat16", "float16", "float32"):
        return fail(f"mlx-video's converter writes bfloat16, float16 or float32, never "
                    f"{args.dtype}", 2)

    emit({"message": "loading mlx-video"})
    try:
        import mlx_video
        from mlx_video.models.wan_2.convert import convert_wan_checkpoint
    except ImportError as exc:
        return fail(
            f"mlx-video is not installed in Apogee's Python environment ({exc}) -- "
            "apogee train setup --with mlx-video",
            3,
        )
    version = video_version(mlx_video)

    options = {"model_version": "auto"}
    if args.q_bits > 0:
        options["quantize"] = True
        options["bits"] = args.q_bits
        options["group_size"] = args.q_group_size or 64
    elif args.dtype:
        options["dtype"] = args.dtype

    emit({"message": "quantizing" if args.q_bits > 0 else "converting"})
    try:
        convert_wan_checkpoint(args.hf_path, args.mlx_path, **options)
    except Exception as exc:  # the converter's own words, whatever they are
        return fail(f"{type(exc).__name__}: {exc}")

    emit({"converted": args.mlx_path, "quantized": args.q_bits > 0, "mlx_video": version})
    return 0


# Where the SafeTensors source mlx-audio's converter reads is written: inside
# the output, so nothing of it outlives a conversion Apogee stops.
AUDIO_SOURCE = ".apogee-source"


def voice_checkpoint(model_dir):
    """The voice model's one PyTorch checkpoint and its voice packs, by name."""
    checkpoints = sorted(name for name in os.listdir(model_dir) if name.endswith(".pth"))
    if len(checkpoints) != 1:
        raise ValueError(f"{model_dir} holds {len(checkpoints)} checkpoints -- a voice model is one")
    voices_dir = os.path.join(model_dir, "voices")
    voices = sorted(name[:-3] for name in os.listdir(voices_dir) if name.endswith(".pt")) \
        if os.path.isdir(voices_dir) else []
    if not voices:
        raise ValueError(f"{model_dir} holds no voices (voices/*.pt)")
    return os.path.join(model_dir, checkpoints[0]), voices


def to_mlx(mx, tensor):
    """One torch tensor as an MLX array: floats read at 32 bits, which every
    precision below converts from; any other type as it is."""
    tensor = tensor.detach().cpu()
    if tensor.is_floating_point():
        tensor = tensor.float()
    return mx.array(tensor.numpy())


def stage_kokoro(torch, mx, model_dir, source):
    """Kokoro's PyTorch release written as the SafeTensors source mlx-audio's
    converter reads: its config naming Kokoro, the checkpoint's modules
    flattened under their own names, each voice pack its `voice` tensor. The
    number of voices."""
    checkpoint, voices = voice_checkpoint(model_dir)
    with open(os.path.join(model_dir, "config.json"), encoding="utf-8") as handle:
        config = json.load(handle)
    if not isinstance(config, dict):
        raise ValueError("config.json is not a JSON object")
    # Kokoro's own config names no type; the converter finds Kokoro by it.
    config.setdefault("model_type", "kokoro")
    os.makedirs(os.path.join(source, "voices"))
    with open(os.path.join(source, "config.json"), "w", encoding="utf-8") as out:
        json.dump(config, out, indent=2)

    # The checkpoint is one state dict a module (bert, bert_encoder,
    # predictor, text_encoder, decoder), trained under DataParallel: its names
    # carry `module.`, which Kokoro's own loader drops too.
    modules = torch.load(checkpoint, map_location="cpu", weights_only=True)
    if not isinstance(modules, dict) or not modules:
        raise ValueError(f"{os.path.basename(checkpoint)} is not Kokoro's checkpoint "
                         "(a state dict a module)")
    weights = {}
    for module, state in modules.items():
        if not isinstance(state, dict):
            raise ValueError(f"{os.path.basename(checkpoint)}: {module} is not a state dict")
        for name, tensor in state.items():
            if name.startswith("module."):
                name = name[len("module."):]
            weights[f"{module}.{name}"] = to_mlx(mx, tensor)
    # `model.safetensors`: the name mlx-audio's converter reads the weights
    # from and never copies beside what it writes.
    mx.save_safetensors(os.path.join(source, "model.safetensors"), weights)
    for voice in voices:
        pack = torch.load(os.path.join(model_dir, "voices", voice + ".pt"), map_location="cpu",
                          weights_only=True)
        mx.save_safetensors(os.path.join(source, "voices", voice + ".safetensors"),
                            {"voice": to_mlx(mx, pack)})
    return len(voices)


def convert_with_audio(args):
    """A voice model as Kokoro publishes it, built into an MLX voice model by
    mlx-audio's own converter (53m): its PyTorch release read through torch
    into the SafeTensors source the converter takes, then the precision as its
    command line hands it over."""
    try:
        voice_checkpoint(args.hf_path)
    except (OSError, ValueError) as exc:
        return fail(f"{args.hf_path} is not a voice model: {exc}", 2)
    if "," in os.path.abspath(args.mlx_path):
        # Kokoro on MLX reads a voice's path as a list of voices at a comma.
        return fail(f"{args.mlx_path} holds a comma, which Kokoro on MLX reads between voices -- "
                    "convert into a path without one", 2)

    emit({"message": "loading mlx-audio"})
    try:
        import torch
        import mlx.core as mx
        import mlx_audio
        from mlx_audio.convert import convert
    except ImportError as exc:
        return fail(
            f"mlx-audio is not installed in Apogee's Python environment ({exc}) -- "
            "apogee train setup --with mlx-audio",
            3,
        )
    version = audio_version(mlx_audio)

    source = os.path.join(args.mlx_path, AUDIO_SOURCE)
    try:
        emit({"message": "reading Kokoro's PyTorch checkpoint and voices"})
        try:
            voices = stage_kokoro(torch, mx, args.hf_path, source)
        except Exception as exc:  # torch's own words, whatever they are
            return fail(f"could not read the voice model: {type(exc).__name__}: {exc}", 4)

        options = {"hf_path": source, "mlx_path": args.mlx_path}
        if args.q_bits > 0:
            options["quantize"] = True
            options["q_bits"] = args.q_bits
            # Unnamed, the group is the mode's own, as its command line leaves it.
            options["q_group_size"] = args.q_group_size if args.q_group_size > 0 else None
            if args.q_mode and args.q_mode != "affine":
                options["q_mode"] = args.q_mode
        elif args.dtype:
            options["dtype"] = args.dtype
        try:
            accepted = set(inspect.signature(convert).parameters)
        except (TypeError, ValueError):
            accepted = set()
        if "model_domain" in accepted:
            # Said, never detected: a voice model is text to speech.
            options["model_domain"] = "tts"

        emit({"message": "quantizing" if args.q_bits > 0 else "converting"})
        try:
            convert(**options)
        except TypeError as exc:
            if "q_mode" in str(exc) or "dtype" in str(exc):
                return fail(f"this mlx-audio ({version}) cannot write that precision: {exc}")
            return fail(f"TypeError: {exc}")
        except Exception as exc:  # the converter's own words, whatever they are
            return fail(f"{type(exc).__name__}: {exc}")
        # Its voices as Kokoro on MLX loads them, wherever the converter left
        # them: copied from the source when it did not.
        if not os.path.isdir(os.path.join(args.mlx_path, "voices")):
            shutil.copytree(os.path.join(source, "voices"), os.path.join(args.mlx_path, "voices"))
    finally:
        shutil.rmtree(source, ignore_errors=True)

    emit({"converted": args.mlx_path, "quantized": args.q_bits > 0, "mlx_audio": version,
          "voices": voices})
    return 0


def audio_version(module):
    version = getattr(module, "__version__", None)
    if version is None:
        try:
            from mlx_audio.version import __version__ as version
        except ImportError:
            version = ""
    return str(version)


def video_version(module):
    version = getattr(module, "__version__", None)
    if version is None:
        try:
            from mlx_video.version import __version__ as version
        except ImportError:
            version = ""
    return str(version)


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
