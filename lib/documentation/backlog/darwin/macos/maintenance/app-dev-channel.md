# APP-M18 · A development build of the app runs on the dev channel

| Field | Value |
|---|---|
| **Type** | 🛠️ Task |
| **Project** | Apple apps · macOS |
| **Status** | 🟢 Ready |
| **Fix version** | Maintenance (any time) |
| **Component** | The app's build and binary discovery (`Apogee.xcodeproj`, `build.sh`, `BinaryDiscovery`) |
| **Labels** | `dev-channel` `build` `binary-discovery` |
| **Epic** | — (maintenance has no track) |
| **Blocked by** | — |
| **Blocks** | — |
| **Reporter** | The user (the call that development runs on the dev channel, 2026-10-31) |
| **Created** | 2026-10-31 |

---

## Summary

A development build of the app drives the release `apogee` over `~/.apogee` and shares the installed `Apogee.app`'s settings, and the way to try a build is the release identity. This item gives the app the CLI's dev channel: its own bundle identifier and settings, `apogee-dev` only, `build.sh open` building it, and the release app unchanged.

## Description

### User story

**As** a contributor to Apogee trying branch work in the app,
**I want** a development build to run on its own dev channel, named "Apogee Dev", driving only `apogee-dev`,
**so that** the production install (the release `apogee`, `~/.apogee` and the installed `Apogee.app`) is changed only by what a release ships, and its updaters are tested for real.

### Background

The user's call, 2026-10-31: development runs on the dev channel. The release `apogee`, `~/.apogee` and the installed `Apogee.app` are the user's production install, changed only by what a release ships, so its updaters are tested for real (the law: the CLI docs' [Development runs on the dev channel](../../../../assistant/cli/CLAUDE.md#development-runs-on-the-dev-channel)). The CLI has had the channel since M10 (`make install MODE=dev`: `apogee-dev` over `~/.apogee-dev`). The app does not.

### Evidence

Measured 2026-10-31; paths under `lib/src/darwin/`.

1. **A development build drives production.** `BinaryDiscovery.candidates(override:)` (`source/data/discovery/BinaryDiscovery.swift:30-34`) tries the override, then `~/.local/bin/apogee` (release), then `apogee-dev`, then `apogee-test`. With both CLIs installed, any build of the app drives the release binary over `~/.apogee`.
2. **A development build shares the installed app's settings.** The app target's `PRODUCT_BUNDLE_IDENTIFIER` is `com.quantumcompiler.apogee` in both Debug and Release (`Apogee.xcodeproj/project.pbxproj`), and `Preferences` sits on `UserDefaults` (`source/data/preferences/Preferences.swift:120-124`), so a development run reads and writes the installed app's route, appearance, recall and binary override. Setting the override to `apogee-dev` in a development build would point the *installed* app at it too.
3. **The way to try a build is the release identity.** `scripts/build.sh open` runs `build_app`, a Release build (`build.sh:132-137`), the same bundle the release ships.

## Acceptance criteria

- [ ] **Given** both CLIs are installed, **when** a dev-channel app runs, **then** it drives `apogee-dev` over `~/.apogee-dev` and leaves the installed app's settings untouched.
- [ ] The release app, CI's artifact and the release asset are unchanged.
- [ ] **Given** a checkout, **when** I run `build.sh open`, **then** it opens "Apogee Dev", its Dock icon visibly not the release app's.
- [ ] The full suite is green.

## Constraints

- **The release channel is byte-identical.** What `build.sh build`/`package`, CI and the release carry keeps its bundle identifier, name, discovery and settings domain. Nothing a user runs changes.
- **The dev channel touches nothing of production.** Its own bundle identifier (so its own `UserDefaults` domain and its own Dock identity), discovery of `apogee-dev` only, never falling back to the release binary. A missing `apogee-dev` is the not-found screen, naming `make -C lib/src/cli install MODE=dev`.
- **One channel per build, decided at build time**, as the CLI bakes its channel: no runtime switch to forget.
- **The four layers** (ADR 0001): discovery stays in Data, takes the channel as a value; nothing reads a build setting below the composition root.
- **Hermetic tests.** The discovery table per channel over scripted binaries, no real `~/.local/bin`.
- **"Apogee Dev" is told apart from the release app in the Dock** by an icon derived from `lib/design/assets/Logo.png` through `scripts/icon.sh` (handed on by 11a, [Milestone H](../../../../assistant/darwin/MILESTONES.md#milestone-h--the-apps-logo)).

## Technical approach

Paths under `lib/src/darwin/`.

| File | Change |
|---|---|
| `Apogee.xcodeproj` | The app target's identifier `com.quantumcompiler.apogee$(APOGEE_CHANNEL_SUFFIX)` and product name, an `ApogeeChannel` Info.plist key; Debug defaulting to dev, Release to release |
| `scripts/build.sh` | `--channel release\|dev`: `build` and `package` default to release (CI's path, unchanged), `open` to dev |
| `source/data/discovery/BinaryDiscovery.swift` | `candidates(override:channel:)`: release as today; dev the override and `apogee-dev` only |
| `source/presentation/app/` | The channel read once from the bundle and passed down; the window and About say "Apogee Dev" on the dev channel |
| `scripts/icon.sh` | `icon.sh generate` makes the dev icon variant beside `AppIcon` |
| `IconSetCheck`, `icon.sh verify` | `IconSetCheck` requires Debug and Release to name `AppIcon`, and `icon.sh verify` (run by `build.sh`) requires the bundle to, so both change with the icon variant |

**Reference.** The CLI's channels (M10, CLI MILESTONES; CLI `DEVELOPER.md` → Install channels): a channel baked at build time, its own root, the release channel byte-identical. The app's override and discovery (1b, M6).

**Docs when it ships:** GUI `CLAUDE.md` (the state of play becomes the rule as built; its "Not yet true of the app" list is retired once this ships), `DEVELOPER.md` (build and channels), the CLI's `DEVELOPER.md` channel table (the app's row).

## Testing

All hermetic: the discovery table per channel over scripted binaries, no real `~/.local/bin`.

- **`tests/data/discovery/` (the per-channel discovery table).** Release unchanged with both CLIs present; dev picking `apogee-dev`, never `apogee`; and not-found naming the install line when `apogee-dev` is absent.
- **The Release build's identity.** A check that the Release build's identifier is unchanged; its identifier and name pinned.
- **The layering check** green.

## Open questions

| Question | Status | Answer |
|---|---|---|
| What are the dev build's identifier and name? | `[default]` | The dev build's identifier is `com.quantumcompiler.apogee.dev` and its name "Apogee Dev". |
| Which builds are the dev channel? | `[default]` | Debug builds (Xcode's Run) are the dev channel; `build.sh open` builds dev; `build` and `package` stay release. |
| Does the dev channel fall back to `apogee-test`? | `[default]` | No: the dev channel has no `apogee-test` fallback; a test-channel app is out of scope until asked. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-10-31 | Development runs on the dev channel, and the app must not disturb the production install. | the user |
| 2026-10-31 | In the GUI's Maintenance queue (M18, ungated): it is the app's build and discovery; the CLI's channel already exists. | the spec |

## Out of scope

- A test-channel app (an `apogee-test` fallback), until asked (the recorded default above).

## Links

- **Blocked by:** — · **Blocks:** —
- **Relates to:** the CLI's [Development runs on the dev channel](../../../../assistant/cli/CLAUDE.md#development-runs-on-the-dev-channel); the CLI's channels (M10, CLI MILESTONES; CLI `DEVELOPER.md` → Install channels); the app's override and discovery (1b, M6); 11a's icon pipeline ([Milestone H](../../../../assistant/darwin/MILESTONES.md#milestone-h--the-apps-logo)); ADR 0001 (the four layers).
- **Follow-on:** once it ships, the GUI `CLAUDE.md`'s "Not yet true of the app" list is retired.
