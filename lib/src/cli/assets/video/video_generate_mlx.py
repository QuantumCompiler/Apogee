#!/usr/bin/env python3
"""Video generation driver for `apogee video generate` on MLX (53l).

Runs ONE MLX video model from Apogee's model store -- mlx-video's build of
Wan's original layout (`apogee models convert <set> --mlx`, 53k): its
config.json, t5_encoder.safetensors, vae.safetensors and the transformer's
weights -- through mlx-video's own Wan generator, on a prompt and optionally
a seed image, and writes the frames it generates as numbered PNGs. Like
video_generate.py, the PyTorch engine's driver, it never downloads, never
reads Apogee's config, and never touches the store or the output path:
Apogee names the model, the tokenizer, the settings and a working directory,
assembles the frames into an mp4 itself through its own ffmpeg seam, and
moves the clip into place only when everything succeeded.

Runs under the Python environment Apogee owns, never the system Python.
Requires the mlx-video set there:  apogee train setup --with mlx-video
(mlx-video built from GitHub at commit 87db56a51758fefb748a359b90a5283bb8ba4837,
the one Apogee reviewed, with the torch its set brings; the versions that
actually ran are reported on the "ready" line and kept in the run's record).

The generator, at that commit, is the module mlx_video.models.wan_2.generate
(the package's console script `mlx_video.wan_2.generate` runs its main(),
which is why the READMEs spell it as a module) and its entry
generate_video(model_dir, prompt, ...). It is called here with keywords, as
its own main() calls it -- never through its command line, so a prompt is
never an argument. Three of its seams are taken over, in this process alone:

  - its tokenizer: it asks the Hugging Face hub for
    AutoTokenizer.from_pretrained("google/umt5-xxl"). Here that one name
    opens the tokenizer the request names -- the google/umt5-xxl/ the Wan set
    it was converted from stores -- from its files alone, and the hub is
    offline, so any other name fails rather than downloads;
  - its progress: the tqdm bar over its denoising steps becomes one "step"
    record a step, as the PyTorch driver reports them;
  - its output: save_video, which would encode an mp4 itself, writes each
    frame as a PNG instead, so the clip is assembled as every clip is.

    video_generate_mlx.py --request <request.json>

The request, written by Apogee as UTF-8 JSON (the PyTorch driver's, its
engine named):

  {"engine": "mlx", "pipeline": "<the model's dir>", "tokenizer": "<dir>",
   "prompt": "...", "negative_prompt": "..."?, "image": "<png>"?, "seed": N,
   "width": W, "height": H, "frames": N (4n+1), "steps": N, "guidance": G?,
   "shift": S?, "frames_dir": "<dir>",
   "loras": [{"path": "<file.safetensors>", "scale": S}, ...]?}

Each LoRA goes to mlx-video's own loader as its `--lora` does: onto every
model the mode loads -- a single model's one, a pair's both halves.

The training drivers' protocol, one JSON object per line on stdout:
  {"message": "..."}                                       as it goes
  {"error": "..."}                                         fatal; a non-zero exit follows
  {"event": "ready", "device", "dtype", "pipeline", "mode", "engine": "mlx", "mlx", "mlx_video"}
  {"event": "step", "step": N, "steps": N}                 once per denoising step
  {"event": "done", "frames": N, "width": W, "height": H, "seed": N}   at the end

Everything a library prints goes to stderr, which Apogee captures: the
protocol's stdout is a duplicate of the original descriptor, and descriptor 1
itself is pointed at stderr before anything is imported.
"""

import argparse
import inspect
import json
import os
import signal
import struct
import sys
import zlib

# ── Protective environment, before any ML import ─────────────────────────────
os.environ.setdefault("TOKENIZERS_PARALLELISM", "false")
# The model and its tokenizer are local directories. These make anything
# else fail loudly rather than download quietly.
os.environ["HF_HUB_OFFLINE"] = "1"
os.environ["TRANSFORMERS_OFFLINE"] = "1"
os.environ["HF_HUB_DISABLE_TELEMETRY"] = "1"
os.environ["HF_DATASETS_OFFLINE"] = "1"

# Ctrl-C at a terminal reaches the whole foreground process group. Apogee
# ends this child itself and removes what it wrote; a KeyboardInterrupt here
# would race it with a traceback.
signal.signal(signal.SIGINT, signal.SIG_IGN)

_OUT = os.fdopen(os.dup(1), "w", encoding="utf-8", newline="\n")
os.dup2(2, 1)
sys.stdout = sys.stderr

# The name mlx-video's generator asks the hub for, and where Wan's original
# layout stores that same tokenizer.
HUB_TOKENIZER = "google/umt5-xxl"
# The label of the generator's one progress bar: its denoising steps.
DENOISING = "Diffusion"


def emit(obj):
    _OUT.write(json.dumps(obj) + "\n")
    _OUT.flush()


def fail(message, code=1):
    emit({"error": message})
    return code


def arguments(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--request", required=True)
    return parser.parse_args(argv)


def read_request(path):
    with open(path, encoding="utf-8") as handle:
        request = json.load(handle)
    for key in ("pipeline", "tokenizer", "prompt", "seed", "width", "height", "frames", "steps",
                "frames_dir"):
        if key not in request:
            raise ValueError(f"the request names no {key}")
    return request


def read_config(model_dir):
    with open(os.path.join(model_dir, "config.json"), encoding="utf-8") as handle:
        config = json.load(handle)
    if not isinstance(config, dict):
        raise ValueError("config.json is not a JSON object")
    return config


def write_png(path, frame):
    """One RGB frame -- anything with a (height, width, 3) shape whose bytes
    are 8-bit, as mlx-video's decoded uint8 frames are -- written as a PNG
    with nothing but the standard library. Its width and height."""
    shape = tuple(int(side) for side in getattr(frame, "shape", ()))
    if len(shape) != 3 or shape[2] != 3:
        raise ValueError(f"a frame of shape {shape} is not RGB")
    height, width = shape[0], shape[1]
    data = frame.tobytes() if hasattr(frame, "tobytes") else memoryview(frame).tobytes()
    stride = width * 3
    if len(data) != height * stride:
        raise ValueError(f"a {width}x{height} frame holds {len(data)} bytes, not 8-bit RGB")
    rows = b"".join(b"\x00" + data[row * stride:(row + 1) * stride] for row in range(height))

    def chunk(kind, body):
        return (struct.pack(">I", len(body)) + kind + body
                + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF))

    with open(path, "wb") as out:
        out.write(b"\x89PNG\r\n\x1a\n")
        out.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        out.write(chunk(b"IDAT", zlib.compress(rows, 1)))
        out.write(chunk(b"IEND", b""))
    return width, height


def version_of(module, distribution):
    """The version of the package that ran: its own word, else its
    distribution record's, else unstated."""
    own = getattr(module, "__version__", None)
    if own:
        return str(own)
    try:
        from importlib import metadata

        return metadata.version(distribution)
    except Exception:  # no record: a checkout, a copy
        return ""


def main(argv):
    args = arguments(argv)
    try:
        request = read_request(args.request)
    except (OSError, ValueError) as exc:
        return fail(f"cannot read the request {args.request}: {exc}", 2)
    model_dir = request["pipeline"]
    try:
        config = read_config(model_dir)
    except (OSError, ValueError) as exc:
        return fail(f"{model_dir} is not an MLX video model (its config.json: {exc})", 2)
    if not os.path.isdir(request["tokenizer"]):
        return fail(f"the tokenizer {request['tokenizer']} is not there", 2)
    frames = int(request["frames"])
    if frames < 1 or (frames - 1) % 4 != 0:
        return fail(f"mlx-video's Wan generator takes 4n+1 frames -- {frames} is not one", 2)
    image = request.get("image") or None
    if image and not os.path.isfile(image):
        return fail(f"the seed image {image} is not there", 2)
    loras = request.get("loras") or []
    for lora in loras:
        if not os.path.isfile(lora.get("path", "")):
            return fail(f"the LoRA {lora.get('path', '')} is not there", 2)
    # The model's mode says whether a seed image is taken (its config's
    # model_type, which the generator reads): TI2V one or none, I2V one.
    model_type = str(config.get("model_type") or "")
    if model_type == "t2v" and image:
        return fail("this MLX video model is text-to-video (model_type t2v): it takes no seed "
                    "image -- generate without --image", 4)
    if model_type == "i2v" and not image:
        return fail("this MLX video model is image-to-video (model_type i2v): it starts from a "
                    "seed image -- give it one with --image", 4)
    frames_dir = request["frames_dir"]
    os.makedirs(frames_dir, exist_ok=True)

    emit({"message": "loading mlx-video"})
    try:
        import transformers
        import mlx.core as mx
        from mlx_video.models.wan_2 import generate as wan
    except ImportError as exc:
        return fail(
            f"mlx-video is not installed in Apogee's Python environment ({exc}) -- "
            "apogee train setup --with mlx-video",
            3,
        )
    entry = getattr(wan, "generate_video", None)
    if not callable(entry):
        return fail("mlx-video's Wan generator has no generate_video -- this is not the commit "
                    "Apogee reviewed; apogee train setup --with mlx-video", 4)
    accepted = inspect.signature(entry).parameters
    for name in ("model_dir", "prompt", "width", "height", "num_frames", "steps", "seed",
                 "output_path"):
        if name not in accepted:
            return fail(f"mlx-video's generate_video takes no {name} -- this is not the commit "
                        "Apogee reviewed; apogee train setup --with mlx-video", 4)
    if loras and "loras" not in accepted:
        return fail("this mlx-video's Wan generator takes no LoRA -- generate without --lora", 4)

    # Its tokenizer: the stored one, by the one name it asks for.
    tokenizer = request["tokenizer"]
    hub = transformers.AutoTokenizer.from_pretrained

    def stored_tokenizer(name, *rest, **options):
        if name == HUB_TOKENIZER:
            name = tokenizer
            options["local_files_only"] = True
        return hub(name, *rest, **options)

    transformers.AutoTokenizer.from_pretrained = staticmethod(stored_tokenizer)

    # Its progress: each denoising step, said once it is done.
    steps = int(request["steps"])
    if hasattr(wan, "tqdm"):
        def stepping(iterable=None, *rest, **options):
            label = options.get("desc", rest[0] if rest else None)
            if label != DENOISING:
                yield from iterable
                return
            total = len(iterable) if hasattr(iterable, "__len__") else steps
            for index, item in enumerate(iterable, start=1):
                yield item
                emit({"event": "step", "step": index, "steps": total})

        wan.tqdm = stepping
    else:
        emit({"message": "this generator reports no steps; the denoising runs silent"})

    # Its output: the frames, as PNGs, where Apogee assembles them.
    written = {"frames": 0, "width": 0, "height": 0}

    def save_frames(video, output_path=None, fps=None, **_):
        for index, frame in enumerate(video, start=1):
            width, height = write_png(os.path.join(frames_dir, f"f{index:05d}.png"), frame)
            if index == 1:
                written["width"], written["height"] = width, height
            written["frames"] = index

    wan.save_video = save_frames

    default = getattr(mx, "default_device", None)
    device = str(default()) if callable(default) else ""
    device = "gpu" if "gpu" in device else ("cpu" if "cpu" in device else device)
    # The weights' width as the converter wrote it: quantized bits, or
    # unstated for a floating-point build, whose type its config does not say.
    quantization = config.get("quantization")
    bits = quantization.get("bits") if isinstance(quantization, dict) else None
    dtype = f"{bits}-bit" if bits else ""
    emit(
        {
            "event": "ready",
            "device": device,
            "dtype": dtype,
            "pipeline": f"Wan {model_type}" if model_type else "Wan",
            "mode": "i2v" if image else "t2v",
            "engine": "mlx",
            "mlx": version_of(mx, "mlx"),
            "mlx_video": version_of(sys.modules.get("mlx_video"), "mlx-video"),
        }
    )

    call = {
        "model_dir": model_dir,
        "prompt": request["prompt"],
        "width": int(request["width"]),
        "height": int(request["height"]),
        "num_frames": frames,
        "steps": steps,
        "seed": int(request["seed"]),
        # Never written: its save_video writes the frames above instead.
        "output_path": os.path.join(os.path.dirname(os.path.abspath(frames_dir)), "unused.mp4"),
    }
    if request.get("negative_prompt"):
        call["negative_prompt"] = request["negative_prompt"]
    if image:
        call["image"] = image
    if request.get("guidance") is not None:
        call["guide_scale"] = float(request["guidance"])
    if request.get("shift") is not None:
        call["shift"] = float(request["shift"])
    if loras:
        call["loras"] = [(lora["path"], float(lora.get("scale", 1.0))) for lora in loras]

    emit({"message": f"denoising {steps} steps on MLX"})
    try:
        entry(**{key: value for key, value in call.items() if key in accepted})
    except Exception as exc:  # the library's own words, whatever they are
        return fail(f"generation failed: {type(exc).__name__}: {exc}", 5)
    if written["frames"] == 0:
        return fail("mlx-video's generator wrote no frames", 5)
    emit(
        {
            "event": "done",
            "frames": written["frames"],
            "width": written["width"],
            "height": written["height"],
            "seed": int(request["seed"]),
        }
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
