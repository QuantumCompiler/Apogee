"""A stub of mlx-video's Wan converter (53k), `mlx_video.models.wan_2.convert`
at the commit Apogee pins: no Metal, no weights, no network. Its function is
the real one's, `convert_wan_checkpoint`, with the real signature, and it
writes what the real one writes into `output_dir` -- `config.json` (carrying
`quantization` when quantized), `t5_encoder.safetensors` when the T5
checkpoint is there, `vae.safetensors` when a VAE checkpoint is, and the
transformer's `model.safetensors` -- each a small file with a real SafeTensors
header saved with `format: mlx`. Like the real one it prints its progress on
stdout, which the driver must keep off the protocol, and it does not fail when
a checkpoint is missing: it writes less. Steered by the environment:

  STUB_MLX_CONVERT_RECORD  a file each call appends its arguments to, one
                           JSON object a line, `"converter": "mlx_video"`
  STUB_MLX_CONVERT_ERROR   the converter raises ValueError with this message
  STUB_MLX_VIDEO_SKIP      an output file's name it does not write -- a
                           result Apogee must not commit
"""

import json
import os
import struct


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


def convert_wan_checkpoint(checkpoint_dir, output_dir, dtype="bfloat16", model_version="auto",
                           quantize=False, bits=4, group_size=64):
    import torch  # noqa: F401 -- the real one loads the .pth checkpoints through it

    record = os.environ.get("STUB_MLX_CONVERT_RECORD")
    if record:
        with open(record, "a") as out:
            out.write(json.dumps({"converter": "mlx_video", "checkpoint_dir": checkpoint_dir,
                                  "output_dir": output_dir, "dtype": dtype,
                                  "model_version": model_version, "quantize": quantize,
                                  "bits": bits, "group_size": group_size}) + "\n")
    error = os.environ.get("STUB_MLX_CONVERT_ERROR")
    if error:
        raise ValueError(error)
    skip = os.environ.get("STUB_MLX_VIDEO_SKIP", "")
    os.makedirs(output_dir, exist_ok=True)
    version = "2.2" if os.path.exists(os.path.join(checkpoint_dir, "Wan2.2_VAE.pth")) else "2.1"
    if model_version == "auto":
        print(f"Auto-detected Wan{version} checkpoint")  # the real one's words, on stdout

    print("Converting transformer (single model)...")
    if skip != "model.safetensors":
        # Each precision its own weights, as the real one's are.
        width = bits * 8 + 1 if quantize else {"float16": 66, "float32": 128}.get(dtype, 64)
        _save(os.path.join(output_dir, "model.safetensors"),
              [("blocks.0.self_attn.q.weight", width), ("blocks.0.ffn.0.weight", 64)])
    with open(os.path.join(checkpoint_dir, "config.json")) as src:
        source = json.load(src)
    config = {key: source[key] for key in ("model_type", "dim", "ffn_dim", "num_heads",
                                           "num_layers", "in_dim", "out_dim", "text_len")
              if key in source}
    if quantize:
        config["quantization"] = {"group_size": group_size, "bits": bits}
    with open(os.path.join(output_dir, "config.json"), "w") as out:
        json.dump(config, out, indent=2)
    print(f"  Saved config to {os.path.join(output_dir, 'config.json')}")
    if os.path.exists(os.path.join(checkpoint_dir, "models_t5_umt5-xxl-enc-bf16.pth")) and \
            skip != "t5_encoder.safetensors":
        print("Converting T5 encoder...")
        _save(os.path.join(output_dir, "t5_encoder.safetensors"), [("encoder.block.0.weight", 32)])
    for vae in ("Wan2.1_VAE.pth", "Wan2.2_VAE.pth"):
        if os.path.exists(os.path.join(checkpoint_dir, vae)) and skip != "vae.safetensors":
            print(f"Converting VAE ({vae[:6]})...")
            _save(os.path.join(output_dir, "vae.safetensors"), [("decoder.conv_in.weight", 32)])
            break
    print(f"\nConversion complete! Output: {output_dir}")
