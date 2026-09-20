#!/usr/bin/env python3
"""PEFT/CUDA training driver for Apogee.

Runs under the Python environment Apogee owns (`apogee train setup --trainer
peft`), never the system Python, and speaks the one line protocol every
shipped driver speaks -- one JSON object per line on stdout:

  {"iteration": N, "total_iters": N, "loss": F, "lr": F, "throughput": F}
  {"message": "..."}      a note
  {"error": "..."}        fatal; a non-zero exit follows
  {"fused_dir": "..."}    fuse's terminal record
  {"text": "..."}         infer's terminal record

Modes (--mode):
  train  LoRA / QLoRA fine-tune with transformers + peft (+ bitsandbytes)
  fuse   merge the adapter into the base model (merge_and_unload) -> SafeTensors
  infer  generate one answer with the adapter, for eval scoring

Requires the `peft` requirement set:  apogee train setup --trainer peft

The trainer is transformers' own `Trainer` over a tokenised dataset with the
prompt masked EXACTLY (the prompt's token count, from the chat template with
the generation prompt appended) rather than trl's SFTTrainer with the
response-template collator the reference driver used: that collator was
removed from trl, and the template heuristic it needed was a guess.
"""

import json
import os
import platform
import sys

# ── Apple Silicon guard ────────────────────────────────────────────────────────
# bitsandbytes requires NVIDIA CUDA. On macOS ARM it calls abort() (SIGABRT)
# inside its C-extension init when CUDA is absent -- which no try/except
# catches. So the guard runs BEFORE any other import. Apogee selects
# train_mlx.py on darwin/arm64; this script must not run there.
#
# APOGEE_TRAINING_STUB_MODULES is set only by the protocol test, which runs
# this script under stub `transformers`/`peft`/`torch` modules and never
# imports the real bitsandbytes -- it is what lets the emitter be proven on
# the one merge-blocking CI target, which is Apple Silicon.
if (
    sys.platform == "darwin"
    and platform.machine() == "arm64"
    and not os.environ.get("APOGEE_TRAINING_STUB_MODULES")
):
    print(json.dumps({"error": (
        "train_peft.py requires NVIDIA CUDA and is not supported on Apple Silicon. "
        "Run 'apogee train run' with --trainer mlx (auto selects it on macOS arm64)."
    )}), flush=True)
    sys.exit(1)

import argparse
from pathlib import Path

# ── Protective environment variables ──────────────────────────────────────────
# Must be set before any ML library import.
# KMP_DUPLICATE_LIB_OK: suppresses the OpenMP abort when PyTorch's bundled
#   libomp and another copy (numpy/OpenBLAS) are both loaded.
# TOKENIZERS_PARALLELISM: keeps HuggingFace tokenizers from forking workers
#   that deadlock with multiprocessing.
os.environ.setdefault("KMP_DUPLICATE_LIB_OK", "TRUE")
os.environ.setdefault("TOKENIZERS_PARALLELISM", "false")

# Apogee never reaches the HuggingFace Hub from a driver: the base model is a
# local SafeTensors snapshot, and these make anything else fail loudly.
os.environ["HF_HUB_OFFLINE"] = "1"
os.environ["TRANSFORMERS_OFFLINE"] = "1"
os.environ["HF_DATASETS_OFFLINE"] = "1"
os.environ["HF_HUB_DISABLE_TELEMETRY"] = "1"

MAX_SEQUENCE_TOKENS = 2048
LORA_RANK = 16
LORA_ALPHA = 32
LORA_DROPOUT = 0.05
LEARNING_RATE = 2e-4
DEFAULT_ITERS = 1000
DEFAULT_BATCH = 4


def emit(obj):
    print(json.dumps(obj), flush=True)


def fail(message, code=1):
    emit({"error": message})
    sys.exit(code if code else 1)


def check_deps():
    missing = []
    for pkg in ["torch", "transformers", "peft", "bitsandbytes", "accelerate"]:
        try:
            __import__(pkg)
        except ImportError:
            missing.append(pkg)
    if missing:
        fail(
            f"missing Python packages in the training environment: {', '.join(missing)}. "
            "Run: apogee train setup --trainer peft"
        )


def read_records(dataset):
    records = []
    with open(dataset, encoding="utf-8") as f:
        for number, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            try:
                records.append(json.loads(line))
            except json.JSONDecodeError as e:
                fail(f"{dataset}:{number}: not JSON ({e.msg})")
    if not records:
        fail(f"the dataset is empty: {dataset}")
    return records


def split_example(tokenizer, record):
    """The prompt text and the full text of one example.

    Chat lines go through the tokenizer's template -- the prompt is every
    message but the last assistant turn WITH the generation prompt appended,
    so its token count is exactly where the completion starts. Flat lines
    are `prompt` + `completion`.
    """
    if "messages" in record:
        messages = record["messages"]
        if not messages or messages[-1].get("role") != "assistant":
            return None
        prompt = tokenizer.apply_chat_template(
            messages[:-1], add_generation_prompt=True, tokenize=False
        )
        full = tokenizer.apply_chat_template(messages, tokenize=False)
        return prompt, full
    if "prompt" in record and "completion" in record:
        return record["prompt"], record["prompt"] + record["completion"]
    return None


def load_tokenizer(path):
    from transformers import AutoTokenizer

    tokenizer = AutoTokenizer.from_pretrained(path, trust_remote_code=True, local_files_only=True)
    if tokenizer.pad_token is None:
        tokenizer.pad_token = tokenizer.eos_token
    return tokenizer


def load_base(path, quantized):
    import torch
    from transformers import AutoModelForCausalLM, BitsAndBytesConfig

    if quantized:
        return AutoModelForCausalLM.from_pretrained(
            path,
            quantization_config=BitsAndBytesConfig(
                load_in_4bit=True,
                bnb_4bit_quant_type="nf4",
                bnb_4bit_use_double_quant=True,
                bnb_4bit_compute_dtype=torch.bfloat16,
            ),
            device_map="auto",
            trust_remote_code=True,
            local_files_only=True,
        )
    return AutoModelForCausalLM.from_pretrained(
        path,
        device_map="auto",
        torch_dtype=torch.bfloat16,
        trust_remote_code=True,
        local_files_only=True,
    )


def run_train(args):
    check_deps()
    if not args.dataset or not args.output_dir:
        fail("--dataset and --output-dir are required for train mode")

    import torch
    from peft import LoraConfig, TaskType, get_peft_model
    from transformers import Trainer, TrainerCallback, TrainingArguments

    emit({"message": f"loading tokenizer and model from {args.model}"})
    tokenizer = load_tokenizer(args.model)
    quantized = args.method == "qlora"
    emit({"message": "loading model in 4-bit (QLoRA)" if quantized else "loading model (LoRA)"})
    model = load_base(args.model, quantized)

    lora = LoraConfig(
        r=LORA_RANK,
        lora_alpha=LORA_ALPHA,
        lora_dropout=LORA_DROPOUT,
        bias="none",
        task_type=TaskType.CAUSAL_LM,
        target_modules=args.target_modules.split(",") if args.target_modules else "all-linear",
    )
    model = get_peft_model(model, lora)
    if args.grad_checkpoint:
        model.enable_input_require_grads()
        model.gradient_checkpointing_enable()

    emit({"message": f"tokenising {args.dataset}"})
    examples = []
    skipped = 0
    for record in read_records(args.dataset):
        split = split_example(tokenizer, record)
        if split is None:
            skipped += 1
            continue
        prompt_text, full_text = split
        ids = tokenizer(full_text, truncation=True, max_length=MAX_SEQUENCE_TOKENS)["input_ids"]
        labels = list(ids)
        if args.mask_prompt:
            prompt_len = len(tokenizer(prompt_text, truncation=True,
                                       max_length=MAX_SEQUENCE_TOKENS)["input_ids"])
            for i in range(min(prompt_len, len(labels))):
                labels[i] = -100
        examples.append({"input_ids": ids, "labels": labels})
    if not examples:
        fail("no usable examples: each line needs `messages` ending in an assistant turn, "
             "or `prompt` and `completion`")
    if skipped:
        emit({"message": f"{skipped} line(s) skipped: no assistant turn or no prompt/completion"})
    emit({"message": f"{len(examples)} example(s)" +
          (" with the prompt masked out of the loss" if args.mask_prompt else "")})

    class ListDataset(torch.utils.data.Dataset):
        def __init__(self, rows):
            self.rows = rows

        def __len__(self):
            return len(self.rows)

        def __getitem__(self, index):
            return self.rows[index]

    pad_id = tokenizer.pad_token_id

    def collate(batch):
        width = max(len(row["input_ids"]) for row in batch)
        input_ids = [row["input_ids"] + [pad_id] * (width - len(row["input_ids"])) for row in batch]
        labels = [row["labels"] + [-100] * (width - len(row["labels"])) for row in batch]
        attention = [[1] * len(row["input_ids"]) + [0] * (width - len(row["input_ids"]))
                     for row in batch]
        return {
            "input_ids": torch.tensor(input_ids),
            "labels": torch.tensor(labels),
            "attention_mask": torch.tensor(attention),
        }

    steps = args.iters if args.iters else DEFAULT_ITERS
    batch = args.batch_size if args.batch_size else DEFAULT_BATCH
    os.makedirs(args.output_dir, exist_ok=True)
    bf16 = bool(torch.cuda.is_available() and torch.cuda.is_bf16_supported())
    training_args = TrainingArguments(
        output_dir=args.output_dir,
        max_steps=steps,
        per_device_train_batch_size=batch,
        gradient_accumulation_steps=1,
        learning_rate=LEARNING_RATE,
        bf16=bf16,
        fp16=not bf16 and torch.cuda.is_available(),
        logging_steps=1,
        save_strategy="no",
        report_to="none",
    )

    class Progress(TrainerCallback):
        def on_log(self, _args, state, _control, logs=None, **_kwargs):
            if logs and "loss" in logs:
                emit({
                    "iteration": int(state.global_step),
                    "total_iters": steps,
                    "loss": float(logs.get("loss", 0.0)),
                    "lr": float(logs.get("learning_rate", 0.0)),
                    "throughput": float(logs.get("train_steps_per_second", 0.0)),
                })

    emit({"message": f"training: {args.method}, {steps} step(s), batch {batch}"})
    trainer = Trainer(
        model=model,
        args=training_args,
        train_dataset=ListDataset(examples),
        data_collator=collate,
        callbacks=[Progress()],
    )
    trainer.train()

    emit({"message": f"saving adapter to {args.output_dir}"})
    trainer.model.save_pretrained(args.output_dir)
    tokenizer.save_pretrained(args.output_dir)
    emit({"message": "training complete"})


def run_fuse(args):
    check_deps()
    if not args.adapter_path or not args.output_dir:
        fail("--adapter-path and --output-dir are required for fuse mode")
    from peft import PeftModel

    emit({"message": f"loading base model {args.model}"})
    tokenizer = load_tokenizer(args.model)
    base = load_base(args.model, quantized=False)
    emit({"message": f"loading adapter from {args.adapter_path}"})
    model = PeftModel.from_pretrained(base, args.adapter_path, local_files_only=True)
    emit({"message": "merging the adapter into the base model (merge_and_unload)"})
    model = model.merge_and_unload()
    os.makedirs(args.output_dir, exist_ok=True)
    emit({"message": f"saving the fused model to {args.output_dir}"})
    model.save_pretrained(args.output_dir, safe_serialization=True)
    tokenizer.save_pretrained(args.output_dir)
    emit({"fused_dir": args.output_dir})


def run_infer(args):
    check_deps()
    if args.prompt is None:
        fail("--prompt is required for infer mode")
    import torch
    from peft import PeftModel

    tokenizer = load_tokenizer(args.model)
    model = load_base(args.model, quantized=False)
    # An empty adapter path is the untuned base: eval's pairwise baseline.
    if args.adapter_path:
        model = PeftModel.from_pretrained(model, args.adapter_path, local_files_only=True)
    model.eval()

    prompt = args.prompt
    if getattr(tokenizer, "chat_template", None):
        prompt = tokenizer.apply_chat_template(
            [{"role": "user", "content": prompt}], add_generation_prompt=True, tokenize=False
        )
    ids = tokenizer(prompt)["input_ids"]
    input_ids = torch.tensor([ids]).to(model.device)
    with torch.no_grad():
        out = model.generate(
            input_ids=input_ids,
            max_new_tokens=args.max_tokens,
            do_sample=False,
            pad_token_id=tokenizer.pad_token_id,
        )
    text = tokenizer.decode(out[0][len(ids):], skip_special_tokens=True)
    emit({"text": text.strip()})


def main():
    parser = argparse.ArgumentParser(description="PEFT/CUDA training driver for Apogee")
    parser.add_argument("--mode", default="train", choices=["train", "fuse", "infer"])
    parser.add_argument("--model", required=True, help="Local SafeTensors snapshot directory")
    parser.add_argument("--dataset", help="Path to .jsonl training data (train mode)")
    parser.add_argument("--data-dir", help="Accepted for symmetry with train_mlx.py; unused")
    parser.add_argument("--method", default="lora", choices=["lora", "qlora"])
    parser.add_argument("--output-dir", help="Adapter directory (train) or fused model (fuse)")
    parser.add_argument("--adapter-path", default="", help="Adapter directory (fuse/infer modes)")
    parser.add_argument("--iters", type=int, default=None, help=f"Training steps (default {DEFAULT_ITERS})")
    parser.add_argument("--batch-size", type=int, default=None, help=f"Per-device batch (default {DEFAULT_BATCH})")
    parser.add_argument("--num-layers", type=int, default=None,
                        help="Accepted for symmetry; PEFT targets modules, see --target-modules")
    parser.add_argument("--grad-checkpoint", action="store_true")
    parser.add_argument("--mask-prompt", action="store_true",
                        help="Completion-only loss: the prompt tokens are excluded")
    parser.add_argument("--target-modules", default=None,
                        help="Comma-separated LoRA target module names (default: all-linear)")
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
