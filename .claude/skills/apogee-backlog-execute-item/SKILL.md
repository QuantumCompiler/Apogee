---
name: apogee-backlog-execute-item
description: Take the next recommended Apogee backlog item (or a named one) — CLI, GUI or mod — and work it per that application's docs-first process; the skill resolves which queue is meant and asks when it cannot. Use when the user says "take the next backlog item", "continue", "work the queue", "next item", or names a specific backlog document to implement.
---

# Work an Apogee Backlog Item

Apogee is built docs-first: every work item has its own document in `<home>/backlog/`, and an agent builds **straight from that document**. This skill is the procedure for taking one.

## 0. Which application?

Apogee's documentation is per application ([the router](../../../lib/documentation/CLAUDE.md)): the CLI's home is [`lib/documentation/cli/`](../../../lib/documentation/cli/README.md), the GUI's [`lib/documentation/gui/`](../../../lib/documentation/gui/README.md), and the mod's (the Claude plugin) [`lib/documentation/mod/`](../../../lib/documentation/mod/README.md). Resolve which queue this invocation means before touching anything, in order:

1. **The arguments.** An id or table names its queue — the CLI's version tables (`v0.1.<x>`, `v0.2.0`) with their numeric tracks (`52a`) and the standing `M<n>`/`A<n>` queues; the GUI's and the mod's indexes with their lettered tracks. A named document resolves by which backlog holds it. A bare version can mean any queue — they all carry work per release.
2. **The conversation.** What the work touches: the harness's commands, backends and seams are the CLI's; screens, windows and the design are the GUI's; the Claude plugin (panes, slash commands, `$`-API work, `lib/src/mod/`) is the mod's. Work that spans queues gets cross-queue siblings, gated both ways.
3. **Still ambiguous? Ask.** One AskUserQuestion — which application's queue (the CLI, the GUI, or the mod) — before anything is treated as decided. Never guess between them.

Everything below reads from the chosen home, written `<home>`: its backlog README is the authority on that queue's tables, numbering, document format and link depths, and its assistant docs (`<home>/assistant/CLAUDE.md` first) carry the invariants. Where this skill says `<home>`, substitute the chosen application's directory.

## 1. Load the working process first

Before touching the queue, read the contributor docs in `<home>/assistant/` — they are the source of truth and override anything remembered from prior sessions:

1. `CLAUDE.md` (`<home>/assistant/CLAUDE.md`) — **read fully**: the working process, adopted invariants, Code Style (the home's own hard rules), and the Documentation and Status flow that defines "done".
2. `SPEC.md` (`<home>/assistant/SPEC.md`) — product shape, non-goals, principles.
3. `ROADMAP.md` (`<home>/assistant/ROADMAP.md`) — release context and the outstanding `[user]` decisions.
4. `DEVELOPER.md` (`<home>/assistant/DEVELOPER.md`) — architecture reference, as needed while building.

## 2. Pick the item

Read `<home>/backlog/README.md` (`<home>/backlog/README.md`) — lifecycle, document format, the **gate convention**, and the priority-ordered index.

- **The next item** is the topmost 🟢 row per the home README's own rule (🔒 rows wait on the items their Status cell names; every pending item assumes everything already shipped, and needs only its own gate chain).
- If the user named a specific item, take that one instead — but verify its gate is satisfied and say so if it isn't.

## 3. Read the item's document — it IS the working spec

Read the whole document: What/why, Core constraints, Seam + files, **Reference**, Decisions made, Open calls, Guardrails, Acceptance criteria, Scope note.

- **Split-first rule:** if the item is marked **split first** (in its Scope note or index row), do NOT build from it. The work is to groom it into its listed sub-documents (confirm the split with the user), add them to the index, and stop.
- **Reference:** read the in-house precedents and external prior art the section names before designing — the item docs cite them because they carry proven behavior and recorded lessons.

## 4. Resolve the Open calls before writing code

Open calls are tagged:

- `[user]` — a user-owned decision that **blocks the build**. Ask the user (AskUserQuestion) and wait. Do not start implementation until every `[user]` call on the item is answered.
- `[default: …]` — take the stated default, and record it as a new dated entry under **Decisions made** in the item document. The user may veto later.
- `(consumed decision)` — already decided in another item; do not reopen it.

## 5. Build

- Mark the item's index row **in progress** in the backlog README so parallel sessions see it's taken.
- One item at a time. Honor the item's Core constraints and the repo-wide rules (CLAUDE.md → Invariants and Code Style).
- Tests per the item's Guardrails; verify with `lib/scripts/cicd.sh --test` before calling anything done.
- For a **CLI item**, a check on real weights names the capability and the measure, not the models, per the model families rules (`lib/documentation/cli/assistant/DEVELOPER.md` → On real weights). A **GUI item**'s guardrails are hermetic — scripted fakes and fixture event streams, no network, no binary required — with any live pass against the installed binary optional and said; real models are always the user's run. A GUI probe may drive the installed `apogee` binary against a sandboxed `APOGEE_HOME` with the scripted mock backend — never the real `~/.apogee`.
- If the item changes the pipeline — a CI job added, removed or renamed, a matrix row, a new workflow, or what the CLI is built from — follow DEVELOPER.md → Changing the pipeline (`lib/documentation/cli/assistant/DEVELOPER.md#changing-the-pipeline`). Its required-checks step comes after the pull request's run passes and before the merge, and `--apply` changes repository settings: tell the user it is needed, and run it only when they say so.

## 6. Ship — the Documentation and Status flow

When the acceptance criteria pass, in the same change (per CLAUDE.md):

1. Record the work in `MILESTONES.md` (`<home>/assistant/MILESTONES.md`) (fold into the matching milestone, or start a new lettered one).
2. **Delete the item's backlog document and its index row** — the backlog holds pending work only. Update any other document whose gate this satisfied.
3. Check the box / move the line in ROADMAP.md.
4. Update the Codebase Map in CLAUDE.md and DEVELOPER.md for new/moved files.
5. Update SPEC.md only if scope, non-goals, or principles actually changed.

Then **stop and wait** for the user to ask to continue. (A bare "Continue" means: re-read CLAUDE.md, then take the topmost claimable item.)
