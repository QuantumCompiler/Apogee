# MOD-1c · The pane: Apogee beside the chat

| Field | Value |
|---|---|
| **Type** | ✨ Feature |
| **Project** | Claude plugin (mod) |
| **Status** | 🟢 Ready |
| **Fix version** | v0.2.1 |
| **Component** | The pane (`src/presentation/pane/`) |
| **Labels** | `pane` `read-back` `confirmed-actions` |
| **Epic** | Track 1 · the mod is born |
| **Blocked by** | — (1b shipped 2026-11-21, [Milestone A](../../../assistant/mod/MILESTONES.md#milestone-a--the-mod-is-born)) |
| **Blocks** | [2b](../v0.2.2/mod-graphs-and-knowledge.md) |
| **Reporter** | The mod spike (2026-10-21) |
| **Created** | 2026-10-21 |

---

## Summary

The visual half of the mod (the spike's feature 3): a pane beside the chat showing backends and their models, training, and the version ledger, with promote and rollback as buttons that confirm with the exact command before they run. Every datum comes from a measured JSON document, re-read after every action.

## Description

### User story

**As** a person using Apogee from the Claude desktop app,
**I want** a pane beside the chat that shows my backends and models, my training state and my version ledger, and lets me promote or roll back after seeing the exact command,
**so that** I can watch and steer Apogee without leaving the chat, and never run a destructive command I have not seen.

### Background

The pane is opened with `$.ui.open` and drawn by the render hook. It has three sections:

- **Backends and their models** — from `models list|status`.
- **Training** — from `train status`: the cycle, pipelines, running and recent runs.
- **The version ledger** — from `train versions`.

**Promote and rollback are buttons**, each confirming with the exact command shown before it runs: the house's destructive manners, the GUI's shown-batch pattern.

The pane re-reads after every action (read back, don't mirror) and polls the training view only while visible.

The adapters column (per backend, with switching) is the recorded follow-on. Its CLI side is ready now that track 53 has shipped (v0.2.0); it is noted, not gated.

### Evidence

1. **Every datum is a measured JSON document** (the mod spike, 2026-10-21): the documents the pane reads were measured live.

## Acceptance criteria

- [ ] **Given** the real documents, **when** the pane is open, **then** it shows backends/models, training state and the ledger from them, and re-reads them after actions.
- [ ] **Given** no binary or no backends, **when** the pane is open, **then** its empty and absent states are honest (the stated empty states).
- [ ] **Given** the pane is open, **when** I press promote or rollback, **then** the command runs only after its shown-command confirmation.
- [ ] **Given** the training view is polling, **when** the pane is hidden, **then** polling stops.

## Constraints

- **Layers.** [ADR 0001](../../../adrs/mod/layer-enforcement.md): render in Presentation; state and logic below.
- **Read back.** Documents are re-read after mutations.
- **Visibility-scoped polling.** Polls only while visible.
- **Absence honest.** No binary, no backends: the stated empty states.
- **Nothing invented.** A number the documents don't carry isn't shown.

## Technical approach

| File | Change |
|---|---|
| `src/presentation/pane/` | the Pane component, status atoms |
| `src/business/` | grown: the pane's reads and actions |
| `types/index.d.ts` | the atoms |

**Reference.** The mod spike (2026-10-21) — the documents measured live; the GUI's monitor/read-back patterns in miniature; track 53 (CLI) — the adapters follow-on, referenced.

## Testing

Hermetic.

- **The pane's view logic over scripted documents.** View-logic matrices: each section, empty states, the confirm-then-run flow, visibility-scoped polling.
- **The confirm-shows-the-command pin.** Promote and rollback show their exact command before running.
- **Visibility-scoped polling.** Polling runs only while the pane is visible.
- **`tsc`/validate clean.**

## Open questions

| Question | Status | Answer |
|---|---|---|
| How are the pane's layout and density decided? | `[default]` | At build. |
| How does the ledger section present the versions document? | `[default]` | It lists the versions document as-is. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-10-21 | The pane is reads plus two confirmed actions; anything session-grade (a chat in the pane) is explicitly out (the SPEC's non-goal). | the spec |
| 2026-11-21 | 1b shipped ([Milestone A](../../../assistant/mod/MILESTONES.md#milestone-a--the-mod-is-born)), and this item builds on its shape: every run of the binary goes through `src/infrastructure/runner/`, discovery's included (`runBinary` for a read, `superviseBinary` for a command that writes the store); `src/business/client/readDocument` reads a command's document tolerantly, `src/data/documents/` holds the decoders (`check`, `models list`, `models status`, `train status`; `train versions` is this item's to add); `src/business/commands/` plans promote and rollback with the CLI's argv (neither takes `--yes`, so the confirmation is the one gate) and runs each as a job from its start (`src/business/jobs/detachJob`: answered at once, the run begun from a timer outside the dispatch, since the API ends a spawn when the dispatch that started it is abandoned; its end toasted), while a completion keeps `launchJob`'s 30 s window; `src/presentation/commands/apogee.ts` asks with `$.ui.ask`, the exact command shown; the status line is read back after a command ends (`refreshStatusLineLater`), every read waiting for the one before it; `$.clock` is Infrastructure's noun (`src/infrastructure/timer/`), which visibility-scoped polling stands on; tests script the machine through `tests/support/machine.ts` (commands, answers, the mocked clock) with documents from `tests/support/recorded.ts` and `handmade.ts`. Flagged for veto with 1b's decisions | 1b (the run) |

## Out of scope

- Adapters UI (the track 53 follow-on)
- Chat surfaces (anything session-grade, such as a chat in the pane; the SPEC's non-goal)
- Any CLI change

## Links

- **Blocked by:** — (1b shipped 2026-11-21, [Milestone A](../../../assistant/mod/MILESTONES.md#milestone-a--the-mod-is-born)) · **Blocks:** [2b](../v0.2.2/mod-graphs-and-knowledge.md)
- **Follow-on:** the adapters column (per backend, with switching), its CLI side track 53 (shipped v0.2.0) — noted, not gated
- **Relates to:** the mod spike (2026-10-21); the GUI's monitor/read-back patterns and shown-batch pattern; [ADR 0001](../../../adrs/mod/layer-enforcement.md)
