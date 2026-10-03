---
name: apogee-backlog-swap-version
description: Slide Apogee's queued releases by one version — insert a new version target ahead of the queue (bumping later versions, their directories, track prefixes and every reference up), or remove a vacated version (sliding everything back down). Use when the user says "this goes as v0.1.X, bump the rest", "renumber the queue", or "collapse that version".
---

# Swap a version into (or out of) the Apogee backlog queue

The backlog index keeps one table per queued release, a directory per table, and a track prefix per release (27a, 27b… under v0.1.4; 28a… under v0.1.5). Inserting a new release ahead of queued ones — or removing one — means **everything at and above the affected version slides by one**: version strings, directories, track prefixes, and every reference in every document. Numbers are identities and a renumber is a deliberate act, so **this skill runs only on the user's explicit instruction**, and the result records that it happened, dated, as the user's call.

## 1. Ground in the live state first

Parallel sessions build and ship while you work. Before touching anything:

1. Re-read [`lib/documentation/backlog/README.md`](../../../lib/documentation/backlog/README.md) and check `git status` — know which items are 🚧, whether recent ships landed, and that the tree is committed enough for `git mv` (fall back to plain `mv` for untracked files; both move, only tracked ones carry history).
2. **Survey every reference before replacing any.** `grep -rln "v0\.1\.[4-9]"` (the affected range) across `lib/documentation/` to list the files in scope; confirm **nothing has shipped** under any affected version — only forward-looking references may be blanket-bumped.
3. **Find the bare track-number mentions by hand.** Lettered ids (`27a`) are regex-safe; bare ones (`**27, machine-mode integrations**`, "backlog item 27, for", "the number freed by …") are not — and **never blind-replace bare numbers**: `\b28\b` matches the 28 in `2026-09-28` (this has caused real damage). Grep for them, list them, and replace each as an exact longer string.
4. **Removal direction only:** verify the numbers being slid *down onto* have nothing shipped under them — grep MILESTONES for the target prefixes. A number with shipped work recorded is never reused; if one is in the way, stop and tell the user.

## 2. Move the directories — order prevents collisions

- **Insert** (releases slide up): rename **top-down** — `git mv v0.1.7 v0.1.8`, then 6→7, 5→6, 4→5 — then `mkdir` the vacated directory for the new table.
- **Remove** (releases slide down): the vacated directory must already be empty (its items shipped, cancelled, or moved — that happened before this skill); delete it, then rename **bottom-up** — `git mv v0.1.5 v0.1.4`, then 6→5, 7→6, 8→7.

## 3. The scripted replace — ordered, word-bounded, case-sensitive

One Python pass over the surveyed files (all backlog `*.md` plus `assistant/{ROADMAP,MILESTONES,SPEC,CLAUDE,DEVELOPER}.md` — the last two are usually no-ops but cheap to include). Apply replacements **in the order that prevents cascades**: descending when bumping up (`\b30([a-f])\b → 31\1` before `29→30` …; `v0\.1\.7\b → v0.1.8` before `6→7` …), ascending when bumping down (`28x→27x` before `29x→28x`). Three pattern classes:

1. **Lettered items:** `\b<N>([a-f])\b`, case-sensitive — the letter suffix is what keeps dates and model names (`Qwen3.8-27B`) safe.
2. **Version strings:** `v0\.1\.<N>\b` — this also rewrites every link path (`v0.1.4/machine-handshake.md` → `v0.1.5/…`), matching the directory moves exactly.
3. **The hand-surveyed bare mentions:** each as its own exact string (`**27, machine-mode integrations**` → `**28, …**`).

A track whose document is deliberately unwritten (a "its document is still to be written" row) renumbers **by reference only** — authoring it is never a side effect of a swap.

## 4. What the script cannot do — the judgment edits

- **Narratives that the renumber makes false.** Any "(the number freed by …)" or "asked for … for vX" story must be re-read after the pass and rewritten to stay true (e.g. "as track 30; renumbered 31 the same day when X took v0.1.4 — nothing has shipped under either number").
- **Record the renumber itself**, dated, where the queue's story is told: the new (or removed) track's preamble paragraph states that the former tracks N–M and versions vA–vB became N+1–M+1 and vA+1–vB+1 (or the reverse), numbers bumped with their directories and references, the user's call.
- **The index:** insert the new version's table at its release-order position (or remove the vacated one), with its preamble; every other table keeps its rows in order — a swap renumbers, it never reorders.
- **ROADMAP:** insert the new release's section at the right spot under **Up next** (or remove it); the scripted pass already bumped the surviving section headers and links.
- **MILESTONES is edited only where it references pending work** — forward-looking lines like "backlog item 27, for v0.1.4" or a shipped spike's description of its pending split. Shipped history is never rewritten beyond those references.
- **SPEC** usually needs only what the script already did (targeted-version mentions in scope bullets). Read its diff and confirm it is exactly that.

## 5. Validate before reporting

Run the standard routine (a throwaway script, not by eye):

- Parse every index table: rows ↔ documents on disk, both directions; each File link's display name equals its basename; **each row's Version cell equals its directory**.
- Sequences ascend: A*, M*, and the lettered items across the version tables.
- Every 🔒 gate names a real pending id.
- Full relative-link sweep over the backlog, README, ROADMAP, MILESTONES, CLAUDE, DEVELOPER (the two illustrative `](url)` / `](absolute URL)` lines in MILESTONES are known false positives).
- Stray-reference greps for the old paths (`v0.1.4/machine-`, `v0.1.5/task-`, …): zero hits.

If an Edit fails because a parallel session touched the file, re-read the live region and re-anchor — never force the stale version over it.

## 6. Report

Tell the user: the version mapping (old → new, per release), the prefix mapping (old track → new), how many files the scripted pass touched, every hand edit (especially rewritten narratives and the recorded renumber note), what changed in MILESTONES/SPEC and why it was only that, and the validation results. If the swap was the insert direction and the new table's documents don't exist yet, say so — authoring them is `/apogee-backlog-create-item`'s job, not this skill's.
