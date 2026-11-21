# APP-8c · Getting the app onto iPhone and iPad

| Field | Value |
|---|---|
| **Type** | ✨ Feature |
| **Project** | Apple apps |
| **Status** | 🔒 Blocked |
| **Fix version** | v0.2.5 |
| **Component** | The app's build and pipeline: `build.sh`, `cicd.sh`, `darwin.yml`, the iOS target's signing settings |
| **Labels** | `ios` `ipados` `ci` `code-signing` |
| **Epic** | Track 8 · the iPad and getting the app onto iPhone and iPad |
| **Blocked by** | [8a](app-builds-for-ios-and-ipados.md) |
| **Blocks** | [9c](three-apple-platforms-whole.md) |
| **Reporter** | The Apple-platforms spike and brainstorm (2026-11-07), on the user's vision |
| **Created** | 2026-11-07 |

---

## Summary

An ad-hoc-signed app cannot be installed on iPhone or iPad, and CI builds no iOS app (wall **W10**). This item gets the built app **onto the person's iPhone and iPad** by the path the user picks, and makes the pipeline build and test the iOS app on every pull request with no secret, leaving the Mac's ad-hoc-signed app and its release asset unchanged. Besides the gate on 8a, the item is blocked by the `[user]` call in Open questions (the Apple Developer Program, or personal sideloading).

## Description

### User story

**As** a person who runs Apogee on their own machine and owns an iPhone and an iPad,
**I want** the app installed on those devices by a path I chose, and built and tested by the pipeline on every pull request,
**so that** I can use Apogee from them as a client of my own server, while the CLI and the Mac app stay standalone and need no Apple account, team, secret or server.

### Background

The vision (the user's call, 2026-11-07): Apogee is a private, self-hosted harness that feels like Claude on every device the person owns, with a strong machine of theirs serving their own desktops and phones. On iPhone and iPad the app is a client of that server only.

[8a](app-builds-for-ios-and-ipados.md) makes the app's code compile for those devices, and [8b](../ipados/v0.2.5/ipad-app.md) and [9a](../ios/v0.2.5/iphone-app.md) give it their layouts. This item gets the built app **onto the person's iPhone and iPad**, and makes the pipeline build and test it on every pull request. It closes wall **W10**.

It delivers three things:
- **An iOS build and test in CI on every pull request**, for iPhone and iPad simulators, with no secret, so forks keep working.
- **A way onto the person's devices**: the one the user's call (Open questions) picks, as one `build.sh` command and a written procedure.
- **The Mac's ad-hoc-signed app and its release asset unchanged.**

### Evidence

**W10 — an ad-hoc-signed app cannot be installed on iPhone or iPad, and CI builds no iOS app.** Measured 2026-11-07 at HEAD (`4f92316d`):

1. **The app is signed ad hoc, with no team.** Both project configurations set `CODE_SIGN_IDENTITY = "-"`, `CODE_SIGN_STYLE = Manual`, `DEVELOPMENT_TEAM = ""` and `ENABLE_HARDENED_RUNTIME = NO` (`Apogee.xcodeproj/project.pbxproj:7878-7884` Release, `:8731-8737` Debug), per the user's 2026-10-20 call (`scripts/build.sh:36-42`, `darwin.yml:25-35`). An ad-hoc signature has no provisioning profile. iPhone and iPad install only an app whose profile names the device (a development or ad hoc profile) or one delivered by TestFlight or the App Store. So no build of today's app can reach a device.
2. **The project builds for the Mac only.** `SDKROOT = macosx` (`project.pbxproj:7889`, `:8744`). The resolved Release settings say `SUPPORTED_PLATFORMS = macosx`. `xcodebuild -scheme Apogee -showdestinations` lists "My Mac" and "Any Mac" as compatible, and every iOS Simulator destination as incompatible ("iPad (A16)'s iOS Simulator platform doesn't match Apogee.app's supported platforms"). 8a changes the project. This item builds what 8a makes.
3. **The pipeline builds the Mac only.** `build.sh` has one target (`TARGETS=(macos-arm64)`, `:49`), refuses any other (`:92-94`), and builds for `-destination "platform=macOS,arch=arm64"` (`:127-129`). `cicd.sh` lists `APPS=(cli darwin)` with darwin's one target `macos-arm64` (`:46`, `:52`). `darwin.yml`'s one job builds and tests that (`:115`), packages it (`:120`) and keeps `apogee-gui-darwin` (`:124-132`).
4. **No signing secret exists, by design.** The CLI pipeline says "Fork PRs run with a read-only token and no secrets. Nothing here needs a secret, so they work -- keep it that way" (`ci.yml:212-213` at HEAD; `:223-224` in the uncommitted `ci.yml` edit in the working tree on 2026-11-07). The app's pipeline: "no Developer ID, no notarization, no signing secret anywhere" (`darwin.yml:26-27`). A required check that needed a secret would fail every fork's pull request.
5. **The host's toolchain can build both.** This Mac has Xcode 27.1 (`27A9275`) with the `iphoneos27.1` and `iphonesimulator27.1` SDKs beside `macosx27.0`, and an iOS 27.0 simulator runtime (`xcodebuild -showsdks`, `xcrun simctl list runtimes`). Whether GitHub's `xcode-27` image (`darwin.yml:106`) carries an iOS simulator runtime was not measured here. The job checks that itself (Testing).
6. **The person's Apple account appears to have a membership already.** This Mac's keychain holds a "Developer ID Application" and an "Apple Development" identity (`security find-identity -v -p codesigning`: 2 valid). It also holds an Xcode-managed iOS team profile, valid until 2027-03-23 with 3 registered devices. Apple lists Certificates, Identifiers & Profiles as a membership-only feature, and a Developer ID identity is a program certificate. Whether Apogee uses that membership is still the user's call (Open questions).
7. **Apple's terms**, checked 2026-11-07 on developer.apple.com:
   - **A free account** gets a Personal Team: "up to 3 devices", "up to 3 apps per device", "10 App IDs", and profiles that expire after 7 days (`/support/compare-memberships/`).
   - **The Apple Developer Program** is 99 USD per membership year (`/programs/enroll/`) and adds TestFlight and App Store Connect (`/help/account/membership/programs-overview`).
   - **TestFlight** allows up to 100 internal testers, and a build can be tested for up to 90 days (`/help/app-store-connect/test-a-beta-version/testflight-overview`).
   - **Ad hoc** profiles allow up to 100 devices per product family per membership year (`/help/account/devices/devices-overview/`).

## Acceptance criteria

- [ ] **Given** a pull request, including a fork's, **when** `build darwin` runs, **then** it builds the iOS app for a device (unsigned) and runs the suite on an iPhone and an iPad simulator, with no secret, and the fork's pull request passes it.
- [ ] **Given** this item's changes, **when** the macOS app is built, packaged and released, **then** it is unchanged: `apogee-gui-darwin.zip` holds the same app and README, `codesign -dv` reports an ad-hoc signature, and the release a merge publishes attaches it as before.
- [ ] **Given** the path the user chose, **when** the person follows it for their iPhone and iPad, **then** the app installs on both, launches, opens on 7a's connection screen, and pairs with the person's `apogee-dev serve` (CLI 59a).
- [ ] GUI `DEVELOPER.md` says, in a few steps, how a person gets the app onto an iPhone or iPad, and what that path costs (account, expiry, devices).
- [ ] **Given** no Apple account, team, secret or server, **when** the CLI and the Mac app are built, tested, packaged and released, **then** all of it works as today.

## Constraints

- **The anti-goal: nothing here may make standalone use need a server, a network or an account** (GUI SPEC → Non-goals → No server requirement). The CLI and the Mac app keep building, testing, packaging and releasing exactly as they do today, with no Apple account, no team, no secret and no network beyond what they use now. An Apple membership, if the user chooses one, is needed only to put the *iOS* app on a device.
- **The Mac's signing stays ad hoc** (the user's 2026-10-20 call). `apogee-gui-darwin` keeps its name, contents, ad-hoc signature and README, and `release-from-pr.sh` keeps attaching it (the asset's name `:84`, the app's change check `:420-421`, the zip added to the release's assets `:476` and `:487`). Moving the Mac app to Developer ID and notarization would be a separate call and a separate item, not a side effect of this one.
- **No secret on the pull-request path** (`ci.yml:212-213`). Every job a pull request runs, and every required check, works with no secret. If the user's call puts signing in CI, those secrets go only into a step that runs after a merge or on request, never into a required job, never on argv or into a log, and never into the app bundle.
- **Secrets are unleakable by construction** (CLI CLAUDE.md → the invariant of that name). A distribution build carries no credential. The device's pairing credential is created on the device when it pairs ([CLI 59a](../../cli/v0.2.3/paired-devices.md)) and kept in the device's keychain by [7a](../v0.2.4/app-connection-local-or-server.md). It is never in a build, a profile or the repository.
- **Never-listens; serving = server.** The iOS app is a client of `apogee serve` only. It opens no listening socket and starts no process. Distribution changes no part of that.
- **Additive protocol** (CLI [ADR 0006](../../../adrs/cli/backwards-compatibility.md)). A phone's app and the person's server update on their own schedules, and a TestFlight build stays installed for up to 90 days. So the app tolerates a server older or newer than itself, as the protocol already requires. Distribution must not make the app require a newer server. Version skew is 7a's capabilities handshake (consumed).
- **Parity** (CLI [ADR 0002](../../../adrs/cli/mode-parity.md); GUI [ADR 0003](../../../adrs/gui/parity.md)). Distribution adds no command and no capability. What the iOS app offers is 8b and 9a's.
- **The layers** (GUI [ADR 0001](../../../adrs/gui/layer-enforcement.md)). This item changes build settings, scripts and the pipeline only, never a module's code. The layering check reads the project's targets and build settings (`tests/architecture/layering/ModuleMap.swift:163-167` the outside settings, `:205-223` packages, products and linked frameworks) and must stay green on the iOS settings, still refusing every package, product and linked library.
- **The dev channel** (CLI CLAUDE.md → Development runs on the dev channel; GUI M18, [`app-dev-channel.md`](../macos/maintenance/app-dev-channel.md)). A development build on a device gets the dev channel's own identifier, so it never shares settings or an App Store Connect record with the release app. A live pass on a device pairs with the person's `apogee-dev serve`, never their production server.

## Technical approach

| File | Change |
|---|---|
| `lib/src/darwin/scripts/build.sh` | `TARGETS` grows `ios-arm64` (a device build) and `ios-simulator-arm64`. The macOS target, its Release build, its ad-hoc signature check (`:143-145`) and `package` stay byte-for-byte as they are. `test --target ios-simulator-arm64` runs the scheme's tests on an iPhone and an iPad simulator. One new verb, the way onto a device, shaped by the user's call: `device` (Xcode's automatic signing with the person's team, installed on a connected iPhone or iPad), or `upload` (an archive exported and sent to TestFlight). |
| `lib/scripts/cicd.sh` | darwin's targets (`:52`) grow the two iOS ones. An iOS build with `--no-defer` on a host without the SDK fails, as `macos-arm64` does today. |
| `.github/workflows/darwin.yml` | `build darwin` gains steps that build `ios-arm64` with signing off (it compiles and links) and test on the simulators. They are steps of the existing job, not a new required check (Open questions). Under the program-in-CI path only, a separate job runs on request or after a merge and uploads to TestFlight with the repository's signing secrets. It is never required. |
| `lib/scripts/changed.sh` | Unchanged. `gui-darwin` already covers `lib/src/darwin/` and `darwin.yml` (`:50-62`). |
| `lib/scripts/release-from-pr.sh` | Unchanged (Open questions: the release carries no iOS build). |
| `Apogee.xcodeproj` | The signing settings for the iOS configurations of 8a's iOS app target only (style, team, identifier and the dev channel's suffix), and the bundle's upload declarations where 8a has not added them. The macOS configurations are not edited. |
| `lib/src/cli/tests/scripts/` | A pipeline check that fails any `secrets.` reference in a job a pull request runs (Testing). |

**Reference.**
- Siblings: [8a](app-builds-for-ios-and-ipados.md), the code building for iOS, this item's gate. [8b](../ipados/v0.2.5/ipad-app.md), the iPad app. [9a](../ios/v0.2.5/iphone-app.md) and [9b](../ios/v0.2.5/foldable-iphone.md), the iPhone and the foldable, which reach the person's devices by this item's path. [9c](three-apple-platforms-whole.md), v0.2.5 whole, gated on this item. [7a](../v0.2.4/app-connection-local-or-server.md), the connection screen a freshly installed app opens on. [CLI 59a](../../cli/v0.2.3/paired-devices.md), the pairing that admits the device.
- Precedents: GUI 1a and 1d (the ad-hoc build and its pipeline, the 2026-10-20 calls), M17 (the release carries the Mac app), M18 (the dev channel's identifier), and the required-names pattern (`darwin.yml:2-11`: a required job never gets a job-level `if:`).
- Apple's terms as checked in Evidence item 7.

**Docs when it ships:** GUI `DEVELOPER.md` (the targets, CI's iOS steps, how to install on an iPhone or iPad), GUI `CLAUDE.md` (Stack & environment and the release section: the iOS build and its signing), and the CLI `DEVELOPER.md`'s pipeline section (darwin's new steps).

## Testing

All hermetic: no device, no account, no network, no binary.

- **The layering check** pins the macOS configurations' signing as ad hoc (identity `-`, `DEVELOPMENT_TEAM` empty, hardened runtime off), so it cannot drift.
- **A pipeline check** fails any job a pull request runs that names `secrets.`. It sits with the CLI suite's script checks under `lib/src/cli/tests/scripts/`. At HEAD none of them tests a workflow; a `required_checks_check.py` for `required-checks.py` is uncommitted in the working tree on 2026-11-07, and is its neighbour if it has landed when this item is built.
- **The iOS build** fails loudly when the runner has no simulator runtime: `build darwin` fails, naming the runtime, when it has no iOS simulator.
- **The Mac's packaging checks:** `build.sh build`'s `codesign --verify` and `package`'s check of the unpacked zip still pass.
- **The required checks** are unchanged (`required-checks.py --pr <N>` reads the same job names).
- **The iOS suite** runs the same hermetic tests as the Mac's: scripted fakes and recorded documents, never a device, a binary or a network.

## Open questions

| Question | Status | Answer |
|---|---|---|
| Join (or use) the Apple Developer Program, or install by personal sideloading? This call blocks the item. | `[user]` | The options. **(A1) The program, with CI signing:** a distribution certificate and an App Store Connect API key go into the repository's secrets; a job after the merge (or on request) archives, signs and uploads to TestFlight, and the person installs from TestFlight on every iPhone and iPad; builds stay testable for 90 days; costs 99 USD a year, plus the repository's first signing secrets; fork pull requests are unaffected, because the job is never on their path. **(A2) The program, signed on the user's Mac:** `build.sh upload` (or `device`) signs with Xcode's automatic signing and the user's team, from the user's machine; CI builds unsigned and holds no secret; TestFlight, or a development install on up to 100 devices per family a year; costs 99 USD a year, and a manual step each release. **(B) No program, a personal team:** `build.sh device` installs from the user's Mac on up to 3 devices; each install expires after 7 days, so the app must be re-installed weekly; no TestFlight; CI builds unsigned and holds no secret. The evidence above suggests the account already has a membership, so A1 or A2 may cost nothing new. A2 keeps the repository free of secrets and leaves A1 as a later step. |
| Is CI's iOS work a new job, or steps in the existing `build darwin` job? | `[default]` | CI's iOS work is **steps in the existing `build darwin` job**, not a new job. A new job's name is a new required check, which means `required-checks.py --pr <N>` then `--apply` (the user's to run, `darwin.yml:6-9`). Same runner, so the cost is a longer job. |
| How does CI build and test the iOS app without a secret? | `[default]` | CI builds `ios-arm64` with signing off (`CODE_SIGNING_ALLOWED=NO`) to prove it compiles and links for a device, and runs the suite on one iPhone and one iPad simulator. Both are secret-free. A missing simulator runtime on the runner fails the step by name. It is never skipped silently. |
| Does the release carry an iOS build? | `[default]` | **The release carries no iOS build.** An `.ipa` installs only on devices its profile names, or through TestFlight, so a release asset would install nowhere for anyone but the person. `release-from-pr.sh` and `apogee-gui-darwin` are unchanged. |
| What build number does each upload get on the program paths? | `[default]` | On the program paths, each upload's build number is `<lib/release/VERSION>.<n>`, with `n` the run number in CI or a local counter on the user's Mac, so two uploads of one version never collide. `MARKETING_VERSION` stays the release, as `build_app` stamps it (`build.sh:136-137`). The macOS build's stamping is unchanged. |
| What identifier and name does the iOS dev build use? | `[default]` | The iOS dev build's identifier is M18's: `com.quantumcompiler.apogee.dev`, named "Apogee Dev", with its own record if TestFlight is chosen. If M18 has not shipped when this item is built, this item sets that suffix on the iOS configurations only. |
| Which declarations does the iOS bundle carry for Apple's upload checks? | `[default]` | The iOS bundle carries the declarations Apple's upload checks require, where 8a has not added them: the local-network usage string (the person's server is often on their own network) and a privacy manifest naming the app's `UserDefaults` use. They are checked against Apple's current upload rules when the item is built. |
| Is there a Mac Catalyst or "Designed for iPad" build on the Mac? | `[default]` | No Mac Catalyst or "Designed for iPad" build on the Mac. The Mac keeps its own native target. |
| One app bundle for iPhone and iPad, or two? | `(consumed decision)` | 8a's. The default assumed here is one, for both device families. The foldable iPhone is the iPhone family: 9b's. |
| Where does the pairing credential live on the device, and which connection screen does a fresh install open on? | `(consumed decision)` | 7a's and CLI 59a's. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-10-20 | The Mac app is signed ad hoc, with no team (GUI 1a and 1d, the ad-hoc build and its pipeline; recorded at `scripts/build.sh:36-42`, `darwin.yml:25-35`). Recalled here, not made by this item. | the user |
| 2026-11-07 | Specced from the Apple-platforms spike and brainstorm. The user's calls that shape this item: iPhone and iPad are clients of the person's Apogee server only, with no core on the device; the server is an add-on: the CLI and the Mac app stay standalone and the default, and anything that makes them need a server, a network or an account is a failure; v0.2.5 is the target where macOS, iPadOS and iOS (the iPhone and the foldable iPhone) fully work; no customer accounts, billing or metering: the server admits only the person and their devices, so distribution reaches the person's own devices, not the public. | the user |
| 2026-11-07 | The Mac app's ad-hoc signing stays (the user's 2026-10-20 call) unless the user's call in Open questions changes it, and that would be its own item. | the user |
| 2026-11-07 | Placed in v0.2.5 (track 8), gated on 8a. A device can only take a build once the code builds for iOS. 9c, the v0.2.5 whole, is gated on this item, because "fully working on iPhone and iPad" means installed on them. It sits beside 8b, not after it: the path onto a device is independent of the iPad's layout, and 8b's live checks need a device build. | the spec |

## Out of scope

- The Mac app's Developer ID and notarization (a separate call).
- App Store submission and review for the public (the server admits only the person; paid features are a someday).
- The iPad and iPhone layouts (8b, 9a, 9b).
- The connection and pairing (7a, CLI 59a).
- Android and Windows, which follow v0.2.5.

## Links

- **Blocked by:** [8a](app-builds-for-ios-and-ipados.md), and the `[user]` call in Open questions · **Blocks:** [9c](three-apple-platforms-whole.md)
- **Relates to:** [8b](../ipados/v0.2.5/ipad-app.md), [9a](../ios/v0.2.5/iphone-app.md), [9b](../ios/v0.2.5/foldable-iphone.md), [7a](../v0.2.4/app-connection-local-or-server.md), [CLI 59a](../../cli/v0.2.3/paired-devices.md), [M18](../macos/maintenance/app-dev-channel.md), GUI 1a and 1d, M17, CLI [ADR 0006](../../../adrs/cli/backwards-compatibility.md), CLI [ADR 0002](../../../adrs/cli/mode-parity.md), GUI [ADR 0003](../../../adrs/gui/parity.md), GUI [ADR 0001](../../../adrs/gui/layer-enforcement.md)
