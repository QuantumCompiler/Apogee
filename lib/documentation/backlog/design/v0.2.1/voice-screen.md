# Voice screen

**What / why.** Item 1m ports the prototype's Voice place into the React app: the list of speeches, the form that speaks new text, and the selected speech with its waveform, player, sentence table and record. It is one of the feature items of track 1 (the user's call, 2026-11-14: rebuild the prototype in React with the production GUIs' layers). Measured on 2026-11-14:
- **The source being ported** (`lib/design/spec/html/main.html`, line numbers at HEAD, `48f46831`, 7,300 lines):
  - the `voice` section of `renderVals()`, 6886–6966 (81 lines), which builds `vox`;
  - the `ui.isVoice` markup block, 1960–2036 (77 lines), with the selected speech in `vox.hasSel`, 2005–2032.
  - The recon's working-tree copy placed them at 7181–7261, 1986–2062 and 2031–2058. The working tree keeps moving under another session's uncommitted design work (the section starts at 7357 now), but both blocks are unchanged (diffed identical, HEAD against the working tree). [1c](../../../assistant/design/DEVELOPER.md#the-old-prototypes-baseline)'s baseline (shipped, at `b375fac9`) pins the numbers the port works from.
- **The helpers it leans on:** `splitSentences` and `spokenSecs` (4041–4047); `voiceList()` (4065–4070), a fixed list of 28 voices in 2 groups; the shared `player` and `seek` in "media: shared" (6226–6271, owned by 1k); the speech step in `tick()` (2795–2805), where a pending speech becomes a finished one after 5 ticks.
- **Its state:** `speeches`, `speechSel`, `vf` (the form) and `vsay` (the run) at 2630–2636, and `defaultVoice` at 2575. Others read it too: the sidebar's ⌘7 badge (4818) and the monitor's process row "Kokoro-82M · speaking" (4319).
- **One preset, `voice`:** `applyStart` routes to the screen (2692) and leaves the player paused 3.4 s into the first speech (2728). One board draws it, `ScreenVoice.dc.html` (canvas: Build page, row y 4040, x 10640, "Voice").
- **The data is invented** (critic V3). The two speeches (`walkthrough-intro.wav`, `standup-reminder.wav`), the 28 voices and the "Kokoro-82M · cpu" line are hand-written, and the sentence timings are estimated from text length (`spokenSecs`: characters / 15.2).
- **The CLI's real documents exist as files:**
  - `speech-torch.json` and `speech-mlx.json` in `lib/src/darwin/tests/business/media/Fixtures/` are full `voice.speech` records: two sentences each, with `segments`, `sample_rate`, `device`, `run_seconds` and `created_at`.
  - `models-stored-info-voice.json` and `models-stored-list.json` in `tests/business/reads/Fixtures/` give the store's voice model and its `voices` field (3 voices in 2 languages).
  - No successful `voice say` stream was ever recorded. The only voice streams are the two refusals, `voice-refused-runtime*.jsonl`.
- **The Mac module `voice`:** `VoiceViewModel.swift` (367 lines), `VoiceScreen.swift` (453 lines, 18 named previews), 3 `Previews/` files and 6 test files.

**Core constraint(s).**
- **The prototype is the source of truth.** The React screen draws what the prototype draws: the speak form inline on the screen, the list, the selected speech, its three actions ("Use these settings", "Use this voice for compositions", "Show in Finder") and both command lines. Where the Mac app differs, this document names the difference and does not port it:
  - the Mac app speaks from a **say sheet** (`sheetOpen`), not an inline form;
  - it lets you **pick the voice model**, Kokoro on PyTorch or its MLX build (CLI 53m);
  - it finds speeches by **scanning folders** for their records, `~/Music/Apogee` plus folders you add;
  - it **asks to install the voice runtime** when a speech is refused for it. The prototype's voice runtime is always installed (`runtimes.voice: true`, 2577).
- **The layers** (the design's ADR 0001, from [1a](../../../adrs/design/react-design-prototype.md)): `VoiceViewModel` is a plain TypeScript class that never imports React. Its services are injected: the stand-in's read runner, job starter, speech records and preferences. The view only draws it.
- **Every string is the binary's.** Voices come from `models info`'s `voices` field. Speeches, timings and facts come from the record. The record's timings are used as written, never estimated again, so `spokenSecs` does not survive the port.
- **One player.** The waveform's play button, time and seek use 1k's shared player from `styles`, never a copy.
- **Shared state goes through services.** The chosen voice for compositions is a preference, read by [1l](compose-screens.md) and [1i](settings-and-appearance.md). The header's model line opens the store ([1h](models-screens.md)). The sidebar badge and the monitor row read the running speech from 1e's shell.

**Seam + files.** Under the prototype's source (`lib/design/prototype/`, laid out by [1b](../../../assistant/design/DEVELOPER.md)):
- `presentation/voice/ViewModels/VoiceViewModel.ts`: the speeches, the selection, the form (text, voice, speed, output name, errors, command), the run, and the selected speech's view (bars, sentences, facts, command). Its names follow the Mac's where they exist: `speeches`, `selected`, `voices`, `text`, `voice`, `speed`, `run`, `select`, `speak`, `plannedArguments`, `currentSentence`.
- `presentation/voice/Views/`: the screen, the speak card, the speech list and the selected speech.
- `presentation/voice/Previews/`: named states, each built over the stand-in at fixed fixtures:
  - a speech selected, its second sentence lit;
  - no speeches yet ("Nothing spoken yet.");
  - speaking;
  - the form refusing text over 4,000 characters;
  - the form refusing a name that exists.
- Wiring into [1d](cli-stand-in.md)'s stand-in:
  - `models list` answers with `models-stored-list.json`;
  - `models info Kokoro-82M/voice/5f3a2c91d0e4` answers with `models-stored-info-voice.json`;
  - the speech records are `speech-torch.json` and `speech-mlx.json`, read in place;
  - `voice say` replays a hand-made stream, `voice-say-done-handmade.jsonl`: the `job` line as `voice-refused-runtime.jsonl` records it, then one `segment` event in the shape of the CLI's machine-mode reference (`machine-mode.md:418`), then the `document` line holding the new record (`:450`).
- The `voice` preset: the route, the two records, `speech-torch.json` selected, the player paused on its second sentence, and the prototype's sample text in the form.
- Tests: the per-preset test and the view-model tests below.

**Reference.**
- The old prototype's guide: [`lib/design/spec/CLAUDE.md`](../../../../design/spec/CLAUDE.md) (the 30-tick check this item's preset test replaces).
- The law mirrored: [layer enforcement](../../../adrs/gui/layer-enforcement.md), [presentation MVVM](../../../adrs/darwin/presentation-mvvm.md), [module schemes and previews](../../../adrs/darwin/module-schemes-and-previews.md) (canned documents are "the binary's own").
- The record's shape: [machine-mode reference](../../../reference/cli/machine-mode.md), `voice.speech` at 1209–1239, the `segment` event at 418 and 448, the `document` event at 450.
- The Mac module: `lib/src/darwin/source/presentation/voice/`, and its tests in `lib/src/darwin/tests/presentation/voice/`.
- The shared player and the runtime-consent sheet: [`video-and-runtime-consent.md`](video-and-runtime-consent.md).

**Decisions made** (dated):
- 2026-11-14: The user's call: the prototype is rebuilt in React, TypeScript and Vite with the production GUIs' four layers and MVVM modules. The prototype, not the Mac app, is the source of truth.
- 2026-11-14: The user's call: the build is one self-contained `lib/design/main.html` that opens with a double-click, and it, not the canvas, is where the screen is reviewed. Parity screenshots are taken by Playwright in [1s](parity-proof.md). The work is the design queue's own, in v0.2.1.
- 2026-11-14: The recorded default: data is the Mac app's recorded fixtures, read in place. A state nobody recorded gets a hand-made file in the CLI's exact shape, named `*-handmade*`. The successful `voice say` stream is such a state.
- 2026-11-14: The recorded default: parity is judged on layout, states, controls and wording, not pixels or data.
- 2026-11-14: Item 1m, gated on 1k for the shared player.

**Open calls:**
- [default: the speak form stays inline, as the prototype draws it. The Mac's say sheet, model picker, folder scan and install flow are named as Mac-only and not ported. A new design for them is drawn in the React app after this item lands.]
- [default: the voice chips come from the recorded `voices` field, so the preset shows 3 voices in 2 languages rather than the prototype's 28. The group headings keep the prototype's form, the language and its count in capitals.]
- [default: the two records are used as recorded. Each lasts 0.1 s as recorded (the PyTorch one on a stub Kokoro, `versions.kokoro` "0.0-stub"), and the screen shows that honestly. A longer hand-made record is not made.]
- [default: the 96 waveform bars are kept as drawn. Their heights are decoration, as in the prototype. Which bars are lit follows the record's sentence timings and the player. The Mac app draws no bars.]
- [default: the header line names the store's voice model (from `models list`) and its engine in the CLI's words, as the Mac's `engineLine` names the engine, in place of the fixed "Kokoro-82M · cpu".]
- (consumed decision: 1d) where hand-made files live, and how the stand-in serves speech records.
- (consumed decision: 1k) the shared player.
- (consumed decision: 1s) the side-by-side for the `voice` preset.

**Guardrail(s).**
- **The `voice` preset test** (the successor of the 30-tick check): builds the screen at the preset, steps the clock 30 ticks, and fails on a throw or on any drawn string holding `undefined` or `NaN`.
- **View-model tests over the stand-in:**
  - the lit sentence follows the record's own offsets: a position of 0.06 s in `speech-torch.json` lights sentence 2;
  - text over 4,000 characters shows the error with its count and disables Speak;
  - a planted output name that already exists shows "never writes over a file" and disables Speak;
  - Speak shows the running row and line, then selects the new record and raises the toast;
  - "Use this voice for compositions" writes the preference that 1l's form reads;
  - a planted record missing `segments` is skipped and noted, never a throw.
- **The layering check** (1b) stays green. A planted `import React` in `voice/ViewModels/` fails it.

**Acceptance criteria:**
- [ ] The built `lib/design/main.html`, opened at the `voice` preset, draws:
  - the header, with the model line and "New speech";
  - the two recorded speeches;
  - the speak card: text and count, the sentence line, voice groups, speed and its reset, "Save as", the command, Speak;
  - the selected speech: waveform, player, the sentence table with STARTS and LASTS, the record's facts, its command, the three actions.
- [ ] Speak runs the hand-made stream end to end. The ⌘7 badge shows 1 while it speaks, and the new speech ends up selected.
- [ ] Every voice view-model test and the `voice` preset test pass in the local gate. The layering check is green.
- [ ] 1s's side-by-side for `voice` lists each difference as intended (real data, named Mac differences) or as a defect, and has no defects left.

**Scope note.** Earmarked for v0.2.1 (design track 1); gated on 1k (the shared player). Out of scope:
- the Mac-only parts named above: the say sheet, the model picker, the folder scan, the runtime install;
- any change to the old prototype, which is frozen;
- recording new CLI output, which the user runs.
