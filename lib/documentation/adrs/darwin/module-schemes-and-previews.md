# ADR 0004 — Every Presentation module has its own scheme, and every view it draws has a #Preview

**Status:** Accepted · **Date:** 2026-10-21

**Scope.** The macOS application (`lib/src/darwin`). Schemes and `#Preview` are Xcode's and Swift's. When the Linux and Windows applications are built, each records its own equivalent: a view that can be seen on its own, per module, without the app or a binary.

## Context

Measured 2026-10-21 on the `v0.2.0` branch:

- **One scheme serves everything.** `Apogee.xcodeproj` has a single shared scheme, `Apogee` (`xcshareddata/xcschemes/Apogee.xcscheme`). It builds the app and runs every test bundle. It is what `scripts/build.sh`, CI and the layering check use. Working on one module in Xcode means building and testing all of them.
- **No view can be seen on its own.** There are 19 Presentation modules and 122 view types in 30 files (121 `View`s, and `styles`' `MediaPlayer`, an `NSViewRepresentable`), and none has a preview. A screen's look is checked in two ways: by running the whole app against the installed binary, or by an offscreen render. Every new screen in track 2 (2b–2i) was drawn by a render harness written by hand in a scratch folder: compiled against the Debug frameworks, it drove the real binary over a scratch home. None of those harnesses is in the repository, so the next change to a view starts again from nothing.
- **Xcode previews need a scheme that builds the module.** The preview canvas builds the active scheme. With only `Apogee`, previewing one view builds the whole application.

The user's call, 2026-10-21: each Presentation module gets its own scheme, and the views each module creates can be previewed with `#Preview`.

## Decision

### 1. One shared scheme per Presentation module

Every Presentation module's framework target has a shared scheme of its own:

- **Its file** is `Apogee.xcodeproj/xcshareddata/xcschemes/<Target>.xcscheme`, named exactly as the target (`PresentationSessions.xcscheme`).
- **It is committed**, under `xcshareddata`. It is never a per-user scheme under `xcuserdata`, and never left to Xcode's automatic scheme creation, so every checkout has it.
- **Its Build action** builds the module's framework target. Xcode builds the modules it depends on with it.
- **Its Test action** runs the module's own test bundle, `<Target>Tests`, in Debug, so ⌘U in that scheme runs that module's tests alone.
- **It has no Run action executable**, since a framework has none. Previews build it in Debug, as `Apogee` does.

**This covers every Presentation module**, `styles` and `app` included. Lower layers draw nothing and take no scheme by this record.

**The shared `Apogee` scheme stays the whole:** the app and every test bundle. `scripts/build.sh`, CI and the layering check's rule that every bundle runs still go through it, and only through it. A module's scheme is for working on that module (its previews, its tests), never a second way to build or ship the app.

### 2. Every view a module declares has a `#Preview`

- **Every top-level type conforming to `View`** has at least one preview in its own file, after the view. This covers the types under a module's `Views/` and `Components/`, and `styles`' components.
- **Each preview is named** for the state it shows: `#Preview("Read")`, `#Preview("A play narrated")`, `#Preview("Failed")`.
- **A view whose look changes with its view-model's state** previews each state a person sees differently: not read yet, read, failed, running. The read state is the minimum. Where the design has a board for a state, that state has a preview.
- **Swift's `#Preview` macro, never `PreviewProvider`.** One form across the codebase.
- **A helper view private to a file** is covered by the preview of the view that uses it. A helper a second file uses is top-level, and has its own preview.

### 3. Previews are hermetic, as the tests are

- **A preview never starts `apogee`**, never reads `~/.apogee` or the person's files, and never reaches the network.
- **It builds the view's view-model over in-module fakes** of the Business protocols the view-model takes (`CommandRunning`, `JobStarting`, `ChatStarting`). The fakes answer with canned documents.
- **It drives that view-model through the same public API the app does** (`connect`, `refresh`, `select`, a form's fields), in the preview's own setup or `.task`. It never reaches past the view-model to set state the app could not.
- **The canned documents are the binary's own:** recorded fixtures from `tests/business/reads/Fixtures/` and `tests/data/protocol/Fixtures/`, or slices of them, never shapes made up for a preview. The design's law that every string on a board is the binary's applies to previews too. When a fixture is re-recorded, the previews that copy it are updated in the same change.
- **Preview support lives in the module's `Previews/` directory**, one of ADR 0002's "further directories as the module genuinely needs them": the fakes and the documents.
  - Each module keeps its own, as each test bundle keeps its own scripted support.
  - Preview support is not shipped code, so it never justifies a shared module. A fake a second module wants is copied.
- **Everything to do with previews is debug-only.** Each `#Preview` block and every file under `Previews/` sits inside `#if DEBUG`. The shipped app (Release, `scripts/build.sh build`) carries none of it.

## Consequences

- **Each module can be built, previewed and tested alone in Xcode.** Pick its scheme and open a view: the canvas draws it from recorded documents, with no app and no binary. ⌘U runs that module's tests in seconds rather than the whole suite.
- **A view's look no longer needs a scratch render harness.** The live pass against the installed binary remains an item's check that the screen works for real, which a preview cannot show.
- **The cost:**
  - a new view comes with its preview, and the documents behind it;
  - a new Presentation module comes with a scheme file;
  - a re-recorded fixture comes with updates to the previews that copy it.
- **Previews show; tests decide.** A preview is never a test's substitute. The view-model's tests stay the proof of behavior (ADR 0002).
- **The project file gains one scheme file per module.** These are hand-editable XML, like the shared scheme. The layering check reads them, as it reads `Apogee.xcscheme` today.

## Enforcement

These rules land with the adoption, in the layering check (`lib/src/darwin/tests/architecture/layering/LayeringCheck.swift`). As with ADR 0001's and 0002's rules, each is verified against a planted violation in `LayeringTests`:

- Every Presentation framework target has a shared scheme named for it. Its Build action references that target, and its Test action runs the target's `<Target>Tests` bundle. A missing scheme fails by the target's name, and so does one that builds or tests the wrong target.
- Every top-level `View` type in a Presentation module's sources has a `#Preview` in its file. A file with a view and no preview fails by the file and the view.
- `#Preview` blocks and `Previews/` files sit inside `#if DEBUG`, and `PreviewProvider` appears nowhere.
- A `Previews/` file never names a real starter or runner (`ApogeeClient`, `ReadRunner`, `JobStarter`, `ProcessSpawner`), so no preview can spawn the binary.
- The MVVM shape rule admits `Previews/` beside `Views/` and `ViewModels/`.

The rule also reaches the docs that new work follows:
- the GUI's CLAUDE.md checklist: a new view has its preview, and a new Presentation module its scheme;
- the Presentation layer card's "Changing this layer";
- DEVELOPER.md's "Adding a module".

## Adoption

Accepted 2026-10-21, not yet built. None of the 19 existing Presentation modules has a scheme, and none of their 122 views has a preview. The change that adopts this record brings them in line, together with:
- the layering check's new rules;
- the preview support each module needs;
- the doc lines above.

Until then, review holds new work to this record. The status line changes to "Accepted" with a dated **Landed** note, as ADR 0002's records its landing.

**Landed 2026-10-21**, the same day:
- **The 19 schemes**, `PresentationStyles` to `PresentationApp`, each building its framework and running its own test bundle.
- **274 named previews over the 122 views.** `app` declares no view of its own: `ApogeeApp` is the scene.
  - Each module's `Previews/` holds its fakes, and its documents copied byte for byte from the recorded fixtures.
  - Its view-models are driven through their public API: reads, supervised runs and recorded chat streams, played as the binary paces them.
  - The media places write their records to a folder under the temporary directory.
- **The layering check's rules**, each pinned on a planted violation. The MVVM shape rule already admitted `Previews/` as a further directory, so it needed no change.
- **Declared dependencies, never reached around.** A preview that names another layer's type has that module declared on its target:
  - `PresentationGraph` gained `DataProtocol` and `DataPreferences`;
  - `PresentationModels` gained `InfrastructureProcess` and `DataPreferences`;
  - `PresentationSettings` and `PresentationVoice` gained `DataProtocol` and `InfrastructureProcess`;
  - `PresentationSession` and `PresentationVideo` gained `InfrastructureProcess`;
  - `PresentationSuites` and `PresentationMonitor` gained `DataProtocol`.

  Each is a downward edge ADR 0001 allows. Swift would have let a preview make those types through an initializer's parameter, with no import. That leaves the link to auto-linking, which is how the 2026-10-20 Debug link failure happened.
- **A state no recorded document shows has no preview,** rather than one made up:
  - compose's failure;
  - a failed `task list`, `knowledge list`, `embed list` or `chats list` read;
  - a website permission;
  - a question card with several questions.

  Each gains its preview when a recording of it exists.
