# Edit by instruction: the project rewritten, scenes re-rendered selectively

**What / why.** The market leader's signature feature, consistently reported across its reviews (the composer spike, 2026-10-19): editing a finished video by **typed command** — "change the accent", "remove scene 2", "add an intro" — instead of a timeline. Apogee's composer makes that cheap because the project document is the only state ([`video-compose.md`](video-compose.md)) and selective re-rendering is already the law (49d, shipped: runs seeded from records, seeds held, provenance chained). This item is the loop over a project: **`apogee video compose --from <project.json> [--edit "<instruction>"] [--redo-scene <n>] [overrides…] -o out.mp4`** — `--edit` runs **one schema-held model pass** that rewrites the project document (scenes added, removed, reordered, narration or prompts changed — structure validated, never a free rewrite), the **diff is printed scene by scene** before anything renders, and then only the scenes whose records changed re-render (seeds held per 49d's law; an untouched scene's clip and narration are reused byte-for-byte); `--redo-scene <n>` is the deterministic knob beside the natural-language door (that scene re-rolled on a fresh seed); assembly always re-runs (it is cheap and the output is one file). The edited document chains to its parent, so a final traces through its edits to the original brief.

**Core constraint(s).** **The document is the only state** (`(consumed decision)` — the composer owns the schema); the edit pass is structured output (26f) against that schema with the one corrective retry, and a pass that fails validation changes nothing, said. **The diff is shown before any render** — scene-level, plain words (added / removed / narration changed / prompt changed / untouched); `--dry-run` prints the edited document and the diff and renders nothing. **Selective re-render is record comparison**, never heuristics: a scene re-renders exactly when its narration text, visual prompt, asset, seed or duration target differs from its record; seeds hold through an edit (the 49d canonical move — same seed, new prompt) unless `--redo-scene` re-rolls one. Finished records are never deleted by a failed edit run; cancellation keeps them (the composer's law). Caps and spend honesty are the composer's, re-applied to the edited plan before rendering. Every mode: the flags on the one verb (completion free), the document chaining per the family schema, machine-mode.md's section grown.

**Seam + files.**
- `business/video/compose.h/.cpp` — the edit pass (the schema-held call over a closure), the scene-level diff, the record comparison driving the re-render set; the chaining fields.
- `presentation/cli/video_cmd.cpp` — `--from`/`--edit`/`--redo-scene` on `compose`, the diff print, `--dry-run`; the parity row's wording.
- Tests (fake drivers and a scripted editor model): the edit matrix (add / remove / reorder / narration change / prompt change / no-op — each diff golden, each re-render set exact); a failed validation changing nothing; seeds held through edits and re-rolled by `--redo-scene`; reuse proven byte-for-byte on untouched records; chained documents conformant; `--dry-run` rendering nothing.

**Reference.** [`video-compose.md`](video-compose.md) — the document and walk consumed whole; 49d (shipped) — the seed-hold and chaining law; 26f — the structured edit's hold; the composer spike (2026-10-19) for the feature verification.

**Decisions made** (dated):
- 2026-10-19 — The set lands in v0.1.9 as track 50 (the user's placement call on the spike report).
- 2026-10-19 — Edit-by-instruction is **a document rewrite plus record comparison** — no timeline, no session, no new render path; the natural-language door and the deterministic knob ship together.
- 2026-10-19 — The diff always prints before a render; a failed or invalid edit changes nothing and says so.
- 2026-10-19 — **Diff wording, chaining shape, and flow**: the diff's exact wording and the chained `edits` field shape land at build within the family schema; interactive runs proceed after the diff print — local compute on the user's own ask — with `--dry-run` the no-render door (the default, confirmed at grooming).

**Open calls:** none — groomed 2026-10-19.

**Guardrail(s).** The edit matrix with golden diffs and exact re-render sets; validation-failure inertness; seed-hold and re-roll pins on the argv; byte-for-byte reuse of untouched records; chain conformance; no network, no weights — the real edited-video pass is the user's run, recorded.

**Acceptance criteria:**
- [ ] `--edit` rewrites the project through the schema-held pass, prints the scene diff, and re-renders exactly the changed scenes; untouched clips and narration are reused byte-for-byte.
- [ ] `--redo-scene` re-rolls one scene's seed; everything else holds; assembly re-runs and the output replaces atomically.
- [ ] A failing edit changes nothing, said; `--dry-run` renders nothing; documents chain to their parents.
- [ ] The full suite is green over the fakes.

**Scope note.** Earmarked for v0.1.9; gated on [`video-compose.md`](video-compose.md) (50c). Out of scope: a timeline or TUI editor (the recorded later question with the interactive session), multi-project operations, any new render machinery.
