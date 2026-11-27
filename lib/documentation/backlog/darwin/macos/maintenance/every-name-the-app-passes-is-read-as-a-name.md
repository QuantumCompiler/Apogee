# APP-M23 · Every name the app passes is read as a name

| Field | Value |
|---|---|
| **Type** | 🐞 Bug |
| **Project** | Apple apps · macOS |
| **Status** | 🔒 Blocked |
| **Fix version** | Maintenance (any time) |
| **Component** | Every argv the app builds: Store, Downloads, Library, Review, Tasks, Sessions, Suites, Voice, Settings, Xcode, Records, ClipForm; the previews; the layering check |
| **Labels** | `argv` `cli-parity` `layering-check` |
| **Epic** | — |
| **Blocked by** | [M21](a-listed-name-reaches-the-verb-as-listed.md) |
| **Blocks** | — |
| **Reporter** | The CLI-M45/M46 investigation (2026-11-22), then a sandboxed investigation, two skeptics and a critic (2026-11-26) |
| **Created** | 2026-11-26 |

---

## Summary

APP-M21 gives `Argv` a line builder and moves the rows where a person meets harm today. About 95 argv sites in 24 files still pass a name, path or id as a positional before any `--`. The CLI's parser reads a flag-shaped one (`-x`, `-h`, `--graph`) as an option before the verb runs: 78 of 81 name positions probed with `-x` were refused, and every one with the name after `--` but one reached the verb. A typed `-h` in a create form prints help with exit 0, and the app toasts it as success. The item moves every remaining site onto the builder, so the rule holds for the whole app. It adds three checks that keep it: a recorded-verdict sweep, a layering rule, and a test that the previews' recorded reads still answer.

## Description

### User story

**As** someone using any place in the app,
**I want** every name, path or id the app passes to reach the verb as that word,
**so that** what works at the prompt works in the app, and a refusal is in the CLI's words, whichever place I use.

### Steps to reproduce

The sandbox, 2026-11-26, with the built CLI `apogee 0.2.0 (33ea685d, 2026-11-22, macos-arm64)`: a throwaway `APOGEE_HOME` and `HOME`, `env -i`, stdin from `/dev/null`, and mock backends. A hand-written `-x` entry sits in each config section (backends, agents, MCP servers, suites, review targets, symphonies). The app was not launched. A sweep builds the app's own shape for every verb it passes a name or path to, read at each cited line, with the name `-x`, then again with the name after `--`.

1. Run the sweep.
2. Type `-h` as the name in a create form, for example Library › New agent (`agents create -h`) or `symphonies create -h --from digest`.

### Actual result

1. 78 of the 81 name positions are refused by the parser: exit 106 (`… is required`) or 109 (`The following argument was not expected`). With the name after `--`, 74 of 75 forms reach the verb. The one that doesn't is `graph build --source F -- -x`, where the repeatable `--source` takes the `--`.
2. Exit 0 with the verb's help and nothing written. The app's done path says the first or last stdout line, so the help's line is the success toast.

### Expected result

1. Every form reaches the verb, which either acts or refuses in its own words.
2. The CLI makes the thing or refuses by its name rule, and the person reads which.

### Background

APP-M21 builds the seam, `Argv.line(_:options:names:)`, with `positionals(of:)` and `shown(_:)`, and moves Collections, Graph, Training's datasets and the Models backend rows. This item is every other site, the readers that take an argv by position, and the checks.

It also completes what GUI 4g began: 4g's record left "typed names in create, rename and targets, which the CLI validates" before `--`. For a flag-shaped name the CLI's validation never runs, because the parser refuses first, and most names are read back from a listing, not typed. The dated MILESTONES row that supersedes 4g's is this item's, once every site has moved.

### Evidence

All measured 2026-11-26 by an investigator, two skeptics and a critic, each in its own sandbox. The app was read, never launched or built.

1. **The sweep**: 78 of 81 name positions refused in the app's shape, and 74 of 75 reaching the verb after `--` (Steps). The aggregate counts were spot-checked, not recounted: about 95 name sites in 24 files, about 230 test expectations in 57 files, and over 100 preview prefixes.
2. **The remaining sites**, by place:
   - **Store and Downloads**: `StoreViewModel.swift:161`, `:209`, `:310-324`, `:475`, `:651`, `:657`, `:723-730`, `:747`, `:752`; `StoreForms.swift:214`, `:221-225`, `:304`, `:311`; `DownloadsViewModel.swift:197-210`, `:245`, `:283`, `:404`, `:451`, `:484`; `DeletePlan.swift:51`, `:60`.
   - **Library**: agents, symphonies and MCP servers (`LibraryViewModels.swift:232`, `:302`, `:489-491`, `:550`, `:579`, `:598`, `:685`, `:692`, `:709`, `:720`; `Library.swift:121`).
   - **Review**: `mr`, `targets add`, `skills show`, `targets delete` (`ReviewViewModel.swift:48-53`, `:78-82`, `:255`, `:367`).
   - **Tasks**: `status`, `recipes show`, `resume`, `halt`, `cancel` (`TasksViewModel.swift:169`, `:409`, `:435`, `:441-442`, `:499`, `:504`).
   - **Sessions**: `models info`, `chats delete`, `title`, `info`, `compact`, `show` (`SessionLibraryViewModel.swift:532`, `:1007`, `:1130`, `:1154`, `:1185`, `:1246`; `SavedChat.swift:69`).
   - **Suites and Voice**: `SuitesViewModel.swift:171`, `:321`; `VoiceViewModel.swift:117`.
   - **Settings and Xcode**: `SettingsViewModel.swift:101`, `:517`, `:524`, `:529`, `:536`, `:541-542`, `:548`, `:557`, `:648`, `:692`; `XcodeSettings.swift:218`, `:269`.
   - **Records**: `knowledge info`, `link`, `status`, `delete` (`RecordsViewModel.swift:184`, `:271`, `:277`, `:287`).
   - **Training's remaining runs**: `train run`, promote, rollback, `pipeline resume`/`show`/`status`, `datasets pull`, eval, `regime run`, `datasets prepare` (`TrainingViewModel.swift:74`, `:123`, `:256`, `:259`, `:517`, `:556`, `:1188`, `:1240`, `:1261`, `:1272`, `:1283`, `:1308`, `:1312`, `:1315`).
   - **The monitor's label**: `ChildRegistry.swift:125-130`.
3. **What reaches a person.** A typed `-h` toasted as success in every create, add and rename form: `embed ingest`, `datasets create`, `agents create`, `symphonies create`, `config add-backend`, `review targets add`, `graph build`, `mcp create`. Library's `change()` then selects `-h`. A listed hand-written `-x` agent, MCP server, suite, review target, symphony or pipeline gets `Run with --help for more information.`.
4. **Config entries the CLI refuses after `--` too.** For entries written by hand or before 55f, most deletes and edits are refused after `--` as well (`agents delete --keep-files -- -x`: `agent '-x' is not one both config formats can edit -- …`). For those, the fix brings the CLI's words, not the act. Reads (`models info`, `symphonies show`) and the set-defaults gain real parity.
5. **Options that eat `--`.** Probed: `--source`, `--register-with`, `--skills`, `--args`, `--lora` take a following `--`; `--mcp` does not. Help shows ` ...` either way, so the builder's set comes from recorded verdicts.
   - **ClipForm** (`ClipForm.swift:80-85`) puts `--lora` last before `Argv.text(prompt)`: `video generate -o O --lora L -- '-dash first'` exits 109. That is latent today, since the `--output-format stream-json` that streaming adds separates them. `--lora` before `-o` reaches the verb. The design prototype pins today's order (`ClipForm.test.ts:31-32`).
   - **Review** keeps `--repo`/`--owner` last, after `--skills`.
6. **Readers by position** that the new order breaks: `TasksViewModel.swift:441-442`, `StoreViewModel.swift:475`, `DownloadsViewModel.swift:210`, `:484`, and the `prefix(3)` busy labels (`SettingsViewModel.swift:648`, `:692`; `XcodeSettings.swift:269`). The value lookups for `-o` and `--with` (`VideoViewModel.swift:294`, `VoiceViewModel.swift:262`, `RuntimeInstall.swift:50`) find their option before `--` and are safe.
7. **Lines the views write by hand**, which drift from what runs once the argv changes: `SessionsScreen.swift:133`, `:137`; `StoreView.swift:600`; `ModelsScreen.swift:516`, `:633`.
8. **Previews drift silently.** The preview reads are keyed by prefix (`PreviewSupport.swift:43-45` and siblings), and a prefix that stops matching shows an empty state without failing.
9. **A layering rule must be sound.** A rule that treats "the element after a dash literal" as an option value passes the broken shape: `embed delete --yes -x` exits 106. And `LayeringCheck.code(_:)` blanks every string literal and its interpolations (`LayeringCheck.swift:606-614`). A sound rule: an array literal whose first element is a command word (`tests/presentation/shell/Fixtures/command-words.txt`) holds only string literals, read from the original source at the masked positions, so every name goes through the builder.
10. **Recorded verdicts and the refresh script.** The store-command (163 lines), store-route (12) and drafter-pairing (7) verdict files are keyed by the exact line. `refresh-store-commands-fixture.sh:29` defaults to `$HOME/.local/bin/apogee`, the production install, and sets `APOGEE_HOME` only. The drafter verdicts have no refresh script.

## Acceptance criteria

- [ ] **Given** every argv the app builds outside `__complete` asks, **then** it is built by `Argv.line`: options first, then `--` and every positional, with no option that eats `--` directly before it.
- [ ] **Given** a hand-written `-x` agent, MCP server, suite, review target, symphony or pipeline, **when** its row's acts run, **then** each reaches the verb, which acts or refuses in its own words, never `Run with --help for more information.`.
- [ ] **Given** a typed `-h` in any create, add or rename form, **then** the CLI makes the thing or refuses by its rule, and the app never toasts the verb's help as success.
- [ ] **Given** a video generate whose prompt begins with `-`, on a binary that does not stream, **then** the line reaches the verb (`--lora` before `-o`).
- [ ] **Given** the readers by position, the busy labels and the monitor's label, **then** each reads `Argv.positionals(of:)` or `Argv.shown`, and shows the line a person types.
- [ ] **Given** the hand-written lines in Sessions, Store and Models, **then** each is built from the argv with `Argv.shown`.
- [ ] **Given** a new site that builds a command array with a name in it, **then** the layering check fails it, naming the file and line.
- [ ] **Given** a preview whose recorded read stops matching its prefix, **then** a test fails, rather than the preview drifting to an empty state.
- [ ] **Given** the store-command, store-route and drafter-pairing verdicts, **then** they are recorded again from the built CLI in a sandbox, and every builder's line with `-x`, `-h` and `--graph` has a recorded verdict that is not a parse refusal.

## Constraints

- **APP-M21's builder is the one seam.** No site orders its own array, and later options go through `Argv.adding`, never `+`.
- **The CLI owns every name rule**: the app adds none, and passes names as names.
- **`__complete` asks are exempt**, since the protocol takes its words verbatim.
- **Recorded verdicts come from the built CLI in a sandbox.** The binary is passed explicitly, never the refresh script's `~/.local/bin` default, and both `HOME` and `APOGEE_HOME` are set (the dev channel rule: development never touches the production install).
- **Every view keeps its named previews** ([ADR 0004](../../../../adrs/darwin/module-schemes-and-previews.md)), built through the same builder.
- **Hermetic tests in Swift Testing**, Swift 6, no warnings, a doc comment on every public declaration.

## Technical approach

Paths under `lib/src/darwin/` unless written otherwise.

| File | Change |
|---|---|
| `source/presentation/models/ViewModels/StoreViewModel.swift`, `StoreForms.swift`, `DownloadsViewModel.swift`, `DeletePlan.swift`, `Views/StoreView.swift`, `Views/ModelsScreen.swift` | Names after `--` through the builder; `--register-with` never last; `registerIt`, `reference` and `registeredAfter` read `positionals(of:)`; the hand-written lines built with `Argv.shown`. |
| `source/presentation/library/ViewModels/LibraryViewModels.swift`, `source/business/reads/Library.swift` | Agents, symphonies and MCP servers through the builder; the play's JSON flag through `Argv.adding`. |
| `source/presentation/review/ViewModels/ReviewViewModel.swift` | The ref or name after `--`; `--repo`/`--owner` last after `--skills`; the JSON flag through `Argv.adding`. |
| `source/presentation/tasks/ViewModels/TasksViewModel.swift` | Ids after `--`; the stream flag through `Argv.adding`; `resumedTask` reads `positionals(of:)`. |
| `source/presentation/sessions/ViewModels/SessionLibraryViewModel.swift`, `source/business/reads/SavedChat.swift`, `Views/SessionsScreen.swift` | Ids after `--` (for `title`, the id and the title share one `--`); the dialog lines from `Argv.shown`. |
| `source/presentation/suites/ViewModels/SuitesViewModel.swift`, `voice/ViewModels/VoiceViewModel.swift` | Names after `--`. `config get <key path>` stays one key word. |
| `source/presentation/settings/ViewModels/SettingsViewModel.swift`, `XcodeSettings.swift` | Through the builder for the one rule; a listed host is not trimmed; the busy labels read `Argv.shown`, not `prefix(3)`. |
| `source/presentation/knowledge/ViewModels/RecordsViewModel.swift` | The ids and the link's ref after `--`. |
| `source/presentation/training/ViewModels/TrainingViewModel.swift` | The remaining runs (Evidence 2) through the builder; the stream appends through `Argv.adding`. |
| `source/business/media/ClipForm.swift` | `--lora` before `-o`. |
| `source/business/jobs/ChildRegistry.swift` | The monitor's label from `Argv.shown`. |
| `source/presentation/*/Previews/*` | Preview prefixes built through the builder. |
| `tests/architecture/layering/LayeringCheck.swift` | The source rule (Evidence 9), beside M20's `dialogViolations`, pinned on a planted miniature: today's `["embed","ingest",collection,path]` and `role.verb + [backend]` fail at their lines, and the builder's shapes hold. |
| `tests/presentation/shell/Fixtures/argv-name-verdicts.txt` (new), its refresh script (new), `tests/presentation/models/Fixtures/*verdicts*.txt`, `scripts/refresh-store-commands-fixture.sh` | The verdict sweep's recording, with the binary a required argument and dash-named stores, datasets and config entries seeded. The store and drafter verdicts recorded again, and a refresh path for the drafter verdicts. The store script takes the binary as a required argument and sets `HOME`. |
| `tests/presentation/<module>/…` | Every expectation that pins an argv moves to the new order. A preview-reads test runs each module's preview reads and fails a prefix that no longer answers. |

**Reference.** APP-M21's builder. M20's layering rule (`dialogViolations`), the source-check precedent. M2's recorded store-command verdicts, the verdict-sweep precedent.

**Docs when it ships:**
- darwin `CLAUDE.md`: a checklist rule that every positional goes after `--`, an option that takes `--` is never last before it, and lines are shown with `Argv.shown`.
- `DEVELOPER.md`: the BusinessReads row and the layering check's list of checks.
- The business layer card's Modules line, written plainly with no backticks.
- MILESTONES: a dated row that supersedes 4g's "typed names … which the CLI validates".

## Testing

All hermetic: scripted runners and jobs, recorded verdicts, no window, no binary at test time.

- **The bug as a fixture: the verdict sweep.** Every builder's line, with the names `-x`, `-h` and `--graph`, is looked up in `argv-name-verdicts.txt`, recorded from the built CLI in a sandbox. The test fails on a line never recorded, and on a verdict that is a CLI11 parse refusal (`is required`, `not expected`, or help on stdout).
- **Per place, scripted**: a name-before-`--` prefix answers CLI11's refusal, and the `--` prefix answers the recorded document, so each test fails today. For example, Library: a typed `-h` agent runs `agents|create|--|-h`.
- **ClipForm**: with streaming off, a `-dash first` prompt's line has `--lora` before `-o`.
- **The layering rule**: the planted miniatures fail at their lines, and the builder's shapes pass.
- **The previews**: the preview-reads test.
- **The moved expectations**: every existing argv expectation in the 57 files, in the new order.
- **Gate:** `lib/scripts/cicd.sh --app darwin --test`, run alone.

## Open questions

| Question | Status | Answer |
|---|---|---|
| Should the rule cover every positional, CLI-minted ids included, or only names and paths? | `[default]` | Every positional, so the layering rule needs no idea of which values are ids. Only `__complete` asks are exempt. Ids never begin with `-`, so `Argv.shown` prints them without `--`, and the dialogs read as today apart from option order. |
| Which checks keep the rule for new sites? | `[default]` | All three together: the verdict sweep, the layering rule (sound only with the builder), and the preview-reads test. |
| For config entries the CLI also refuses after `--`, is the CLI's refusal enough? | `[default]` | Yes. The words name the rule, and for a backend they point to rename-backend, which APP-M21 makes work. The app adds no way round a CLI refusal. |
| Should the design prototype's mirrors move in this item? | `[default]` | No. The prototype draws none of these lines, and no screen or state is new. A design follow-up may bring its mirrors up to date (`ClipForm.ts:77` and its test among them). |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-26 | Specced from the investigation behind APP-M21 (its Evidence, and this item's). Placed in **Maintenance** as M23, gated on M21, whose builder it uses. The split puts the reachable harm first, and keeps each item one focused session. | the spec |

## Out of scope

- **The seam and the reachable rows** (Collections, Graph, Training's datasets, the Models backend rows): [APP-M21](a-listed-name-reaches-the-verb-as-listed.md).
- **A job's notes on done**: [APP-M22](a-jobs-notes-reach-the-person.md).
- **The design prototype's mirrors**: a design follow-up.
- **The CLI asides** (APP-M21's Out of scope), for the CLI queue.

## Links

- **Blocked by:** [APP-M21](a-listed-name-reaches-the-verb-as-listed.md) · **Blocks:** —
- **Relates to:**
  - [APP-M22](a-jobs-notes-reach-the-person.md);
  - [APP-7c](../../v0.2.4/every-place-against-a-server.md) and [APP-7d](../../v0.2.4/apogeekit-swift-package.md): the argv baseline and the ApogeeKit carve read better after this lands;
  - GUI 4g; M20's layering rule; [ADR 0004](../../../../adrs/darwin/module-schemes-and-previews.md).
