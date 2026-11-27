# CLI-M49 · A printed command names the binary and root that printed it

| Field | Value |
|---|---|
| **Type** | 🐞 Bug |
| **Project** | CLI |
| **Status** | 🔒 Blocked |
| **Fix version** | Maintenance (any time) |
| **Component** | Output › the program word and root flag of every printed command (`contracts/paths`, CLI-M48's `contracts/suggested_command`) |
| **Labels** | `remedies` `dev-channel` `root-flags` |
| **Epic** | — (maintenance has no track) |
| **Blocked by** | [M48](a-suggested-command-runs-as-printed.md) |
| **Blocks** | — |
| **Reporter** | The investigation of the APP-M21 and APP-M22 probes' CLI asides (2026-11-26): a sandboxed investigator, then two skeptics, one of whom found this case, and a critic the same day |
| **Created** | 2026-11-26 |

---

## Summary

`apogee --dev graph show plain Fox` fails with `run: apogee graph build plain`. Pasted, that line builds the release root's graph, and the dev root still has none. Every printed command starts with the literal word `apogee` and drops the root flag that chose the run's root. So a release binary run with `--dev`, `--test` or `--custom` points the person at another root, and every command an `apogee-dev` build prints sends the developer to the release binary over `~/.apogee`, which the dev channel's law keeps read-only. The item gives CLI-M48's renderer its program word from one place in contracts: the build's installed name, then the root flag in force. A printed command then runs as printed against the root that printed it.

## Description

### User story

**As** someone who pastes the command Apogee prints, from a run under `--dev`, `--test` or `--custom`, or from an `apogee-dev` build,
**I want** that command to name the binary and the root that printed it,
**so that** it acts on the install I was working in, and a development build never sends me to the release install.

### Steps to reproduce

The sandbox, 2026-11-26, with the built binary `apogee 0.2.0 (33ea685d, 2026-11-22, macos-arm64)`, which carries HEAD 5b8927c9's CLI strings (that commit touched none of the lines below): `env -i`, `PATH` a folder holding an `apogee` link to the built binary and then `/usr/bin:/bin`, `HOME` a throwaway folder three levels deep and `APOGEE_HOME` unset, so a bare run roots at `$HOME/.apogee` and `--dev` at `$HOME/.apogee-dev`, both inside the sandbox. Stdin from `/dev/null`, and a mock extractor backend.

1. Run `apogee config init` and `apogee config add-backend extractor --type mock --model-path extractor.json`, each bare and with `--dev`. Ingest `plain` (one file) into both roots with `embed ingest`.
2. Run `apogee --dev graph show plain Fox`, then `apogee --dev graph stats plain`.
3. Paste the printed command into sh: `sh -c 'apogee graph build plain'`.
4. Run `apogee graph stats plain`, then `apogee --dev graph stats plain`.

### Actual result

2. Exit 1, stderr `apogee graph: no graph built for 'plain' -- run: apogee graph build plain`. Then exit 0, stdout `No graph built for "plain". Run: apogee graph build plain`.
3. Exit 0: `Graph build complete for "plain":`, `Nodes upserted: 2`, ``Run `apogee graph stats plain` for the graph's shape.``, and `graph.enabled set on 'plain'`. All of it in the release root.
4. `Knowledge graph for "plain":` with `Nodes: 2 (concept 2)` for the release root, and `No graph built for "plain". Run: apogee graph build plain` for the dev root.

### Expected result

2. `apogee graph: no graph built for 'plain' -- run: apogee --dev graph build plain`, and `Run: apogee --dev graph build plain`: the flag as typed. Printed by an `apogee-dev` build, `run: apogee-dev graph build plain`.
3. Pasted, the line builds the dev root's graph.
4. The release root still has no graph, and the dev root has two nodes.

### Background

M10 (2026-10-04, MILESTONES' `install-channels` entry) made the install channel a build fact:
- three channels, each with its own root;
- the root flags `--release`, `--dev`, `--test` and `--custom <config file>`;
- one chain in `contracts/paths.h` that picks a run's root.

It gave the flag a spelling "for messages" (`RootFlag::spelling()`), and `version` and `check` say which rung chose the root. The channel's binary name, `apogee-dev` or `apogee-test`, is set only in CMake. CLAUDE.md records it as "A channel changes the root and the binary's name (`apogee-dev`), never the contents".

Two later records made that name load-bearing:
- **The dev channel's law** (the user's call, 2026-10-31). A development build is `apogee-dev` over `~/.apogee-dev`, and `~/.apogee` is read-only to development "through any door (the release binary's own verbs included)".
- **GUI M18** (2026-11-21). The Mac app's development build drives `apogee-dev` alone.

One printed command already names its binary and root. 56d's `acp registration` (2026-11-01) prints the running binary as the Executable. It puts the root flag ahead of `acp`, with `--custom`'s file made absolute (`acp_registration.cpp:55-64`). No remedy followed it: each site writes `apogee ` by hand.

How it was found: the investigation of the APP-M21 and APP-M22 probes' CLI asides took APP-M21's aside, "remedies that print a dash name without `--`", as its remedy-names seam. That aside became CLI-M48. One skeptic, checking M48's fix, ran a remedy under `--dev` (`v1`–`v11`), and the pasted line built the wrong root. The critic ran it again (`c:d1`–`c:d8`) and split it into its own item, this one.

The design, in prose:
- One function in contracts gives the words a printed command starts with: the baked channel's installed name, then the root flag in force, if one chose the root. Each word goes through M48's `shell_word`.
- M48's renderer takes them as its default program word, so every command it builds names the binary and root that printed it. That covers the CLI's stderr and stdout, `check`'s text and JSON, the HTTP bodies and the tool results.
- A root chosen by `APOGEE_HOME` adds no word: the shell that pastes the command keeps the variable.
- The flag needs no new record. The root command already keeps it in force for the run (`root.cpp:203`), and `version` and `acp registration` read it there.
- The commands with nothing to quote, which M48's sweep leaves literal, are built through the renderer too, so none still says `apogee` from a dev build.
- The house voice `apogee <verb>: ` and the version line keep the product's word.

### Evidence

All measured or read 2026-11-26 with the binary named above. Labels prefixed `c:` are the critic's runs, `v1`–`v11` the fix skeptic's, and `http01` the investigator's.

1. **The measured case** (`c:d1`–`c:d8`, reproducing the fix skeptic's `v1`–`v11` in a second sandbox).
   - `c:d1`–`c:d3`: `config init`, the extractor and the ingest succeed in both roots (`registered 'plain' in <home>/.apogee-dev/config/config.json`, and the same under `.apogee`).
   - `c:d4`: `apogee --dev graph show plain Fox` exits 1, stderr `apogee graph: no graph built for 'plain' -- run: apogee graph build plain`.
   - `c:d5`: `apogee --dev graph stats plain` exits 0, stdout `No graph built for "plain". Run: apogee graph build plain`.
   - `c:d6`: `sh -c 'apogee graph build plain'` exits 0 with `Graph build complete for "plain":`, `Nodes upserted: 2` and ``Run `apogee graph stats plain` for the graph's shape.``.
   - `c:d7`: `graph stats plain` gives `Knowledge graph for "plain":` and `Nodes: 2 (concept 2)`.
   - `c:d8`: `--dev graph stats plain` still gives `No graph built for "plain"`.

   The skeptic's `v8` and `v10` print the same remedy under `--dev`, `v9` builds the release root's graph, and `v11` (`graph show plain Fox`, bare) shows the release root's `Fox (concept) -- 1 mention(s)`.
2. **The binary knows the flag chose its root, and says so everywhere but the remedy.** `v1`: `apogee --dev version` prints `channel: release` and `root: <home>/.apogee-dev (set by --dev)`. That is `version_command.cpp:15-20` reading `current_root()` and `root_reason()` (`paths.cpp:67-79`). `check`'s Version rows read the same (`check.cpp:3302`).
3. **Root cause: the program word is a string literal at every site.** The measured lines:
   - `graph show`'s `fail_user` at `graph.cpp:1535-1536`;
   - `graph stats`'s line at `graph.cpp:1196-1197`;
   - the build's hint at `graph.cpp:356`;
   - `build_hint` at `navigate.cpp:564`, which the HTTP bodies and the graph tools carry.

   Two facts the sites have no access to:
   - **The binary's name.** `paths.h` has `channel_name`, `channel_directory` and `channel_flag` (`:57-107`), but nothing gives a channel's program name. `paths.h` names it only in a comment ("the name it installs under beside the others (`apogee`, `apogee-dev`, `apogee-test`)", `:49-50`), and the build sets it only in CMake's `OUTPUT_NAME` (`source/CMakeLists.txt:104-110`).
   - **The flag's words.** `RootFlag::spelling()` (`paths.h:129-130`, "The flag as typed, for messages"; `paths.cpp:60-65`) is used only by `root_reason` (`paths.cpp:70-71`) and the root refusals (`paths.cpp:137`, `:156`; `root.cpp:199`). It joins `--custom` and its file raw, unquoted.

   The flag in force is already kept. `apply_root_flags` holds a `RootFlagScope` for the run (`root.cpp:203`, a member at `root.h:81`), read through `root_flag()` (`paths.h:216-217`, `paths.cpp:201-205`) by `acp registration` (`acp_cmd.cpp:98`).
4. **A dev build's printed commands name the release binary** (read, not run: the rules forbade building a dev binary). Every site writes the literal, so an `apogee-dev` build prints `apogee …`. Pasted, that runs the release binary over its baked root, `~/.apogee`, which CLAUDE.md's dev-channel section (`:295`) makes read-only to development through any door. Some of these doors write:
   - check's chmod rows end `(or: apogee check --fix)` (`check.cpp:863`, `:1323`, `:1354`, `:1582`, `:1841`);
   - check's Config row for a YAML config says `apogee config migrate` (`check.cpp:439`).

   MILESTONES (`:12059`) records the dev build as `lib/src/cli/build/default/source/apogee-dev`, channel dev.
5. **The Mac app's development build** (read, not run).
   - It drives `apogee-dev` alone (`BinaryDiscovery.swift:44-45`; CLAUDE.md's dev-channel section, `:298`; GUI M18).
   - Its Settings shows each `check` remedy as selectable text (`SettingsView.swift:279-280`).
   - Its Console strips only a leading `apogee` (`ConsoleViewModel.swift:213`, `:273`).

   So today a remedy pasted into the dev app's Console runs on `apogee-dev` by accident, while the same text pasted into a terminal runs the release binary.
6. **Fully literal printed commands are outside CLI-M48's renderer.**
   - M48's sweep and its source check find a string literal followed by a name (about 190 sites).
   - A command with nothing joined to it is in neither. A grep over `source/` finds about 425 string literals in 98 files that hold `apogee <word>` with nothing joined after them, comment lines and the `apogee <verb>: ` voice left out. Help text and prose are included in that count; how many are remedies was not counted.
   - Examples that are remedies: `check.cpp:439` and `:863` above, the compat notice's `'apogee config migrate'` (`root.cpp:233`), the config conflict's (`paths.cpp:264`), and the setup remedies `kVideoSetupRemedy`, `kVoiceSetupRemedy`, `kMlxVideoSetupRemedy` and `kMlxAudioSetupRemedy` (`business/video/runtime.h:183`, `:200`, `:204`, `:254`).
7. **Blast radius.** Once M48 routes them, every printed command goes through one renderer:
   - the CLI's stderr and stdout;
   - `check`'s text and its JSON `remedy`;
   - the HTTP admin graph bodies (`http01`: `GET /v1/admin/graph/-y/neighbors?node=Fox` answers 404 with `no graph built for '-y' -- run: apogee graph build -y`);
   - the graph tools' results, which a saved session keeps (`c:t04`; the stream's `tool_call` event carries no text, `c:t03`).

   The wrong root matters in two cases: a release binary run with `--dev`, `--test` or `--custom <path>` (only `--dev` was run; the others by reading), and every printed command of an `apogee-dev` or `apogee-test` build. A root chosen by `APOGEE_HOME` is unaffected, since the shell that pastes the command inherits it. Not measured: an agent relaying a remedy and running it through `run_command`.
8. **What already works**, and must stay:
   - **`APOGEE_HOME` carries over** to a pasted command, because the shell keeps the variable.
   - **An echoed flag can't silently disagree.** The chain refuses a flag and an `APOGEE_HOME` that name different roots, naming both (`paths.h:40-41`, `paths.cpp:154-161`). So a pasted `--dev` meeting a different inherited `APOGEE_HOME` is refused, never run on a third root.
   - **`RootFlag::spelling()`** already spells the flag for messages.
   - **`acp registration`** already puts the flag ahead of the verb and makes `--custom`'s file absolute (`acp_registration.cpp:14-18`, `:55-64`).
9. **Not pinned.** No test renders a printed command under a dev or test build or a root flag: the tests hold no `apogee-dev `, `apogee --dev ` or `apogee-test ` string. `navigate_test.cpp:689` pins the release, no-flag bytes (`no graph built for 'plain' -- run: apogee graph build plain`), which stay.

## Acceptance criteria

- [ ] **Given** a release binary, `HOME` a temp folder, `APOGEE_HOME` unset and `plain` ingested into both roots, **when** `apogee --dev graph show plain Fox` fails, **then** stderr reads `apogee graph: no graph built for 'plain' -- run: apogee --dev graph build plain`. Pasted into sh, that line builds the dev root's graph (`--dev graph stats plain` shows two nodes) and leaves the release root's alone (`graph stats plain` still says `No graph built for "plain"`). Today the paste builds the release root's (`c:d6`–`c:d8`).
- [ ] **Given** the same run, **when** `--dev graph stats plain` and `--dev graph build plain` print their hints, **then** they read `Run: apogee --dev graph build plain` and ``Run `apogee --dev graph stats plain` for the graph's shape.``.
- [ ] **Given** an `apogee-dev` build, **when** it prints any command (a remedy on stderr, a hint on stdout, a `check` row's text or JSON `remedy`, an HTTP body, a tool result), **then** the command begins `apogee-dev `, and none names a bare `apogee`. `graph show`'s reads `run: apogee-dev graph build plain`, check's chmod rows end `(or: apogee-dev check --fix)`, and check's Config row reads `apogee-dev config migrate`, which is what the Mac development app's Settings then shows.
- [ ] **Given** an `apogee-test` build, **then** the same holds with `apogee-test `.
- [ ] **Given** `--custom "<a folder holding a space>/config/config.json"`, **then** a printed command carries `--custom "<that file, absolute>"` after the program word and before the verb, and, pasted into sh, runs against that root.
- [ ] **Given** a release build and a root chosen by `APOGEE_HOME` or by no flag, **then** every printed command's bytes are today's, and the existing pins and goldens pass unedited.
- [ ] **Given** `serve` under a root flag, **when** `GET /v1/admin/graph/<name>/neighbors` answers 404 for an unbuilt graph, **then** its message names the same command the CLI prints for that graph.
- [ ] **Given** any of these runs, **then** every stderr line still begins `apogee <verb>: ` (`apogee graph: ` here), the compat notice still begins `apogee: `, `version`'s first line still begins `apogee `, and check's chmod rows still begin `chmod`.
- [ ] **Given** a planted fully literal printed command outside the renderer (`"run 'apogee config init'"`), **then** `cli.one_suggested_command` fails naming the line. On the shipped tree it passes.

## Constraints

- **The house voice keeps `apogee`.** `apogee <verb>: ` (`graph.cpp:48` and its siblings) and the root's `apogee: ` never take the channel's name or the flag. The Mac app keeps exactly those lines as a run's notes ([APP-M22](../../../assistant/darwin/MILESTONES.md#2026-11-26--jobs-notes-reach-the-person-maintenance-item-m22-a-jobs-notes-reach-the-person); CLI-M46's Constraints, "Every note keeps the house voice on stderr"), and the tests pin the prefix.
- **The version line keeps `apogee`.** Its first word is the product's: the release scripts read line one (M10's record), and the Mac app accepts a binary only when that word is `apogee` (`BinaryDiscovery.swift:94`).
- **One answer, in contracts.** The program words come from one function in `paths.h`, beside `channel_flag`. No site spells a channel's binary name. The three names are CMake's `OUTPUT_NAME`s (`source/CMakeLists.txt:106-110`), and `paths_test` holds them at compile time, as it holds the channel names.
- **The flag is read, never decided again.** The renderer reads `root_flag()`, which the root command already keeps for the run. Nothing new is recorded, and `RootFlag::spelling()` stays the messages' spelling.
- **A printed command runs as printed** (CLI-M48's invariant). The program words go through `shell_word` like every other word, so a `--custom` file holding a space is quoted.
- **Development runs on the dev channel** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md#development-runs-on-the-dev-channel), `:289-298`). A dev build's printed command never sends the developer to `apogee` over `~/.apogee`. Live passes and "try it" steps name `apogee-dev`.
- **Tests stay hermetic and put back what they change.** `APOGEE_HOME` is a temp folder, or, for a run under `--dev`, `HOME` and `USERPROFILE` are a temp folder with `APOGEE_HOME` and `APOGEE_CONFIG` unset. `BakedChannelGuard` restores the baked channel, and `RootFlagScope` restores the flag.
- **Layers** ([ADR 0001](../../../adrs/cli/layer-enforcement.md)): the words live in Data (`contracts`), reached downward by the Business sites (`navigate.cpp:564`) and by Presentation.
- **Mode parity** ([ADR 0002](../../../adrs/cli/mode-parity.md)): the HTTP bodies and the tool results carry the words the CLI prints, because one renderer builds them all.
- **The machine documents grow additively** ([ADR 0006](../../../adrs/cli/backwards-compatibility.md)): `check`'s JSON `remedy` keeps its field and shape. Only its text names the binary, and a release build with no flag prints today's bytes.
- **Code style**: a `.h`/`.cpp` pair, clang-format, and no warning on any CI compiler (the MinGW scan of the changed files).

## Technical approach

Paths under `lib/src/cli/`.

| File | Change |
|---|---|
| `source/data/contracts/paths.h`, `paths.cpp` | `channel_program(Channel)`, constexpr beside `channel_flag`: `apogee`, `apogee-dev`, `apogee-test`. `program_words()`: `channel_program(baked_channel())`, then the words of `root_flag()` when a flag is in force. Those words are the channel flag, or `--custom` and its file made absolute and normalized. They are returned unquoted: the renderer quotes. `RootFlag::spelling()` is unchanged. |
| `source/data/contracts/suggested_command.h`, `.cpp` (CLI-M48's) | A command's program word, `apogee` by default in M48, becomes `program_words()`, each word through `shell_word`. A command that names its own program (check's `chmod`) keeps it. |
| `source/presentation/cli/check.cpp`, `root.cpp`, `source/data/contracts/paths.cpp`, `source/business/video/runtime.h`, `.cpp`, and every other file holding a fully literal printed command | Each such command is built through the renderer. That includes check's Config row (`check.cpp:439`), the chmod rows' `(or: apogee check --fix)` (`:863`, `:1323`, `:1354`, `:1582`, `:1841`), the compat notice (`root.cpp:233`), the config conflict (`paths.cpp:264`), the four setup remedies (`runtime.h:183`, `:200`, `:204`, `:254`), and the rest the source check names. The house voice before each is untouched. |
| `tests/scripts/cmake/one_suggested_command.cmake` (CLI-M48's) | Also fails on a fully literal printed command outside the renderer: a string literal holding `apogee ` and a lower-case word not followed by `:`. Help text, completion descriptions and prose naming a verb in passing go on a list of this rule's own, each with its reason and each checked to name a line that exists, so M48's allowlist for joined literals stays empty, as M48 ships it. It still refuses to pass when it matches nothing. |
| `tests/data/contracts/paths_test.cpp` | `channel_program`'s three names at compile time. `program_words()` for each baked channel with no flag, with each channel flag, and with `--custom` (a relative file made absolute). |
| `tests/data/contracts/suggested_command_test.cpp`, `tests/presentation/cli/suggested_command_test.cpp` (both CLI-M48's), `tests/presentation/httpserver/admin_graph_navigate_test.cpp` | The fixtures under Testing. |

The interface:

```cpp
/// The name a channel's executable installs under: `apogee`, `apogee-dev`,
/// `apogee-test` -- what CMake's OUTPUT_NAME gives it.
[[nodiscard]] constexpr std::string_view channel_program(Channel channel) noexcept;

/// The words a printed command starts with: this build's name, then the root
/// flag in force for the run, if one chose the root. Unquoted.
[[nodiscard]] std::vector<std::string> program_words();
```

What a printed command begins with:

| Build | Root chosen by | A printed command begins |
|---|---|---|
| `apogee` | the baked channel, or `APOGEE_HOME` | `apogee ` (today's bytes) |
| `apogee` | `--dev` | `apogee --dev ` |
| `apogee` | `--custom "/a b/config/config.json"` | `apogee --custom "/a b/config/config.json" ` |
| `apogee-dev` | the baked channel, or `APOGEE_HOME` | `apogee-dev ` |
| `apogee-dev` | `--dev` | `apogee-dev --dev ` |
| `apogee-test` | the baked channel | `apogee-test ` |

Choices behind the design:
- **From the baked channel, never from `argv[0]` or `executable_path()`.** A printed command names the install a person types. A build-tree path or a symlink's name is not one. `acp registration` prints the path only because Xcode needs an executable.
- **The root flags go between the program word and the verb**, where the root command reads them, as 56d puts them ahead of `acp`.
- **The flag needs no new record.** `root.cpp` already holds it in force for the run (`:203`), behind one lock that `serve`'s threads read (`paths.cpp:19-29`). A refused root never reaches a command, so no remedy can print a flag the chain refused.
- **Consumed from CLI-M48:** the renderer, `shell_word` and its word rules, the order of options and `--`, the shell-replayed parse-back harness, and `cli.one_suggested_command`. A literal M48 already routed needs nothing here.

**Reference.**
- `acp registration` (56d, `acp_registration.cpp:55-64`): the root flag ahead of the verb, and `--custom`'s file made absolute (`:14-18`). It also carries a typed `--config` (`:65-71`), which this item does not (Open questions).
- `version` and `check`, which already say the rung (`version_command.cpp:15-20`, `check.cpp:3302`).
- `paths.h`'s constexpr per-channel spellings (`channel_flag`, `channel_directory`), which `channel_program` joins.
- For tests: `BakedChannelGuard` (`tests/support/channel_guard.h`), and `root_test.cpp`'s `Sandbox` (`:179-186`) for a run under `--dev`.

**Docs when it ships:**
- CLAUDE.md: M48's invariant gains "and names the binary and root that printed it". The "Which root, chosen once" sub-rule's "A channel changes the root and the binary's name" gains that every command it prints names that binary.
- DEVELOPER.md: the `contracts/paths_test.cpp` row and M48's `suggested_command_test` rows.
- `reference/cli/machine-mode.md` (check's `remedy`, `mcp presets`' `register`) and `reference/cli/http-api.md`: a printed command names the binary and root that printed it.
- MILESTONES: a dated entry.

## Testing

Every test is hermetic. `APOGEE_HOME` is a temp folder, or, for a run under `--dev`, `HOME` and `USERPROFILE` are a temp folder with `APOGEE_HOME` and `APOGEE_CONFIG` unset. `BakedChannelGuard` and `RootFlagScope` put the channel and the flag back. Each fixture fails on today's tree unless marked as a guard. The shell replay stays in the Data test and the in-process runs in the Presentation test ([ADR 0004](../../../adrs/cli/tests-mirror-architecture.md)).

- **The bug as a fixture**, in `tests/presentation/cli/suggested_command_test.cpp`, in `root_test.cpp`'s `Sandbox` shape. CliHome can't be used here: its `APOGEE_HOME` would refuse `--dev`.
  - Run `config init` and a mock extractor bare and with `--dev`, and ingest `plain` into both roots.
  - `--dev graph show plain Fox` exits 1 with stderr `apogee graph: no graph built for 'plain' -- run: apogee --dev graph build plain`.
  - Replay the printed command through `/bin/sh` to its argv (M48's parse-back), drop the program word, and run the rest in process.
  - Then `--dev graph stats plain` shows `Nodes:`, and `graph stats plain` still says `No graph built for "plain"`.
- **The hints**: in the same sandbox, `--dev graph stats plain` prints `Run: apogee --dev graph build plain`, and `--dev graph build plain` prints ``Run `apogee --dev graph stats plain` for the graph's shape.``.
- **`--custom`**: with `--custom "<temp>/a b/config/config.json"`, the remedy carries `--custom` and that file, double-quoted. Replayed, it reads that root.
- **A dev and a test build**: CliHome under `BakedChannelGuard(Dev)`.
  - `graph show`'s remedy reads `run: apogee-dev graph build plain`.
  - `check` text and `--output-format json`: every remedy naming an Apogee command begins `apogee-dev `, a chmod row ends `(or: apogee-dev check --fix)`, and a YAML config's row reads `apogee-dev config migrate`.
  - Under `Test`, the same with `apogee-test `.
- **Unchanged (guard)**: a release build under CliHome's `APOGEE_HOME` prints a bare `apogee`. Every existing pin passes unedited, `navigate_test.cpp:689` among them.
- **The house voice (guard)**: in each run above, every stderr line begins `apogee graph: ` (or the verb's own), and `version`'s first line begins `apogee `.
- **Data**, in `tests/data/contracts/suggested_command_test.cpp`:
  - with `BakedChannelGuard(Dev)`, `{graph, build}` with `-x` renders `apogee-dev graph build -- -x`, and with `Test`, `apogee-test graph build -- -x`;
  - with a `RootFlagScope` holding `--dev` on a release build, it renders `apogee --dev graph build -- -x`;
  - with `--custom` naming a file under a folder holding a space, the file is double-quoted;
  - a `chmod` command is unchanged under every channel;
  - after each case, the channel and the flag are the ones before it.
- **`paths_test`**: `channel_program`'s three names held at compile time. `program_words()` for every baked channel with no flag, each channel flag, and `--custom` with a relative file, which comes out absolute.
- **HTTP**, in `admin_graph_navigate_test.cpp`: under a `RootFlagScope` holding `--custom` at the fixture's own root (so the chain agrees with its `APOGEE_HOME`), the neighbors 404 for an unbuilt graph names `apogee --custom <that file> graph build <name>`, as the CLI prints it.
- **The source check**: `cli.one_suggested_command` fails on a planted `"run 'apogee config init'"` and passes on the shipped tree.
- **Gate:** `lib/scripts/cicd.sh --app cli --test`, clang-format, and the MinGW scan of the changed files.

## Open questions

| Question | Status | Answer |
|---|---|---|
| A release binary run with `--dev`: should its printed commands echo `apogee --dev`, or print `apogee-dev`? | `[default]` | Echo what was typed: `apogee --dev`. An `apogee-dev` build prints `apogee-dev`, which is the name the dev channel's law gives it. A flag naming the build's own channel is echoed too (`apogee-dev --dev …`): redundant, but it runs as printed and keeps one rule. |
| Should `--custom <path>` be echoed? | `[default]` | Yes, as two words, `--custom` and the file, each quoted by `shell_word`, the file made absolute as `acp registration` prints it (`acp_registration.cpp:62`), so the command runs from any folder. A remedy without it runs against another root. |
| Do the fully literal printed commands take the program word in this item? | `[default]` | Yes. Every command the output tells a person to run is built through the renderer, literal or not: a remedy, a door, a `Run:` or `run:` hint, `check`'s remedies. So `apogee check --fix` and `apogee config migrate` stop sending a dev build's user to `~/.apogee`. Help text, completion descriptions and prose that names a verb in passing keep `apogee`, each on the source check's list for this rule with its reason, apart from M48's allowlist, which stays empty. |
| Should `--config <file>` be echoed beside the root flag? | `[default]` | No, not in this item. It names a config file, not the root (M10's record), and it was not measured. A follow-up that wants it has 56d's shape: `--config <absolute file>` beside a channel flag or none, never beside `--custom` (`acp_registration.cpp:65-71`). |
| How a word is quoted, where `--` goes, and the Windows rule | `(consumed decision)` | [CLI-M48](a-suggested-command-runs-as-printed.md) owns them. This item's words go through its `shell_word`. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-26 | Specced from the investigation of the APP-M21 and APP-M22 probes' CLI asides, from runs under `--dev` (`v1`–`v11`, re-run as `c:d1`–`c:d8`; Evidence). **The seam is which binary and root a printed command names**, not how its words are quoted. | the spec |
| 2026-11-26 | Placed in **Maintenance** as M49, by the user's call to spec the APP-M21/M22 CLI asides as maintenance items from M48, grouped only where they share one seam. It is its own item rather than part of CLI-M48: M48 decides how a printed command's words are spelled so that it parses, and this item decides which program and root flag the command starts with. This item touches the dev channel's law and the Mac app's Console, which M48 does not. | the user |
| 2026-11-26 | **Blocked by CLI-M48**, which supplies the one renderer. Without it, the program word would be changed by hand at each of about 190 sites, then rewritten by M48. When M48 is split, the gate is its Pass A, as M48 records: the renderer and `shell_word` are there. Pass B moves the remaining name sites and ships `cli.one_suggested_command`. Built before Pass B, this item moves onto the renderer, with no other change, any site Pass B has not moved yet (as CLI-M50 does), and starts `one_suggested_command.cmake` with this item's rule, which Pass B then extends. | the spec |
| 2026-11-26 | **The flag is echoed as typed, and the binary's name comes from the build** (the first two Open questions). `APOGEE_HOME` adds nothing, since the shell that pastes the command keeps it. | default |
| 2026-11-26 | **The fully literal printed commands are in scope** (Evidence 6). The investigation sized this item as one small session, on the belief that every remedy passes through M48's renderer. About 425 literals do not, among them `check --fix` and `config migrate`, which write. The build may run past one session. The source check names each literal, so nothing is left to memory. | the spec |
| 2026-11-26 | **No gate on the Mac app's Console.** The release app is unchanged while it drives the release `apogee`, its first candidate. It falls back to `apogee-dev`, then `apogee-test`, only when no `apogee` answers (`BinaryDiscovery.swift:38-43`), and its Console then meets the same word. The dev app's Console strips only `apogee`, so a pasted `apogee-dev …` line stops running there until a GUI follow-up strips the channel's word too. The dev app is a development tool, and the harm this item removes, a dev build pointing at `~/.apogee`, is the worse one. | the spec |
| 2026-11-26 | **No record is reversed.** CLAUDE.md's "A channel changes the root and the binary's name (`apogee-dev`), never the contents" already says a dev build is `apogee-dev`; this item makes its printed commands say so. M48's "plain names keep today's bytes" holds for a release build with no root flag, which is where every existing test runs. | the spec |

## Out of scope

- **How a printed command's words are quoted and ordered**, the renderer itself, its source check and the quoter folds: [CLI-M48](a-suggested-command-runs-as-printed.md).
- **The rename door** a refused backend name is pointed to: [CLI-M50](a-refused-backend-name-gets-the-rename-that-reaches-it.md).
- **The TUI's row actions** passing a dash-led name: [CLI-M51](the-tuis-row-actions-reach-a-dash-led-row.md).
- **The house voice and the version line** (`apogee <verb>: `, `apogee: `, `apogee 0.2.0 …`), CLI11's usage line (the app named `apogee`, `root.cpp:91`), and help text. They keep `apogee`.
- **The Mac app's Console**, which strips only a leading `apogee` (`ConsoleViewModel.swift:213`, `:273`): a GUI follow-up for the darwin queue, so that a pasted `apogee-dev …` line runs there too.
- **The TUI's exec line**, which strips no leading `apogee` at all (`tui_runner.cpp:73-88`; `c:w01`: `apogee apogee graph stats plain` exits 109). A whole pasted remedy never runs there, plain or not. An aside of the investigation, not in this item.
- **`--config <file>`** (Open questions).
- **A printed command read on another machine.** Relayed to a paired device (CLI-59d) or shown by the app against a server (darwin 7c), it names the server's binary and root. What a remote reader is told belongs to those items.
- **An agent relaying a remedy** and running it through `run_command`: not measured.
- **The completion stubs**, which call `apogee` by name and which a dev install never writes (M10).

## Links

- **Blocked by:** [CLI-M48](a-suggested-command-runs-as-printed.md) · **Blocks:** —
- **Sibling:**
  - [CLI-M48](a-suggested-command-runs-as-printed.md), the renderer this item names the binary in;
  - [CLI-M50](a-refused-backend-name-gets-the-rename-that-reaches-it.md), whose door is built through the same renderer and takes the program word with no change of its own;
  - [CLI-M51](the-tuis-row-actions-reach-a-dash-led-row.md), the TUI's argv builders.
- **Relates to:**
  - [APP-M21](../../../assistant/darwin/MILESTONES.md#2026-11-26--listed-name-reaches-the-verb-maintenance-item-m21-a-listed-name-reaches-the-verb-as-listed), whose remedy aside started the seam;
  - [APP-M22](../../../assistant/darwin/MILESTONES.md#2026-11-26--jobs-notes-reach-the-person-maintenance-item-m22-a-jobs-notes-reach-the-person), which keeps the house-voice lines this item leaves alone;
  - [CLI-M46](a-collection-the-config-cannot-hold-is-refused-before-it-is-written.md), whose two `check` remedies M48 builds and this item names the binary in;
  - [CLI-M58](a-runs-notes-are-said-once-on-stderr.md), which says a run's notes as `apogee <verb>: …`, the voice this item doesn't touch;
  - CLI-59d (v0.2.3, [reads and commands over the network](../v0.2.3/reads-and-commands-over-the-network.md)) and darwin 7c ([every place against a server](../../darwin/v0.2.4/every-place-against-a-server.md)), where a server's printed commands are read elsewhere;
  - [ADR 0002](../../../adrs/cli/mode-parity.md) and [ADR 0006](../../../adrs/cli/backwards-compatibility.md);
  - CLAUDE.md's [Development runs on the dev channel](../../../assistant/cli/CLAUDE.md#development-runs-on-the-dev-channel) and "Which root, chosen once";
  - M10 (2026-10-04, the channels and root flags), 56d (2026-11-01, `acp registration`, the precedent) and GUI M18 (2026-11-21, the Mac app's dev build on `apogee-dev`).
