# CLAUDE.md

Project context for coding agents working on **the Apogee mod** — the Claude plugin. Read this before making changes. You arrived here from the [documentation router](../../CLAUDE.md); CLI work belongs in [`cli/`](../cli/CLAUDE.md) and the desktop apps in [`gui/`](../darwin/CLAUDE.md). This file tells you **how to work here**; the sibling documents tell you **what** and **why**.

## Reading order & the docs system

`lib/documentation/assistant/mod/` is a docs-as-source-of-truth system — the same lifecycle as the CLI's and GUI's: discussed in chat → ROADMAP (and SPEC on shape changes) → a backlog document, written as a ticket (key `MOD`) → built straight from it → MILESTONES, document deleted. The table of documents and their jobs is the CLI guide's, mirrored; the [backlog README](../../backlog/mod/README.md) carries this queue's format (the Apple apps' [ticket format](../../backlog/darwin/README.md#document-format)) and index. **Repo-wide process is the CLI docs' to record** (the router's rule) — this file states the mod's own rules and deltas, never a second copy.

---

## What this is

The Apogee mod: a **TypeScript plugin running inside Claude** (the desktop app or Claude Code) on the user's own machine — Apogee's local models offered to Claude as tools, `/apogee` slash commands, a pane beside the chat showing backends, models and training state, and a status line. It drives the same `apogee` binary the GUI drives, reading its `--output-format json` documents and machine mode. See [SPEC.md](SPEC.md) for scope and the hard limits. The scaffold has shipped ([MILESTONES.md](MILESTONES.md) → Milestone A): the plugin, its four layers under the layering check, and binary discovery with the not-installed guard. So have its working surfaces (1b): Apogee's served tools and `apogee_complete` for Claude, the `/apogee` set and the status line. The pane is track 1's next item.

## Stack & environment

- **Language / platform:** TypeScript, Claude's plugin API (the user's call, 2026-10-21, with the mod spike): `.claude-plugin/plugin.json`, `hooks/hooks.json` → `hooks/register.tsx`, the `$` APIs (`$.process.run`, `$.tool.register`, `$.command.register`, `$.ui.*`, atoms).
- **Key dependency:** the `apogee` binary and its contracts — the JSON documents (28h), [machine-mode](../../reference/cli/machine-mode.md) for anything session-grade, and `apogee mcp serve` / `__mcp-tools` for MCP-served tools (the CLI's track 54).
- **Toolchain:** `npm run check` in `lib/src/mod/` (DEVELOPER.md → Build & test): `tsc -p .` against the vendored 2.1.326 API declarations, the layering check, the node tests, `claude plugin validate --strict .` and `claude plugin test .`; developed locally with `claude --plugin-dir lib/src/mod`. **The engine floor is Claude Code 2.1.326** (plugin.json's `engines`): an older CLI refuses the layered module (2.1.289 measured 2026-11-21), so the two `claude` gates name a 2.1.326-or-newer binary. TypeScript 6.0.3 is the one npm dependency. CI reality is recorded honestly in the pipeline item: runners gate on `tsc` (plus validate where the `claude` CLI installs headlessly); validate/test are dev-host gates.
- **Where it runs:** local Claude sessions on the machine where `apogee` and the models live — never a cloud session (the plugin runs commands on its host).

---

## ⚠ The mod is a client of the binary

**The rule.** Every capability reaches the mod by running the `apogee` binary — one-shot JSON reads, machine-mode children, or the MCP server's tools. The mod never opens or connects to a localhost port for Apogee, never reimplements a capability, never parses prose output when a document exists, and never carries secrets (keys stay in Apogee's own flows).

Why this is non-negotiable: it is the GUI's machine-mode-client invariant, third surface — "parity is the product" holds only while every front end drives the one core, and "local front-ends are pipes" is the recorded product shape.

- Reads are `--output-format json` documents, tolerated additively (unknown fields ignored, never fatal — the CLI's ADR 0006 consumed).
- Long runs never block a Claude turn: launched, toasted, polled from the documents (the mod's async law).
- A missing binary is a typed, stated condition with the install hint — the GUI's discovery ladder pattern.

Enforced by: the CLI's conformance suites on the contract side; mod-side, the layering check (ADR 0001's, `npm run layering`, since 1a): `$.http` is refused in every layer, so nothing of the mod opens a port, and only Infrastructure reaches `$.process`. Review holds the rest: that Infrastructure reaches it through its one runner (since 1b), tolerant reads, no prose parsed where a document exists.

---

## Codebase Map

The plugin at `lib/src/mod/` ([ADR 0001](../../adrs/mod/layer-enforcement.md): four layers, dependencies down, each package at `src/<layer>/<package>/`, its tests at `tests/<layer>/<package>/`). As built by 1a; the planned growth is in the last column.

| Path | Contents | Planned |
|------|----------|---------|
| `.claude-plugin/plugin.json` | The manifest: `name` `apogee`, version, `engines` (`claude-code >=2.1.326`), `types`, and the `binary` and `channel` options. | |
| `hooks/hooks.json`, `hooks/register.tsx` | The hooks module, the composition root and the folder's one code file: reads the options and wires `session.start`, `command.run` (`/apogee`) and `tool.run` (`mcp__apogee__*`) to Presentation, holding nothing itself. | Every `on(...)` registration lives here. |
| `types/index.d.ts` | The contract, declarations only: the `$.state` values (`apogee.binary`, `apogee.tools`, `apogee.jobs`, `apogee.status`) and their types, the floor every layer takes its types from. | |
| `src/infrastructure/discovery/` | Binary discovery: the override, then the channel's places in `~/.local/bin`, each verified by `version`; the install hints. | |
| `src/infrastructure/runner/` | The process runner, the one door to the binary: `runBinary` (`$.process.run`, within its time, stdin written and closed) and `superviseBinary` (`$.process.spawn`, read to its end, no time of its own). | |
| `src/infrastructure/timer/` | `$.clock` wrapped: `now`, `after`, `every`. | |
| `src/data/documents/` | The CLI's documents as tolerant types: `check`, `models list`, `models status`, `train status`, `apogee mcp serve`'s frames (the handshake, `tools/list`, `tools/call`), and `config get`'s one value, its bare line (no document holds one). | `train versions` for the pane. |
| `src/data/state/` | The `$.state` atoms: the binary found, the tools offered, the jobs, the status line's reading. | The pane's atoms. |
| `src/business/binary/` | `findBinary`: discovery's answer kept, and the absence said once a session. | |
| `src/business/client/` | The Apogee client: the session's binary, and `readDocument`, a command's document read through the runner. | The pane's reads. |
| `src/business/commands/` | The `/apogee` set: the plan (each word's real argv, promote and rollback confirmable), its reads, and the confirmed commands run supervised. | |
| `src/business/served/` | Apogee's MCP server, one exchange a run: the listing and each call. | |
| `src/business/surfaces/` | What the session is offered at its start: the served tools, and `apogee_complete`'s offer or why not; then what the engine took of it, kept. | |
| `src/business/complete/` | `apogee_complete`'s decisions: the local, unmetered backends that chat, each backend's bound, the spend checks (no active suite, a utility model that bills nothing), the one-shot's argv, the served path. | |
| `src/business/jobs/` | The async law: a completion's 30 s window, then a job; a confirmed command a job from its start, begun from a timer; each kept for its poll. | |
| `src/business/status/` | The status line's reads: once at the start, then once a minute, and after a confirmed command. | |
| `src/presentation/notices/` | The not-installed toast. | |
| `src/presentation/session/` | The session start, in order: the binary found, `/apogee` registered, the tools registered, the status line started. | |
| `src/presentation/commands/` | `/apogee`: registered, its confirmation asked (`$.ui.ask`), its documents formatted. | |
| `src/presentation/tools/` | The tools Claude is given: the served tools, `apogee_complete` and `apogee_job`, registered and answered. | |
| `src/presentation/status/` | The status line's words, staleness said. | The pane (`src/presentation/pane/`, 1c). |
| `tests/<layer>/<package>/`, `tests/support/` | `claude plugin test`'s hermetic tests over the scripted machine (`support/machine.ts`); `support/apogee.ts` drives `/apogee` and the tools, `support/recorded.ts` holds the recorded documents, `support/handmade.ts` the hand-made ones. | |
| `tests/architecture/layering/` | The layering check (node, the TypeScript compiler API) and its planted violations. | |
| `tests/pipeline/changed.test.mjs` | `changed.sh mod`'s proof over throwaway commits in a shared clone. | |
| `vendor/claude-code/index.d.ts` | The 2.1.326 API declarations, vendored unmodified for `tsc`. | Replaced whole when the engine floor moves. |
| `package.json`, `package-lock.json`, `tsconfig.json` | The checks' toolchain: TypeScript 6.0.3 and the npm scripts. | |

## Working process

The CLI docs' lifecycle, mirrored — read [Working process](../cli/CLAUDE.md#working-process) there. The mod's deltas: mod work lands in **this** tree's boards and queue; an item needing a CLI change (a new document, a served tool) gets a CLI-queue sibling, cross-gated by path, the waiting row's "Waits on" on the [backlog overview](../../backlog/README.md#keeping-this-page-true) naming it (`CLI 58a`), or the tie listed under "Ties to later releases" when that row is in a later release.

**Superpowers** works inside the lifecycle as the CLI docs map it ([Superpowers fits the docs-first lifecycle](../cli/CLAUDE.md#superpowers-fits-the-docs-first-lifecycle), 2026-11-07).

**Development runs on the dev channel** (the user's call, 2026-10-31; the law is the CLI docs' [Development runs on the dev channel](../cli/CLAUDE.md#development-runs-on-the-dev-channel)): the release `apogee` over `~/.apogee` is the user's production install, changed only by what a release ships. A development build of the mod drives `apogee-dev` over `~/.apogee-dev`, and a live pass names `apogee-dev`. Since 1a, discovery says so through the plugin's `channel` option: a development load (`--plugin-dir`, kept as `apogee@inline`) sets it to `dev`, and then discovery looks at the `binary` override and `apogee-dev` alone, never the release binary ([DEVELOPER.md → A development load](DEVELOPER.md#a-development-load-drives-apogee-dev)). Never set either option for an installed `apogee`: those are the production install's.

## Implementing a Feature

- [ ] `npm run check` passing in `lib/src/mod/` with a 2.1.326-or-newer `claude` (`tsc`, the layering check, the node tests, `plugin validate --strict`, `plugin test`); tests per the item's Testing section (hermetic: scripted process output over `tests/support/machine.ts`, no live binary required).
- [ ] The client invariant holds (no ports, no reimplementation, tolerant reads); the four-layer law holds (ADR 0001).
- [ ] The affected docs updated in the same change (the CLI guide's Documentation and Status flow, this home's files).

## Code Style

The first rules, earned by 1a (2026-11-21); more accrete with each item.

- **`$` nouns stay in their layer**, as the layering check holds them: `process`, `fs`, `env` and `clock` (since 1b) in Infrastructure; `state`, `store` and the atom library in Data; `ui`, `command`, `tool` in Presentation; `http` nowhere; the composition root and the contract none. A noun the table does not name yet gets its row, in the check and the ADR's Landed note, from the item that first uses it.
- **A function that takes `$` from another file is a `function` declaration** (`export async function locate($: EngineInterface, …)`), never an arrow bound to a `const`: validate, and the engine at load, follow `$` past an import only into a declaration. Spell every call `$.noun.event(…)` and hand `$` itself on: validate refuses `$` destructured, bound to another name or spread (measured on 2.1.326, 2026-11-21).
- **Every `on(...)` is in `hooks/register.tsx`**, which reads the options once and hands `$` down; the layers never register hooks.
- **Code lives at `src/<layer>/<package>/`, in TypeScript.** `hooks/` holds `hooks/register.tsx` alone, and `types/` holds `.d.ts` files alone, importing nothing of the mod but each other; the layering check refuses code anywhere else by name, and a JavaScript file in a layer too, since the engine would load what the check cannot read.
- **Imports between the mod's files are relative and extensionless** (`'../../data/state/binary'`); types come from `'claude-code'` and the contract (`'../../../types'`), type-only.
- **A `$.state` value is an atom with a `shape` tag**, declared in `types/index.d.ts`; bump the tag when its fields change.
- **The state's types read as the API's do:** booleans `is…` (`isAbsenceSaid`), unions tagged by `kind`.
- **Every run of the binary goes through Infrastructure's runner** (since 1b), discovery's `version` probe included: a read by `runBinary`, its document decoded tolerantly by Data; a command that writes the store by `superviseBinary`, with no time of its own. The API ends a spawn with its loop, the `next.signal` of the dispatch that started it (the person interrupting, its budget spent), or the module's unload, so such a command is begun from a timer, outside any dispatch, as a job (`detachJob`). A run that cannot start is a value said, never a throw.
- **A document is read whatever the exit status, and a missing field is said, never guessed** (since 1b): `check` exits 1 with its report; each decoder notes what it lacked, and the surface says it under what it shows.
- **A command that changes the store runs only on the person's yes, the exact command shown** (since 1b): the CLI's `train promote` and `train rollback` take no `--yes`, so the confirmation is the one gate, and the mod never passes `--yes`.
- **Anything long goes on as a job** (the async law, since 1b): `launchJob` waits up to its window and answers there, or keeps a job, answers its id, toasts its end and keeps its outcome for `apogee_job`; a command that writes the store is a job from its start (`detachJob`, above). Work that outlives its call catches its own end (a module unloading under it has nobody to tell).
- **The surfaces are registered at the session's start and answered by the root's hooks** (since 1b): `$.tool.register` and `$.command.register` with literal names, each registration on its own so one refused never drops the rest, what the engine took kept and said (never what was offered), a `tool.run` hook that answers only the names the mod registered and passes on the rest, and a `.catch` on each.
- **Tests are hermetic and mirror the layers:** a plugin test (`*.test.ts`, under `claude plugin test`) scripts the machine through `tests/support/machine.ts` (its commands, the person's answers and the mocked clock) and reads the state through `tests/support/state.ts`; a document comes from `tests/support/recorded.ts` (copied whole from a recording, its source cited) or `tests/support/handmade.ts` (`*_HANDMADE`, in the CLI's exact shape, its source named); a check that reads files (`*.test.mjs`) runs under node, never under `claude plugin test`.

## Release and Install Infrastructure

Repo-wide and recorded in the CLI docs. The mod joins it application-by-application like the GUI did: its `changed.sh` entry landed with the scaffold (`changed.sh mod`: `lib/src/mod/`, `lib/scripts/changed.sh` and `.github/workflows/mod.yml`, the last declared ahead of the workflow), and its pipeline is the queue's pipeline item (1d); distribution (a marketplace entry, a plugin-dir install) is that item's recorded question.
