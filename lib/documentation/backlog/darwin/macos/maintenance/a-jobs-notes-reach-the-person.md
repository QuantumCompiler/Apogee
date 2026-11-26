# APP-M22 · A job's notes reach the person

| Field | Value |
|---|---|
| **Type** | 🐞 Bug |
| **Project** | Apple apps · macOS |
| **Status** | 🟢 Ready |
| **Fix version** | Maintenance (any time) |
| **Component** | The jobs core (`ProcessSpawner`, `SupervisedJob`, `JobProgress`), and the done paths of Knowledge › Collections, Knowledge › Records and Graph |
| **Labels** | `jobs` `cli-parity` `silent-failure` `design` |
| **Epic** | — |
| **Blocked by** | — |
| **Blocks** | [CLI-M58](../../../cli/maintenance/a-runs-notes-are-said-once-on-stderr.md) |
| **Reporter** | The CLI-M45/M46 investigation (2026-11-22), then a sandboxed investigation, two skeptics and a critic (2026-11-26) |
| **Created** | 2026-11-26 |

---

## Summary

When a job exits 0, the app shows one stdout line and never reads stderr. The CLI says what a person must know about a finished run on stderr, in its house voice (`apogee <verb>: …`), and keeps the exit at 0 by design:
- `apogee embed: could not register 'my notes' in config` (the collection forgets its chunking);
- `apogee graph: could not set graph.enabled` (retrieval won't use the graph);
- `apogee embed: not registered in config -- … cannot open config file`;
- `apogee knowledge: validated by utility (checker): …`.

The app says "Ingested" or "Built", and the note is gone. Even the 8 KiB stderr tail it keeps measurably loses a note from the head of a long run. The item has the jobs core read a successful run's stderr as lines and keep the house-voice notes. Collections, Records and Graph show them on a card, after the design draws that state.

## Description

### User story

**As** someone ingesting a collection, capturing a record or building a graph in the app,
**I want** to see what the CLI said about the finished run, in its words,
**so that** I learn the collection was not registered, the graph won't be used, or the record was validated, as I would at the prompt.

### Steps to reproduce

The sandbox, 2026-11-26, with the built CLI `apogee 0.2.0 (33ea685d, 2026-11-22, macos-arm64)`: a throwaway `APOGEE_HOME` and `HOME`, `env -i`, stdin from `/dev/null`, `config init`, mock backends, and a mock embedder. The app was not launched. The app's handling was read in its source, and each run's stdout and stderr were recorded as two pipes.

1. Run `apogee embed ingest 'my notes' '<home>/docs/a note.txt'`, as the app's Ingest… sheet does.
2. Read what the app does with that run's end (`SupervisedJob.swift:72`, `JobProgress.swift:122-148`, `CollectionsViewModel.swift:225-233`).

### Actual result

1. Exit 0. Stdout is `1 file(s), 1 chunk(s)`. Stderr is `apogee embed: could not register 'my notes' in config -- collection name 'my notes' is not one both config formats can edit -- …`. The config is byte-identical.
2. Stdout lines become notices, and stderr only feeds the busy line and, on failure, the alert. On `.done` the app toasts `progress?.notices.first ?? "Ingested"`, here `1 file(s), 1 chunk(s)`, and drops the run. The note is shown nowhere.

### Expected result

2. The toast still says `1 file(s), 1 chunk(s)`. A card on the collection's page shows the CLI's note word for word until it is dismissed, the next run starts, or another collection is chosen.

### Background

The app's jobs core reads a child's stderr for two things only (`SupervisedJob.swift:85-97`, `JobProgress.swift:141`):
- the busy line, a 250 ms sample of the tail's last line, which 1i recorded as display-only;
- a failure's last line, shown in the alert.

Stderr is otherwise kept as an 8 KiB `BoundedTail` (`BoundedTail.swift:17-22`, `ProcessSpawner.swift:118`), which no done path reads. 1b's record says the stderr is "never shown". That describes what was built; no decision was made about a successful run's notes. The GUI SPEC says the app says what the binary says.

The CLI's never-fatal design puts these notes on stderr with exit 0 (`embed.cpp:446-467`, `graph.cpp:975-1001`, `knowledge.cpp:556`). CLI-M46 will turn most of the collection-name notes into refusals before the write, exit 1, which the app already shows. But notes remain after it:
- an existing store the config can't hold;
- a config-less home;
- a registration that fails after the write;
- a gone source tree;
- a verifier's verdict on a capture.

CLI-M46's Out of scope names this as the app's own item.

### Evidence

All measured 2026-11-26 by an investigator, two skeptics and a critic, each in its own sandbox, with the built CLI. Each run's stdout, stderr and exit were recorded (`c1`–`c8`).

1. **The notes are on stderr only, on exit 0.**
   - `c1`, `embed ingest 'my notes' …`: stdout `1 file(s), 1 chunk(s)`, stderr the `could not register` line.
   - `c1b`, the same with a plain name: stderr empty.
   - `c7`, `graph build` on that store: two notes, `could not register` and `could not set graph.enabled`.
   - `c8`, `knowledge capture` into a hand-set `knowledge.db: team notes`: the store and the archive written, the note on stderr, exit 0.
   - `c6`, `embed ingest` in a config-less home: `apogee embed: not registered in config -- …/config.json: cannot open config file (run 'apogee config init' to create one)`.
   - `c4b`, `graph build` with a source tree gone: `apogee graph: warning: source tree '…/src2' is not a directory`, then a `[graph] parsing …` line.
   - `c2`, `knowledge capture` in a suite with `--validate extraction=on`: `apogee knowledge: validated by utility (checker): the record agrees with its source`. A good-news note, in the same voice.
2. **Each done path keeps one stdout line and drops the rest**:
   - `CollectionsViewModel.swift:225`, `:233`: `notices.first ?? "Ingested"`;
   - `RecordsViewModel.swift:351`, `:354`: `notices.first ?? "Done"`;
   - `GraphViewModel.swift:285`, `:291`: `notices.last ?? "Built"`.

   Each sets its run to `nil` afterwards.
3. **The tail loses a head note.** `c5`, `embed ingest 'fresh one' <150 files> --graph`, exits 0 with 9622 bytes of stderr in 153 lines, with notes at lines 1, 152 and 153. The 8 KiB tail starts mid-line (`xt (file 21/150, chunk 21/150)`), so the first note is not in it. Reading only the tail at the end would miss it. CLI-M46 moves graph build's note to before extraction, which is the head.
4. **What counts as a note: the house voice.** A note is a line beginning `apogee`, then one or more lowercase words, then `: `.
   - **Excluded**: the verb-less `apogee: … is in the older YAML format and still loads -- 'apogee config migrate' converts it to config.json` (`c3`). It rides every run in a YAML home, and Settings › Health already shows it as `check`'s Config row (`check.cpp:434-438`).
   - **Excluded**: `[graph] …` narration, which is progress.
   - **Precedents**: the app already decides on the house prefix at `RuntimeInstall.swift:79` (`lines.last(where: { $0.hasPrefix("apogee ") })`) and `RecordsViewModel.swift:155`.
   - **Contract**: the prefix is a string literal in the CLI (`embed.cpp:454`, `:467`; `graph.cpp:917`, `:988`, `:1000`; `knowledge.cpp:181`, `:556`), partly pinned by CLI tests (`suites_e2e.sh:568`), and documented nowhere.
   - **Not word-keyed**: the same `graph.enabled` note reads differently in two homes, so the rule keys on the shape, not the words.
5. **A failure is unchanged.** `c6b`, `embed ingest notes … --graph` in a config-less home, exits 1, with a note line before its last line, `apogee embed: --graph: the config cannot be read: …`. The alert shows the last line (`JobProgress.swift:141`), and CLI-M46's refusals rely on that.
6. **The tone is neutral.** The same voice carries bad news (`could not register`), plain notes, and good news (`validated by …`, `apogee train: note: …`). Models' `DeleteNarrationCard` (`DeleteNarrationCard.swift:10-35`) already draws CLI lines in a neutral card (secondary text on the code background, with Dismiss). Promoting it to `styles`, rather than copying it, is ADR 0002's rule.
7. **The toast stays the outcome.** The providers-register precedent (MILESTONES): "Its first line is the toast, and a card keeps every line". The shell's rule (`ShellViewModel.swift:87-88`) is that a toast is "never the only place something is said: the screen that did it shows it too". So a toast suffix with no card is not an option.
8. **Where it lands.** There are 19 `startJob(` call sites. The collection-name class lands in Collections (ingest), Records (capture and reindex) and Graph (build, update, communities and dedupe).
   - Training, Settings' fix, Sessions' compact, the Downloads card and the console's tail have done paths of their own. Each would get the notes for free from the core, and is left to a follow-up.
   - Compose's edit preview (`ComposeViewModel.swift:470`) is decided by its document, and is unaffected.
9. **The design draws first.** In the prototype, Collections' ingest and Records' capture are still old-toast stubs (`CollectionsViewModel.ts:154-157`, `RecordsViewModel.ts:175-176`), while Graph already runs supervised jobs (`GraphViewModel.ts:440-451`). The prototype's jobs core mirrors the app's (`JobProgress.ts:101-156`, `SupervisedJob.ts:12-36`), and its stand-in already plays a recorded `.err` (`StandInChild.ts:10-11`). Design CLAUDE.md: an item in another queue that draws design draws it there (precedents M11, M12). The contract board already rates a stderr line in the CLI's words as ready (`contract.ts:152`, `:161`).
10. **Nothing breaks.** No test ends a successful job with an `apogee` stderr line. The 64 `JobEnd(` call sites keep compiling with a defaulted parameter. `JobUpdate` is unchanged, so the console's exhaustive switch (`ConsoleViewModel.swift:357-363`) is untouched.
    - A default value that reads another parameter does not compile, so `JobEnd`'s notes default is resolved in the body (typechecked on a scratch file).
    - Every child (sessions, reads, commands, jobs, version probes) comes through `ProcessSpawner`, so the line framing is opt-in, asked for by the jobs core alone.
11. **A remainder on stdout.** Some text-mode verbs print warnings on stdout after the first line, and the one-line done paths drop them the same way:
    - `graph update`'s `[graph] source tree … is not a directory`, which is the app's Rebuild (`c4`, stderr empty);
    - `[graph] entity vectors skipped … full-text only`;
    - ingest's `binding.warning` and `decision.note` (`embed.cpp:418-423`).

    No shape marks them, so this item's rule can't catch them. They are named in Out of scope.

## Acceptance criteria

- [ ] **Given** a supervised job that exits 0 with `apogee <verb>: …` lines on stderr, **then** `JobProgress.notes` holds those lines word for word and in order (the first 20, with a count of the rest). It holds no `apogee: ` line and no `[tag] …` line.
- [ ] **Given** a run whose stderr exceeds 8 KiB with a note on its first line (`c5`), **then** all of its notes are kept, the first one included.
- [ ] **Given** an ingest that exits 0 with a note (`c1`, `c6`), **then** the toast is the outcome line as today, and the collection's page shows a card with the note word for word. The card clears on Dismiss, the next ingest, another collection chosen, or a refresh that drops the selection. A run with no note (`c1b`) shows no card.
- [ ] **Given** a capture or reindex with a note (`c2`, `c8`), **then** Records shows the same card, and the toast is unchanged.
- [ ] **Given** a graph build, update, communities or dedupe with notes (`c7`, `c4b`), **then** Graph shows the card apart from its `dropped` toast. It clears on Dismiss, the next run, or another graph chosen, never on a node's selection.
- [ ] **Given** a run that exits non-zero (`c6b`), **then** the alert shows stderr's last line as today, and no card is drawn.
- [ ] **Given** any child that is not a supervised job (a session, a read, a command, a version probe), **then** its stderr handling is unchanged: the 8 KiB tail, the busy line and the failure line.
- [ ] **Given** the design prototype, **then** it draws Collections, Records and Graph done with a note, from the same recordings, before the app ships the state. Its contract board marks the KNOWLEDGE and CODE GRAPH rows ready.

## Constraints

- **The app says what the binary says** ([GUI SPEC](../../../../assistant/gui/SPEC.md)): each note is shown as printed, never reworded or summarised.
- **Infrastructure learns no CLI convention.** Line framing of stderr is opt-in (`ChildCommand.framesErrors`), and the note rule lives in BusinessJobs. Every child that doesn't ask behaves byte for byte as today, and each framed line is capped.
- **A failure is unchanged**: the alert shows stderr's last line, and notes are kept only on `.done`.
- **The toast is never the only place something is said** (`ShellViewModel.swift:87-88`). Notes go on the place's card, and the toast stays the outcome line.
- **Promote, never copy** ([ADR 0002](../../../../adrs/darwin/presentation-mvvm.md)): Models' `DeleteNarrationCard` moves to `styles/Components` as the shared said-card, in the app and the prototype.
- **New states are drawn in the design first** ([design CLAUDE.md](../../../../assistant/design/CLAUDE.md)), each with a named preview in the app ([ADR 0004](../../../../adrs/darwin/module-schemes-and-previews.md)).
- **Hermetic tests in Swift Testing**, with Swift 6 strict concurrency, no warnings, and a doc comment on every public declaration.

## Technical approach

Paths under `lib/src/darwin/source/` unless written otherwise.

| File | Change |
|---|---|
| `infrastructure/process/ChildCommand.swift` | `framesErrors: Bool` (default false): ask for stderr as lines as well as the tail. |
| `infrastructure/process/RunningChild.swift` | `var errorLines: AsyncStream<String>? { get }`, with a protocol-extension default of nil, so the test children are untouched. The doc at `:47-48` is revised. |
| `infrastructure/process/ProcessSpawner.swift`, `LineFramer.swift` | When asked, a second `LineFramer`, capped per line (`maxLine`), beside the unchanged tail append. It yields each line, and in `closeErrors()` says the last unended line and finishes the stream before the exit's waiters resume, the failsafe path included. |
| `infrastructure/process/BoundedTail.swift` | `cut: Bool`, set when bytes were dropped. |
| `business/jobs/JobNotes.swift` (new) | `JobNotes.isNote(_:)`: `apogee`, one or more lowercase words, then `: `; never `apogee: `, never `[tag] …`. The value is `JobNotes { lines (the first 20, in order); more }`, with `init(in tail: BoundedTail)` skipping a cut first line. The doc names the house-voice precedents. |
| `business/jobs/SupervisedJob.swift` | `start` sets `framesErrors`. A task collects notes from `child.errorLines`, falling back to the tail when that is nil, and `pump` awaits it before `.ended`. `JobEnd` gains `notes`, through `init(…, notes: JobNotes? = nil)` resolved in the body as `notes ?? JobNotes(in: stderrTail)`. |
| `business/jobs/JobProgress.swift` | `public private(set) var notes = JobNotes()`, set from `end.notes` only when the status becomes `.done`. The failure words and stops are unchanged. |
| `presentation/models/Views/DeleteNarrationCard.swift` → `presentation/styles/Components/` | Promoted as the said-card: a title, every line as printed (neutral, code font, secondary text), and Dismiss, with a styles preview. Models draws it unchanged. |
| `presentation/knowledge/ViewModels/CollectionsViewModel.swift`, `RecordsViewModel.swift`, `Views/KnowledgeScreen.swift` (+ Previews) | `doneNotes` kept on `.done` (`:214-239`; Records `:342-360`), cleared on Dismiss, the next run, `select(_:)` of another collection, or a refresh that drops the selection. The card is drawn under the collection's page and on Records, with named previews "Collections after an ingest that left a note" and "Records after a capture that left a note". |
| `presentation/graph/ViewModels/GraphViewModel.swift`, `Views/GraphScreen.swift` (+ Previews) | `doneNotes` on `.done` (`:276-298`), the dedupe path included, apart from the `dropped` toast. It is cleared on Dismiss, the next run, `choose(_:)` (`:175-182`) or `reread()` falling to another graph, never on a node's `select(_:)` (`:208`). Named preview: "a rebuild that left a note". |
| `lib/design/prototype/source/…` | Promote the said-card (`DeleteNarrationCard.tsx` → `styles/Components`). Mirror `JobNotes`, `JobEnd.notes` and `JobProgress.notes` in `business/jobs`. Port Knowledge's ingest and Records' capture from toast stubs to supervised runs over the stand-in. Draw the three done-with-a-note states as presets (`knowledge:ingested-with-a-note`, `knowledge:captured-with-a-note`, `graph:built-with-a-note`) reading the new recordings, with gallery entries. Mark the KNOWLEDGE and CODE GRAPH contract rows ready. `npm run check` and `npm run build`; `lib/design/main.html` rebuilt. |
| `../tests/business/jobs/Fixtures/` (new), `JobNotesTests.swift` (new), `JobsTests.swift`; `../tests/infrastructure/process/…`; `../tests/presentation/{knowledge,graph}/…` | The fixtures under Testing. |

**Reference.** Models' delete narration card and the providers-register precedent ("its first line is the toast, and a card keeps every line"). `RuntimeInstall.swift:79`, the house prefix already read. `JobsTests.swift:21`, `:102`, the precedent for a real child in a test.

**Rejected:**
- **Reading only the tail at the end**: it loses head notes (Evidence 3), and a bigger tail only moves the limit.
- **Diffing the 250 ms busy sample**: it depends on timing, and decides on a display-only line.
- **`knowledge capture --json`**: the validation becomes data the app would have to word itself, and every job should keep one route.
- **A new machine face in the CLI**: a cross-queue change for facts the house line already carries.

**Docs when it ships:**
- MILESTONES: dated revisions of 1b ("never shown") and 1i (the busy line), and of the console's "reads stderr as a bounded tail"; and a decision row recording the reliance on the house prefix, with its precedents.
- The doc comments at `RunningChild.swift:47-48`, `BoundedTail.swift:5-7` and `ProcessSpawner.swift:4-5`.
- darwin `CLAUDE.md` (its stderr lines and the Codebase Map), the infrastructure and business layer cards, and `DEVELOPER.md`.

## Testing

All hermetic: recorded output, scripted jobs, one real `/bin/sh` child, no window, no apogee binary.

- **The bug as a fixture: recordings.** From the built CLI in sandbox homes, with paths written `/Users/someone`, committed to `tests/business/jobs/Fixtures/` as `.out`, `.err` and `.exit`:
  - notes that survive CLI-M46: `embed-ingest-no-config` (`c6`), `graph-build-source-gone` (`c4b`), `knowledge-capture-validated` (`c2`);
  - today's collection-name class: `embed-ingest-unregistrable` (`c1`), `graph-build-unregistrable` (`c7`), `knowledge-capture-unregistrable` (`c8`);
  - the long run `embed-ingest-graph-many` (`c5`), committed as recorded: 9622 bytes, with notes at lines 1, 152 and 153;
  - guards with no notes: `embed-ingest-registered` (`c1b`), `embed-ingest-legacy-yaml` (`c3`, the verb-less line excluded), and `graph-update-source-gone` (`c4`, the stdout remainder, named so a follow-up flips it);
  - a failure: `embed-ingest-no-config-graph` (`c6b`).
- **`JobNotesTests`**:
  - the rule table: the house lines are notes, a multi-word verb (`apogee mcp serve: `) included; `apogee: …` and `[graph] …` are not;
  - `JobNotes(in:)` over each `.err`;
  - over `c5`'s tail, the cut first line is skipped, giving 2 notes, not a fragment.
- **The fold**: each `.out` line in as `.said`, then `.ended(exit 0, tail(.err))`.
  - `c1` gives `.done`, `notices == ["1 file(s), 1 chunk(s)"]`, and `notes.lines ==` its note.
  - The guards give no notes.
  - `c6b` gives `.failed(<last line>)` with no notes, and a stop gives none.
- **A real child through `JobStarter(spawner: ProcessSpawner(), folder:)`.** A `/bin/sh` script cats `c5`'s `.err` to stderr and exits 0. `end.notes.lines` holds all 3 in order, while `end.stderrTail.text` lacks the first and `end.stderrTail.cut` is true.
- **`ProcessSpawnerTests`, `LineFramerTests`, `BoundedTailTests`**:
  - not asked, `errorLines` is nil;
  - asked, lines are whole across chunks, `\r\n` is trimmed, the stream finishes at exit and at the failsafe, and the tail is unchanged;
  - the line cap holds, and `cut` is set.
- **View-model tests over a `ScriptedJob` replaying a recording**:
  - **Collections**: `c1` and `c6` toast `1 file(s), 1 chunk(s)` and keep the note word for word; `c1b` leaves none. The next `ingest()`, `select(_:)` of another collection and Dismiss each clear it. `c6b` sets the failure with no card.
  - **Records**: `c2` and `c8` toast `Captured kr-…`, and the card holds the line.
  - **Graph**: `c7` and `c4b` toast `notices.last`, and the card is kept apart from `dropped`. `choose(_:)` clears it; a node's `select(_:)` does not. Graph's `end(status:)` in `Scripted.swift` gains `stderr:`, as Knowledge's has.
- **The prototype**: its `npm run check` covers the mirrored `JobNotes` and the presets.
- **Gate:** `lib/scripts/cicd.sh --app darwin --test`, run alone, and the design's `npm run check` and `npm run build`.

## Open questions

| Question | Status | Answer |
|---|---|---|
| What counts as a note? | `[default]` | A stderr line on a run that exited 0, beginning `apogee`, then one or more lowercase words, then `: `, kept word for word and in order: the first 20, plus a count of the rest. Never the verb-less `apogee: ` line (Settings › Health shows it), never `[tag] …` narration. The reliance on this unpinned prefix is recorded as a dated decision, with the app's precedents. |
| Where are notes shown, in what tone, and what does the toast say? | `[default]` | On a card on the place's page: the promoted narration card, neutral, never a warning colour, since the same voice carries good news. The toast stays the outcome line. The card belongs to its collection or graph. The look is drawn in the design first, for the user to see. |
| Read stderr as lines in Infrastructure, or only the tail at the end? | `[default]` | As lines, opt-in for supervised jobs only, each line capped. The tail measurably loses a head note. The 8 KiB tail and the busy sampler stay. |
| Should a failed run also show the notes said before its failure line? | `[default]` | No. The failure path is unchanged, and CLI-M46's refusals rely on its last line. |
| Which screens get the card? | `[default]` | Collections, Records and Graph, where the collection-name class lands. Training, Settings, Sessions, Downloads and the console are a follow-up, since a toast suffix without a card would break the shell's rule. |
| Should this also cover warnings printed on stdout? | `[default]` | No. No shape marks them, so this rule can't catch them. The follow-up chose the CLI side (2026-11-26): [CLI-M58](../../../cli/maintenance/a-runs-notes-are-said-once-on-stderr.md) says them in the house voice on stderr, so this rule keeps them with no app change, and it waits on this item so no note the app shows today goes unshown; [CLI-M59](../../../cli/maintenance/the-http-graph-jobs-say-what-the-cli-says.md) and [CLI-M60](../../../cli/maintenance/the-models-verbs-say-their-notes-on-stderr.md) do the same for the HTTP graph jobs and the models verbs. The `graph-update-source-gone` guard names it; re-recording it as a note once CLI-M58 ships is a chore for this queue. |
| Does it wait on CLI-M45 or CLI-M46? | `[default]` | No: the rule keys on the shape, not the words. CLI-M46 now says its notes keep the house voice on stderr, its graph build's before-extraction note included. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-26 | Specced from CLI-M46's Out of scope, a sandboxed investigation, two skeptics and a critic (Evidence). Placed in **Maintenance** as M22, its own item beside APP-M21 (different seam: the jobs core, not argv). Both edit `CollectionsViewModel.ingest()`, so they are built one after the other, with no gate. | the spec |
| 2026-11-26 | **The design draws it here**, as M11 and M12 did: no design-queue sibling. | the spec |
| 2026-11-26 | **Warnings on stdout go to the CLI** (CLI-M58, with M59 and M60): the CLI says them in the house voice on stderr, so this item's rule keeps them with no app change. **CLI-M58 waits on this item**: two of the notes it moves (knowledge reindex's note, and a nothing-to-extract rebuild's embedding warning) are the app's toast today, so moving them first would hide them until this item shows stderr notes. | the spec |

## Out of scope

- **Every other done path** (Training, Settings' fix, Sessions' compact, the Downloads card, the console's tail) and a source check that every done path reads notes. They are a follow-up, "every done path shows its notes".
- **Warnings on stdout** (Evidence 11): the CLI's [CLI-M58](../../../cli/maintenance/a-runs-notes-are-said-once-on-stderr.md), gated on this item, with [CLI-M59](../../../cli/maintenance/the-http-graph-jobs-say-what-the-cli-says.md) and [CLI-M60](../../../cli/maintenance/the-models-verbs-say-their-notes-on-stderr.md). Re-recording the `graph-update-source-gone` guard as a note once CLI-M58 ships is a chore for this queue.
- **A remote job's notes** (APP-7c over CLI-59d): 7c fills `JobEnd.notes` from a server job's warnings.
- **Names passed as flags**: [APP-M21](a-listed-name-reaches-the-verb-as-listed.md) and [APP-M23](every-name-the-app-passes-is-read-as-a-name.md).

## Links

- **Blocked by:** — · **Blocks:** [CLI-M58](../../../cli/maintenance/a-runs-notes-are-said-once-on-stderr.md), whose notes move to stderr only once this item shows them
- **Relates to:**
  - [APP-M21](a-listed-name-reaches-the-verb-as-listed.md), which also edits `ingest()`;
  - [CLI-M46](../../../cli/maintenance/a-collection-the-config-cannot-hold-is-refused-before-it-is-written.md), whose notes this item shows and whose refusals the failure path already shows;
  - [APP-7c](../../v0.2.4/every-place-against-a-server.md), a remote job's notes;
  - [CLI-M59](../../../cli/maintenance/the-http-graph-jobs-say-what-the-cli-says.md) and [CLI-M60](../../../cli/maintenance/the-models-verbs-say-their-notes-on-stderr.md), the HTTP graph jobs' and the models verbs' notes;
  - 1b and 1i, the jobs core's records; the providers-register precedent; [ADR 0002](../../../../adrs/darwin/presentation-mvvm.md); [ADR 0004](../../../../adrs/darwin/module-schemes-and-previews.md).
