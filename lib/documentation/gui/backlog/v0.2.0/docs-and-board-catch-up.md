# The GUI docs and the contract board catch up with what shipped

**What / why.** The CLI/GUI audit (2026-10-22) found the GUI's planning docs and the design's contract board describing an earlier state:

- **The contract board** (`lib/design/spec/html/BuildContract.dc.html`) marks only G9 and G17 closed. The gaps CLI track 52 shipped (G1–G8, G11–G16, CLI MILESTONES Milestone AO: :9087, :9136, :9178, :9210, :9261) still read as open with "Until then" fallbacks. The contract map (`:73`) states CLI limits that no longer hold. The header still says "22 gaps open" and "Checked against apogee 0.1.9" (`:48`, `:54`), and the first call (`:100`) recommends landing G1/G8/G9/G11/G12 before v1, though they shipped. A probe of the branch binary confirms the capabilities: the session event announces schema 2026-10-20 with `accepts` including detach/set/compact and `events` including memory/sources/job; `chat --help` has `--private`; `config set` has its table; `chats recall --forget` exists; `models pull` and `video compose` take stream-json. GUI MILESTONES:718 leaves flipping each gap to "each CLI ship's own docs pass", and that pass did not happen.
- **gui ROADMAP.md:80** keeps "The answer as Markdown … a deliberate later step" as an idea, though M8/M9 shipped it (`:71-72`).
- **gui SPEC.md** has three stale gates. `:25`'s TODO waits on the first spike's feature cut, which shipped as 1a–1q. `:37` says parity is held mechanically "once 2a lands", and 2a shipped 2026-10-20 (Milestone C). `:41`'s success-criteria TODO cites open/drive/cancel as examples, and 1c already shipped exactly those.
- **gui CLAUDE.md:26** names only Milestones A and B as shipped, missing C (CLI parity, 2a–2i, M2, M3) and D (MLX kinds in the app, 3a, 3b).

This item reconciles them. The board flips each shipped gap to landed after confirming it by probe against the current binary, updates its stats, map rows and calls, and republishes per its own repo↔canvas law (`lib/design/spec/CLAUDE.md`). The GUI docs state what shipped.

**Core constraint(s).** **The board's own law**: each flip confirmed by a probe of the current binary, edits made in the repo file and published to the canvas identically, `canvas.json` untouched (content-only), never sending `support.js` or the `.md` files. **Docs state facts as of the change**: SPEC's gates become what shipped, and its success criteria are written from shipped outcomes (the GUI's first release's real behaviors), not invented. **History stays history**: shipped milestones are cited, not rewritten.

**Seam + files.** `lib/design/spec/html/BuildContract.dc.html` (+ the canvas publish); `lib/documentation/gui/assistant/ROADMAP.md`, `SPEC.md`, `CLAUDE.md`. Checks: the design's node health check green after the edit; a link sweep over the GUI docs.

**Reference.** The design CLAUDE's update law; GUI MILESTONES:718 (gaps flip in ship docs passes); CLI Milestone AO (track 52's ships); [CLI 55i](../../../cli/assistant/MILESTONES.md#milestone-ar--the-audits-cli-findings) (the CLI docs' twin, shipped 2026-10-25); the CLI/GUI audit (2026-10-22).

**Decisions made** (dated):
- 2026-10-22: Specced from the CLI/GUI audit (the user's call). G14's flip lands here, and the app's use of it is 4f ([Milestone E](../../assistant/MILESTONES.md#milestone-e--the-audits-gui-findings), shipped 2026-10-25: the compose sheet reads the captions row, and Health draws skipped rows with their remedy).

**Open calls:**
- [default: SPEC's success criteria are written from three to five behaviors the shipped app demonstrably has (each traceable to a milestone), stated as observable outcomes.]

**Guardrail(s).** The design's health check; probe-backed flips; the link sweep.

**Acceptance criteria:**
- [ ] The board shows every shipped gap as landed with current stats, map rows and calls; the canvas matches the repo.
- [ ] gui ROADMAP, SPEC and CLAUDE.md state what shipped, with no stale gates; the health check and link sweep pass.

**Scope note.** Earmarked for v0.2.0 (track 4); ungated, small.
