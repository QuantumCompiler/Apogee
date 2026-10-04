---
name: apogee-cli-backlog-execute-call-resolution
description: Resolve every open call in a backlog group's documents, one by one — the user answers the user-owned calls, confirms or vetoes the recorded defaults, and each answer lands in its document as a dated decision with its consequences propagated, so the group builds later with nothing left to ask. Use when the user points at a group ("resolve the open calls in v0.1.4", "groom maintenance's calls", "clear the questions in architecture").
---

# Resolve a Group's Open Calls

A backlog group (a table: `v0.1.4`, `maintenance`, `architecture`) builds fastest when nothing in it is still a question. This skill is the standalone grooming pass: walk every document in the group, surface every entry under **Open calls**, and resolve them **one by one** — each answer recorded where it belongs *before* the next question is asked. It changes documents, never code: `/apogee-cli-backlog-execute-item` and `/apogee-cli-backlog-execute-group` still do the building; after this pass, their ask-everything-first steps find nothing left to ask.

## 1. Ground and inventory

1. Re-read the live [backlog README](../../../lib/documentation/backlog/README.md) and `git status` — parallel sessions ship and take items; a 🚧 item's calls belong to its builder, so its document is **skipped and said**, and a placeholder with no document (a "still to be written" row) likewise.
2. Read **every document in the group's table**, in table order, and build the inventory: for each doc, its calls by tag — `[user]` (blocks the build), `[default: …]` (agent-decidable, veto-able), `(consumed decision)` (owned elsewhere — verify the owner still exists; if it shipped, repoint the note to the MILESTONES record, that's the whole resolution).
3. Show the user the map before the first question: how many calls, which documents, which are user-owned — so they know the size of the sitting before it starts.

## 2. Resolve, one by one, in build order

Work through the documents in the table's build order, and within a document top to bottom. **An answer is recorded before the next question is asked** — the documents are never left holding a batch of unwritten decisions.

- **`[user]` calls get full attention, one at a time:** one AskUserQuestion each, phrased in plain architecture language — what the call decides, what each option costs, and the document's recommendation (when it has one) as the first, recommended option. No jargon, no stacking three decisions into one question.
- **`[default: …]` calls are confirm-or-veto, batched lightly:** up to four per AskUserQuestion round, each with "take the default (recommended)" first and the default's own wording shown. A veto becomes the user's call, recorded as such; a confirmation locks the default.
- **A call that evidence has mooted** (the feature shipped differently, the dependency vanished) is not asked — it is removed with a dated decision explaining why, and said in the report.
- **"Skip" is a real answer:** a call the user declines to decide stays open, stays tagged, and is listed in the report as still blocking — never silently dropped, never guessed instead.

## 3. Record each resolution completely

Per answer, in the document, immediately:

1. **Decisions made** gains the dated entry — the question, the choice, whose call it was ("the user's call, <date>" / "the default, confirmed <date>").
2. **The call leaves Open calls.**
3. **Consequences propagate**: any constraint, seam line, guardrail or acceptance criterion that branched on the call is rewritten to its decided form (the house precedent: when the config-format comments call landed on JSONC, the editor contract, migration golden, template line and acceptance criteria all flipped to the decided shape in the same pass — a decision recorded but not propagated is drift wearing a date).
4. **The flags come down elsewhere**: an index row, track preamble or ROADMAP line that advertised the call as blocking is updated to say it was answered, with the date — the "**was answered the same day**" phrasing is the house style.

## 4. Validate and report

- The group's documents grep clean: no `[user]` tags remain except the deliberate skips; no `[default:` entries remain except those skipped or deliberately left for build time (say which and why).
- The standard index validation (rows ↔ docs, gates, links) still passes — resolution edits documents, so the sweep proves nothing tore.
- Report: every call asked and its answer, one line each; the decisions recorded per document; the consequences that were propagated; the skips (🚧 docs, declined calls, missing documents) with what still blocks what; and the group's state now — ideally "nothing in this table waits on a question."
