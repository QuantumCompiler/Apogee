# Video iteration: the next run, seeded from the last

**What / why.** One clip is never the clip: the workflow every video harness lives on is the loop — regenerate with one thing changed, extend the shot, preview cheap and render the winner — and track 49's first cut was strictly one-shot (the user's finding, 2026-10-19). The mechanics are nearly free in this house because the pieces already stand: `--image` image-to-video ([`video-generate.md`](video-generate.md)), last-frame extraction through the `platform/ffmpeg` seam, and — the amendment that landed with this item — **every run's reproducible record**, the sidecar document carrying the model, settings, seed and device. This item is the loop over that record, three moves on one verb:

- **`video generate --from <run-document-or-clip> [overrides…]`** — a new run seeded from a previous one: the sidecar (named directly, or found beside a named mp4) supplies every setting, **the seed held fixed unless `--seed` changes it**, any flag overriding its recorded twin (`--prompt "…but at dusk"` the canonical move). The new run's document chains to its parent.
- **`video generate --extend <clip.mp4> "<prompt>"`** — continuation: the clip's **last frame** extracted through the ffmpeg seam and fed as the image seed to the I2V path — the standard way longer videos are built shot by shot; the document records the parent clip.
- **`--draft` / `--final`** — the preview loop: two named preset pairs (steps × resolution) over **the same seed**, so a cheap draft that moves right becomes the full-quality render of *that* clip, not a new roll.

**Core constraint(s).** **Iteration is a new run, never session state**: no daemon, no interactive mode, no new driver protocol — `--from` is document-reading sugar over the one verb, pipe-scriptable, and the runtime (49b ([shipped, Milestone AM](../../assistant/MILESTONES.md#milestone-am--video-generation))) is consumed untouched. **The seed is held by default** on `--from` and `--final` (the point of the loop); overriding it is explicit. **Determinism honesty is the amendment's** (`(consumed decision)` — [`video-generate.md`](video-generate.md)): same machine and pinned versions, the device named in the document; `--from` a document recorded on another device proceeds and says so. **Provenance chains**: each document names its parent (document path or clip), so a final render traces to its drafts; chains are data in the documents, nothing stored elsewhere. The caps are the verb's (`(consumed decision)`), drafts under them by construction; `--extend` honors the I2V path's rules whole and the frame extraction is the seam's captured child. Every mode: the overridden-settings resolution is printed before the run (what came from the document, what the flags changed), the exec line covers the flags (ADR 0010), machine-mode.md's document section grows the parent fields.

**Seam + files.**
- `presentation/cli/video_cmd.h/.cpp` — `--from` (the sidecar reader, the override merge with the seed rule, the printed resolution), `--extend` (last-frame via `platform/ffmpeg`, then the I2V path), `--draft`/`--final` (the preset pairs as named constants beside the caps).
- `data/contracts/` — the run document's schema grows `seed` (the amendment's), `parent` and `preset`; the sidecar reader one function the verb and tests share.
- Tests (over the fake driver, as the siblings): the `--from` matrix (document direct, clip-with-sidecar, missing sidecar refused naming it, every override precedence case, the seed held and the seed overridden); `--extend`'s frame extraction over a fixture clip and a fake ffmpeg, the I2V argv contract; draft→final over one seed byte-equal argv but the preset pair; chained documents conformant; the printed resolution golden.

**Reference.** [`video-generate.md`](video-generate.md) — the verb, the caps, the amended reproducible record, all consumed; 49b ([shipped, Milestone AM](../../assistant/MILESTONES.md#milestone-am--video-generation)) — the driver untouched; `platform/ffmpeg` (26e) — the extraction seam; the video-generation spike (2026-10-18) and the user's iteration question (2026-10-19) — the loop most video harnesses carry, built here as document-seeded runs.

**Decisions made** (dated):
- 2026-10-19 — The item joins v0.1.9 as 49d (the user's call: amend 49c, spec the loop as its sibling), gated on the verb.
- 2026-10-19 — Iteration is **a new run seeded from a run document** — no session, no state store, no new protocol; the three moves are `--from`, `--extend`, and the draft/final presets.
- 2026-10-19 — The seed holds by default on `--from` and `--final`; changing it is explicit. The interactive video session (a TUI view, a timeline) stays the recorded later question.
- 2026-10-19 — The sidecar's name and the `parent`/`preset` field shapes are one schema the verb and the loop share, settled at build; `--from <clip>` requires the sidecar beside it and refuses by name — settings are never guessed from pixels (the default, confirmed 2026-10-19).
- 2026-10-19 — The draft/final preset values are named constants beside the caps, tuned at build against the reference pipeline; the two flags are mutually exclusive and each names its preset in the printed resolution (the default, confirmed 2026-10-19).


**Guardrail(s).** The override-precedence table exhaustively (document < flags, seed's hold-by-default proven both ways); the missing-sidecar and cross-device cases said; `--extend`'s extraction and I2V contract over fixtures; draft→final seed equality pinned on the argv; chained-document conformance; the printed resolution golden; no network, no weights, the fake driver throughout.

**Acceptance criteria:**
- [ ] `--from` reruns a recorded run with overrides, seed held unless changed, the new document chaining to its parent; a clip finds its sidecar or refuses naming it.
- [ ] `--extend` continues a clip from its last frame through the I2V path, recorded as the parent.
- [ ] `--draft` then `--final` render the same seed at the two presets; the resolution print says what came from where.
- [ ] Documents and machine-mode.md carry the new fields; the full suite is green over the fake driver.

**Scope note.** Earmarked for v0.1.9; gated on [49c](video-generate.md) (the amended verb and its record). Out of scope: an interactive session or TUI view (the recorded later question), multi-clip stitching beyond one `--extend` hop, audio, any runtime change.
