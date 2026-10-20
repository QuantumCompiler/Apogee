# CLAUDE.md

Project context for coding agents working on **the Apogee GUI** — the desktop applications. Read this before making changes. The root [`README.md`](../../../../README.md) is the public-facing overview of the whole project; this file and the rest of `lib/documentation/gui/assistant/` are the GUI's contributor-facing detail. You arrived here from the [documentation router](../../CLAUDE.md); CLI work belongs in [`cli/`](../../cli/assistant/CLAUDE.md) instead. This file tells you **how to work here**; the sibling documents tell you **what** and **why**.

## Reading order & the docs system

`lib/documentation/gui/assistant/` is a docs-as-source-of-truth system — work is discussed in chat, written up here, then handed to a coding session to build. Read them in this order, then pick the one that matches your intent:

| Doc | Purpose |
|---|---|
| **CLAUDE.md** (this file) | The contributor entry point: how to work, the hard rules, the codebase map. |
| [**SPEC.md**](SPEC.md) | The product spec — what the GUI is, its scope, non-goals, and design principles. |
| [**ROADMAP.md**](ROADMAP.md) | The high-level running board — each release at the feature/theme level, plus loose ideas and explicit non-goals. |
| [**backlog/**](../backlog/README.md) | **The work queue: one document per pending work item** (a sibling directory of `assistant/`). Its README carries the format, lifecycle, and a **priority-ordered index**. |
| [**MILESTONES.md**](MILESTONES.md) | The detailed record of finished work — what shipped and how it was built. |
| [**DEVELOPER.md**](DEVELOPER.md) | The package-by-package architecture reference: directories, files, interfaces, build/test commands. |

**How work flows through the docs:** a feature is discussed in chat → written up in ROADMAP.md (and SPEC.md if it changes product shape) → given its own document in `lib/documentation/gui/backlog/` once specced → an agent builds it straight from that document → folded into MILESTONES.md when done, and the backlog document deleted. There is no separate TODO file — the backlog document *is* the working spec.

**Repo-wide process is the CLI docs' to record** (the router's rule): versioning, the release pipeline, CI, the merge-is-the-release law and the install contract live in [`cli/assistant/CLAUDE.md`](../../cli/assistant/CLAUDE.md). This file states the GUI's own rules and its deltas, never a second copy.

---

## What this is

The Apogee GUI: one native desktop application per platform (`lib/src/darwin/`, `lib/src/linux/`, `lib/src/windows/`), each a graphical front end that **drives the `apogee` CLI over its stdio machine mode** — the harness's capabilities rendered, never reimplemented. See [SPEC.md](SPEC.md) for scope and principles; nothing has shipped yet ([MILESTONES.md](MILESTONES.md) starts empty).

## Stack & environment

- **Language / frameworks:** _TODO:_ undecided — the first spike settles each platform's shell (native per platform vs. one cross-platform shell) before any code exists.
- **Key dependency:** the `apogee` binary and its machine-mode contract — [`machine-mode.md`](../../cli/reference/machine-mode.md) and `machine-schema.json` (shipped in every CLI archive, printed by the binary itself). A development GUI can take the CLI from a release install or from CI's `apogee-<target>` artifacts.
- **Platforms:** macOS (Apple silicon), Linux (x64/arm64), Windows (x64/arm64) — the CLI's five targets. Primary dev host is macOS. _TODO:_ which platform's app is built first is an open call for the first spike.
- **Build:** each application under `lib/src/<app>/` is self-contained and owns its own build; when the first one lands it joins `cicd.sh`'s `APPS` list, gets its own entry in `lib/scripts/changed.sh`, and a pipeline of its own beside the CLI's (recorded in the CLI docs' map).

---

## ⚠ The GUI is a machine-mode client

**The rule.** Every capability reaches the GUI by driving the `apogee` binary as a child process over the stdio machine mode. The GUI never links the CLI's libraries, never calls a provider itself, never reimplements a capability the CLI owns, and never opens a listening socket for local work.

Why this is non-negotiable: "parity is the product" is the repo's founding law — every capability reaches every surface through one shared core. The moment a GUI grows its own path to a provider or its own copy of a capability, the surfaces drift apart and the CLI's test suite stops vouching for what the GUI does. The stdio contract is also the recorded product shape: local front-ends are pipes; `apogee serve` exists solely for remote deployments.

- Build against the protocol as the schema states it: `machine-schema.json` rides in every CLI archive, and the session's `capabilities` line announces what the binary speaks.
- The protocol grows **additively** (the CLI's ADR 0006): a GUI must tolerate events and fields it does not know, and must never require a protocol change that breaks an older front end.
- The CLI child's stderr is captured, never surfaced raw; cancellation is the protocol's `cancel` line, never a kill as the first resort.
- Secrets never pass through the GUI: keys are entered through the CLI's own flows, and the protocol carries no key fields by construction (the CLI's secrets invariant).

Enforced by: the contract side is test-locked in the CLI (`cli.machine_schema_conformance` pins the doc, the schema and the binary to each other; `cli.machine_mode` drives the real binary). GUI-side enforcement — a conformance suite over a recorded session, a link-policy analog — accretes here with the first application; until then this section is held by review.

---

## Codebase Map

Nothing is built yet. The planned layout (the repo's recorded shape for sibling apps):

| Path | Contents (planned) |
|------|----------|
| [`../adrs/`](../adrs/README.md) | The GUI's architecture decision records — [ADR 0001](../adrs/layer-enforcement.md): four layers, dependencies point down, regardless of language and framework. |
| `lib/src/darwin/` | The macOS application — self-contained, own build, `source/<layer>/<package>/` and `tests/` mirroring it (ADR 0001's shape). |
| `lib/src/linux/` | The Linux application — same shape. |
| `lib/src/windows/` | The Windows application — same shape. |

_TODO:_ fill with real packages as the first application lands; what is shared between the three apps (a machine-mode client core?) is a first-spike question, decided within ADR 0001's layers.

---

## Working process

The GUI is built docs-first, the same lifecycle as the CLI's. Where new work is written down, how a backlog item is worked, and the bare-"Continue" convention are exactly the CLI docs' rules — read [Working process](../../cli/assistant/CLAUDE.md#working-process) there. The GUI's deltas:

- GUI work lands in **this** tree's ROADMAP/backlog/MILESTONES, never the CLI's; an item that needs a CLI change (a new protocol event, a new read) gets a sibling item in the CLI's backlog, gated both ways.
- **Versioning is repo-wide** (the release, `lib/release/VERSION`, the merge-is-the-release law — the CLI docs record it). The GUI ships inside those releases once it exists; its pipeline joins CI beside the CLI's.

## Implementing a Feature

Every change should satisfy this before merge:

- [ ] Tests for the new behavior pass, and existing tests stay green. _TODO:_ the test command lands with the first application's build.
- [ ] The machine-mode invariant holds: no linked CLI, no second capability, unknown protocol events tolerated.
- [ ] **The four-layer law holds** ([ADR 0001](../adrs/layer-enforcement.md)): a new package declares its layer, dependencies point down, the UI framework's types stay in Presentation — whatever the language.
- [ ] The affected docs are updated in the same change (see **Documentation and Status**).

## Code Style

- _TODO:_ accretes with the first application (language undecided). Until then: match the surrounding code's conventions; when in doubt, find the closest existing analogue and follow it.

## Documentation and Status

When a work item ships, in the same change:

1. **Extend [MILESTONES.md](MILESTONES.md)** — fold the work into the matching milestone (or start a new one for a genuinely new area): goal, what was built, trade-offs.
2. **Delete the backlog document** and its index row — the backlog holds pending work only.
3. **Update [ROADMAP.md](ROADMAP.md)** — check the box / move the line to shipped.
4. **Update the [Codebase Map](#codebase-map) and [DEVELOPER.md](DEVELOPER.md)** for any new/moved/deleted files.
5. **Update [SPEC.md](SPEC.md)** only if scope, non-goals, or principles actually changed.

---

## Release and Install Infrastructure

Repo-wide and recorded in the CLI docs ([Release and Install Infrastructure](../../cli/assistant/CLAUDE.md#release-and-install-infrastructure)): a merge into `stable` publishes the release `lib/release/VERSION` names. The GUI's own packaging, install path and pipeline join that machinery when the first application lands — each app gets its own `changed.sh` entry and its own pipeline, and can take the CLI from the run's `apogee-<target>` artifacts. _TODO:_ the specifics are the first application's items.
