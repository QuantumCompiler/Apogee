# MOD-1b · Tools, slash commands and the status line

| Field | Value |
|---|---|
| **Type** | ✨ Feature |
| **Project** | Claude plugin (mod) |
| **Status** | 🟢 Ready |
| **Fix version** | v0.2.1 |
| **Component** | The mod's working surfaces: tools, the `/apogee` set, the status line (`src/business/`, `src/presentation/`) |
| **Labels** | `mcp-tools` `slash-commands` `status-line` `async` |
| **Epic** | Track 1 · the mod is born |
| **Blocked by** | — |
| **Blocks** | [1c](mod-pane.md) |
| **Reporter** | The mod spike (2026-10-21) |
| **Created** | 2026-10-21 |

---

## Summary

The mod's working surfaces, all over contracts the spike measured live (2026-10-21): the tools (Apogee's MCP server registered into the session, plus `apogee_complete`), the `/apogee` slash set, and the status line. Anything long launches, toasts and polls; a turn is never blocked.

## Description

### User story

**As** a person using Apogee from the Claude desktop app,
**I want** Claude to call Apogee's read tools and a local completion, a `/apogee` command set that runs the real commands, and a status line showing my backend, promoted version and run state,
**so that** I can use and watch Apogee from the chat without a turn ever blocking and without a destructive command running unconfirmed.

### Background

The item delivers three surfaces:

- **The tools.** Apogee's MCP server registered into the session: the 17 read-only tools answer today, and the registration JSON is what `mcp presets` prints. Beside them, **`apogee_complete`**, offered via `$.tool.register`, running `apogee complete -m <backend> "<prompt>"` one-shot. That is the CLI path now, switching to 54b's served tool now that the CLI's opt-in callable set has shipped (2026-11-01: `complete` on the server, offered only where the person lists it under a local backend's `mcp_serve:`). 54b is cross-referenced, never gated.
- **The `/apogee` slash set** — check, models, train status, promote, rollback — each the real command with its JSON document rendered; the destructive two (promote, rollback) confirm before `--yes`.
- **The status line** — the active backend, the promoted version and the run state, from `models status` + `train status`, polled gently, with staleness said.

**The async law binds everything long** (the spike's W4): a training run or a big completion launches, toasts, and polls — a turn is never blocked.

### Evidence

1. **The contracts are measured** (the mod spike, 2026-10-21, live): the server handshake, the measured documents, and the one-shot complete.
2. **W4 — the async law** (the mod spike, 2026-10-21): long work (a training run, a big completion) must launch, toast and poll rather than block a turn.
3. **The served path has shipped** (2026-11-01): the CLI's 54a ([shipped 2026-11-01](../../../assistant/cli/MILESTONES.md#milestone-w--the-mcp-client): `apogee mcp serve`, the status reads served) and 54b ([shipped 2026-11-01](../../../assistant/cli/MILESTONES.md#milestone-w--the-mcp-client): `complete` served opt-in per backend, local and unmetered only, at most 1024 tokens a call; [tools.md](../../../reference/cli/tools.md#calling-a-local-model-complete)).

## Acceptance criteria

- [ ] **Given** a configured backend, **when** Claude calls `apogee_complete` and the registered read tools, **then** each call succeeds and its results return within the turn or as the async pattern.
- [ ] **Given** the `/apogee` set, **when** a command runs, **then** it runs the real command with its document rendered, and the destructive two (promote, rollback) run only after confirmation.
- [ ] **Given** the mod is running, **when** the backend, promoted version or run state changes or the reads go stale, **then** the status line tracks them and says staleness.

## Constraints

- **The ⚠ client invariant.** Documents in, commands out, tolerant reads.
- **Layers.** Handler logic in Business, registration in Presentation ([ADR 0001](../../../adrs/mod/layer-enforcement.md)).
- **No `--yes` without the confirmation.**
- **Completions bounded.** A max-tokens default, said in the tool's description.
- **Honest about spend.** The tool descriptions say it: local and unmetered, or not offered.
- **Hermetic tests** over scripted process output.

## Technical approach

| File | Change |
|---|---|
| `src/business/` | the client: reads, the complete runner, poll logic |
| `src/presentation/` | tool/command/status registration |
| `src/data/` | the document types |

`apogee_complete` ships on the CLI path first (`apogee complete -m <backend> "<prompt>"`, one-shot, via `$.tool.register`); 54b's served tool replaces the plumbing, not the surface.

**Reference.** The mod spike (2026-10-21) — the server handshake, the measured documents, the one-shot complete; the CLI's 54a ([shipped 2026-11-01](../../../assistant/cli/MILESTONES.md#milestone-w--the-mcp-client): `apogee mcp serve`, the status reads served) and 54b ([shipped 2026-11-01](../../../assistant/cli/MILESTONES.md#milestone-w--the-mcp-client): `complete` served opt-in per backend, local and unmetered only, at most 1024 tokens a call; [tools.md](../../../reference/cli/tools.md#calling-a-local-model-complete)) siblings — referenced with fallbacks, since a person's config may list no backend; the GUI's jobs/read-back disciplines mirrored in miniature.

## Testing

Hermetic, over scripted process output: no live binary required by any test.

- **Handler matrices** over scripted documents.
- **Confirm pins.** The confirm gates: no `--yes` without the confirmation.
- **The async matrix.** The async pattern (launch → toast → poll) on a scripted clock.
- **Argv pins** per command.
- **`tsc`/validate clean.**

## Open questions

| Question | Status | Answer |
|---|---|---|
| What is the slash set's exact roster and output rendering? | `[default]` | Decided at build per the prototype of least surprise (the command's own document, formatted). |
| What poll cadence? | `[default]` | Conservative. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-10-21 | `apogee_complete` ships on the CLI path first; 54b's served tool replaces the plumbing, not the surface. | the spec |
| 2026-11-01 | The CLI's 54a and 54b shipped: `complete` on the server, offered only where the person lists it under a local backend's `mcp_serve:`; `apogee_complete` switches to 54b's served tool — cross-referenced, never gated. | — |
| 2026-11-21 | 1a shipped ([Milestone A](../../../assistant/mod/MILESTONES.md#milestone-a--the-mod-is-born)), and this item builds on its shape: the found binary is in `$.state` (`apogee.binary`: path, place, version, reported channel and the places passed over), kept by `src/business/binary/findBinary` at `session.start`; every `on(...)` lives in `hooks/register.tsx`, the only code file of `hooks/` (a handler it grows goes into a layer's package, never a second file beside it, and `types/` holds `.d.ts` files alone); each `$` noun is held to its layer by the layering check (`command` and `tool` registration are Presentation's, `process` Infrastructure's); a function taking `$` from another file is a `function` declaration (the 2.1.326 engine's rule); tests script the machine through `tests/support/machine.ts`. Flagged for veto with 1a's decisions | 1a (the run) |

## Out of scope

- The pane (1c)
- RAG beyond the server's `search_documents`
- Any CLI change

## Links

- **Blocked by:** — (1a shipped 2026-11-21, [Milestone A](../../../assistant/mod/MILESTONES.md#milestone-a--the-mod-is-born)) · **Blocks:** [`mod-pane.md`](mod-pane.md) (1c)
- **Relates to:** the mod spike (2026-10-21, W4); the CLI's 54a and 54b ([Milestone W — the MCP client](../../../assistant/cli/MILESTONES.md#milestone-w--the-mcp-client)), referenced with fallbacks, never gated; [tools.md](../../../reference/cli/tools.md#calling-a-local-model-complete); the GUI's jobs/read-back disciplines; [ADR 0001](../../../adrs/mod/layer-enforcement.md)
