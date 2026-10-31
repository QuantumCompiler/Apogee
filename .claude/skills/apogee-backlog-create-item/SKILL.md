---
name: apogee-backlog-create-item
description: Spec a new Apogee work item — CLI, GUI or mod — into the right backlog: write its document, place it in the index table the user names, and update that application's planning docs; the skill resolves which application is meant and asks when it cannot. Use when the user wants to add, spec, or queue a new feature/fix ("add a backlog item", "spec this into the backlog", "queue this up", "create an item for X in v0.1.Y / Maintenance").
---

# Create an Apogee Backlog Item

Apogee is docs-first: an idea becomes a line on the roadmap, then — once specced — **one document in `<home>/backlog/`** that an agent later implements verbatim. This skill is the procedure for authoring that document *well*. A backlog doc is a working spec, not a ticket: wrong or thin content propagates straight into built software.

**The user names the destination** — which of the home's tables the item joins and where it sits is their placement call; take it from their request, and ask only when they genuinely haven't said. Identity follows the home README's own numbering, ever-assigned (shipped ids are spent): the CLI's version-track letters and `M<n>`/`A<n>`; the GUI's lettered tracks, further tables born with their first item.

## 0. Which application?

Apogee's documentation is per application ([the router](../../../lib/documentation/CLAUDE.md)): the CLI's home is [`lib/documentation/cli/`](../../../lib/documentation/cli/README.md), the GUI's [`lib/documentation/gui/`](../../../lib/documentation/gui/README.md), and the mod's (the Claude plugin) [`lib/documentation/mod/`](../../../lib/documentation/mod/README.md). Resolve which queue this invocation means before touching anything, in order:

1. **The arguments.** An id or table names its queue — the CLI's version tables (`v0.1.<x>`, `v0.2.0`) with their numeric tracks (`52a`) and the standing `M<n>`/`A<n>` queues; the GUI's and the mod's indexes with their lettered tracks. A named document resolves by which backlog holds it. A bare version can mean any queue — they all carry work per release.
2. **The conversation.** What the work touches: the harness's commands, backends and seams are the CLI's; screens, windows and the design are the GUI's; the Claude plugin (panes, slash commands, `$`-API work, `lib/src/mod/`) is the mod's. Work that spans queues gets cross-queue siblings, gated both ways.
3. **Still ambiguous? Ask.** One AskUserQuestion — which application's queue (the CLI, the GUI, or the mod) — before anything is treated as decided. Never guess between them.

Everything below reads from the chosen home, written `<home>`: its backlog README is the authority on that queue's tables, numbering, document format and link depths, and its assistant docs (`<home>/assistant/CLAUDE.md` first) carry the invariants. Where this skill says `<home>`, substitute the chosen application's directory.

## 1. Read before writing

1. `<home>/backlog/README.md` (`<home>/backlog/README.md`) — the **document format** (section order), the **open-call tag semantics** (`[user]` / `[default: …]` / `(consumed decision)`), the **gate convention**, the **index-table format and numbering rules**, and the current index.
2. `<home>/assistant/CLAUDE.md` (`<home>/assistant/CLAUDE.md`) — the working process, adopted invariants, and Code Style (constraints new items must carry where they apply).
3. `<home>/assistant/SPEC.md` (`<home>/assistant/SPEC.md`) — scope, non-goals, and principles. If the idea conflicts with a non-goal or changes product shape, **stop and raise that with the user first** — a SPEC revision is its own decision, made before the item exists.
4. `<home>/assistant/ROADMAP.md` (`<home>/assistant/ROADMAP.md`) — where releases stand and what's already queued or parked.
5. **Scan the existing backlog documents** for overlap. If the idea belongs inside an existing item's scope, extend that document instead of creating a near-duplicate — one concern, one home.

## 2. Write the document

Kebab-case filename in **the target table's subdirectory** of `<home>/backlog/` (the tree mirrors the index tables: `architecture/`, `maintenance/`, `v0.1.<x>/` — create the directory with a table's first item), following the format in the backlog README exactly. Links out of the backlog climb two levels (`../../assistant/…`); same-table doc links are bare filenames, cross-table ones `../<table-dir>/<file>`. Quality bar per section:

- **What / why** — what it delivers and why it's worth building, concrete enough to build from. If it's too big for one focused session, mark it **split first** and list the sub-documents the grooming must produce.
- **Core constraint(s)** — pull in the repo invariants it touches (never-listens, parity/backfill rules, secrets hygiene, code style) plus its own; a constraint the item doesn't state is a constraint the builder won't honor.
- **Seam + files** — planned `.h`/`.cpp` paths under `lib/src/`, consistent with the layout existing items already use; name the interfaces it extends and the items it consumes decisions from.
- **Reference** — optional: the in-house precedents the item mirrors (shipped items, existing seams) and any external prior art it draws on, cited by name; omit it rather than cite vaguely.
- **Decisions made** — dated entries for anything settled while specing (including why it sits where it sits in the queue — the user's placement call is one of them).
- **Open calls** — every call tagged: `[user]` only for genuinely user-owned decisions (they block the build), `[default: …]` with a stated recommendation for agent-decidable picks. Don't reopen decisions other items own — mark those `(consumed decision)`.
- **Guardrail(s)** — what's tested so the change can't regress silently. For a **CLI item**, a check on real weights names the capability and the measure, not the models, per the model families rules (`lib/documentation/cli/assistant/DEVELOPER.md` → On real weights). A **GUI item**'s guardrails are hermetic — scripted fakes and fixture event streams, no network, no binary required — with any live pass against the installed binary optional and said; real models are always the user's run.
- **Acceptance criteria** — observable, checkable outcomes; no vibes.
- **Scope note** — earmarked for `<version>` / Maintenance or Architecture item / gated on `<item>` / unscheduled, honoring the gate convention (name a real item; no cycles).

## 3. Place it and update the docs

1. **Index** (backlog README (`<home>/backlog/README.md`)): insert the row into **the table the user named** (creating that table and its directory if it is the first item there), at the position they gave — or, with a next-in-sequence id, at the table's end, which keeps rows ascending; a position higher in the build order than the id allows is a wider renumber, confirmed with the user (`/apogee-backlog-swap-item` territory). Follow the index-table format the README defines — the gate in the **Status** cell, **split first** marker if applicable — and update the track preamble above the table.
2. **ROADMAP.md (`<home>/assistant/ROADMAP.md`)**: add the item's line in the matching section (the in-progress release's board, the **Up next** release it targets, the Maintenance/Architecture section, or the ideas parking lots).
3. **SPEC.md (`<home>/assistant/SPEC.md`)**: only if scope, non-goals, or principles actually changed — with a dated revision note, per the house style.
4. Do **not** touch MILESTONES.md (finished work only) and do not start implementing — creating the item and building it are separate steps (`/apogee-backlog-execute-item` handles the build).

## 4. Report

Tell the user: the new document's path, its id, its index position and gate, which docs were updated, and every `[user]` open call it carries — those are the questions that will block the build later, so surfacing them now is the point.
