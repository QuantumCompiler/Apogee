# Apogee GUI — Backlog

**The work queue: one document per work item.** This directory holds every GUI feature and fix that has been discussed and specced but **not yet built** — the layer between the loose ideas in [ROADMAP.md](../assistant/ROADMAP.md) and the shipped record in [MILESTONES.md](../assistant/MILESTONES.md). There is no separate TODO file: an agent takes an item from the index below and **implements it straight from its document**. (The CLI's queue is its own, at [`cli/backlog/`](../../cli/backlog/README.md); an item that needs a CLI change gets a sibling document there, gated both ways.)

## Lifecycle

1. A feature is **discussed in chat** and gets a line on [ROADMAP.md](../assistant/ROADMAP.md) (and a [SPEC.md](../assistant/SPEC.md) update if it changes product shape).
2. Once specced — seam, files, decisions, open calls — it gets **its own document here** (kebab-case filename, listed in the index below in priority order).
3. When an agent **takes** it (told explicitly, or via the bare-"Continue" convention: the topmost item whose gate is satisfied), the document **is** the working spec — confirm scope with the user first (the document's **Open calls** are the natural questions), mark its index row **in progress** (🚧), and build. One item at a time.
4. When it **ships**, the work is recorded in [MILESTONES.md](../assistant/MILESTONES.md) per the docs flow in [CLAUDE.md](../assistant/CLAUDE.md), and the document is **deleted, along with its index row** — a completed item must never remain in the backlog. On the same pass, update any gate the shipped work just satisfied.

**The standing invariant:** every document in this directory is pending work. If it's shipped (in MILESTONES.md), it's not here.

**Tags and gates** follow the CLI backlog's conventions: an open call is `[user]` (genuinely user-owned; blocks the build) or `[default: …]` (agent-decidable, with the recommendation recorded); a decision owned by another item is `(consumed decision)`; a row's Status is 🟢 workable, 🔒 gated on a named real item (no cycles), or 🚧 in progress.

## Document format

Every item document follows this shape (sections in order; omit one only when it's genuinely empty):

```markdown
# <Item title>

**What / why.** <The feature or fix in a paragraph: what it does for the user, and why it's worth building — evidence baked in.>

**Core constraint(s).** <The rules the design must not violate — invariants it touches, compatibility it must keep.>

**Seam + files.** <Where the change plugs into the codebase: the interfaces it extends and the files it touches.>

**Decisions made** (dated):
- <YYYY-MM-DD> — <a design decision and its rationale>

**Open calls:**
- <tagged questions, per the conventions above>

**Guardrail(s).** <What must be tested or checked so the change can't regress silently.>

**Acceptance criteria:**
- [ ] <observable outcome that means "done">

**Scope note.** <Status: unscheduled / earmarked for <version> / gated on <item> — and anything explicitly out of scope.>
```

## Index

Priority-ordered: the topmost item whose gate is satisfied is the next one to build.

**Track 1, the macOS application**, opened 2026-10-20 (the user's call, from that day's GUI spike: **Swift and SwiftUI**, macOS first, **unsigned ad-hoc** for now). The spike probed the real things and found the integration already cheap: the whole machine-mode contract works against the shipped v0.1.9 binary (hello, the capabilities-bearing session event, streaming, asks as `question` events, cancel — first event in ~1s), and a **~40-line Swift Foundation probe** drove it end to end (`session → thinking → question`) with no bridging layer; Xcode 27.0 is licensed and SwiftUI parses. The walls, measured: no GUI project or build wiring exists (`cicd.sh:40` `APPS=(cli)`; `changed.sh` knows only `cli`); ADR 0001 has no mechanical teeth until the first build; a `.app` inherits no shell `PATH` and `machine-schema.json` is not installed as a file (the binary prints it — verified). The split: **1a** the scaffold (SwiftPM, the four layers with the layering check at birth, the window, the `Apogee.app` ad-hoc bundle, `APPS`/`changed.sh`); **1b** the client core (discovery, the child, tolerant typed events, the session, the reads); **1c** the session window (the conversation rendered and steered, the mock-actor fixture); **1d** the pipeline (the darwin workflow beside the CLI's, the gated build, the packaged artifact). The CLI-scoped repo skills were mirrored for the GUI the same day, closing the spike's W4 without an item. Gated 1a → 1b → 1c, 1a → 1d. The one **[user]** call (1d: ship in releases from day one, or a CI artifact until 1c) **was answered the same day**: a CI artifact until the session window ships, release wiring its own later step. **Groomed 2026-10-20**: every call resolved; nothing in the track waits on a question. **1a shipped 2026-10-20** ([MILESTONES → Milestone A](../assistant/MILESTONES.md#milestone-a--the-macos-application)) as an **Xcode project** rather than the SwiftPM package it was groomed as (the user's call at the build): `Apogee.xcodeproj` with every module a framework target and its layering check, the ad-hoc-signed `Apogee.app`, `cicd.sh --app` and `changed.sh gui-darwin` -- which opens 1b and 1d. A new module in either is a framework target, its test bundle in the shared scheme, and its line on the layer card. **1b shipped 2026-10-20** ([MILESTONES → Milestone A](../assistant/MILESTONES.md#milestone-a--the-macos-application)): the client core -- the captured child, the tolerant event decoder over a recorded real stream, the session with its turn tracking, cancel and stop escalation, discovery and the reads -- which opens 1c. **1c shipped 2026-10-20** ([MILESTONES → Milestone A](../assistant/MILESTONES.md#milestone-a--the-macos-application)): the session window -- one chat session rendered from a SwiftUI-free transcript folded from its events, questions and permission asks answered natively, Stop and Esc as `cancel` -- which ends the user's 2026-10-20 "a CI artifact until the session window ships" wait on putting the app in releases: that wiring is not in 1d and needs its own item, on the user's word. **1d shipped 2026-10-20** ([MILESTONES → Milestone A](../assistant/MILESTONES.md#milestone-a--the-macos-application)): `.github/workflows/darwin.yml`, the app's own pipeline -- `what changed (darwin)`, then `build darwin` on the `xcode-27` image through `cicd.sh`, the ad-hoc-signed `Apogee.app` kept as the artifact `apogee-gui-darwin`. **Track 1 is complete**; its emptied table stays until the pre-merge docs pass retires it.

**Grown 2026-10-20, the design-completion split** (the user's call, from that day's spike over `lib/design/spec` — the complete front-end design): the prototype certified by its own health check (**29/29 start screens, no problems**), its contract board verified against the installed 0.1.9 (29 parts ready, 13 the app's own, 15 CLI gaps — each with its recorded fallback — plus two new gaps the spike's probes found, all owned by the CLI queue's track 52), and 26 of 29 screens unbuilt beyond the shipped session window. The split: **1e** the app shell (sidebar, routes, keys, overlays, the window law); **1f** the session library and inspector; **1g** session states and the input's reach; **1h** the models library and roles; **1i** downloads and the one supervised-jobs core; **1j** tasks; **1k** the monitor and suites; **1l** the code graph screen; **1m** video clips; **1n** compose; **1o** voice; **1p** the five settings panes (the three-theme Appearance setting superseding 1a's system-following default, the design's call); **1q** the design board's own corrections (macOS 27+ stands; the new gaps join its ledger). The fallback law binds the set: a screen builds on the contract board's stated fallback and upgrades when its CLI item ships — cross-referenced, **never gated on the CLI queue**. No **[user]** call blocks any item. **1e shipped 2026-10-20** ([MILESTONES → Milestone B](../assistant/MILESTONES.md#milestone-b--the-designs-screens)): the app shell -- the seven places on ⌘1–⌘7, the route kept across launches, the overlay and toast frame, the window law -- which opens 1f, 1h, 1j, 1k, 1l and 1p. **1f shipped 2026-10-20** ([MILESTONES → Milestone B](../assistant/MILESTONES.md#milestone-b--the-designs-screens)): the session library -- the saved conversations by date, opened with `--resume`, renamed and deleted with read-back, one live child at a time, the inspector -- which opens 1g. **1g shipped 2026-10-20** ([MILESTONES → Milestone B](../assistant/MILESTONES.md#milestone-b--the-designs-screens)): the session states and the input's reach -- new, private, failed with Try again, recalled and sources, the permission diff, attach and `@`, the context meter, settings by `set` or by restart, slash acts -- each on 52a/52d where the build announces them and the board's fallback where not. **1h shipped 2026-10-20** ([MILESTONES → Milestone B](../assistant/MILESTONES.md#milestone-b--the-designs-screens)): the models library and roles -- every kind under the CLI's groups, details from `models info` with 52e's typed facts where stated, delete/rename/repair asked and read back, the roles through their verbs -- which opens 1i. **1i shipped 2026-10-20** ([MILESTONES → Milestone B](../assistant/MILESTONES.md#milestone-b--the-designs-screens)): the jobs core -- every long child supervised in one place, stopped as Ctrl-C does, the 52b stream or the busy-line fallback -- and downloads over it, which opens 1m. **1j shipped 2026-10-20** ([MILESTONES → Milestone B](../assistant/MILESTONES.md#milestone-b--the-designs-screens)): tasks -- the ledger's tasks read on focus, a run started from the grants-up-front sheet and followed from its events, halt, cancel and resume as their verbs.

| # | Item | File | Status |
|---|---|---|---|
| 1k | Monitor & suites — the 2s system read while open, children's memory, suites with an honest app-side fit verdict | [`monitor-suites.md`](monitor-suites.md) | 🟢 |
| 1l | The code graph screen — search, explain cards and navigation over the graph documents, client-side deterministic layout, supervised rebuild | [`graph-screen.md`](graph-screen.md) | 🟢 |
| 1m | Video clips — the Movies-folder records library, the generate/iterate sheet argv-exact, supervised runs, the install consent | [`video-clips.md`](video-clips.md) | 🟢 |
| 1n | Compose — brief to video with scene progress from the project document, stop/resume truthful, edit with its diff; 52c's fields when present | [`compose-screens.md`](compose-screens.md) | 🔒 1m |
| 1o | Voice — the Music-folder speeches library, say with the model's own voices, playback by the document's timings | [`voice-screen.md`](voice-screen.md) | 🔒 1m |
| 1p | Settings — five panes over the real verbs; keys via stdin retained nowhere; the three-theme switch in `styles`; check with runnable remedies | [`settings-panes.md`](settings-panes.md) | 🟢 |
| 1q | Design board corrections — macOS 27+ stands, G16/G17 join the ledger with their owners, the settled verify rows; republished per the board's law | [`design-board-sync.md`](design-board-sync.md) | 🟢 |
