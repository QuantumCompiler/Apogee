# The app shell: the sidebar, the routes, the keys

**What / why.** The shipped app is one session window; the design is a whole application around it — the sidebar (`main.html`'s `ap-side`: Sessions, Models, Tasks, Graph, Video, Compose, Voice, Suites, plus the Monitor overlay and Settings), a route per screen, **a key for every destination** (`BuildLayout.dc.html`'s navigation and keys; the Settings › Keyboard list in `main.html`'s `set.keys`), the overlay frame (the monitor sheet, the settings window, sheets and the toast), and the window law (**at least 960×640; below 900 wide the sidebar becomes icons and side panes close** — the design's decision 10). This item builds that frame: the sidebar and routing as a Presentation module, each destination mounting its screen (placeholders where an item hasn't shipped — the `EmptyState` component 1a seeded, naming what will live there), the keyboard map, the overlay scaffolding, and the shipped session window re-homed as the Sessions route's content. The 29-screen prototype is the reference (`main.html`, certified 29/29 by its own health check, 2026-10-20); this item draws the frame, not the screens.

**Core constraint(s).** ADR 0001/0002 whole: a `shell` Presentation module (`Views/`, `ViewModels/`), visuals from `styles`, the view-model SwiftUI-free and tested over scripted state; no Business reach from views. **Navigation is state, honestly**: the route is app state (restorable across launches per the design's "window layout lives in Application Support"), never window juggling. **Placeholders are honest** — an unbuilt destination says so plainly, never a dead control. Keys per the design's list; every destination reachable by keyboard.

**Seam + files.** `source/presentation/shell/` (sidebar, routing, overlays frame, the key map), `source/presentation/app/` (the window hosting the shell), `source/data/preferences/` (the restored layout/route), `styles` grown only by promotion. Tests: the route/view-model matrix (every destination, the key map, the collapse-below-900 state, restoration), hermetic.

**Reference.** `lib/design/spec/html/main.html` (the frame and `set.keys`), `BuildLayout.dc.html` (regions, sizes, keys), the design's decisions 4 and 10; the shipped `session` module (1c) re-homed, `(consumed decision)` for its internals.

**Decisions made** (dated):
- 2026-10-20 — Track 1 grown from the design-completion spike (the user's call); the shell is the first item, everything screen-shaped gates on it.
- 2026-10-20 — macOS 27+/arm64 stands (the shipped 1a decision); the design board's macOS-14 line is corrected by [`design-board-sync.md`](design-board-sync.md).

**Open calls:**
- [default: exact key assignments are the design's `set.keys` list; conflicts with system keys resolved at build and recorded.]

**Guardrail(s).** The view-model matrices (routes, keys, collapse, restore) over scripted state; the layering check already holds the module shape; no binary, no network in tests.

**Acceptance criteria:**
- [ ] Every design destination exists in the sidebar with its key, mounting its screen or an honest placeholder; the session window lives at Sessions.
- [ ] The window floors at 960×640 and collapses the sidebar below 900; route and layout restore across launches; the full suite is green.

**Scope note.** Earmarked for v0.2.0; first of the design-completion set, ungated. Out of scope: every screen's own content (the sibling items), the monitor's data (1k), settings' panes (1p).
