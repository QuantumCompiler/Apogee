# CLI-61a · Apogee's own sessions as training data

| Field | Value |
|---|---|
| **Type** | ✨ Feature |
| **Project** | CLI |
| **Status** | 🟢 Ready |
| **Fix version** | v0.2.6 |
| **Component** | `datasets` (export, review) and the session record |
| **Labels** | `training-data` `sessions` `privacy` `secrets-scrub` |
| **Epic** | Track 61 · Apogee-tuned training |
| **Blocked by** | — |
| **Blocks** | [61b](apogee-tuning-regime.md) |
| **Reporter** | The Apple-platforms spike and brainstorm (2026-11-07), on the user's vision |
| **Created** | 2026-11-07 |

---

## Summary

A session reaches a dataset today only as plain question-and-answer text: tool calls, their results, permission outcomes and the person's corrections are lost, a `/private` chat is mined, nothing is scrubbed and nothing is reviewed. This item turns the person's own sessions into training data, opt-in and reviewed, for [61b](apogee-tuning-regime.md) to train on and measure: a session record kept as each turn runs, `apogee datasets export` into step records (SFT) and preference pairs, a scrub of secrets and paths, `apogee datasets review` before anything becomes a dataset, and private chats never.

## Description

### User story

**As** a person running Apogee on my own machines,
**I want** to turn chats I name into reviewed, scrubbed training data that keeps the tools offered, the calls made, what each returned, what I allowed and refused, and where I said "no, not that",
**so that** a model tuned for Apogee learns from what Apogee actually does, without any private chat or secret leaving my control.

### Background

**The vision (the user's, 2026-11-07).** Apogee is a private, self-hosted harness that feels like Claude on every device the person owns. A strong machine (for example 512 GB of RAM and a finely tuned large model) serves the person's own desktops and phones. A model tuned for Apogee has to learn from what Apogee actually does, and the best record of that is the person's own sessions: the tools offered, the calls made, what each call returned, what the person allowed and refused, and where the person said "no, not that". This item turns those sessions into training data, opt-in and reviewed, for [61b](apogee-tuning-regime.md) to train on and measure.

**What this item delivers.**
- **A session record** kept as each turn runs: the tool definitions offered, each step's permission mode and whether `ask_user` was offered, and each call's permission outcome and error flag.
- **`apogee datasets export`**: named sessions, or a named collection of them, become two kinds of record. A **step record** (SFT) is one per assistant step, with the conversation up to it and the tools it was offered. A **preference pair** pairs a step the person rejected with the step that followed their correction.
- **The scrub**: secrets and paths, before anything is written.
- **`apogee datasets review`**: an export is staged outside the datasets the trainers read and becomes a dataset only when the person accepts it.
- **Private chats never**: the shared miner skips them too, so `datasets create --from sessions` and the cycle stop taking them.

### Evidence

Re-measured 2026-11-07 at HEAD `4f92316d`: the source, and the branch binary (`apogee 0.2.0 (4f92316d)`) over a scratch `APOGEE_HOME` with the mock backend, a scripted `write_file` call and no model.

**W12 — a session reaches a dataset only as plain question-and-answer text.** The spike's wording, "nothing turns Apogee's sessions into a dataset", is corrected by measurement: `datasets create --from sessions` exists (`reference/training.md:236-243`), and the cycle's `sessions` source mines the same way (`reference/training.md:876-882`; `business/training/cycle.cpp:364`). Both go through one miner, `training::mine_sessions` (`business/training/datasets.cpp:269-324`). It keeps one user-and-assistant text exchange at a time and skips everything else. What that loses:

1. **Tool calls and their results are dropped.** A scripted `chat --tools` session whose model called `write_file` mined to one line: the request paired with the answer after it. Neither the call nor its result is in the dataset (`grep -c 'tool_calls\|denied'` over it: `0`). The miner's own comment says tool messages are skipped (`datasets.h:132-137`).
2. **A permission outcome is not data.** A session keeps a tool's result as message text only. `ChatMessage::from_tool_result` drops `ToolResult::is_error` (`data/contracts/types.cpp:263-270`; the flag at `types.h:131-136`), and `Session` has no field for an outcome (`data/logger/session.h:137-253`). A refusal shows only in its words, and the words are wrong in one case. With stdin a pipe there is no confirm function (`presentation/cli/permissions.cpp:317-320`), so nobody is asked, yet the saved result reads "Error: the user denied permission to run 'write_file'." (`business/agent/tool.cpp:249-257`; the probe's session file).
3. **A correction becomes an ordinary pair.** The probe's "no, that was wrong; leave it" and the answer after it became a line of their own. Nothing pairs a rejected answer with a better one. `dataset_shape` knows `chat`, `flat` and `eval` (`datasets.cpp:51-93`), and a `prompt`/`chosen`/`rejected` line read `shape: unknown` in `datasets info`. No trainer reads preference pairs: a grep for `dpo`, `preference`, `chosen` or `rejected` over `business/training/` and `assets/training/` finds the words only in other senses (`chosen` in comments and prose, `rejected` as a status in the `knowledge-capture` kit, `kits/knowledge-capture.yaml:80`), never as a field a dataset or trainer reads.
4. **A private chat is mined.** A chat marked `/private` is saved with `"private": true` (`session.h:183`). The probe's private chat gave both its exchanges to the dataset, "my secret thing" included (`2 example(s) from 1 session(s)`). `load_session_views` hands the miner the backend, the start time and the messages, never the flag (`presentation/operations/dataset_core.cpp:22-33`). A `chat --private` chat is never written (`session.h:189`), so it cannot be mined.
5. **Nothing scrubs.** A grep for `redact` and `scrub` across the source finds redacted thinking (`presentation/views/thinking_view.h`), `config show`'s masked `api_key` (`presentation/cli/config_cmd.cpp:121-130`) and the `redact-text` symphony, a model asked to redact a text (`data/contracts/assets_symphonies.cpp:197`). None of them touches a dataset. Once calls are mined, a key the model read or a path it wrote would go into a dataset as it stands. The probe planted `sk-ant-api03-PLANTED` and the scratch path in a call's arguments; it is missing from the dataset today only because the call is.
6. **Nothing is reviewed.** `datasets create` writes straight into `training/datasets/`, where `train run` reads.
7. **The trainers take one completion per line.** `train_peft.py` trains on the last assistant message of a chat line, with every message before it as prompt, and passes no `tools` to the chat template (`assets/training/train_peft.py:129-136`). So a whole tool-using session as one line would teach its final answer and none of its calls.

## Acceptance criteria

- [ ] **Given** a scratch home, **when** a scripted `apogee chat --tools` session calls `write_file`, **then** it saves a `record` naming the `write_file` call `unasked` on a pipe, and `no` when a scripted confirm refuses it.
- [ ] **Given** that chat, **when** I run `apogee datasets export mine --chat <that chat>`, **then** it stages step records with the call and its result in `messages`, and the definitions in `tools`; the refused step is not a completion, and a pair is staged from the `no` and the reply after the correction.
- [ ] **Given** the planted `sk-ant-api03-PLANTED` and the scratch home's path in the session, **when** it is exported, **then** they appear nowhere staged (`[secret]` and `~` in their place), and the review's summary counts both.
- [ ] **Given** no chat, filter or `--all`, **when** I run `apogee datasets export mine`, **then** it is refused, naming them.
- [ ] **Given** a `/private` chat, **when** I name it with `--chat <a /private chat>`, **then** it is refused.
- [ ] **Given** the probe's home, **when** I run `datasets create --from sessions`, **then** the dataset no longer contains "my secret thing".
- [ ] **Given** a staged export, **when** nothing has accepted it, **then** nothing is in `training/datasets/`; **when** I run `apogee datasets review mine --accept`, **then** `datasets info mine` reads `chat`, and the pairs' dataset reads `preference`.
- [ ] All of it runs with no `serve`, no pairing and no network. `reference/training.md` documents it, and the full suite is green.
- [ ] Optional, and said if skipped: on `apogee-dev`, a real `execute` session in a scratch project exports, reviews and is accepted, and `train run --trainer mock` reads the accepted dataset.

## Constraints

- **The anti-goal: nothing here makes standalone use need a server.** The CLI alone and the Mac app over pipes stay complete and stay the default (the user's call, 2026-11-07). The export reads the session store on the machine that holds it and writes under that machine's `training/`. A standalone install's chats export exactly as a server's do. Nothing reads a pairing, an account or the network, and no test starts `serve`. A session a phone or a Mac ran on the person's server ([59c](../v0.2.3/many-sessions-at-once.md)) is a chat file on the server and is exported there, by the same verb.
- **Opt-in by naming.** Nothing is exported unless the person names it: one or more chats, or a collection (a filter, or `--all`). A bare `datasets export <name>` is refused, naming the three ways. Recording the session's steps is not exporting. The record stays in the person's own `sessions/`, which is private (`0700`).
- **Private chats never.** A chat marked `/private` is skipped by every path that mines sessions: the export, `datasets create --from sessions` and the cycle's source. Naming one with `--chat` is refused, not skipped silently.
- **Reviewed before use.** An export lands in `training/staged/<name>/`, which no trainer, pipeline, regime or cycle lists. Only `datasets review <name> --accept` moves it into `training/datasets/`. That is the person's act, taken at the machine.
- **Secrets are unleakable by construction** (CLI CLAUDE.md → Secrets are unleakable by construction). The scrub runs before the first byte is staged.
  - Known secrets are found by a matcher built in `secrets/`. It holds digests of every key the one resolver could hand out, plus the admin token, and returns positions, never a key. `key_for` stays the one function that returns a key.
  - Known shapes (provider key prefixes, private-key blocks, bearer headers, `KEY=`/`TOKEN=` lines) are found by pattern.
  - Paths are rewritten: the home folder becomes `~`, the user name `<user>`, the host name `<host>`, and an ACP chat's `cwd` becomes `.`.
  - 60a's definitions have no slot for a command, an environment or a key, so the recorded definitions cannot carry one.
- **Never listens; serving is the server's.** This item opens nothing. Training control is CLI-only (`reference/training.md:981-989`), and the export and the review get no admin route here: a parity carve-out, stated in training.md.
- **Additive** ([ADR 0006](../../../adrs/cli/backwards-compatibility.md)).
  - The session record is new optional fields. An older file has none and reads as it does now. A session written before this item exports as plain exchanges only, never as step records, because its outcomes and offered tools were never kept.
  - `chats show`, resume and recall read sessions unchanged.
  - `dataset_shape` gains `preference`, so an existing dataset reads as before.
  - `datasets create --from sessions` keeps its line shape. Its one change is the private skip (and the scrub, below).
- **Parity** ([ADR 0002](../../../adrs/cli/mode-parity.md)). The outcome is recorded in the one gate and the one loop. Every surface that runs a turn (chat, the TUI, machine mode, `execute`, `acp`, `task`, a served session) saves the same record. One miner and one scrub serve the export, `create` and the cycle.
- **The four layers** ([ADR 0001](../../../adrs/cli/layer-enforcement.md)).
  - The export and the scrub are pure code in `business/training/`, inside its allow-list (`tests/scripts/cmake/layering.cmake:429`). They receive the sessions as views and the secret matcher as a closure, as models arrive there today.
  - The record's type is in `data/logger/`, and the matcher in `data/secrets/`.
  - `presentation/operations/dataset_core.cpp` builds both and hands them down.
- **UTF-8 where it enters** (CLI CLAUDE.md). Session text is already valid UTF-8 on disk. The scrub replaces whole code points and never splits one.
- **The dev channel** ([CLI CLAUDE.md → Development runs on the dev channel](../../../assistant/cli/CLAUDE.md#development-runs-on-the-dev-channel)). Live passes name `apogee-dev` over `~/.apogee-dev`. `~/.apogee`'s sessions are never exported, staged or rewritten by development.

## Technical approach

| File | Change |
|---|---|
| `data/logger/session.h/.cpp` | `Session` gains `record`, written only when it is non-empty, so a chat with no tool turns saves exactly as before (its fields below) |
| `business/agent/tool.h/.cpp` | the gate says which rung decided: `ToolCallReport` (`tool.h:78-85`) gains `permission`, set when a call leaves `pending` (its words below) |
| `presentation/cli/permissions.cpp` and the machine-mode confirm | say which answer was given (`Answer`, `permissions.cpp:23`); the denial text a model reads is unchanged |
| `business/agentloop/loop.h/.cpp` | the turn collects its steps and calls, and the session drivers write them with the turn's save |
| `business/training/session_export.h/.cpp` (new) | `SessionView` widens to carry the chat id, `private`, `cwd`, the system prompt and the record; `export_sessions(views, selection, scrub)` returns step records, preference pairs, and the counts of what it left out and why |
| `business/training/scrub.h/.cpp` (new) | `scrub(text, finder, paths)` returns the text and a count per kind (`secret`, `pattern`, `path`, `withheld`) |
| `business/training/datasets.h/.cpp` | `mine_sessions` skips private views, and `create` and the cycle get the scrub; `dataset_shape` gains `preference` |
| `data/secrets/resolve.h/.cpp` | `known_secrets(config, env)` returns a finder that holds digests and gives back positions |
| `presentation/operations/dataset_core.h/.cpp` | views with the record, the finder, and the staged store (`training/staged/<name>/`, created `0700` by itself) |
| `presentation/cli/datasets.cpp` | the `datasets export` and `datasets review` verbs (syntax below); `datasets list` names staged exports apart from datasets; chat names complete, store-aware ([ADR 0007](../../../adrs/cli/tab-completion.md)) |
| `assets/training/train_mlx.py` | lays out mlx-lm's data directory itself (`train_mlx.py:22`); it drops the record's `apogee` key there if mlx-lm refuses unknown keys |

**The record's fields** (`Session.record`):
- `definitions`: 60a's `ToolDefinition` JSON, keyed by `definition_hash` (`business/agent/tool.h:175`), each kept once.
- `steps`: one per assistant message the loop produced. Each holds the message index, the hashes offered, the permission mode, and whether `ask_user` was offered.
- `calls`: keyed by call id, each with its `permission` word and `error`.

**The `permission` words** on `ToolCallReport`:
- `ungated`: a tool that never asks.
- `allowed`: by config, mode, `--allow` or a session approval.
- `yes`, `session` or `always`: the person was asked and said so.
- `no`: the person was asked and refused.
- `denied`: by config, mode or `--deny`.
- `unasked`: nobody could be asked, as with a pipe or no `capabilities.ask`.

**The verbs** in `presentation/cli/datasets.cpp`:
- `datasets export <name> (--chat <id|name>… | --backend/--since/--until/--task … | --all) [--shape steps|pairs|both]`
- `datasets review <name> [--show N] [--drop N,…] [--pair N] [--accept] [--discard]`

**Reference.**
- In-house precedents:
  - `datasets create --from sessions` and its miner (W12): this item widens it.
  - The cycle's `log_consent` rule (`reference/training.md:878-881`): consent stated where the data is taken.
  - 26l's `/private` and 52d's `chat --private`.
  - The tool-calling kit's declarations, held to the registry by `tests/business/training/role_kits_test.cpp`.
  - 56a's `ToolCallReport`: the structured twin of the status line, the natural carrier for the outcome.
- Siblings:
  - [60a](../v0.2.4/one-tool-definition.md) supplies the definitions the record keeps. Its scope hands this item the training kit's copied declarations and any training use of the permission class.
  - [60b](../v0.2.4/device-tools.md) is why a step's definitions may say `runs: device`: a call into the Mac's own files, recorded on the server.
  - [61b](apogee-tuning-regime.md) trains on the accepted dataset and cuts its held-out suite from it, by session.
  - The Apple platforms land whole in [GUI 9c](../../darwin/v0.2.5/three-apple-platforms-whole.md) first, so the sessions this item exports include the phones'.
- External prior art: none checked for this document. The pair's field names (`prompt`, `chosen`, `rejected`) are Apogee's choice. Whether a trainer reads them as they are is 61b's to settle.

**Docs when it ships:** `reference/training.md` gains a "Sessions as training data" section: the record, the two shapes, the scrub, staging and review, the private rule, and the carve-out under "Over HTTP". The `datasets create` paragraph says private chats are skipped. `assistant/DEVELOPER.md`'s session-file paragraph (`:581`) names the `record` fields.

## Testing

Hermetic: a temp `APOGEE_HOME`, the mock backend scripting `tool_calls` (`data/backends/mock.h:67-72`), no model, no network.

- **`tests/business/training/session_export_test.cpp`** — one step record per assistant step; a `no` step is never a completion and makes a pair with the reply to the correction that follows it; `denied` and `unasked` steps are context only; a session with no record gives plain exchanges; a private view is skipped, and named, it is refused. This is the export's table test: it pins each step kind (a completion, context only, or half of a pair).
- **`tests/business/training/scrub_test.cpp`** — each pattern, the path table, code points kept whole, and the counts. The scrub's table pins every pattern and path rule, and the code-point case.
- **`tests/business/training/datasets_test.cpp` and `cycle_test.cpp`** — the private skip, and the `preference` shape.
- **`tests/data/logger/session_test.cpp`** — the record round trip, an older file reading with none, and a no-tool chat saving byte for byte as before. The session tests pin the additive record.
- **`tests/business/agent/tool_test.cpp`** — the permission word for every rung, including `unasked` with no confirm function; the denial text is unchanged. The gate test pins `unasked` as distinct from `no`, with the model's denial text unchanged.
- **`tests/data/secrets/resolve_test.cpp`** — the finder finds a planted key in every rung, and its type has no accessor that returns one.
- **`tests/presentation/cli/datasets_test.cpp`** — the verbs, the refusals, and staging versus acceptance.
- **`tests/presentation/cli/leak_test.cpp`** — the distinctive key placed in a session's tool result never reaches a staged file, an accepted dataset, stdout or a log. The leak test pins that a key planted in every resolver rung and in a tool result reaches no staged file, dataset, output or log.
- **The private case** runs in all three paths: export, `create` and the cycle.
- **`harness.layering`** stays green: `training/` takes the finder as a closure and includes nothing of `secrets/`.

## Open questions

| Question | Status | Answer |
|---|---|---|
| What is a collection? | `[default]` | A filter over the store: `--backend`, `--since`, `--until`, `--task` (a task's chats), or `--all` for every written, non-private chat. No named, saved collection is added. A filter typed again is the same collection, and the staged export's `review.json` records the filter it was made with. |
| What does a step record hold? | `[default]` | One line per assistant step. `messages` holds the session's own system prompt (if set) and the conversation through that step. `tools` holds the IR projection of the definitions offered at it. `apogee` holds `{chat, turn, step, permission_mode, ask_offered, runs}`. This fits `train_peft.py` as it reads lines today (`:129-136`), with the last message as the completion and `mask_prompt` as it is. |
| When is a step never a completion? | `[default]` | When the person said `no` to its call, when its call was `denied` or `unasked`, or when its call returned an error. Each stays in the context of the steps after it, so the model learns from the denial text it saw. |
| What is a preference pair, and where do pairs come from? | `[default]` | A pair has `prompt` (the conversation before the rejected step), `rejected` (that step) and `chosen` (the next assistant step after the person's following turn, when that step was not itself refused). Pairs come from two places only: the person's `no`, from the record, and an answer the person marks in review with `--pair N`. Nothing is inferred from the words of a reply. |
| How is a call's result kept? | `[default]` | As the model saw it, scrubbed. A read of a file named like a credential (`.env`, `*.pem`, `id_*`, `credentials*`, anything under `secrets/`) has its whole result withheld as `[withheld: <name>]`. A line over 256 KB is left out and counted. |
| What does the scrub replace with, and what of a path it cannot rewrite? | `[default]` | The replacements are `[secret]`, `~`, `<user>`, `<host>` and `.`. An absolute path the table cannot rewrite is kept and listed in the review's summary, never silently dropped. |
| How does the review run, and what does `--accept` write? | `[default]` | On a terminal it steps through each record with the scrub's marks shown, answering keep, drop, pair or quit (saved). Off a terminal, the flags do the same. `--accept` writes `training/datasets/<name>.jsonl` and `<name>.from-sessions.json` (the chats, the filter, the counts, the scrub's counts, the binary's version and the time), and then removes the staged folder. |
| Do `datasets create --from sessions` and the cycle's source change shape or gain a review? | `[default]` | They keep their plain exchanges with no review. `create` is the person's explicit act, and the cycle's `log_consent` is its documented consent. Both gain the private skip and the scrub, and the richer shapes reach the cycle only as accepted exports dropped in its `directory` queue. |
| Which chats keep the record? | `[default]` | Every written chat, whether or not it is ever exported. It is local and private, and a chat cannot be recorded after the fact. `/private` chats are recorded too, and are never exported. |
| What happens to the tool-calling kit's copied declarations? | `[default]` | The kit keeps them as text, since a kit is a shipped asset and the declarations are what its model is shown. `role_kits_test.cpp` compares them with 60a's definitions' IR projection, not the registry's structs. Nothing in the kit is regenerated. |
| Who writes corrections? | `[default]` | The person; corrections are the person's own. A teacher model rewriting a rejected step into a chosen one is not done here. |
| What is done with the accepted dataset (the trainers reading `tools`, a preference trainer, the held-out split by session, the protocol eval and the regime)? | `(consumed decision)` | [61b](apogee-tuning-regime.md) owns it. |
| How does a device's definitions reach a session? | `(consumed decision)` | [60b](../v0.2.4/device-tools.md) owns it. This item records whatever definitions the loop was offered. |
| Which commands may a paired device run? | `(consumed decision)` | [59d](../v0.2.3/reads-and-commands-over-the-network.md) owns it. This item adds no network door, and neither `export` nor `review` is offered over one here. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-07 | Specced from the Apple-platforms spike and brainstorm; the user's calls follow | — |
| 2026-11-07 | **The vision.** Apogee as a private, self-hosted harness that feels like Claude on every device the person owns, mimicking frontier harnesses (Claude Code, Codex, Gemini CLI, Ollama, vLLM) with local models. Training on the person's own sessions is how the tuned model learns Apogee rather than a generic chat. | the user |
| 2026-11-07 | **No accounts.** Paid features are a someday: no customer accounts, billing or metering. The data is the person's, exported on the person's machine, and named for no one else. | the user |
| 2026-11-07 | **Where the work happens is C.** Server tools by default, and a connected desktop offers its own files and shell. A step keeps its definitions' `runs`, so a call into the Mac's files is recorded as one. | the user |
| 2026-11-07 | **Remote is an add-on**, never the only way Apogee runs. Hence the anti-goal: the export works the same on a standalone install. | the user |
| 2026-11-07 | **iPhone and iPad are clients of the person's server only**; the Mac and the CLI stay standalone. So a phone's or a tablet's sessions are chat files on the server and are exported there, and a Mac's are exported where they ran: on the Mac when it ran alone, on the server when it pointed at one. No device sends its sessions anywhere for this item. | the user |
| 2026-11-07 | **Placement.** The common tool definition lands before v0.2.5, and Apogee-tuned training after it. Track 61 sits in v0.2.6, the first version after the three Apple platforms are whole. | the user |
| 2026-11-07 | The item's terms, from the plan: opt-in, per session or collection; tool calls and their results, permission outcomes and the person's corrections; the train track's shapes (SFT, and preference pairs); private chats never; secrets and paths scrubbed; reviewed before use. | the spec |
| 2026-11-07 | Ungated and first in track 61; 61b is gated on it. It builds on 60a's `ToolDefinition`, which is earmarked for v0.2.4 and so ships first by release order. If 60a has not shipped when this is taken, the record keeps the IR projection the registry gives today (`definitions()`: name, description, schema), and the classes follow when 60a lands. | the spec |
| 2026-11-07 | W12 is restated from measurement. The miner exists, and the wall is everything but text pairs: calls, outcomes, corrections, the private flag, the scrub and the review. | the spec |
| 2026-11-07 | The record is kept from when this item ships, not reconstructed. A denial's words are not a reliable outcome (W12.2), so an older session's tool turns are never guessed into step records or pairs. | the spec |
| 2026-11-18 | Moved here from v0.3.1, when the releases were merged; the id is unchanged. | the user |

## Out of scope

- Training on the new shapes: rendering `tools` in the trainers, and any preference trainer (61b).
- An eval of any kind (61b).
- Reconstructing records for sessions written before this item.
- Inferring a correction from the words of a reply.
- Teacher-written corrections.
- An admin route or network door for export or review.
- A review screen in the macOS, iPad or iPhone app, a later GUI item that is not queued.
- Changing the cycle's consent or its plain-exchange source beyond the private skip and the scrub.

## Links

- **Blocked by:** — · **Blocks:** [61b](apogee-tuning-regime.md) (🔒 61a)
- **Relates to:** [60a](../v0.2.4/one-tool-definition.md) (the definitions the record keeps), [60b](../v0.2.4/device-tools.md) (device definitions, `runs: device`), [59c](../v0.2.3/many-sessions-at-once.md) (served sessions), [59d](../v0.2.3/reads-and-commands-over-the-network.md) (paired-device commands), [GUI 9c](../../darwin/v0.2.5/three-apple-platforms-whole.md) (the Apple platforms whole), 56a (`ToolCallReport`), 26l (`/private`), 52d (`chat --private`), [ADR 0001](../../../adrs/cli/layer-enforcement.md), [ADR 0002](../../../adrs/cli/mode-parity.md), [ADR 0006](../../../adrs/cli/backwards-compatibility.md), [ADR 0007](../../../adrs/cli/tab-completion.md), [CLI CLAUDE.md → Development runs on the dev channel](../../../assistant/cli/CLAUDE.md#development-runs-on-the-dev-channel)
- **Follows from:** the Apple-platforms spike and brainstorm (2026-11-07), tracks 59, 60 and 61
