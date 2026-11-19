# Video and the runtime consent

**What / why.** Port the Video place's clips tab and the one-time runtime install from the old prototype into the React app: the clips list, a clip's page with its player, the generate form, a run as it goes, the install consent sheet and the install card. It also moves the media player into the shared `styles` module, where Compose (1l) and Voice (1m) pick it up. Measured on 2026-11-14 (the recon of that day):
- **The prototype source to port** (`lib/design/spec/html/main.html`, its `renderVals()` sections):
  - media shared 6521–6566 (46 lines): `player`, `seek`, `phaseRow`, `pipeOpts`, `captionsNote`;
  - video clips 6567–6670 (104): the list, a clip's page, its facts and its lineage chain, the run card;
  - the generate form 6671–6818 (148): builds `vid` (6798), the tabs and the exact `apogee video generate` command;
  - runtime consent 7262–7299 (38): `rtc` (7266) and the install card `inst`, exposed as `inc` (7284).
- **Its markup:** `ui.isVideo` 1597–1984 (388 lines), of which this item takes the tab bar and the clips block `isClips` 1608–1761 (`isCompose`, 1763–1983, is 1l's), plus the consent sheet `ui.runtimeOpen` 2431–2443.
- **Its helpers and clock:** the "video and voice" helper group (from 4191: `framesOf`, `clipName`, `freshClipForm`, `formFrom`, `clipErrors`, `buildRun`, `vrunTiming`, `vrunAt`, `startGenerate`), and two `tick()` steps: a run turns into a clip after `vrunTiming().total`; an install ends after 11 ticks, the first try failing on purpose and the next going in and re-running the refused generate. The player's position and the run's phases are computed from `clock` at render time, not stepped.
- **Line ranges** are the recon's (the 7,604-line working file). The file moves daily. At HEAD (7,300 lines) the same sections sit 295 lines higher in `renderVals()` (media shared at 6226) and 26 higher in the markup (`ui.isVideo` at 1571). In the working file at this writing (7,780 lines) they sit 176 lower and 7 lower. Sizes match, except runtime consent: 31 lines at HEAD, 38 once the one-install-at-a-time guard landed. Find each block by its section comment (`/* ---------- video: clips ---------- */` and so on) and re-read the ranges at 1c's frozen baseline.
- **Presets (4)**, each with its Screen board: `video` (ScreenVideoClip), `video:run` (ScreenVideoRun), `video:install` (ScreenVideoInstall), `consent` (ScreenVideoConsent). The `video` and `consent` branches of `applyStart()` set them (2697–2714 at HEAD).
- **The Mac module:** `video` has 5 files, 1,357 lines (`VideoViewModel.swift` 410, `VideoScreen.swift` 561, `Previews/` 386) and 27 named previews. The shared parts sit in `styles/Components/`: `MediaPlayer` (42), `InstallCard` (132), `InstallConsentSheet` (63). Video, Compose and Voice all use them.
- **The data:** the prototype's 5 clips are invented (critic V3: 0 CLI-shaped keys). The Mac app's `tests/business/media/Fixtures` holds 41 recorded files, and those behind this screen are:
  - `clip-mlx.json` and `clip-torch.json`: real `video.generation` records;
  - `generate-refused-runtime(-m29).err/.jsonl`: the refusal that raises the consent;
  - `install-video-done.txt`, `install-video-failed.err/.txt`: the install's two endings;
  - `engine-command-verdicts.txt`: the commands the CLI accepts.
  - `business/reads/Fixtures` adds `media-default-video.txt` (and `-unset`), `media-list.json` and `store-info-kind-video.json`.
- **What no one recorded:** a successful generate's step stream (only refusals were recorded), clips with a parent (both records have `"parent":null`), and the prototype's refusal line about LoRAs. The CLI does say that line (`video_generate.cpp:696`: "a LoRA loads through peft, which the video runtime does not have yet").

**Core constraint(s).**
- **The four layers and the module shape** (the design's ADR 0001 from 1a, mirroring [`layer-enforcement.md`](../../../adrs/gui/layer-enforcement.md) and [`presentation-mvvm.md`](../../../adrs/darwin/presentation-mvvm.md)). The view-models are plain TypeScript classes over injected services and never import React. `video` imports only `shell` and `styles`, never `compose` or `voice`.
- **The prototype is the source of truth.** The port mirrors the prototype's behavior and wording. Where the Mac app differs, the prototype wins (named here so 1s can call each one intended):
  - the Mac app reads the clip library from record files on disk (`MediaLibrary`, `~/Movies/Apogee`); the port reads the same record shape from the stand-in;
  - the Mac app shows a run as step events (`JobProgress`); the prototype draws three phases (load the pipeline, denoise, assemble the clip) and a log;
  - the Mac previews show the torch-and-diffusers refusal; the prototype's `consent` preset shows the LoRA-needs-peft one;
  - the Mac app's install fails only when the CLI's does; the prototype's fails once on purpose, so the failed card is reachable.
- **Every part maps to the CLI** ([the design's rules](../../../../design/spec/CLAUDE.md)). Every run is the command a person would type, built from the form. Every refusal and every install line is the CLI's own words, from a recorded or hand-made file.
- **Nothing installs without a yes.** The consent sheet asks once; Decline starts nothing; Install starts one supervised install with Stop. While another install runs (Settings' `apogee bootstrap`), Install starts nothing and says why, as the prototype does.
- **The old prototype is frozen** (fixes only) from 1c's baseline.

**Seam + files.**
- `lib/design/prototype/src/presentation/video/`:
  - `ViewModels/VideoViewModel.ts`, named after the Mac's. It holds the tab, the clips, the selection, the form, the run, the consent and the install.
  - `Views/VideoScreen.tsx` with the clips list, the clip page (player, actions, facts, LoRAs, the lineage chain, files, the command), the generate form and the run card. Like the Mac's `VideoScreen<Compositions>`, it takes the compositions view as a slot the app root fills.
  - `Previews/`: one named preview per visible state, reusing the Mac's 27 names where they fit.
- `presentation/styles/Components/`: `MediaPlayer` (from `player`, `seek`), `PhaseRow`, `InstallCard` (`inst`), `InstallConsentSheet` (`rtc`). Each one is promoted on its second use, and each already has a second user (1l, 1m, 1i).
- `business/media/`: the clip record read from a `video.generation` document; the form to the `apogee video generate` argument list (`ClipForm` in the Mac); the consent read from a refusal (`InstallConsent`); the install's output folded into its card (`RuntimeInstall`).
- The stand-in (1d): the 4 presets as `{fixtures, route, clock}`, and a run sequence for the install (failed, then done).
- Hand-made files, in the CLI's exact shape and named `*-handmade*`:
  - the prototype's lineage clips (draft, final, extension) as `video.generation` records keyed like `clip-mlx.json`;
  - `video-generate-run-handmade.jsonl`, a generate's step stream in the job protocol's shape;
  - `generate-refused-peft-handmade.err`, holding the line from `video_generate.cpp:696`.
- Tests: `presentation/video/` view-model tests, the per-preset check for the 4 presets, and the `business/media/` folds.

**Reference.**
- The darwin module: `lib/src/darwin/source/presentation/video/`, `styles/Components/{MediaPlayer,InstallCard,InstallConsentSheet}.swift`, and `business/media/{ClipForm,InstallConsent,MediaRecords}.swift`.
- [`module-schemes-and-previews.md`](../../../adrs/darwin/module-schemes-and-previews.md): named previews per state.
- The old design guide's node check ([`lib/design/spec/CLAUDE.md`](../../../../design/spec/CLAUDE.md), "Checking a change"), which the per-preset test replaces.
- Siblings: [`DEVELOPER.md` → the old prototype's baseline](../../../assistant/design/DEVELOPER.md#the-old-prototypes-baseline) (1c's baseline, shipped), [`DEVELOPER.md` → the CLI stand-in](../../../assistant/design/DEVELOPER.md#the-cli-stand-in) (1d), [`styles-shell-and-the-first-screen.md`](styles-shell-and-the-first-screen.md) (1e), [`compose-screens.md`](compose-screens.md) (1l), [`voice-screen.md`](voice-screen.md) (1m), [`settings-and-appearance.md`](settings-and-appearance.md) (1i), [`gallery-and-foundations.md`](gallery-and-foundations.md) (1q), [`parity-proof.md`](parity-proof.md) (1s).

**Decisions made** (dated):
- 2026-11-14: The user's call: rebuild the prototype in React and TypeScript with the production GUIs' four layers and MVVM modules, so the prototype stays the design's cross-platform source of truth.
- 2026-11-14: The recorded default: data comes from the Mac app's recorded CLI fixtures, read in place. A state nobody recorded gets a hand-made file in the CLI's exact shape, named `*-handmade*`.
- 2026-11-14: The recorded default: parity is judged on layout, states, controls and wording, not pixels or data.
- 2026-11-14: The split: Video and the consent are one item, because the consent is raised from the generate form and its install card is drawn in the clips list. It moves the player to `styles`, so 1l and 1m wait on it.

**Open calls:**
- [default: the player plays off the stand-in's clock at the prototype's rate (0.4 s of clip per tick) and freezes under `still`. It decodes no real video and draws the prototype's frame.]
- [default: the install's first try fails and the next succeeds, as in the prototype. The stand-in serves `install-video-failed.*`, then `install-video-done.txt`, then re-runs the refused generate.]
- [default: until 1l lands, the app root fills the compositions slot with a plain placeholder, as the Mac previews do ("The compositions").]
- [default: the run is a job in the stand-in's jobs service, not view-model state. The sidebar, the monitor and the models screen read it from there (the prototype's `vrun` is read by all three). Places already built show it now; later ones pick it up when they land.]
- [default: Settings' "Install peft…" button on the `Video · diffusers` backend row opens the same sheet. Whichever of 1i and 1k lands second wires it.]
- (consumed decision: 1d) the stand-in's job, run-sequence and preset types, and where hand-made files live; (consumed decision: 1e) the `styles` module and the two media tokens; (consumed decision: 1l) the compositions tab's content; (consumed decision: 1q) the ComponentsMedia board, drawn from these real components; (consumed decision: 1s) the parity verdicts.

**Guardrail(s).**
- **The per-preset test** for `video`, `video:run`, `video:install` and `consent`: build the screen from the preset, step 30 ticks, and fail on a throw or on `undefined` or `NaN` in the output. Proven on a planted `undefined` in a clip's facts.
- **View-model tests:**
  - a generate refused for its runtime opens the consent with the CLI's line, and Decline starts nothing;
  - Install runs once, fails in the CLI's words, then goes in on the second try and re-runs the refused clip;
  - Install while a bootstrap runs starts nothing;
  - Stop on a run leaves no clip and no record;
  - the player toggles, seeks and stops at the clip's end.
- **The command test:** the form's argument list for the default, `--engine mlx` and `--engine torch` matches the "taken" lines of `engine-command-verdicts.txt`.
- **The shape check:** every hand-made record parses through the same reader as `clip-mlx.json`, with the same keys.
- **The layering check (1b)** fails on a planted `import React` in `VideoViewModel.ts` and on a planted `video` → `compose` import.

**Acceptance criteria:**
- [ ] The built `lib/design/main.html` opens at each of the 4 presets and shows the list, the clip page and player, the run, the install card and the consent sheet. Each matches 1c's baseline in layout, states, controls and wording, and every difference is listed as real data or a defect.
- [ ] Clicking through works from the file: on the `rain` clip (it uses a LoRA), Generate opens the consent; Install fails, Install again succeeds, and the clip is made. Stop on a run leaves nothing behind.
- [ ] `MediaPlayer`, `InstallCard` and `InstallConsentSheet` live in `styles`, and the video module uses them from there.
- [ ] A named preview exists for each visible state, and the local checks (typecheck, tests, layering, staleness) are green.

**Scope note.** Earmarked for v0.2.1 (design track 1); gated on 1e. It gates 1l and 1m on the shared player. Out of scope:
- the compositions tab (1l) and voice (1m);
- the Settings panes (1i), except wiring the shared sheet;
- the gallery's ComponentsMedia board (1q);
- any change to the CLI or the Mac app;
- real video playback.
