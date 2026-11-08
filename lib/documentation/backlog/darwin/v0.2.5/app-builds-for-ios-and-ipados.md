# The app's code builds for iPhone and iPad

**What / why.** The user's vision, 2026-11-07: Apogee is a private, self-hosted harness that feels like Claude on every device the person owns. A strong machine of theirs serves their own desktops and phones. The iPhone and the iPad are clients of that server only (GUI SPEC → [Core surfaces](../../../assistant/gui/SPEC.md#core-surfaces)), "built from the macOS app's Swift code". Today that code builds for the Mac and nothing else. This item makes the same code, one codebase, build and test for iOS and iPadOS, with the Mac app unchanged. Measured at HEAD `4f92316d` on 2026-11-07. Paths are under `lib/src/darwin/`.

**W9 — the app's code is the Mac's alone.**
1. **Every target is a Mac target.** The project's two configurations set `SDKROOT = macosx`, `MACOSX_DEPLOYMENT_TARGET = 27.0` and `ARCHS = arm64` (`Apogee.xcodeproj/project.pbxproj:7874-7889` Release, `:8727-8744` Debug), and no target overrides them. That covers all 56 native targets. `xcodebuild -target InfrastructureProcess -showBuildSettings` resolves `SUPPORTED_PLATFORMS = macosx`. `xcodebuild -scheme PresentationStyles -showdestinations` lists "My Mac" and "Any Mac", and rejects every iOS Simulator with "iPad (A16)'s iOS Simulator platform doesn't match PresentationStyles.framework's supported platforms". The 20 shared schemes (`Apogee` and one per Presentation module, ADR 0004) build Mac frameworks only.
2. **The layers below Presentation are free of AppKit, but not of the Mac.** The SPEC's "already free of AppKit" is true. It is not enough. The probe compiled each module whole with the iPhoneSimulator 27.1 SDK (`swiftc -swift-version 6 -target arm64-apple-ios27.0-simulator -wmo`, a scratch copy under the session's scratchpad, nothing in the tree touched):
   - `DataProtocol` and `DataPreferences` build as they are.
   - `InfrastructureProcess` fails with 8 distinct errors in 3 files. Foundation's `Process` does not exist on iOS (`infrastructure/process/ProcessSpawner.swift:50`). "`posix_spawn_file_actions_addchdir` is unavailable in iOS" (`LoginShell.swift:146`). libproc's `proc_pidinfo` and `proc_taskinfo` are not in the SDK (`ProcessMemory.swift:10-12`). `ShellPath` runs the login shell (`ShellPath.swift:191`, `:200`), so it falls with `LoginShell`. Without those four files, the rest of the module builds: `ChildCommand`, `RunningChild`, `ChildSpawner` (`RunningChild.swift:96`), `CapturedRun`, `LineFramer` and `BoundedTail`.
   - The other lower modules fail only on that one door and on the home folder. `FileManager.homeDirectoryForCurrentUser` "is unavailable in iOS" (`data/discovery/BinaryDiscovery.swift:21`, `business/jobs/SupervisedJob.swift:227`, `business/session/ApogeeClient.swift:45`). The convenience initializers name `ProcessSpawner()` directly (`business/reads/ReadRunner.swift:20`, `SupervisedJob.swift:204`, `:209`, `ApogeeClient.swift:44`).
   - With those four files stubbed and those three home lines replaced in the scratch copy, all eight lower modules build for iOS. So the lower layers' change is exactly those sites.
3. **Presentation reaches AppKit in 8 of its 19 modules.** Of 184 Swift files, 43 import SwiftUI, none imports UIKit, and 7 import AppKit: `styles/Palette.swift`, `styles/Components/MarkdownText.swift`, `models/Views/ModelsScreen.swift`, `models/Views/StoreView.swift`, `graph/Views/GraphScreen.swift`, `knowledge/Views/KnowledgeScreen.swift` and `app/Views/AppDelegate.swift`. The AppKit types in use, by module:
   - **styles:** `NSColor`'s dynamic provider (`Palette.swift:70-91`). `NSWorkspace.open` and `NSPasteboard` (`MarkdownText.swift:52`, `:59-62`). `MediaPlayer`, an `NSViewRepresentable` over AVKit's `AVPlayerView` (`MediaPlayer.swift:11-33`).
   - **app:** `NSApplicationDelegateAdaptor` (`ApogeeApp.swift:45`), and `AppDelegate: NSApplicationDelegate`, which holds ⌘Q (`AppDelegate.swift:9`, `:21`).
   - **models:** Show in Finder (`ModelsScreen.swift:330`, `StoreView.swift:344`).
   - **graph:** `NSSavePanel` twice (`GraphScreen.swift:129`, `:137`).
   - **knowledge:** `NSSavePanel` with an `NSButton` accessory (`KnowledgeScreen.swift:72-74`).
   - **video, voice, compose:** Show in Finder (`VideoScreen.swift:224`, `VoiceScreen.swift:194`, `ComposeView.swift:184`). These three files **do not import AppKit**: SwiftUI on macOS brings it along. So a check of imports alone misses them.
4. **SwiftUI's Mac-only surface is in use too.** The probe found these with AppKit's types stubbed. Each is `@available(iOS, unavailable)` or absent from the iOS interface:
   - `.buttonStyle(.link)`, 11 sites: `session/Views/TranscriptRow.swift:51`, `SessionExtras.swift:291`, `graph/Views/GraphScreen.swift:363`, `:415`, `:432`, `models/Views/StoreView.swift:152`, `:158`, `:355`, `:369`, `DownloadsView.swift:94` and `video/Views/VideoScreen.swift:241`.
   - `.pickerStyle(.radioGroup)`, 3 sites: `models/Views/ModelsScreen.swift:575`, `StoreView.swift:451` and `settings/Views/SettingsView.swift:147`.
   - `.pointerStyle(.frameResize…)`: `shell/Views/ShellView.swift:40` and `sessions/Views/SessionsScreen.swift:42`.
   - `openSettings`: `shell/Views/Sidebar.swift:10`, `:41`.
   - `onExitCommand`: `session/Views/SessionWindow.swift:29`.
   - The `Window` scene (`app/Views/ApogeeApp.swift:54`) and the `Settings` scene (`:99`).
   
   Three view-models default to the Mac:
   - `AppViewModel.swift:72` and `SessionViewModel.swift:79` default to `homeDirectoryForCurrentUser`.
   - `MonitorViewModel.swift:32` defaults to `ProcessMemory.resident`.
   
   With every site above stubbed in the scratch copy, all 19 Presentation modules built for iOS. Console, Library, Monitor, Review, Suites, Tasks and Training needed nothing of their own.
5. **The tests are the Mac's too.** The last local run's result bundle counts 981 tests (`build/macos-arm64/tests.xcresult`, `totalTestCount`). Of them, 123 sit in 14 files that name the Mac:
   - 11 files spawn real children through `ProcessSpawner`, or read `ProcessMemory`, in `tests/infrastructure/process/`, `data/discovery/BinaryDiscoveryTests.swift`, `business/jobs/JobGroupTests.swift`, `business/session/` (`ChatLaunchTests`, `MachineSessionTests`, `RealBinaryTests`), `presentation/app/ChildrenSeenTests.swift` and `presentation/sessions/OverrideSkippedTests.swift`.
   - 3 styles files import AppKit or use `NSPasteboard` (`CodeLexerTests`, `MarkdownTests`, `StylesTests`).
   
   The layering check reads the repository from disk (`tests/architecture/layering/SourceTree.swift:13-14`, `DiskTree`).
6. **The layering check knows no platform.** It holds every target to one folder "with no membership exceptions" (`LayeringCheck.swift:15-16`, `ModuleMap.swift:189`). It also holds the UI frameworks, which already include `UIKit` (`LayeringCheck.swift:41-43`), to Presentation and out of view-models. It reads no `SUPPORTED_PLATFORMS`. So nothing stops a module from going Mac-only, or a cross-platform file from reaching AppKit. ADR 0004's scope is "The macOS application" ([module-schemes-and-previews.md](../../../adrs/darwin/module-schemes-and-previews.md)).

This item delivers:
- **Every framework, its test bundle and the app target building for macOS, iOS and iPadOS** from one codebase. The child-process transport stays macOS-only. 7a's network transport is present everywhere.
- **The Mac's code isolated in platform files inside its modules.** Every AppKit use and every Mac-only SwiftUI call goes into such a file, with an iOS counterpart where the iOS app needs the piece to exist.
- **The layering check, per platform**, and a new ADR that records the rule.
- **Each Presentation module's scheme building for both** the Mac and the iOS Simulator (ADR 0004).
- **The hermetic suite running on an iPhone and an iPad simulator.**
- **An iOS app that launches** to the shell, composed over 7a's connection only. It is not yet a product: its layouts are 8b's and 9a's, and getting it onto a device is 8c's.

**Core constraint(s).**
- **The anti-goal: nothing here may make standalone use need a server** (the user's call, 2026-11-07; SPEC → Non-goals, No server requirement). The Mac app over pipes to its local CLI stays complete and the default.
  - Every Mac code path moves verbatim into a `+macOS` file. None is routed, wrapped or removed.
  - The Mac's composition still builds every place over `ProcessSpawner`, discovery and the login-shell `PATH`.
  - The server-only composition exists only in `+iOS` files and never compiles into the Mac app.
  - A Mac that never pairs runs exactly today's code.
- **Never-listens / serving = server** (the CLI's *Interactive turns never open a listening socket*; GUI SPEC → No HTTP path for local work). The iOS build has no `ProcessSpawner` at all, so it cannot start a process. It reaches the CLI only through 7a's outbound connection, opens no listening socket, and runs no model (no on-device core, the user's call).
- **The machine-mode client rule, as amended 2026-11-07** ([CLAUDE.md → The GUI is a machine-mode client](../../../assistant/darwin/CLAUDE.md)): one protocol, two transports. `EventDecoder` and every read decoder are the same code on both platforms. Only the transport differs.
- **The layers** (GUI [ADR 0001](../../../adrs/gui/layer-enforcement.md), [ADR 0002](../../../adrs/darwin/presentation-mvvm.md)):
  - UIKit joins AppKit in Presentation only.
  - Platform files never put a UI framework below Presentation or in a `ViewModels/` file.
  - A view-model stays Foundation and Observation only. A platform default it needs is handed in by the composition, never chosen inside it.
  - Shared visuals stay in `styles`. An iOS counterpart of a styles component is a `styles` file, never a second copy in a place.
- **Secrets are unleakable** (the CLI's *Secrets are unleakable by construction*; the GUI rule *Secrets never pass through the GUI*). 8a adds no secret and no place to keep one. The pairing credential is 7a's, in the Keychain.
- **Additive protocol** (the CLI's [ADR 0006](../../../adrs/cli/backwards-compatibility.md)). The iOS build tolerates unknown events and fields by the same decoder, and needs no protocol change.
- **Parity** (GUI [ADR 0003](../../../adrs/gui/parity.md); the CLI's [ADR 0002](../../../adrs/cli/mode-parity.md)). There is one `ParityTable` for every platform, and no carve-out of the iOS build's own. A Mac-only affordance absent from the iOS build in 8a is a gap that 8b, 9a and 7c close, never a reclassified row.
- **The dev channel** ([CLAUDE.md → Development runs on the dev channel](../../../assistant/darwin/CLAUDE.md#development-runs-on-the-dev-channel); M18 [`app-dev-channel.md`](../macos/maintenance/app-dev-channel.md)). An iOS development build carries the dev identity ("Apogee Dev", M18's identifier), never the release one. On the Mac, a development build still drives `apogee-dev` only.
- **Swift 6, strict concurrency, warning-free on both platforms** (CLAUDE.md → Code Style).

**Seam + files.**
- `Apogee.xcodeproj`:
  - Every framework target and test bundle: `SUPPORTED_PLATFORMS = "macosx iphoneos iphonesimulator"`, `SDKROOT = auto`, `IPHONEOS_DEPLOYMENT_TARGET = 27.0` beside the Mac's 27.0, `TARGETED_DEVICE_FAMILY = "1,2"` (iPhone and iPad), `SUPPORTS_MACCATALYST = NO`.
  - The app target `Apogee`: the same, with its iOS keys under `[sdk=iphone*]` conditions so the Mac's resolve unchanged. Those keys are the generated Info.plist's launch screen, scene manifest and orientations, `SUPPORTS_MAC_DESIGNED_FOR_IPHONE_IPAD = NO` and `SUPPORTS_XR_DESIGNED_FOR_IPHONE_IPAD = NO`.
  - The app embeds the same frameworks on both.
  - No new target and no membership exceptions.
- `source/infrastructure/process/`:
  - `ProcessSpawner.swift`, `LoginShell.swift` and `ProcessMemory.swift` become `+macOS` files, unchanged inside.
  - `ShellPath.swift` keeps its values (`ShellPath`, `Resolution`, `wellKnownDirectories`, `launchdPath`) everywhere. Its run of the login shell, `.app` and `answer()` move to `ShellPath+macOS.swift`.
  - The infrastructure layer card names the platform files in its prose, outside the backticked **Modules** line, which the check reads as module names.
- `source/data/discovery/BinaryDiscovery.swift`: the `home:` default moves to a `BinaryDiscovery+macOS.swift` overload. Discovery stays a Mac fact, and the iOS composition never calls it.
- `source/business/reads/ReadRunner.swift`, `business/jobs/SupervisedJob.swift` and `business/session/ApogeeClient.swift`: the convenience initializers that name `ProcessSpawner()` and `homeDirectoryForCurrentUser` move to `+macOS` files. The injected initializers stay everywhere, and the iOS composition hands them 7a's `RemoteSpawner` (through 7a's `RoutingSpawner`, whose local branch exists only on the Mac).
- `source/business/device/` and 7b's Presentation pieces ([7b](../macos/v0.2.4/mac-offers-its-files-and-shell.md), queued before this item): the Mac offering its own files and shell is Mac code. `DeviceToolHost`'s injected initializer stays everywhere, and anything that starts the device child over `ProcessSpawner` moves to `+macOS` files. The grant sheet, the **On this Mac** card and Settings' **This Mac** pane are `+macOS` files. The iOS composition never builds the host, so the iOS build offers no device tools. If 7b has not shipped when this item is built, the rule applies to it when it lands.
- `source/presentation/styles/`:
  - `Palette+macOS.swift` (`NSColor`) and `Palette+iOS.swift` (`UIColor`'s dynamic provider on `userInterfaceStyle`), both feeding the one public `Palette.color`.
  - `MarkdownText`'s link opener and code copy as `+macOS` (`NSWorkspace`, `NSPasteboard`) and `+iOS` (the system's URL opener, `UIPasteboard`). `MarkdownDocument.swift:618`'s comment follows.
  - `MediaPlayer+macOS.swift` (`AVPlayerView`, as today) and `MediaPlayer+iOS.swift`, a `UIViewControllerRepresentable` over `AVPlayerViewController`. It names AVKit's type, for the reason `MediaPlayer.swift:5-10` records.
  - Two shared components that every place uses in place of the Mac-only styles: `Components/LinkButtonStyle` (`.link` on the Mac, a plain tinted button on iOS) for the 11 `.link` sites, and `Components/ChoicePicker` (`.radioGroup` on the Mac, `.inline` on iOS) for the 3 `.radioGroup` sites.
- `source/presentation/shell/`: the divider's `pointerStyle` and the sidebar's `openSettings` row go to `+macOS` files. On iOS the divider is not drawn and the Settings row is absent until 8b gives iOS its Settings place.
- `source/presentation/session/Views/SessionWindow+macOS.swift`: `onExitCommand`. Esc on iOS is 8b's.
- `source/presentation/sessions/`: the inspector divider's `pointerStyle`, as the shell's.
- `models/`, `graph/`, `knowledge/`, `video/`, `voice/`, `compose/`: the Show in Finder buttons (5) and the save panels (graph 2, knowledge 1) move into each module's `Views/…+macOS.swift`. The iOS build draws neither.
- `source/presentation/app/`:
  - `ApogeeApp+macOS.swift` keeps the `Window` and `Settings` scenes and the delegate adaptor, and `AppDelegate+macOS.swift` keeps ⌘Q's wait.
  - `ApogeeApp+iOS.swift` has one `WindowGroup` mounting the same `ShellView`.
  - `AppViewModel`'s `home:` default goes to the Mac file. The iOS composition builds the places over 7a's server routing only: no discovery, no login shell and no monitor memory by process id (`MonitorViewModel.residentBytes` handed `{ _ in nil }`).
  - With nothing paired, the iOS app shows 7a's not-paired state. Pairing on iOS is 8b's.
  - `SessionViewModel.folder`'s default becomes the composition's to hand in.
  - `source/main/ApogeeMain.swift` is unchanged (`ApogeeApp.main()` on both).
- `tests/`:
  - The 123 Mac-bound tests move into `+macOS` test files in their bundles, so they compile and run on the Mac only. The rest of each file runs on both. The styles tests split by what they touch: the pasteboard on the Mac, everything else everywhere.
  - `architecture/layering`: its suite runs on the Mac, because it checks the project and reads it from disk. It gains the per-platform rules (Guardrails), each pinned on a planted miniature as the existing ones are (`LayeringTests.swift`, `planted.change`).
- Docs, in the same change:
  - a new GUI ADR 0005, `apple-platforms.md`: one codebase for three Apple platforms; platform files; the per-platform check; each Presentation scheme built per platform, extending ADR 0004's scope, which is append-only and is named, not edited;
  - GUI `CLAUDE.md`: Stack & environment's platforms, the platform-file rule under Code Style, the Codebase Map's platform files, and the checklist line;
  - `DEVELOPER.md`: the `xcodebuild` lines that build for and test on the iOS Simulator, and which tests are the Mac's;
  - the four layer cards (`source/<layer>/CLAUDE.md`).

**Reference.**
- [7a](../v0.2.4/app-connection-local-or-server.md), this item's gate. Its `RemoteSpawner`/`RoutingSpawner` is the only transport the iOS build has. Its constraint "No UI framework is used below Presentation, which 8a relies on" is the one this item extends to the Mac's non-UI APIs.
- [8b](../ipados/v0.2.5/ipad-app.md), the iPad's layouts, Settings, pairing and keys, built on this. [8c](iphone-and-ipad-distribution.md), the `build.sh` targets, CI's iOS steps and the path onto a device, built on this.
- [7b](../macos/v0.2.4/mac-offers-its-files-and-shell.md), the Mac's device tools, whose host and sheets are Mac files here. Its default "iPhone and iPad offer no device tools" is the one this item builds.
- [9a](../ios/v0.3.0/iphone-app.md), the iPhone's compact shell. [7c](../v0.2.4/every-place-against-a-server.md), what each place does against a server, including the iOS form of Show in Finder and the save panels.
- Precedents:
  - 1a, the Xcode project as the module map.
  - 4c's `ChildRegistry` and `ProcessSpawner`, the door being isolated.
  - The rule that a public default argument names only its own module (CLAUDE.md → Code Style, 2026-10-20), which is why the Mac defaults become Mac-file overloads.
  - ADR 0004, the per-module schemes.
  - M18, the dev identity.

**Decisions made** (dated):
- 2026-11-07: Specced from the Apple-platforms spike and brainstorm. The user's calls that shape this item:
  - Apogee is a private, self-hosted harness that feels like Claude on every device the person owns, a strong machine of theirs serving their own desktops and phones. Paid features are a someday: no customer accounts, billing or metering, so this item adds no account of any kind.
  - Where the work happens: server tools by default, and a connected desktop offers its own files and shell. Device tools touch the device's own files, so they are the Mac's (7b), and the iOS build carries none.
  - iPhone and iPad are clients of the person's Apogee server only, with no core on the device. The Mac and the CLI stay standalone.
  - Remote mode is an add-on, never the only way the application runs. Anything that makes the Mac app need a server, a network or an account is a failure.
  - The "iPhone duo" is the foldable iPhone, whose two screens are 9b's. This item builds its device family.
  - v0.3.0 is the target where macOS, iPadOS and iOS fully work, in successive versions before it.
- 2026-11-07: In v0.2.5 (track 8), gated on 7a. Without 7a's connection the iOS build would have no transport at all, and an app with nothing to drive. With it, the iOS composition reaches the server through the same seam every place already uses. 8b and 8c are gated on this item, because both need code that builds for iOS.
- 2026-11-07: One codebase, not a fork. This is the SPEC's "built from the macOS app's Swift code". The probe shows the cost is bounded: 4 files and 7 lines below Presentation, and in Presentation 7 AppKit imports and about 35 Mac-only call sites (W9.2–W9.4).

**Open calls:**
- [default: a platform file is named `<Name>+macOS.swift` or `<Name>+iOS.swift` and is wholly inside `#if os(macOS)` or `#if os(iOS)`. iPadOS is `os(iOS)`. Nowhere else does `#if os(` or a `canImport` choose a platform. The one-folder, no-exceptions rule stands, so no per-file platform filters are used.]
- [default: `InfrastructureProcess` stays one module, with its Mac files guarded, rather than splitting into a protocol module and a Mac module. Every importer already reaches it, and the protocols it declares are what 7a's transport implements.]
- [default: one multiplatform app target, plain `Apogee` (the repository's naming), with its iOS keys under `[sdk=iphone*]` conditions, not a second app target. The layering check keeps its one application path (`LayeringCheck.swift:46-47`). This is what 8c's "the iOS configurations of 8a's iOS app target" edits.]
- [default: iOS and iPadOS 27.0, arm64, iPhone and iPad device families. No Mac Catalyst, no "Designed for iPad" on the Mac or Vision Pro: the Mac's app is the native one.]
- [default: Mac-only affordances with no iOS meaning yet (Show in Finder ×5, the three save panels, the divider drags, Esc) are absent from the iOS build. They are never stubbed to do nothing visibly. Their iOS forms are 7c's, 8b's and 9a's.]
- [default: the suite runs on one iPhone and one iPad simulator of the iOS 27.0 runtime, the newest of each the runtime carries. This Mac has the iOS 27.0 runtime and the 27.1 SDKs (`xcrun simctl list runtimes`, `xcrun --sdk iphonesimulator --show-sdk-path`).]
- [default: the layering check also refuses the AppKit names found here (`NSWorkspace`, `NSPasteboard`, `NSSavePanel`, `NSOpenPanel`, `NSApplication`, `NSColor`, `NSView`, `NSButton`) outside a `+macOS` file, because three files reach `NSWorkspace` with no AppKit import (W9.3).]
- (consumed decision) `build.sh`'s iOS targets, CI's iOS build and test steps, and signing are 8c's. Until 8c lands, the iOS build and suite are the `xcodebuild` lines in `DEVELOPER.md`.
- (consumed decision) The transport, pairing and the not-paired state are 7a's (and CLI 59a's). The iPad's shell, Settings, pairing, Esc and keys are 8b's. The iPhone's compact shell is 9a's, and the foldable's two screens are 9b's.
- (consumed decision) iPhone and iPad offer no device tools: no shell, and their files sit in the app's container. That is 7b's default, and this item builds it by keeping the device-tool host and its sheets in `+macOS` files.
- (consumed decision) The dev identity's identifier and suffix are M18's, and the iOS configurations inherit them. [default: if M18 has not shipped when this item is built, M18 is built first (it is ungated), so no iOS Debug build ever carries the release identity.]
- No [user] call blocks this item. The Apple Developer Program call is 8c's.

**Guardrail(s).**
- **The Mac is unchanged.**
  - Every target's resolved macOS build settings (`-showBuildSettings -destination 'platform=macOS'`) are compared before and after. Only `SDKROOT`, `SUPPORTED_PLATFORMS` and the iOS-only keys this item sets (`IPHONEOS_DEPLOYMENT_TARGET`, `TARGETED_DEVICE_FAMILY`, `SUPPORTS_MACCATALYST` and the `[sdk=iphone*]` keys) may differ.
  - The Mac suite still runs every one of its tests. The count from `totalTestCount` (981 at HEAD) may only grow, and none is lost to a guard.
  - `build.sh build`'s ad-hoc signature check and `package` pass unchanged.
- **The layering check's new rules**, each pinned on a planted miniature:
  - every target supports exactly macOS, iOS and the iOS Simulator;
  - AppKit is imported only in a `+macOS` file, and UIKit only in a `+iOS` file, both still in Presentation and never in a `ViewModels/` file;
  - a platform file is wholly guarded by its platform;
  - no `#if os(` or platform `canImport` sits outside a platform file;
  - the AppKit names above appear only in `+macOS` files;
  - every Presentation scheme's target supports both platforms (ADR 0004 per platform).
- **The iOS build compiles and its suite passes**, by the `DEVELOPER.md` lines, warning-free. Until 8c puts it in CI, a compile break on iOS that the check's textual rules miss (a Mac-only SwiftUI call in a shared file) is caught only by that local run. The build's record says so.

**Acceptance criteria:**
- [ ] `xcodebuild -scheme Apogee` builds for an iPhone and an iPad simulator and for `generic/platform=iOS` with signing off, warning-free.
- [ ] The hermetic suite passes on an iPhone and an iPad simulator. `DEVELOPER.md` names the Mac-only tests and their count.
- [ ] Each of the 19 Presentation schemes lists the iOS Simulators as eligible in `-showdestinations` and builds for them.
- [ ] No file outside a `+macOS` file imports AppKit or names an AppKit type. No file outside a `+iOS` file imports UIKit.
- [ ] On the Mac, `build.sh build`, `test` and `package` pass, the app is the same app, and the full suite is green.
- [ ] The iOS app launches in the simulator to the shell and starts no process. With nothing paired, it says so in 7a's words.

**Scope note.** Earmarked for v0.2.5 (track 8); 🔒 gated on [7a](../v0.2.4/app-connection-local-or-server.md). Out of scope:
- the iPad's layouts, Settings, pairing and keys (8b);
- getting the app onto a device, the `build.sh` targets and CI (8c);
- the iPhone (9a) and the foldable (9b);
- what each place does against a server (7c);
- device tools on iPhone and iPad, which offer none (7b's default);
- any new design board, since this item draws no new screen and the not-paired state is 7a's;
- Mac Catalyst, watchOS and visionOS;
- Windows and Android, which follow v0.3.0 (a recorded default).
