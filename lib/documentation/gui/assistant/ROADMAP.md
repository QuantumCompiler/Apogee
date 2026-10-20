# Apogee GUI — Roadmap

How we work: features get discussed in chat, written up here (and in [SPEC.md](SPEC.md)), given their own document in [`backlog/`](../backlog/README.md) when they're specced, then an agent takes the backlog item and builds it straight from that document (there is no separate TODO file — the backlog is the queue). When work ships it's recorded in detail in [MILESTONES.md](MILESTONES.md); this file stays high-level.

**Version scheme:** repo-wide — the release `lib/release/VERSION` names, published when its branch merges into `stable` (the CLI docs record the law). The GUI's themes ride those releases once the first application lands.

---

## Shipped releases

*(Nothing shipped yet — the first tagged release carrying GUI work starts this board.)*

---

## In progress

### v0.2.0 — The GUI effort opens

The documentation tree was restructured per application (2026-10-20, the user's call): CLI docs under `documentation/cli/`, this system scaffolded under `documentation/gui/`, the [router](../../CLAUDE.md) between them.

- [x] The docs home — this system, scaffolded 2026-10-20; `adrs/` and `reference/` beside it the same day.
- [x] **ADR 0001** — the four-layer law (Infrastructure / Data / Business / Presentation, dependencies down) holds for every GUI application regardless of language and framework (the user's call, 2026-10-20).
- [x] **The first spike** — ran 2026-10-20: the machine-mode integration proven cheap (the shipped binary driven end to end by a ~40-line Swift probe), the walls measured (no build wiring, no layering teeth, binary discovery from a `.app`), the split landed as track 1. The user's calls, same day: **macOS first, Swift and SwiftUI, unsigned ad-hoc**.
- [x] **The darwin scaffold** *(1a, shipped 2026-10-20 — [MILESTONES → Milestone A](MILESTONES.md#milestone-a--the-macos-application))* — `Apogee.xcodeproj` (an Xcode project, the user's call at the build), the four layers as framework targets with the layering check at birth, the SwiftUI window, the ad-hoc-signed `Apogee.app`, `cicd.sh --app` and `changed.sh gui-darwin`.
- [x] **The machine-mode client core** *(1b, shipped 2026-10-20 — [MILESTONES → Milestone A](MILESTONES.md#milestone-a--the-macos-application))* — discovery across the install channels, the captured child, tolerant typed events over an async stream, answers and cancel down the pipe, the JSON reads.
- [x] **The session window** *(1c, shipped 2026-10-20 — [MILESTONES → Milestone A](MILESTONES.md#milestone-a--the-macos-application))* — one chat session rendered and steered: streaming answers, thinking, tool lines, the protocol's questions as native UI, stop/Esc as `cancel`; the mock-actor conversation as the standing fixture.
- [ ] **The darwin pipeline** *([1d](../backlog/darwin-pipeline.md))* — the GUI's own workflow beside the CLI's: the gated build on the macOS runner through `cicd.sh`, the ad-hoc-signed, verified `Apogee.app` artifact — a per-run CI artifact until 1c ships (the user's call, answered 2026-10-20).
- See the [`backlog/`](../backlog/README.md) index for the work queue.

---

## Fast follow

*(empty)*

## Unspecced ideas

- **`Apogee.app` in releases** — the merge's release carrying the ad-hoc-signed app beside the CLI's archives. The user's 2026-10-20 call kept it a CI artifact until the session window shipped, which it now has (1c); how the release scripts take it is its own item, on the user's word.
- **Setting the binary override in the app** — today the override is the `binaryOverride` default (`defaults write com.quantumcompiler.apogee binaryOverride /path/to/apogee`); a "Locate apogee…" chooser on the not-found screen would set it (1c's note).
- **The answer as Markdown** — the window shows an answer as its exact text; rendering its Markdown, as the CLI's terminal does, is a deliberate later step (1c's note).

## Ideas / candidate features

*(none yet)*

## Explicit non-goals

Ruled out on purpose (see [SPEC.md](SPEC.md) → Non-goals for the rationale):

- **No capability of its own**
- **No HTTP path for local work**
