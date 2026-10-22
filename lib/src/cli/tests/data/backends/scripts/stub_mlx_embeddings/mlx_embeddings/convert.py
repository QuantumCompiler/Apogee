"""A stub `mlx_embeddings.convert` for `models convert --mlx` of an embedding
model (53h): no Metal, no weights, no network. It writes what the real one
writes -- the top level's `*.json` files copied (`modules.json` among them,
never a `N_Dense/` directory's), `config.json` carrying `quantization` when
quantized, the tokenizer, and the weights saved with `format: mlx`. Its
signature is mlx-embeddings' own (0.1.0). Steered by the environment, as the
stub mlx-lm's is:

  STUB_MLX_CONVERT_RECORD  a file each call appends its keyword arguments to,
                           one JSON object a line, `"converter": "mlx_embeddings"`
  STUB_MLX_CONVERT_ERROR   convert() raises ValueError with this message
  STUB_MLX_UNSUPPORTED     comma-separated model_types convert() refuses as the
                           real one refuses a type it has no module for
"""

import glob
import json
import os
import shutil

from mlx_lm.convert import _shard


def convert(hf_path, mlx_path="mlx_model", quantize=False, q_group_size=64, q_bits=4,
            q_mode="affine", dtype="float16", upload_repo=None, revision=None,
            dequantize=False, skip_vision=True):
    with open(os.path.join(hf_path, "config.json")) as src:
        config = json.load(src)
    model_type = config.get("model_type")
    record = os.environ.get("STUB_MLX_CONVERT_RECORD")
    if record:
        with open(record, "a") as out:
            out.write(json.dumps({"converter": "mlx_embeddings", "hf_path": hf_path,
                                  "mlx_path": mlx_path, "quantize": quantize, "q_bits": q_bits,
                                  "q_group_size": q_group_size, "q_mode": q_mode,
                                  "dtype": dtype, "model_type": model_type}) + "\n")
    print("[INFO] Loading")  # the real one prints; the driver must keep it off the protocol
    if model_type in os.environ.get("STUB_MLX_UNSUPPORTED", "").split(","):
        raise ValueError(f"Model type {model_type} not supported.")
    error = os.environ.get("STUB_MLX_CONVERT_ERROR")
    if error:
        raise ValueError(error)
    os.makedirs(mlx_path, exist_ok=True)

    _shard(os.path.join(mlx_path, "model.safetensors"),
           [("model.layers.0.weight", 64), ("dense.0.weight", 32), ("dense.1.weight", 32)])
    for source in glob.glob(os.path.join(hf_path, "*.json")) + glob.glob(
            os.path.join(hf_path, "*.py")):
        shutil.copy(source, mlx_path)
    if quantize:
        scheme = {"group_size": q_group_size or (32 if q_mode == "mxfp4" else 64),
                  "bits": q_bits or 4, "mode": q_mode or "affine"}
        config["quantization"] = scheme
        config["quantization_config"] = scheme
    with open(os.path.join(mlx_path, "config.json"), "w") as out:
        json.dump(config, out, indent=4)
    if not os.path.exists(os.path.join(mlx_path, "tokenizer.json")):
        with open(os.path.join(mlx_path, "tokenizer.json"), "w") as out:
            json.dump({"model": {"type": "BPE"}}, out)
