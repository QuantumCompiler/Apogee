---
name: apogee-backlog-item
description: Take the next recommended Apogee backlog item (or a named one) and work it per the repo's docs-first process. Use when the user says "take the next backlog item", "continue", "work the queue", "next item", or names a specific backlog document to implement.
---

# Work an Apogee Backlog Item

Apogee is built docs-first: every work item has its own document in `lib/documentation/backlog/`, and an agent builds **straight from that document**. This skill is the procedure for taking one.

## 1. Load the working process first

Before touching the queue, read the contributor docs in `lib/documentation/assistant/` — they are the source of truth and override anything remembered from prior sessions:

1. [`CLAUDE.md`](../../../lib/documentation/assistant/CLAUDE.md) — **read fully**: the working process, adopted invariants, Code Style (hard rules: `.h`/`.cpp` pairs; smart pointers only, never owning raw pointers), and the Documentation and Status flow that defines "done".
2. [`SPEC.md`](../../../lib/documentation/assistant/SPEC.md) — product shape, divergences from Ommi, non-goals, principles.
3. [`ROADMAP.md`](../../../lib/documentation/assistant/ROADMAP.md) — release context and the outstanding `[user]` decisions.
4. [`DEVELOPER.md`](../../../lib/documentation/assistant/DEVELOPER.md) — architecture reference, as needed while building.

## 2. Pick the item

Read [`lib/documentation/backlog/README.md`](../../../lib/documentation/backlog/README.md) — lifecycle, document format, the **gate convention**, and the priority-ordered index.

- **The next item** is the topmost row that is not in progress, whose "Build after" gate is satisfied. A Phase-1 (v0.1.0) item needs only its gate chain; a Phase-2/3 item additionally assumes the complete v0.1.0 set has shipped.
- If the user named a specific item, take that one instead — but verify its gate is satisfied and say so if it isn't.

## 3. Read the item's document — it IS the working spec

Read the whole document: What/why, Core constraints, Seam + files, **Reference (Ommi)**, Decisions made, Open calls, Guardrails, Acceptance criteria, Scope note.

- **Split-first rule:** if the item is marked **split first** (in its Scope note or index row), do NOT build from it. The work is to groom it into its listed sub-documents (confirm the split with the user), add them to the index, and stop.
- **Reference (Ommi):** the sibling project at `~/Data/Development/Projects/Ommi` is the reference implementation. Read the Ommi sources/docs the section names before designing — the item docs cite them because they carry proven behavior and recorded lessons.

## 4. Resolve the Open calls before writing code

Open calls are tagged:

- `[user]` — a user-owned decision that **blocks the build**. Ask the user (AskUserQuestion) and wait. Do not start implementation until every `[user]` call on the item is answered.
- `[default: …]` — take the stated default, and record it as a new dated entry under **Decisions made** in the item document. The user may veto later.
- `(consumed decision)` — already decided in another item; do not reopen it.

## 5. Build

- Mark the item's index row **in progress** in the backlog README so parallel sessions see it's taken.
- One item at a time. Honor the item's Core constraints and the repo-wide rules (CLAUDE.md → Invariants and Code Style).
- Tests per the item's Guardrails; verify with `lib/scripts/cicd.sh --test` before calling anything done.

## 6. Ship — the Documentation and Status flow

When the acceptance criteria pass, in the same change (per CLAUDE.md):

1. Record the work in [`MILESTONES.md`](../../../lib/documentation/assistant/MILESTONES.md) (fold into the matching milestone, or start a new lettered one).
2. **Delete the item's backlog document and its index row** — the backlog holds pending work only. Update any other document whose gate this satisfied.
3. Check the box / move the line in ROADMAP.md.
4. Update the Codebase Map in CLAUDE.md and DEVELOPER.md for new/moved files.
5. Update SPEC.md only if scope, non-goals, or principles actually changed.

Then **stop and wait** for the user to ask to continue. (A bare "Continue" means: re-read CLAUDE.md, then take the topmost claimable item.)
