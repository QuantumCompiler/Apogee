# The Apogee logo is the app's icon and the project's official logo

**What / why.** The user's call, 2026-11-14: use `lib/design/assets/Logo.png` as the macOS app's icon and as Apogee's official logo. The asset was committed in v0.2.0 (`7b5f5003`, "Logo Commit"). Measured that day:
- **The asset:** 2048 × 2048 PNG, RGB with an alpha channel, 723 KB.
- **The artwork:** a white-outlined rocket, centered and angled up and to the right, over a full-bleed black field. The field carries a faint gray pattern of small technology glyphs (chips, atoms, chat bubbles, terminals, neural nets, padlocks).
- **The logo is used nowhere:**
  - **The app has no icon.** `lib/src/darwin` has no asset catalog (`find lib/src/darwin -name '*.xcassets'` finds none), and the project sets no `ASSETCATALOG_COMPILER_APPICON_NAME` and no `CFBundleIconName` (`Apogee.xcodeproj/project.pbxproj`). So `Apogee.app`, which every release carries since GUI M17, shows macOS's generic app icon in the Dock, Finder, the app switcher and the About panel.
  - **The rest of the project shows no logo:** not the root `README.md` (it opens with a title and a tagline), not the design canvas (`lib/design/spec`), not the release notes.

This item makes the logo the app's icon, in every size macOS asks for, and the project's official mark wherever Apogee presents itself.

**Core constraint(s).**
- **One master.** `lib/design/assets/Logo.png` stays the single source. Every size the app ships is generated from it by a script in the repo, never redrawn by hand. Re-running the script after the master changes refreshes them all.
- **Hold-time checks, not eyeballing.** A test confirms that the app's icon set is complete and was made from the current master.
- **The design's law** (`lib/design/spec/CLAUDE.md`): every design update lands in `lib/design/spec` too. Where the canvas draws the app's identity, it draws this logo.
- **Ad-hoc signing and the release are unchanged.** The icon rides inside `Apogee.app` and the `apogee-gui-darwin.zip` that GUI M17's release step already carries.
- **The dev channel** (GUI M18, [`app-dev-channel.md`](../macos/maintenance/app-dev-channel.md)): a development build named "Apogee Dev" must be told apart from the release app in the Dock. M18 owns that variant, derived from this same master.

**Seam + files.**
- `lib/src/darwin/source/main/Assets.xcassets/AppIcon.appiconset/`: the macOS icon set, 16, 32, 128, 256 and 512 points at @1x and @2x (16 through 1024 pixels), with its `Contents.json`.
- `Apogee.xcodeproj/project.pbxproj`: the app target compiles the catalog and sets `ASSETCATALOG_COMPILER_APPICON_NAME = AppIcon`. Only the app target changes; no framework.
- `lib/src/darwin/scripts/icon.sh`, new: generates the icon set from the master with `sips`, the same tool the logo's measurements were taken with. It also records the master's SHA-256 in the set's `Contents.json` `info` block, so the check below can verify it.
- `lib/src/darwin/scripts/build.sh`: `build` and `package` confirm the built app's `Info.plist` names the icon, and that `Contents/Resources/Assets.car` holds it (`assetutil --info`). Neither step regenerates anything.
- Test: `tests/architecture/` (beside the layering check) checks:
  - every size the set declares exists at its exact pixel size;
  - the recorded hash matches the master, so a changed master without re-running `icon.sh` fails, naming the stale set;
  - the app target names the icon.
- `README.md`: the logo at the top, a reduced copy at a fixed width over the title, the tagline below it.
- `lib/design/spec`: the canvas's app identity (the window's header, and the About board if the canvas draws one) shows the logo, published to the canvas per the design's law.
- Docs: darwin `DEVELOPER.md` (the icon, its script, the check), darwin `MILESTONES.md` on ship.

**Reference.**
- GUI M17 (darwin MILESTONES: the macOS app ships with its release), whose zip now carries the icon.
- GUI M18 (the dev channel's "Apogee Dev"), which derives its variant from this master.
- Items 8a and 8b ([`app-builds-for-ios-and-ipados.md`](../v0.2.5/app-builds-for-ios-and-ipados.md), [`ipad-app.md`](../ipados/v0.2.5/ipad-app.md)) reuse the same master for the iOS and iPadOS icon (a single 1024-pixel image), when those targets exist.

**Decisions made** (dated):
- 2026-11-14: The user's call: `lib/design/assets/Logo.png` is the macOS app's icon and Apogee's official logo. It is a darwin item in v0.2.1.
- 2026-11-14: Track 11, a new track in the v0.2.1 table (tracks 5 and 6 have shipped, and track 10 is taken by the admin-mode split).

**Open calls:**
- [default: the artwork is used full-bleed, as drawn, and macOS's rounded-rectangle icon mask frames it. No Liquid Glass layered `.icon` file (Icon Composer) for now; a layered version, the rocket over the field, is a later refinement if wanted.]
- [default: the same artwork at every size. At 16 and 32 pixels the gray glyphs blur into the black. The rocket stays legible because its white outline is thick. If a live look shows the small sizes reading poorly, a rocket-only crop is used for 16–32 pixels and shown to the user before shipping.]
- [default: the generated sizes are committed (a build needs no script run), and the check catches staleness.]
- [default: the README shows the logo at 160 pixels wide, a reduced PNG generated by the same script, not the 723 KB master.]
- (consumed decision: GUI M18) the "Apogee Dev" variant; (consumed decision: 8a, 8b) the iOS and iPadOS icon.

**Guardrail(s).** The icon-set check (sizes, hash against the master, the target's setting), proven on a planted stale set and a planted missing size. `build.sh package` refuses a bundle whose `Info.plist` names no icon. The design canvas's health check stays green.

**Acceptance criteria:**
- [ ] `Apogee.app` from `build.sh build` shows the rocket in Finder, the Dock, ⌘-Tab and the About panel (the user's look, against a build from this branch).
- [ ] Every size macOS asks for is in the bundle, generated from `lib/design/assets/Logo.png`; changing the master without re-running `icon.sh` fails the check.
- [ ] The README and the design canvas show the logo; the suite is green.

**Scope note.** Earmarked for v0.2.1 (track 11); ungated. Out of scope: a layered Liquid Glass icon, the iOS and iPadOS icons (8a and 8b reuse this master), the "Apogee Dev" variant (M18), and any change to the artwork itself.
