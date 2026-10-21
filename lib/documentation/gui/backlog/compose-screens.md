# Compose: a brief to a video, edited by instruction

**What / why.** The design's Compose screens (`ScreenCompose`, `…New`, `…Edit`, `…Stopped`): a new composition from a brief, scene-by-scene rendering progress, stop and resume, and edit-by-instruction with a scene diff. The contract board's map: the composer and its whole edit vocabulary are **ready** (`video compose` with `--from`, `--edit`, `--redo-scene`, `--voice`, `-m`, `--music`, `--no-captions` — 50c/50d shipped), **scene progress is app-side** (the `video.project` document beside the output, saved after every step — the screen re-reads it), resume is the same command again (ready), and two fallbacks stand until the CLI's 52c lands: a new brief **plans and renders in one run with Stop** (G12), and the edit diff is **compared documents** needing provenance (G13). This item builds the screens over the jobs core and the project document: the new-composition sheet (brief, assets folder, music, captions toggle), the running view reading the project document per scene, stopped/resume, and the edit flow (instruction in, the diff rendered — from 52c's `from`/`changes` fields when present, the board's compare-fallback until then).

**Core constraint(s).** ADR 0001/0002; **the project document is the progress** — the screen re-reads the document the composer saves, never a parallel accounting; every run is the person's command, argv-pinned; spend honesty rendered as the CLI says it (the plan print with its scale); stop keeps finished scenes (the composer's law, shown truthfully: a stopped composition lists what stands).

**Seam + files.** `source/presentation/compose/` (the four screens; view-models), `source/business/media/` grown (project-document reads, compose run assembly). Tests: the running view over fixture project documents evolving per scene; argv pins (new, edit, redo-scene, resume); the stopped/resume states; the diff render over 52c-shaped fixtures and the compare-fallback; hermetic.

**Reference.** The `ScreenCompose*` boards; the contract board's VIDEO AND VOICE rows (G12/G13 fallbacks); 50c/50d `(consumed decision)` — the documents and flags; [52c](../../cli/assistant/MILESTONES.md#milestone-ao--the-desktop-clients-cli-growths) (shipped 2026-10-20) cross-referenced, not gated.

**Decisions made** (dated):
- 2026-10-20 — From the design-completion spike (the user's call); the project document is the one progress source (the design's app-side row, standing).

**Open calls:**
- [default: the scene card's fields and the diff's rendering per the prototype; details at build.]

**Guardrail(s).** Project-document matrices (fresh, mid-run, stopped, resumed, edited); argv pins; diff renders both shapes; hermetic — the real composition is the user's run.

**Acceptance criteria:**
- [ ] A brief composes with scene progress read from the project document; stop keeps finished scenes and resume fills gaps, both shown truthfully.
- [ ] Edit renders its diff (52c fields or the compare-fallback) and re-renders selectively via the real flags; the full suite is green.

**Scope note.** Earmarked for v0.2.0; gated on 1m, video clips ([shipped 2026-10-20](../assistant/MILESTONES.md#milestone-b--the-designs-screens)) (1m — the media business layer and jobs idioms it extends). Out of scope: the CLI's plan documents (52c), voice (1o), a timeline editor (the design has none).
