---
name: apogee-backlog-execute-group
description: Execute an entire backlog table unsupervised — given a group id (v0.1.3, maintenance, architecture), read every item document, gather all blocking user input up front, then build each item serially to the full docs-first bar, committing once per shipped item. Use when the user names a group to run ("execute v0.1.3", "run the maintenance table", "work the whole group").
---

# Execute a Backlog Group

The group id is an index table: a version (`v0.1.3`), `maintenance`, or `architecture`. This skill runs **every item in that table, serially, without supervision** — the user states the group and walks away. Each item individually gets the full `/apogee-backlog-execute-item` treatment (that skill's process is the per-item law: docs loaded, open calls resolved, built to the guardrails, verified, shipped through the Documentation and Status flow). What this skill adds is the batch contract: all reading and all questions **up front**, autonomous problem-solving **in the middle**, and a **commit per shipped item** at each boundary.

## 1. Read everything first, ask everything first

1. Load the working process per `/apogee-backlog-execute-item` step 1 (CLAUDE.md fully, SPEC, ROADMAP, DEVELOPER as needed) and the [backlog README](../../../lib/documentation/backlog/README.md).
2. **Read every document in the group's table before building anything.** The run's order is the table's build order with gates honored; an item whose gate chain leaves the group and is not shipped is marked out of the run at the start, and said.
3. **Resolve every blocking call now.** Collect the `[user]` open calls across *all* the group's documents and ask them together (AskUserQuestion), before the first line of code — mid-run there is no user. `[default: …]` calls are taken as stated and recorded as dated decisions in their documents. An unanswered `[user]` call drops only its own item (and its dependents) from the run, and the drop is announced in the plan, not discovered at item seven.
4. Check `git status` and the index's 🚧 marks: this skill assumes it owns the checkout — commits punctuate the run — so another session mid-item on the same checkout means stop and tell the user before starting.

## 2. The serial loop

For each item, in order:

1. Mark its row 🚧, build **per its document** — constraints, seam, guardrails, acceptance criteria, real-weights rules where they apply — and verify (`lib/scripts/cicd.sh --test` before anything is called done).
2. Ship it through the Documentation and Status flow: MILESTONES entry; the document deleted with its index row (and any other item's gate this satisfied, updated); ROADMAP line checked/moved; codebase maps updated; SPEC only on a real shape change.
3. **Commit — this skill's own authorization, and only here.** Exactly one commit per shipped item, at its boundary, never a push:

   ```
   git add -A
   git commit -m "<branch> : <Title>"
   ```

   `<branch>` is the current branch name (`git branch --show-current` — on the `v0.1.3` branch, `v0.1.3`), whatever the group is. `<Title>` is the item's document filename, kebab-case split into words and title-cased, with known acronyms fully uppercased, matching the existing history's convention: `sampling-profiles` → `v0.1.3 : Sampling Profiles`, `cli-busy-line` → `v0.1.3 : CLI Busy Line`, `gguf-header-cache` → `v0.1.3 : GGUF Header Cache`. Session-level attribution trailers follow the session's own rules; the subject line is exactly this form. Outside this skill, committing stays the user's move — one group run grants one commit per item it ships, nothing more.

## 3. Unsupervised means unsupervised

This skill is chosen precisely when successive items should complete with nobody watching:

- **Issues are yours to solve.** A failing test, a surprising seam, a doc ambiguity — debug it, consult the Ommi reference the document cites, re-read the assistant docs, adapt. Never stop to ask what step 1 could have asked.
- **Unforeseen judgment calls** get the most conservative choice consistent with SPEC and the invariants, recorded as a dated decision in the item's document *before* it ships (so the decision lands in the MILESTONES record), and flagged in the final report for veto.
- **A genuinely stuck item** — not buildable to its acceptance criteria after real effort — does not ship broken and does not poison the run: revert the working tree to the last commit boundary (the previous item's commit), restore the item's row from 🚧 with a dated note of what blocked it in its document, skip anything gated on it, and continue with the rest. The run ends with the truth, not with a plausible-looking pile.
- Report outcomes faithfully at each boundary line of the final report: tests that passed, families skipped and said, anything deferred.

## 4. End of the run

- A version table that empties is **not** a release: the roll (VERSION bump, ROADMAP state swap, table retirement) is `/apogee-maintenance-update-release`'s job, on the user's word. Maintenance and Architecture tables persist empty — they are permanent fixtures.
- Run the standard index validation (rows ↔ docs, sequences, gates, links) — the run deleted rows and documents in step pairs, and the sweep proves it stayed consistent.

## 5. Report

One report for the whole run: per item — shipped with its commit subject and verification result, or skipped with why (unanswered call, out-of-group gate, stuck with the blocking note's location); every decision made autonomously, gathered for veto; the group's final state; and what remains the user's (the release roll, pushes, anything skipped).
