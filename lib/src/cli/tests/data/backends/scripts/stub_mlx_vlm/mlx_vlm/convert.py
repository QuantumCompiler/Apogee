"""A stub `mlx_vlm.convert` for `models convert --mlx` of a set that sees or
hears (53f): no Metal, no weights, no network. It writes what the real one
writes -- the whole model, its towers kept: `config.json` (carrying
`quantization` when quantized), the tokenizer and the processor's files,
SafeTensors shards saved with `format: mlx`, and the index. Its signature is
mlx-vlm's own (0.6.1 on). Steered by the environment, as the stub mlx-lm's is:

  STUB_MLX_CONVERT_RECORD  a file each call appends its keyword arguments to,
                           one JSON object a line, `"converter": "mlx_vlm"`
  STUB_MLX_CONVERT_ERROR   convert() raises ValueError with this message
  STUB_MLX_UNSUPPORTED     comma-separated model_types convert() refuses as the
                           real one refuses a type it has no package for

A draft head (53i) -- a type `speculative/drafters/` holds -- is written as
its few layers alone, its config kept: the drafter mlx-vlm builds.
"""

import json
import os
import shutil

from mlx_lm.convert import _shard

# What mlx-vlm carries over beside the weights: the tokenizer's files and the
# processor's, which a picture is read through.
CARRIED = ("tokenizer.json", "tokenizer_config.json", "generation_config.json",
           "chat_template.jinja", "chat_template.json", "preprocessor_config.json",
           "processor_config.json")


def convert(hf_path, mlx_path="mlx_model", quantize=False, q_group_size=64, q_bits=4,
            q_mode="affine", dtype=None, upload_repo=None, revision=None, dequantize=False,
            trust_remote_code=True, quant_predicate=None):
    with open(os.path.join(hf_path, "config.json")) as src:
        config = json.load(src)
    model_type = config.get("model_type")
    record = os.environ.get("STUB_MLX_CONVERT_RECORD")
    if record:
        with open(record, "a") as out:
            out.write(json.dumps({"converter": "mlx_vlm", "hf_path": hf_path,
                                  "mlx_path": mlx_path, "quantize": quantize, "q_bits": q_bits,
                                  "q_group_size": q_group_size, "q_mode": q_mode,
                                  "dtype": dtype, "trust_remote_code": trust_remote_code,
                                  "model_type": model_type}) + "\n")
    print("[INFO] Loading")  # the real one prints; the driver must keep it off the protocol
    if model_type in os.environ.get("STUB_MLX_UNSUPPORTED", "").split(","):
        raise ValueError(f"Model type {model_type} not supported. "
                         f"Error: No module named 'mlx_vlm.models.{model_type}'")
    error = os.environ.get("STUB_MLX_CONVERT_ERROR")
    if error:
        raise ValueError(error)
    if os.path.exists(mlx_path):
        raise ValueError(f"Cannot save to the path {mlx_path} as it already exists.")
    os.makedirs(mlx_path)

    if quantize:
        scheme = {"group_size": q_group_size or (32 if q_mode == "mxfp4" else 64),
                  "bits": q_bits or 4, "mode": q_mode or "affine"}
        config["quantization"] = scheme
        config["quantization_config"] = scheme
    elif dtype:
        config["torch_dtype"] = dtype

    # A drafter (53i) -- a type its speculative path holds, as the real one's
    # loader falls back to -- is its few layers alone; anything else the
    # whole model, its tower kept.
    drafter = os.path.isfile(os.path.join(os.path.dirname(__file__), "speculative", "drafters",
                                          str(model_type), "__init__.py"))
    tensors = ([("model.layers.0.weight", 32), ("pre_projection.weight", 16)] if drafter else
               [("language_model.model.layers.0.weight", 64),
                ("vision_tower.blocks.0.weight", 32)])
    _shard(os.path.join(mlx_path, "model.safetensors"), tensors)
    with open(os.path.join(mlx_path, "model.safetensors.index.json"), "w") as out:
        json.dump({"metadata": {}, "weight_map": {name: "model.safetensors"
                                                  for name, _ in tensors}}, out)
    with open(os.path.join(mlx_path, "config.json"), "w") as out:
        json.dump(config, out, indent=4)
    for name in CARRIED:
        source = os.path.join(hf_path, name)
        if os.path.exists(source):
            shutil.copy(source, mlx_path)
    with open(os.path.join(mlx_path, "README.md"), "w") as out:
        out.write("---\nlibrary_name: mlx\ntags:\n- mlx-vlm\n---\n")
