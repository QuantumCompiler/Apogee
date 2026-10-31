# CLAUDE.md

Project context for coding agents working on **the Apogee mod** — the Claude plugin. Read this before making changes. You arrived here from the [documentation router](../../CLAUDE.md); CLI work belongs in [`cli/`](../../cli/assistant/CLAUDE.md) and the desktop apps in [`gui/`](../../gui/assistant/CLAUDE.md). This file tells you **how to work here**; the sibling documents tell you **what** and **why**.

## Reading order & the docs system

`lib/documentation/mod/assistant/` is a docs-as-source-of-truth system — the same lifecycle as the CLI's and GUI's: discussed in chat → ROADMAP (and SPEC on shape changes) → a backlog document → built straight from it → MILESTONES, document deleted. The table of documents and their jobs is the CLI guide's, mirrored; the [backlog README](../backlog/README.md) carries this queue's format and index. **Repo-wide process is the CLI docs' to record** (the router's rule) — this file states the mod's own rules and deltas, never a second copy.

---

## What this is

The Apogee mod: a **TypeScript plugin running inside Claude** (the desktop app or Claude Code) on the user's own machine — Apogee's local models offered to Claude as tools, `/apogee` slash commands, a pane beside the chat showing backends, models and training state, and a status line. It drives the same `apogee` binary the GUI drives, reading its `--output-format json` documents and machine mode. See [SPEC.md](SPEC.md) for scope and the hard limits; nothing has shipped yet ([MILESTONES.md](MILESTONES.md) starts empty).

## Stack & environment

- **Language / platform:** TypeScript, Claude's plugin API (the user's call, 2026-10-21, with the mod spike): `.claude-plugin/plugin.json`, `hooks/hooks.json` → `hooks/register.tsx`, the `$` APIs (`$.process.run`, `$.tool.register`, `$.command.register`, `$.ui.*`, atoms).
- **Key dependency:** the `apogee` binary and its contracts — the JSON documents (28h), [machine-mode](../../cli/reference/machine-mode.md) for anything session-grade, and `apogee mcp serve` / `__mcp-tools` for MCP-served tools (the CLI's track 54).
- **Toolchain:** `tsc -p <folder>`, `claude plugin validate <folder>`, `claude plugin test <folder>`; developed locally with `claude --plugin-dir <folder>`. CI reality is recorded honestly in the pipeline item: runners gate on `tsc` (plus validate where the `claude` CLI installs headlessly); validate/test are dev-host gates.
- **Where it runs:** local Claude sessions on the machine where `apogee` and the models live — never a cloud session (the plugin runs commands on its host).

---

## ⚠ The mod is a client of the binary

**The rule.** Every capability reaches the mod by running the `apogee` binary — one-shot JSON reads, machine-mode children, or the MCP server's tools. The mod never opens or connects to a localhost port for Apogee, never reimplements a capability, never parses prose output when a document exists, and never carries secrets (keys stay in Apogee's own flows).

Why this is non-negotiable: it is the GUI's machine-mode-client invariant, third surface — "parity is the product" holds only while every front end drives the one core, and "local front-ends are pipes" is the recorded product shape.

- Reads are `--output-format json` documents, tolerated additively (unknown fields ignored, never fatal — the CLI's ADR 0006 consumed).
- Long runs never block a Claude turn: launched, toasted, polled from the documents (the mod's async law).
- A missing binary is a typed, stated condition with the install hint — the GUI's discovery ladder pattern.

Enforced by: the CLI's conformance suites on the contract side; mod-side, the layering check (ADR 0001's, landing with the scaffold) and review until the first build's gates exist.

---

## Codebase Map

Nothing is built yet. The planned layout ([ADR 0001](../adrs/layer-enforcement.md): four layers, dependencies down, in `src/<layer>/<package>/`):

| Path | Contents (planned) |
|------|----------|
| `lib/src/mod/.claude-plugin/`, `hooks/` | The plugin manifest and the register entry — the composition root. |
| `lib/src/mod/src/infrastructure/` | The process runner over `$.process.run`; binary discovery. |
| `lib/src/mod/src/data/` | The documents as types (tolerant decoding); the mod's own state atoms. |
| `lib/src/mod/src/business/` | The Apogee client: reads, supervised runs, the tool/command handlers' logic. |
| `lib/src/mod/src/presentation/` | The pane, status line, toasts, slash registration — the only layer that touches `$.ui`. |

## Working process

The CLI docs' lifecycle, mirrored — read [Working process](../../cli/assistant/CLAUDE.md#working-process) there. The mod's deltas: mod work lands in **this** tree's boards and queue; an item needing a CLI change (a new document, a served tool) gets a CLI-queue sibling, cross-gated by path.

**Development runs on the dev channel** (the user's call, 2026-10-31; the law is the CLI docs' [Development runs on the dev channel](../../cli/assistant/CLAUDE.md#development-runs-on-the-dev-channel)): the release `apogee` over `~/.apogee` is the user's production install, changed only by what a release ships. A development build of the mod drives `apogee-dev` over `~/.apogee-dev`, and a live pass names `apogee-dev`; the scaffold (1a) gives its binary discovery a way to say so.

## Implementing a Feature

- [ ] `tsc` clean and `claude plugin validate` passing; tests per the item's guardrails (hermetic: scripted process output, no live binary required).
- [ ] The client invariant holds (no ports, no reimplementation, tolerant reads); the four-layer law holds (ADR 0001).
- [ ] The affected docs updated in the same change (the CLI guide's Documentation and Status flow, this home's files).

## Code Style

- _TODO:_ accretes with the first build. Until then: match the surrounding code; `$`-API access stays in its layer (process in infrastructure, UI in presentation).

## Release and Install Infrastructure

Repo-wide and recorded in the CLI docs. The mod joins it application-by-application like the GUI did: its own `changed.sh` entry and pipeline when the scaffold lands (the queue's pipeline item); distribution (a marketplace entry, a plugin-dir install) is that item's recorded question.
