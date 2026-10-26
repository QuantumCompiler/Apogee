# Apogee GUI — Roadmap

How we work: features get discussed in chat, written up here (and in [SPEC.md](SPEC.md)), given their own document in [`backlog/`](../backlog/README.md) when they're specced, then an agent takes the backlog item and builds it straight from that document (there is no separate TODO file — the backlog is the queue). When work ships it's recorded in detail in [MILESTONES.md](MILESTONES.md); this file stays high-level.

**Version scheme:** repo-wide — the release `lib/release/VERSION` names, published when its branch merges into `stable` (the CLI docs record the law). The GUI's themes ride those releases once the first application lands.

---

## Shipped releases

*(Nothing shipped yet — the first tagged release carrying GUI work starts this board.)*

---

## In progress

### v0.2.0 — The GUI effort opens

The documentation tree was restructured per application (2026-10-20, the user's call): CLI docs under `documentation/cli/`, this system scaffolded under `documentation/gui/`, the [router](../../CLAUDE.md) between them.

- [x] The docs home — this system, scaffolded 2026-10-20; `adrs/` and `reference/` beside it the same day.
- [x] **ADR 0001** — the four-layer law (Infrastructure / Data / Business / Presentation, dependencies down) holds for every GUI application regardless of language and framework (the user's call, 2026-10-20).
- [x] **The first spike** — ran 2026-10-20: the machine-mode integration proven cheap (the shipped binary driven end to end by a ~40-line Swift probe), the walls measured (no build wiring, no layering teeth, binary discovery from a `.app`), the split landed as track 1. The user's calls, same day: **macOS first, Swift and SwiftUI, unsigned ad-hoc**.
- [x] **The darwin scaffold** *(1a, shipped 2026-10-20 — [MILESTONES → Milestone A](MILESTONES.md#milestone-a--the-macos-application))* — `Apogee.xcodeproj` (an Xcode project, the user's call at the build), the four layers as framework targets with the layering check at birth, the SwiftUI window, the ad-hoc-signed `Apogee.app`, `cicd.sh --app` and `changed.sh gui-darwin`.
- [x] **The machine-mode client core** *(1b, shipped 2026-10-20 — [MILESTONES → Milestone A](MILESTONES.md#milestone-a--the-macos-application))* — discovery across the install channels, the captured child, tolerant typed events over an async stream, answers and cancel down the pipe, the JSON reads.
- [x] **The session window** *(1c, shipped 2026-10-20 — [MILESTONES → Milestone A](MILESTONES.md#milestone-a--the-macos-application))* — one chat session rendered and steered: streaming answers, thinking, tool lines, the protocol's questions as native UI, stop/Esc as `cancel`; the mock-actor conversation as the standing fixture.
- [x] **The darwin pipeline** *(1d, shipped 2026-10-20 — [MILESTONES → Milestone A](MILESTONES.md#milestone-a--the-macos-application))* — the GUI's own workflow beside the CLI's (`.github/workflows/darwin.yml`): the gated build on the `xcode-27` runner through `cicd.sh`, the ad-hoc-signed, verified `Apogee.app` artifact — a per-run CI artifact (the user's call, answered 2026-10-20); its place in releases is its own later step.
- [x] **The design-completion spike** — ran 2026-10-20 over `lib/design/spec` (the complete front-end design): the prototype certified 29/29 by its own health check, the contract board verified against the installed binary (15 real CLI gaps plus 2 found by probe, all owned by the CLI queue's track 52), 26 of 29 screens unbuilt. The split landed as 1e–1q the same day.
- [x] **The app shell** *(1e, shipped 2026-10-20 — [MILESTONES → Milestone B](MILESTONES.md#milestone-b--the-designs-screens))* — the sidebar's seven places on ⌘1–⌘7, the route kept across launches, the overlay and toast frame, the 960×640 floor and the collapse below 900; the shipped session window re-homed at Sessions, an honest placeholder at every unbuilt place.
- [x] **Sessions complete** *(1f and 1g, shipped 2026-10-20 — [MILESTONES → Milestone B](MILESTONES.md#milestone-b--the-designs-screens))* — the library and inspector (the saved sessions by date, one live child at a time, rename and delete read back); the states and the input's reach (new, private, failed with Try again, recalled and sources, the permission diff, attach and `@`, the context meter, settings by the `set` line or by restart, slash acts), each on the CLI's 52a/52d where the build announces them and the board's fallback where not.
- [x] **Models complete** *(1h and 1i, shipped 2026-10-20 — [MILESTONES → Milestone B](MILESTONES.md#milestone-b--the-designs-screens))* — the library, details and roles (every kind under the CLI's groups, the typed facts where stated, changes asked and read back); downloads over the one supervised-jobs core (the 52b stream where the build has it, the busy-line fallback where not, Stop as Ctrl-C, the named resume command).
- [x] **Tasks, monitor & suites, graph** *(1j, 1k and 1l, shipped 2026-10-20 — [MILESTONES → Milestone B](MILESTONES.md#milestone-b--the-designs-screens))* — the task screens over the task events, grants up front; the 2 s monitor while open and the suites with an honest fit; the graph screen over its documents with a client-side deterministic layout.
- [x] **The media studio** *(1m, 1n and 1o, shipped 2026-10-20 — [MILESTONES → Milestone B](MILESTONES.md#milestone-b--the-designs-screens))* — clips from the Movies records, compose with scene progress from the project document, voice with the document's timings.
- [x] **Settings** *(1p, shipped 2026-10-20 — [MILESTONES → Milestone B](MILESTONES.md#milestone-b--the-designs-screens))* — five panes over the real verbs, keys via stdin retained nowhere, the three-theme switch in `styles`.
- [x] **Design board corrections** *(1q, shipped 2026-10-20 — [MILESTONES → Milestone B](MILESTONES.md#milestone-b--the-designs-screens))* — macOS 27+ stands; the probe-found gaps join the board's ledger; republished per its law.
- [x] **ADR 0004** *(adopted 2026-10-21 — [MILESTONES → Milestone A](MILESTONES.md#milestone-a--the-macos-application))* — every Presentation module of the macOS app has its own shared scheme, and every view it draws a hermetic `#Preview` over the binary's recorded documents (the user's call, 2026-10-21), held by the layering check.
- See the [`backlog/`](../backlog/README.md) index for the work queue.

### v0.2.0 — Track 2, CLI parity

Opened 2026-10-20 (the user's call, from the first hands-on run: the GUI does everything the CLI and TUI do — the SPEC's Full-parity principle, same day). The audit against the TUI's classification found eight command families with no GUI place and no law holding coverage; the split closes the law first, then the families. The same evening the user's next run found the store's verbs unreached: Models listed backends, and nothing could convert, quantize or register stored weights. That became 2h–2i, after the design grew its Stored weights view.

- [x] **The parity law** *(2a, shipped 2026-10-20 — [MILESTONES → Milestone C](MILESTONES.md#milestone-c--cli-parity))* — ADR 0003 and the classification table, held by test against the binary's own word list; the carve-outs recorded.
- [x] **The console** *(2b, shipped 2026-10-20 — [MILESTONES → Milestone C](MILESTONES.md#milestone-c--cli-parity))* — any verb run supervised, completion from the binary, carve-outs refused with their reasons; the catch-all while screens accrete.
- [x] **Knowledge & collections** *(2c, shipped 2026-10-20 — [MILESTONES → Milestone C](MILESTONES.md#milestone-c--cli-parity))* — the record and store screens, supervised ingest and builds, the graph screen's prose half.
- [x] **The training studio** *(2d, shipped 2026-10-20 — [MILESTONES → Milestone C](MILESTONES.md#milestone-c--cli-parity))* — Train and Datasets over the documents, runs on the trainer's structured stream, promote/rollback confirmed.
- [x] **Agents, MCP & symphonies** *(2e, shipped 2026-10-20 — [MILESTONES → Milestone C](MILESTONES.md#milestone-c--cli-parity))* — the resource screens, native in-app editing validated by the binary, plays into the session.
- [x] **Execute sessions, plays & recipes** *(2f, shipped 2026-10-21 — [MILESTONES → Milestone C](MILESTONES.md#milestone-c--cli-parity))* — the picker's execute door, recipe expansion shown before launch, analyze console-covered.
- [x] **The review screen** *(2g, shipped 2026-10-21 — [MILESTONES → Milestone C](MILESTONES.md#milestone-c--cli-parity))* — dry-run-first, click-consented posting, targets, sweeps and skills; silence a first-class pass.
- [x] **The model store** *(2h, shipped 2026-10-21 — [MILESTONES → Milestone C](MILESTONES.md#milestone-c--cli-parity))* — Backends and Stored weights apart, as the design draws them: every set by its handle, with made-from links; register one or all, remove a backend, delete with or without its backends, rename.
- [x] **Convert, quantize and check as jobs** *(2i, shipped 2026-10-21 — [MILESTONES → Milestone C](MILESTONES.md#milestone-c--cli-parity))* — the sheets build the CLI's own chain, with names as the CLI gives them; each run is supervised on the Jobs tab, using the G18 and G20 fallbacks.

### v0.2.0 — Track 3, MLX kinds in the app

Opened 2026-10-21 from the CLI's MLX-for-every-kind spike (the user's call): the CLI's 53e–53m make MLX directories of six kinds and add MLX engines for video and voice, and drafters for Gemma 4. The app follows what the binary states.

- [x] **MLX kinds in Stored weights** *(3a, shipped 2026-10-22 — [Milestone D](MILESTONES.md#milestone-d--mlx-kinds-in-the-app))* — each MLX set says its kind and offers only its kind's actions; the convert sheet says when towers are kept.
- [x] **MLX engines and drafters** *(3b, shipped 2026-10-22 — [Milestone D](MILESTONES.md#milestone-d--mlx-kinds-in-the-app))* — drafters paired with their bases, and the clip and say sheets naming their engine.


### v0.2.0 — Track 4, the CLI/GUI audit's findings

Opened 2026-10-22 (the user's call): a thorough bug and feature-disparity audit over the CLI and GUI, 14 dimensions, probes against the branch binary and a three-lens adversarial panel per finding. 111 of 112 findings survived; the GUI's 68 (11 high) land here as twelve items. The CLI's 43 are its track 55.

- [x] **Private sessions survive** *(4a, shipped 2026-10-25 — [Milestone E](MILESTONES.md#milestone-e--the-audits-gui-findings))* — leaving Sessions never wipes a private conversation; ⌘Q completes the shutdown; a stop during launch reaps its child.
- [x] **Honest session lines** *(4b, shipped 2026-10-25 — [Milestone E](MILESTONES.md#milestone-e--the-audits-gui-findings))* — lines queued between turns, relaunch on current settings, dead cards closed, earlier and live rows kept apart, resumed chips and warnings, renames that stick.
- [x] **Children end cleanly** *(4c, shipped 2026-10-25 — [Milestone E](MILESTONES.md#milestone-e--the-audits-gui-findings))* — Stop always ends a job; no leaks; a failing override said; PATH read safely; every child on the monitor.
- [x] **Markdown and code keep every line** *(4d, shipped 2026-10-25 — [Milestone E](MILESTONES.md#milestone-e--the-audits-gui-findings))* — link definitions, short math answers, pipe paragraphs, bare fences, heredoc shifts.
- [x] **Store jobs keep their identity** *(4e, shipped 2026-10-25 — [Milestone E](MILESTONES.md#milestone-e--the-audits-gui-findings))* — a quantize registers its own GGUF; resumes only on stops; delete's warnings shown.
- [x] **The media studio, finished** *(4f, shipped 2026-10-25 — [Milestone E](MILESTONES.md#milestone-e--the-audits-gui-findings))* — supervised installs, compose's voice model and consent, honest toasts, captions said up front.
- [x] **Console and argv safety** *(4g, shipped 2026-10-25 — [Milestone E](MILESTONES.md#milestone-e--the-audits-gui-findings))* — carve-outs refused after root options, the exec line's refusals, user text after `--`.
- [x] **Buttons that do what they say** *(4h, shipped 2026-10-26 — [Milestone E](MILESTONES.md#milestone-e--the-audits-gui-findings))* — Tasks' resume in its folder and state, no stale saves, results on their own selection, empty states and failures said.
- [x] **Training and suites parity** *(4i, shipped 2026-10-26 — [Milestone E](MILESTONES.md#milestone-e--the-audits-gui-findings))* — adapter promote and editing, datasets pull, the shared-base fit, a chat under any suite.
- [x] **Pipeline and cycle screens** *(4j, shipped 2026-10-26 — [Milestone E](MILESTONES.md#milestone-e--the-audits-gui-findings))* — named pipelines run, resumed and watched stage by stage; the cycle's state, a pass run, halted and resumed; on the CLI's 55h JSON.
- [ ] **The remaining verbs** *([4k](../backlog/v0.2.0/settings-sessions-graph-parity.md))* — saved-chat compact/info/recall, one provider registered, `server_tools` shown, graph export/dedupe/delete, MCP remove, raw records, capabilities from launch.
- [ ] **Docs and the board catch up** *([4l](../backlog/v0.2.0/docs-and-board-catch-up.md))* — shipped gaps flipped by probe; ROADMAP, SPEC and CLAUDE stating what shipped.

---

## Fast follow

- [x] **Stored weights by handle** — maintenance item M1 *(shipped 2026-10-20 — [MILESTONES → Milestone B](MILESTONES.md#milestone-b--the-designs-screens))*: the user's run of the built app showed none of their SafeTensors sets. The CLI names every stored, unconfigured row `(not configured)`, and the GUI keyed rows by that name, so 18 stored rows drew as one, and info, delete and repair, Voice's voices and Settings' pipelines were all asked for `(not configured)`. Fixed by naming a stored row by its handle and showing the fold.
- [x] **Store commands the CLI takes** — maintenance item M2 *(shipped 2026-10-21 — [MILESTONES → Milestone C](MILESTONES.md#milestone-c--cli-parity))*: the user's run of 2026-10-21 met the binary's refusals from the Models place — an MLX conversion of two precisions (`-t 8bit --register-with 4bit`, where since the CLI's M16 `--register-with` names every precision) and a rename of the MLX backend `GPT-OSS-20B-mixed_4_8` (`models rename`, which takes a GGUF family or a stored folder; a backend is `config rename-backend`, which the app never reached); the same form also sends a GGUF precision other than f16 with Register, refused. The sheets build the forms the CLI takes, the binary's completion says which names `models rename` takes, and every sheet command is held to the binary's recorded verdicts — the 2i tests had pinned the refused commands.
- [x] **Convert offers what the set takes** — maintenance item M3 *(shipped 2026-10-21 — [MILESTONES → Milestone C](MILESTONES.md#milestone-c--cli-parity))*: Stored weights offers both conversions on every SafeTensors set, so the user could start one on `Wan-AI--Wan2.2-TI2V-5B`, a diffusion model no converter takes, and on their draft heads. Once `models info` states `data.converts_to`, the buttons follow it; an older binary as today.
- [ ] **Only backends that chat are offered to chat** *([M4](../backlog/maintenance/chat-choices-follow-kind.md))* — Sessions, Settings and Training offer every backend, so the embedding entry the CLI's 53h registers can be chosen to chat with; their choices follow each row's `kind`.
- [ ] **The Backends table says kinds and drafters** *([M5](../backlog/maintenance/backends-say-kind-and-drafter.md))* — the kind beside the format, a paired base marked, a drafter paired from the base's details, a pairing that can no longer draft drawn.
- [ ] **A session's window says its chat and model** *([M10](../backlog/maintenance/session-label-follows-its-chat.md))* — the user's window said `claude` and an empty chat's id while Gemma answered; the label follows each answer's model, and a new session is identified by the CLI's `chat_id` or a fresh snapshot that skips empty chats.
- [x] **Code blocks coloured by syntax** — maintenance item M9 *(shipped 2026-10-22 — [MILESTONES → Milestone A](MILESTONES.md#milestone-a--the-macos-application))* — M8's code blocks draw in one colour; the design's five code colours join `Palette`, a token-level lexer of the app's own colours the common languages a fence names, and a diff colours by line.
- [x] **Chat answers render their markdown** — maintenance item M8 *(shipped 2026-10-22 — [MILESTONES → Milestone A](MILESTONES.md#milestone-a--the-macos-application))* — every answer is drawn as plain text, so `**bold**`, lists and code fences show raw; a shared renderer over Foundation's parser draws them as the design does, live and resumed, streaming without flicker.
- [x] **A resumed chat shows its earlier turns** — maintenance item M7 *(shipped 2026-10-22 — [MILESTONES → Milestone A](MILESTONES.md#milestone-a--the-macos-application))* — a reopened conversation draws only `resumed: … aren't shown here`; its turns, read by the CLI's `chats show`, drawn as live ones under a divider.
- [x] **The app's children get the shell's PATH** — maintenance item M6 *(shipped 2026-10-22 — [MILESTONES → Milestone A](MILESTONES.md#milestone-a--the-macos-application))* — launched from the Dock the app gives `apogee` launchd's `PATH`, so every cloud backend's vendor CLI is unreachable (the user's `claude` default refused); the login shell's `PATH` is resolved once at launch, bounded, with a well-known-directories fallback.

## Unspecced ideas

- **`Apogee.app` in releases** — the merge's release carrying the ad-hoc-signed app beside the CLI's archives. The user's 2026-10-20 call kept it a CI artifact until the session window shipped, which it now has (1c); how the release scripts take it is its own item, on the user's word.
- **Setting the binary override from the not-found screen** — since 1p, Settings › General's "Use another apogee…" sets the `binaryOverride` default; a "Locate apogee…" chooser on the not-found screen itself would set it before any session starts (1c's note).
- **The answer as Markdown** — the window shows an answer as its exact text; rendering its Markdown, as the CLI's terminal does, is a deliberate later step (1c's note).

## Ideas / candidate features

*(none yet)*

## Explicit non-goals

Ruled out on purpose (see [SPEC.md](SPEC.md) → Non-goals for the rationale):

- **No capability of its own**
- **No HTTP path for local work**
