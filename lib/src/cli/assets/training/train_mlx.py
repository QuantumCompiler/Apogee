#!/usr/bin/env python3
"""MLX LoRA training driver for Apogee (Apple Silicon).

Runs under the Python environment Apogee owns (`apogee train setup --trainer
mlx`), never the system Python, and speaks the one line protocol every
shipped driver speaks -- one JSON object per line on stdout:

  {"iteration": N, "total_iters": N, "loss": F, "lr": F, "throughput": F}
  {"message": "..."}      a note
  {"error": "..."}        fatal; a non-zero exit follows
  {"fused_dir": "..."}    fuse's terminal record
  {"text": "..."}         infer's terminal record

Modes (--mode):
  train  LoRA fine-tune through `mlx_lm.lora` (the default)
  fuse   merge the adapter into the base model (`mlx_lm.fuse`) -> SafeTensors
  infer  generate one answer with the adapter, for eval scoring

Requires the `mlx` requirement set:  apogee train setup --trainer mlx

Two departures from the reference driver, both found by reading mlx_lm:
`mlx_lm.lora --data` wants a DIRECTORY holding train.jsonl and valid.jsonl,
not a file (a file path fails with "training set not found"), so train mode
lays that directory out from the dataset; and `--mask-prompt` is forwarded,
where the reference script rejected the flag its own orchestrator passed.
"""

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

# ── Protective environment variables ──────────────────────────────────────────
# Set before any ML import, and inherited by the mlx_lm subprocesses.
# KMP_DUPLICATE_LIB_OK: suppresses the OpenMP abort when two bundled libomp
#   copies (numpy/OpenBLAS and mlx's dependencies) load into one process.
# TOKENIZERS_PARALLELISM: keeps HuggingFace tokenizers from forking workers
#   that deadlock with multiprocessing on macOS.
os.environ.setdefault("KMP_DUPLICATE_LIB_OK", "TRUE")
os.environ.setdefault("TOKENIZERS_PARALLELISM", "false")

# Apogee never reaches the HuggingFace Hub from a driver: the base model is a
# local SafeTensors snapshot, and these make loading anything else fail
# loudly rather than download quietly.
os.environ["HF_HUB_OFFLINE"] = "1"
os.environ["TRANSFORMERS_OFFLINE"] = "1"
os.environ["HF_DATASETS_OFFLINE"] = "1"
os.environ["HF_HUB_DISABLE_TELEMETRY"] = "1"

# `Iter 10: Train loss 2.123, Learning Rate 1.000e-05, It/sec 2.345, ...`
# Newer mlx_lm prints no `/total`; the total then comes from --iters.
_ITER_RE = re.compile(
    r"Iter\s+(\d+)(?:/(\d+))?[:\s]+.*?[Ll]oss\s+([\d.]+)"
    r".*?[Ll]earning [Rr]ate\s+([\d.eE+\-]+)"
    r".*?It/sec\s+([\d.]+)",
)

MLX_DEFAULT_ITERS = 1000


def emit(obj):
    print(json.dumps(obj), flush=True)


def fail(message, code=1):
    emit({"error": message})
    sys.exit(code if code else 1)


def check_mlx():
    try:
        import mlx_lm  # noqa: F401
    except ImportError:
        fail(
            "mlx-lm is not installed in the training environment. "
            "Run: apogee train setup --trainer mlx"
        )


def lay_out_data_dir(dataset, data_dir):
    """mlx_lm.lora reads {train,valid}.jsonl from a directory.

    Every example trains; the validation set is a copy of the first tenth
    (at least one line), so mlx_lm's periodic validation has something to
    read without holding examples back from a small dataset. The number it
    reports is therefore in-sample -- the eval gate is the real check.
    """
    lines = [line for line in Path(dataset).read_text(encoding="utf-8").splitlines() if line.strip()]
    if not lines:
        fail(f"the dataset is empty: {dataset}")
    data_dir.mkdir(parents=True, exist_ok=True)
    (data_dir / "train.jsonl").write_text("\n".join(lines) + "\n", encoding="utf-8")
    valid = lines[: max(1, len(lines) // 10)]
    (data_dir / "valid.jsonl").write_text("\n".join(valid) + "\n", encoding="utf-8")
    return len(lines), len(valid)


def stream(cmd):
    """Runs cmd, yielding its output lines (stderr folded in: mlx_lm prints
    progress on stdout and warnings on stderr, and both are notes here; the
    driver's OWN stderr is what Apogee captures as the failure tail)."""
    proc = subprocess.Popen(
        cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace"
    )
    for line in proc.stdout:
        line = line.rstrip()
        if line:
            yield line
    proc.wait()
    if proc.returncode != 0:
        fail(f"{cmd[2]} exited with code {proc.returncode}", proc.returncode)


def run_train(args):
    check_mlx()
    if not args.dataset or not args.output_dir:
        fail("--dataset and --output-dir are required for train mode")
    data_dir = Path(args.data_dir) if args.data_dir else Path(args.output_dir).parent / "data"
    total, valid = lay_out_data_dir(args.dataset, data_dir)
    emit({"message": f"{total} training example(s); validation is a copy of the first {valid}"})
    if args.method == "qlora":
        emit({"message": "qlora on MLX: LoRA is applied to the snapshot as stored -- "
                         "a quantized snapshot trains as QLoRA, a full-precision one as LoRA"})

    cmd = [
        sys.executable, "-m", "mlx_lm.lora",
        "--model", args.model,
        "--train",
        "--data", str(data_dir),
        "--adapter-path", args.output_dir,
    ]
    if args.iters is not None:
        cmd += ["--iters", str(args.iters)]
    if args.batch_size is not None:
        cmd += ["--batch-size", str(args.batch_size)]
    if args.num_layers is not None:
        cmd += ["--num-layers", str(args.num_layers)]
    if args.grad_checkpoint:
        cmd.append("--grad-checkpoint")
    if args.mask_prompt:
        cmd.append("--mask-prompt")

    total_iters = args.iters if args.iters else MLX_DEFAULT_ITERS
    for line in stream(cmd):
        m = _ITER_RE.search(line)
        if m:
            emit({
                "iteration": int(m.group(1)),
                "total_iters": int(m.group(2)) if m.group(2) else total_iters,
                "loss": float(m.group(3)),
                "lr": float(m.group(4)),
                "throughput": float(m.group(5)),
            })
        else:
            emit({"message": line})
    emit({"message": f"adapter saved to {args.output_dir}"})


def run_fuse(args):
    check_mlx()
    if not args.adapter_path or not args.output_dir:
        fail("--adapter-path and --output-dir are required for fuse mode")
    emit({"message": f"fusing {args.adapter_path} into {args.model}"})
    cmd = [
        sys.executable, "-m", "mlx_lm.fuse",
        "--model", args.model,
        "--adapter-path", args.adapter_path,
        "--save-path", args.output_dir,
    ]
    for line in stream(cmd):
        emit({"message": line})
    emit({"fused_dir": args.output_dir})


def run_infer(args):
    check_mlx()
    if args.prompt is None:
        fail("--prompt is required for infer mode")
    from mlx_lm import load, generate

    # An empty adapter path is the untuned base: eval's pairwise baseline.
    model, tokenizer = load(args.model, adapter_path=args.adapter_path or None)
    prompt = args.prompt
    if getattr(tokenizer, "chat_template", None):
        prompt = tokenizer.apply_chat_template(
            [{"role": "user", "content": prompt}], add_generation_prompt=True, tokenize=False
        )
    text = generate(model, tokenizer, prompt=prompt, max_tokens=args.max_tokens, verbose=False)
    emit({"text": text.strip()})


def main():
    parser = argparse.ArgumentParser(description="MLX LoRA training driver for Apogee")
    parser.add_argument("--mode", default="train", choices=["train", "fuse", "infer"])
    parser.add_argument("--model", required=True, help="Local SafeTensors snapshot directory")
    parser.add_argument("--dataset", help="Path to .jsonl training data (train mode)")
    parser.add_argument("--data-dir", help="Where {train,valid}.jsonl are laid out (train mode)")
    parser.add_argument("--method", default="lora", choices=["lora", "qlora"])
    parser.add_argument("--output-dir", help="Adapter directory (train) or fused model (fuse)")
    parser.add_argument("--adapter-path", default="", help="Adapter directory (fuse/infer modes)")
    parser.add_argument("--iters", type=int, default=None)
    parser.add_argument("--batch-size", type=int, default=None)
    parser.add_argument("--num-layers", type=int, default=None)
    parser.add_argument("--grad-checkpoint", action="store_true")
    parser.add_argument("--mask-prompt", action="store_true",
                        help="Completion-only loss: the prompt tokens are excluded")
    parser.add_argument("--prompt", help="Prompt text (infer mode)")
    parser.add_argument("--max-tokens", type=int, default=256, help="Max new tokens (infer mode)")
    args = parser.parse_args()

    if args.mode == "train":
        run_train(args)
    elif args.mode == "fuse":
        run_fuse(args)
    else:
        run_infer(args)


if __name__ == "__main__":
    main()
