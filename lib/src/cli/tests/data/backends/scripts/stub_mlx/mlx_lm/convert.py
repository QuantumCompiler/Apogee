"""A stub `mlx_lm.convert` for `models convert --mlx` (27b): no Metal, no
weights, no network. It writes what the real one writes -- a directory with
`config.json` (carrying `quantization` when quantized), the tokenizer files,
SafeTensors shards saved with `format: mlx`, and the index -- small enough to
hash in a test. Steered by the environment:

  STUB_MLX_CONVERT_RECORD  a file each call appends its keyword arguments to,
                           one JSON object a line
  STUB_MLX_CONVERT_ERROR   convert() raises ValueError with this message
  STUB_MLX_CONVERT_HANG    "1": write one shard, then wait to be ended (what a
                           Ctrl-C mid-conversion meets)
  STUB_MLX_CONVERT_SHORT   "1": the shard is cut short of what its header
                           describes -- a result Apogee must not commit
  STUB_MLX_CONVERT_NOQUANT "1": asked to quantize, writes no `quantization`
"""

import json
import os
import shutil
import struct
import time


def _shard(path, tensors, short=False):
    header = {"__metadata__": {"format": "mlx"}}
    offset = 0
    for name, elements in tensors:
        header[name] = {"dtype": "F16", "shape": [elements], "data_offsets": [offset, offset + 2 * elements]}
        offset += 2 * elements
    text = json.dumps(header).encode()
    with open(path, "wb") as out:
        out.write(struct.pack("<Q", len(text)))
        out.write(text)
        out.write(b"\0" * (offset // 2 if short else offset))


def convert(hf_path, mlx_path="mlx_model", quantize=False, q_group_size=None, q_bits=None,
            q_mode="affine", dtype=None, **rest):
    record = os.environ.get("STUB_MLX_CONVERT_RECORD")
    if record:
        with open(record, "a") as out:
            out.write(json.dumps({"hf_path": hf_path, "mlx_path": mlx_path, "quantize": quantize,
                                  "q_bits": q_bits, "q_group_size": q_group_size,
                                  "q_mode": q_mode, "dtype": dtype}) + "\n")
    print("[INFO] Loading")  # the real one prints; the driver must keep it off the protocol
    error = os.environ.get("STUB_MLX_CONVERT_ERROR")
    if error:
        raise ValueError(error)
    if os.path.exists(mlx_path):
        raise ValueError(f"Cannot save to the path {mlx_path} as it already exists.")
    os.makedirs(mlx_path)

    with open(os.path.join(hf_path, "config.json")) as src:
        config = json.load(src)
    if quantize and os.environ.get("STUB_MLX_CONVERT_NOQUANT") != "1":
        scheme = {"group_size": q_group_size or 64, "bits": q_bits or 4, "mode": q_mode or "affine"}
        config["quantization"] = scheme
        config["quantization_config"] = scheme
    elif dtype:
        config["torch_dtype"] = dtype

    if os.environ.get("STUB_MLX_CONVERT_HANG") == "1":
        _shard(os.path.join(mlx_path, "model-00001-of-00002.safetensors"), [("w0", 4096)])
        while True:
            time.sleep(0.1)

    _shard(os.path.join(mlx_path, "model.safetensors"), [("model.layers.0.weight", 64)],
           short=os.environ.get("STUB_MLX_CONVERT_SHORT") == "1")
    with open(os.path.join(mlx_path, "model.safetensors.index.json"), "w") as out:
        json.dump({"metadata": {}, "weight_map": {"model.layers.0.weight": "model.safetensors"}}, out)
    with open(os.path.join(mlx_path, "config.json"), "w") as out:
        json.dump(config, out, indent=4)
    for name in ("tokenizer.json", "tokenizer_config.json", "generation_config.json",
                 "chat_template.jinja"):
        source = os.path.join(hf_path, name)
        if os.path.exists(source):
            shutil.copy(source, mlx_path)
    with open(os.path.join(mlx_path, "README.md"), "w") as out:
        out.write("---\nlibrary_name: mlx\n---\n")
