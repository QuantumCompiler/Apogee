---
name: apogee-create-backlog-item
description: Spec a new Apogee work item into the backlog — write its document, place it in the priority index, and update the planning docs. Use when the user wants to add, spec, or queue a new feature/fix ("add a backlog item", "spec this into the backlog", "queue this up", "create an item for X").
---

# Create an Apogee Backlog Item

Apogee is docs-first: an idea becomes a line on the roadmap, then — once specced — **one document in `lib/documentation/backlog/`** that an agent later implements verbatim. This skill is the procedure for authoring that document *well*. A backlog doc is a working spec, not a ticket: wrong or thin content propagates straight into built software.

## 1. Read before writing

1. [`lib/documentation/backlog/README.md`](../../../lib/documentation/backlog/README.md) — the **document format** (section order), the **open-call tag semantics** (`[user]` / `[default: …]` / `(consumed decision)`), the **gate convention**, and the current index.
2. [`lib/documentation/assistant/CLAUDE.md`](../../../lib/documentation/assistant/CLAUDE.md) — the working process, adopted invariants, and Code Style (constraints new items must carry where they apply).
3. [`lib/documentation/assistant/SPEC.md`](../../../lib/documentation/assistant/SPEC.md) — scope, non-goals, principles, and the recorded divergences from Ommi. If the idea conflicts with a non-goal or changes product shape, **stop and raise that with the user first** — a SPEC revision is its own decision, made before the item exists.
4. [`lib/documentation/assistant/ROADMAP.md`](../../../lib/documentation/assistant/ROADMAP.md) — where releases stand and what's already queued or parked.
5. **Scan the existing backlog documents** for overlap. If the idea belongs inside an existing item's scope, extend that document instead of creating a near-duplicate — one concern, one home.

## 2. Check the Ommi reference

Apogee is a re-implementation of Ommi (`~/Data/Development/Projects/Ommi`). Before designing anything, check whether Ommi has an analog of this feature — its docs (`lib/cli/documentation/`) and source carry proven behavior, recorded invariants, and hard-won lessons. If an analog exists, read it and cite the packages/features in the item's **Reference (Ommi)** section, stating any deliberate divergence. If none exists, say so explicitly there — an honest "no analog" beats a vague citation.

## 3. Write the document

Kebab-case filename in `lib/documentation/backlog/`, following the format in the backlog README exactly. Quality bar per section:

- **What / why** — what it delivers and why it's worth building, concrete enough to build from. If it's too big for one focused session, mark it **split first** and list the sub-documents the grooming must produce.
- **Core constraint(s)** — pull in the repo invariants it touches (never-listens, parity/backfill rules, secrets hygiene, code style) plus its own; a constraint the item doesn't state is a constraint the builder won't honor.
- **Seam + files** — planned `.h`/`.cpp` paths under `lib/src/`, consistent with the layout existing items already use; name the interfaces it extends and the items it consumes decisions from.
- **Reference (Ommi)** — from step 2.
- **Decisions made** — dated entries for anything settled while specing (including why it sits where it sits in the queue).
- **Open calls** — every call tagged: `[user]` only for genuinely user-owned decisions (they block the build), `[default: …]` with a stated recommendation for agent-decidable picks. Don't reopen decisions other items own — mark those `(consumed decision)`.
- **Guardrail(s)** — what's tested so the change can't regress silently.
- **Acceptance criteria** — observable, checkable outcomes; no vibes.
- **Scope note** — earmarked / gated on `<item>` / unscheduled, honoring the gate convention (name a real item; no cycles).

## 4. Place it and update the docs

1. **Index** ([backlog README](../../../lib/documentation/backlog/README.md)): insert the row at its priority position — right phase, `Build after` column, **split first** marker if applicable — and renumber the affected rows and the phase preamble's track numbering. Confirm the position with the user if it isn't obvious.
2. **[ROADMAP.md](../../../lib/documentation/assistant/ROADMAP.md)**: add the item's line in the right section (the in-progress release's list, Fast follow, or the ideas parking lots).
3. **[SPEC.md](../../../lib/documentation/assistant/SPEC.md)**: only if scope, non-goals, or principles actually changed — with a dated revision note, per the house style.
4. Do **not** touch MILESTONES.md (finished work only) and do not start implementing — creating the item and building it are separate steps (`/apogee-backlog-item` handles the build).

## 5. Report

Tell the user: the new document's path, its index position and gate, which docs were updated, and every `[user]` open call it carries — those are the questions that will block the build later, so surfacing them now is the point.
