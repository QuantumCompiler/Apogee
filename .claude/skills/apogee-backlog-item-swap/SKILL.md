---
name: apogee-backlog-item-swap
description: Move an Apogee backlog item between index tables — a release table to a standing queue (Maintenance/Architecture), a standing queue back into a release, or release to release — re-identifying it for its destination, closing the number gap it leaves, git-mv'ing its document, and sweeping every reference. Use when the user says "move M4 into v0.1.3", "make 26n a maintenance item", "pull that item into this release".
---

# Swap a backlog item between tables

The backlog index gives every item an identity shaped by its table: release items carry their release's track prefix and a letter (`26q`, `28c`), standing-queue items carry `M<n>` or `A<n>`. Moving an item between tables therefore means a **re-identification**, not just a moved row: a new id from the destination's sequence, the gap in the source's sequence closed, the document `git mv`'d into the destination's directory, and every reference — index, preambles, ROADMAP, the documents' own scope notes and cross-links — updated. Ids are identities and a renumber is a deliberate act, so **this skill runs only on the user's explicit instruction**, and the result records the move, dated, as the user's call.

## 1. Ground in the live state first

Parallel sessions build and ship while you work:

1. Re-read [`lib/documentation/backlog/README.md`](../../../lib/documentation/backlog/README.md) and check `git status` — know which items are 🚧 (an in-flight document gets id-reference edits only, nothing else; moving a 🚧 item itself is worth flagging to the user first), and whether recent ships changed the tables since you last looked.
2. **Survey every reference before editing**: grep the moving item's id and its filename across `lib/documentation/`, and the ids of every trailing item in the source table (they will slide). List the hits; you will edit each as a targeted string.
3. **Bare ids are landmines.** `M3`/`M4` match the **M3 Max chip** in MILESTONES' performance records; bare numbers match dates (`\b28\b` hits `2026-09-28`) and model names (`27B`). Never blind-replace an id — every occurrence is replaced as part of a longer exact string, or hand-verified first.

## 2. The new id and the position — the destination decides

- **Into a release table:** the item takes the release's track prefix and the **next letter ever assigned** in that track — shipped letters count and are never reused (26m shipped, so after 26q comes 26r).
- **Into Maintenance / Architecture:** the next `M<n>` / `A<n>` ever assigned, same rule.
- A next-in-sequence id naturally **appends at the end of the table**, which keeps rows ascending. If the user wants the item higher in the build order than its id allows, that is a wider renumber of the destination's rows — confirm it with them before doing it.
- Gates are table-agnostic (they name ordering among pending items wherever they sit): the moved item keeps its gate, and any item whose gate names the moved id gets the new id.

## 3. Close the gap the item leaves

Trailing items in the source sequence slide down one (M5→M4, M6→M5; or a release tail re-letters, the 26n/26o → M1/M2 precedent) — **only over ids nothing ever shipped under**; a shipped id is permanently spent (grep MILESTONES to confirm, minding the false positives above). Each slid item needs the same sweep as the moved one: index row, ROADMAP link label, its own scope note — where the old id is kept as dated history, "(M6 until 2026-10-03's re-number)" style, never silently erased.

## 4. The move mechanics

Per the index's own moving-rows rule, all of a piece:

1. **The document:** `git mv` into the destination table's directory (plain `mv` for an untracked file). Fix its links for the new location: same-table links become bare filenames, cross-table ones `../<table-dir>/<file>`; out-of-backlog links (`../../assistant/…`) keep their depth — every table directory sits at the same level.
2. **The document's content:** the scope note gets the new identity and the destination's wording — `earmarked for v0.1.<x>` with its position, or `Maintenance item M<n>; claimable at any time by name` — and **Decisions made** gains a dated entry recording the move, the new id, who called it, and what it re-numbered.
3. **The index:** the row moves to the destination table (new id, new Version cell — `v0.1.<x>`, `maint.`, or `arch.`), the source table's trailing rows re-id, and **both tables' preambles** tell the story (the precedents: "moved to the Maintenance table as M1 and M2 …, re-lettering this tail"; "specced into Maintenance as M4 … and moved here later that day").
4. **ROADMAP:** the item's line moves between sections (a release board ↔ the Maintenance/Architecture section), restyled to the destination's line conventions; slid items' link labels update in place.
5. **MILESTONES** changes only if it carries a forward-looking reference to a moved or slid id (rare for item moves); shipped history is never rewritten.

## 5. Validate before reporting

The standard scripted routine, never by eye:

- Index tables parse; rows ↔ documents on disk both ways; each row's Version cell matches its directory; File display names equal their basenames.
- Every per-table sequence ascends and the source's gap is closed (M-, A-, and each release's letters).
- Every 🔒 gate names a real pending id — including gates that named the moved item.
- Full relative-link sweep (the two illustrative `](url)` / `](absolute URL)` lines in MILESTONES are known false positives).
- Stale greps: the old id appears only inside deliberate dated history notes; the old `<table-dir>/<file>` path appears nowhere.

If an Edit fails because a parallel session touched the file, re-read the live region and re-anchor — never force the stale version over it.

## 6. Report

Tell the user: old id → new id and where the row landed; which trailing ids slid and to what; the document's new path; every file touched (index, ROADMAP, each doc), the dated history notes recorded; anything deliberately untouched (an in-flight 🚧 document, MILESTONES' shipped history); and the validation results.
