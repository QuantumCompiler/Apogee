# The design draws track 4's other additions

**What / why.** Track 4 and the GUI Maintenance run (shipped 2026-10-25/26: [Milestone E](../../assistant/MILESTONES.md#milestone-e--the-audits-gui-findings), and M5 in [Milestone D](../../assistant/MILESTONES.md#milestone-d--mlx-kinds-in-the-app)) grew the app's existing screens with states and controls the design never drew. Each item recorded it under "Not done", and 4l listed them together. Training's two new tabs are their own item, [M11](design-draws-pipelines-and-cycle.md); this item is everything else, by place:
- **Sessions:**
  - the keep-or-end question before a switch ends a private conversation (4a);
  - the strip saying which lines wait for the turn to end, a card closed by its turn ("not answered: the turn ended first"), and the read's warning row above the earlier turns (4b);
  - the suite chooser and the header's suite name (4i);
  - a saved chat's Details… and Compact…, and the inspector's Recall section with Forget (4k).
- **Models:**
  - each tab's delete confirm showing the CLI's plan, and the narration card after (4e);
  - a backend's adapters and their sheet (4i);
  - the kind beside the format, the drafter glyph, Pair a drafter…, and a pairing that can no longer draft (M5).
- **Media:**
  - the install card running, failed and done (4f's shared `InstallCard`);
  - compose's voice-model picker, its captions line, a plan toasted "Planned", the edit preview with Stop and its limit, and skipped projects listed (4f).
- **Settings:**
  - General's passed-over override (4c);
  - Health's skipped rows with their remedy (4f);
  - a provider row's Register…, and a backend's `server_tools` (4k).
- **Training and Suites:**
  - adapter promote mode and its refusal reopened, and datasets Pull… (4i);
  - the Suites fit as the CLI's verdict, Open a chat under it, and Switch the session to it (4i).
- **Library, Tasks, Knowledge, Review and Graph:**
  - the editor while saving, a failed read said, and an empty state with no binary (4h);
  - an interrupted task's Resume line (4h);
  - the target sheet's lenses (4h);
  - MCP Remove… and a record's raw conversation (4k);
  - Graph's More menu: export, the report, dedupe and delete (4k).

The design is the reference every app is built from (`lib/design/spec/CLAUDE.md`). Each of these is an app state a Linux or Windows build would have to rediscover.

**Core constraint(s).**
- **The design's own law** (`lib/design/spec/CLAUDE.md`):
  - **The repo and the canvas move together:** read the canvas first, publish each changed file, read it back identical.
  - **A screen, flow or copy change** goes in `html/main.html`, and its screen boards follow.
  - **A new state worth its own board** gets its start preset, its `html/Screen<Name>.dc.html`, its `json/canvas.json` entry and its README row.
  - **Every part maps to the CLI,** in the CLI's words, every colour is a token, keyboard first, copy in plain words.
- **Draw what the app does, from the recordings.** Every word on a new state is the binary's, from the recorded documents its item kept under `lib/src/darwin/tests/**/Fixtures/`, never invented.
- **The contract board stays true.** A new part whose command is already mapped needs no row. A part resting on a gap the GUI runs found (the CLI's [M29](../../../cli/backlog/maintenance/refusals-and-suites-reach-drivers.md)–[M33](../../../cli/backlog/maintenance/config-verbs-finish-their-job.md)) names it, as the ledger draws gaps.

**Seam + files.**
- **`lib/design/spec/html/main.html`:** each place's markup block and its `renderVals()` section (`chat`, `ins`, `mdl`, `vid`, `comp`, `vox`, `trn` and the rest) gain the states above, with canned data from the recordings.
- **New boards,** per the open call below, with their starts, `json/canvas.json` entries and README rows.
- **`html/BuildContract.dc.html`:** only where a new part rests on a command it doesn't yet map.
- **`html/ComponentsMedia.dc.html`** gains the install card, if it is drawn there as the media components are.

**Reference.**
- Every app state named above has a named `#Preview` in its module, inside `#if DEBUG`, over the module's `Previews/` fakes (ADR 0004). They are the drawn reference of what the app shows.
- The design's recent precedents for growing an existing screen: 2h's store boards (canvas version 42) and 1q's board corrections, in [Milestone C](../../assistant/MILESTONES.md#milestone-c--cli-parity) and [Milestone B](../../assistant/MILESTONES.md#milestone-b--the-designs-screens).

**Decisions made** (dated):
- 2026-10-26 — **Opened from the GUI v0.2.0 and Maintenance runs' records** (the user's call: "spec them into the backlog"; placed in GUI Maintenance, the user's call the same day). Training's tabs are split out as M11, since they are new boards rather than states of existing ones.

**Open calls:**
- [default: three new boards: the install card running (media), the delete confirm showing its plan (models), and the suite chooser (session). Every other state is drawn in `main.html` and reached through the prototype.]
- [default: if the build runs past one focused session, it ships place by place in that order (sessions, models, media, settings, training and suites, then the rest), the item's document recording what landed, and is not split into new items.]

**Guardrail(s).**
- **The design's health check** (every start screen for 30 ticks): no problems, the new starts among them.
- **The canvas law:** each changed file read first, published and read back identical.
- **Words checked against the recordings:** each new state's words appear in its item's recorded documents.
- **The contract board's counts** still match its chips, wherever it changed.

**Acceptance criteria:**
- [ ] Every state listed above is drawn in the prototype, in the CLI's words, reachable from its place.
- [ ] The three new boards exist, on the canvas and in the repo, identical.
- [ ] The health check passes, and the contract board stays consistent.

**Scope note.** GUI Maintenance, claimable by name, no gate; design only, with no app code. Its sibling is [M11](design-draws-pipelines-and-cycle.md), Training's tabs. States the CLI's M29–M33 would change, such as a typed footprint or a single-provider register, are drawn as the app draws them today, and redrawn when those ship.
