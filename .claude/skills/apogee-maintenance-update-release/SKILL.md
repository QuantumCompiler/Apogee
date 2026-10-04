---
name: apogee-maintenance-update-release
description: Advance or correct Apogee's live release version — bump the version pointer the pipeline publishes from and reconcile every document that names the in-development release, so code, CI and docs agree on one number. Use at a release boundary or on a version change ("bump the version", "open v0.1.X", "roll the release", "this branch ships as vX.Y.Z").
---

# Update the release version and its documents

Apogee's live version is stated in more than one place, and the pipeline trusts them: **`lib/release/VERSION`** names the release CI publishes (a merged PR that changed the CLI publishes via *tag and release* under exactly this name — and one that forgot to bump it would merge and publish nothing, which the workflow guards against), **`lib/src/cli/CMakeLists.txt`** carries the binary's own `VERSION` (what `apogee check` prints), the work branch is named for the release, and the documents narrate it. This skill is the one pass that moves that number — forward at a release boundary, or sideways when the user re-targets the in-development version — and reconciles everything that names it. It changes **state about the release, never the release itself**: publishing is CI's job on the merged PR, and the notes are `/apogee-maintenance-summarize-release`'s.

## 1. Ground in the live state first

1. `git status` and the current branch; the latest *published* release (`gh release list`, or ROADMAP's Shipped sections); the current `lib/release/VERSION` and the `VERSION` line in `lib/src/cli/CMakeLists.txt`. If those two already disagree, say so before anything else — that is a bug to fix, not to roll over.
2. Confirm the direction with what the user said: **a boundary roll** (the in-progress release is done; open the next) or **a re-target** (the same in-development work ships under a different number). The steps below are the boundary roll; a re-target is the same sweep with no shipped/next section swap.
3. Parallel sessions ship while you work: re-read the live ROADMAP and backlog README, and re-anchor any edit that fails.

## 2. The gate: is the release actually done?

At a boundary roll, the shipping version's **backlog table must be empty** — every row shipped (deleted, in MILESTONES) or deliberately moved. Rows still standing mean the roll waits, or the user moves them (`/apogee-backlog-swap-item` to another table, or the whole-queue slide via `/apogee-backlog-swap-version`) — ask, never relocate items on your own initiative. Likewise the shipped version's MILESTONES record and ROADMAP board should exist and read complete; gaps are reported, not papered over.

## 3. The sweep — every carrier of the live number

In one pass, oldest-truth first:

1. **`lib/release/VERSION`** — the new number, bare (`0.1.4`), no `v`.
2. **`lib/src/cli/CMakeLists.txt`** — the project `VERSION` line, kept byte-equal in meaning with the file above.
3. **ROADMAP.md** — the finished release's `### v0.1.<n>` section moves from **In progress** to **Shipped releases** (release date and `[release notes](…/releases/tag/v0.1.<n>)` link in its header, checkboxes resolved — unfinished lines move with their items, they don't vanish); the next release's **Up next** section is promoted to **In progress**.
4. **Backlog README** — the shipped version's (now empty) table and its directory go away; the next version's table is now the earliest, which makes it the bare-"Continue" queue. The format spec and preambles need no change for a roll — that is the point of the standing rules.
5. **Root `README.md`** — the status paragraph ("… are out, and `v0.1.<n>` is in development on its branch") gains the shipped release in its list and names the new in-development version.
6. **The rest by survey, not memory:** grep the old number across `lib/documentation`, `README.md`, `lib/scripts` and `.github` and judge each hit — **live-state mentions** (in-development wording, install/CI references to the current version) update; **historical mentions** (MILESTONES records, Shipped sections, dated decisions, backlog documents' "earmarked for" of *future* versions) stay exactly as written. Versioned directory names under `backlog/` are queue state, not history — they follow rule 4, nothing else.

Never blind-replace the bare number: `0.1.3` lives inside dates, decisions and shipped records. Every hit is judged, then edited as a longer exact string.

## 4. What this skill never does

- **Never publishes, tags, or pushes** — the pipeline releases on the merged PR; branch creation for the new version is the user's move (say that it's next, don't do it).
- **Never writes MILESTONES' shipped record or the release notes** — the ship flow and `/apogee-maintenance-summarize-release` own those; this skill only verifies they exist at a boundary.
- **Never renumbers queued future versions** — sliding v0.1.5→v0.1.6 targets is `/apogee-backlog-swap-version`.
- **Never rewrites history** — a shipped section, record, or dated decision keeps its numbers forever.

## 5. Validate before reporting

Scripted, never by eye:

- `lib/release/VERSION` == CMakeLists' `VERSION` == ROADMAP's **In progress** header == the backlog's earliest version table — one number, four places.
- The old number survives only in historical contexts (Shipped sections, MILESTONES, dated decisions) — grep and judge every remaining hit.
- The backlog index still validates (rows ↔ docs, Version cells match directories, sequences ascend, gates resolve) and the full link sweep passes (the two illustrative MILESTONES lines are known false positives).

## 6. Report

Tell the user: old number → new number and which kind of roll it was; every file touched; the state swap in ROADMAP (what moved to Shipped, what became In progress); what the gate found (leftover rows, missing records) and how it was resolved or why it blocks; what remains theirs — creating the new branch, merging the PR that makes CI publish, and the release notes.
