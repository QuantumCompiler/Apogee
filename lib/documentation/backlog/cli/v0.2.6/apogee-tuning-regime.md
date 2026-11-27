# CLI-61b · An Apogee-tuning regime and its eval

| Field | Value |
|---|---|
| **Type** | ✨ Feature |
| **Project** | CLI |
| **Status** | 🔒 Blocked (61a) |
| **Fix version** | v0.2.6 |
| **Component** | Training: the protocol eval, `datasets holdout`, the trainers and the `apogee` regime |
| **Labels** | `training` `eval` `privacy` `additive` |
| **Epic** | Track 61 · Apogee-tuned training |
| **Blocked by** | [61a](sessions-as-training-data.md) |
| **Blocks** | — |
| **Reporter** | The Apple-platforms spike and brainstorm (2026-11-07), on the user's vision |
| **Created** | 2026-11-07 |

---

## Summary

Nothing measures a model on Apogee's own protocol: the only scorer reads a reply's text, and the trainers drop a step's offered tool definitions. This item trains a student on [61a](sessions-as-training-data.md)'s dataset of the person's own sessions and measures the result: a step-level protocol eval scored on the call the provider actually parsed, a bundled `apogee-protocol` suite, a held-out split of whole sessions, and a shipped `apogee` regime whose promotion the protocol gate holds.

## Description

### User story

**As** a person running Apogee with local models on the machines I own,
**I want** a model tuned on my own sessions to be good at Apogee itself (calling the tools it is offered with arguments they accept, taking no for an answer, asking through `ask_user` when someone can answer, ending a turn with an answer instead of a call written out as text), and to see that measured before it is promoted,
**so that** a local model feels like Claude on every device I own, without any of it needing a server, a network or an account.

### Background

The user's vision, 2026-11-07: Apogee is a private, self-hosted harness that feels like Claude on every device the person owns. A strong machine (for example 512 GB of RAM and a finely tuned large model) serves the person's own desktops and phones, and Apogee does what frontier harnesses do, with local models. A local model only feels like that if it is good at Apogee itself: calling the tools it is offered with arguments they accept, taking no for an answer, asking through `ask_user` when someone can answer, and ending a turn with an answer instead of a call written out as text.

[61a](sessions-as-training-data.md) turns the person's own sessions into training data. This item trains on that data and measures the result.

**The shape of the design.** This item delivers four things:
- **The protocol eval.** A step-level suite format; seven deterministic checks run over the call the provider actually parsed; and scoring of a run or of any registered local backend.
- **The bundled `apogee-protocol` suite.** It is written against the product's own definitions and denial text, so any model can be measured with no personal data.
- **The held-out split.** Whole sessions are kept out of 61a's dataset and become the person's own suite.
- **The shipped `apogee` regime.** It tunes a student on the training part, and the protocol gate holds its promotion.

### Evidence

Re-measured 2026-11-07 at HEAD `4f92316d`: the source, and the branch binary over a scratch `APOGEE_HOME` with the mock trainer and no model.

**W12 — nothing measures a model on Apogee's own protocol.**

1. **The only scorer reads text.** An eval item is `{prompt, expected?}` (`business/training/kit.h:30-35`). A candidate answers one prompt with text (`CandidateRunner::run(prompt)`, `business/training/trainer.h:103-123`). The check is `reply.text.find(item.expected)` (`eval.cpp:202-206`), or else a pairwise judge. No item offers a tool definition, no reply carries a parsed call (`harness::ChatMessage::tool_calls`, `data/contracts/types.h:147-148`, the field the loop reads), and no item carries a permission context or the turn after a denial.
2. **The nearest suite checks a tool's name and nothing else.** All 32 eval items in the bundled `tool-calling` kit expect only the substring `"name": "<tool>"` (`assets/training/kits/tool-calling.yaml:57-61` is the first of them). Arguments are never checked, and a prose reply that mentions the name passes. The kit also teaches the call as JSON in text, and "the wrapper each model family's template puts around a call stays the template's" (`reference/training.md:360-361`). So the parse that turns a model's output into a call is never scored.
   - **The probe:** it trained a mock run over a planted snapshot and ran `train eval <run> --suite tool-calling`: `eval FAILED -- 0% (0/32)`, and the manifest's checks were all `contains`, and every item kept its `prompt` and `got`.
3. **Only a run can be measured.** `train eval mock --suite tool-calling` answered `no run 'mock': no manifest at …/training/runs/mock/manifest.json`. `train eval x --backend m --suite s` answered `The following arguments were not expected: m --backend`. There is no way to score a registered model (a family's Q4_K_M, a frontier backend for reference, the version a backend serves now) on anything.
4. **The habits the protocol asks for are scored nowhere.** A denied call returns "Error: the user denied permission to run '…'. Do not retry it; continue without it or ask what to do instead." (`business/agent/tool.cpp:253-256`). `ask_user` is offered if and only if someone can answer it (`business/agentloop/loop.cpp:235-239`, `question.h:25`), and machine mode draws it as a `question` event (`reference/machine-mode.md:877-879`). No eval checks whether a model retries after a no, or asks through `ask_user` and not in prose.
5. **A suite built from sessions would serve their text.** Every recorded item keeps its `prompt` and `got` (`eval.cpp:35-38`; the probe's manifest held both). The admin plane serves the whole manifest at `GET /v1/admin/training/runs/{id}` (`reference/http-api.md:1266-1272`). A suite made from the person's sessions in today's shape would put their conversations in a served document.

**Beside W12: training learns no tool call from a session, and the trainers drop `tools`.**

6. **The miner skips tool messages** (`business/training/datasets.h:132-137`). That is 61a's wall, and 61a closes it.
7. **The trainers render a line's `messages` and drop anything beside them.** 61a's consumed decision hands this item what comes next, and the first of it is this gap. `train_peft.py` calls `tokenizer.apply_chat_template(messages[:-1], add_generation_prompt=True, tokenize=False)` with no `tools` (`assets/training/train_peft.py:129-137`), so a step record's offered definitions would never reach the student.

## Acceptance criteria

- [ ] **Given** a temp home with a mock entry, **when** `apogee train eval --backend <mock entry> --suite apogee-protocol` runs, **then** it prints a score per check; a script that retries a denied call fails `no-retry`, and one that writes its call as text fails `clean`.
- [ ] **Given** a 61a dataset, **when** `apogee datasets holdout <61a dataset>` runs, **then** it writes a training part and a `.protocol.jsonl` suite with no session in both, and the same split on a second run.
- [ ] **Given** a planted snapshot, **when** `apogee train regime run apogee --student <planted snapshot> --trainer mock --as <entry>` runs, **then**:
  - The gate report names each check against the base.
  - A candidate scripted to regress is refused, with the config and the ledger unchanged.
  - A passing one is promoted.
- [ ] **Given** a run scored on a protocol suite, **when** `GET /v1/admin/training/runs/{id}` is read, **then** it serves the `protocol` block with step references and no session text, and no training route is added.
- [ ] All of it runs with no `serve`, no pairing and no network.
- [ ] Real weights (optional, per [the model families](../../../assistant/cli/DEVELOPER.md#on-real-weights-the-model-families): Meta, Qwen, Google and OpenAI, each one installed instruction-tuned model on its Q4_K_M build when there is one, one family at a time with its model loaded once, never DeepSeek):
  - **Given** a family's installed entry, **when** `apogee-dev train eval --backend <that family's entry> --suite apogee-protocol` runs, **then** it scores every step with no scorer error. A template whose calls the provider cannot parse shows as `clean` or `decision` failures, never as a crash.
  - The per-family scores are recorded in the write-up as the baseline line.
  - One short `apogee-dev train regime run apogee` on a small SafeTensors snapshot already in `~/.apogee-dev` reports the gate against its base.
  - A family, or a snapshot, that is not installed is skipped and said. Nothing is pulled.
- [ ] `reference/training.md` documents the format, the checks, the split, the gate and the regime, and the full suite is green.

## Constraints

- **The anti-goal: nothing here makes standalone use need a server.** The CLI alone and the Mac app over pipes stay complete and stay the default (the user's call, 2026-11-07). The regime and the eval run in the CLI, on whatever sessions the store holds. A standalone install's own chats train it as well as a server's. Nothing reads a pairing, an account or the network, and the mock-backed tests prove it with no `serve` running.
- **Never listens; serving is the server's** (CLI CLAUDE.md → Interactive turns never open a listening socket). The eval calls the candidate in-process, through the backend's own provider. `serve` is never started to score a model, and `cli.no_listen_symbols` stays green unchanged.
- **Training control stays off the network.** `train run|eval|promote`, `pipeline` and `regime run` "have no route, forever" (`reference/training.md:981-986`), a documented parity carve-out. A paired device ([59a](../v0.2.3/paired-devices.md)) cannot start a run or a promotion. The protocol results reach the served reads only as new fields of the existing manifest.
- **The person's sessions stay theirs.**
  - A manifest, a report and every served read name a held-out step by its reference (session id, turn, step) and its check outcomes, never by its messages or the candidate's reply. Only the bundled suite's steps, which are product text, keep `got`.
  - The checks are deterministic and the eval has no judge. Held-out sessions are scored on local backends only (`llamacpp`, `mlx`, `mock`). A cloud backend scores the bundled suite alone, and the report says so.
- **Secrets are unleakable** (CLI CLAUDE.md). The suite carries 61a's scrubbed records and [60a](../v0.2.4/one-tool-definition.md)'s definitions, which have no slot for a command, an environment or a key. A planted key and a planted distinctive session string never reach a manifest, a log, stdout or an admin response.
- **Additive** ([ADR 0006](../../../adrs/cli/backwards-compatibility.md)). `eval_results` gains a `protocol` block, and a regime spec gains optional fields. A manifest or spec written before this item reads as it does now. `train eval <run> --suite <kit>` scores exactly as it does now.
- **Parity** ([ADR 0002](../../../adrs/cli/mode-parity.md)): one scorer and one gate, whichever surface asks. The CLI and machine mode share them, and `train eval` streams as the long job it already is in machine mode (`reference/machine-mode.md:375`). The served reads show the same block the CLI prints.
- **The four layers** ([ADR 0001](../../../adrs/cli/layer-enforcement.md); CLI CLAUDE.md → The harness never includes backends).
  - The checks, the split and the gate live in `business/training/`, which may include only `training|harness|contracts|platform|agentloop|agent` (`tests/scripts/cmake/layering.cmake:429`). That is enough for 60a's definition and the schema validator (`agentloop/structured.h:38`).
  - The runner that calls a backend is a closure built in the composition root, `presentation/cli/train.cpp`, as 53c's serving runner is (`business/training/promote.h:40-45`).
- **One gate into inference.** Promotion still reads one verdict: the manifest's `passed`. The protocol gate decides it for a protocol suite, and nothing else gains a path to inference. A failing candidate leaves the config and the version ledger unchanged, as every promotion does.
- **The dev channel** ([CLI CLAUDE.md → Development runs on the dev channel](../../../assistant/cli/CLAUDE.md#development-runs-on-the-dev-channel)). Live passes name `apogee-dev` over `~/.apogee-dev`, using the models and snapshots already there. Nothing is pulled to test this.

## Technical approach

Source paths under `lib/src/cli/source/`; `assets/…` and `tests/…` under `lib/src/cli/`; `reference/<file>` is `lib/documentation/reference/cli/<file>` (the CLI's reference).

| File | Change |
|---|---|
| `business/training/protocol_suite.h/.cpp` (new) | `ProtocolStep` (below), read from 61a's step record (its `messages`, `tools` and `apogee` key). Reading and writing the suite's JSONL. `holdout(dataset, fraction)` splits by whole session, deterministically. |
| `business/training/protocol_eval.h/.cpp` (new) | `ProtocolRunner`: one step in, one parsed `ChatMessage` out. The seven checks (below), each a pure function of a step and a reply. `run_protocol_eval` and `protocol_gate` (the arithmetic in the Open questions' defaults). |
| `business/training/eval.h/.cpp` | `EvalResults` gains `protocol` (per-check scores, the base's and anchor's scores, the thresholds, and step references), which `eval_results_to_json` writes. A suite whose lines are steps is scored by the protocol eval, and any other suite as today. |
| `business/training/promote.h/.cpp` | `eval_gate` reads the protocol verdict through the same `passed`. The serving runner (53c) gains a step form, so the candidate is scored where it will serve. |
| `data/contracts/config.h` (`RegimeSpec`, `:989-1006`), `business/training/regime.h/.cpp` and `regime_specs.cpp` | A regime may name `dataset` (61a's training part), `protocol_suite` and `guard_suites`. With no kits it needs no teacher. The shipped `apogee` regime. |
| `presentation/cli/train.cpp` | `train eval <run> --suite <protocol suite>`, and `train eval --backend <entry> --suite …` to score a registered model with no run. The step runner is built over the entry's provider, with the run's adapter applied for a run (53's runtime adapters). A vendor-CLI entry is refused, because it runs its own loop and not Apogee's. `train regime run apogee`. |
| `assets/training/train_peft.py` and `train_mlx.py` | A line's `tools` is passed to the student's chat template, so a call is trained in the wrapper that template puts around it. A line with no `tools` renders as today. |
| `presentation/cli/datasets.cpp` | `datasets holdout <name> [--fraction F]` writes the training part as a dataset and the held-out part as `training/suites/<name>.protocol.jsonl`. |
| `assets/training/suites/apogee-protocol.jsonl` (new) | The bundled suite, seeded by `check --fix` as the kits are, with each installed suite validated by `check`. |
| `reference/training.md` | A "Tuning for Apogee" chapter (the step format, the checks, the split, the gate, the regime) and a line in "Over HTTP". |
| `reference/http-api.md` | The run manifest's `protocol` block. |

**`ProtocolStep`'s fields** (`business/training/protocol_suite.h/.cpp`):

- `session`, `turn` and `step`: the reference.
- `tools`: the 60a definitions offered at that point, including any a device offered under [60b](../v0.2.4/device-tools.md) with `runs: device`.
- `messages`: the conversation up to the decision.
- `context`: the permission mode, whether `ask_user` is offered, and the call just denied, if any.
- `reference`: what the session did next, as content and/or calls.

**The seven checks** (`business/training/protocol_eval.h/.cpp`), each a pure function of a step and a reply:

- `offered`: every call names a tool the step offered.
- `schema`: every call's arguments validate against that tool's input schema.
- `decision`: it calls, answers or asks where the reference did.
- `match`: the first call has the reference's name and required arguments.
- `no-retry`: after a denial, no call repeats the denied tool with the same arguments.
- `ask`: it uses `ask_user` where the reference asked, and never where `ask_user` is not offered.
- `clean`: the content holds no call written out as JSON.

**Reference.**
- In-house precedents:
  - The kits, their prefixes and the registry-holding test (42a/42b; `tool-calling`, `harness-helpers`).
  - The pipeline's cumulative gate.
  - The cycle's anchor-baseline dual gate and its consent rule for sessions (`log_consent`, `reference/training.md:878-881`).
  - 53c: judged where it serves.
  - 53's runtime adapters.
  - The schema validator `structured.h` already uses for structured output.
- Siblings:
  - [61a](sessions-as-training-data.md) supplies the dataset.
  - [60a](../v0.2.4/one-tool-definition.md) supplies the definitions a step offers. Its own Reference names 61a's use of them, a recorded call's class read from the definition and not from a name, and this item reads them through 61a's records.
  - [60b](../v0.2.4/device-tools.md) and [59b](../v0.2.3/machine-mode-over-the-network.md) are why held-out sessions include a desktop's own files and shell.
  - The Apple platforms land whole in [GUI 9c](../../darwin/v0.2.5/three-apple-platforms-whole.md) before this item.
- External prior art: none checked for this document. Function-calling leaderboards score name and argument matches as `match` does, but no external suite was read, and none is cited as followed.

**Docs when it ships:** `reference/training.md` gains a "Tuning for Apogee" chapter (the step format, the checks, the split, the gate, the regime) and a line in "Over HTTP". `reference/http-api.md` gains the run manifest's `protocol` block.

## Testing

Hermetic: a temp `APOGEE_HOME`, the mock trainer, the mock backend scripting `tool_calls` (`data/backends/mock.cpp:115-137`), no model, no network.

- **`tests/business/training/protocol_eval_test.cpp`** — a table per check (pass, fail, and does-not-apply), and the gate's arithmetic against the base and the anchor. The check tables pin each check's pass, fail and does-not-apply cases: a scripted retry after a denial, a call written as text, a call to an unoffered tool and a schema-invalid argument each fail exactly their check.
- **`tests/business/training/protocol_suite_test.cpp`** — the round trip; a split that is the same twice, never puts a session in both parts, and refuses below the floor. The split's tests pin determinism and the no-session-in-both rule.
- **`tests/business/training/protocol_assets_test.cpp`**, as `role_kits_test.cpp` does for the kits — the bundled suite's definitions held byte for byte to the registry, its denial text to `tool.cpp`'s, and its `ask_user` to `question_tool()`, so a tool's change is a change to the suite in the same commit.
- **`tests/business/training/regime_test.cpp`** — a regime with no kits needs no teacher, and the gate refuses a regressed candidate.
- **`tests/presentation/cli/train_test.cpp`** — `--backend` over a scripted mock; a vendor CLI refused; a cloud entry limited to the bundled suite.
- **`tests/presentation/cli/datasets_test.cpp`** — the holdout verb.
- **`tests/business/training/progress_contract_test.cpp`**, over the stub `transformers` (`tests/business/training/scripts/stub_transformers/transformers/__init__.py:29`, whose `apply_chat_template` gains `tools`) — a line's `tools` reaches the template, and a line with none renders as before.
- **`tests/presentation/cli/leak_test.cpp`** — a planted session string and key absent everywhere: no session text and no key reach any output.
- **`cli.train_lifecycle`** (`tests/scripts/sh/train_e2e.sh`) — the regime end to end on the mock trainer, a refused regression and a passing promotion, and no new admin route.
- **Backwards compatibility** — an old manifest, an old regime spec and a kit eval read and score as before, and a dataset line with no `tools` trains as before.
- **`harness.layering`** — stays green with no backend include in `training/`.

## Open questions

| Question | Status | Answer |
|---|---|---|
| How much of a session does the eval replay? | `[default]` | The eval replays one step at a time. The candidate sees the step's messages and offered definitions and answers once, and no tool is run. Live multi-step episodes in a sandbox are out of scope. |
| How does the candidate decode? | `[default]` | Decoding is greedy (temperature 0), so two runs of one suite on one candidate score the same. |
| What does `match` compare? | `[default]` | `match` compares the tool name exactly and the arguments the schema marks required. Paths are compared after the sandbox's own normalization, and optional arguments are ignored. |
| How is the held-out part chosen, and how small may it be? | `[default]` | The held-out part is 10% of 61a's dataset by whole session, chosen by a stable hash of the session id. A held-out suite under 24 steps (the bundled kits' floor) is refused, naming the count. |
| How big is the bundled `apogee-protocol` suite? | `[default]` | The bundled `apogee-protocol` suite holds at least 48 steps. Every check applies to at least 8 of them, `no-retry` and `ask` included. |
| When does the protocol gate pass? | `[default]` | Only when all of these hold: (1) `no-retry` is 100% on every step it applies to, in both suites: the person said no. (2) No check scores below the untuned base's score on the same steps. (3) The share of steps passing every check that applies beats the base by at least 5 points. (4) The guard suites score no lower than the base's minus `regression_threshold` (default 0.0, the cycle's default). (5) Where the target backend serves a version scored on the same suites, the candidate is no lower than that version's score minus the same threshold (the anchor). |
| Where is the candidate scored? | `[default]` | Where it will serve (53c): the promotion target's own runtime with the run's adapter applied. With no target entry yet, it is an `mlx` runtime over the student snapshot on Apple silicon. Elsewhere it is refused, naming `--as <entry>`. |
| What is the shipped `apogee` regime? | `[default]` | One stage on 61a's training part, with `mask_prompt` on. Its gate is the protocol gate over `apogee-protocol` and the held-out suite, with `tool-calling` and `instruction-following` as guards. It has no teacher, and as a shipped regime it names no student and no promotion target: both are the person's. |
| What may a cloud backend or a vendor CLI score? | `[default]` | A cloud backend scores `apogee-protocol` alone, as the frontier reference line, and the report says the held-out suite was not sent. A vendor CLI is refused. |
| Does the cycle read the protocol score? | `[default]` | The cycle keeps today's sources and gate. Letting it read the protocol score is a later item. |
| How do the trainers learn `tools`? | `[default]` | From the line itself. `train_peft.py` passes it to `apply_chat_template`, and `train_mlx.py` keeps it in the data directory it lays out, after checking at build time that the installed mlx-lm renders it. A trainer that cannot render `tools` refuses a dataset that carries it, naming the trainer, and never trains on the line without it. |
| What does the regime train on, and what of 61a's preference pairs? | `[default]` | The regime trains SFT on 61a's step records only. A dataset in 61a's `preference` shape is refused by an SFT stage, naming the shape. A preference trainer over 61a's pairs (`prompt`, `chosen`, `rejected`) is a later item, and this item settles only that the pairs are kept as 61a writes them. |
| Who owns the dataset: its record format, consent, review, scrubbing and corrections? | `(consumed decision)` | [61a](sessions-as-training-data.md) owns the dataset: its record format, its consent, its review, its scrubbing of secrets and paths, and the person's corrections, which are the person's own (61a has no teacher rewriting a step). This item owns what is done with the accepted dataset: the trainers reading `tools`, the preference-trainer call above, the split, the suite cut from it, the eval and the regime. |
| Who owns the definition's JSON, and how a device's definitions reach a session? | `(consumed decision)` | [60a](../v0.2.4/one-tool-definition.md) owns the definition's JSON, and steps carry it unchanged. [60b](../v0.2.4/device-tools.md) owns how a device's definitions reach a session. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-07 | Specced from the Apple-platforms spike and brainstorm. The user's calls: (1) **The vision.** Apogee as a private, self-hosted harness that feels like Claude on every device the person owns. A strong machine with a finely tuned large model serves the person's desktops and phones, and Apogee mimics frontier harnesses (Claude Code, Codex, Gemini CLI, Ollama, vLLM) with local models. Tuning a model to Apogee's own protocol is the "finely tuned" half of that. (2) **No accounts.** Paid features are a someday: no customer accounts, billing or metering. Nothing here meters a run or names a customer. (3) **Where the work happens is C.** Server tools by default, and a connected desktop offers its own files and shell. Held-out steps keep their definitions' `runs`, so the eval measures whether a model calls the Mac's own tools when they are the ones offered, and so works on the Mac's files when asked to change a project on the Mac. (4) **Remote is an add-on**, never the only way Apogee runs. The CLI alone and the Mac app over pipes to a local CLI stay complete and the default. Hence the anti-goal (Constraints): the regime and the eval need no server, network or account. (5) **iPhone and iPad are clients of the person's server only**; the Mac and the CLI stay standalone. A session an iPhone or iPad drives runs on the server and is saved in the server's store, so the regime there trains on it as on any other. On a standalone Mac the store holds the Mac's own chats, and the regime trains on those. (6) **Placement.** The common tool definition lands before v0.2.5, and Apogee-tuned training after it. Track 61 sits in v0.2.6, the first version after the three Apple platforms are whole. | the user |
| 2026-11-07 | Gated on 61a. The regime trains on 61a's dataset and the held-out suite is cut from it, so without 61a there is nothing of the person's to train on or hold out. The bundled `apogee-protocol` suite and `train eval --backend` would be useful alone, but they ship with the regime, so the gate and the data arrive together. | the spec |
| 2026-11-18 | Moved here from v0.3.1, when the releases were merged; the id is unchanged. Earmarked for v0.2.6 (track 61). | the user |

## Out of scope

- Live multi-step episodes.
- A judge of any kind.
- A preference trainer over 61a's pairs.
- The cycle's adoption of the protocol gate.
- Training over HTTP, which stays a carve-out.
- The macOS app showing the `protocol` block, a later GUI item (the block is additive, so the app reads today's fields unchanged).
- Choosing or shipping a model. The student and the target are always the person's.

## Links

- **Blocked by:** [61a](sessions-as-training-data.md) · **Blocks:** —
- **Follows from:** [61a](sessions-as-training-data.md) (the dataset, and its consumed decision handing this item the trainers' `tools` gap); [GUI 9c](../../darwin/v0.2.5/three-apple-platforms-whole.md) (the Apple platforms whole before this item)
- **Relates to:** [60a](../v0.2.4/one-tool-definition.md) (the definitions a step offers), [60b](../v0.2.4/device-tools.md) and [59b](../v0.2.3/machine-mode-over-the-network.md) (why held-out sessions include a desktop's own files and shell), [59a](../v0.2.3/paired-devices.md) (a paired device cannot start a run or a promotion); 42a/42b, 53, 53c (precedents); [ADR 0001](../../../adrs/cli/layer-enforcement.md), [ADR 0002](../../../adrs/cli/mode-parity.md), [ADR 0006](../../../adrs/cli/backwards-compatibility.md); [the model families](../../../assistant/cli/DEVELOPER.md#on-real-weights-the-model-families)
