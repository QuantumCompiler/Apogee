# Apogee GUI — Backlog

**The work queue: one document per work item.** This directory holds every GUI feature and fix that has been discussed and specced but **not yet built** — the layer between the loose ideas in [ROADMAP.md](../../assistant/darwin/ROADMAP.md) and the shipped record in [MILESTONES.md](../../assistant/darwin/MILESTONES.md). There is no separate TODO file: an agent takes an item from the index below and **implements it straight from its document**. (The CLI's queue is its own, at [`backlog/cli/`](../cli/README.md); an item that needs a CLI change gets a sibling document there, gated both ways.) The release-by-release view across every product is the [backlog overview](../README.md); this index is the authority on its rows, and every change to it reaches the overview in the same pass ([Keeping this page true](../README.md#keeping-this-page-true)).

## Lifecycle

1. A feature is **discussed in chat** and gets a line on [ROADMAP.md](../../assistant/darwin/ROADMAP.md) (and a [SPEC.md](../../assistant/gui/SPEC.md) update if it changes product shape).
2. Once specced — files, decisions, open questions — it gets **its own document here**, written as a ticket ([Document format](#document-format)) (kebab-case filename, listed in the index below in priority order). The same pass adds it to the [backlog overview](../README.md#keeping-this-page-true): its release's section, or its track's line under Later releases.
3. When an agent **takes** it (told explicitly, or via the bare-"Continue" convention: the topmost item whose gate is satisfied), the document **is** the working spec — confirm scope with the user first (the document's **Open questions** are the natural questions), mark its index row **in progress** (🚧) and its ticket's header Status (🚧 In progress), and build. One item at a time.
4. When it **ships**, the work is recorded in [MILESTONES.md](../../assistant/darwin/MILESTONES.md) per the docs flow in [CLAUDE.md](../../assistant/darwin/CLAUDE.md), and the document is **deleted, along with its index row** — a completed item must never remain in the backlog. On the same pass, every item it gated has its index row and its ticket's header updated in the same pass: **Blocked by** loses the shipped id, **Status** turns 🟢 Ready when that was its last gate, and the shipped id leaves any **Blocks**. Its row leaves the [backlog overview](../README.md#keeping-this-page-true) too, with the counts, "can start today" and any "Waits on" it satisfied updated.

**The standing invariant:** every document in this directory is pending work. If it's shipped (in MILESTONES.md), it's not here.

**Tags and gates** follow the CLI backlog's conventions: an open question's Status cell is `[user]` (genuinely user-owned; blocks the build), `[default]` (the Answer is the recorded default: the agent takes it, records it in the Decision log, and the user may veto) or `(consumed decision)` (owned by another item); a row's Status is 🟢 workable, 🔒 gated on a named real item (no cycles), or 🚧 in progress. A changed gate, a split, or a row moved to another table changes the [backlog overview](../README.md#keeping-this-page-true) in the same pass.

## Document format

Every item document reads like a ticket written in markdown (the user's call, 2026-11-21): a header of fields, then a story, the evidence, and tables for the files, the open questions and the decisions — the CLI's [document format](../cli/README.md#document-format), with this queue's key and project. It is still the **working spec** an agent builds from without asking anyone. Sections in this order; omit one only when it's genuinely empty (the Open questions table instead holds one row, `None open`):

```markdown
# <KEY>-<id> · <Item title>

| Field | Value |
|---|---|
| **Type** | <🐞 Bug · ✨ Feature · 🛠️ Task> |
| **Project** | <Apple apps, or Apple apps · macOS · Apple apps · iPadOS · Apple apps · iOS for one OS's item> |
| **Status** | <the index row's Status: 🟢 Ready · 🔒 Blocked · 🚧 In progress · ❌ Cancelled> |
| **Fix version** | <vX.Y.Z · Maintenance (any time)> |
| **Component** | <the area it changes: a command family, a module, a screen> |
| **Labels** | <2–4 kebab-case labels in backticks; `split-first` when the item is split first> |
| **Epic** | <Track N · <the track's name>, or — for a table with no track> |
| **Blocked by** | <each gate, linked> or — |
| **Blocks** | <each item gated on this one, linked> or — |
| **Reporter** | <who or what raised it: the user, a spike (dated), a design pass, an audit, a bug report> |
| **Created** | <YYYY-MM-DD: the earliest date the document records> |

---

## Summary

<One to three sentences: what is wrong or missing, and what the item delivers.>

## Description

### User story

**As** <who>, **I want** <the capability>, **so that** <the outcome>.

### Steps to reproduce

<🐞 Bug only, from what was actually seen: 1. … 2. …>

### Actual result

<🐞 Bug only.>

### Expected result

<🐞 Bug only.>

### Background

<The context that is not evidence: history, how the item came about, the design's shape in prose.>

### Evidence

<Numbered: every measurement, probe, quote and `file:line`, each dated with its method. Root cause, blast radius, what already works and a spike's walls (**W3 — …**) are bold-led items here.>

### Split first

<Only when the item is marked split first: the sub-documents the grooming must produce.>

## Acceptance criteria

- [ ] **Given** <state>, **when** <action>, **then** <observable outcome that means "done">.

## Constraints

- **<Short name.>** <A rule the design must not violate: an invariant it touches, compatibility it must keep, with its ADR or source.>

## Technical approach

Paths under `<common root>`. <one line, when the paths share one>

| File | Change |
|---|---|
| `path/to/file` | <what changes there> |

<Prose the table cannot hold: an interface sketch, a protocol shape, a worked example.>

**Reference.** <Optional: the in-house precedents this item builds on and any external prior art, with each deliberate divergence from it.>

**Docs when it ships:** <Optional: the documents the build updates.>

## Testing

<One line on the hermetic rule, when one applies.>

- **<Test suite or check.>** <What must be tested or checked so the change can't regress silently.>
- **Gate:** <the command that proves it, when the item names one.>

## Open questions

| Question | Status | Answer |
|---|---|---|
| <a question to resolve before building> | `[user]` · `[default]` · `(consumed decision)` | <the recommendation, the default in full, or the item that owns it> |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| <YYYY-MM-DD> | <a design decision and its rationale> | <the user · default · the spec · the spike · the item's build · —> |

## Out of scope

- <each exclusion>

## Links

- **Blocked by:** … · **Blocks:** … · **Sibling:** … · **Relates to:** … · **Reverses / Follows from / Follow-on:** …
```

- **The key** is `APP` for this queue, followed by the item's id from the index (`APP-7d`, `APP-M19`); the **Project** is `Apple apps`, or `Apple apps · macOS` / `· iPadOS` / `· iOS` for an item in one OS's folder. The title words are the index row's.
- **The header restates the index row.** Status, Fix version, Blocked by and Blocks say what the item's row in the index says; **the index stays the authority**, so any change to a row (a status, a move to another table, a gate added or satisfied) changes the document's header in the same pass, along with the Epic and the key-id H1 when the move changes them. A cross-queue gate is linked with the queue's name (`[CLI 59b](../../cli/…)`).
- **The Status cell of an open question** is one of the tags above: `[user]`, `[default]` or `(consumed decision)`. An answered question leaves the table and becomes a Decision-log row.
- **Type** is 🐞 Bug for behaviour that is wrong today, ✨ Feature for a new capability, 🛠️ Task for upkeep with no user-visible change. A bug's Steps to reproduce come only from what was actually seen; its evidence is baked in.
- **Line references into other backlog documents** name the document and its section, never a line number, since documents are re-shaped; line references into source code stay.

## Index

Priority-ordered: the topmost item whose gate is satisfied is the next one to build.

**Tracks 1–4 have shipped**, complete in [v0.2.0](https://github.com/QuantumCompiler/Apogee/releases/tag/v0.2.0), released 2026-10-31:
- **Track 1, the macOS application** (1a–1q), opened 2026-10-20 from that day's GUI spike (the user's call: Swift and SwiftUI, macOS first, unsigned ad-hoc) and grown the same day by the design-completion split: the app, its client core, the session window and its pipeline in [Milestone A](../../assistant/darwin/MILESTONES.md#milestone-a--the-macos-application) (1a–1d), every screen of the design in [Milestone B](../../assistant/darwin/MILESTONES.md#milestone-b--the-designs-screens) (1e–1q).
- **Track 2, CLI parity** (2a–2i), opened 2026-10-20 (the user's call: the GUI does everything the CLI and TUI do, the SPEC's Full parity principle): the parity law, the console, and a place for every command family, the model store among them, in [Milestone C](../../assistant/darwin/MILESTONES.md#milestone-c--cli-parity).
- **Track 3, MLX kinds in the app** (3a, 3b), opened 2026-10-21 from the CLI's MLX-for-every-kind spike: each MLX set's kind, engines and drafters, in [Milestone D](../../assistant/darwin/MILESTONES.md#milestone-d--mlx-kinds-in-the-app).
- **Track 4, the CLI/GUI audit's GUI findings** (4a–4l), opened 2026-10-22 (the user's call: a thorough bug and feature-disparity audit over both applications), in [Milestone E](../../assistant/darwin/MILESTONES.md#milestone-e--the-audits-gui-findings).

**Tracks 5, 6 and 11 have shipped**, complete in [v0.2.1](https://github.com/QuantumCompiler/Apogee/releases/tag/v0.2.1), released 2026-11-26:
- **Track 5, Apogee in Xcode, the app's side** (5a), opened 2026-10-31 from that day's Xcode research (the user's call; the CLI's track 56 is the capability): Settings › Xcode, in [Milestone F](../../assistant/darwin/MILESTONES.md#milestone-f--apogee-in-xcode-the-apps-side).
- **Track 6, the second bug sweep's GUI findings** (6a–6j), opened 2026-10-31 (the user's call: a second bug sweep over both applications), in [Milestone G](../../assistant/darwin/MILESTONES.md#milestone-g--the-second-sweeps-gui-findings).
- **Track 11, the app's logo** (11a), opened 2026-11-14 (the user's call): the macOS app's icon generated from `lib/design/assets/Logo.png`, in [Milestone H](../../assistant/darwin/MILESTONES.md#milestone-h--the-apps-logo).

Their story is [ROADMAP.md](../../assistant/darwin/ROADMAP.md), and each item's record is in [MILESTONES.md](../../assistant/darwin/MILESTONES.md). The earliest version table open is **v0.2.4**'s, the Mac as a client and the server window; the standing Maintenance table holds the GUI's upkeep.

### Maintenance

Upkeep and fixes, claimable at any time by name: opened 2026-10-20 with its first item, mirroring the CLI's conventions (`M<n>` ever-assigned, the documents in `maintenance/`). **M1** comes from the user's run of the built app the same day, when the Models library showed none of the store's SafeTensors sets: every stored row arrives named by the CLI's `(not configured)` placeholder, and the GUI keyed rows by it. **M1 shipped 2026-10-20** ([MILESTONES → Milestone B](../../assistant/darwin/MILESTONES.md#milestone-b--the-designs-screens)). **M2** and **M3** come from the user's next run, 2026-10-21, when the Models place's store verbs met three refusals: an MLX conversion of two precisions, a backend's rename, and a diffusion model's conversion to MLX. Two were the app's commands, built against the binary's `--help` and not the rules between its flags (M2, the convert sheet's chains and the rename). The third is a set no converter takes, which the CLI does not yet say: the CLI's M20 states it, and M3 offers only what it states. The CLI's **M20 shipped 2026-10-21**, which opens M3. **M2 and M3 shipped 2026-10-21** ([MILESTONES → Milestone C](../../assistant/darwin/MILESTONES.md#milestone-c--cli-parity)). **M4** and **M5** come from track 3's record (3a and 3b's "Not done" in [Milestone D](../../assistant/darwin/MILESTONES.md#milestone-d--mlx-kinds-in-the-app); the user's call, 2026-10-22: "Queue the MLX track's follow-ups as backlog items"): M4, an embedding entry offered as a chat backend by Sessions, Settings and Training; M5, the Backends table silent on kinds and drafters, with no pairing from the base's side. Every call is a recorded default; the CLI's M22–M25 are their siblings in spirit, neither gated on the other. **M6** comes from the user's run the same day: a session refused `no backend serves 'claude'` though `claude` works in their terminal. Launched from the Dock, the app hands its children launchd's `PATH`, so every vendor CLI (`claude`, `codex` in `~/.local/bin`, `gemini` in `/opt/homebrew/bin`, `ollama` in `/usr/local/bin`) is out of reach. That is the cost 1b recorded and deferred. The CLI's M26 makes the refusal say so. **M6 shipped 2026-10-22** ([Milestone A](../../assistant/darwin/MILESTONES.md#milestone-a--the-macos-application)): the app asks the person's login shell for its `PATH` once at launch, bounded, with a well-known-directories fallback, and every `apogee` child gets it; Settings says which. **M7** comes from the user's request the same day: "When we reload a chat, I want the previous turns to show", where a reopened conversation drew only the `resumed: … aren't shown here` notice; it is gated on the CLI's M27, the read of a saved conversation's turns. **M7 shipped 2026-10-22** ([Milestone A](../../assistant/darwin/MILESTONES.md#milestone-a--the-macos-application)): a reopened conversation reads its turns through `chats show` and draws them as live ones above an "earlier in this conversation" divider; an older binary keeps the notice. **M8** comes from M7's record the same day: the app draws every chat answer as plain text (`TranscriptRow.swift:25–31`, a `Text` of a `String`, which SwiftUI never parses), while the design draws prose, inline code and captioned code blocks with Copy. **M8 shipped 2026-10-22** ([Milestone A](../../assistant/darwin/MILESTONES.md#milestone-a--the-macos-application)): a shared `MarkdownText` over Foundation's parser draws every chat answer's markdown, live and resumed, streaming without flicker. **M9** comes from M8's record (the user's call the same day: "spec the syntax colouring for code blocks"): M8 draws fenced code in one colour, while the design colours it with five tokens the app does not yet carry, each clearing 4.5:1 on `surface.code` in every theme. **M9 shipped 2026-10-22** ([Milestone A](../../assistant/darwin/MILESTONES.md#milestone-a--the-macos-application)): the five code colours in `Palette`, a lexer of the app's own for 20 languages and their aliases, and diffs coloured by line. **M10** comes from the user's run the same day: a window labelled `claude` and named for an abandoned empty chat (`9a66`, 0 turns) while their Gemma conversation (`d3e2`) ran in it. The label never followed a `set` switch (each `result.model` ignored), and a new session opened from an unsent one was pinned to the left one's empty chat by a snapshot taken from a stale list; the CLI's M28 names the chat outright. **M4 shipped 2026-10-26** ([Milestone D](../../assistant/darwin/MILESTONES.md#milestone-d--mlx-kinds-in-the-app)): every place that picks a backend to chat with or to train -- the session sheet, the execute door, Synthesize, adapter promote and the Roles chat row -- offers only the rows whose kind chats, while the places that show backends keep every entry. **M5 shipped 2026-10-26** ([Milestone D](../../assistant/darwin/MILESTONES.md#milestone-d--mlx-kinds-in-the-app)): the Backends table says an `mlx` entry's kind and marks a base with a drafter paired, a drafter pairs from its base's details among what the binary's completion offers, and a pairing that can no longer draft is drawn in the CLI's words. **M10 shipped 2026-10-26** ([Milestone A](../../assistant/darwin/MILESTONES.md#milestone-a--the-macos-application)): the header, the inspector, the context meter and the monitor follow each answer's `result.model`, and a new session is identified by the CLI's `chat_id` -- or, against an older binary, by a fresh snapshot that passes over empty chats -- so Rename, Delete and a relaunch act on the conversation shown. **M11** and **M12** come from the GUI v0.2.0 and Maintenance runs' records (the user's call, 2026-10-26: "spec them into the backlog", placed here): the app grew screens and states the design never drew -- Training's Pipelines and Cycle tabs (M11, two new boards and the contract board's rows), and every other state track 4 and M5 added (M12). Both are design only, claimable by name; every call is a recorded default. Their CLI siblings in spirit, the gaps those runs found, are the CLI's M29–M33, neither gated on the other. **M11 shipped 2026-10-26** ([Milestone E](../../assistant/darwin/MILESTONES.md#milestone-e--the-audits-gui-findings)): the prototype's Training place draws Pipelines and Cycle from 4j's recordings, two new boards show a pipeline run and the cycle, and the contract board maps their reads and streams (canvas version 48). **M12 shipped 2026-10-26** ([Milestone E](../../assistant/darwin/MILESTONES.md#milestone-e--the-audits-gui-findings)): every other state track 4 and M5 added is drawn in the prototype in the CLI's words, three as new boards (a suite chosen, a delete's plan, an install running), the machine's own states reached by an `apogee` prop (canvas version 49). **M13–M16** are the app's adoption of the CLI's M29–M33 (the user's call, 2026-10-26: "spec them into the backlog and then execute them"). M13 reads the session's `suite` and the refusals the CLI now sends; M14 reads named pipelines, the cycle's kept halt (fixing the Halt wording M11 found wrong) and `datasets pull`'s stream; M15 reads chat details, recall, dedupe, raw records and the footprint as documents; M16 reads the new list-row fields and the finished config verbs. Each keeps today's fallback for an older binary; every call is a recorded default. **M13 shipped 2026-10-26** ([Milestone E](../../assistant/darwin/MILESTONES.md#milestone-e--the-audits-gui-findings)): a session's suite and execute's orchestrating come from `session`, each suite line appears once, a refused launch is drawn as refused, and the install consent reads the stream's `error`. **M14 shipped 2026-10-26**: the Pipelines tab lists the named specs and their stages from `train pipeline list`/`show`, Halt is offered during a pass where the binary keeps it, and Pull… runs `datasets pull` as a job, Prepare it taking its document's folder. **M15 shipped 2026-10-26**: Details…, Recall (its summaries listed, Forget naming them), Dedupe…, the raw conversation and the Suites fit read the CLI's documents, each falling back to its text read on an older binary. **M16 shipped 2026-10-26**, and with it the app has adopted the CLI's M29–M33: a killed task's row says interrupted, drafters come from `models list`, Register runs `providers register`, Permissions reads its hosts through the CLI, and the CLI's default-set and role-cleared lines are shown. **M17** comes from the user's look at the v0.2.0 release, 2026-10-31: it carried no `Apogee.app`, by the pipeline's design (the app was a run artifact, its release "its own later step, on the user's word"). The user's word, the same day: v0.2.0's app was attached by hand from #11's Darwin run, and M17 makes every release carry it. **M17 shipped 2026-10-31** ([MILESTONES](../../assistant/darwin/MILESTONES.md)). **M18** comes from the user's call the same day that development runs on the dev channel, so the production install's updaters are tested for real: a development build of the app drives the release `apogee` and shares the installed app's settings, and M18 gives it the CLI's dev channel. **M18 shipped 2026-11-21** ([Milestone A](../../assistant/darwin/MILESTONES.md#2026-11-21--app-dev-channel-maintenance-item-m18-a-development-build-of-the-app-runs-on-the-dev-channel)): the Debug build is "Apogee Dev" on `com.quantumcompiler.apogee.dev`, its own settings and an amber icon, driving `apogee-dev` alone, and `build.sh open` builds it; the Release app is unchanged. **M19** comes from the design pass of 2026-11-14 (the user's call: "Maintenance, as M19"): its whole-design check found Settings › Xcode's Copy (5a) silent in the app while the design says "Copied to the clipboard", and a row with no label named "Copy " to VoiceOver. The app's toast lives only in the main window, so M19 also gives the Settings window its own toast while it is in front; it reverses 5a's for-veto decision that Copy says nothing, and every call is a recorded default. **M19 shipped 2026-11-21** ([Milestone F](../../assistant/darwin/MILESTONES.md#2026-11-21--xcode-copy-says-copied-maintenance-item-m19-copy-in-settings--xcode-says-it-copied)): Copy says "Copied to the clipboard" once the pasteboard took the value, in the Settings window's own toast, which shows every line Settings says while it is in front; every Copy is named for what it copies; the toast view is promoted to `styles`. **M20** comes from the user's report of 2026-11-21: deleting a session from its right-click menu never works. The CLI deletes (a sandboxed run of the installed 0.2.0), and a probe of the dialog showed why: pressing a confirmation dialog's button closes the dialog, which clears the question in the view-model before the button's action reads it back, so nothing runs and nothing is said. Twenty-three buttons in twelve dialogs share the shape, every one since v0.2.0's first places; M20 hands each button its answer and adds a check that refuses the shape; every call is a recorded default. **M20 shipped 2026-11-21** ([Milestone B](../../assistant/darwin/MILESTONES.md#2026-11-21--confirmations-act-on-their-answer-maintenance-item-m20-a-confirmed-action-runs)): every confirming button hands its view-model the answer the dialog showed, the view-model clearing its question only while it is still that answer; a new chat asked under a suite keeps it through the dialog; and the layering check fails a dialog whose button reads its question back, naming the file and line. **M21–M23** come from the CLI's M45–M46 investigation (2026-11-22), whose Out of scope named two app bugs, then a sandboxed investigation, two skeptics and a critic each (2026-11-26). The app passes a listed name before any `--`, so the CLI's parser reads a flag-shaped one (`-x`, `-h`) as an option: a `-x` collection's page never loads, a dataset `-h`'s Delete toasts the verb's help as success, and Ingest more… into ` sp` writes a different store `sp`. **M21** gives `Argv` a line builder and moves those rows; **M23**, gated on it, moves the app's other ~95 sites and adds the checks that keep the rule. **M22**: the jobs core never reads a successful run's stderr, so the CLI's `apogee <verb>: …` notes (a collection not registered, a graph retrieval won't use, a capture validated) never reach the person; it reads them as lines and shows them on a card, drawn in the design first. Every call is a recorded default. **M21 shipped 2026-11-26** ([Milestone E](../../assistant/darwin/MILESTONES.md#2026-11-26--listed-name-reaches-the-verb-maintenance-item-m21-a-listed-name-reaches-the-verb-as-listed)): `Argv` builds a line as its command, its options, then `--` and the names, an option that eats `--` never last before it, and shows it as a person types it; Collections, Graph, Training's datasets and the Models backend rows run on it, a listed name passed byte for byte, so the `-x` page loads, a dataset `-h` is deleted and said so, and Ingest more… adds to ` sp`. M23 waits on nothing now.

| # | Item | Version | File | Status |
|---|---|---|---|---|
| M22 | A job's notes reach the person — the jobs core reads a successful run's stderr as lines, keeps the CLI's `apogee <verb>: …` notes, and Collections, Records and Graph show them on the promoted narration card, drawn in the design first | maint. | [`a-jobs-notes-reach-the-person.md`](macos/maintenance/a-jobs-notes-reach-the-person.md) | 🟢 |
| M23 | Every name the app passes is read as a name — the app's other ~95 argv sites on M21's builder, the readers by position and the shown lines, with a verdict sweep, a layering rule and a preview-reads test that keep it | maint. | [`every-name-the-app-passes-is-read-as-a-name.md`](macos/maintenance/every-name-the-app-passes-is-read-as-a-name.md) | 🟢 |

### v0.2.4

**The Apple-platforms plan: tracks 7, 8 and 9**, opened 2026-11-07. The vision, the user's calls and the measurements are in the CLI's [track 59–61 preamble](../cli/README.md#v023); the GUI SPEC and the machine-mode-client rule were amended the same day. The app is close to portable. It has 184 Swift files: 7 import AppKit, none import UIKit, and one file, `ProcessSpawner.swift`, starts the CLI. That one seam can take a network transport beside the child process. The release order:
- **v0.2.4, the Mac as a client** (7a–7c), on the CLI's v0.2.3 server: the connection, local or your server; the Mac offering its own files and shell; every place working against a server.
- **v0.2.5, iPad, iPhone, the foldable and the three platforms whole** (8a–8c, 9a–9c): the code built for iOS and iPadOS, the iPad app, and getting the app onto iPhone and iPad, then the iPhone, the foldable and the proof of the whole. **8c carries the [user] call** (the Apple Developer Program, or personal sideloading).

The Mac stays complete with no server, and 9c checks it.

**Apps as clients: 7d and 7e**, opened 2026-11-14 (the CLI's [v0.2.3 preamble](../cli/README.md#v023) carries the spike). **7d** carves the client core into ApogeeKit, a Swift package other apps embed and the Mac app depends on; **7e** writes the package's law (its ADR, the layering and public-interface checks, the contributor rules).

**Track 10, the server window**, opened 2026-11-14 from the admin-mode and people spike (the CLI's [track 62 preamble](../cli/README.md#v024) carries the walls and the user's calls; the GUI SPEC's Core surfaces 3 was amended the same day). On the Mac that serves, a separate **Apogee Server** window drives the local CLI over pipes, never the port: **10a** draws it in the design first (closing A9), and **10b**, **split first**, builds Server, Devices, Apps, Activity and Audit. The People place is **10c**, in the same release, on the CLI's track 63.

**Merged 2026-11-18** (the user's call): v0.2.4 took in v0.2.6 and v0.2.7 (track 10, the server window and its People place), and v0.2.5 took in v0.3.0 (track 9). The items kept their ids; their documents moved into the `v0.2.4/` and `v0.2.5/` folders, and every reference moved with them. Nothing had shipped under any of those versions.

| # | Item | Version | File | Status |
|---|---|---|---|---|
| 7a | The app's connection: local or your server — a transport seam under ChildSpawner (the child process by default and unchanged, or a paired server's connection), Settings → Connections to pair, pick and forget servers, and an indicator showing the active connection everywhere; with no server paired, the app is exactly today's | v0.2.4 | [`app-connection-local-or-server.md`](v0.2.4/app-connection-local-or-server.md) | 🔒 CLI 59b, CLI 59d |
| 7b | The Mac offers its own files and shell to a server session — a folder granted per project and server, each device call run by the Mac's own local apogee and asked on the session window's permission card marked On this Mac, grants revocable in Settings, never offered while connected locally | v0.2.4 | [`mac-offers-its-files-and-shell.md`](macos/v0.2.4/mac-offers-its-files-and-shell.md) | 🔒 7a, CLI 60b |
| 7c | Every place works against a server — each place's reads, commands and jobs run through the active connection, the media places show and play the server's records, the parity table classifies each command's remote meaning, and an act a server cannot do says why | v0.2.4 | [`every-place-against-a-server.md`](v0.2.4/every-place-against-a-server.md) | 🔒 7a, CLI 59e |
| 7d | ApogeeKit: the client core as a Swift package — the lower layers and 7a's connection carved into a root Package.swift other apps embed, the Mac app linking it as its one copy, the layering check reading both maps, a sample command-line client building outside the app | v0.2.4 | [`apogeekit-swift-package.md`](v0.2.4/apogeekit-swift-package.md) | 🔒 7a |
| 7e | Maintaining ApogeeKit — a darwin ADR (the one client core; its public interface only grows), the layering check's package rules and a public-interface check against a baseline, the darwin contributor guide's rule and checklist lines, a DEVELOPER recipe | v0.2.4 | [`maintaining-apogeekit.md`](v0.2.4/maintaining-apogeekit.md) | 🔒 7d |
| 10a | Design the server window — the Apogee Server window drawn in the design app before it is built: how it opens from the Apogee menu (only on the Mac that serves), and its six places (Server, Devices with pairing and the QR, Apps with each scope beside what the app asked for, Activity, Audit, and People drawn now for 10c); each part drawn against its CLI words, with every missing word added as a gap on the contract page with its fallback | v0.2.4 | [`design-the-server-window.md`](macos/v0.2.4/design-the-server-window.md) | 🟢 |
| 10b | The server window: administering this Mac's server — a second Apogee Server window on the Mac that serves, driving the local CLI over pipes, with Server, Devices, Apps, Activity and Audit (the last two shown when the binary has them); serve split into a screen and a refusal, devices and apps made screens of this window (7a amended), ADR 0003 amended; split first into three | v0.2.4 | [`server-window.md`](macos/v0.2.4/server-window.md) | 🔒 10a, CLI 62a, CLI 62c, CLI 62h |
| 10c | People in the server window — the owner and members in the Apogee Server window on the Mac that serves: each member's preset and access, their devices and apps, usage against their limit, and the household cloud key granted with its limit; adding, renaming and removing people through the local CLI over pipes, built as 10a draws it, previews over recorded documents, metadata only and never a member's chat contents | v0.2.4 | [`people-screens.md`](macos/v0.2.4/people-screens.md) | 🔒 10b, CLI 63a, CLI 63c |

### v0.2.5

The iPad and getting the app onto iPhone and iPad (track 8), then the iPhone, the foldable iPhone and the three Apple platforms whole (track 9): the plan's target ([preamble](#v024)).

Track 9 joined this release on 2026-11-18, when v0.2.5 took in v0.3.0 (the user's call; [v0.2.4](#v024) records the merge).

| # | Item | Version | File | Status |
|---|---|---|---|---|
| 8a | The app's code builds for iPhone and iPad — one codebase for macOS, iOS and iPadOS, the Mac's code isolated in platform files, the layering check per platform, every scheme and the hermetic suite on the iOS simulator, the Mac app unchanged | v0.2.5 | [`app-builds-for-ios-and-ipados.md`](v0.2.5/app-builds-for-ios-and-ipados.md) | 🔒 7a |
| 8b | The iPad app — every place in iPad layouts (sidebar beside or over the place, side panes beside or as sheets at every Split View, Slide Over and Stage Manager size), the Mac's keys with a hardware keyboard and the pointer, Settings as a sheet, first launch pairing with the person's server or saying how to set one up, one ParityTable | v0.2.5 | [`ipad-app.md`](ipados/v0.2.5/ipad-app.md) | 🔒 8a |
| 8c | Getting the app onto iPhone and iPad — the iOS app built and tested on every pull request with no secret, a way onto the person's devices by the path the user picks, and the Mac's ad-hoc app and release unchanged | v0.2.5 | [`iphone-and-ipad-distribution.md`](v0.2.5/iphone-and-ipad-distribution.md) | 🔒 8a |
| 9a | The iPhone app — every place in compact layouts (tabs and stacks, chat first), remote-only over the person's own server, pairing on first launch, one parity table, and a conversation started on the Mac continued on the iPhone through the server | v0.2.5 | [`iphone-app.md`](ios/v0.2.5/iphone-app.md) | 🔒 8b |
| 9b | The foldable iPhone — folding and unfolding moves between the compact and regular shells on one route, keeping the open session, the draft, a half-answered question, a running job's view and the transcript's place, with no connection change | v0.2.5 | [`foldable-iphone.md`](ios/v0.2.5/foldable-iphone.md) | 🔒 9a |
| 9c | v0.2.5: macOS, iPadOS and iOS whole — every command classified per platform, a hermetic whole pass per platform against a scripted server, the Mac proven complete with no server, and the release shipped with pairing documented | v0.2.5 | [`three-apple-platforms-whole.md`](v0.2.5/three-apple-platforms-whole.md) | 🔒 9b, 8c |
