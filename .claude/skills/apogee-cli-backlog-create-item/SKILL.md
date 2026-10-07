---
name: apogee-cli-backlog-create-item
description: Spec a new Apogee work item into the backlog — write its document, place it in the index table the user names, and update the planning docs. Use when the user wants to add, spec, or queue a new feature/fix ("add a backlog item", "spec this into the backlog", "queue this up", "create an item for X in v0.1.Y / Maintenance").
---

# Create an Apogee Backlog Item

Apogee is docs-first: an idea becomes a line on the roadmap, then — once specced — **one document in `lib/documentation/backlog/`** that an agent later implements verbatim. This skill is the procedure for authoring that document *well*. A backlog doc is a working spec, not a ticket: wrong or thin content propagates straight into built software.

**The user names the destination.** Which table the item joins — a version table (`v0.1.<x>`), **Maintenance**, or **Architecture** — and where it sits in that table's build order is the user's placement call; take it from their request, and ask only when they genuinely haven't said. The destination then dictates the identity per the index's numbering rules: a release whose pending items share a track prefix hands out that track's next letter ever assigned (shipped letters are spent); Maintenance and Architecture hand out the next `M<n>` / `A<n>`.

## 1. Read before writing

1. [`lib/documentation/backlog/README.md`](../../../lib/documentation/backlog/README.md) — the **document format** (section order), the **open-call tag semantics** (`[user]` / `[default: …]` / `(consumed decision)`), the **gate convention**, the **index-table format and numbering rules**, and the current index.
2. [`lib/documentation/assistant/CLAUDE.md`](../../../lib/documentation/assistant/CLAUDE.md) — the working process, adopted invariants, and Code Style (constraints new items must carry where they apply).
3. [`lib/documentation/assistant/SPEC.md`](../../../lib/documentation/assistant/SPEC.md) — scope, non-goals, and principles. If the idea conflicts with a non-goal or changes product shape, **stop and raise that with the user first** — a SPEC revision is its own decision, made before the item exists.
4. [`lib/documentation/assistant/ROADMAP.md`](../../../lib/documentation/assistant/ROADMAP.md) — where releases stand and what's already queued or parked.
5. **Scan the existing backlog documents** for overlap. If the idea belongs inside an existing item's scope, extend that document instead of creating a near-duplicate — one concern, one home.

## 2. Write the document

Kebab-case filename in **the target table's subdirectory** of `lib/documentation/backlog/` (the tree mirrors the index tables: `architecture/`, `maintenance/`, `v0.1.<x>/` — create the directory with a table's first item), following the format in the backlog README exactly. Links out of the backlog climb two levels (`../../assistant/…`); same-table doc links are bare filenames, cross-table ones `../<table-dir>/<file>`. Quality bar per section:

- **What / why** — what it delivers and why it's worth building, concrete enough to build from. If it's too big for one focused session, mark it **split first** and list the sub-documents the grooming must produce.
- **Core constraint(s)** — pull in the repo invariants it touches (never-listens, parity/backfill rules, secrets hygiene, code style) plus its own; a constraint the item doesn't state is a constraint the builder won't honor.
- **Seam + files** — planned `.h`/`.cpp` paths under `lib/src/`, consistent with the layout existing items already use; name the interfaces it extends and the items it consumes decisions from.
- **Reference** — optional: the in-house precedents the item mirrors (shipped items, existing seams) and any external prior art it draws on, cited by name; omit it rather than cite vaguely.
- **Decisions made** — dated entries for anything settled while specing (including why it sits where it sits in the queue — the user's placement call is one of them).
- **Open calls** — every call tagged: `[user]` only for genuinely user-owned decisions (they block the build), `[default: …]` with a stated recommendation for agent-decidable picks. Don't reopen decisions other items own — mark those `(consumed decision)`.
- **Guardrail(s)** — what's tested so the change can't regress silently. A check on real weights names the capability and the measure, not the models: it runs on [the model families](../../../lib/documentation/assistant/DEVELOPER.md#on-real-weights-the-model-families) (Meta, Qwen, Google, OpenAI, each on its Q4_K_M build when installed, one family at a time; the excluded models are listed there).
- **Acceptance criteria** — observable, checkable outcomes; no vibes.
- **Scope note** — earmarked for `<version>` / Maintenance or Architecture item / gated on `<item>` / unscheduled, honoring the gate convention (name a real item; no cycles).

## 3. Place it and update the docs

1. **Index** ([backlog README](../../../lib/documentation/backlog/README.md)): insert the row into **the table the user named** (creating that table and its directory if it is the first item there), at the position they gave — or, with a next-in-sequence id, at the table's end, which keeps rows ascending; a position higher in the build order than the id allows is a wider renumber, confirmed with the user (`/apogee-cli-backlog-swap-item` territory). Follow the index-table format the README defines — the gate in the **Status** cell, **split first** marker if applicable — and update the track preamble above the table.
2. **[ROADMAP.md](../../../lib/documentation/assistant/ROADMAP.md)**: add the item's line in the matching section (the in-progress release's board, the **Up next** release it targets, the Maintenance/Architecture section, or the ideas parking lots).
3. **[SPEC.md](../../../lib/documentation/assistant/SPEC.md)**: only if scope, non-goals, or principles actually changed — with a dated revision note, per the house style.
4. Do **not** touch MILESTONES.md (finished work only) and do not start implementing — creating the item and building it are separate steps (`/apogee-cli-backlog-execute-item` handles the build).

## 4. Report

Tell the user: the new document's path, its id, its index position and gate, which docs were updated, and every `[user]` open call it carries — those are the questions that will block the build later, so surfacing them now is the point.
