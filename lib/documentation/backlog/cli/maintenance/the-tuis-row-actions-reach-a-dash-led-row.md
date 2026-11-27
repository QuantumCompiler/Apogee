# CLI-M51 · The TUI's row actions reach a row whose name starts with a dash

| Field | Value |
|---|---|
| **Type** | 🐞 Bug |
| **Project** | CLI |
| **Status** | 🟢 Ready |
| **Fix version** | Maintenance (any time) |
| **Component** | The full-screen shell › the acts the Datasets, Train, Agents and MCP views run on a row as the shell's child (`cli/tui_training`, `cli/tui_agents`) |
| **Labels** | `tui` `argv` `silent-failure` |
| **Epic** | — (maintenance has no track) |
| **Blocked by** | — |
| **Blocks** | — |
| **Reporter** | The investigation of the APP-M21 and APP-M22 probes' CLI asides (2026-11-26): found by its sandboxed investigator, then two skeptics and a critic the same day |
| **Created** | 2026-11-26 |

---

## Summary

The full-screen shell's row acts hand the row's name to the command as a bare word, ahead of any `--`, so a dataset named `-x`, which `apogee datasets create --from empty -- -x` makes today, can't be deleted from the Datasets view: the child exits 106 with `name is required`. A dataset named `-h` is worse: the child prints the verb's help and exits 0, the view draws that help as the delete's answer, and the dataset stays. The item puts the options, then `--`, then the name in all five argv lists the views build, and opens the MCP view's `t` on `-- <name> ` for a name that starts with `-`.

## Description

### User story

**As** someone working in `apogee tui`,
**I want** each key on a row to act on that row, whatever its name,
**so that** a dataset, agent, MCP server or backend whose name starts with a dash is deleted, enabled, disabled, rolled back or tested from its view, as it is at the prompt with `--`.

### Steps to reproduce

The sandbox, 2026-11-26, with the built binary `apogee 0.2.0 (33ea685d, 2026-11-22, macos-arm64)`: `env -i` with `PATH`, `HOME` and `APOGEE_HOME` at a fresh folder three levels deep, stdin from `/dev/null`, mock backends only. The shell itself was not driven. Step 2 runs the exact argv the Datasets view's `x` hands its child (`tui_training.cpp:278-279`), which `child_answer` runs with no shell in between (`tui_child.cpp:154-158`).

1. Run `apogee datasets create --from empty -- -x`.
2. Run the view's argv: `apogee datasets delete -x --yes`.
3. Run `apogee datasets delete --yes -- -x`.

### Actual result

1. Exit 0, stdout `…/training/datasets/-x.jsonl` and `0 example(s)`.
2. Exit 106, stdout empty, stderr `name is required` then `Run with --help for more information.`. `-x.jsonl` is still there. The view's `x`, answered `y`, runs this argv (read in the code, not driven), so the view can't delete the row.
3. Exit 0, stdout `will remove:`, `  …/training/datasets/-x.jsonl`, a blank line, `removed.`. The file is gone.

### Expected result

1. The same.
2. The view's `x` runs `datasets delete --yes -- -x`. The view draws `will remove:` and `removed.`, `-x.jsonl` is gone, and the row leaves the table when the view reads again.
3. The same.

### Background

The Train and Datasets views shipped with 37f, and the Agents and MCP views with 37g, both on 2026-10-10. Both records chose to run every act as the command itself, as a child of the shell's own binary (MILESTONES, the 37f and 37g entries; [ADR 0010](../../../adrs/cli/the-shell-is-a-mode.md)). 37g's entry says: "The config each writes is the command's by construction." Each act builds its argv by hand as a short list, with the row's name as a plain word:
- the Datasets view's `x`: `{"datasets", "delete", row.key, "--yes"}`;
- the Train view's `R`: `{"train", "rollback", <the row's backend>}`;
- the Agents view's `x`: `{"agents", "delete", row.key}`;
- the MCP view's `e` and `d`: `{"mcp", "enable", row.key}` and `{"mcp", "disable", row.key}`.

The MCP view's `t` opens its input row on `<name> `, and the typed line is split and handed to `mcp test`.

CLI11 reads a word shaped like an option as an option before the verb runs. A required name left empty is exit 106, and `-h` prints the verb's help with exit 0. A name starting with `-` therefore never reaches the verb from these views.

Datasets can carry such a name with no hand edit. `valid_dataset_name` admits a leading `-`. That corrects APP-M21's aside that datasets have no name rule: they have one, and it lets the dash through. Agents, MCP servers and backends need a hand-edited config, or one written before 55f.

The remedy-names investigation (2026-11-26), which followed the CLI asides of the APP-M21 and APP-M22 probes, found this beside them. It was first noted as an aside, thought to need a terminal, and was then measured without one by running the argv the view builds (Evidence 2). It is its own seam, the argv builders. Those print nothing and quote nothing, so this isn't the printed-command seam of CLI-M48–M50. The made-thing-names investigation measured the same argv on a `-h` dataset that day, and so did APP-M21's. APP-M21 fixes the Mac app's copy of this bug class, and this item fixes the CLI's own shell.

The design: in each argv list, the options come first, then `--`, then the row's name. The MCP view's `t` opens on `-- <name> ` when the name starts with `-`. A name that doesn't start with `-` and needs no quoting shows no visible change. That covers every agent, MCP server and backend name the CLI's own create verbs make. The asks' words, such as `Delete dataset -x ('apogee datasets delete -x')?`, are a printed command, so CLI-M48's renderer owns them.

### Evidence

All measured 2026-11-26 with the built binary `apogee 0.2.0 (33ea685d, 2026-11-22, macos-arm64)`. Its stamp names HEAD~1, but it carries HEAD 5b8927c9's CLI strings (the critic's `c:v00`: `strings` finds HEAD's `no pipeline run 'train pipeline resume' takes` once). Of the files cited here, that commit changed only `train.cpp`, whose `train rollback` was read, not probed. Labels prefixed `c:` are the remedy-names critic's runs; `td1`/`td2` are the cause skeptic's, `t01`–`t06` the fix skeptic's, and `kd1`–`kd5` and `e1` the investigator's. Each was run in its own sandbox. Source lines are HEAD's.

1. **The CLI makes a dataset named `-x`.**
   - `c:t10` `datasets create --from empty -- -x` exits 0 and prints `…/training/datasets/-x.jsonl` / `0 example(s)`. `kd1`, `t01` (`-x`) and `t02` (`-y`) did the same.
   - `valid_dataset_name` (`business/training/datasets.cpp:42-49`) takes letters, digits, `-`, `_` and `.`, from 1 to 128 bytes, and refuses only a dot as the first character. `DatasetStore::write` calls it at `:157-160`.
   - `kd2`–`kd5` refuse a space and metacharacters: `'my notes' is not a dataset name: letters, digits, '-', '_' and '.', not starting with a dot`.
   - In the made-thing-names investigation, `POST /v1/admin/datasets {"name":"-h","from":"empty"}` answered 201 (CLI-M56's evidence).
   - `t05` `datasets list` lists `-x` and `-y`. That is the read the Datasets view draws (`tui_training.cpp:210-221`, `DatasetStore::list`).
2. **The view's delete is refused.**
   - `c:t11` runs `datasets delete -x --yes`, the argv from `tui_training.cpp:278-279`. It exits 106, stdout empty, stderr `name is required` / `Run with --help for more information.`.
   - `td1` and `t03`/`t04` (for `-x` and `-y`) measured the same.
   - In the view, `child_answer` returns the child's lines and `exit 106` (`tui_child.cpp:154-158`). The view draws them under the table, then reads the table again with the row still in it (`ListAction::run`, `tui/list_view.h:44-48`; by reading).
3. **A dataset named `-h` reports a false success.**
   - The same argv, `datasets delete -h --yes`, prints the verb's help on stdout (`Remove a dataset` / `Usage: apogee datasets delete [OPTIONS] name`), exits 0 and leaves `-h.jsonl`. Measured that day in the made-thing-names investigation (CLI-M56's Evidence 2, `crit-c`), and in APP-M21's, whose Steps 3 and Evidence 6 run the Mac app's identical argv.
   - With exit 0, `child_answer` adds no exit line. So the view draws `datasets delete`'s help as the delete's answer, and `-h` is still listed after the read (by reading).
4. **The verb is reached once the name follows `--`.**
   - `c:t12` `datasets delete --yes -- -x` exits 0 with `will remove:` / `  …/training/datasets/-x.jsonl` / `removed.`, and the file is gone. `td2` and `t06` measured the same.
   - APP-M21 measured `datasets delete --yes -- -h` removing `-h`.
   - The order matters. After `--`, every word is an operand: APP-M21 measured `embed delete -- -x --yes` exiting 109.
5. **Why the parser refuses.** CLI11 2.4.2 (`Version.hpp:16`) classifies each word before the verb's callback runs (`App_inl.hpp:1039-1057`): `--` is the positional mark, `--word` is a long option, and `-` followed by a non-digit is a short option. A required positional left empty is exit 106, and `-h` or `--help` prints help with exit 0. A bare `-` and a shape like `-1` are read as names (APP-M21's Evidence 1).
6. **The same shape at four more sites**, by reading at HEAD. None was probed with a dash-led row, since each needs a hand-edited entry (Evidence 11).
   - `tui_agents.cpp:143`, the Agents view's `x`: `{"agents", "delete", row.key}`.
   - `tui_agents.cpp:227` and `:233`, the MCP view's `e` and `d`: `{"mcp", "enable", row.key}` and `{"mcp", "disable", row.key}`.
   - `tui_training.cpp:195-197`, the Train view's `R`: `{"train", "rollback", version_of(row).value_or(VersionRow{}).backend}`.
   - `tui_agents.cpp:212`, the MCP view's `t`. It opens its input row on `row.key + " "`. `line_tokens` splits the line, and the words follow `{"mcp", "test"}` (`:203-210`). So a server named `-x` gets the line `-x <tool>`, which runs `mcp test -x <tool>`.
7. **The five verbs take `--` cleanly.**
   - Their definitions:
     - `datasets delete`: one positional and `-y,--yes` (`presentation/cli/datasets.cpp:592-596`);
     - `agents delete`: one positional, `--purge` and `--keep-files` (`agents_cmd.cpp:269-277`);
     - `mcp enable` and `mcp disable`: one positional (`mcp_cmd.cpp:297-302`);
     - `mcp test`: three positionals and no option (`:256-264`);
     - `train rollback`: one positional (`train.cpp:2839-2844`).
   - None has an option that collects several values. Only such an option eats a `--` placed straight after it (`App_inl.hpp:1975-1999`). `e1`, `graph build --source src -- -w`, exits 109.
   - So `<verb> [options] -- <name>` reaches each verb. Measured for `datasets delete` (`c:t12`), `symphonies play --input hi -- -q` (`c:y06`), `symphonies show -- -q` (`c:y08`), and the config deletes (`c:e04`, `c:e05`, Evidence 10).
8. **No shell, so no quoting.** `child_answer` calls `run_child_text(self_command(...))`. `self_command` (`tui_child.cpp:52-68`) builds the command from the words as given, after `--config` when the shell was given one, with `APOGEE_HOME` set. `run_child_lines` hands it to `platform::start_child` and closes its stdin (`:91-99`). In argv, `--` is the whole fix. The `t` prefill is different: it is a line that `line_tokens` splits with POSIX quoting and no expansion (`line_tokens.h:7-14`). A key holding a blank or a quote would split there, which is why its spelling is CLI-M48's `shell_word`.
9. **Root cause.** The five argv builders and the prefill put `row.key` (or the row's backend) where CLI11 reads it, with no `--` in front (`tui_training.cpp:195-197`, `:278-279`; `tui_agents.cpp:143`, `:212`, `:227`, `:233`). That shape has stood since 37f and 37g (2026-10-10). The names reach the shape by three routes:
   - **datasets**: through `valid_dataset_name`'s leading `-`;
   - **agents and MCP servers**: through a config load that refuses only an empty name (`config.cpp:2117-2118`, `:2171-2172`);
   - **backends**: through configs written before 55f or edited by hand, since 55f's add rule refuses a leading `-` at add (`config_edit.cpp:707-724`).
10. **What a hand-written row then meets.** Once the name reaches the verb, the verb answers in its own words.
    - On a JSONC config, the line editor never finds a quoted key outside 55f's rule, and refuses it in 57e's words (`missing_entry`, `config_edit.cpp:687-699`).
    - Measured: `c:e05` `config delete-backend -- -x` gives `apogee config: backend '-x' is not one both config formats can edit -- a name is letters, digits, '_', '-', '.' and '/', first neither '-' nor '.', and not null; rename it with 'apogee config rename-backend'`. `c:e04` `config delete-graph -- -x` gives the same rule with no way out.
    - Not measured for agents, MCP servers or a rollback. `c:e01` stopped at `no agent named '-x'`, and `c:e03` at `no 'mcp_servers:' section`, before any entry existed.
    - After this item, the shell shows whatever the verb answers instead of `name is required`: on a JSONC config most likely that refusal, though only the config deletes above measured it. Which way out it names is CLI-M50's.
11. **Blast radius.**
    - **Sites:** five argv builders and one prefill, in two files. The Datasets, Train, Agents and MCP views are every view whose act runs the command as a child. The other views call a core in process, with the name as a value.
    - **Reachable today with no hand edit:** datasets (Evidence 1), through the Datasets view's `x`.
    - **Reachable only through a hand edit:** agents, MCP servers and backends (Evidence 9).
    - **The effect** is a refusal (exit 106), or the verb's help shown as a success for `-h` (Evidence 3). No other row is acted on: argv never passes through a shell, and the parser refuses before the verb runs.
    - **Another dataset's name** is printed in one case, by reading. A row named `--` beside a dataset named `--yes` builds `datasets delete -- --yes`. That reads `--yes` as the name and prints `will remove: …/--yes.jsonl`, then `re-run with --yes to delete.`, since `--yes` is no longer a flag (`presentation/cli/datasets.cpp:597-604`, `:177-183`). Nothing is removed.
12. **What already works**, and must stay:
    - **Every plain name.** The child tests pin the acts on plain rows: the Datasets view's delete (`tui_training_test.cpp:403-424`), the Train view's rollback (`:327-372`), the MCP view's `e`/`d` and `t` (`tui_agents_test.cpp:178-201`, `:203-238`), and the Agents view's delete (`:240-276`). The `t` hint reads `test: tools ▏` (`:228`).
    - **argv is passed as given** (Evidence 8), so no word needs quoting there.
    - **The in-process views reach any name.** The Collections view deletes with `delete_collection(row.key)` (`tui_knowledge.cpp:150`), and the Symphonies view shows `symphony_show_text(context, row.key)` (`tui_symphonies.cpp:57`). A dash-led collection or symphony is reached there today (by reading).
    - **The Train view's Enter** runs `train eval <run id>` (`tui_training.cpp:131`), on an id the CLI mints from the clock (`new_run_id`, `business/training/manifest.cpp:144`).
    - **The typed asks** (`r`, `p`, `P` in Train; `p`, `s` in Datasets; the tool and arguments after `t`'s prefill) are the person's own line. A `--` typed there works as at a prompt.
13. **Not pinned.** No test in `tui_training_test.cpp` or `tui_agents_test.cpp` has a row whose name starts with `-`.

## Acceptance criteria

- [ ] **Given** a dataset file `training/datasets/-x.jsonl` and the Datasets view on its row, **when** `x` is pressed and the ask answered `y`, **then** the child runs `datasets delete --yes -- -x`, the view draws `will remove:` and `removed.`, `-x.jsonl` is gone, and the row leaves the table when the view reads again. Today the child exits 106 with `name is required`, and the file stays.
- [ ] **Given** a dataset `-h`, **when** it is deleted from the view the same way, **then** `-h.jsonl` is gone and the answer is the verb's `removed.`. Today the child prints `datasets delete`'s help and exits 0, the view draws that help as the answer, and the dataset stays.
- [ ] **Given** hand-written JSONC entries named `-x` (an agent, a disabled MCP server, and a backend with a two-version ledger), **when** the Agents view's `x`, the MCP view's `e` and then `d`, and the Train view's `R` run on them, **then** each child's answer, and the config and ledger it leaves, equal what `apogee agents delete -- -x`, `apogee mcp enable -- -x`, `apogee mcp disable -- -x` and `apogee train rollback -- -x` answer and leave on a twin home, each home's path written alike. The answer may be the verb's success or its own refusal. None is `name is required`, an `exit 106` line or a help page.
- [ ] **Given** the MCP view on a server named `-x`, **when** `t` opens the input row, **then** it reads `-- -x ` (`test: -- -x ▏`). A tool typed after it runs as `mcp test -- -x <tool> <arguments>`, answering as that command answers.
- [ ] **Given** rows with plain names (`starter`, `tuned`, `reviewer`, `quiet`, `tools`), **then** every act leaves what it leaves today, every ask reads as today, and `t` opens on `tools ` (`test: tools ▏`).
- [ ] **Given** each argv list the four views build, **then** a row's name comes after the act's options and a `--`, and no option comes after the `--`.

## Constraints

- **The act is the command** ([ADR 0010](../../../adrs/cli/the-shell-is-a-mode.md); MILESTONES, the 37f and 37g entries; [DEVELOPER.md → Adding a TUI view](../../../assistant/cli/DEVELOPER.md#adding-a-tui-view), steps 2 and 5). Each act still runs the command itself as the shell's child, and leaves exactly what `apogee <verb> [options] -- <name>` leaves on a twin install. No act moves in process.
- **Options, then `--`, then the name.** After `--`, CLI11 reads every word as an operand, so an option placed there is a stray positional (exit 109). An option that collects several values eats a `--` placed straight after it. None of the five verbs has one, and none may be put directly before the `--`.
- **argv only, no shell** (the item's own rule). `child_answer` hands the words to `platform::start_child` as given. No quoting is added to argv: `--` is the fix there.
- **The asks' words are CLI-M48's.** The confirm texts (`tui_training.cpp:186-191`, `:272-275`; `tui_agents.cpp:136-140`) change only through CLI-M48's renderer. This item's tests pin no ask for a dash-led name, and the plain-name pins stay as they are.
- **No new quoter.** The `t` prefill spells the key with CLI-M48's `shell_word`, and this item writes no quoting of its own. CLI-M48 folds the five private quoters that exist into that one function.
- **The CLI owns the name rules** (the same rule APP-M21 keeps for the Mac app). The shell adds none and passes a listed name byte for byte. Whatever a verb then refuses (57e's editor refusal for an odd config key; CLI-M56's create rule, which judges new names only) reaches the person in the verb's words.
- **Plain names see nothing new.** For a name that doesn't start with `-` and that `shell_word` leaves bare (every name the CLI's create verbs make for these four views), no byte the person sees changes: not the asks, not the answers, not the `t` prefill. Only a hand-written MCP server key that needs quoting (a blank, a quote, a `$`) opens `t` differently, quoted once CLI-M48's `shell_word` is wired in (Open questions).
- **Existing names keep working** ([ADR 0006](../../../adrs/cli/backwards-compatibility.md)). A `-x` dataset already on disk stays deletable from the view after CLI-M56 refuses new ones, so the fixture writes the file directly, not through `datasets create`.
- **Hermetic tests on the existing pattern.** A `CliHome` per test, children of the built binary (`APOGEE_EXECUTABLE`, skipped where the build made none), no PTY, no live install. A live look, if one is taken, runs `apogee-dev` over a throwaway `APOGEE_HOME`, never `apogee` over `~/.apogee` ([CLAUDE.md → Development runs on the dev channel](../../../assistant/cli/CLAUDE.md#development-runs-on-the-dev-channel)).

## Technical approach

Paths under `lib/src/cli/`.

| File | Change |
|---|---|
| `source/presentation/cli/tui_training.cpp` | The Train view's `R` (`:195-197`) runs `{"train", "rollback", "--", <backend>}`. The Datasets view's `x` (`:278-279`) runs `{"datasets", "delete", "--yes", "--", row.key}`. The confirms (`:186-191`, `:272-275`) are left to CLI-M48. |
| `source/presentation/cli/tui_agents.cpp` | The Agents view's `x` (`:143`) runs `{"agents", "delete", "--", row.key}`. The MCP view's `e` and `d` (`:227`, `:233`) run `{"mcp", "enable", "--", row.key}` and `{"mcp", "disable", "--", row.key}`. `t`'s prefill (`:212`) is `-- ` when the key starts with `-`, then the key spelled by CLI-M48's `shell_word`, then a space. Every name `mcp create` can make (letters, digits, `_` and `-`: `business/scaffold/mcp_server.cpp:221-227`) is spelled as itself, so `tools ` stays byte for byte. The confirm (`:136-140`) is left to CLI-M48. |
| `tests/presentation/cli/tui_training_test.cpp` | The Datasets `-x` and `-h` cases and the Train `-x` rollback, under Testing. |
| `tests/presentation/cli/tui_agents_test.cpp` | The Agents `-x` delete, the MCP `-x` `e`, `d` and `t` cases, under Testing. |

The argv, before and after:

| View · key | Today | After |
|---|---|---|
| Datasets · `x` | `datasets delete -x --yes` | `datasets delete --yes -- -x` |
| Train · `R` | `train rollback -x` | `train rollback -- -x` |
| Agents · `x` | `agents delete -x` | `agents delete -- -x` |
| MCP · `e` / `d` | `mcp enable -x` / `mcp disable -x` | `mcp enable -- -x` / `mcp disable -- -x` |
| MCP · `t` (the input row) | `-x ▏` | `-- -x ▏` |

The `--` goes into the argv for every name, not only a dash-led one. No one sees the argv, CLI11 reads a plain name the same after `--`, and one shape per act leaves nothing to decide per row. No new helper is added: each act stays one literal list, so its order is visible where it is built. `child_answer`, `self_command` and `ListAsk::prefill` keep their signatures.

The item consumes two decisions. The spelling of a word for a reader of POSIX quoting is CLI-M48's (`shell_word` in `source/data/contracts/suggested_command.h`, reachable from Presentation). The words of every printed ask are CLI-M48's too. Whichever of the two items lands second wires `t`'s prefill to `shell_word`. Built first, this item puts the key into the prefill as today, with `-- ` before a dash-led one.

**Reference.**
- APP-M21's `Argv.line`, the Mac app's builder for the same order (the command, its options, then `--` and the names), and its measured lines for `datasets delete --yes -- -h`.
- The twin-home oracle of the existing child tests: `tui_training_test.cpp:327-372`, whose rollback leaves `train rollback`'s config and ledger, and `tui_agents_test.cpp:240-276`, whose delete leaves `agents delete`'s config. This item's cases compare against a twin that ran the command with `--`.
- CLI-M45's completion, which offers a dash-led name only after `--`. That is the same reading of the parser, from the offering side.

**Docs when it ships:**
- DEVELOPER.md's `tui_training` and `tui_agents` rows (`:299-300`): `x` as `datasets delete --yes -- <name>`, `R` as `train rollback -- <backend>`, `x` as `agents delete -- <name>`, `e`/`d` as `mcp enable|disable -- <name>`, and `t` on `-- <server> ` for a name starting with `-`.
- DEVELOPER.md's test rows (`:893-894`).
- DEVELOPER.md's "Adding a TUI view", step 2 (`:1277`): a row's name handed to a child goes after the act's options and a `--`.
- MILESTONES: a dated entry, recorded as a follow-on to 37f and 37g.

## Testing

Each case fails on today's binary unless marked as a guard. The child cases run against the built binary and skip where the build made none, as the existing ones do.

- **The bug as a fixture** (`tui_training_test.cpp`). A Home whose `training/datasets/-x.jsonl` is written directly, the Datasets view on the built binary.
  - Move to `› -x`, press `x`, then `y`.
  - The frame holds `will remove:` and `removed.`, and holds neither `name is required` nor `exit 106`. `-x.jsonl` no longer exists.
  - Today the frame shows `name is required` and `exit 106`, and the file stays.
- **The false success** (`tui_training_test.cpp`). The same for `training/datasets/-h.jsonl`: the frame holds `removed.` and no `Usage: apogee datasets delete` line, and `-h.jsonl` is gone.
- **The rollback** (`tui_training_test.cpp`). The setup is a hand-written JSONC backend `-x`, and a two-version ledger and weights for `-x` laid out as `seed_versions` lays them out for `tuned`.
  - A twin home runs `train rollback -- -x`. The view presses `R`, then `y`.
  - The config and `training/versions/-x.json` equal the twin's, each home's path written `HOME`.
  - The frame holds the twin's first answer line, and no `name is required`.
- **Agents and MCP** (`tui_agents_test.cpp`). A JSONC Home with an agent `-x` and an MCP server `-x` (`/bin/false`, `enabled: false`).
  - Twins run `agents delete -- -x`, `mcp enable -- -x` and then `mcp disable -- -x`.
  - The view's `x`, `e` and `d` leave the same config and draw the same first answer line, each home's path written alike. None draws `name is required`.
- **`t` on a dash-led server** (`tui_agents_test.cpp`). A server `-x` running Apogee's own tools (`command: <binary>`, `args: [__mcp-tools]`, as the existing `t` case sets up `tools`).
  - Pressing `t` on `› -x` shows `test: -- -x ▏`.
  - The ask with `-- -x read_file '{"path": "note.txt"}'` returns what `mcp test -- -x read_file '{"path": "note.txt"}'` writes, its stderr then its stdout, holding `hello from the note`.
- **The plain names (guard).** Every existing case in both files passes unchanged, including the asks' words and `test: tools ▏`.
- **Gate:** `lib/scripts/cicd.sh --app cli --test`, clang-format, and the MinGW scan of the changed files.

## Open questions

| Question | Status | Answer |
|---|---|---|
| Does every argv carry the `--`, or only one whose name starts with `-`? | `[default]` | Every one. No one sees the argv, CLI11 reads a plain name the same after `--`, and one shape per act leaves nothing to decide per name (a row named `--` included). |
| Does `t`'s prefill spell every key with `shell_word`, or only a dash-led one? | `[default]` | Every key. `shell_word` leaves a word in its bare set as it is, and every name `mcp create` makes is in that set, so a plain key opens as today. A hand-written key holding a blank or a quote then reads back as itself through `line_tokens`, instead of splitting into a server and a tool. |
| Built before CLI-M48, how does the prefill spell a key that needs quoting? | `[default]` | As today: the key raw, with `-- ` before it when it starts with `-`. No private quoter is written here. Whichever item lands second wires the prefill to `shell_word`. |
| What do the asks say for a dash-led row (`Delete dataset -x ('apogee datasets delete -x')?`)? | `(consumed decision)` | CLI-M48, whose renderer spells every printed command, the TUI's confirm texts included: CLI-M48 routes the Datasets confirm (`tui_training.cpp:272-275`), and CLI-M61 the Train and Agents confirms (`tui_training.cpp:186-191`; `tui_agents.cpp:136-140`). |
| Should a new dataset name starting with `-` be refused? | `(consumed decision)` | CLI-M56, whose create rule refuses a leading `-` for new names and keeps existing ones deletable (ADR 0006). This item makes an existing one deletable from the shell. |
| What way out does a hand-written agent, MCP server or backend get once the verb refuses it? | `(consumed decision)` | CLI-M50. It gives a backend the rename door, and records that agents and MCP servers have no rename verb. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-26 | **The diagnosis.** The five argv lists and the `t` prefill put a row's name where CLI11 reads a dash-led word as an option, so the act never reaches its verb. Specced from the remedy-names investigation, a sandboxed investigation checked by two skeptics and a critic, which found it as an aside and measured it (Evidence). | the spec |
| 2026-11-26 | Placed in **Maintenance** as M51, by the user's call to spec the CLI asides of the APP-M21 and APP-M22 probes as maintenance items from M48, grouped only where they share one seam. **This one is its own seam**: the TUI's argv builders, where nothing is printed and nothing passes a shell. It is neither the printed-command renderer (CLI-M48–M50) nor the dataset name rules (CLI-M55, CLI-M56), and no pending CLI item owned it. | the user (placement) · the spec (seam) |
| 2026-11-26 | **Not gated on CLI-M48.** The argv fix needs no renderer. Only the prefill's spelling uses `shell_word`, and whichever item lands second wires it. CLI-M48 alone changes the asks' words. | the spec |
| 2026-11-26 | **Not gated on CLI-M56 either.** That item refuses new dash-led dataset names. Existing ones keep being listed, so the shell must still delete them, and this item's fixture writes the file directly so it holds once CLI-M56 ships. | the spec |
| 2026-11-26 | **No recorded intent is reversed.** 37f's and 37g's "every act is the command run as the shell's child" is kept, and made true for every name. | the spec |

## Out of scope

- **The asks' words**, such as `('apogee datasets delete -x')`, and the program word they print: [CLI-M48](../../../assistant/cli/MILESTONES.md#2026-11-27--a-suggested-command-runs-as-printed-for-every-name-the-cli-makes-maintenance-item-m48-a-suggested-command-runs-as-printed-for-every-name-the-cli-makes) (shipped 2026-11-27) for the Datasets ask, [CLI-M61](every-printed-command-goes-through-the-renderer.md) for the Train and Agents asks, and [CLI-M49](../../../assistant/cli/MILESTONES.md#2026-11-27--a-printed-command-names-the-binary-that-printed-it-maintenance-item-m49-a-printed-command-names-the-binary-and-root-that-printed-it) (shipped 2026-11-27).
- **Creation rules for dataset names**: [CLI-M56](a-new-dataset-name-is-one-every-verb-reads.md) for new names, and [CLI-M55](a-dataset-is-named-not-pathed-at-every-reader.md) for the readers.
- **A way out for a hand-written entry the editors refuse**: [CLI-M50](../../../assistant/cli/MILESTONES.md#2026-11-27--a-refused-backend-name-gets-the-rename-that-reaches-it-maintenance-item-m50-a-backend-name-the-editors-refuse-gets-the-rename-door-that-reaches-it) (shipped 2026-11-27) for backends. The gap for agents and MCP servers, which have no rename verb, is recorded there.
- **The Mac app's argv**: [APP-M21](../../../assistant/darwin/MILESTONES.md#2026-11-26--listed-name-reaches-the-verb-maintenance-item-m21-a-listed-name-reaches-the-verb-as-listed) for its Training datasets delete and the other rows where a person meets harm today, and [APP-M23](../../../assistant/darwin/MILESTONES.md#2026-11-27--every-name-the-app-passes-is-read-as-a-name-maintenance-item-m23-every-name-the-app-passes-is-read-as-a-name) for every other site.
- **The TUI exec line's handling of a leading `apogee`.** `exec_words` (`tui_runner.cpp:73-88`) doesn't strip it, so a whole pasted command fails there (`c:w01`, `apogee apogee graph stats plain`, exit 109 `The following argument was not expected: apogee`). It was left as an aside, and no item owns it.
- **Lines the person types**: the asks' arguments, the tool and JSON after `t`'s prefill, and the exec line. A `--` typed there works as at a prompt.

## Links

- **Blocked by:** — · **Blocks:** —
- **Relates to:**
  - [CLI-M48](../../../assistant/cli/MILESTONES.md#2026-11-27--a-suggested-command-runs-as-printed-for-every-name-the-cli-makes-maintenance-item-m48-a-suggested-command-runs-as-printed-for-every-name-the-cli-makes) (shipped 2026-11-27), which owns `shell_word`, which the `t` prefill consumes, and the Datasets ask's words; [CLI-M61](every-printed-command-goes-through-the-renderer.md), which owns the Train and Agents asks' words;
  - [CLI-M50](../../../assistant/cli/MILESTONES.md#2026-11-27--a-refused-backend-name-gets-the-rename-that-reaches-it-maintenance-item-m50-a-backend-name-the-editors-refuse-gets-the-rename-door-that-reaches-it) (shipped 2026-11-27), the way out a hand-written backend meets once its row's act reaches the verb;
  - [CLI-M55](a-dataset-is-named-not-pathed-at-every-reader.md), whose read rule keeps every listed dataset, `-h` included, readable and deletable by name, and which leaves the shell's `--` to this item (its Open questions);
  - [CLI-M56](a-new-dataset-name-is-one-every-verb-reads.md), whose create rule stops new dash-led datasets, while existing ones still need this item;
  - [CLI-M45](../../../assistant/cli/MILESTONES.md#2026-11-27--a-collection-is-named-not-pathed-at-every-reader-maintenance-item-m45-a-collection-is-named-not-pathed-at-every-reader) (shipped 2026-11-27), whose completion offers a dash-led name only after `--`;
  - [APP-M21](../../../assistant/darwin/MILESTONES.md#2026-11-26--listed-name-reaches-the-verb-maintenance-item-m21-a-listed-name-reaches-the-verb-as-listed), the Mac app's version of this bug class, and [APP-M23](../../../assistant/darwin/MILESTONES.md#2026-11-27--every-name-the-app-passes-is-read-as-a-name-maintenance-item-m23-every-name-the-app-passes-is-read-as-a-name), its other sites;
  - [APP-M22](../../../assistant/darwin/MILESTONES.md#2026-11-26--jobs-notes-reach-the-person-maintenance-item-m22-a-jobs-notes-reach-the-person), whose probes' CLI asides, with APP-M21's, started the investigation that found this.
- **Follows from:** 37f and 37g (MILESTONES, 2026-10-10), the views and their "the command run as the shell's child" record; [ADR 0010](../../../adrs/cli/the-shell-is-a-mode.md), the shell is a mode.
