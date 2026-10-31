"""A stub `mlx_audio.convert` for `models convert --mlx` of a voice model
(53m): no Metal, no weights, no network. Its function is the real one's,
`convert`, with the real signature (0.4.0 to 0.6.1), and it writes what the
real one writes for Kokoro into `mlx_path` -- every `*.json` of the source
and every file under its subdirectories copied (its `voices/` packs), the
weights saved with `format: mlx`, and `config.json` naming `model_type`,
carrying `quantization` when quantized. Like the real one it reads the
source's SafeTensors weights alone, refusing a source with none in its own
words, and prints its progress on stdout, which the driver must keep off the
protocol. Steered by the environment:

  STUB_MLX_CONVERT_RECORD  a file each call appends its arguments to, one
                           JSON object a line, `"converter": "mlx_audio"`,
                           with the source's tensor names and voices
  STUB_MLX_CONVERT_ERROR   convert() raises ValueError with this message
  STUB_MLX_AUDIO_SKIP      `voices` or `model.safetensors`: what it does not
                           write -- a result Apogee must not commit
"""

import glob
import json
import os
import shutil
import struct
import sys


def _save(path, tensors):
    header = {"__metadata__": {"format": "mlx"}}
    offset = 0
    for name, elements in tensors:
        header[name] = {"dtype": "F16", "shape": [elements],
                        "data_offsets": [offset, offset + 2 * elements]}
        offset += 2 * elements
    text = json.dumps(header).encode()
    with open(path, "wb") as out:
        out.write(struct.pack("<Q", len(text)))
        out.write(text)
        out.write(b"\0" * offset)


def _names(path):
    with open(path, "rb") as handle:
        length = struct.unpack("<Q", handle.read(8))[0]
        header = json.loads(handle.read(length))
    return sorted(key for key in header if key != "__metadata__")


def convert(hf_path, mlx_path="mlx_model", quantize=False, q_group_size=None, q_bits=None,
            dtype=None, upload_repo=None, revision=None, dequantize=False, quant_predicate=None,
            q_mode="affine", model_domain=None):
    print(f"[INFO] Loading model from {hf_path}")  # the real one's words, on stdout
    sys.stderr.write("stub mlx-audio: noise on stderr\n")
    with open(os.path.join(hf_path, "config.json")) as src:
        config = json.load(src)
    weights = glob.glob(os.path.join(hf_path, "*.safetensors"))
    voices = sorted(os.path.basename(path)[:-len(".safetensors")]
                    for path in glob.glob(os.path.join(hf_path, "voices", "*.safetensors")))
    record = os.environ.get("STUB_MLX_CONVERT_RECORD")
    if record:
        with open(record, "a") as out:
            out.write(json.dumps({"converter": "mlx_audio", "hf_path": hf_path,
                                  "mlx_path": mlx_path, "quantize": quantize, "q_bits": q_bits,
                                  "q_group_size": q_group_size, "q_mode": q_mode,
                                  "dtype": dtype, "model_domain": model_domain,
                                  "model_type": config.get("model_type"),
                                  "weights": [name for path in sorted(weights)
                                              for name in _names(path)],
                                  "voices": voices}) + "\n")
    error = os.environ.get("STUB_MLX_CONVERT_ERROR")
    if error:
        raise ValueError(error)
    if not weights:
        raise FileNotFoundError(f"No safetensors found in {hf_path}")
    skip = os.environ.get("STUB_MLX_AUDIO_SKIP", "")
    os.makedirs(mlx_path, exist_ok=True)
    print(f"\n[INFO] Model domain: TTS, type: {config.get('model_type')}")
    for source in glob.glob(os.path.join(hf_path, "*.json")):
        shutil.copy(source, mlx_path)
    if skip != "voices":
        for source in glob.glob(os.path.join(hf_path, "**", "*.safetensors"), recursive=True):
            relative = os.path.relpath(source, hf_path)
            if os.path.dirname(relative):
                os.makedirs(os.path.join(mlx_path, os.path.dirname(relative)), exist_ok=True)
                shutil.copy(source, os.path.join(mlx_path, relative))
    if skip != "model.safetensors":
        # Each precision its own weights, as the real one's are.
        width = (q_bits or 4) * 8 + 1 if quantize else {"float16": 66, "float32": 128}.get(
            dtype, 64)
        _save(os.path.join(mlx_path, "model.safetensors"),
              [("bert.embeddings.word_embeddings.weight", width), ("decoder.weight", 64)])
    config["model_type"] = config.get("model_type") or "kokoro"
    if quantize:
        config["quantization"] = {"group_size": q_group_size or (32 if q_mode == "mxfp4" else 64),
                                  "bits": q_bits or 4, "mode": q_mode or "affine"}
    with open(os.path.join(mlx_path, "config.json"), "w") as out:
        json.dump(config, out, indent=2)
    print(f"[INFO] Conversion complete! Model saved to {mlx_path}")
