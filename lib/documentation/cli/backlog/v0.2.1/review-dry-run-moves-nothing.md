# A dry-run review sweep moves no watermark, so what it previewed is still posted later

**What / why.** `review sweep --dry-run` is meant as a preview: "Review and keep the documents; post nothing" (`presentation/cli/review_cmd.cpp:685-686`). It is also the macOS app's default sweep: the Review place's "Sweep (dry run)" is the sheet's default action (`ReviewScreen.swift:511`), and `sweepDry()` (`ReviewViewModel.swift:311-315`) runs `review sweep … --dry-run` (`SweepForm.arguments`, `ReviewViewModel.swift:110`). But a dry run saves the watermarks exactly as a posting sweep does. So previewing a target uses up the merge requests it previewed. The posting sweep or cron run that follows starts past them, reports nothing new, and never posts those reviews. Found by the second bug sweep (2026-10-31, finding 90) and re-read at HEAD the same day:

1. **A dry run advances and saves every repository's watermark.** `run_sweep` calls `store.set` with no condition at all four places that move a mark: a first-seen repository seeded from `--since` (`business/review/sweep.cpp:138`), or at now (`:141`), a skipped draft (`:162`), and each reviewed merge request (`:184`, `store.set(key, item.updated_at);`). `WatermarkStore::set` (`sweep.cpp:47-55`) rewrites `watermarks.json` atomically on every call. `SweepOptions::dry_run` (`sweep.h:81`) is read nowhere in `run_sweep` except the output document (`{"dry_run", options.dry_run}`, `sweep.cpp:194`). The command builds one store at `harness::review_dir() / "watermarks.json"` (`review_cmd.cpp:520`) for dry and real runs alike, and passes `flags.dry_run` only to the options (`:527`) and to each review's `ReviewMrOptions` (`:553`), which is what keeps the dry run from posting.
   *Failure:* target `acme` has `post: true`, and the person's cron runs `review sweep`. In the app they press Sweep (dry run) to see what it would find. Merge requests #12 and #13 are reviewed and nothing is posted, but the watermark moves past both. They then press "Sweep and post…", and the cron runs later. Both sweeps start past #12 and #13, report `0 reviewed`, and never post those reviews. The reviews come back only when each merge request is next updated (a push or comment moves its `updated_at` past the mark), or when the person moves `review/watermarks.json` aside by hand.
   *Also:* a dry run over a repository seen for the first time seeds its mark for good: at now (so a later `--since` posting sweep no longer reaches back, since `--since` only seeds a first-seen repository), or at the dry run's `--since`.
   *Not designed:* the 48d record (`MILESTONES.md:8749ff`) describes the watermark advancing per merge request and says nothing about dry runs, and so do the `--dry-run` help and `machine-mode.md:988-1001`. `review/sweep_test.cpp` has no dry-run case (its four cases, `:159-274`, run posting sweeps only), and `cli.review_mr`'s sweep section (`tests/scripts/sh/review_e2e.sh:112-151`) never runs `--dry-run`.

What already works: a dry run posts nothing (each review runs with `dry_run`, `review_cmd.cpp:553`), its document says `"dry_run": true` and is kept under `review/sweeps/`, and the app asks before any sweep that posts.

**Core constraint(s).**
- **A dry run changes nothing a later run reads.** After `--dry-run`, `review/watermarks.json` is unchanged byte for byte, or still absent if it was. The sweep lock and the kept document are not part of this: the lock is released, and the document is the preview's whole point.
- **One sweep path.** The dry run goes through the same `run_sweep` loop as a posting sweep: the same listings, the same cap and drafts rules, the same `review_one`. It differs only in where the marks it moves are kept. A separate preview loop would drift from the real one.
- **Older versions keep working** (ADR 0006, [backwards-compatibility](../../adrs/backwards-compatibility.md)): the `review.sweep` document keeps its fields. `watermark_after` in a dry run reports where a posting sweep would leave the mark, and `machine-mode.md` says it was not saved.
- **Tests live in the layer of what they test** (ADR 0004, [tests-mirror-architecture](../../adrs/tests-mirror-architecture.md)) and stay hermetic: a temp `APOGEE_HOME`, the scripted forge and reviewer, no network.
- **Development runs on the dev channel**: any live check names `apogee-dev`, run under a scratchpad `APOGEE_HOME` with the e2e's scripted `gh`. It never uses the release `apogee`, `~/.apogee` or a real forge.

**Seam + files.**
- `source/business/review/sweep.h/.cpp`: `WatermarkStore` gains an in-memory copy (for example `[[nodiscard]] WatermarkStore preview() const`, a copy whose `set` keeps the mark and writes nothing). `run_sweep` works on that copy when `options.dry_run` and on the store itself otherwise, so the loop at `:121-190` is unchanged. The first-sweep progress line (`:144`) says what a posting sweep would do when the run is dry.
- `source/presentation/cli/review_cmd.cpp`: the `--dry-run` help (`:685-686`) adds that no watermark moves, and the first-sweep text line (`:593-594`) gets the same dry-run wording.
- Tests: `tests/business/review/sweep_test.cpp` (the new case), `tests/scripts/sh/review_e2e.sh` (`cli.review_mr`: a dry sweep before the posting one).
- Docs: `reference/machine-mode.md` (the sweep paragraph: a dry run's `watermark_after` is unsaved), and the 48d behavior in `SPEC.md`'s Code review line only if its wording implies otherwise (it does not today).
- The app needs no change: its "Sweep (dry run)" already passes `--dry-run`, and once this ships it is the preview it claims to be.

**Reference.** 48d, `review-sweep` (`MILESTONES.md:8749`): the store, the per-merge-request advance, and the decision that "to reach back on one already swept, move `review/watermarks.json` aside". This item keeps all of that for posting sweeps and takes dry runs out of it. 48c's `review mr --dry-run` is the precedent: reviewed, nothing posted, nothing remembered.

**Decisions made** (dated):
- 2026-10-31: Specced from the second CLI/GUI bug sweep (the user's call: every finding becomes a v0.2.1 item in its queue, CLI track 57, GUI track 6).
- 2026-10-31: One finding, one item, in the CLI queue. The sweep finding was filed against both queues, but the defect is entirely in `run_sweep`'s store handling. The app's dry-run button is correct, so no GUI item is opened.
- 2026-10-31: A dry run moving the watermark is a defect, not a design. Nothing in 48d's record, the help or `machine-mode.md` says a preview consumes what it previews, and the help's "post nothing" says the opposite.

**Open calls:**
- [default: the dry run works on an in-memory copy of the store inside `run_sweep`, keyed on `options.dry_run`, rather than the command opening a read-only store. The business layer then owns the rule, and `sweep_test` pins it without the binary.]
- [default: a dry run's `watermark_after` reports where a posting sweep would leave the mark (the copy's value), and `first_sweep` is still said. The document keeps its shape, and `dry_run: true` says none of it was saved.]
- [default: a dry run still takes `review/sweep.lock`. It reads the store a posting sweep writes, and one sweep at a time stays the rule.]
- [default: no migration. Marks already moved by past dry runs cannot be told apart from real ones. The MILESTONES record and the release note give the way back: move `review/watermarks.json` aside and sweep with `--since`.]

**Guardrail(s).**
- `sweep_test`: a dry run with a seeded mark over two merge requests on a `post: true` target reviews both, leaves the store file byte-identical, and the posting sweep after it reviews and posts the same two. A dry run over a first-seen repository, with and without `--since`, leaves `watermarks.json` absent. Its `watermark_after` equals what the posting sweep then saves.
- `cli.review_mr`: on the real binary, a `--dry-run --since 2026-10-01` sweep before the `--allow-post` one leaves `watermarks.json` absent. The posting sweep that follows still posts exactly once (`--method POST` counted as today).

**Acceptance criteria:**
- [ ] `review sweep --dry-run` leaves `review/watermarks.json` exactly as it found it, absent included, on every path: first-seen at now, `--since`, drafts skipped, merge requests reviewed.
- [ ] After a dry run, a posting sweep (the app's "Sweep and post…", or cron) reviews and posts every merge request the dry run previewed.
- [ ] A dry run's document still reports `watermark_before`, `watermark_after` and `first_sweep`, and `machine-mode.md` and the `--dry-run` help say a dry run saves no watermark.
- [ ] The full suite is green.

**Scope note.** Earmarked for v0.2.1 (track 57); ungated. Out of scope: posting-sweep watermark semantics (48d's per-merge-request advance, the posted review that touches `updated_at`), a `--since` that reaches back on a repository already swept, and any change to the app's Review place.
