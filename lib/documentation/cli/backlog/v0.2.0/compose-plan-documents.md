# The composer plans on paper: a dry-run for new briefs, a structured edit diff

**What / why.** The design's compose flow shows the plan before anything renders and an edit's changes scene by scene — and both are one document short, per the contract board: **(G12)** `video compose "<brief>" --dry-run` does not exist for a *new* brief (`--dry-run` ships only with `--from`), so "a new composition planned before anything renders" has no source and the app's fallback is plan-and-render-in-one-run with Stop; **(G13)** the edit's `--dry-run` prints the edited project, but the scene diff is stderr prose and a scene does not name its source, so the app would diff two documents by hand. This item closes both, on the shipped composer's own machinery (50c's schema-held plan, 50d's document rewrite): `--dry-run` on a new brief runs **the plan call alone** — the schema-held scene plan, priced and said exactly as a real run's plan print — and writes the planned project document, rendering nothing; and the edit dry-run's document gains **per-scene provenance**: each scene's `from` (its source scene in the parent, or `new`) and `changes` (which of narration, prompt, seed, duration, asset differ) — the same record comparison 50d's selective re-render already computes, surfaced as data instead of prose.

**Core constraint(s).** **No second planner and no second differ**: the dry-run plan is 50c's one member call; the `from`/`changes` fields are read off 50d's own record comparison — the printed prose and the document fields come from one computation. **Spend honesty stands**: a new-brief dry-run makes exactly one model call (the plan) and says so; nothing renders, nothing is touched on disk beyond the document the user asked for. **Additive**: the project document family gains fields (versioned per its schema's law); existing documents parse unchanged. The flags complete (ADR 0007); classification unchanged (flags on the one verb); machine-mode.md's compose rows grow (28h).

**Seam + files.** `business/video/compose.h/.cpp` (the plan-only walk; the diff fields beside the comparison), `data/contracts/` (the project schema's additive fields), `presentation/cli/video_cmd.cpp` (`--dry-run` without `--from`), `machine-mode.md`. Tests (fake drivers, scripted planner): the new-brief dry-run writing the planned document and touching nothing else (one plan call, pinned); the edit dry-run's `from`/`changes` exact against the prose diff's cases (the 50d matrix reused); schema conformance; old documents parsing.

**Reference.** 50c/50d (shipped — the plan call, the record comparison, the document family consumed whole); the contract board's G12/G13 rows and fallbacks; 26f — the plan's hold.

**Decisions made** (dated):
- 2026-10-20 — Specced from the GUI design spike, the user's call; G12 before-v1 per the design's recommendation.
- 2026-10-20 — One computation per fact: the plan call and the record comparison are reused, never reimplemented for the document.

- 2026-10-20 — **The `from`/`changes` spellings** at build, within the family schema's versioning (the default, confirmed at grooming).

**Open calls:** none — groomed 2026-10-20.

**Guardrail(s).** The plan-only pin (one model call, no render, no store writes); the diff-field equality with 50d's matrix; schema conformance and old-document parsing; no network, no weights.

**Acceptance criteria:**
- [ ] `video compose "<brief>" --dry-run` writes the planned project document — spend said, nothing rendered; the same brief without the flag renders that plan.
- [ ] The edit dry-run's document names each scene's `from` and `changes`, matching the printed diff case for case; the full suite is green.

**Scope note.** Earmarked for v0.2.0 (track 52); ungated (50c/50d shipped). Out of scope: any new plan or edit semantics, the GUI's rendering of these documents, G11's streaming (its own item).
