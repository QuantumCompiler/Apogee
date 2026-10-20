# The darwin scaffold: a buildable four-layer app from day one

**What / why.** Nothing of the GUI exists in the build: `lib/scripts/cicd.sh:40` is `APPS=(cli)` and `lib/scripts/changed.sh` knows only `cli` (`:36`, `:51`) — measured 2026-10-20 (the GUI spike) — while the toolchain wall that blocked Swift work in September is gone: **Xcode 27.0 licensed, Swift 6.4 answering, SwiftUI parsing against the SDK**, all proven the same day. This item makes `lib/src/darwin/` real: a **self-contained SwiftPM project** (the repo's app convention: each application owns its own build) in **Swift and SwiftUI** (the user's call, 2026-10-20) — the four-layer directory shape of [ADR 0001](../adrs/layer-enforcement.md) (`source/<layer>/<package>/`, tests mirroring), a minimal SwiftUI window (the app exists, launches, shows its name), an **`.app` bundling step** (SwiftPM builds the executable; a small script assembles `Apogee.app` with its `Info.plist` and **ad-hoc codesigning** — the user's call, 2026-10-20: unsigned ad-hoc for now), and a headless build-and-test entry (`swift build` / `swift test`) wired into `cicd.sh`'s `APPS` and `changed.sh`. And the law gets its teeth with the first commit, not later: **the layering check lands here** (ADR 0001's Enforcement says it arrives with the first application's build) — Package.swift's target dependency graph *is* the module map, so the check holds each target to its layer's row and fails on an upward edge, the CLI's `harness.layering` as the reference.

**Core constraint(s).** **ADR 0001 whole**: four layers, dependencies down, SwiftUI's types confined to Presentation — the scaffold's first packages already sorted (Presentation: the app/window; Infrastructure, Data, Business present even if nearly empty, so the shape is real, not aspirational). **Self-contained**: nothing above `lib/src/darwin/` knows how it compiles; `cicd.sh` calls one entry point and defers darwin on non-macOS hosts through its existing defer machinery (`--no-defer` on a Mac runner fails loud, as the CLI's targets do). **`changed.sh` gains a `gui-darwin` entry** (what the app is built from) so CI can gate on it — the one-declaration rule. **The machine-mode invariant stands from day one** (the GUI docs' ⚠): the scaffold links nothing of the CLI's. Hermetic tests: `swift test` runs with no network, no `apogee` binary required.

**Seam + files.**
- `lib/src/darwin/Package.swift` — the package: one target per layer-package, dependencies declared per ADR 0001; the app executable target.
- `lib/src/darwin/source/presentation/app/` — the SwiftUI `App`/`WindowGroup` minimal window, MVVM-shaped per [ADR 0002](../adrs/presentation-mvvm.md) (`Views/`, `ViewModels/`); `source/presentation/styles/` — the shared visual module (typography, colors, the common components), seeded small and real; `source/{infrastructure,data,business}/` seeded with their first (small, honest) packages.
- `lib/src/darwin/tests/` — mirroring; the **layering check** (a test reading Package.swift's target graph against the layer map, failing on an undeclared or upward edge, refusing to pass on an empty target list — and holding ADR 0002's presentation rules: every Presentation module `Views/`/`ViewModels/`-shaped, no SwiftUI import under `ViewModels/`, `styles` depending on no sibling Presentation module).
- `lib/src/darwin/scripts/bundle.sh` (or Makefile target) — executable → `Apogee.app` with `Info.plist`, `codesign --sign -` (ad-hoc).
- `lib/scripts/cicd.sh` — darwin joins `APPS` (built on macOS hosts, deferred elsewhere); `lib/scripts/changed.sh` — the `gui-darwin` entry.

**Reference.** [ADR 0001 (gui)](../adrs/layer-enforcement.md) — the law this enforces; the CLI's `harness.layering` and link policy — the reference implementation; the CLI's app-owned-build convention (its CLAUDE.md map) — the shape `lib/src/<app>/` follows; the GUI spike (2026-10-20) — the toolchain and seam measurements above.

**Decisions made** (dated):
- 2026-10-20 — **Swift and SwiftUI** for the macOS application (the user's call, with the spike).
- 2026-10-20 — **Unsigned, ad-hoc codesigned** for now (the user's call; Developer ID + notarization is the recorded later option).
- 2026-10-20 — SwiftPM as the build system (the default, standing): version-controllable, headless, CI-friendly; the `.app` assembled by a bundling step rather than an Xcode project.
- 2026-10-20 — macOS 27+, Apple silicon only (the default, standing — matching the CLI's `macos-arm64` target and the dev host); the app is `Apogee.app`.
- 2026-10-20 — The layering check ships in this item, not later (ADR 0001's enforcement clause honored at birth).
- 2026-10-20 — **[ADR 0002](../adrs/presentation-mvvm.md) lands with the scaffold** (the user's call, same day): Presentation modules are MVVM (`Views/`, `ViewModels/`, module-specific extras) sharing one `styles` module; the scaffold seeds the shape and the check holds it.

- 2026-10-20 — **First packages and the map's format**: the per-layer seed packages named at build (small and honest stubs 1b fills — `infrastructure/process`, `data/protocol`, `business/session` the expected shape); the layer map's declaration format decided by what the check reads most robustly (the default, confirmed at grooming).
- 2026-10-20 — **`Info.plist` minimums at build**: bundle id `com.quantumcompiler.apogee`, the version mirroring the repo release (the default, confirmed at grooming).

**Open calls:** none — groomed 2026-10-20.

**Guardrail(s).** The layering check (an upward edge fails naming the targets; verified against a planted violation; refuses an empty graph); `swift test` green and hermetic; the bundle step produces an `Apogee.app` that `codesign --verify` accepts (ad-hoc) and that launches on the dev host (the user's one manual check, recorded); `cicd.sh` on a non-mac host defers darwin without failing; `changed.sh gui-darwin` answers correctly for a darwin-only and a docs-only diff.

**Acceptance criteria:**
- [ ] `lib/src/darwin/` builds and tests headlessly on the dev host; the window opens and shows the app.
- [ ] The four layers exist as directories and SwiftPM targets; the layering check fails a planted upward edge by name — and a planted SwiftUI import in a `ViewModels/` file, and a planted `styles` → sibling dependency.
- [ ] `Apogee.app` assembles with ad-hoc signing; `cicd.sh` builds darwin on macOS and defers it elsewhere; `changed.sh gui-darwin` exists and answers.
- [ ] No CLI code linked; no network or binary dependence in tests.

**Scope note.** Earmarked for v0.2.0; first of track 1, ungated. Out of scope: the machine-mode client ([`machine-mode-client-core.md`](machine-mode-client-core.md)), any real UI beyond the window ([`session-window.md`](session-window.md)), CI jobs ([`darwin-pipeline.md`](darwin-pipeline.md)), notarization (recorded later), linux/windows apps.
