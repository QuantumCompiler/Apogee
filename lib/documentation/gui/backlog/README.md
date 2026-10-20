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

**Track 1, the macOS application**, opened 2026-10-20 (the user's call, from that day's GUI spike: **Swift and SwiftUI**, macOS first, **unsigned ad-hoc** for now). The spike probed the real things and found the integration already cheap: the whole machine-mode contract works against the shipped v0.1.9 binary (hello, the capabilities-bearing session event, streaming, asks as `question` events, cancel — first event in ~1s), and a **~40-line Swift Foundation probe** drove it end to end (`session → thinking → question`) with no bridging layer; Xcode 27.0 is licensed and SwiftUI parses. The walls, measured: no GUI project or build wiring exists (`cicd.sh:40` `APPS=(cli)`; `changed.sh` knows only `cli`); ADR 0001 has no mechanical teeth until the first build; a `.app` inherits no shell `PATH` and `machine-schema.json` is not installed as a file (the binary prints it — verified). The split: **1a** the scaffold (SwiftPM, the four layers with the layering check at birth, the window, the `Apogee.app` ad-hoc bundle, `APPS`/`changed.sh`); **1b** the client core (discovery, the child, tolerant typed events, the session, the reads); **1c** the session window (the conversation rendered and steered, the mock-actor fixture); **1d** the pipeline (the darwin workflow beside the CLI's, the gated build, the packaged artifact). The CLI-scoped repo skills were mirrored for the GUI the same day, closing the spike's W4 without an item. Gated 1a → 1b → 1c, 1a → 1d. The one **[user]** call (1d: ship in releases from day one, or a CI artifact until 1c) **was answered the same day**: a CI artifact until the session window ships, release wiring its own later step. **Groomed 2026-10-20**: every call resolved; nothing in the track waits on a question. **1a shipped 2026-10-20** ([MILESTONES → Milestone A](../assistant/MILESTONES.md#milestone-a--the-macos-application)) as an **Xcode project** rather than the SwiftPM package it was groomed as (the user's call at the build): `Apogee.xcodeproj` with every module a framework target and its layering check, the ad-hoc-signed `Apogee.app`, `cicd.sh --app` and `changed.sh gui-darwin` -- which opens 1b and 1d. A new module in either is a framework target, its test bundle in the shared scheme, and its line on the layer card. **1b shipped 2026-10-20** ([MILESTONES → Milestone A](../assistant/MILESTONES.md#milestone-a--the-macos-application)): the client core -- the captured child, the tolerant event decoder over a recorded real stream, the session with its turn tracking, cancel and stop escalation, discovery and the reads -- which opens 1c.

| # | Item | File | Status |
|---|---|---|---|
| 1c | The session window — the conversation rendered and steered: streaming, thinking, tool lines, questions answered natively, cancel; the mock-actor conversation as the fixture | [`session-window.md`](session-window.md) | 🟢 |
| 1d | The darwin pipeline — the GUI's own workflow: the `changed.sh`-gated build on the macOS runner through `cicd.sh`, the ad-hoc-signed `Apogee.app` artifact | [`darwin-pipeline.md`](darwin-pipeline.md) | 🟢 |
