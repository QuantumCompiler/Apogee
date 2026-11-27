# CLI-M50 · A backend name the editors refuse gets the rename door that reaches it

| Field | Value |
|---|---|
| **Type** | 🐞 Bug |
| **Project** | CLI |
| **Status** | 🟢 Ready |
| **Fix version** | Maintenance (any time) |
| **Component** | Remedies › every door that names `config set-backend` or `config delete-backend` on a configured backend: `check`, `models delete`, `models pull` and `convert`, `config set-backend`'s adapter refusals, `train promote`, and a load's adapter and drafter refusals |
| **Labels** | `remedies` `backends` `config` `check` |
| **Epic** | — (maintenance has no track) |
| **Blocked by** | — |
| **Blocks** | — |
| **Reporter** | The investigation of the APP-M21 and APP-M22 probes' CLI asides (2026-11-26): found by its sandboxed investigator, then two skeptics and a critic the same day |
| **Created** | 2026-11-26 |

---

## Summary

Some backends have a name outside the config's name rule: written by hand, or kept from a config older than 2026-10-25. Apogee prints doors for them that the editors refuse. `models delete` and `check` name `config delete-backend` and `config set-backend` on such a name, and in the JSONC config `config init` writes, every config editor refuses it, even after `--` (57e), pointing at `config rename-backend` without its arguments. The item makes every door that names one of those two editors on such a name lead with the rename that reaches it, `rename it first: apogee config rename-backend -- -x <new name>, then apogee config delete-backend <new name>`, and leaves every plain name's door byte for byte.

## Description

### User story

**As** someone whose config holds a backend named `-x`, `a b` or `qwen@home`, written by hand or by an Apogee older than 55f,
**I want** the door `check` or `models delete` prints for it to be one that runs,
**so that** following Apogee's advice repoints or removes that backend, instead of ending at another refusal.

### Steps to reproduce

The sandbox, 2026-11-26, with the built binary `apogee 0.2.0 (33ea685d, 2026-11-22, macos-arm64)`: a throwaway `APOGEE_HOME` and `HOME` three levels deep, `env -i`, stdin from `/dev/null`, `config init`, mock backends only.

1. Hand-edit `config.json` (the JSONC file `config init` writes) to add a mock backend `-x` and a llamacpp backend `-gone` whose `model_path` is `/nonexistent-m48/x.gguf`. `models list` lists both.
2. Run `apogee models delete --yes -- -x`.
3. Run `apogee check`, then `apogee check --output-format json`.
4. Run each printed door the way CLI-M48 will print it, with the options first and `--` before the name: `apogee config delete-backend -- -x`, and `apogee config set-backend --model-path <dir>/m.gguf -- -gone`.
5. Run `apogee config rename-backend -- -x renamed-x`.

### Actual result

2. Exit 1: `apogee models: backend '-x' (mock) has no model file to delete -- remove the backend itself with 'apogee config delete-backend -x'`.
3. Exit 1. The row is ` FAIL  backend: -gone  model_path does not exist: /nonexistent-m48/x.gguf`, and its `run:` line is `apogee config set-backend -gone --model-path <an existing .gguf> to repoint it, or apogee config delete-backend -gone to remove it`. The JSON row's `remedy` holds the same words.
4. Each exits 1: `apogee config: backend '-x' is not one both config formats can edit -- a name is letters, digits, '_', '-', '.' and '/', first neither '-' nor '.', and not null; rename it with 'apogee config rename-backend'`, and the same for `-gone`. No door runs as printed.
5. Exit 0: `renamed backend '-x' to 'renamed-x' in <home>/config/config.json, every reference to it with it`.

### Expected result

2. Exit 1, with one stderr line: `apogee models: backend '-x' (mock) has no model file to delete -- to remove the backend itself, rename it first: apogee config rename-backend -- -x <new name>, then apogee config delete-backend <new name>`.
3. The `-gone` row's `run:` line and its JSON `remedy` both read `apogee config rename-backend -- -gone <new name> && apogee config set-backend <new name> --model-path <an existing .gguf> to repoint it, or apogee config rename-backend -- -gone <new name> && apogee config delete-backend <new name> to remove it`.
4. Each printed command runs as printed once `<new name>` is filled in: the rename exits 0, and the door then lands on the new name.

### Background

A backend's name is a key in the config. Since 55f (8d5aec51, 2026-10-25), a new key must be one both config formats edit: `yaml_plain_key`'s set, which is letters, digits, `_ - . /`, first neither `-` nor `.`, and not null. A key outside that set is still read. It may have been written by hand, or it may come from a config written before that date (ADR 0006: an older config loads). 55f recorded how such a key is reached: "existing odd names reached by `rename-backend`".

The editors and the doors grew apart:
- **2c50f86c (2026-10-03, v0.1.3).** `models delete`'s refusal for a backend with no model file names `config delete-backend <name>` (`models_pull.cpp:869-874`).
- **00635d8a (2026-10-19, M14).** `check`'s two doors for a dangling entry (`check.cpp:539-544`), and `backend_doors` for an entry `models delete` keeps (`models_pull.cpp:416-419`). The record: "The doctor's doors run as printed".
- **8d5aec51 (2026-10-25, 55f).** The add rule. A refused add already says "a backend already named otherwise is reached by 'apogee config rename-backend'".
- **57e (2026-11-01).** Every set and delete that does not find such a name (in a JSONC config, every one) says it is "not one both config formats can edit", and for a backend adds `rename it with 'apogee config rename-backend'`. Before 57e, those edits said `not found in config` while `config get backends` listed the entry.

So since 57e the editors point to the rename, but no door moved. Each door still names the editor that refuses the name, and nothing on the door's path asks the rule.

APP-M21's probes found it from the Mac app's side. Its Out of scope notes that `models delete` suggests `config delete-backend -x`. Its Evidence 7 notes that the app's own way out, the Models rename row, needs `--`. The investigation of those CLI asides found that the door fails twice:
- **As printed, it doesn't parse.** CLI-M48 fixes that for every printed command.
- **Once it parses, the editor refuses the name.** This item fixes that.

The investigation split the two: rendering a door is CLI-M48's change, and choosing which door to print is this one.

The design is one choice, made by name, in one place. Where a door names `config set-backend` or `config delete-backend` on a backend whose name is outside the rule, it names `config rename-backend <name> <new name>` first, then the same door on `<new name>`. A plain name's door is today's, byte for byte. CLI-M48's renderer prints every command, so the rename door gets its `--` before a dash-led name, and quotes a name holding a space or a `;`.

### Evidence

All measured 2026-11-26 with the built binary `apogee 0.2.0 (33ea685d, 2026-11-22, macos-arm64)`, channel release. Every run used `env -i`, with `PATH`, `HOME` and `APOGEE_HOME` set to a throwaway folder three levels deep, stdin from `/dev/null`, and mock backends only. The stamp names HEAD~1, but the binary carries HEAD 5b8927c9's strings (the critic's `c:v00`). 5b8927c9 touched none of the files whose strings this item quotes (`models_pull.cpp`, `check.cpp`, `config_edit.cpp`). Unprefixed labels are the investigator's records, `c:` labels are the critic's, and the skeptics' are named. Source lines are read at HEAD.

1. **`models delete` names an editor that refuses the name.**
   - The investigator's `m01`: `models delete --yes -- -x`, over a hand-written mock backend `-x`, exits 1 with `apogee models: backend '-x' (mock) has no model file to delete -- remove the backend itself with 'apogee config delete-backend -x'`. The cause skeptic's `m01` re-ran it byte for byte.
   - As printed, the door doesn't parse (`m02`: exit 106, `name is required`). That half is CLI-M48's.
   - The site is `models_pull.cpp:869-874`.
2. **With `--`, every editor refuses the name.**
   - `c:e05`: `config delete-backend -- -x` exits 1 with `apogee config: backend '-x' is not one both config formats can edit -- a name is letters, digits, '_', '-', '.' and '/', first neither '-' nor '.', and not null; rename it with 'apogee config rename-backend'`.
   - The same words come back for `a;b` and `it's` (`d2`, `d3`), for `--help` (the fix skeptic's `h01`), and on the cause skeptic's re-runs (`m05`, `m06`).
   - The refusal is `missing_entry` (`config_edit.cpp:687-699`, 57e). A set or delete that finds no entry under a name outside the rule says so "whether or not the config holds it", and only a backend gets the rename pointer (`:693-695`; MILESTONES' 57e entry: "the rename pointer only for a backend, the one with a rename verb").
   - In a JSONC config the editor never finds such a key, because the bridge quotes it. Two tests pin this: `config_jsonc_test.cpp:579-606` and `:761-787`.
3. **`check`'s doors reach the same refusal.** The investigator's `ck1` and `j04` ran `check` and `check --output-format json` over a hand-written llamacpp `-gone` whose `model_path` is `/nonexistent-m48/x.gguf`.
   - The text row and its `run:` line: ` FAIL  backend: -gone  model_path does not exist: /nonexistent-m48/x.gguf` / `run: apogee config set-backend -gone --model-path <an existing .gguf> to repoint it, or apogee config delete-backend -gone to remove it`. The JSON row's `remedy` carries the same words, and the `a b` row reads the same with `a b`.
   - With a real file in the blank:
     - as printed, `o1` exits 106 (`name is required`);
     - with `--` straight after `set-backend`, `o2` exits 109 (`The following arguments were not expected: …/m.gguf --model-path --`);
     - with the option first, then `--`, which is how CLI-M48 will print it, `o3` reaches the verb and exits 1 with 57e's refusal for `-gone`.
   - The first two are CLI-M48's. The site is `check.cpp:539-544`.
   - The cause skeptic's `ck1`, `j01` and `o1`–`o3` matched.
4. **`rename-backend` reaches the name.**
   - `d4`: `config rename-backend -- 'my model' my-model` exits 0 with `renamed backend 'my model' to 'my-model' in …/config.json, every reference to it with it`.
   - The cause skeptic's `m07` renamed `-x` to `renamed-x`, and the fix skeptic's `k03` renamed `-x` to `x2`. Each exited 0.
   - `rename_backend` takes any old name and holds the new one to the rule (`config_edit.cpp:3050-3075`: "The old name may be any").
5. **Root cause: no door site asks the rule.**
   - grep finds no `rename-backend` and no `yaml_plain_key` in `check.cpp` or `models_pull.cpp` (read at HEAD, 2026-11-26).
   - Each door joins the name to `"apogee config delete-backend "` or `"apogee config set-backend "`.
   - The rule is `yaml_plain_key` (`config_yaml.cpp:206-217`), and `missing_entry` refuses every editor on a name outside it.
6. **Other kinds have no way out.**
   - `c:e04`: `config delete-graph -- -x` exits 1 with `apogee config: graph '-x' is not one both config formats can edit -- a name is …, and not null`, with no pointer.
   - Suites, agents and MCP servers have no rename verb.
   - How hand-written suites, agents and MCP servers reach that refusal was not measured. `c:e01`–`e03` stopped earlier, at `no agent named '-x'`, `no 'suites:' section in this config` and `no 'mcp_servers:' section in this config`.
7. **How names get here.**
   - New ones have been refused at add since 55f. `kb1`: `config add-backend --type mock -- -x` exits 1 with `apogee config: backend name '-x' is not one both config formats can edit -- …; a backend already named otherwise is reached by 'apogee config rename-backend'`. `kb2`–`kb5` refuse `my notes`, `a;b`, `it's` and `$HOME` the same way (`append_entry`, `config_edit.cpp:715-724`).
   - So a name reaches these doors only through a hand edit or a config written before 2026-10-25. Such a config loads: `h02`'s `models list` shows every hand-written name.
8. **The records promise doors that run.**
   - M14: "The doctor's doors run as printed" (MILESTONES.md:3948).
   - 55f: "names are restricted at add to the set both formats edit, with existing odd names reached by `rename-backend`" (MILESTONES.md:11232).
   - The Adapters rows: "Each failure names its doors, each a command that runs as printed" (`reference/cli/training.md:135-136`).
9. **A YAML config differs.** There the line editor finds a hand-written odd key and edits it (`config_edit_test.cpp:2344-2353`, `qwen@home`). Every probe ran on the JSONC `config.json` that `config init` writes.
10. **The rename must name the key as stored.** `Config::backends` folds case (`config.h:1169`), so `models delete -- -X` finds `-x`. `rename-backend` matches the old key exactly until CLI-M54 ships. CLI-M54's finding: `config rename-backend Foo renamed3`, next to a backend `foo`, says `no backend named 'Foo'`.
11. **The same door elsewhere, found by reading at HEAD.** A grep for `set-backend` and `delete-backend` joined to a backend's name finds the same door at these sites. None was run with an odd name. Four of them (`backend_doors` and the three registration refusals) are among the six the investigation named; the other ten are new.
    - `check.cpp:526-528`: a llamacpp entry with no `model_path` (`set-backend <name> --model-path <file.gguf>`).
    - `check.cpp:1096-1098`: the Adapters rows' `set_backend` and `pin_all`, behind every `set-backend` door those rows name (`:1158-1159`, `:1181-1182`, `:1207`, `:1217-1218`, `:1253-1256`).
    - `models_pull.cpp:416-419`: `backend_doors`, said for each entry `models delete` keeps (`:424-427`).
    - `models_pull.cpp:2867-2870`, `:2911-2914` and `:3195-3198`: the registration refusals of `models pull` and `models convert`.
    - `store_references.cpp:400-427`: `clear_door`, said for each reference `models delete` keeps (`models_pull.cpp:376-380`).
    - `backend_fields.cpp:308-320`: `set_backend_command` and `adapters_take_off`, the doors in 55e's refusals and in a merged promote's (`:101-112`, `:134-141`, `:331-333`). By reading, 55e's refusals come after a trial of the same edit (`:98-100`), so in a JSONC config an odd name gets 57e's refusal there instead (returned by the `catch` at `:145-147`); they reach such a name in a YAML config. The merged promote's (`train.cpp:814-816`) reads the loaded config and makes no trial edit first.
    - `train.cpp:552-554` and `:1054-1056`: `train promote`'s refusals.
    - `adapters.cpp:138-140`: `retake`, behind a load's adapter refusals (`:149`, `:155`, `:160`, `:316`, `:334`).
    - `mlx_local.cpp:569-570`: the drafter's remedy, which `check` shows as its row's `remedy` (`check.cpp:656`).
12. **Who reads these doors.**
    - `check`'s rows reach:
      - its text and `--output-format json`;
      - the TUI's Doctor view (`tui_doctor.cpp:61`, `:90`);
      - `mcp serve`'s check document (`mcp_serve.cpp:277-285`);
      - the Mac app's Settings › Health, as selectable text (`SettingsView.swift:279-280`).

      All of them read `run_checks`.
    - `backend_fields.cpp` is the one core that `config set-backend` and `PATCH /v1/admin/backends/{id}` share (`backend_fields.h:11-13`).
    - The Mac app offers `config rename-backend` on every Models row (`ModelsViewModel.swift:68-70`). APP-M21 makes that row pass `--`.
13. **Not pinned.** No test in `check_test.cpp` or `models_pull_test.cpp` plants a backend outside the rule. grep finds no `rename-backend` in either file.
14. **Blast radius.**
    - **Sites:** the six the investigation named (`models_pull.cpp:416-419`, `:869-874`, `:2867-2870`, `:2911-2914`, `:3195-3198`, and `check.cpp:539-544`), measured at `:869-874` and `check.cpp:539-544`. Then the ten more in point 11, found by reading.
    - **Who is reached:** only a backend written by hand or kept from before 55f (point 7).
    - **The harm, after CLI-M48:** a refusal, not a wrong target. Today an unquoted `a;b` door deletes backend `a` (the investigator's `m09`), which CLI-M48 fixes. Once quoted, every such door ends at 57e's refusal.
    - **The registration refusals** of `models pull` and `models convert` are reached only when the name the chain registers matches a hand-written entry outside the rule. That was not measured. Once the entry is renamed away, `check_registrations`' dry run still refuses the chain's own registration under that name (`models_pull.cpp:2846-2858`, 55f's rule). So those sites' "or register this one by hand under another name" stays the way to register the model.
15. **What already works**, and must stay:
    - `rename-backend` reaches the name (point 4), and the new name is one every editor takes.
    - 57e's refusal names `rename-backend`, though without its arguments (point 2).
    - After CLI-M48, a pasted door reaches that refusal and nothing else.
    - A plain name's doors run as printed, and the tests pin them: `check_test.cpp:243-266`, and `models_pull_test.cpp:185-196`, `:578-588` and `:685-709` (the last runs the repoint door with a real file in its blank).

## Acceptance criteria

- [ ] **Given** a hand-written backend `-x`, **when** `models delete --yes -- -x` refuses it, **then** its remedy reads `rename it first: apogee config rename-backend -- -x <new name>, then apogee config delete-backend <new name>`. The full line is `apogee models: backend '-x' (mock) has no model file to delete -- to remove the backend itself, rename it first: apogee config rename-backend -- -x <new name>, then apogee config delete-backend <new name>`. Once `<new name>` is filled in, both steps run as printed: the rename exits 0, the delete exits 0, and the config holds neither name. Today `m01`'s door, with CLI-M48's `--`, reaches 57e's refusal.
- [ ] **Given** `check`'s row for a hand-written `-gone` whose model is gone, **then** the rename door comes before both of its doors, in the text `run:` line and the JSON `remedy` alike: `apogee config rename-backend -- -gone <new name> && apogee config set-backend <new name> --model-path <an existing .gguf> to repoint it, or apogee config rename-backend -- -gone <new name> && apogee config delete-backend <new name> to remove it`. Each door, run alone with its blanks filled, exits 0 at every step. Today `o3` exits 1 with 57e's refusal.
- [ ] **Given** a backend named `a b`, **then** its doors lead with `apogee config rename-backend "a b" <new name>` (quoted by CLI-M48, with no `--`), and each step runs.
- [ ] **Given** a configured backend outside the rule at any other site in Evidence 11 (`check`'s no-`model_path` row and Adapters rows, `backend_doors`, `clear_door`, the registration refusals, `set_backend_command` and `adapters_take_off`, `train promote`'s refusals, `retake`, the drafter's remedy), **then** every door naming `config set-backend` or `config delete-backend` on it leads with the rename. A `remedy` field chains the rename with ` && `, and a sentence says `rename it first: …, then …`.
- [ ] **Given** a backend stored as `-x` and a command that names it `-X`, **then** the rename door names `-x`, the key as stored.
- [ ] **Given** a YAML config holding a hand-written `qwen@home`, **then** its doors lead with the rename too, and each step runs.
- [ ] **Given** a plain backend name, **then** every door is byte-identical to today. The existing pins and goldens pass unedited.
- [ ] **Given** any of these, **then** a refusal stays one stderr line beginning `apogee <verb>: `, and no new line or stream appears.

## Constraints

- **A door runs as printed** (M14, MILESTONES.md:3948; `reference/cli/training.md:135-136` for the Adapters rows). Every step a door names runs once its blanks are filled, on the state it was named for. Under [ADR 0006](../../../adrs/cli/backwards-compatibility.md), a record's guarantee holds until a dated revision.
- **Follow 55f and 57e.** Existing odd names are reached by `rename-backend` (MILESTONES.md:11232). The editors' refusal and the add rule stay as they are, and no editor learns to edit an odd JSONC key. The new name is one `rename-backend` takes (`config_edit.cpp:3054-3057`).
- **Plain names keep today's doors, byte for byte.** Every existing pin passes unedited.
- **One choice, by name, in one place.** No site tests `yaml_plain_key` itself. The choice reads neither the config nor its format.
- **CLI-M48 renders every command.** This item chooses the commands, and CLI-M48's renderer prints them: `--` before a dash-led name, a quoted name where a shell would split it, and `<new name>` as a verbatim placeholder. No command string is built by hand.
- **The rename names the key as stored.** That is the config's own spelling, not the typed one, because `rename-backend` matches the old key exactly until [CLI-M54](rename-and-get-find-an-entry-by-its-folded-name.md) ships.
- **The `remedy` field keeps its documented shape** (`reference/cli/machine-mode.md:1711-1713`, written for the Adapters rows; the Config row's two doors take the same shape). Doors are `, or `-joined, each a command line before ` to <what it does>`. The rename is chained into each door with ` && `, as `check`'s re-promote door already chains two commands (`check.cpp:1064`).
- **The house voice is unchanged.** A refusal stays one stderr line beginning `apogee <verb>: ` (CLI-M46's constraint; [APP-M22](../../../assistant/darwin/MILESTONES.md#2026-11-26--jobs-notes-reach-the-person-maintenance-item-m22-a-jobs-notes-reach-the-person) keeps exactly those lines as a run's notes).
- **Mode parity** ([ADR 0002](../../../adrs/cli/mode-parity.md)). Every reader of `check`'s report, and the HTTP twin of `backend_fields.cpp`, changes through the one change, with no code per surface.
- **Tests and probes are hermetic,** under a temporary `APOGEE_HOME`. A live pass names `apogee-dev` ([the dev channel](../../../assistant/cli/CLAUDE.md#development-runs-on-the-dev-channel)).
- **No change to the Mac app, the mod or the design prototype.** They display the remedy as text.

## Technical approach

Paths under `lib/src/cli/`.

| File | Change |
|---|---|
| `source/data/contracts/backend_door.h`, `.cpp` (new), and the contracts list in `source/data/CMakeLists.txt` | `backend_edit_door`: the commands that edit a configured backend with a given door, in order. It returns the door alone for a name `yaml_plain_key` takes. Otherwise it returns `config rename-backend <name> <new name>` and then the door on `<new name>`. It comes with two renderings: a `remedy` door (joined by ` && `) and a sentence (`rename it first: <rename>, then <door>`). |
| `source/presentation/cli/models_pull.cpp` | `backend_doors` (`:416-419`), `models delete`'s refusal (`:869-874`) and the three registration refusals (`:2867-2870`, `:2911-2914`, `:3195-3198`) take their door through it, passing the key as stored (`backend->first`, `found->first`). The refusals keep "or register this one by hand under another name". |
| `source/presentation/cli/check.cpp` | The Config rows' doors (`:526-528`, `:539-544`), and the Adapters rows' `set_backend` and `pin_all` (`:1096-1098`), take theirs through it. Every Adapters door built on them then chains the rename. |
| `source/presentation/operations/backend_fields.cpp`, `store_references.cpp` | `set_backend_command` and `adapters_take_off` (`:308-320`), and `clear_door` (`:400-427`), do the same. |
| `source/presentation/cli/train.cpp` | `train promote`'s two refusals (`:552-554`, `:1054-1056`). |
| `source/data/backends/adapters.cpp`, `mlx_local.cpp` | `retake` (`:138-140`), and the drafter's remedy (`mlx_local.cpp:569-570`, a `remedy` field, so chained). |
| `tests/data/contracts/backend_door_test.cpp` (new), `tests/CMakeLists.txt` | The rule's table (Testing). |
| `tests/presentation/cli/models_pull_test.cpp`, `check_test.cpp`, `models_convert_test.cpp` | The hand-written `-x`, `-gone` and `a b` cases, the Adapters case, the YAML case and the registration case (Testing). `doors_of` (`check_test.cpp:3436-3459`, which CLI-M48 makes split outside quotes) also cuts a door at ` && `, so a case runs its commands in order. |

The interface sketch below uses CLI-M48's types (`SuggestedCommand`, `CommandPart`, `operand`, `render_all`), as M48 ships them:

```cpp
namespace apogee::harness {

/// The commands that edit configured backend `name` (the key as the config
/// stores it) with `door`, a `config set-backend` or `config delete-backend`
/// naming the operand it is given, in order: `door(operand(name))` alone when
/// `name` is one both config formats edit (`yaml_plain_key`, 55f); else
/// `config rename-backend <name> <new name>`, then `door(<new name>)`, the
/// new name a placeholder part.
[[nodiscard]] std::vector<SuggestedCommand> backend_edit_door(
    std::string_view name, const std::function<SuggestedCommand(CommandPart)>& door);

/// As one door of a `remedy` field: the commands joined by ` && `.
[[nodiscard]] std::string backend_door_remedy(const std::vector<SuggestedCommand>& commands);

/// As a sentence says it: the command alone, or
/// `rename it first: <rename>, then <door>`.
[[nodiscard]] std::string backend_door_said(const std::vector<SuggestedCommand>& commands);

}  // namespace apogee::harness
```

Each site keeps its own words around the door:
- **A `remedy` field** (`check`'s rows, the drafter's `MlxReadiness::remedy`) puts the chained door where today's command stands, followed by its ` to <what it does>`.
- **A sentence** puts the said form where today's command stands. When the name is outside the rule, the sentence drops the single quotes it set around one command, because `rename it first: …, then …` delimits the two commands itself.

For a plain name, both renderings print exactly what CLI-M48 prints for the door alone. The helper sits in `contracts` (Data) because it is reached from Data (`adapters.cpp`, `mlx_local.cpp`) and from Presentation, and `backends` already links `contracts`. It reads nothing: the name alone decides.

CLI-M48 was split on 2026-11-27. Its Pass A, which kept the id M48, moves the measured backend doors onto the renderer (`backend_doors`, `models_pull.cpp`'s delete-backend doors, `check`'s set-backend row, `backend_fields.cpp`), and is this item's gate. Its Pass B, [CLI-M61](every-printed-command-goes-through-the-renderer.md), moves the rest (`train.cpp` among them). If this item is built before Pass B, any site in the table that Pass B has not moved yet is moved onto the renderer here, with no other change, so no door is built by hand.

The item consumes three decisions:
- From **CLI-M48**: the renderer, its placeholders and its parse-back.
- From **55f**: the rule and the rename as the way to an odd name.
- From **57e**: the refusal this item stops sending people into.

**Reference.**
- `missing_entry` (`config_edit.cpp:687-699`, 57e) and `append_entry`'s refusal (`:715-724`, 55f) already say the same rule and the same way out. This item has the doors say it, with arguments.
- `check`'s re-promote door chains `train eval … && train promote …` in one door (`check.cpp:1064`), and its missing-entry door chains `config add-backend … && config set-backend …` (`:2750-2755`). That is the ` && ` precedent.
- `doors_of` and "every door an adapter row names runs as printed" (`check_test.cpp:3436-3459`, `:3507`), and "the doctor's door for a dangling backend is one the binary accepts" (`models_pull_test.cpp:685-709`). These are the replay precedents the fixtures extend.

**Docs when it ships:**
- MILESTONES: a dated entry.
- DEVELOPER.md's `cli/check_test.cpp` and `cli/models_pull_test.cpp` rows: a backend outside the name rule gets doors that lead with the rename.
- `reference/cli/machine-mode.md:1711-1713`: for a backend outside the name rule, a door chains `config rename-backend` before its command with ` && `.

## Testing

Each case runs in a `CliHome` under a temporary `APOGEE_HOME`. Each fails once CLI-M48 has landed and before this item ships, unless it is marked as a guard.

- **The bug as a fixture**, in `models_pull_test.cpp` and `check_test.cpp`. A `CliHome` whose JSONC config holds a hand-written mock `-x`, a llamacpp `-gone` and an `a b` (each `model_path` missing), and a plain `gone` beside them.
  - `models delete --yes -- -x` exits 1, and its one stderr line ends `rename it first: apogee config rename-backend -- -x <new name>, then apogee config delete-backend <new name>`. Each command is replayed through CLI-M48's parse-back, with `<new name>` filled as `x2`: through `/bin/sh` to its argv, the program word dropped, then `CliHome::run`. The rename exits 0, the delete exits 0, the config holds neither `-x` nor `x2`, and `gone` is untouched.
  - In `check --output-format json`, the `-gone` row's `remedy` equals the string in the acceptance criteria, and the text report's `run:` line matches it. Each door is cut at `, or ` and ` to `, then its ` && ` commands run in order on a fresh copy of the home. `<new name>` is `gone2`, and `<an existing .gguf>` is a `minimal_gguf` file, as `check_test.cpp`'s `DoorHome` writes. Every step exits 0, and `check` then has no failing row for that backend. The `a b` row behaves the same, quoted.
- **Plain names (guard).** `check_test.cpp:243-266` and the Adapters door case (`:3507`), and `models_pull_test.cpp:185-196`, `:578-588` and `:685-709`, all pass unedited.
- **Unit,** in `backend_door_test.cpp`:
  - plain names (`local`, `qwen-home`, `org/model`, `9b`, `true`) give the door alone, rendered byte-identical to the door by itself;
  - names outside the rule (`-x`, `--help`, `a b`, `a;b`, `qwen@home`, `null`, `.x`) give the rename (old name as given, then `<new name>`) and then the door on `<new name>`;
  - both renderings are pinned for `-x` and for `a b`.
- **Adapters rows.** A JSONC variant of `DoorHome` whose entry is `"my local"`, with a missing adapter and an unpinned one. Each Adapters door leads with the rename and, run in order, ends with the row passing.
- **A YAML config.** A hand-written `qwen@home` with a missing model: `check`'s doors lead with the rename, and replayed, the rename and then `set-backend` each exit 0.
- **The registration refusal,** in `models_convert_test.cpp` beside `:586-606`. With a hand-written JSONC entry `"my text-F16"` pointing elsewhere, `models convert org--text --register --base-name "my text"` exits 1 before the converter runs. Its door leads with the rename, and the config is unchanged. After following the door, a re-run is refused by 55f's add rule, still before the converter (guard: today's rule).
- **Stored spelling.** A config holding `-x` and a `models delete --yes -- -X` gives a rename door naming `-x`.
- **The house voice.** `models delete`'s refusal stays one stderr line beginning `apogee models: `.
- **Gate:** `lib/scripts/cicd.sh --app cli --test`, clang-format, and the MinGW scan of the changed files.

## Open questions

| Question | Status | Answer |
|---|---|---|
| What should doors say for other kinds outside the rule? Graph entries are refused with no way out (`c:e04`), and suites, agents and MCP servers have no rename verb. | `[default]` | Leave them out of this item and record the gap. If they are taken up, the door names the config file and the key to hand-edit. |
| How does a `check` row's `remedy` carry the rename? Its doors are `, or `-joined commands, each before ` to <what it does>` (`machine-mode.md:1711-1713`). | `[default]` | Each door chains it: `apogee config rename-backend <name> <new name> && <the door on <new name>>`, with CLI-M48's `--` before a dash-led name, as the re-promote door already chains two commands (`check.cpp:1064`). Every door stays one command line before ` to `, and each runs alone on the state it was named for. A sentence (`models delete`'s refusal, the kept-backend line, a load's refusal) says `rename it first: <rename>, then <door>` instead, which is the item's wording. |
| In a YAML config, the line editor finds a hand-written odd key and edits it (`config_edit_test.cpp:2344-2353`). Should the door lead with the rename there too? | `[default]` | Yes. It is one rule, by name, in both formats, as 55f and 57e say it ("not one both config formats can edit"). The door gains a step there, and every step still runs: the YAML rename is pinned at `config_edit_test.cpp:2351-2352`. No door reads the config's format. |
| `models pull` and `models convert`'s registration refusals: lead with the rename there, when the chain can't register under that name anyway? | `[default]` | Yes, one rule at every editor door: it removes the entry, as the door says. Keep "or register this one by hand under another name", which is how the model gets registered. Once the entry is gone, `check_registrations`' dry run refuses the chain's own registration under that name in 55f's words, which is today's rule. |
| How a suggested command is rendered: `--`, quoting, option order, placeholders. | `(consumed decision)` | [CLI-M48](../../../assistant/cli/MILESTONES.md#2026-11-27--a-suggested-command-runs-as-printed-for-every-name-the-cli-makes-maintenance-item-m48-a-suggested-command-runs-as-printed-for-every-name-the-cli-makes) (shipped 2026-11-27). |
| The program word and root a door begins with (`apogee`, `apogee --dev`, `apogee-dev`). | `(consumed decision)` | [CLI-M49](../../../assistant/cli/MILESTONES.md#2026-11-27--a-printed-command-names-the-binary-that-printed-it-maintenance-item-m49-a-printed-command-names-the-binary-and-root-that-printed-it) (shipped 2026-11-27). The rename door takes whatever word the renderer gives. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-26 | **The diagnosis.** The door sites never ask `yaml_plain_key`. Since 57e every config editor refuses a name outside it and points at `rename-backend` without arguments, so a door naming `set-backend` or `delete-backend` on such a name can't run as printed, even once CLI-M48 adds its `--`. Only `rename-backend` reaches the name (Evidence). | the spec |
| 2026-11-26 | **Placed in Maintenance as M50.** The user's call was to spec the APP-M21/M22 CLI asides as maintenance items from M48, grouped only where they share one seam. This is its own item because choosing which door to print is a different change from rendering one, so the investigation split it from CLI-M48 rather than drop it. CLI-M48 renders whatever door a site names. This item changes which door a site names, for a backend the editors refuse. | the user |
| 2026-11-26 | **Blocked by CLI-M48.** The rename door and the doors on `<new name>` are printed by M48's renderer. Without it, the rename door for `-x` would itself print as `rename-backend -x …`, which the parser refuses, and a name like `a;b` would run the wrong command. M48 was split on 2026-11-27, and the gate is its Pass A, which kept the id M48; any site Pass B ([CLI-M61](every-printed-command-goes-through-the-renderer.md)) has not yet moved is moved here. | the spec |
| 2026-11-26 | **Not gated with CLI-M49 or CLI-M54.** CLI-M49 changes the program word every rendered command begins with, the rename door's included, and either can land first. CLI-M54 makes `rename-backend` find a key in any case. Until it ships, this item names the key as stored, so the door runs either way. | the spec |
| 2026-11-26 | **The scope is every door naming the two editors on a configured backend.** That is the six sites the investigation named and the ten more found by reading at HEAD (Evidence 11), all through one helper. Doors on a backend not in the config (`add-backend`), and doors naming other verbs on a backend, are left out (Out of scope). | the spec |
| 2026-11-26 | **No recorded intent is reversed.** 57e's refusal and 55f's add rule stand. M14's "The doctor's doors run as printed" is what this item makes true for these names. | the spec |

## Out of scope

- **Rendering a printed command**: `--`, quoting, option order, the five quoters and `a;b` naming another target. That is [CLI-M48](../../../assistant/cli/MILESTONES.md#2026-11-27--a-suggested-command-runs-as-printed-for-every-name-the-cli-makes-maintenance-item-m48-a-suggested-command-runs-as-printed-for-every-name-the-cli-makes) (shipped 2026-11-27).
- **The editors' own refusal** keeps 57e's words, `rename it with 'apogee config rename-backend'`. It already names the way out. It is also what a person sees when the TUI's row actions ([CLI-M51](the-tuis-row-actions-reach-a-dash-led-row.md)) or the Mac app ([APP-M23](../../../assistant/darwin/MILESTONES.md#2026-11-27--every-name-the-app-passes-is-read-as-a-name-maintenance-item-m23-every-name-the-app-passes-is-read-as-a-name)'s default) reach the verb with such a name.
- **The program word and root** a door names: [CLI-M49](../../../assistant/cli/MILESTONES.md#2026-11-27--a-printed-command-names-the-binary-that-printed-it-maintenance-item-m49-a-printed-command-names-the-binary-and-root-that-printed-it) (shipped 2026-11-27).
- **Dead ends for other config kinds** (graph entries, suites, agents, MCP servers) and **a rename verb for them**. Recorded as a gap (Open questions).
- **Symphony names.** A dash-led symphony can be created once a `symphonies:` section exists, which is the made-thing-names seam ([CLI-M53](a-symphony-the-cli-writes-is-one-it-can-delete.md)).
- **Doors on a backend not in the config.** That covers `check`'s `config add-backend … && config set-backend …` for an active version with no entry (`check.cpp:2750-2755`), and `train promote`'s `add it first` (`train.cpp:545-547`). No entry exists to rename, and 55f's add rule refuses such a name in its own words.
- **Doors naming other verbs on such a backend** (`train rollback <backend>` at `check.cpp:2742`, `train promote … --as <backend>` at `:1050`). Whether those verbs reach a name outside the rule was not measured.
- **The TUI's row actions.** Passing a dash-led name after `--` is [CLI-M51](the-tuis-row-actions-reach-a-dash-led-row.md). The Config view's delete key calls the editor directly (`tui_workbench.h:23-27`), so by reading it meets 57e's refusal on such a name. That was not measured, and it is not in this item.
- **The Mac app.** Its Models rename row passing `--` is [APP-M21](../../../assistant/darwin/MILESTONES.md#2026-11-26--listed-name-reaches-the-verb-maintenance-item-m21-a-listed-name-reaches-the-verb-as-listed). It shows `check`'s remedy as text, so it needs no change here.
- **Refusing such a name at load.** An older config loads (ADR 0006).

## Links

- **Blocked by:** — (CLI-M48 [shipped 2026-11-27](../../../assistant/cli/MILESTONES.md#2026-11-27--a-suggested-command-runs-as-printed-for-every-name-the-cli-makes-maintenance-item-m48-a-suggested-command-runs-as-printed-for-every-name-the-cli-makes)): it renders the rename door and the door on `<new name>` · **Blocks:** —
- **Sibling:** [CLI-M49](../../../assistant/cli/MILESTONES.md#2026-11-27--a-printed-command-names-the-binary-that-printed-it-maintenance-item-m49-a-printed-command-names-the-binary-and-root-that-printed-it) (shipped 2026-11-27), the program word the rename door begins with; [CLI-M51](the-tuis-row-actions-reach-a-dash-led-row.md), the TUI's row actions, from the same investigation.
- **Relates to:**
  - [CLI-M54](rename-and-get-find-an-entry-by-its-folded-name.md), which makes `rename-backend` find a key in any case;
  - [APP-M21](../../../assistant/darwin/MILESTONES.md#2026-11-26--listed-name-reaches-the-verb-maintenance-item-m21-a-listed-name-reaches-the-verb-as-listed), whose aside (`models delete` suggests `config delete-backend -x`) and Evidence 7 (the app's rename row) this item answers on the CLI's side;
  - [APP-M23](../../../assistant/darwin/MILESTONES.md#2026-11-27--every-name-the-app-passes-is-read-as-a-name-maintenance-item-m23-every-name-the-app-passes-is-read-as-a-name), whose default relies on the CLI's refusal naming `rename-backend`;
  - CLI-59d (v0.2.3, [reads and commands over the network](../v0.2.3/reads-and-commands-over-the-network.md)), which relays `check --output-format json` to a paired device byte for byte, the rename door included.
- **Follows from:** M14 (00635d8a, MILESTONES 2026-10-19, "The doctor's doors run as printed"), 55f (8d5aec51, MILESTONES 2026-10-25, the add rule and "existing odd names reached by `rename-backend`") and 57e (MILESTONES 2026-11-01, the odd-name refusal); [ADR 0002](../../../adrs/cli/mode-parity.md) and [ADR 0006](../../../adrs/cli/backwards-compatibility.md).
