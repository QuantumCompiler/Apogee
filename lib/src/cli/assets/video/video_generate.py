#!/usr/bin/env python3
"""Video generation driver for `apogee video generate` (49b).

Runs ONE diffusion pipeline from Apogee's model store -- a directory holding
model_index.json and its components -- on a prompt, and optionally on a seed
image, and writes the frames it generates as numbered PNGs. It never
downloads, never reads Apogee's config, and never touches the store or the
output path: Apogee names the pipeline, the settings and a working directory,
assembles the frames into an mp4 itself through its own ffmpeg seam, and moves
the clip into place only when everything succeeded.

Runs under the Python environment Apogee owns, never the system Python.
Requires the video set there:  apogee train setup --with video
(torch>=2.6, diffusers>=0.35, transformers>=4.46, accelerate>=1.0, ftfy>=6.1,
sentencepiece>=0.2, peft>=0.13 -- floors, as every set's are; the versions
that actually ran are reported on the "ready" line and kept in the run's
document).

    video_generate.py --request <request.json>

The request, written by Apogee as UTF-8 JSON so a prompt is never an argument
(nor visible in a process listing, nor bound by a command line's length):

  {"pipeline": "<dir>", "prompt": "...", "negative_prompt": "..."?,
   "image": "<png>"?, "seed": N, "width": W, "height": H, "frames": N,
   "steps": N, "guidance": G?, "variant": "fp16"?, "frames_dir": "<dir>",
   "loras": [{"path": "<file.safetensors>", "scale": S}, ...]?}

Each LoRA (49e) is applied through diffusers' own loader -- the pipeline's
`load_lora_weights`, then `set_adapters` with every strength -- before the
first step; Apogee only names the files, and never reads their weights.

The training drivers' protocol, one JSON object per line on stdout:
  {"message": "..."}                                       as it goes
  {"error": "..."}                                         fatal; a non-zero exit follows
  {"event": "ready", "device", "dtype", "pipeline", "mode", "torch", "diffusers"}
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
import sys

# ── Protective environment, before any ML import ─────────────────────────────
os.environ.setdefault("KMP_DUPLICATE_LIB_OK", "TRUE")
os.environ.setdefault("TOKENIZERS_PARALLELISM", "false")
# MPS lacks a few operators a pipeline may use; torch then runs them on the
# CPU instead of failing the clip.
os.environ.setdefault("PYTORCH_ENABLE_MPS_FALLBACK", "1")
# The pipeline is a local directory. These make anything else fail loudly
# rather than download quietly.
os.environ["HF_HUB_OFFLINE"] = "1"
os.environ["TRANSFORMERS_OFFLINE"] = "1"
os.environ["HF_HUB_DISABLE_TELEMETRY"] = "1"
os.environ["DIFFUSERS_OFFLINE"] = "1"

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
    parser.add_argument("--request", required=True)
    return parser.parse_args(argv)


def read_request(path):
    with open(path, encoding="utf-8") as handle:
        request = json.load(handle)
    for key in ("pipeline", "prompt", "seed", "width", "height", "frames", "steps", "frames_dir"):
        if key not in request:
            raise ValueError(f"the request names no {key}")
    return request


def pick_device(torch):
    """CUDA, then Apple's MPS, then the CPU -- as torch reports them -- and the
    type the weights run at there."""
    if torch.cuda.is_available():
        return "cuda", torch.bfloat16, "bfloat16"
    mps = getattr(torch.backends, "mps", None)
    if mps is not None and mps.is_available():
        return "mps", torch.bfloat16, "bfloat16"
    return "cpu", torch.float32, "float32"


def image_to_video(diffusers, pipe):
    """The pipeline's image-to-video form: itself when it is one, else its
    sibling class over the same components (WanPipeline ->
    WanImageToVideoPipeline), or None when diffusers has none."""
    name = type(pipe).__name__
    if "ImageToVideo" in name or "I2V" in name:
        return pipe
    sibling = getattr(diffusers, name.replace("Pipeline", "ImageToVideoPipeline"), None)
    if sibling is None or not hasattr(sibling, "from_pipe"):
        return None
    return sibling.from_pipe(pipe)


def apply_loras(pipe, loras):
    """Each LoRA loaded through the pipeline's own loader, every strength set
    at once; the failure in words, or None."""
    if not callable(getattr(pipe, "load_lora_weights", None)):
        return (
            f"this pipeline ({type(pipe).__name__}) cannot take a LoRA -- diffusers has no "
            "LoRA loader for its base model; generate without --lora"
        )
    names = []
    for index, lora in enumerate(loras, start=1):
        name = f"lora{index}"
        try:
            pipe.load_lora_weights(
                os.path.dirname(lora["path"]),
                weight_name=os.path.basename(lora["path"]),
                adapter_name=name,
            )
        except Exception as exc:  # the library's own words
            return f"could not apply the LoRA {lora['path']}: {type(exc).__name__}: {exc}"
        names.append(name)
    try:
        pipe.set_adapters(names, adapter_weights=[float(lora.get("scale", 1.0)) for lora in loras])
    except Exception as exc:
        return f"could not set the LoRAs' strengths: {type(exc).__name__}: {exc}"
    emit({"message": f"applied {len(names)} LoRA{'' if len(names) == 1 else 's'}"})
    return None


def main(argv):
    args = arguments(argv)
    try:
        request = read_request(args.request)
    except (OSError, ValueError) as exc:
        return fail(f"cannot read the request {args.request}: {exc}", 2)
    if not os.path.isfile(os.path.join(request["pipeline"], "model_index.json")):
        return fail(f"{request['pipeline']} is not a pipeline (no model_index.json)", 2)
    if request.get("image") and not os.path.isfile(request["image"]):
        return fail(f"the seed image {request['image']} is not there", 2)
    loras = request.get("loras") or []
    for lora in loras:
        if not os.path.isfile(lora.get("path", "")):
            return fail(f"the LoRA {lora.get('path', '')} is not there", 2)
    os.makedirs(request["frames_dir"], exist_ok=True)

    emit({"message": "loading torch and diffusers"})
    try:
        import torch
        import diffusers
    except ImportError as exc:
        return fail(
            f"the video runtime is not installed in Apogee's Python environment ({exc}) -- "
            "apogee train setup --with video",
            3,
        )

    device, dtype, dtype_name = pick_device(torch)
    emit({"message": "loading the pipeline"})
    options = {"torch_dtype": dtype, "local_files_only": True}
    if request.get("variant"):
        options["variant"] = request["variant"]
    try:
        pipe = diffusers.DiffusionPipeline.from_pretrained(request["pipeline"], **options)
        mode = "t2v"
        if request.get("image"):
            i2v = image_to_video(diffusers, pipe)
            if i2v is None:
                return fail(
                    f"this pipeline ({type(pipe).__name__}) has no image-to-video form -- "
                    "generate without a seed image",
                    4,
                )
            pipe, mode = i2v, "i2v"
        pipe = pipe.to(device)
    except Exception as exc:  # the library's own words, whatever they are
        return fail(f"could not load the pipeline: {type(exc).__name__}: {exc}", 4)
    if loras:
        failure = apply_loras(pipe, loras)
        if failure:
            return fail(failure, 4)

    emit(
        {
            "event": "ready",
            "device": device,
            "dtype": dtype_name,
            "pipeline": type(pipe).__name__,
            "mode": mode,
            "torch": getattr(torch, "__version__", ""),
            "diffusers": getattr(diffusers, "__version__", ""),
        }
    )

    steps = int(request["steps"])
    accepted = inspect.signature(pipe.__call__).parameters
    if "num_frames" not in accepted:
        return fail(f"{type(pipe).__name__} does not generate video (it takes no num_frames)", 4)
    call = {
        "prompt": request["prompt"],
        "height": int(request["height"]),
        "width": int(request["width"]),
        "num_frames": int(request["frames"]),
        "num_inference_steps": steps,
        # On the CPU, whatever the device: the same seed is the same noise on
        # every run of this machine and these versions.
        "generator": torch.Generator(device="cpu").manual_seed(int(request["seed"])),
        "output_type": "pil",
    }
    if request.get("negative_prompt"):
        call["negative_prompt"] = request["negative_prompt"]
    if request.get("guidance") is not None:
        call["guidance_scale"] = float(request["guidance"])
    if mode == "i2v":
        call["image"] = diffusers.utils.load_image(request["image"])

    def on_step(_pipe, step, _timestep, kwargs):
        emit({"event": "step", "step": step + 1, "steps": steps})
        return kwargs

    if "callback_on_step_end" in accepted:
        call["callback_on_step_end"] = on_step
    else:
        emit({"message": "this pipeline reports no steps; the denoising runs silent"})

    emit({"message": f"denoising {steps} steps on {device}"})
    try:
        result = pipe(**{key: value for key, value in call.items() if key in accepted})
    except Exception as exc:
        return fail(f"generation failed: {type(exc).__name__}: {exc}", 5)

    frames = result.frames[0]
    width, height = 0, 0
    for index, frame in enumerate(frames, start=1):
        if index == 1:
            width, height = frame.size
        frame.save(os.path.join(request["frames_dir"], f"f{index:05d}.png"), format="PNG")
    if not frames:
        return fail("the pipeline returned no frames", 5)
    emit(
        {
            "event": "done",
            "frames": len(frames),
            "width": width,
            "height": height,
            "seed": int(request["seed"]),
        }
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
