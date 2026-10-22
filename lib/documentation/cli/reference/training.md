# Training

The reference for fine-tuning local models with Apogee: the Python boundary,
datasets and training kits, the run itself -- `apogee train
run|eval|promote|rollback|versions|status` -- and the orchestration over
runs: multi-stage **pipelines** under a cumulative gate, **regimes** that
distil a teacher across kits in one command, and the unattended,
scheduler-invoked **cycle** with its anchor gate and circuit breaker.

Apogee fine-tunes **full-weight SafeTensors snapshots** and promotes the
result to a GGUF a `llamacpp` backend runs -- or, on Apple silicon with
`--target mlx`, registers the fused SafeTensors themselves as an `mlx`
backend, with no conversion at all (see `train promote`).

## The Python boundary

Training execution is Python -- `mlx_lm` on Apple Silicon,
`transformers`/`peft`/`trl`/`bitsandbytes` on CUDA, the `datasets` library
for Parquet -- and none of it is assumed installed. Apogee owns a **virtual
environment under its data directory**:

```
~/.apogee/training/venv/          the environment (never the system Python)
~/.apogee/training/scripts/       the shipped Python drivers, seeded by `apogee check --fix` (the trainers', the mlx backend's mlx_generate.py, and mlx_convert.py for `models convert --mlx`)
~/.apogee/training/scripts/convert/  llama.cpp's converter, vendored at the pinned revision
~/.apogee/training/kits/          the bundled training kits, seeded the same way
~/.apogee/training/regimes/       the shipped regime specs, seeded the same way; yours beside them
~/.apogee/training/datasets/      trainer-ready datasets, one .jsonl per dataset
~/.apogee/training/datasets/raw/  downloaded dataset files, for `datasets prepare`
~/.apogee/training/runs/<id>/     one directory per run: manifest.json, adapters/
~/.apogee/training/versions/      one ledger per backend (<backend>.json), and adapter versions (<backend>/v<N>.lora.gguf, or the directory <backend>/v<N>.lora)
~/.apogee/training/suites/        your hand-written eval suites
~/.apogee/training/pipelines/<id>/ one manifest per pipeline run (its stage runs live under runs/)
~/.apogee/training/regime/<id>/   a regime's synthesised dataset and materialised suite per kit
~/.apogee/training/cycle/         the cycle's history.json, cycle.lock, queue/ (and consumed/), work/
```

The `training` row is private (`0700`): a dataset mined from your sessions
holds your own words.

### `apogee train setup`

```bash
apogee train setup                        # create the environment, install nothing
apogee train setup --with prepare         # + the `datasets` library, for Parquet
apogee train setup --trainer auto         # + the trainer stack for this host
apogee train setup --trainer mlx --with convert
apogee train setup --python python3.12    # this Python for a new environment, this run
apogee train setup --rebuild              # remake it on a Python every set runs on (asked)
```

Creates `training/venv/` with `python -m venv`, then installs the requirement
sets asked for with the environment's own pip. **Which Python** (since M19,
2026-10-21): the newest interpreter on PATH -- `python3.<minor>`, `python3`,
`python`, each asked its version -- that every set this host can install
runs on, because one environment serves them all. Each set's range is
carried from its packages' published `requires_python` (read 2026-10-21):
`voice` runs on `>=3.10,<3.13` (Kokoro and misaki), `mlx` on `>=3.11`
(mlx-lm), every other set on `>=3.10` -- so an Apple-silicon Mac whose
`python3` is 3.13 or newer gets its environment from a 3.12 or 3.11 beside it,
and setup says which and why. `training.python` in the config (`config set
training.python <path>` writes it), or `--python` for one run, is used as
given; one outside a requested set's range is refused before anything is
installed, naming both. No interpreter is ever downloaded: with none that
fits, setup says what it found and where one comes from (`brew install
python@3.12`).

An environment already there is kept. A set its Python does not run on is
refused before pip, naming `apogee train setup --rebuild --with <set>`;
`--rebuild` chooses again, asks `[y/N]` at a terminal (on a pipe it needs
`--yes`), and reinstalls every set the environment held, in its order -- a
rebuild that fails puts the old environment back as it was. `apogee check`'s
`python env` row names the environment's Python, and a set's row that its
Python cannot take names that rebuild.

Nothing else ever installs a Python package. Every install's last step,
`apogee bootstrap` (51), runs this command for the sets this host can
install -- `APOGEE_MINIMAL_INSTALL=1` skips it -- and otherwise a command
that needs the environment and finds it missing asks on a terminal, and on a
pipe refuses naming this command.

| Set | Packages | Needed by |
|---|---|---|
| `prepare` | `datasets` | Parquet in `datasets prepare` (JSON, JSONL and CSV need nothing) |
| `mlx` | `mlx-lm` | `train run --trainer mlx`, the Apple Silicon trainer; an `mlx` backend, which runs a model directory through it for chat and `complete`; and `models convert --mlx`, which converts a SafeTensors language model into an MLX directory with its converter -- and, with `--text-only`, the language model alone of a set that also sees or hears, its towers left out |
| `peft` | `torch`, `transformers`, `peft`, `bitsandbytes`, `accelerate` | `train run --trainer peft`, the CUDA trainer |
| `convert` | `torch`, `transformers`, `gguf`, `numpy`, `sentencepiece`, `protobuf` | `train promote`'s GGUF conversion (not `--target mlx`, which converts nothing) |
| `mlx-vlm` | `mlx-vlm` | an `mlx` backend over a vision model reading pictures as they are; without it such a model's pictures go to the `vision` role, and `apogee check` says so. Since 53f also `models convert --mlx` of a set whose `config.json` declares a vision, audio or omni tower (`vision_config`, `audio_config`, `thinker_config`): built through mlx-vlm's own converter with its towers kept, and recorded as kind `vision`, so an `mlx` backend over it runs every turn through mlx-vlm, text included. MLX is offered such a set where the mlx-vlm installed builds its type, read from its own `models/`; otherwise `--text-only` where mlx-lm builds it, and `models info` says which. Its floor, `0.6.1` (read 2026-10-21), is the first release that builds Qwen3-Omni, Gemma 4's unified model and Qwen 3.5; an environment holding an older one keeps it until this set is installed again. Since 53g it is also how an `mlx` backend hears: over an omni model mlx-vlm built (recorded `vision`, its `config.json` declaring an `audio_config` or a `thinker_config`), with the installed mlx-vlm holding its type's module, an attached recording is heard as it is on the turn it is attached and read as its transcript after; any other `mlx` backend has its recordings transcribed by the `transcription` role, as before |
| `mlx-embeddings` | `mlx-embeddings` | since 53h, an embedding model on MLX: `models convert --mlx` of a set sentence-transformers publishes (its `modules.json` naming a Pooling module) builds it whole -- its encoder, and the dense layers its pooled vector passes through -- through mlx-embeddings' own converter, and records it as kind `embedding`. MLX is offered such a set, second after its GGUF, exactly where the mlx-embeddings installed builds its `model_type`, read from its own `models/`; without this set, or for a type it has no module for, the set converts to GGUF alone and `--mlx` is refused as before. `config scan --register` registers what it built as an `mlx` entry, which embeds through the seeded `mlx_embed.py` -- one child holding the model, batches of 64 texts, unmetered -- for the `embedding` role (`config set-default-embedding`), and answers no chat; its vectors are as wide as its `config.json` says (`models info`'s `vectors:` line), and a collection it vectorises is bound to that backend's own space (`mlx:<model>`), so the same set's GGUF build is another model to it. Its floor, `0.1.0` (read 2026-10-21), is the first release whose `gemma3_text` pools before its dense layers and converts its padding mask, as the module this route was read against does; it brings mlx-vlm and mlx-lm with it |

`--trainer auto` picks `mlx` on macOS/arm64, `peft` where `nvidia-smi` is on
PATH, and says so when neither fits. Versions are floors, not exact pins.

```jsonc
"training": {
  "python": "/opt/homebrew/bin/python3.12",   // the interpreter the venv is seeded FROM (config set training.python)
  "trainer": "auto",            // mlx on Apple Silicon, peft with nvidia-smi; or name one
  "judge_backend": "paid",      // judges eval items with no `expected`; unset = they skip
  "eval_suite_path": "~/.apogee/training/suites/mine.jsonl",   // `train eval` without --suite
  "retain_versions": 3,         // promoted GGUFs kept per backend; 0 keeps all
  "gate_mode": "hard",          // hard: promote refuses an unevaluated/failing run; soft: warns
  "pipelines": {},              // named pipelines -- see Pipelines
  "regimes": {},                // named regimes -- see Regimes
  "cycle": {}                   // the unattended loop -- see The cycle
}
```

`apogee check` reports the environment and its sets, every seeded script
against the shipped copy (an edit is kept and shown; a missing file is
repaired by `--fix`, and so is an earlier Apogee's unedited copy, which
`--fix` brings up to this build's -- the converter tree's rule), the vendored converter tree as one row, the trainer
this host would use and whether its set is installed, the `convert` set,
every installed kit, every version ledger's consistency (the active
version's GGUF exists and the backend points at it), every named
pipeline's student and datasets, the cycle's configuration (the pipeline
and backend it names must exist; a non-`llamacpp` backend fails), a halted
cycle with `apogee train cycle resume` as the remedy, and `paths.hf_dir`
when set. Its `Adapters` section (53c) has a row for every adapter an entry
applies: its scale one the loader allows, its file there and the kind its
runtime reads, pinned, and the base's weights the ones it was pinned to --
the hash read from the memo a load keeps, never written, and a base the
doctor cannot read skipped, never passed. Each failure names its doors, each
a command that runs as printed: the set without it, the set pinned again,
`--model-path` back to the weights it was pinned to when the store holds
them, and -- for a version `train promote --adapter` made, when its base
changed -- that run promoted again, its gate scoring it over the base the
entry serves now.

### The script protocol

Every shipped driver speaks one line protocol on stdout -- one JSON object
per line:

```
{"message": "..."}                                    a note
{"error": "..."}                                      fatal; a non-zero exit follows
{"rows_written": N, "rows_skipped": N, "out": "..."}  prepare's terminal record
{"iteration": N, "total_iters": N, "loss": F, "lr": F, "throughput": F}   a trainer's progress
{"fused_dir": "..."}                                  a trainer's fuse record
{"text": "..."}                                       a trainer's infer record
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
**eval suite** for one skill. Eight ship, seeded under `training/kits/` --
four general skills:

| Kit | Skill |
|---|---|
| `instruction-following` | follow explicit constraints (length, format, casing, required words) precisely |
| `structured-output` | emit well-formed, minimal JSON of a requested shape |
| `summarization` | condense a passage into a faithful, concise summary |
| `reasoning` | solve arithmetic and logic problems with a correct final answer |

and four **role chores** -- the jobs Apogee hands a suite's small helper
models, each in the product's own request format:

| Kit | Chore |
|---|---|
| `utility-chores` | the utility role's: a follow-up rewritten as a standalone search query, and a tool's output summarised for the model that acts on it |
| `extraction-fidelity` | the extraction role's: the knowledge graph's entities and relations, and the shipped symphonies' fact, action and card stages, each one JSON object held to its schema |
| `tool-calling` | the one tool call a request needs -- the right tool, and arguments its schema accepts -- against Apogee's own file, shell and git tools |
| `knowledge-capture` | the capture clerk's: a raw discussion read into one canonical intent record |

A role-chore kit's prompts are Apogee's own requests, byte for byte: the
fixed part of each -- an instruction, a clerk's system prompt, a stage's
prompt, a set of tool declarations -- is one of the kit's **prefixes**, and a
test fails the build the moment a prefix and the product drift apart. A clerk's
system prompt rides in the one turn a kit holds, a blank line before the
text it reads. `tool-calling` teaches the call's content as JSON (`{"name":
…, "arguments": {…}}`); the wrapper each model family's template puts around a
call stays the template's. The `web-search` kit stays deferred.

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

A kit whose prompts begin with a fixed text names each once under
`prefixes:`, and then every prompt begins with one:

```yaml
prefixes:              # name -> the text a prompt begins with, byte for byte
  ask: "Answer the question below.\n\nQuestion: "
eval:
  - prefix: ask        # this prompt is the prefix's text, then its own
    prompt: "Why is the sky blue?"
    expected: "scatter"
```

The teacher is shown each prefix once, names the one an example begins with
in a `prefix` field, and writes only what follows it; Apogee puts the text
back, so a long fixed text is never copied out at every call, and an example
naming none of the kit's is dropped. In a kit with prefixes, an eval item
without one is refused.

Drop a `<name>.yaml` of the same shape beside the bundled ones; `apogee
check` validates every installed kit.

## The run

```bash
apogee models pull Qwen/Qwen2.5-0.5B --safetensors        # a student
apogee train setup --trainer auto --with convert          # the stack, once
apogee datasets synth maths --teacher paid --kit reasoning
apogee train run Qwen--Qwen2.5-0.5B --dataset maths --iters 500 --mask-prompt
apogee train eval 20260919-143022 --suite reasoning --judge paid
apogee train promote 20260919-143022 --as qwen-maths
apogee chat -m qwen-maths
```

### `train run`

```bash
apogee train run <student> --dataset <name|path> [--method lora|qlora] [--iters N]
                 [--batch-size N] [--num-layers N] [--grad-checkpoint] [--mask-prompt]
                 [--trainer auto|mlx|peft|mock]
```

A LoRA (or QLoRA) fine-tune of a **SafeTensors snapshot** -- a directory
holding `config.json` and `*.safetensors`, named as a path or as a snapshot
under `paths.hf_dir` or `models/` -- executed by a Python trainer
subprocess under the environment: `train_mlx.py` over `mlx_lm` on Apple
Silicon, `train_peft.py` over `transformers` + `peft` (+ `bitsandbytes` for
QLoRA) on CUDA. **Only full-precision weights are trainable**: a GGUF or a
backend name is refused naming `apogee models pull <owner>/<repo>
--safetensors`. `--trainer auto` (the default, or `training.trainer`) picks
`mlx` on macOS/arm64 and `peft` where `nvidia-smi` is on PATH; `mock` is an
in-process trainer that needs no Python, for trying the whole chain.

The driver's JSONL progress becomes the status line -- `iter n/N · loss L ·
lr R · T it/s` -- and on a pipe a line every tenth. Ctrl-C terminates the
child and records the run as `cancelled`. Every run gets a directory
`training/runs/<YYYYMMDD-HHMMSS>/` with `adapters/` and a `manifest.json`
recording the trainer, the student, the dataset and its sha256, the
hyperparameters, the final loss, `status` (`running` while it runs, then
`complete`, `failed` or `cancelled`) and the timestamps. **A crashed driver
is a failed run, never a silent success**: an `{"error"}` line is its own
event, a non-JSON line (a stack trace) is kept as a message, and the exit
code is carried -- three gaps that would otherwise be silent, closed by the
protocol.

| Flag | Default | Description |
|---|---|---|
| `--dataset` | required | a dataset name (`datasets list`) or a `.jsonl` path |
| `--method` | `lora` | `lora` or `qlora` (4-bit on PEFT; on MLX the snapshot's own precision) |
| `--iters` | the driver's (1000) | training iterations |
| `--batch-size` | the driver's (4) | batch size per step |
| `--num-layers` | the driver's (16) | transformer layers LoRA is applied to (MLX) |
| `--grad-checkpoint` | off | slower, less memory |
| `--mask-prompt` | off | completion-only loss: the prompt tokens are excluded |
| `--trainer` | `training.trainer`, else `auto` | `auto`, `mlx`, `peft`, `mock` |

Two details the drivers get right:
`train_mlx.py` lays out the `{train,valid}.jsonl` directory `mlx_lm.lora`
actually wants (every example trains; validation is a copy of the first
tenth, so nothing is held back from a small dataset), and `train_peft.py`
masks the prompt **exactly** -- the token count of the chat template with
the generation prompt appended -- through transformers' own `Trainer`
rather than a response-template heuristic.

### `train eval`

```bash
apogee train eval <run-id> [--suite <path|name>] [--judge <backend>] [--force]
```

The gate. A suite is `{prompt, expected?}` JSONL: a path, a name under
`training/suites/`, a prepared `<name>.eval.jsonl` (`datasets prepare
--as-eval`), or a kit's inline eval by kit name. An item **with**
`expected` is a deterministic substring check. One **without** is a
**pairwise judge** comparison of the candidate (the base plus the adapter,
through the trainer's own inference, no fuse, no GGUF) against **its own
untuned base** -- did the adapter help? -- the judge answering `A`, `B` or
`TIE` with anything else a tie, and a judge or baseline failure a tie
too, under the never-fail contract the rerank judge keeps. Without a judge
such items **skip and auto-pass, loudly, with the count**. The judge is
named explicitly (`--judge` or `training.judge_backend`), is never a vendor
CLI, and gets a 1024-token budget so a reasoning judge reaches its verdict.

**The gate is 100%**: `score` is reported, `passed` only when every item
passed. The results land in the run's manifest; a second `eval` shows them
and re-runs only with `--force`.

### `train promote`

```bash
apogee train promote <run-id> --as <backend> [--force] [--quantize TYPE] [--keep-fused]
apogee train promote <run-id> --as <backend> --target mlx [--force]
apogee train promote <run-id> --as <backend> --adapter [--force] [--suite <path|name>] [--judge <backend>]
```

The only path from a run to inference, in order: the **eval gate** (hard by
default -- an unevaluated or failing run is refused; `training.gate_mode:
soft` makes that a warning; `--force` skips it); **fuse** the adapter into
the base (`runs/<id>/fused/`, SafeTensors); **convert** to GGUF with
llama.cpp's own `convert_hf_to_gguf.py`, vendored at the pinned revision
and seeded under `training/scripts/convert/`, run under the environment's
interpreter with the `convert` set, into `training/versions/<backend>/
v<N>.gguf` (F16; `--quantize Q4_K_M` runs the in-process quantizer when the
binary links llama.cpp, else refuses naming `apogee models quantize`);
**verify** the header with the GGUF reader **before any config is
touched**; then **register** through the one config editor -- a new
`llamacpp` entry appended, or an existing one's `model_path` replaced in
place, comment-preserving and byte-exact -- and append the **version
ledger** (`training/versions/<backend>.json`: `active_version`, and per
version the run id, the GGUF path, the time and the eval score). Version
numbers are `max + 1`, never the count. `retain_versions` (default 3) prunes
the oldest inactive GGUFs, never the active one, and keeps the pruned
entries as history. The fused checkpoint is removed after a successful
conversion unless `--keep-fused`. **A failure at any step leaves the config
and the ledger unchanged.**

**`--target mlx`** (Apple silicon, the `mlx` backend): the same plan with the
conversion taken out -- the gate, then the **fuse straight into the model
store** (`models/<model>/mlx/<id>/`, claimed while it is written), the
directory **read whole** (configuration, tokenizer, every shard within its
header -- the store's MLX check), committed under the id its weights hash to
with an `apogee-snapshot.json` record (`source: train`, `ref: <backend>
v<N>`, `transform: promote run <id>`), and only then the config: a new
`type: mlx` entry, or an existing `mlx` entry's `model_path` replaced in
place. No GGUF is written and the `convert` set is never asked for. The
ledger records the version's `mlx_path` where a GGUF version records its
`gguf_path`; numbering, retention (a pruned MLX version's directory removed
whole) and rollback are the same. Without `--target`, a promote into an
existing entry makes what that entry runs (`mlx` for an `mlx` entry), and a
new name makes a GGUF. Refused before anything is built: a `--target` an
existing entry does not run, `--quantize` with `--target mlx` (it makes a
GGUF), and a promote of one kind into a backend whose versions are the other
-- a rollback repoints `model_path` and never changes a backend's type.
`--keep-fused` is a note: the fused weights are the version. Registered on a
machine that cannot run it (off Apple silicon, no `mlx-lm`), the entry is
kept and the reason said. The regime and the cycle still promote GGUFs.

**`--adapter`** (53b; opt-in -- the merge above stays the default): the
fuse is skipped and the adapter itself is the version, applied over the
base the entry already runs and never merged into it -- kilobytes or a few
megabytes a version instead of a whole model. The same gate first: the mode
changes what is written, never what is judged. `--as` names an **existing**
`llamacpp` or `mlx` entry with a `model_path` -- its base, which is never
touched. For a `llamacpp` entry the run's PEFT adapter is converted by
llama.cpp's own `convert_lora_to_gguf.py`, vendored at the pinned revision
beside `convert_hf_to_gguf.py` and seeded with it under
`training/scripts/convert/`, run under the environment's interpreter with
the `convert` set (`--base` the run's base snapshot, the Hub offline) into
`training/versions/<backend>/v<N>.lora.gguf` (F16), whose header is read
before anything else is touched. For an `mlx` entry the run's mlx-lm
adapter directory is copied as it is -- no conversion, no `convert` set --
into `training/versions/<backend>/v<N>.lora/`, the trainer's numbered
checkpoints left in the run. Either way the adapter is then **pinned** to
the base's weight hash (53a's rule, the hash remembered in
`cache/weight-hashes.json`) and **registered** on the entry's `adapters:`
through the one config editor, in the place of the active version's adapter
and at its scale -- an adapter the ledger did not put there stays as it is
-- and the ledger records the version's `adapter_path` and the
`base_sha256` it was pinned to. Numbering, retention (a pruned version's
adapter removed) and rollback (the entry's adapter repointed, with its pin)
are the same. Refused before anything is written: a name no entry has,
`--quantize` and `--keep-fused` (nothing is fused), a second adapter on an
`mlx` entry (mlx-lm loads one), a pipeline stage after the first (it trained
over the stage before's fused checkpoint, which no entry runs), and
**formats that would cross** -- an mlx run's adapter onto a `llamacpp`
entry, or a peft run's onto an `mlx` one, each refused naming both formats.
A backend's versions are merged or adapters, never both: a merged promote
into a backend holding adapter versions is refused (it would replace the
base under them), and an adapter promote into one holding merged versions
(its `model_path` is a fine-tune, not the base the adapter was trained
over).

**Onto a quantized base** (53c): an adapter is trained against
full-precision weights, and running it over a quantized base costs quality
nobody measured -- so when the entry's base is quantized (a GGUF whose
header's `general.file_type` is `Q4_K_M` or any other quantized type, an MLX
directory whose `config.json` says 4-bit or a publisher's quantization), the
gate scores the candidate **where it will serve**. After the adapter is built
and pinned, and before anything is registered, the suite runs through the
entry's own runtime -- llama.cpp or mlx-lm, the quantized base with every
adapter the entry will apply, greedy at the trainer's 256-token answer budget
-- and a judge, when there is one, compares it with the bare base on the same
runtime. That eval is the gate, one per candidate: the run's recorded eval,
scored where its trainer ran, is not read, and no full-precision score is
computed beside it -- so no `train eval` is needed first. The suite is
`--suite`, else the run's recorded eval's, else `training.eval_suite_path`;
the judge `--judge`, else the recorded eval's, else `training.judge_backend`.
The report says where it scored -- `[promote] scored on
tiny-Q4_K_M.gguf (Q4_K_M): 100% (24/24)` while it runs, `eval: 100% (24/24),
scored on tiny-Q4_K_M.gguf (Q4_K_M)` in the summary -- and the ledger records
it as the version's `eval_scored_on`. A candidate under the floor (the gate's
100%) is refused saying where it was scored, its adapter removed, the config
and the ledger unchanged; `gate_mode: soft` makes that a warning, and
`--force` skips the scoring altogether, said. With no suite anywhere a hard
gate refuses before anything is built, naming `--suite`; `--suite` and
`--judge` on any other promote are refused, since there the run's recorded
eval gates. A mock run is scored by the mock's own candidate over the base's
path, so the chain runs on the real binary with nothing installed.

### `train rollback`, `versions`, `status`

```bash
apogee train rollback <backend>      # repoint model_path (or, for adapter versions, the entry's adapter) at the previous version
apogee train versions [<backend>]    # the ledger(s): version, time, run, eval, weights (a GGUF, mlx/<id>, or v<N>.lora[.gguf]), active
apogee train status                  # the runs (newest first, running ones counted) and the active versions
```

`rollback` repoints the backend at the highest version below the active
one (not "active minus one": numbers have gaps after a prune) and **deletes
nothing**; a pruned target, one whose file (or MLX directory, or adapter) is
gone, or an entry whose type is no longer the version's, is refused by name.
An adapter version's rollback repoints the entry's adapter in the active
version's place, with the pin the ledger recorded; the base is not touched.
The filesystem is the source of truth for all three: nothing is cached.
`status` also rolls up the cycle (idle, running now, or halted with the
reason; the anchor) and the pipelines (the running one, else the latest).

## Pipelines

```bash
apogee train pipeline run --pipeline <spec.yaml|name> [--continue-on-fail] [--judge <backend>] [--trainer …]
apogee train pipeline resume <pipeline-id> [--pipeline <spec>] [--continue-on-fail] [--judge …]
apogee train pipeline status [<pipeline-id>]
```

A pipeline teaches several skills **in sequence without forgetting**. Its
spec -- a YAML file, or a `training.pipelines:` entry, read by the same
parser -- names the student snapshot and ordered stages:

```yaml
name: skills
student: Qwen--Qwen2.5-0.5B             # as `train run` names one
stages:
  - name: instructions
    dataset: instructions               # a dataset name, or a .jsonl path
    eval_suite: instruction-following   # a kit's inline suite, a suites/ name, a prepared .eval, or a path
    iters: 500
  - name: reasoning
    dataset: maths
    eval_suite: reasoning
    iters: 500
    rehearsal_fraction: 0.1             # mix a tenth of each prior dataset into this stage
```

Each stage is a **fresh LoRA on top of the previous stage's fused
weights**: stage 0 trains from the snapshot; every intermediate stage that
ran is fused into a concrete SafeTensors checkpoint
(`runs/<id>-s<N>/fused/`) the next stage trains from -- never a stack of
raw adapters -- and **the final stage is left unfused** for `train promote`.
A stage's fields are `train run`'s (`method`, `iters`, `batch_size`,
`num_layers`, `grad_checkpoint`, `mask_prompt`); `rehearsal_fraction`
mixes a deterministic sample of each prior stage's dataset into the stage's
own (the mix is written beside the run as `rehearsal.jsonl`, and the
stage's manifest records it as what trained).

**The cumulative gate is the contract.** Stage N is evaluated on the union
of suites 0..N at 100%, so a stage that improves its own task but regresses
an earlier one fails. Under the default hard gate the run stops there --
`aborted`, the stage `failed`, the later ones `pending` -- and the fix is
`pipeline resume`, which continues from the first stage that has not
passed, the passed ones untouched. `--continue-on-fail` (or `training.gate_mode:
soft`) goes on instead, the failed stage still fused for the next; the
summary shows the mixed statuses. A resume refuses a complete run, and a
spec whose stage count changed (the passed stages did so under a different
plan). Ctrl-C leaves the run `aborted` and resumable.

Every stage is an ordinary run `<pipeline-id>-s<N>` under `training/runs/`
carrying `parent_run` and `pipeline_run_id`, so `train eval` (which finds
the cumulative results already recorded) and `train promote` take one
directly; the pipeline's own manifest under `training/pipelines/<id>/` is
rewritten on every transition (`pending` → `training` → `evaluating` →
`fusing` → `passed` | `failed`; the run `running` → `complete` | `aborted`
| `failed`). `pipeline status <id>` prints the table with the promote
command for the last passing stage; without an id it lists the runs.

## Regimes

```bash
apogee train regime run [<name>] [--teacher <backend>] [--student <snapshot>] [--kit <name> …] [--all-kits]
                        [--as <backend>] [--count N] [--iters N] [--temperature T] [--max-tokens 4096]
                        [--judge <backend>] [--no-promote] [--trainer …] [--regime <spec.yaml>]
```

The distillation workflow in one command. For each kit, in order, the
**teacher** synthesises a dataset through the same core `datasets synth`
uses (parallel batches for an API teacher, retries with backoff) into
`training/regime/<id>/<kit>.jsonl`, and the kit's inline eval is
materialised beside it; the kits then become **one eval-gated pipeline**
over the student -- a stage per kit with the kit's `train:` block as its
defaults (`--iters` overrides every kit's), the cumulative gate across
skills -- and the last passing stage is promoted as `--as` unless
`--no-promote`. A regime is ad hoc from flags, a regime by name, or a spec
file, and **flags always win** over a loaded spec. A name is looked up
nearest first: a `training.regimes:` entry, then a spec under
`training/regimes/` (run by its file's name), then a shipped regime's
compiled-in text while no file of its name is there:

```jsonc
"training": {
  "regimes": {
    "everything": {
      "description": "every kit I care about",   // what `train regime list` shows
      "teacher": "paid",
      "student": "Qwen--Qwen2.5-0.5B",
      "kits": ["instruction-following", "reasoning"],   // ordered; each becomes one stage
      "count": 200,            // examples per kit; unset = each kit's synth.count
      "promote_as": "qwen-tuned",
      "iters": 500,            // per stage; unset = each kit's train.iters
      "temperature": 0.8       // the teacher's; unset = each kit's
    }
  }
}
```

### Shipped regimes

```bash
apogee train regime list [--output-format json]          # name, source, kit count, description
apogee train regime show <name> [--output-format json]   # its kits in order and its tuning
```

Apogee ships three. **`foundation`**: the four general kits as one gated pipeline,
in the order instruction-following → structured-output → summarization →
reasoning (the spec's comments say why), each kit's own count and
iterations. A shipped regime names kits and tuning only -- **never a
teacher or a student**, which are always yours:

```bash
apogee train regime run foundation --teacher <backend> --student <snapshot>
```

**`harness-helpers`**: `utility-chores` → `extraction-fidelity` →
`tool-calling`, the regime that makes a small local model a better suite
member for the helper roles. **`knowledge`**: `knowledge-capture` alone, for
an install that leans on `apogee knowledge`.

`check --fix` seeds each to `training/regimes/<name>.yaml`, skip if
present: `list` says `shipped` while no file is there, `seeded` while the
file is byte for byte the shipped one, and `user` once you edit it (or for
a spec or a `training.regimes:` entry of your own). A spec file under
`training/regimes/` that does not parse is left out of `list` and said on
stderr; a run naming it is told why. Every bundled kit's inline eval holds
at least 24 items (`tool-calling` 32), each with an `expected`, so a
stage's cumulative gate asks something real of the student.

`--all-kits` runs every installed kit alphabetically; an explicit `--kit`
list wins, so the order can be curated. The teacher is named explicitly
(the spend rule) and is never a vendor CLI. Every refusal -- the teacher,
the student, the kits, the judge, the trainer, and for a promotion its
converter -- comes before the first teacher call, and a regime that stops
keeps its synthesised data and suites under its work directory.

## The cycle

```bash
apogee train cycle run [--source <dir>]   # one gated pass, for launchd or cron
apogee train cycle status                 # the history and the anchor
apogee train cycle halt                   # every `cycle run` refuses until resume
apogee train cycle resume                 # clear a halt (manual or the breaker) and the failure count
```

Unattended, scheduler-invoked training: one invocation is **one gated
pass**, and there is no daemon and no `--watch` -- schedule it with
launchd or cron. Configure it under `training.cycle:`:

```jsonc
"training": {
  "cycle": {
    "pipeline": "skills",              // a training.pipelines name, or a spec file
    "backend": "qwen-nightly",         // where a passing cycle promotes
    "regression_threshold": 0.0,       // tolerated drop against the last pass and the anchor
    "circuit_breaker_k": 3,            // consecutive failures that halt the loop; 0 disables
    "sources": [
      {"type": "directory"},           // *.jsonl in training/cycle/queue/ (or `dir`)
      {
        "type": "sessions",            // your own chats
        "log_consent": true,           // REQUIRED for this source
        "backend": "qwen-nightly",     // only chats on this backend (optional)
        "since": "2026-09-01"          // only chats from this date (optional)
      }
    ]
  }
}
```

One pass: the **lock** (`cycle/cycle.lock`, created exclusively; a second
scheduler firing at once is refused naming it), the **circuit breaker**
(halted, or `consecutive_fails ≥ circuit_breaker_k`, refused naming
`cycle resume`), then the sources -- a `directory` queue of `*.jsonl`
(`--source <dir>` replaces the first one for this run), and `sessions`,
your persisted chats, mined one completed exchange at a time exactly as
`datasets create --from sessions` does, **only with `log_consent: true`**
(a source without it fails the config load, naming the two risks: the
sessions are your own data, and a model trained on its own answers
reinforces its mistakes) and **only sessions newer than the watermark** the
last cycle recorded, so a conversation is never trained on twice. No data
records `skipped`, counting no failure. Otherwise the sources are merged
into `cycle/work/merged.jsonl` and the named pipeline runs with **every
stage's dataset replaced by the merged file and its own suite kept**, then
the **anchor-baseline dual gate**: the **final** stage's cumulative score
must not regress beyond `regression_threshold` against the last passing
cycle **and** against the pinned anchor -- set automatically on the first
passing cycle, or `anchor_version` -- because per-cycle no-regression alone
lets tiny regressions accumulate into drift. Under the default hard gate
every stage of a complete pipeline scored 100%, so the dual gate is a
formality there; it is under `training.gate_mode: soft`, where the
per-stage gate is advisory and a stage may complete below 100%, that the
dual gate is the one holding the line. (Reading the score from the last
*passed* stage instead -- 100% by the gate's own definition -- would leave
an anchor gate that could never fail.) **Pass** promotes into
`training.cycle.backend` through the same path `train promote` takes (its
own gate runs again), moves the consumed queue files to `consumed/`,
advances the sessions watermark and resets the failure count; **fail**
discards the candidate -- **nothing reaches inference** -- counts toward
the breaker, and at `k` halts the loop with the reason in the record. The
history is `cycle/history.json`, written atomically at every outcome, and
`cycle halt` / `cycle resume` are the two edits to it, so nobody edits the
file by hand. A failed or refused pass
exits non-zero, so a scheduler's log shows it.

Schedule it with launchd:

```xml
<!-- ~/Library/LaunchAgents/com.apogee.cycle.plist -->
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key> <string>com.apogee.cycle</string>
  <key>ProgramArguments</key>
  <array>
    <string>/usr/local/bin/apogee</string>
    <string>train</string> <string>cycle</string> <string>run</string>
  </array>
  <key>StartCalendarInterval</key>
  <dict> <key>Hour</key> <integer>3</integer> <key>Minute</key> <integer>0</integer> </dict>
  <key>StandardOutPath</key> <string>/tmp/apogee-cycle.log</string>
  <key>StandardErrorPath</key> <string>/tmp/apogee-cycle.log</string>
</dict>
</plist>
```

```bash
launchctl load ~/Library/LaunchAgents/com.apogee.cycle.plist
```

or with cron:

```
0 3 * * * /usr/local/bin/apogee train cycle run >> /tmp/apogee-cycle.log 2>&1
```

## Over HTTP

The datasets slice is a twin family on the admin plane -- `GET/POST
/v1/admin/datasets`, `GET/DELETE /v1/admin/datasets/{id}`, `GET
/v1/admin/datasets/kits`, and `POST /v1/admin/datasets/synth` as an async
job -- see [http-api.md](http-api.md). Synth is teacher inference, not
training, which is why it is exposed.

**Training itself is read-only over HTTP**: `GET /v1/admin/training/status`,
`/runs` (runs and pipeline runs, `?kind=` to keep one), `/runs/{id}`,
`/versions` and `/cycle` under the admin bearer serve the manifests, the
ledgers and the cycle history the CLI writes. `train run|eval|promote|
rollback|setup`, `pipeline run|resume`, `regime run` and `cycle
run|halt|resume` have no route, forever -- an expensive GPU job with live
progress is not a control surface a remote client should be able to start,
and a promotion changes what the server chats with. Each is a documented
parity carve-out.
