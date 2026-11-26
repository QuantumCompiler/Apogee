# APP-M21 · A listed name reaches the verb as listed

| Field | Value |
|---|---|
| **Type** | 🐞 Bug |
| **Project** | Apple apps · macOS |
| **Status** | 🟢 Ready |
| **Fix version** | Maintenance (any time) |
| **Component** | `Argv` (BusinessReads), and the Collections, Graph, Training datasets and Models rows that act on a listed name |
| **Labels** | `argv` `cli-parity` `silent-failure` |
| **Epic** | — |
| **Blocked by** | — |
| **Blocks** | [M23](every-name-the-app-passes-is-read-as-a-name.md) |
| **Reporter** | The CLI-M45/M46 investigation (2026-11-22), then a sandboxed investigation, two skeptics and a critic (2026-11-26) |
| **Created** | 2026-11-26 |

---

## Summary

The app lists what the CLI lists, a collection named `-x` or a dataset named `-h` among them, and then acts on the name by passing it before any `--`. The CLI's parser reads such a name as a flag before the verb runs. So a `-x` collection's page never loads, and the person reads only "Run with --help for more information.". Deleting a dataset named `-h` prints the verb's help, which the app toasts as success while the dataset stays. Ingest more… into a store named ` sp` trims the name, then writes and registers a different store `sp`.

The item gives `Argv` a line builder (the command, then its options, then `--` and the names). It moves the rows a person can reach today onto it, and passes a listed name byte for byte.

## Description

### User story

**As** someone whose collections, datasets or backends include a name the CLI made or an older config kept,
**I want** every act the app offers on a listed name to reach the verb with that exact name,
**so that** the page loads, Delete deletes, Ingest more… adds to the store I chose, and a refusal is in the CLI's own words.

### Steps to reproduce

The sandbox, 2026-11-26, with the built CLI `apogee 0.2.0 (33ea685d, 2026-11-22, macos-arm64)`: a throwaway `APOGEE_HOME` and `HOME`, `env -i`, stdin from `/dev/null`, and a mock backend. The app was not launched. Each step runs the exact argv the app's code builds, read at the line cited.

1. Make the stores the CLI itself writes today: `embed ingest -q --retriever lexical -- -x docs/a.txt`, and the same for `-h`, `--graph`, ` sp` and `my notes`. `embed list --output-format json` lists all of them. It is the document `CollectionsViewModel.refresh()` (`CollectionsViewModel.swift:90`) turns into the Collections list.
2. Select `-x`. The app reads `embed info -x --output-format json` (`:115`).
3. Make a dataset `-h` (`datasets create --from empty -- -h`), and run the app's Delete…: `datasets delete -h --yes` (`TrainingViewModel.swift:257`).
4. Select ` sp` and use Ingest more…, which runs `embed ingest sp <path> --retriever lexical` (`CollectionsViewModel.swift:28-36`, the name trimmed at `:30`).

### Actual result

1. The list holds `-x`, `-h`, `--graph`, ` sp` and `my notes`.
2. Exit 106, stderr `collection is required` then `Run with --help for more information.`. The app's alert says `Run with --help for more information. (exited with status 106)`, and the page never draws: every act on the collection sits inside it (`KnowledgeScreen.swift:317-318`). For `-h`, the read prints help with exit 0, and the alert says `apogee printed something that is not one JSON document`.
3. Exit 0 with the verb's help. The toast is the help's last line, `-y,--yes                    Do not ask for confirmation`, and `-h.jsonl` is still there.
4. Exit 0, `registered 'sp' in …/config.json`. `embeddings/` now holds ` sp.db` and `sp.db`, and the app selects `sp`.

### Expected result

1. The same.
2. `embed info --output-format json -- -x` exits 0, and the page draws.
3. `datasets delete --yes -- -h` exits 0 with `removed.`, and the dataset is gone.
4. `embed ingest --retriever lexical -- ' sp' <path>` adds to ` sp`, and the app selects ` sp`.

### Background

Every apogee verb but `__complete` is parsed by CLI11 (v2.4.2 since v0.1.0). It classifies a word as an option before the verb's callback runs when the word is shaped `--word`, or `-` followed by a non-digit (`App_inl.hpp:1039-1057`). A positional it should have filled is then "required" (exit 106), or a later word shifts into its place (exit 109). A `-h` or `--help` prints the verb's help with exit 0. After `--`, every word is a positional.

GUI 4g (c51bbd86, 2026-10-25) added `Argv` (`source/business/reads/Argv.swift:14-38`) for a person's free text: the text goes after `--`, every option before it. Its record left "typed names in create, rename and targets, which the CLI validates" as they were (MILESTONES, 4g). That assumption fails twice:
- most names are not typed: the app reads them back from the CLI's own listings (`embed list`, `datasets list`, `models list`, `config get`, completion);
- the CLI's validation never runs for a flag-shaped name, because the parser refuses first.

Such names exist, from three sources:
- the CLI writes stores and datasets with them after `--` today (stores until CLI-M46's create rule; datasets have no name rule);
- configs written before 55f (2026-10-25) keep theirs;
- hand-edited JSONC does too.

The app-wide move of every other site onto the builder is APP-M23, gated on this one. This item takes the rows where a person meets harm today, and the seam they need.

### Evidence

All measured 2026-11-26 by an investigator, two skeptics and a critic, each in its own sandbox, with the built CLI. The app was read, never launched or built.

1. **Which names break.** Only words CLI11 classifies as options: `-x`, `-h`, `--graph`, `--help` and their kind.
   - A bare `-` and a negative-number shape like `-1` work in the app's shape today.
   - So do names that spell a verb (`list`, `chat`, `info`), since every command group calls `require_subcommand(1)`.
2. **Collections: the page never loads.** In the app's shape, `embed info -x --output-format json` exits 106, and `embed info -h …` prints help with exit 0. With the name after `--`, all four names exit 0 with their document. The page's acts are drawn only `if let info = model.info` (`KnowledgeScreen.swift:317-318`), so search, Ingest more…, Remove… and Delete… are never reached for these names.
3. **Collections: Ingest more… writes a different store.**
   - A store ` sp` loads, since `embed info ' sp'` works.
   - Ingest more… hands ` sp` to the form, which trims it (`CollectionsViewModel.swift:30`, and again at `:217`).
   - The app runs `embed ingest sp …`, which exits 0 with `registered 'sp' in …`, and `embed list` then lists both ` sp` and `sp`.
4. **The Collections argv today** (`CollectionsViewModel.swift`):
   - `IngestForm.arguments` (`:28-36`): `["embed","ingest",collection,path]`, with `--retriever` and `--graph` appended after;
   - `PendingDelete.arguments` (`:45-50`): `["embed","delete",collection,"--yes"]`, or `…,"--source",source]`;
   - `readInfo` (`:115`): `["embed","info",name]`;
   - `search` (`:167`): `["embed","query",selected] + Argv.text(question)`, so 4g moved the question after `--` and left the collection before it.

   Each has had this shape since GUI 2c (ee4ad968, 2026-10-20). The fixed shapes were probed: `embed ingest --retriever lexical -- -x /abs/b.txt`, `embed delete --yes -- -x`, `embed delete --source b.txt -- -x`, `embed info --output-format json -- -x` and `embed query -- -x fox` each exit 0. `embed delete -- -x --yes` exits 109: after `--`, the flag is a positional.
5. **Graph hides the names.** The list comes from `__complete graph stats ""` (`GraphViewModel.swift:144-148`). It trims each line and drops every name starting with `-`, so `-`, `--graph`, `--help`, `-1`, `-h` and `-x` never appear, and ` sp` becomes `sp`.
   - When only ` sp` exists, `graph stats sp` answers `no graph or collection named`.
   - When both exist, `sp` is listed twice (by reading).
   - The filter is unchanged since 517affc5 (2026-10-20), and nothing records why.
   - `__complete graph stats -- ''` returns the same 13 lines today, and `graph stats --output-format json -- -x` reads the graph.
   - The graph sites (`:156`, `:258`, `:265`, `:336`, `:360`, `:392`) never receive a dash name today; dropping the filter exposes them.
6. **Training: a false success.** Deleting a dataset `-h` toasts the help's last line and leaves the dataset (Steps 3; `TrainingViewModel.swift:257`, `:1318-1319`, `:1330-1340`). Its Delete… is not gated on a read. `datasets delete -x --yes` exits 106 (`name is required`). `datasets delete --yes -- -h` removes it.
7. **Models: the CLI's own way out fails.** For an odd backend, the CLI's edits refuse and point to `config rename-backend`, which the app offers on every row (`ModelsScreen.swift:327`) as `config rename-backend <backend> <new>` (`ModelsViewModel.swift:68-70`). For `-x` that exits 106. `config rename-backend -- -x fixed` reaches the verb. `models info`, the role verbs (`ModelsViewModel.swift:243`, `:513`, `:532`, `:539`, `:589`; `Models.swift:373-375`) and the adapters edit (`BackendAdapters.swift:83`) share the shape.
8. **An option that eats `--`.** CLI11 lets an option that collects several values take a `--` directly after it as its own end (`App_inl.hpp:1975-1999`). Probed:
   - `graph build --source <dir> -- -x` exits 109;
   - `models convert --register-with Q4_K_M -- -x` exits 106;
   - `review targets add --repo o/r --skills security -- -x` exits 106, while `--skills security --repo o/r -- -x` reaches the verb;
   - `agents create --mcp srv -- -x` reaches the verb, because `--mcp` sets `allow_extra_args(false)`. So the help's ` ...` is not the signal.

   The builder must never put such an option directly before `--`. In this item that means New graph, which becomes `graph build --source <folder> --graph <name>` (`--graph` is "the same as NAME", since v0.1.4).
9. **Readers that the new order breaks.** `AdaptersSheet.swift:47` draws `form.arguments.dropLast() + [value]`, and would drop the backend once `--` moves. `StoreJobsTests.pinned` (`StoreJobsTests.swift:27-31`) checks every dash word against the help, and must read `Argv.options(of:)`.
10. **Recorded verdicts.** `store-command-verdicts.txt` (163 lines), `store-route-verdicts.txt` (12) and `drafter-pairing-verdicts.txt` (7) are keyed by the exact line. Those this item's Models rows change are recorded again from the built CLI in a sandbox.
    - `refresh-store-commands-fixture.sh:29` defaults to `$HOME/.local/bin/apogee`, the production install. It is run with the built binary as its argument.
    - The drafter verdicts have no refresh script (they were recorded by hand in 3b).
11. **Severity.** No data is lost to a parse refusal. The real wrong effects are the wrong-target ingest, the dataset's false success, and a typed `-h` in a create form toasted as success with nothing made. Flag-shaped names are rare: they come from CLI use with `--`, hand-edited configs, or configs from before 55f.
12. **What already works**, and must stay:
    - names that are not flag-shaped (`my notes`, `a@b`, `café`);
    - search's question after `--` (4g);
    - CLI-minted ids, which never begin with `-` (chat ids are time-ordered UTC, task ids `task-…`, record ids `kr-…`);
    - `__complete`, which takes its words verbatim (`prefix_command`, `complete_protocol.cpp:658`);
    - every shipped binary, which reads `--` the same way, so no CLI change and no version skew.
13. **Not pinned.** The knowledge tests script `embed|info|notes` by prefix (`KnowledgeViewModelTests.swift:133`), which matches today's shape whatever the name. No test lists a dash name.

## Acceptance criteria

- [ ] **Given** `embed list` lists `-x`, `-h` and `--graph`, **when** each is selected, **then** the app reads `embed info --output-format json -- <name>`, and the page draws.
- [ ] **Given** a listed ` sp`, **when** Ingest more… runs, **then** the line is `embed ingest [options] -- ' sp' <path>`, the name passed byte for byte, and the app selects ` sp`. A name the person types is still trimmed.
- [ ] **Given** a dash-named collection's page, **when** search, Ingest more…, Remove… and Delete… run, **then** each line ends its options with `--` before the collection: `embed query -- -x <question>`, `embed delete --yes -- -x`, `embed delete --source <s> -- -x`.
- [ ] **Given** completion lists `-x`, `--graph` and ` sp` for graphs, **when** Graph loads, **then** its list holds them byte for byte, and stats, update, build, communities, both dedupes and delete each put the graph after `--`. New graph runs `graph build --source <folder> --graph <name>`.
- [ ] **Given** a listed dataset `-h`, **when** Delete… is confirmed, **then** `datasets delete --yes -- -h` runs, the dataset is gone, and the toast is the CLI's `removed.`.
- [ ] **Given** a backend row named `-x`, **when** Rename the backend… runs, **then** `config rename-backend -- -x <new>` runs. `models info`, the role verbs and the adapters edit put the backend after `--`, and the adapters sheet's line shows the backend once.
- [ ] **Given** a name the CLI refuses after `--` (55f's rule for a config entry, CLI-M46's create rule once it ships), **then** the person sees the CLI's own words, never `Run with --help for more information.`.
- [ ] **Given** a name that is not flag-shaped, or a CLI-minted id, **then** every row behaves as today.

## Constraints

- **The CLI owns every name rule** ([darwin CLAUDE.md](../../../../assistant/darwin/CLAUDE.md), the machine-mode-client rule): the app adds none. It passes names as names, and the CLI's refusal is what the person reads ("the app says what the binary says", [GUI SPEC](../../../../assistant/gui/SPEC.md)).
- **Full parity** ([GUI SPEC](../../../../assistant/gui/SPEC.md)): what works at the prompt with `--` works in the app.
- **One seam.** Names go through `Argv`'s builder, never hand-ordered arrays. `Argv.text`, `adding(_:to:)` and `options(of:)` stay, and a later option is added through `adding`, never `+`.
- **An option that takes `--` is never directly before it.** The builder holds that set from the binary's recorded verdicts, not from the help's ` ...`.
- **`__complete` asks are exempt.** The protocol reads its words verbatim, and the word being completed comes last.
- **The view-models stay free of SwiftUI** ([ADR 0002](../../../../adrs/darwin/presentation-mvvm.md)), and every view keeps its named previews ([ADR 0004](../../../../adrs/darwin/module-schemes-and-previews.md)).
- **Hermetic tests in Swift Testing**, with Swift 6, no warnings, and a doc comment on every public declaration ([darwin CLAUDE.md → Code Style](../../../../assistant/darwin/CLAUDE.md#code-style)).

## Technical approach

Paths under `lib/src/darwin/source/`.

| File | Change |
|---|---|
| `business/reads/Argv.swift` | **A line builder.** `Argv.line(_ command: [String], options: [Argv.Option], names: [String])` returns the command, then the options, then, if there are names, `--` and the names, so a line holds one `--`. Options are typed (`.flag("--yes")`, `.value("--retriever", r)`). The builder never leaves an option from the recorded set that takes `--` directly before it, moving it earlier. `positionals(of:)` returns the words after `--`. `shown(_:)` renders the line a person types: options, then names, with `--` printed only when a name after it begins with `-`. `text`, `adding` and `options(of:)` stay. |
| `presentation/knowledge/ViewModels/CollectionsViewModel.swift` | `IngestForm.arguments` (`:28-36`), `PendingDelete.arguments` (`:45-50`), `readInfo` (`:115`) and `search` (`:167`) use the builder. A name handed in by Ingest more… is kept byte for byte, and only typed text is trimmed (`:30`, `:202-230`). `ingest()` selects the name it passed. |
| `presentation/knowledge/Views/KnowledgeScreen.swift` | The delete dialogs' lines (`:155`, `:336`) are built with `Argv.shown` from the argv; `:155` is hand-typed today. |
| `presentation/graph/ViewModels/GraphViewModel.swift` | The list asks `__complete graph stats -- ""`, splits on the tab, and drops the trim (`:147`) and the dash filter (`:148`). In the same change, stats, update, build, communities, both dedupes and delete (`:156`, `:258`, `:265`, `:336`, `:360`, `:392`) put the graph after `--`. New graph (`:269-273`) runs `graph build --source <folder> --graph <name>`. |
| `presentation/training/ViewModels/TrainingViewModel.swift`, `Views/TrainingScreen.swift` | `datasets delete` (`:257`), `datasets create` and `datasets synth` (`:218`, `:1293`; the sheet's line at `TrainingScreen.swift:613` via `Argv.shown`) use the builder. The pipeline completion fallback (`:538-542`) drops its dash filter and trim. |
| `presentation/models/ViewModels/ModelsViewModel.swift`, `BackendAdapters.swift`, `Views/AdaptersSheet.swift`, `business/reads/Models.swift` | `config rename-backend` and `models rename` (`:68-70`, `:575`), `models info`, delete, `config delete-backend`, `set-backend` and the role verbs (`:243`, `:276`, `:513`, `:532`, `:539`, `:589`, `:800`; `Models.swift:373-375`) use the builder. The role verb gets its backend through it, not `+ [backend]`. The adapters edit (`BackendAdapters.swift:83`) becomes `--adapters v -- <backend>`, and `AdaptersSheet.swift:47` builds its line from the argv. |
| `../tests/business/reads/ArgvTests.swift`, `../tests/presentation/{knowledge,graph,training,models}/…`, `../tests/business/reads/Fixtures/`, `../tests/presentation/models/Fixtures/*verdicts*.txt`, `../tests/presentation/models/StoreJobsTests.swift` | The fixtures under Testing. `StoreJobsTests.pinned` reads `Argv.options(of:)`. The verdict lines this item changes are recorded again. |

**Reference.** GUI 4g's `Argv` and its record, the rule this extends from text to names. M2's recorded store-command verdicts (StoreCommandsTests), the pattern for pinning lines against the binary's answers.

**Docs when it ships:**
- darwin `CLAUDE.md`: the Codebase Map's `Argv` row, and a Presentation rule that every positional goes after `--` through the builder.
- `DEVELOPER.md`'s BusinessReads row.
- MILESTONES: a dated entry, revising 4g's "typed names … which the CLI validates".

## Testing

All hermetic: scripted runners and jobs, recorded documents, no window, no binary.

- **Scripting the parse, so each test fails today.** A name-before-`--` prefix answers CLI11's refusal: `.failed(exit 106, stderr "collection is required\nRun with --help for more information.")`, or help on stdout for `-h`. The `--` prefix answers the recorded document. A plain prefix match on `embed|info` would load a `-x` page on today's code too.
- **Recorded documents**, from the built CLI in a sandbox, the home rewritten `/Users/someone`, in `tests/business/reads/Fixtures/`:
  - `collections-list-dashes.json` (` sp`, `-`, `--graph`, `--help`, `-1`, `-h`, `-x`, `my notes`, `notes`);
  - `collection-info-dash-x.json`, from `embed info --output-format json -- -x`;
  - `graph-stats-complete-dashes.txt`, from `__complete graph stats -- ''`.
- **Collections** (`tests/presentation/knowledge/CollectionNamesTests.swift`):
  1. Selecting `-x` reads `embed|info|--|-x`, and `info?.name == "-x"`.
  2. Selecting `-h` reads `embed|info|--|-h`, and the page loads; today it is `.undecodable`.
  3. Question `fox` runs `embed|query|--|-x|fox`, and `-how` runs `embed|query|--|-x|-how`.
  4. `newIngest(into: "-x")` with retriever lexical and graph on gives `["embed","ingest","--retriever","lexical","--graph","--","-x","/Users/someone/notes"]` (probed: exit 0).
  5. `PendingDelete("-x", nil)` gives `["embed","delete","--yes","--","-x"]`; with source `b.txt`, `["embed","delete","--source","b.txt","--","-x"]`.
  6. `newIngest(into: " sp")` keeps `" sp"`, and after the job `selected == " sp"`; today it is `sp`.
  7. A typed `"  notes  "` is still trimmed to `notes`.
- **Graph**: the list is asked as `__complete|graph|stats|--|` and holds ` sp`, `-`, `--graph`, `-h` and `-x` byte for byte. Choosing `-x` reads `graph|stats|--|-x`, and New graph runs `graph|build|--source|<dir>|--graph|<name>`.
- **Training**: `confirm(.deleteDataset("-h"))` runs `datasets|delete|--yes|--|-h`, and the toast is `removed.`, not the help's line.
- **Models**: renaming a `-x` row to `fixed` runs `config|rename-backend|--|-x|fixed`, and the adapters sheet's line holds the backend once.
- **ArgvTests**: the builder's order, one `--`, `positionals(of:)`, `shown(_:)` (no `--` unless a name starts with `-`), and an option from the eating set never directly before `--`.
- **Gate:** `lib/scripts/cicd.sh --app darwin --test`, run alone (the `eventually` deadline flakes under load).
- **A live look** is optional and the user's to take, on "Apogee Dev" (M18) over a home holding a `-x` store.

## Open questions

| Question | Status | Answer |
|---|---|---|
| Should the app validate typed names (a collection or dataset named `-h`)? | `[default]` | No. With names after `--`, the CLI either makes the thing or refuses in its own words, and the false success goes either way. The machine-mode-client rule keeps name rules in the CLI. Datasets having no name rule is the CLI's to decide. |
| Should a name handed in from a listing be trimmed? | `[default]` | No. A listed name is passed and selected byte for byte; only text the person typed is trimmed. The Graph list's trim goes too. |
| What do the dialogs show once names follow `--`? | `[default]` | The line from `Argv.shown`: options, then names, with `--` only when a name begins with `-`. So a delete reads `apogee embed delete --yes notes`, and only a dash name shows the `--`. |
| Must the Graph list's `--` ask wait for CLI-M45? | `[default]` | No. `__complete graph stats -- ''` returns the same list today. CLI-M45's completion change must keep offering dash names after `--`, which its ticket now says. Neither blocks the other. |
| For config entries the CLI also refuses after `--` (a hand-written `-x` backend's delete or edit), is the CLI's refusal enough? | `[default]` | Yes. The words name the rule and point to rename-backend, which the app then runs. The app adds no way round a CLI refusal. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-26 | Specced from CLI-M45's and CLI-M46's Out of scope, a sandboxed investigation, two skeptics and a critic (Evidence). Placed in **Maintenance** as M21. | the spec |
| 2026-11-26 | **Split three ways, not one item.** The brief asked for one item for the names and the done note. The investigation found different seams (argv in Presentation, against the jobs core), and an argv move of about 95 sites in 24 files. So: this item for the seam and the rows where a person meets harm today; APP-M22 for the done note, which draws design first; APP-M23 for every other site, gated on this one. | the spec |
| 2026-11-26 | **No CLI change.** `--` reads the same on every shipped binary (CLI11 v2.4.2 since v0.1.0), and `graph build --graph` exists since v0.1.4, so the app needs no version check. | the spec |

## Out of scope

- **Every other site** (Store, Downloads, Library, Review, Tasks, Sessions, Suites, Voice, Settings, Xcode, Records, ClipForm's `--lora`), the previews' prefixes, the layering rule and the full verdict sweep: [APP-M23](every-name-the-app-passes-is-read-as-a-name.md).
- **A job's non-fatal notes on done**: [APP-M22](a-jobs-notes-reach-the-person.md). It also edits `CollectionsViewModel.ingest()`, so the two are built one after the other.
- **The design prototype's mirrors** of the Collections argv and the ClipForm order (`CollectionsViewModel.ts`, `KnowledgeWorldRules.ts:49-56`, `ClipForm.ts:77`). No screen or state is new, so nothing is drawn first; a design follow-up may bring the mirrors up to date.
- **CLI asides the probes found**, for the CLI queue:
  - remedies that print a dash name without `--` (`graph.cpp:1535`, `:1638`; `models delete` suggests `config delete-backend -x`);
  - `agents create` leaving `prompts/-x.txt` behind on a refused name;
  - `symphonies delete -- -x` saying `not found in config` while `show` finds it;
  - datasets having no name rule.

## Links

- **Blocked by:** — · **Blocks:** [APP-M23](every-name-the-app-passes-is-read-as-a-name.md), which moves every other site onto this item's builder
- **Relates to:**
  - [APP-M22](a-jobs-notes-reach-the-person.md), the done note;
  - [CLI-M45](../../../cli/maintenance/a-collection-is-named-not-pathed-at-every-reader.md), whose completion change keeps the Graph list's `--` ask;
  - [CLI-M46](../../../cli/maintenance/a-collection-the-config-cannot-hold-is-refused-before-it-is-written.md), whose create rule's refusal reaches the person only once the name follows `--`;
  - [APP-7c](../../v0.2.4/every-place-against-a-server.md) and [APP-7d](../../v0.2.4/apogeekit-swift-package.md): land this before 7c records its argv baseline and 7d carves BusinessReads into ApogeeKit;
  - GUI 4g, `Argv`'s first rule; [ADR 0002](../../../../adrs/darwin/presentation-mvvm.md); [ADR 0004](../../../../adrs/darwin/module-schemes-and-previews.md).
