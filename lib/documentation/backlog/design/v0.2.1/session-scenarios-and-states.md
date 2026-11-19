# The session's scenarios and states

**What / why.** The second half of the session screen in the React prototype (design 1f, split on 2026-11-19; [1u](session-turn-and-composer.md) is the first half). It ports the old screen's five hand-written scenarios and the session's states into 1u's `session` module. Each transcript is replayed from a stream that carries the old scenario's words, either captured from the binary or hand-made in its exact shape. This part also makes `chat` the file's default start again. Measured 2026-11-19 at the frozen revision `b375fac9` (`lib/design/prototype/parity/baseline.json`), read with `git show b375fac9:lib/design/spec/html/main.html` (8,082 lines, the same file at HEAD). The parent's line numbers came from 2026-11-14 working trees; each range below was found again by its marker:
- **The five scenarios**, one per `this.state.cs` entry, chosen by `chatId` (`chat.isTaskgroup` … `isPr`, 5149–5153). Their objects are in the `chat` section at 4944–5070:
  - **Streaming**, `isTaskgroup` 275–348, `tg` 4944–4974 (the object at 4954). It shows "earlier: 4 exchanges", a first exchange with `~/code/ingest/` attached and a python answer, the follow-up, "2 earlier exchanges not sent — context budget" and "recalled 1 past session · asyncio timeouts in the scraper". The reasoning pass has an `embedding` helper line, `fetch_url https://docs.python.org/3/library/asyncio-task.html`, `read_file src/ingest/runner.py lines 40–80` and the reasoning itself, followed by three answer paragraphs with a code block. Board ScreenSessionStreaming.
  - **Permission**, `isRetry` 350–443, `rt` 4976–5013: two `read_file` lines, then `write_file` behind the gate. Its card has the change folded, Allow once, Allow for this session, Always allow and Deny; it then ends allowed, denied ("you denied write_file — gpt-oss-20b was told and carried on without it") or unanswered. Board ScreenSessionPermission.
  - **Sources**, `isWal` 445–496, `wal` 5015–5017: "switched to Claude Sonnet — billed per use from here", a 300-page PDF attached, "thought for 6s · embedding · searched 1 document", and an answer citing pages 187 and 192 and `postgresql.conf` lines 212–214, with a SOURCES list. `chat:wal` opens citation 1 (`applyStart` 2733). Board ScreenSessionSources.
  - **Error**, `isCrash` 498–529, `cr` 5019–5041: an image attached, then "Qwen3-VL-8B didn't load" ("It needs 7.9 GB of memory … gpt-oss-20b is holding 12.7 GB"), with "Unload gpt-oss-20b and retry" (loading for 9 ticks, `tick()` 2872–2876), "Ask Claude Sonnet instead · billed" and its details. Board ScreenSessionError.
  - **Question**, `isPr` 531–578, `pr` 5043–5070: `run_command git diff main...pr-482 --stat in ~/code/ingest`, `read_file`, then "Claude is asking" with Per upload, Per batch, Per host and an answer of your own, Answer and Skip. Each answer has its own reply, and the footer says "tokens not reported by the Claude app". Board ScreenSessionQuestion.
- **The session states**, the Mac app's GUI 6a and 6b:
  - the kv chat (603–632), a resumed conversation with a heading-and-table answer, "earlier in this conversation" and "resume: a message could not be read and was skipped";
  - Not sent (660–668) with Send again, and Session ended (669–679) with its details and Start again;
  - the composer's held strip (744) and its words and the life words (`heldWhen`, `lifeSays`, 5194–5205);
  - the GUI 6a block in the `chat` section (5088–5118);
  - the methods: `turnRunning()` 4202, `lifeOf()` 4213, `holdsLines()` 4222, `lifeStart()` 4238–4256, `resend()` 4260, `sendLine()` 4269 and `flushHeld()` 4283;
  - the tick's counters: the child's life (2987–3001: restarting for 5 ticks, then starting for 4) and a saved chat's compaction (3040–3060, done after 30 ticks).
  - The `isStub` block (634–639) is the old note for a saved chat the prototype never wrote. It is not ported: a saved chat is a resumed recording here.
- **Its nine presets** (`data-props` 2512; `applyStart` 2722–2733):
  - `chat` (Streaming at `progress: 30`, mid-reasoning), `chat:retry`, `chat:wal`, `chat:crash` and `chat:pr`;
  - `chat:restarting`, `chat:notsent` and `chat:ended`, each set up by `lifeStart()` on the kv chat, which turns the inspector off (4241);
  - `chat:compacting`: two saved chats compacting as jobs (retry 12 ticks in, kv just begun) with kv's row menu open on Stop compacting. Its open session is Streaming at its starting `progress: 15` (2553), so its main pane is the Streaming transcript at an earlier frame, and the compaction shows only in the list (1g's).
  - Eight have a Screen board: Streaming, Permission, Sources, Error, Question, Restarting, NotSent and Ended. `chat:compacting` has none. The six that keep the inspector draw it open, and all nine draw the session list; both are 1g's.
- **Presets that stand on Streaming.** Four kinds of preset open over the Streaming session, at two frames:
  - 1e's `monitor` (`applyStart` 2734–2736), at `chat`'s frame (`progress: 30`). This part gives `monitor` the same chat rule and freeze, and compares its main pane;
  - `console` (2737), 1g's, which sets no chat or progress and so stands at the starting `progress: 15` (2543, 2553);
  - every `settings:*` preset (2834–2843), 1i's, at `progress: 15` the same way;
  - `chat:compacting` (above), at `progress: 15`.
  - This part exposes the Streaming setup at both frames, so `console` and the settings presets reuse it. 1g is gated on this part. 1i is not: whichever of 1i and this part lands second wires the settings presets to it.
- **Its motion is invented.** `tick()` adds 1 to `cs.taskgroup.progress` every 400 ms, up to `tgEnd()` (40, or 20 at low effort; 2857) plus 10, revealing the hardcoded `REASON` string (4947) slice by slice. The child's life, the held lines and the compaction are counters too (`lifeBy`, `heldBy`, `compacting`).
- **The default start** is `chat` in `data-props` (2512). 1e's recorded default made `monitor` the start until this part (`app/Previews/Presets.ts`'s `defaultStart`; [DEVELOPER.md](../../../assistant/design/DEVELOPER.md#styles-the-shell-and-the-first-screen), "Opening a preset").
- **The streams on hand**, in `lib/src/darwin/tests/data/protocol/Fixtures/`:
  - `mock-actor.jsonl` (24 lines: a question, and a permission as a `question` with `kind: permission`), `permission-cancelled.jsonl` (14), `question-cancelled.jsonl` (4) and `question-then-set.jsonl` (11);
  - `resumed-chat.jsonl` (16) and its `resumed-*` siblings, and `set-between-turns.jsonl`;
  - the `launch-refused-*` `.err` and `.exit` pairs (exit 1).
  - In `business/reads/Fixtures/`: `chats-compact.txt` ("compacted 20261026-142409-5159: 4 messages into 2, by compactor"), `chats-compact-again.txt` and `chats-compact-missing.err`, and the `chats-show-*.json` documents, which hold a resumed chat's earlier turns as the Mac reads them (`SessionViewModel.swift:493`).
- **What no recording has.**
  - No `sources` event exists in any file; it is a Swift literal only (`tests/data/protocol/SessionGrowthTests.swift:11`).
  - `memory` appears as an event only in the tasks module's `task-run.jsonl`.
  - The one `error` among the chat streams is a refused play (`execute-play-refused.jsonl`), not a model that failed to load. The Mac app draws an `error` as an error row (`Transcript.swift:500`) and has no failed-load card.
  - No stream has a child that dies mid-answer.
- **What the mock can and cannot make** (`lib/src/cli/source/data/backends/mock.cpp:132–215`). It reads, per turn, only `text`, `reasoning`, `delay_ms`, `until_cancelled` and `tool_calls`, plus `metered` and `base_model` for the whole file. Its tool calls run through the real harness, so `read_file`, `write_file` behind the gate and `ask_user` are the binary's own. It cannot reach the network, load or fail to load a real model, or run git in the person's `~/code/ingest`. It can name its backend `claude`, as `new-unsent.jsonl`'s `session` line does, which is all Question's Claude lines need (open call below).
- **What the stand-in lacks** (1d, `source/business/session/MachineSession.ts`).
  - A still frame stops only after its start ticks' lines, or at a `question` the preset does not answer; it has no notion of a named event.
  - `close()`, `stop()` and `terminateNow()` end a child at once, so no frame can stand between a stop and the child's exit (Restarting).
  - A child whose rule names a non-zero exit ends failed once its lines are out (`step()`), so a stream cut mid-answer already dies. What is missing is coming back: a second launch, the held lines sent to it, and the frames in between.
  - An answer changes nothing in the replay. `answer()` only counts it (`MachineSession.ts:115–119`), and a chat rule picks its recording at launch, so one recording carries one branch. Permission's allow and deny, each Question answer and Skip, and Error's two fixes each need their own continuation. The mock cannot branch on them either: its turn list is fixed (`mock.cpp:132–181`).
  - A job (`source/business/jobs/SupervisedJob.ts`) plays one line a tick and ends on the tick after its last line, so a one-line `chats compact` recording ends at its second tick. No frame can stand mid-job.

So this part replaces five hand-written transcripts, a string counter and three life counters with captured and hand-made streams, replayed on an extended stand-in.

**Core constraint(s).**
- **The prototype is the source of truth for behavior.** The port keeps every state, control and word the old screen draws. It takes the Mac module's shape and names, and names each place where the Mac app differs (the Error scenario's card, for one).
- **Every line of the transcript comes from a replayed stream**: a recorded event, a recorded `chats show` document, or the preset's own sent text, never a counter over a string. A stream is the binary's own recording or a hand-made file in the CLI's exact shape named `*-handmade*` ([ADR 0004](../../../adrs/darwin/module-schemes-and-previews.md):46, "never shapes made up").
- **The old words carry over** through each scenario's actor file, so the stream is the binary's and the wording still compares.
- **The four layers and MVVM** ([layer enforcement](../../../adrs/gui/layer-enforcement.md), [presentation MVVM](../../../adrs/darwin/presentation-mvvm.md), [the design's ADR 0001](../../../adrs/design/react-design-prototype.md)). View-models are plain TypeScript and never import React. A change to the stand-in stays in its layer, with no recording imported there (layering rule 4).
- **The design's rules** ([`lib/design/spec/CLAUDE.md`](../../../../design/spec/CLAUDE.md#the-designs-rules)): every color a token, keyboard first, the CLI's words, and billed use marked (the switch to Claude Sonnet, "Ask Claude Sonnet instead · billed").
- **Recording never touches a real home.** Captures use 1u's script with the branch build, `lib/src/cli/build/macos-arm64/source/apogee`, in a throwaway `APOGEE_HOME` and working folder, on the mock backend. They never use `~/.local/bin/apogee`, `~/.apogee` or `~/.apogee-dev`.
- **The old prototype stays frozen**, and the Mac app's files are not changed.

**Seam + files** (paths under `lib/design/prototype/`):
- **`source/presentation/session/`** (1u's module):
  - `Transcript` gains `QuestionCard`, `PermissionCard`, `Ending`, `Recall` and the sources rows (the Mac's names);
  - `SessionActions` gains `HeldLine`;
  - `SessionViewModel` gains the child's life (restarting, starting, ended), held lines sent in order (a suite move last), Not sent with Send again, Start again, the question and permission answers, the citations, and the Error card's two fixes;
  - views `PermissionCardView` and `QuestionCardView` (with `DiffView` for the folded change); `TranscriptRow` gains `EndedCard` and `SourcesRow`, and `SessionExtras` gains `HeldLines` (the Mac's files); the component `Components/Card` (with `CardLabel`); and previews per state;
  - `Previews/SessionPresets.ts` gains the nine presets.
- **The compact jobs' state** (open call below): the Mac's `compactions`, `isCompacting(_:)`, `stopCompacting(_:)` and `canCompact`, on the stand-in's `JobStarting`. `modules.json` gives `presentation/session` the edge `business/jobs` for it, and gives `presentation/app` the same edge, since `AppViewModel` builds the stand-in's `JobStarter(child, clock)` and hands it in (a view-model takes `JobStarting`, never the stand-in).
- **The stand-in (1d), extended where these presets need it**, each chat change with its tests in `tests/business/session/MachineSession.test.ts`:
  - a freeze at a named event, in a still frame;
  - an exit replayed on the clock after the input closes;
  - a restart, which is the chat rule's next answer (1d's answer list, "one a call, the last repeating");
  - a continuation picked by what is written. A turn's recording may name one continuation per answer or line written (`yes`, `session`, `always` or `no` for a permission; a question's option label, its free text, or Skip; Error's `set`, or its retry). The turn plays the matching one from that point on. A line no continuation names is noted in the child's `unanswered`, as an unrecorded turn is today (`MachineSession.ts:110`).
  - a job's end held back by its rule's delay, in `source/business/jobs/SupervisedJob.ts`, with its test in `tests/business/jobs/SupervisedJob.test.ts`, so a still frame can stand mid-job (`chat:compacting`). It reads the same `Answer` exit delay a chat's rule uses.
  - The chat changes go in `source/business/session/MachineSession.ts`, and in `source/infrastructure/process/StandInChild.ts`'s `Answer` where a rule must say when its child exits or name its continuations.
- **`source/presentation/app/`**:
  - `Presets.ts`: the nine presets, and `defaultStart` back to `chat`, with its comment ("until 1f ports Sessions");
  - `Presets.ts` also gives `monitor` `chat`'s chat rule and freeze, composed there around `monitorPreset`, since `monitor` may not import `session`;
  - `AppViewModel.applyStart`: each preset's setup, `monitor`'s Streaming session among them, with the Streaming setup exposed at both frames (the old `progress: 30` and `15`, each a named event) for `console` (1g) and the settings presets (1i) to reuse;
  - the tests that expect the monitor as the default start now expect `chat`: `tests/presentation/app/AppViewModel.test.ts:49–52` (its title, "until 1f ports Sessions", rewritten for the `chat` default) and the unknown-preset check at `:112`, and the double-click test in `e2e/opens-from-disk.spec.ts:35–43`;
  - DEVELOPER.md's "Opening a preset" line: the default is `chat`.
- **Streams.** Captures and hand-made files go under `fixtures/data/protocol/` (and `fixtures/business/reads/` for a `chats show` document). The actor files sit beside 1u's script in `scripts/actors/`.
- **Tests**:
  - 1e's `Presets.test.ts` takes the nine;
  - the event coverage test gains `question`, `sources`, `memory` and `error`;
  - a view-model test per scenario and per state.
- **Docs, at ship:** the design's MILESTONES, DEVELOPER.md (the stand-in's new replay rules, the default start) and the Presentation layer card.

**Reference.**
- The Mac app's GUI 6a and 6b ([darwin Milestone G](../../../assistant/darwin/MILESTONES.md#milestone-g--the-second-sweeps-gui-findings)) cover Not sent, Session ended, held lines, and a saved chat's Compact… as a job. They are the behavior behind `chat:restarting`, `chat:notsent`, `chat:ended` and `chat:compacting`. Their tests are `lib/src/darwin/tests/presentation/session/SessionStatesTests.swift` and `ResumedSessionTests.swift`.
- [`reference/cli/machine-mode.md`](../../../reference/cli/machine-mode.md): the `memory`, `sources`, `tool_status` and `error` rows of the event table, answering a question, and answering a permission prompt.
- `lib/src/darwin/scripts/capture-fixture.sh`, 1u's capture script, and the mock backend's actor file (above).
- [The old per-preset check](../../../../design/spec/CLAUDE.md#checking-a-change): every start preset, 30 ticks, flagging throws, `undefined` and `NaN`.
- Siblings:
  - [`session-turn-and-composer.md`](session-turn-and-composer.md) (1u, the gate);
  - [DEVELOPER.md → the CLI stand-in](../../../assistant/design/DEVELOPER.md#the-cli-stand-in) (1d);
  - [`sessions-inspector-execute-and-console.md`](sessions-inspector-execute-and-console.md) (1g, gated on this item) and [`parity-proof.md`](parity-proof.md) (1s).

**Decisions made** (dated):
- 2026-11-14: The user's call: the prototype is rebuilt in React, TypeScript and Vite with the production GUIs' four layers and MVVM modules, built into one self-contained `lib/design/main.html`, in v0.2.1.
- 2026-11-14: The recorded default: data is the CLI's recorded files read in place. A state nobody recorded gets a hand-made file in the CLI's exact shape, named `*-handmade*`.
- 2026-11-14: The recorded default: parity with the frozen old prototype is judged on layout, states, controls and wording, not pixels or data.
- 2026-11-14: The recorded default: the old prototype is frozen (fixes only) from 1c's baseline. New design is drawn in the React app now that 1e has shipped.
- 2026-11-14: The parent (1f) was marked **split first** into 1u and this part. Thirteen presets, five scenarios, a replay engine and new recordings are more than one session builds to the bar.
- 2026-11-19: **The split** (the user's answer before the group run, 2026-11-18: "Item 1f is built as the two documents its own text records (split first); 1g's gate moves to the second"). This part is the parent's second: the scenarios and the session states. It is gated on 1u, and 1g is gated on it.
- 2026-11-19: **The ids** (the run owner's ruling, for veto). Design ids are ever-assigned: an item joining a track takes the track's next letter, and a vacated id stays spent, as the CLI's M5 and M6 did. 1f stays spent, 1u is the first part, and this part is **1v**. Both sit where 1f sat in the build order, ahead of 1g.
- 2026-11-19: **The comparison is scoped to the main pane** (the run owner's ruling, for veto). The old frame draws the session list in all nine of this part's presets and the inspector in six of them (all but `chat:restarting`, `chat:notsent` and `chat:ended`), and both are 1g's. So this part's side-by-side covers the header, the transcript and the composer. The list and the inspector are compared at 1g, and the whole window at 1s. `chat:compacting`'s compaction shows only in the list, so it is compared at 1g. Its main pane is compared here as the Streaming transcript at its earlier frame. `monitor`'s main pane (Streaming at `chat`'s frame, behind the monitor) is compared here too, beside `chat`'s; `console`'s is compared at 1g and the settings presets' at 1s.
- 2026-11-19: **Streams the mock cannot make are hand-made**, named `*-handmade*` in the CLI's exact shape under `fixtures/data/protocol/` (the run owner's ruling, for veto). They are:
  - Streaming's `fetch_url` on docs.python.org (it needs the network), its `embedding` helper line, and its recall of a named past session;
  - Sources' retrieval over an attachment and its `sources` event;
  - Error's failed model load;
  - Question's `run_command git diff` in `~/code/ingest` (a seeded repository);
  - Ended's child dying mid-answer (exit 2, "fatal: the model went away").
  - A stream that needs even one hand-made line is hand-made as a whole. It is built from the capture's own lines, with the missing ones added in the CLI's exact shape, and its record lists which lines were added. Expect more hand-made streams than the parent predicted.
- 2026-11-19: **The freeze at a named event, the replayed exit and restart, the continuation picked by an answer, and a job's held-back end land here** (the run owner's ruling: each lands with the first preset that needs it). `chat`, `monitor` and `chat:compacting` stand mid-reasoning. `chat:restarting` stands between a stop and the next child. `chat:ended`'s Start again and `chat:notsent`'s returned session need a second child. `chat:retry`, `chat:crash` and `chat:pr` play a different reply for each answer or fix. `chat:compacting`'s still frame has both jobs running. None of 1u's presets needs any of these changes.
- 2026-11-19: **The compact jobs' state is this part's; its busy rows and open row menu are 1g's** (the parent's consumed decision, kept).
- 2026-11-19: **`chat` is the default start again**, ending 1e's interim monitor default, as the parent recorded.

**Open calls:**
- [default: record each missing stream with `capture-fixture.sh`'s recipe (here 1u's capture script) against the mock backend in a throwaway `APOGEE_HOME`, from the branch build. Hand-make only what the binary cannot produce (the list above), and name it `*-handmade*`. Capture what the mock can: the Permission scenario (two `read_file`s and a `write_file` behind the gate, in a seeded folder holding `config.toml` and `src/ingest/runner.py`), the reasoning and `read_file` of Streaming, Question's `ask_user`, and the kv chat's earlier turns as a `chats show` document. The capture's own paths are the CLI's data. Unverified until the capture is tried (critic G6).]
- [default: each scenario's actor file carries the old scenario's words (the TaskGroup question and its reasoning, the retry change and its diff, the WAL answer and its citations, the stack trace answer, the PR 482 review and its question), so the stream is the binary's and the wording still compares.]
- [default: a preset freezes its replay at a named event in a still frame. On a live clock the replay starts at that event and plays on to the turn's `result`, as `tick()` takes `progress` from 30 to `tgEnd() + 10` (`main.html:2861–2870` at `b375fac9`). `chat` and `monitor` stop mid-reasoning, as `progress: 30` does today, and `chat:compacting`'s open session at its earlier frame, as `progress: 15` does. `chat:retry` and `chat:pr` stop at their open card, which 1d's replay already does by waiting at an unanswered `question`.]
- [default: each branch is its own capture, with its own actor file, from the shared opening turns, or hand-made. The preset names them all as its turn's continuations, so clicking any choice in the built file plays its own reply. Question's "change answer" (the old `reask`, main.html:5064, which re-opens the card after its reply) has no CLI behind it: the CLI reads one answer per question, and a sent answer cannot be taken back. So the port does not draw it once the answer is written, and lists it as an intended difference (the CLI's behavior). Until Answer is pressed, the choice changes freely, as the old card's options do.]
- [default: Question's Claude lines are not stream content. The stream needs only a `session` line whose `model` names the `claude` backend (as `new-unsent.jsonl`'s does) and a `result` with `usage` absent (as every mock `result` is). Both are CLI shapes the mock already writes when its backend is named `claude`. The card label "Claude is asking" and the footer's "tokens not reported by the Claude app" are the view's words, drawn from that model and the missing usage. The Mac app draws "Apogee is asking" (`Components/Card.swift:43`, `Views/QuestionCardView.swift:28`) and no tokens text when usage is absent (`TranscriptRow.swift:31`). The port keeps the old words, as the prototype is the source of truth, and lists both as differences from the Mac app.]
- [default: the replayed exit and restart. A chat rule's answer may say when its child exits after its input closes, counted in ticks (0, today's behavior, unless it says otherwise), so a still frame can stand at Restarting. A restart is the same rule's next answer, so a second launch replays the next child (`--resume`, and `--suite` after a suite move). The held lines go to that child once its `session` line plays, a suite move last. Not sent is a send refused because the child's input had closed (`SessionError`, as the Mac's `MachineSession.swift:248` throws it and `sendOrHold`, `SessionViewModel.swift:865`, meets it). Session ended is a stream cut mid-answer with a non-zero exit and its stderr tail, in the Mac's `EndedCard` words.]
- [default: the compact jobs' state is a small class in `session/ViewModels/` (the Mac's `compactions`, `isCompacting(_:)`, `stopCompacting(_:)` and `canCompact`), held by the composition until 1g's `SessionLibraryViewModel` takes it. `sessions` may import `session`, but not the reverse (`modules.json`). It runs `chats compact <id> --yes` on `JobStarting`, not streamed, as a job that plays its recorded line (`chats-compact.txt`), reads back, and says its last line or its failure (`chats-compact-missing.err`). `chat:compacting` starts both jobs in its setup, retry first. Each job's rule holds back its end with the same `Answer` exit delay this part adds for a chat, which `SupervisedJob` also reads, so `.ended` plays that many ticks after the last line (0 keeps today's next tick). The delay is long enough that both jobs are still running after the preset's start ticks. The old 30 ticks and the 12-tick offset are invented motion and are not ported.]
- [default: the Error scenario keeps the old card's words and both fixes, since the prototype is the source of truth and the Mac app has no such card (named, not followed). Each fix runs what the CLI has. The switch is a `set` of the model to a billed backend, as `set-model.jsonl` records one. Unload and retry has no command to run: the CLI has no `unload` (searched 2026-11-19; a session's model is held by its own child), so it replays a hand-made continuation, and the record names the gap. Each answer is recorded or hand-made, and where the CLI's words differ from the old card's, the CLI's win and the difference is listed.]
- [default: the kv answer's headings and table are drawn by 1u's markdown component, which already reads them from `markdown-answer.jsonl`; this part adds nothing to `styles` unless a scenario's answer shows a block 1u's does not draw.]
- (consumed decision: 1d) the fixtures folder, the loader, the clock and the chat starter's interface.
- (consumed decision: 1e) the confirm and sheet hosts, the toast and routing.
- (consumed decision: 1u) the session module and its names, the capture script and its actor folder, the markdown and code component, the per-preset and event-coverage tests, `inspectorOpen` and the inspector's placeholder column, the model facts' interim reads, the session window's empty state behind `monitor` (which this part replaces with Streaming), and the `endPrivate` confirm.
- (consumed decision: 1i) the `settings:*` presets, which reuse this part's Streaming setup at `progress: 15`; whichever of 1i and this part lands second wires them.
- (consumed decision: 1g) the session list draws `chat:compacting`'s busy rows and the open row menu. This item delivers the preset's fixtures and the compact jobs' state. The inspector (markup 762–856) is 1g's too, and so is `console`, which reuses this part's Streaming setup at `progress: 15`.

**Guardrail(s).**
- **A test per preset**, the successor of the 30-tick check. The nine presets join 1e's `Presets.test.ts`, still and after 30 ticks, at the three `apogee` modes, and `monitor` is walked with its new chat rule. It fails on a throw, an unanswered call, or `undefined`, `null` or `NaN`, naming the preset.
- **Event coverage.** Every `type` in the streams this part replays maps to a transcript item or a named skip. A planted unknown type is kept as an unknown line, never guessed (as `SessionGrowthTests.swift:20` holds for a `sources` event with no list).
- **No counter remains.** A test fails if a view-model's transcript text is not traceable to a replayed event, a recorded document or the preset's sent text. A planted `REASON`-style string fails it.
- **The stand-in's new replay**, each rule tested against a recording:
  - a still frame stands at its named event after its start ticks and after `advance(150)`; on a live clock the same preset passes it, and after 60 seconds of fake timers its turn has reached its `result`;
  - an exit delayed after the input closes keeps the frame at Restarting until it plays;
  - a second launch replays the rule's next answer;
  - each answer plays its own continuation, and an answer with no recorded continuation is noted in `unanswered`;
  - a job's end held back by its rule's delay plays that many ticks after its last line, and 0 keeps today's next tick;
  - the held lines reach the next child in order, a suite move last;
  - a send to a closed child is Not sent, and Send again is enabled only once the session takes a turn;
  - a stream cut mid-answer ends as Session ended with its exit and stderr tail.
- **Each scenario's view-model test**: allow once, for the session, always, and deny on Permission, each playing its own reply; each citation opening and closing on Sources; both fixes on Error, each playing its own reply; each answer and Skip on Question, each playing its own reply, and no "change answer" once an answer is written; Stop on Streaming.
- **The compact jobs**: two run side by side, one ends and says its line, and Stop says "Not compacted: stopped"; `chat:compacting`'s still frame has both jobs running.
- **Every stream's provenance**: each file a preset names exists, and is either a recording or named `*-handmade*`.
- **1b's layering check** stays green: a recording imported from `business/session` fails it.

**Acceptance criteria:**
- [ ] `chat`, `chat:retry`, `chat:wal`, `chat:crash`, `chat:pr`, `chat:restarting`, `chat:notsent`, `chat:ended` and `chat:compacting` each open in the built `lib/design/main.html` (`?start=<preset>&still=true`) at their frame, and their per-preset test passes at all three `apogee` modes.
- [ ] Opening the file with no `start`, or an unknown one, shows `chat`.
- [ ] The transcript grows only from replayed events. Each stream is listed in this part's record as recorded (with the capture command) or hand-made (with the lines it adds).
- [ ] `?start=monitor&still=true` shows the monitor over the Streaming session at `chat`'s frame, and `monitor`'s per-preset test passes with its chat rule.
- [ ] Side by side with the frozen old prototype (1c), each of the nine presets' main pane (the header, the transcript and the composer), and `monitor`'s behind its overlay, matches on layout, states, controls and wording. Every difference is listed as intended (the CLI's data) or as a defect. The session list and the inspector are left to 1g, and the whole window to 1s.
- [ ] Every view this part adds has a named preview per visible state, and 1b's previews rule passes.
- [ ] The local checks are green: `npm run check`.

**Scope note.** Earmarked for v0.2.1 (design track 1). Split from 1f on 2026-11-19, and gated on [1u](session-turn-and-composer.md). [1g](sessions-inspector-execute-and-console.md) is gated on this item, because the inspector reads the scenarios' state. Out of scope:
- the session module's turn, the composer, the header, the markdown component, the capture script and the `endPrivate` confirm (1u);
- the session list (with `chat:compacting`'s busy rows and row menu), the inspector, the execute door and the console (1g);
- the gallery ([1q](gallery-and-foundations.md)) and the full parity proof ([1s](parity-proof.md));
- any change to the Mac app, its fixtures or its capture script;
- any new design state: the old prototype is frozen, and new design is drawn in the React app after the port, not here.
