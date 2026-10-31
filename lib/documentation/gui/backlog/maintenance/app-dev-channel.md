# A development build of the app runs on the dev channel

**What / why.** The user's call, 2026-10-31: development runs on the dev channel. The release `apogee`, `~/.apogee` and the installed `Apogee.app` are the user's production install, changed only by what a release ships, so its updaters are tested for real (the law: the CLI docs' [Development runs on the dev channel](../../../cli/assistant/CLAUDE.md#development-runs-on-the-dev-channel)). The CLI has had the channel since M10 (`make install MODE=dev`: `apogee-dev` over `~/.apogee-dev`). The app does not. Measured 2026-10-31:

1. **A development build drives production.** `BinaryDiscovery.candidates(override:)` (`source/data/discovery/BinaryDiscovery.swift:30-34`) tries the override, then `~/.local/bin/apogee` (release), then `apogee-dev`, then `apogee-test`. With both CLIs installed, any build of the app drives the release binary over `~/.apogee`.
2. **A development build shares the installed app's settings.** The app target's `PRODUCT_BUNDLE_IDENTIFIER` is `com.quantumcompiler.apogee` in both Debug and Release (`Apogee.xcodeproj/project.pbxproj`), and `Preferences` sits on `UserDefaults` (`source/data/preferences/Preferences.swift:120-124`), so a development run reads and writes the installed app's route, appearance, recall and binary override. Setting the override to `apogee-dev` in a development build would point the *installed* app at it too.
3. **The way to try a build is the release identity.** `scripts/build.sh open` runs `build_app`, a Release build (`build.sh:132-137`), the same bundle the release ships.

**Core constraint(s).**
- **The release channel is byte-identical**: what `build.sh build`/`package`, CI and the release carry keeps its bundle identifier, name, discovery and settings domain. Nothing a user runs changes.
- **The dev channel touches nothing of production**: its own bundle identifier (so its own `UserDefaults` domain and its own Dock identity), discovery of `apogee-dev` only, never falling back to the release binary. A missing `apogee-dev` is the not-found screen, naming `make -C lib/src/cli install MODE=dev`.
- **One channel per build, decided at build time**, as the CLI bakes its channel: no runtime switch to forget.
- **The four layers** (ADR 0001): discovery stays in Data, takes the channel as a value; nothing reads a build setting below the composition root.
- **Hermetic tests**: the discovery table per channel over scripted binaries, no real `~/.local/bin`.

**Seam + files.**
- `Apogee.xcodeproj` — the app target's identifier `com.quantumcompiler.apogee$(APOGEE_CHANNEL_SUFFIX)` and product name, an `ApogeeChannel` Info.plist key; Debug defaulting to dev, Release to release.
- `scripts/build.sh` — `--channel release|dev`: `build` and `package` default to release (CI's path, unchanged), `open` to dev.
- `source/data/discovery/BinaryDiscovery.swift` — `candidates(override:channel:)`: release as today; dev the override and `apogee-dev` only.
- `source/presentation/app/` — the channel read once from the bundle and passed down; the window and About say "Apogee Dev" on the dev channel.
- Tests: `tests/data/discovery/` (the per-channel table), a check that the Release build's identifier is unchanged.
- Docs: GUI `CLAUDE.md` (the state of play becomes the rule as built), `DEVELOPER.md` (build and channels), the CLI's `DEVELOPER.md` channel table (the app's row).

**Reference.** The CLI's channels (M10, CLI MILESTONES; CLI `DEVELOPER.md` → Install channels): a channel baked at build time, its own root, the release channel byte-identical. The app's override and discovery (1b, M6).

**Decisions made** (dated):
- 2026-10-31: The user's call: development runs on the dev channel, and the app must not disturb the production install.
- 2026-10-31: In the GUI's Maintenance queue: it is the app's build and discovery; the CLI's channel already exists.

**Open calls:**
- [default: the dev build's identifier is `com.quantumcompiler.apogee.dev` and its name "Apogee Dev".]
- [default: Debug builds (Xcode's Run) are the dev channel; `build.sh open` builds dev; `build` and `package` stay release.]
- [default: the dev channel has no `apogee-test` fallback; a test-channel app is out of scope until asked.]

**Guardrail(s).** The discovery table per channel (release unchanged with both CLIs present; dev picking `apogee-dev`, never `apogee`, and not-found naming the install line when absent); the Release build's identifier and name pinned; the layering check green.

**Acceptance criteria:**
- [ ] With both CLIs installed, a dev-channel app drives `apogee-dev` over `~/.apogee-dev` and leaves the installed app's settings untouched.
- [ ] The release app, CI's artifact and the release asset are unchanged.
- [ ] `build.sh open` opens "Apogee Dev"; the full suite is green.

**Scope note.** GUI Maintenance; ungated. Once it ships, the GUI `CLAUDE.md`'s "Not yet true of the app" list is retired.
