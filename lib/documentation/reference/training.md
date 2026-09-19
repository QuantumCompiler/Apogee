# Training

The reference for fine-tuning local models with Apogee. This page covers the
floor of the track -- the Python boundary, datasets, and training kits --
which shipped first; the run itself (`apogee train run|eval|promote`) and the
pipelines, regimes and the continuous cycle arrive with the next two items
and extend this page.

Apogee fine-tunes **full-weight SafeTensors snapshots** and promotes the
result to a GGUF a `llamacpp` backend runs. It infers from GGUF only, so a
snapshot is trainable and never runnable, and a promoted GGUF is runnable and
never trainable.

## The Python boundary

Training execution is Python -- `mlx_lm` on Apple Silicon,
`transformers`/`peft`/`trl`/`bitsandbytes` on CUDA, the `datasets` library
for Parquet -- and none of it is assumed installed. Apogee owns a **virtual
environment under its data directory**:

```
~/.apogee/training/venv/        the environment (never the system Python)
~/.apogee/training/scripts/     the shipped Python drivers, seeded by `apogee check --fix`
~/.apogee/training/kits/        the bundled training kits, seeded the same way
~/.apogee/training/datasets/    trainer-ready datasets, one .jsonl per dataset
~/.apogee/training/datasets/raw/  downloaded dataset files, for `datasets prepare`
```

The `training` row is private (`0700`): a dataset mined from your sessions
holds your own words.

### `apogee train setup`

```bash
apogee train setup                        # create the environment, install nothing
apogee train setup --with prepare         # + the `datasets` library, for Parquet
apogee train setup --trainer auto         # + the trainer stack for this host
apogee train setup --trainer mlx --with convert
```

Creates `training/venv/` from `training.python` in the config (or `python3`
on PATH) with `python -m venv`, then installs the requirement sets asked for
with the environment's own pip. Nothing else ever installs a Python package,
and nothing runs `setup` for you: a fresh install downloads nothing unasked.
A command that needs the environment and finds it missing asks on a
terminal, and on a pipe refuses naming this command.

| Set | Packages | Needed by |
|---|---|---|
| `prepare` | `datasets` | Parquet in `datasets prepare` (JSON, JSONL and CSV need nothing) |
| `mlx` | `mlx-lm` | the Apple Silicon trainer (the run item) |
| `peft` | `transformers`, `peft`, `trl`, `bitsandbytes`, `accelerate` | the CUDA trainer (the run item) |
| `convert` | `torch`, `transformers`, `gguf`, `sentencepiece` | the GGUF converter at promote (the run item) |

`--trainer auto` picks `mlx` on macOS/arm64, `peft` where `nvidia-smi` is on
PATH, and says so when neither fits. Versions are floors, not exact pins.

```yaml
training:
  python: /opt/homebrew/bin/python3.12   # the interpreter the venv is seeded FROM
```

`apogee check` reports the environment and its sets, every seeded script
against the shipped copy (an edit is kept and shown; a missing file is
repaired by `--fix`), every installed kit, and `paths.hf_dir` when set.

### The script protocol

Every shipped driver speaks one line protocol on stdout -- one JSON object
per line:

```
{"message": "..."}                                    a note
{"error": "..."}                                      fatal; a non-zero exit follows
{"rows_written": N, "rows_skipped": N, "out": "..."}  prepare's terminal record
```

Apogee frames the stream with the same framer the vendor CLIs use, keeps a
non-JSON line as a message (a stack trace reaches you as text, never
silently dropped), treats an `{"error"}` line as its own event, carries the
exit code, and captures stderr as a tail for the failure report -- never
inherited onto your terminal. Every script sets the environment guards
(`TOKENIZERS_PARALLELISM`, `HF_HUB_OFFLINE`, `TRANSFORMERS_OFFLINE`,
`HF_DATASETS_OFFLINE`, `HF_HUB_DISABLE_TELEMETRY`) **before** any ML import;
skipping them causes silent SIGABRT crashes that mask the real error.

## Datasets

A dataset is a `.jsonl` of chat-format examples, the shape every trainer
reads:

```json
{"messages":[{"role":"user","content":"..."},{"role":"assistant","content":"..."}]}
```

### `datasets prepare`

```bash
apogee datasets prepare ./data.jsonl                          # auto-detect the format
apogee datasets prepare ./data.parquet --format sharegpt      # needs `--with prepare`
apogee datasets prepare ./data.csv --split test --as-eval     # an eval suite
apogee datasets prepare ./data.json --map prompt=q,completion=a
apogee datasets prepare ~/.apogee/training/datasets/raw/org--name   # a pulled dataset
```

Converts a local JSONL, JSON, CSV or Parquet file -- or a directory of them --
to `training/datasets/<stem>.jsonl` (`.eval.jsonl` under `--as-eval`),
through the seeded `prepare_dataset.py` under the environment's
interpreter. JSON, JSONL and CSV go through the standard library; Parquet
needs the `prepare` set.

| Preset | Source columns | Mapping |
|---|---|---|
| `alpaca` | `instruction`, `input` (optional), `output` | instruction[+input] → user, output → assistant |
| `sharegpt` | `conversations` list of `from`/`value` | human → user, gpt → assistant |
| `chatml` | `conversations` or `messages` list of `role`/`content` | role passthrough |
| `oasst` | `role` + `text`, one row per message | consecutive prompter → assistant rows paired |
| `prompt-completion` | `prompt`, `completion` | wrapped in messages |

Without `--format` the preset is detected from the column names; with none
matching, the command fails listing the columns and an example `--map`.
`--flat` emits `{prompt, completion}`; `--as-eval` emits `{prompt,
expected}`; `--out` names another path; `--force` overwrites.

### `datasets create`

```bash
apogee datasets create starter                              # two documented example lines
apogee datasets create mine --from sessions --backend local --since 2026-09-01
apogee datasets create blank --from empty
```

`--from sessions` mines your own persisted chats under `sessions/`: one
example per completed user↔assistant exchange with both sides non-empty,
filtered by `--backend`, `--since` and `--until` (`YYYY-MM-DD`, inclusive).
Thinking and injected retrieval context are never in a session by
construction, so they cannot leak into a dataset; an assistant turn that only
called tools waits for the answer that follows. Mining your own sessions
explicitly needs no consent key; the unattended source of the continuous
cycle (a later item) does.

### `datasets synth` -- teacher-driven distillation

```bash
apogee datasets synth maths --teacher paid --kit reasoning --count 300
apogee datasets synth json  --teacher local --kit structured-output --topic "API responses"
```

A strong **teacher** model fabricates supervised examples for a kit's skill:
batches of the kit's `per_seed` examples with its seeds cycled for
diversity, each reply parsed as a JSON array (fences, prose and the common
field aliases tolerated), prompts de-duplicated, and teacher calls bounded
so a model that keeps returning junk cannot spin -- a teacher that
under-produces yields fewer examples rather than looping.

The teacher is **named explicitly** and is a direct API-billing or local
backend -- a vendor CLI is refused, and naming it is what satisfies the
metered-spend rule. An API teacher runs `--parallel` batches in flight
(default 4); a local or scripted teacher runs one at a time. A batch that
fails is retried with backoff (five times, capped at a minute, on top of the
transport's own `Retry-After` handling) before it is skipped.

| Flag | Default | Description |
|---|---|---|
| `--teacher` | required | the backend that generates |
| `--kit` | required | a kit name (`datasets kits`) or a path |
| `--count` | the kit's `synth.count` | examples to generate |
| `--topic` | | extra focus appended to every batch |
| `--temperature` | the kit's | teacher sampling temperature |
| `--max-tokens` | 4096 | per-call token budget |
| `--parallel` | 4 | batches in flight for an API teacher |
| `--force` | | overwrite an existing dataset |

### `datasets pull`

```bash
apogee datasets pull org/name
apogee datasets pull org/name@main:data/train.parquet
```

Downloads a Hugging Face **dataset**'s data files (Parquet, JSONL, JSON, CSV,
Arrow, text; or the one file named) into `training/datasets/raw/org--name/`
through the models item's copy → verify → commit ladder -- each file checked
against the size and the sha256 Hugging Face publishes for LFS files, the
directory committed by rename so a half download never appears. Then
`datasets prepare` that directory. `HF_TOKEN` in the environment unlocks a
gated repository; it is read at the point of use and never written anywhere.

### `datasets kits`, `list`, `info`, `delete`

`kits` lists the installed kits with their eval counts; `list` and `info`
the datasets with their line counts and shapes; `delete <name> -y` removes
one.

## SafeTensors snapshots

```bash
apogee models pull Qwen/Qwen2.5-0.5B --safetensors
```

Downloads a repository's full-weight snapshot -- `config.json`, the
tokenizer files, every `*.safetensors` shard, any custom code -- into
`paths.hf_dir` when the config sets it, else `models/<owner>--<repo>/`, each
shard verified against its published sha256, the directory committed by
rename. `models list` shows a snapshot as `safetensors` -- trainable with
`apogee train`, not runnable -- and `models delete <owner>--<repo> -y`
removes it whole. Pulling a SafeTensors repository *without* the flag still
refuses, naming both the converter and `--safetensors`.

## Training kits

A kit is one YAML file bundling a teacher **synthesis spec** and an inline
**eval suite** for one skill. Four ship, seeded under `training/kits/`:

| Kit | Skill |
|---|---|
| `instruction-following` | follow explicit constraints (length, format, casing, required words) precisely |
| `structured-output` | emit well-formed, minimal JSON of a requested shape |
| `summarization` | condense a passage into a faithful, concise summary |
| `reasoning` | solve arithmetic and logic problems with a correct final answer |

The reference implementation's `tool-use` and `web-search` kits are
deferred: today a local model is shown no tool definitions and only one
family's native tool tokens are parsed, so a kit teaching a prose tool-call
line would tune a student into lines nobody dispatches. They arrive with the
in-text tool protocol as its own item, re-authored for Apogee's tool names.

```yaml
name: reasoning
description: Solve arithmetic and logic problems with a correct final answer.
skill: reasoning
synth:
  system: |            # the teacher's generation prompt (required)
    You produce supervised fine-tuning examples ...
  seeds:               # topic hints, cycled batch by batch
    - "percentage problems"
  count: 200           # default examples to synthesise
  per_seed: 8          # examples per teacher call
  temperature: 0.7     # teacher sampling temperature (default 0.9)
train:                 # per-stage defaults, read by the pipeline item
  iters: 500
  num_layers: 16
eval:                  # gates the trained model (at least one item)
  - prompt: "What is 15% of 240?"
    expected: "36"     # a substring the answer must contain
```

Drop a `<name>.yaml` of the same shape beside the bundled ones; `apogee
check` validates every installed kit. The format is the reference
implementation's byte for byte, so a kit written for either project runs on
both.

## Over HTTP

The datasets slice is a twin family on the admin plane -- `GET/POST
/v1/admin/datasets`, `GET/DELETE /v1/admin/datasets/{id}`, `GET
/v1/admin/datasets/kits`, and `POST /v1/admin/datasets/synth` as an async
job -- see [http-api.md](http-api.md). Synth is teacher inference, not
training, which is why it is exposed; training control itself (the run
item's `train run|eval|promote|rollback`) is CLI-only, with reads served.
