# The session screen: a turn, the composer and the session's states (split first)

**What / why.** The session screen is the prototype's largest place, and the first one a person sees. Measured 2026-11-14 (the recon's snapshot of `lib/design/spec/html/main.html`, 7,604 lines):
- **Its size.** The `chat` section of `renderVals()` is 4583–4834 (252 lines; it builds `tg` 4617, `rt` 4655, `wal` 4680, `cr` 4684, `pr` 4710 and `chat` 4783). The `composer` section is 4835–4912 (78 lines, `cmp` at 4869). The `ui.isChat` markup is 174–858 (685 lines), and its `chat.notEmpty` block 272–691 (420 lines) holds every transcript. About 1,015 lines in all.
- **Its 13 start presets** (`data-props`, checked against the file): `chat`, `chat:retry`, `chat:wal`, `chat:crash`, `chat:pr`, `chat:new`, `chat:private`, `chat:play`, `chat:suite`, `chat:restarting`, `chat:notsent`, `chat:ended`, `chat:compacting`. Twelve have a Screen board; `chat:compacting` has none.
- **Five transcripts are hand-written markup**, one per scenario in `this.state.cs`:
  - `isTaskgroup` 275–348: a reasoning pass streaming over a TaskGroup question (board ScreenSessionStreaming);
  - `isRetry` 350–443: a permission card with the change folded (ScreenSessionPermission);
  - `isWal` 445–496: an answer over an attached 300-page PDF, citing pages (ScreenSessionSources);
  - `isCrash` 498–529: a model that did not load for lack of memory, with "Unload gpt-oss-20b and retry" and "Ask Claude Sonnet instead · billed" (ScreenSessionError);
  - `isPr` 531–578: two tool calls, then a question card (ScreenSessionQuestion).
- **Its motion is invented.** "Streaming" is `tick()` adding 1 to `cs.taskgroup.progress` every 400 ms, up to `tgEnd()` (40, or 20 at low effort) plus 10, revealing a hardcoded `REASON` string. A sent message becomes a canned `reply()` after 7 ticks. The session's child lifecycle (`lifeBy`: restarting 5 ticks, starting 4, gone), held lines (`heldBy`) and compaction (30 ticks) are counters too. The data has 0 `object:` keys (critic V3), and no protocol event name (`answer_delta`, `thinking_delta`) appears in the file.
- **The Mac app's module** is `session`: 15 files, 6,164 lines. `SessionViewModel` (1,247), `Transcript` (766), `SessionActions`, `Play`; views `SessionWindow`, `TranscriptRow`, `InputBar`, `PermissionCardView`, `QuestionCardView`, `SuiteSheet`, `SessionExtras`; component `Card`. It draws 101 `#Preview` lines, and its tests hold 122 `@Test`s. Its `Transcript` applies protocol events (`.memory` at Transcript.swift:417, `.sources` at :425).
- **The recordings on hand.** `lib/src/darwin/tests/data/protocol/Fixtures/` holds 44 `.jsonl` streams. Counted by event type: `answer_delta` 776, `thinking` 53, `session` 44, `result` 42, `answer_start` 40, `answer_end` 39, `notice` 22, `thinking_delta` 14, `tool_status` 11, `question` 7, `error` 1. `mock-actor.jsonl` (24 lines) carries a question and a permission (a `question` with `kind: permission`).
- **What no recording has.** No `sources` event exists in any file; it is a Swift literal only (`tests/data/protocol/SessionGrowthTests.swift:11`). `memory` appears only in the tasks module's `task-run.jsonl`. The one `error` is a refused play (`execute-play-refused.jsonl`), not a model that failed to load.
- **Line numbers move.** `main.html` is 7,300 lines at HEAD; another session's uncommitted design work has it at 7,780 today. That moves the two script sections 140 lines later (`chat` 4723–4974, `composer` 4975–5052). The markup lines above are unchanged. 1c's baseline fixes the numbers the port reads.

So the port replaces five hand-written transcripts and a string counter with recorded streams replayed through the CLI stand-in. That is more than one session can build to the bar, so the item is split first.

**Split first.** This document is the session screen's whole design, built as two documents, each gated as shown. Final ids are assigned at the split, and nothing already queued is renumbered.

1. **The turn and the composer, with the plain states** (`session-turn-and-composer.md`, gated on 1e). This part includes:
   - the header with the model menu (markup 192–212) and the thinking menu (217–236); the empty state (251–270); the live lines (641–659: notice, user, model) and the pending line (682–688); the play transcript (580–601);
   - the composer's markup (693–759): the slash, @-file and attach menus (695–726), the chips (728–743), Send and Stop (754–755);
   - the `composer` section and the `chat` object's own fields (4783 on);
   - one turn replayed end to end: `session`, `thinking`/`thinking_delta`, `tool_status`, `answer_start`/`answer_delta`/`answer_end`, `result`, `notice`;
   - the answer's markdown and code colors, as a `styles` component (the Mac app's `MarkdownText` and `CodeLexer` sit in its `styles`);
   - presets `chat:new`, `chat:private`, `chat:play`, `chat:suite` (boards ScreenSessionNew, Private, Play, Suite);
   - streams on hand to map per preset: `new-unsent.jsonl`, `private-unsent.jsonl`, `execute-play.jsonl`, `chat-under-suite.jsonl`, `set-suite-refused.jsonl`, `markdown-answer.jsonl`, `code-answer.jsonl`, `stopped-turn.jsonl`;
   - Mac names: `SessionViewModel`, `Transcript` (`TranscriptItem`, `Activity`, `Answer`), `SessionActions` (`SlashCommand`, `PrivateMode`, `SuiteChoice`, `ContextMeter`), `Play` (`PlayNarration`); `SessionWindow`, `TranscriptRow`, `InputBar`, `SuiteSheet`, `SessionExtras`.
2. **The scenarios and the session states** (`session-scenarios-and-states.md`, gated on (1)). This part includes:
   - the five scenarios' markup (275–348, 350–443, 445–496, 498–529, 531–578) and their `renderVals` objects (`tg`, `rt`, `wal`, `cr`, `pr`, 4617–4782);
   - the session states: the kv chat (603–632), Not sent (660–668), Session ended (669–679), the composer's held strip (744), and the "GUI 6a" life block in the `chat` section;
   - presets `chat`, `chat:retry`, `chat:wal`, `chat:crash`, `chat:pr`, `chat:restarting`, `chat:notsent`, `chat:ended`, `chat:compacting` (boards Streaming, Permission, Sources, Error, Question, Restarting, NotSent, Ended);
   - `chat` becomes the file's default start again, ending 1e's interim monitor default;
   - streams on hand: `mock-actor.jsonl`, `permission-cancelled.jsonl`, `question-cancelled.jsonl`, `question-then-set.jsonl`, the `launch-refused-*` `.err`/`.exit` pairs, `chats-compact*.txt` (business/reads);
   - streams to record or hand-make (critic G6): a long reasoning pass with tool calls (Streaming), a `sources` answer over an attachment (Sources), a model that fails to load (Error);
   - Mac names: `PermissionCardView`, `QuestionCardView`; `Transcript`'s `QuestionCard`, `PermissionCard`, `Ending`, `Recall`; `SessionActions`' `HeldLine`.

**Core constraint(s).**
- **The prototype is the source of truth for behavior.** The port keeps every state, control and word the old screen draws. It takes the Mac module's shape and names, and names each place where the Mac app differs.
- **Every line of the transcript comes from a replayed stream.** Never a counter over a string. A stream is the binary's own recording, or a hand-made file in the CLI's exact shape named `*-handmade*` ([ADR 0004](../../../adrs/darwin/module-schemes-and-previews.md):46, "never shapes made up").
- **The four layers and MVVM** ([layer enforcement](../../../adrs/gui/layer-enforcement.md), [presentation MVVM](../../../adrs/darwin/presentation-mvvm.md)). View-models are plain TypeScript classes that never import React, and get the chat starter and the clock by injection from the stand-in (1d).
- **The design's rules** ([`lib/design/spec/CLAUDE.md`](../../../../design/spec/CLAUDE.md#the-designs-rules)): every color a token, keyboard first, the CLI's words, billed use marked.
- **Recording never touches a real home.** A capture runs the binary in a throwaway `APOGEE_HOME` on the mock backend: no model, no network, never `~/.apogee` or `~/.apogee-dev`.

**Seam + files.**
- `presentation/session/` under the prototype's source root (1b): `Views/`, `ViewModels/`, `Components/`, and `Previews/` with one named preview per visible state (the Mac module has 101 `#Preview` lines).
- The stand-in's chat starter and clock (1d): the screen asks it for a session and gets a replayed stream, paced by the clock and frozen by `still`. The 13 presets join 1d's preset table, each naming the files behind it.
- Streams: read in place from `lib/src/darwin/tests/data/protocol/Fixtures/` (and `business/reads/Fixtures/` for the compact reads). New recordings and hand-made files go to the design's own `lib/design/prototype/fixtures/` (1d).
- A capture script beside the prototype that follows `lib/src/darwin/scripts/capture-fixture.sh`'s recipe: a throwaway home, the `mock` backend, a scripted actor file per scenario, the driver's answers.
- Tests: a view-model test per preset, and the event-coverage test (Guardrails).

**Reference.**
- `capture-fixture.sh` (61 lines): writes only `mock-actor.jsonl`, from one fixed actor (an `ask_user` question, a `write_file` behind the permission gate, a streamed answer). Its actor file sets `reasoning`, `text` and `tool_calls` per turn.
- The Mac app's GUI 6a and 6b ([darwin Milestone G](../../../assistant/darwin/MILESTONES.md#milestone-g--the-second-sweeps-gui-findings)): Not sent, Session ended, held lines, and a saved chat's Compact… as a job. They are the behavior behind `chat:restarting`, `chat:notsent`, `chat:ended` and `chat:compacting`.
- The old per-preset check ([`lib/design/spec/CLAUDE.md` → Checking a change](../../../../design/spec/CLAUDE.md#checking-a-change)): every start preset, 30 ticks, flagging throws, `undefined` and `NaN`.

**Decisions made** (dated):
- 2026-11-14: The user's call: the prototype is rebuilt in React, TypeScript and Vite with the production GUIs' four layers and MVVM modules, built into one self-contained `lib/design/main.html`, in v0.2.1.
- 2026-11-14: The recorded default: data is the CLI's recorded files read in place; a state nobody recorded gets a hand-made file in the CLI's exact shape, named `*-handmade*`.
- 2026-11-14: The recorded default: parity with the frozen old prototype is judged on layout, states, controls and wording, not pixels or data.
- 2026-11-14: The recorded default: the old prototype is frozen (fixes only) from 1c's baseline; new design waits for 1e.
- 2026-11-14: **Split first** into the two documents above. Thirteen presets, five scenarios, a replay engine and new recordings are more than one session builds to the bar.

**Open calls:**
- [default: record each missing stream with `capture-fixture.sh`'s recipe against the mock backend in a throwaway `APOGEE_HOME`, from the branch build. Hand-make only what the binary cannot produce, and name it `*-handmade*`. Likely hand-made: `sources` needs a retrieval over an attachment, and a failed model load needs a real backend. Unverified until the capture is tried (critic G6).]
- [default: the capture script lives beside the prototype and the Mac app's script stays as it is. That script writes only `mock-actor.jsonl`, which the Mac app's `EventDecoderTests` hold to, and any edit under `lib/src/darwin/` triggers the `gui-darwin` pipeline (`changed.sh:50-62`).]
- [default: each scenario's actor file carries the old scenario's words (the TaskGroup reasoning, the PR 482 review and its question), so the stream is the binary's and the wording still compares.]
- [default: a preset freezes its replay at a named event. `chat` stops mid-reasoning, as `progress: 30` does today; `chat:retry` and `chat:pr` stop at their open card.]
- [default: the markdown and code view is the prototype's own `styles` component, with no new package. It draws paragraphs, lists, inline code, and fenced code with its caption, Copy and the five code tokens. A language the recordings and the old prototype never show is drawn uncolored.]
- (consumed decision: 1d) the fixtures folder, the loader, the clock and the chat starter's interface.
- (consumed decision: 1g) the session list draws `chat:compacting`'s busy rows and the open row menu. This item delivers the preset's fixtures and the compact jobs' state. The inspector (markup 762–856) is 1g's too.

**Guardrail(s).**
- **A test per preset**, the successor of the 30-tick check. For each of the 13 presets it builds the view-models from the preset's fixtures, runs 30 ticks, and walks the view state. It fails on a throw, `undefined` or `NaN`, naming the preset. Proven on a planted preset whose fixture path is missing.
- **Event coverage.** Every `type` in the streams the screen replays maps to a transcript item or a named skip, as the Mac app's `tool_call` is skipped. A planted unknown type is kept as an unknown line, never guessed (as `SessionGrowthTests.swift:20` holds for a `sources` event with no list).
- **No counter remains.** A test fails if a view-model's transcript text is not traceable to a replayed event.
- 1b's layering check: a planted `import React` in a session view-model fails by name.

**Acceptance criteria:**
- [ ] The two sub-documents exist, each with its markup ranges, presets, streams and gates. Their index rows replace this one, and this document is deleted at the split.
- [ ] Each of the 13 presets opens in the built `lib/design/main.html` at its frame, and its test passes. Opening the file with no `start` shows `chat`.
- [ ] Every view the module exports has a named preview per visible state in `Previews/`, and 1b's previews rule passes.
- [ ] The transcript grows only from replayed events. Each stream is listed in its sub-document as recorded (with the capture command) or hand-made.
- [ ] Side by side with the frozen old prototype (1c), each preset matches on layout, states, controls and wording. Every difference is listed as intended (the CLI's data) or as a defect.
- [ ] The local checks are green: typecheck, unit tests, layering check, staleness.

**Scope note.** Earmarked for v0.2.1 (design track 1); gated on [1e](styles-shell-and-the-first-screen.md). **Split first**: (1) is gated on 1e, and (2) on (1). [1g](sessions-inspector-execute-and-console.md) is gated on this item. Out of scope:
- the session list, the inspector, the execute door and the console (1g);
- the gallery ([1q](gallery-and-foundations.md)) and the full parity proof ([1s](parity-proof.md));
- any change to the Mac app, its fixtures or its capture script;
- any new design state: the old prototype is frozen, and new design waits for 1e.
