# CLI-M56 · A new dataset name is one every verb reads as a name

| Field | Value |
|---|---|
| **Type** | 🐞 Bug |
| **Project** | CLI |
| **Status** | 🔒 Blocked |
| **Fix version** | Maintenance (any time) |
| **Component** | Training › the doors that name a new dataset: `datasets create`, `datasets synth`, `datasets prepare`'s derived name, and `POST /v1/admin/datasets` and `/synth` |
| **Labels** | `datasets` `naming` `training` `http` |
| **Epic** | — (maintenance has no track) |
| **Blocked by** | [M55](a-dataset-is-named-not-pathed-at-every-reader.md) |
| **Blocks** | — |
| **Reporter** | The APP-M21 and APP-M22 probes' CLI asides (2026-11-26), then a sandboxed investigation, two skeptics and a critic the same day |
| **Created** | 2026-11-26 |

---

## Summary

`apogee datasets create --from empty -- -h` writes `training/datasets/-h.jsonl` and exits 0. From then on every verb reads `-h` as its help flag unless it follows `--`, and the TUI's own delete prints the help and keeps the file. The same rule admits `CON`, a name Windows treats as a device; `x.eval`, which lands in the eval-suite slot that `train eval --suite x` reads; and `x.jsonl.jsonl`, which becomes the dataset `x`. The rule can't be tightened where it is, because the one predicate is also the read gate. Once CLI-M55 gives the readers their own rule, this item adds a create rule for new names at every door that names one, on the CLI and over HTTP with the same words, while every dataset already on disk keeps reading.

## Description

### User story

**As** someone naming a new dataset, on the command line, over HTTP, in the TUI or from the Mac app,
**I want** a name that some verb would misread (as a flag, as a Windows device, as an eval suite, or as another dataset) to be refused when I make it,
**so that** every dataset I make can be read, trained on, evaluated and deleted by the name I gave it, on every platform.

### Steps to reproduce

The sandbox, 2026-11-26, with the built binary `apogee 0.2.0 (33ea685d, 2026-11-22, macos-arm64)`: a throwaway `APOGEE_HOME` and `HOME` three levels deep, `env -i PATH=/usr/bin:/bin`, stdin from `/dev/null`, `config init`, and mock backends only.

1. Run `apogee datasets create --from empty -- <n>` for `-h`, `CON`, `x.eval`, `a..b`, a 128-byte name and `x.jsonl`, then for `x.jsonl.jsonl`. List `training/datasets/`.
2. Run `apogee __complete datasets delete ''` and `apogee __complete train eval --suite ''`.
3. With a `-h.jsonl` in the folder, run `apogee datasets delete -h --yes` (the argv the TUI builds), then list the folder.
4. Run `apogee datasets synth --teacher <scripted mock> --kit <tiny kit> -- -h`.
5. Under `apogee serve --bind 127.0.0.1` with the home's admin token, send `POST /v1/admin/datasets {"name":"-h","from":"empty"}`.

### Actual result

1. `-h`, `CON`, `x.eval`, `a..b`, the 128-byte name and `x.jsonl` each exit 0 and write `<n>.jsonl` (`x.jsonl` writes `x.jsonl`, the dataset `x`, by `write`'s one strip of `.jsonl`). `x.jsonl.jsonl` is refused with `dataset already exists: …/training/datasets/x.jsonl`: it names the dataset `x`.
2. `-h`, `a..b`, `CON`, the 128-byte name, `x` and `x.eval`. The suite completion offers `x` among the suites and kits.
3. stdout `Remove a dataset` / `Usage: apogee datasets delete [OPTIONS] name`, exit 0, and `-h.jsonl` is still there.
4. `2 example(s) in 1 teacher call(s)`, `-h.jsonl` written, then `Train on it with:  apogee train run <snapshot> --dataset -h`.
5. HTTP 201, and `-h.jsonl` written.

### Expected result

1. `-h`, `CON`, `x.eval` and `x.jsonl.jsonl` each exit 1 with one stderr line, `apogee datasets: '<n>' is not a dataset name: …`, naming the clause the name breaks, and nothing is written. `x.jsonl.jsonl` is refused whether or not `x` exists. `a..b`, the 128-byte name and `x.jsonl` create as today.
2. Unchanged for what is on disk: every existing dataset is listed and offered (CLI-M55's read rule). New names of these shapes can't be made.
3. Not reached for a new name, since `-h` can't be created. An existing `-h.jsonl` keeps reading and deleting through `datasets delete --yes -- -h`; the TUI's argv is CLI-M51's.
4. Exit 1 with the same words before the kit is loaded or the teacher is called.
5. HTTP 400 with the same words, and no file.

### Background

Datasets are files the home builds, `training/datasets/<name>.jsonl`, written by one store (`training/datasets`, 7973da67, 2026-09-19, the training-datasets item). They live in no config, so 55f's add rule (8d5aec51, 2026-10-25: letters, digits, `_`, `-`, `.` and `/`, first neither `-` nor `.`, and not null) never reached them, and CLI-M46's create rule is for collections. Their one rule, `valid_dataset_name`, keeps paths and odd bytes out but admits a leading `-`, any ending after a `.`, and the Windows device names. MILESTONES' entry for the training-datasets item records no name rule (its mutation list says only that the store validates the name), and `training.md` documents none; the only statement of the rule is the comment at `datasets.h:37`.

The Mac app's investigation (APP-M21, 2026-11-26) met a dataset named `-h` that its Delete… could not remove, and left as a CLI aside that "datasets have no name rule". The made-thing-names investigation that followed, with two skeptics and a critic, corrected the aside: the rule exists, and it admits `-h`, `CON`, `x.eval` and `x.jsonl.jsonl`. The same investigation found that the readers check nothing at all, so `datasets delete --yes -- ../../../victim` removes a file outside the home. That is CLI-M55.

The investigation's settled grouping splits the datasets finding in two, as CLI-M45 and CLI-M46 split collections:
- **CLI-M55** gives every reader an escape-only read rule that accepts every dataset in the folder, and moves the readers off `valid_dataset_name`. It is a safety fix with no regression, so it lands first.
- **This item** adds a create rule beside it for new names only. It narrows what is accepted, so its compatibility and parity effects differ. It can't land while the same predicate still gates `train run --dataset`, `train eval --suite` and the HTTP reads.

The design, in prose: one function, `dataset_create_error`, holding CLI-M55's read rule, today's character set and 128-byte cap, and four clauses (the first character, the device names, `.eval`, and a `.jsonl` left after the one strip). Every door that names a new dataset calls it before any other work, and the CLI and HTTP say the same words. Existing files keep reading and deleting. `prepare --out` keeps taking any path, and `prepare --as-eval` keeps writing `<stem>.eval.jsonl`.

### Evidence

All measured 2026-11-26 with the built binary `apogee 0.2.0 (33ea685d, 2026-11-22, macos-arm64)`, channel release. It carries HEAD 5b8927c9's CLI code: `strings` finds that commit's one new completion string in it, and the commit's CLI changes are outside these seams. Every run used `env -i PATH=/usr/bin:/bin`, `APOGEE_HOME` and `HOME` three levels deep, stdin from `/dev/null`, stdout, stderr and exit recorded apart, and mock backends only. Probe labels name who ran each probe and where:
- `inv-c`: the investigator's datasets matrix;
- `inv-f`: its synth run, with a scripted mock teacher and a tiny kit;
- `inv-http`: its loopback `serve --bind 127.0.0.1`;
- `crit-c`: the critic's home with hand-placed datasets.

A point marked "by reading" was read in the source at HEAD and not run.

1. **The create matrix** (`inv-c`). `datasets create --from empty -- <n>`, one after another in one fresh home:
   - **Created, exit 0**: `-h` as `-h.jsonl`, `CON` as `CON.jsonl`, `x.eval` as `x.eval.jsonl`, `a..b` as `a..b.jsonl`, a 128-byte name, and `x.jsonl` as `x.jsonl` (the dataset `x`, by `write`'s one strip, point 4).
   - **Refused** with `'<n>' is not a dataset name: letters, digits, '-', '_' and '.', not starting with a dot`: `../x`, `a/b`, an absolute path, the empty name, `.`, `..`, a tab, `\x01`, 129 and 300 bytes, `my set` and `café`.
   - **`x.jsonl.jsonl`**, run after `x.jsonl` existed: `dataset already exists: …/training/datasets/x.jsonl`. It names the dataset `x`'s file.

   Afterwards the folder's `.jsonl` files are exactly `-h.jsonl`, `CON.jsonl`, `a..b.jsonl`, `x.eval.jsonl`, `x.jsonl` and the 128-byte name. `__complete datasets delete ''` offers `-h`, `a..b`, `CON`, the 128-byte name, `x` and `x.eval`.
2. **A leading `-` is read as an option wherever it is not after `--`.**
   - `crit-c`, over a hand-placed `-h.jsonl`: `datasets delete -h --yes`, the argv the TUI's Datasets view builds (`tui_training.cpp:278-279`), prints the verb's help on stdout (`Remove a dataset` / `Usage: apogee datasets delete [OPTIONS] name`), exit 0, and `-h.jsonl` is still there.
   - A `-x` fails another way: `datasets delete -x --yes` exits 106 with `name is required` (APP-M21's Evidence).
   - The Mac app passes the name before any `--` at delete, create and synth (`TrainingViewModel.swift:257`, `:1293`, `:218`). APP-M21 measured the delete of `-h` toasted as success with the dataset kept. APP-M23 measured a typed `-h` in the create sheet toasted as success with nothing made.
   - `inv-f`: `datasets synth --teacher <script> --kit <tiny> -- -h` printed `2 example(s) in 1 teacher call(s)`, wrote `-h.jsonl`, then printed `Train on it with:  apogee train run <snapshot> --dataset -h`, the dash name bare (`presentation/cli/datasets.cpp:520`).
   - After `--` every verb reads it: `datasets delete --yes -- -h` removes it (APP-M21's way through).
3. **`x.eval` fills the eval-suite slot.**
   - `<stem>.eval.jsonl` in `training/datasets/` is the documented output of `datasets prepare --as-eval` (`training.md:210`; `presentation/cli/datasets.cpp:315`).
   - It is one of the places `train eval --suite <name>` looks, after `training/suites/<name>.jsonl` (`training.md:483`; `train.cpp:1953-1955`).
   - `create` and `synth` write chat lines (`--from empty` writes none). In `inv-c`, `__complete train eval --suite ''` offered `x` once `x.eval` existed, since the suite completion lists the `.eval.jsonl` stems (`complete_sources.cpp:424`).
   - `train eval --suite x` itself was not run, as it needs a training run.
4. **`x.jsonl.jsonl` is the dataset `x`.** `write` drops one trailing `.jsonl` (`business/training/datasets.cpp:153-156`). The rule accepts the `x.jsonl` that is left (`:157`). `path_for` appends nothing to a name already ending `.jsonl` (`:98-104`). So the file is `training/datasets/x.jsonl`, which `datasets list` shows as `x`.
   - Measured with `x` present (point 1).
   - On a home without `x`, or with `--force`, the same command would write the dataset `x` (by reading).
   - HTTP takes the same name through the same writer (by reading).
5. **Windows device names.** `CON.jsonl` was created on APFS (point 1). Windows treats `CON`, `PRN`, `AUX`, `NUL`, `COM1`–`COM9` and `LPT1`–`LPT9` as devices, case-insensitively and with or without an extension, so a `CON.jsonl` made on a Mac may not survive a move to Windows. Not measured: there is no Windows host here. CLI-M46 refuses the same names for new collections on every platform, case-folded, so a home moves between platforms whole.
6. **HTTP** (`inv-http`). `POST /v1/admin/datasets {"name":"-h","from":"empty"}` answered HTTP 201.
   - Both POST routes check `plain_name`, which is `valid_dataset_name` (`admin_datasets.cpp:26-28`; create `:131`, synth `:228`).
   - Their refusal words are shorter than the CLI's: `'<n>' is not a dataset name` (`:132`, `:229`), against the store's full sentence (`business/training/datasets.cpp:158-160`). So the two doors already disagree on the words.
7. **`prepare`'s derived name, by reading.** With no `--out`, `prepare` names its output after the source file's stem (`presentation/cli/datasets.cpp:307`) and checks it with `valid_dataset_name` (`:311-312`: `'<stem>' is not a dataset name; pass --out`). So:
   - `-h.csv` would write `training/datasets/-h.jsonl`;
   - `x.eval.csv` would write chat lines into `x.eval.jsonl`;
   - `x.jsonl.csv` would write `x.jsonl.jsonl`, which `datasets list` shows as `x.jsonl` and which the name `x.jsonl` reads as `x`.

   The stem check runs after the script check and the Python environment (`:283-301`), so no hermetic test can reach it today. `prepare` was not run, as it needs an environment.
8. **The store's writer has one more caller, by reading.** A regime writes each kit's synthesized dataset as `<kit name>.jsonl` into its own work folder, `training/regimes/<run>/` (`business/training/regime.cpp:108`, `:142-143`; `train.cpp:3647`). It runs its stages by that path (`regime.cpp:162-164`). The investigation counted the doors and did not list this caller. A rule placed inside `write` would hold kit names to it there, after the teacher's calls.
9. **Root cause.** `valid_dataset_name` (`business/training/datasets.cpp:42-49`) is the only predicate, and it allows a leading `-`, any ending after a `.`, and the device names. It serves four roles:
   - the write rule (`:157`);
   - HTTP's `plain_name` on every route (`admin_datasets.cpp:26-28`; `:131`, `:168`, `:179`, `:228`);
   - `prepare`'s stem check (`presentation/cli/datasets.cpp:311`);
   - the read gate of `train run --dataset` and `train eval --suite` (`train.cpp:1899`, `:1952`).

   So it can't be tightened without stopping an existing `-h` from reading. CLI-M55 first moves every reader onto an escape-only read rule (the investigation's settled split). Per the investigator, the store and its rule date from 7973da67 (2026-09-19).
10. **Blast radius.** Five doors name a new dataset in `training/datasets/`:
    - `datasets create` and its twin `POST /v1/admin/datasets`, both through `create_dataset` (`operations/dataset_core.cpp:35-69`);
    - `datasets synth` and `POST /v1/admin/datasets/synth`;
    - `datasets prepare`'s derived name (not `--out`).

    The TUI's prepare and synth asks run the person's typed line through the binary (`tui_training.cpp:252`, `:261-263`). The Mac app's create and synth pass the typed name (`TrainingViewModel.swift:1293`, `:218`). Both get the CLI's refusal with no change of their own, for a name that reaches the verb: a typed `-h` needs APP-M21's `--` first. No reader changes, and the regime's work folder is not one of the doors (point 8).
11. **What already works**, and must stay:
    - the rule already refuses paths, a leading `.`, spaces, control characters, non-ASCII and more than 128 bytes (point 1), and the store writes through a temp file and a rename;
    - a name holding `..` but no separator (`a..b`) creates and reads;
    - `prepare --as-eval` fills the suite slot by design;
    - `datasets delete --yes -- -h` removes a dash-named dataset;
    - after CLI-M55, every dataset in the folder reads and deletes by its listed name, through the CLI, HTTP, the TUI and the Mac app.
12. **Not pinned.** `dataset names are plain` (`tests/business/training/datasets_test.cpp:44-51`) checks today's set only. No test creates a dash-led, device-named, `.eval` or `.jsonl.jsonl` name. No test or client pins the refusal words: a search of `lib/src/cli/tests`, `lib/src/darwin` and `lib/design` for `not a dataset name` finds nothing outside the source's own lines.

## Acceptance criteria

- [ ] **Given** a home without `-h.jsonl`, **when** `datasets create --from empty -- -h` runs, **then** it exits 1 with one last stderr line `apogee datasets: '-h' is not a dataset name: it starts with '-', …`, stdout is empty, and no `-h.jsonl` or temp file exists. Today it creates `-h.jsonl` and exits 0.
- [ ] **Given** `CON`, `con`, `con.txt` or `Lpt1`, **when** `datasets create --from empty -- <n>` runs on any platform, **then** it exits 1 naming the Windows device name, and nothing is written. Today `CON.jsonl` is created.
- [ ] **Given** `x.eval` or `x.EVAL`, **when** `datasets create` or `datasets synth` runs, **then** it exits 1 naming the eval-suite slot, no `x.eval.jsonl` is written, and `__complete train eval --suite ''` offers no new `x`. Today `x.eval.jsonl` is created and `x` is offered as a suite.
- [ ] **Given** a home with or without a dataset `x`, **when** `datasets create --from empty -- x.jsonl.jsonl` runs, **then** it exits 1 naming the doubled `.jsonl`, and `x.jsonl` is neither created nor touched. Today the name lands on `x.jsonl`: with `x` present it is refused as `dataset already exists: …/training/datasets/x.jsonl`.
- [ ] **Given** a scripted mock teacher and a kit, **when** `datasets synth --teacher <scripted mock> --kit <kit> -- -h` runs, **then** it exits 1 with the same words before the kit is loaded or the teacher called: no `[synth]` line, and nothing written. Today it writes `-h.jsonl` after one teacher call.
- [ ] **Given** a source file `-h.csv` and no `--out`, **when** `datasets prepare` runs in a home with no Python environment, **then** it exits 1 with the rule's words and `pass --out`, before the environment is asked for, and nothing is written.
- [ ] **Given** a source file `-h.csv` and `--out <path>`, **when** `datasets prepare` runs, **then** the path is taken as today, unjudged by the create rule.
- [ ] **Given** a loopback `serve` and the admin token, **when** `POST /v1/admin/datasets {"name":"-h","from":"empty"}` is sent, **then** it answers 400 (`invalid_request_error`) with exactly the message the CLI prints after `apogee datasets: `, and no file is written. Today it answers 201.
- [ ] **Given** a loopback `serve` with a served teacher, **when** `POST /v1/admin/datasets/synth` is sent with `-h`, `CON`, `x.eval` or `x.jsonl.jsonl`, **then** it answers 400 with the same message and no job starts.
- [ ] **Given** an existing `-h.jsonl`, `CON.jsonl` or `x.eval.jsonl`, **when** `datasets info -- <n>`, `datasets delete --yes -- <n>` or HTTP GET and DELETE run, **then** each behaves as CLI-M55 leaves it, and `datasets delete --yes -- -h` exits 0.
- [ ] **Given** an existing `-h.jsonl`, **when** `datasets create --force --from empty -- -h` runs, **then** it is refused with the same words and the file's bytes are unchanged (the `--force` default under Open questions).
- [ ] **Given** `a..b`, a 128-byte name, `x.jsonl` or `my-set_1.v2`, **when** `datasets create --from empty -- <n>` runs, **then** it creates as today, and a 129-byte name is refused as today.
- [ ] **Given** a plain source file and the Python environment, **when** `datasets prepare --as-eval` runs, **then** it writes `<stem>.eval.jsonl` as today.
- [ ] **Given** a regime's work folder, **when** `DatasetStore::write` is called there with a kit name the create rule refuses (`-x`), **then** it writes as today (the placement default under Open questions).

## Constraints

- **Two rules, one function each.** The create rule includes CLI-M55's read rule and is called only by the doors that name a new dataset. No reader calls it, and no door keeps its own copy: `plain_name` stops being a second spelling of the create rule.
- **Existing datasets keep working** ([ADR 0006](../../../adrs/cli/backwards-compatibility.md)). Every file in the folder keeps reading and deleting through CLI-M55's read rule, whatever its name. The create rule judges only a name a door is asked to write.
- **Mode parity** ([ADR 0002](../../../adrs/cli/mode-parity.md)). The CLI and HTTP refuse the same names with the same message, and a refusal over HTTP is a 400, never a 409. So the words never contain `already exists`, which `admin_datasets.cpp:155` maps to 409. A dataset created over HTTP stays byte-identical to the CLI's.
- **Refused before any work.** The name is judged before the existing-file check, the sessions mining, the kit, the teacher and the Python environment, so a refusal costs nothing and writes nothing (CLI-M55's order for synth; CLI-M46's refusal before the write).
- **The same create-rule shape as CLI-M46**: first neither `-` nor `.`, and the Windows device names case-folded on every platform, from one list both rules share.
- **The house voice on stderr.** One line, `apogee datasets: <words>`, the last on stderr, exit 1, stdout empty. The TUI's child answer and any client that shows a failed run's last stderr line then show it with no change of their own.
- **`prepare --out` and `--as-eval` keep their meaning.** An explicit `--out` is a path, not a dataset name (`training.md`'s `datasets prepare`), and `--as-eval` writes `<stem>.eval.jsonl`.
- **The cap stays 128 bytes** (`datasets.h:37`). CLI-M46's 200-byte cap is for collections.

## Technical approach

Paths under `lib/src/cli/`.

| File | Change |
|---|---|
| `source/business/training/datasets.h`, `.cpp` | `std::string dataset_create_error(std::string_view name)`, empty when the name is accepted. It drops one trailing `.jsonl` as `write` does (`:153-156`) and judges what is left: CLI-M55's read rule, today's character set and 128-byte cap, the first character neither `-` nor `.`, no Windows device name, no `.eval` ending (case-folded), and no `.jsonl` ending left. `valid_dataset_name` becomes the character-set half, which the create rule calls and `DatasetStore::write` keeps as its floor (`:157`); no reader calls either. |
| `source/data/contracts/collection_name.h`, `.cpp` (CLI-M45's), or CLI-M55's `source/data/contracts/plain_name.h`, `.cpp` if CLI-M45 has not landed | `windows_device_name(std::string_view)`: the part before the first `.`, case-folded, against `con`, `prn`, `aux`, `nul`, `com1`–`com9`, `lpt1`–`lpt9`. One function for this rule and CLI-M46's create rule: whichever item lands second reuses the first's. |
| `source/presentation/operations/dataset_core.cpp` | `create_dataset` (`:35-69`) applies the create rule first, before any session is mined (`:53`), and throws its words. Both create doors share it. |
| `source/presentation/cli/datasets.cpp` | synth's first check (CLI-M55's, ahead of `store.exists` at `:451`) uses the create rule. `prepare`'s derived name is judged as `dataset_create_error(stem + ".jsonl")`, right after the source is found (`:280-282`) and before the script and Python environment (`:283-301`), refusing with the rule's words and `; pass --out`. An explicit `--out` is not judged. |
| `source/presentation/httpserver/admin_datasets.cpp` | POST create (`:131-133`) and synth (`:228-230`) answer 400 with `dataset_create_error`'s words in place of `plain_name`'s. GET and DELETE stay on CLI-M55's read rule. |
| `tests/business/training/datasets_test.cpp` | The rule's table, and the store's floor. |
| `tests/presentation/cli/datasets_test.cpp` | The CLI fixture. |
| `tests/presentation/httpserver/admin_datasets_test.cpp` | The HTTP fixture. |
| `tests/scripts/sh/datasets_e2e.sh` | On the real binary: `create -- -h` refused, its words the last stderr line; in the half that runs with `python3`, `prepare --as-eval` still writes `<stem>.eval.jsonl`. |

**The words.** Today's prefix stays, `'<n>' is not a dataset name: `, and the clause the name breaks follows it, so a test pins the prefix and a fragment of the clause. A name CLI-M55's read rule refuses keeps M55's own words (`… is not a dataset name -- a name, never a path`):

| Name | Clause | Words after the prefix |
|---|---|---|
| empty, `..`, `a/b`, `a\b`, an absolute path | CLI-M55's read rule | CLI-M55's words |
| `my set`, `café`, a tab, 129 bytes | character set and cap | `letters, digits, '-', '_' and '.', at most 128 bytes` |
| `-h`, `.x` | first character | `it starts with '-', which every verb reads as an option` · `it starts with '.'` |
| `CON`, `con.txt`, `Lpt1` | device name | `'con' is a Windows device name, which Windows does not keep as a file` |
| `x.eval`, `x.EVAL.jsonl` | `.eval` ending | `'x.eval.jsonl' is the eval-suite slot 'train eval --suite x' reads, which 'datasets prepare --as-eval' fills` |
| `x.jsonl.jsonl` | `.jsonl` left after the strip | `one '.jsonl' is dropped, and 'x.jsonl' would be the dataset 'x'` |

**The strip.** The function takes the name as typed and drops one `.jsonl` itself, so `create -- x.jsonl` still makes `x`. `prepare` hands it the derived stem with `.jsonl` appended, so the stem itself is judged. A stem `x.jsonl` (from `x.jsonl.csv`) is then refused, where it would have been written as `x.jsonl.jsonl`. Under `--as-eval`, an accepted stem still gets `.eval.jsonl`.

**Where it runs.** The create rule runs at the doors that name a new dataset in `training/datasets/`: `create_dataset`, synth's first check on both doors, and `prepare`'s derived name. `DatasetStore::write` keeps today's character set and cap as its floor, so the HTTP synth job's write (`admin_datasets.cpp:299-300`) and the CLI's (`datasets.cpp:506`) are behind an early check, and a regime's work-folder write is unchanged (Evidence 8; the placement question below).

The item extends `create_dataset`'s refusal (a `std::runtime_error` carrying the words), HTTP's `error_response(400, …)`, and synth's first check. It consumes decisions from:
- CLI-M55: the read rule, its words, where it lives, and synth's first check;
- CLI-M46: the device-name list and its case-fold;
- APP-M21 and CLI-M51: the `--` that lets a refusal reach the verb from the app and the TUI;
- CLI-M48: how the printed `--dataset <name>` lines quote a name.

**Reference.** `collection_create_error` in CLI-M46's Technical approach, which includes CLI-M45's `collection_read_error`: the read and create split this item copies, with the device names case-folded on every platform. 55f's `yaml_plain_key` (`data/contracts/config_yaml.cpp:206-216`), the house's first-character rule. `prepare`'s `pass --out` refusal (`presentation/cli/datasets.cpp:311-312`), kept and widened. The HTTP synth test's `CHECK(fixture.jobs.list().empty())` (`tests/presentation/httpserver/admin_datasets_test.cpp:291`), the "no job started" assertion.

**Docs when it ships:**
- `reference/cli/training.md`'s Datasets section: the create rule's table, a line that existing names keep reading, and that `--out` is a path.
- `reference/cli/http-api.md`'s `POST /v1/admin/datasets` and `/synth`: `400` for a name the create rule refuses, with the CLI's words.
- DEVELOPER.md's rows for `training/datasets_test.cpp`, `cli/datasets_test.cpp` and `httpserver/admin_datasets_test.cpp`.
- MILESTONES: a dated entry, recording the rule as new, since the training-datasets entry recorded none.

## Testing

Hermetic: a temp `APOGEE_HOME`, the scripted mock teacher, and no Python environment for any refusal. A try-it step names `apogee-dev`, never `apogee`. Each case fails on today's binary unless marked as a guard (measured 2026-11-26, or by reading where Evidence marks the case so).

- **The bug as a fixture**, in `tests/presentation/cli/datasets_test.cpp`, whose in-process root parses `--` as the command line does. In a fresh home after CLI-M55, for `-h`, `CON`, `con.txt`, `x.eval` and `x.jsonl.jsonl`:
  - `datasets create --from empty -- <n>` exits 1;
  - the last stderr line begins `apogee datasets: '<n>' is not a dataset name: ` and carries its clause's fragment;
  - stdout is empty, and `training/datasets/` holds nothing new.

  `x.jsonl.jsonl` runs once with `x` absent and once present, `x.jsonl`'s bytes unchanged. A hand-placed `-h.jsonl` under `create --force --from empty -- -h` is refused, its bytes unchanged.
- **synth**: `datasets synth --teacher teacher --kit reasoning -- -h` exits 1 with the same words, with no `[synth]` line on stderr and no `-h.jsonl`.
- **prepare**: a one-line source `-h.csv` and no `--out` exits 1 with the rule's words and `pass --out`, with none of the Python environment's words (the fixture has no environment), and nothing written.
- **The rule's table**, in `tests/business/training/datasets_test.cpp`:
  - refused: `-h`, `.x`; `con`, `CON`, `Con.txt`, `lpt9`, `com1.x`; `x.eval`, `x.EVAL`, `x.eval.jsonl`; `x.jsonl.jsonl`; 129 bytes;
  - accepted: `com10`, `console`, `lpt`; `x.jsonl` (one strip); `a..b`, 128 bytes, `my-set_1.v2`;
  - CLI-M55's read-rule refusals keep M55's words;
  - guard: `DatasetStore::write` still takes `-x` into a temp folder, the floor a regime's work folder relies on.
- **HTTP**, in `admin_datasets_test.cpp`: `POST /v1/admin/datasets` with each refused name answers 400, its message equal to `dataset_create_error`'s, with no file. `POST /v1/admin/datasets/synth` does the same, and `fixture.jobs.list().empty()` holds.
- **Guards**, passing today and after:
  - hand-placed `-h.jsonl`, `CON.jsonl` and `x.eval.jsonl` read through `datasets info -- <n>` and delete through `datasets delete --yes -- <n>` and HTTP DELETE;
  - `datasets create --from empty a..b` creates;
  - `datasets create --force` over a plain `starter` still overwrites.
- **The real binary**, in `tests/scripts/sh/datasets_e2e.sh`: `datasets create --from empty -- -h` exits 1 with its words as the last stderr line. In the half that runs with `python3`, `prepare --as-eval` still writes `<stem>.eval.jsonl` (guard).
- **Gate:** `lib/scripts/cicd.sh --app cli --test`, clang-format, and the MinGW scan of the changed files.

## Open questions

| Question | Status | Answer |
|---|---|---|
| Refuse a leading `-` for new dataset names, though datasets live in no config? | `[default]` | Yes: one plain-name house rule, matching CLI-M46's create rule and 55f's add rule (first neither `-` nor `.`). A dash name needs `--` at every verb, and today the TUI's delete reads it as a flag (Evidence 2). |
| Refuse `.eval` at create and synth? | `[default]` | Yes. Those verbs write chat lines, and `<stem>.eval.jsonl` is the documented suite slot that `prepare --as-eval` fills and `train eval --suite <stem>` reads. The ending is matched case-folded (`x.EVAL`), since on APFS and NTFS `x.EVAL.jsonl` is the file `--suite x` opens (by reading). `prepare`'s derived name takes the same clause, and `--as-eval` still appends `.eval.jsonl` to an accepted stem. |
| Refuse Windows device names on every platform? | `[default]` | Yes, as CLI-M46 does, so a home moves between platforms whole: `con`, `prn`, `aux`, `nul`, `com1`–`com9` and `lpt1`–`lpt9`, the part before the first `.` case-folded. Not measured on Windows. |
| Does the rule bind `--force` over an existing dataset whose name it refuses? | `[default]` | Yes. `create --force` and `synth --force` over an existing `-h.jsonl`, `CON.jsonl` or `x.eval.jsonl` are refused with the same words, and the file is left as it was. `--force` replaces the file whole, unlike CLI-M46's ingest into an existing store, which adds to it. Overwriting `x.eval.jsonl` would replace a prepared suite with chat lines, and `x.jsonl.jsonl --force` would overwrite the dataset `x`. The way out is `datasets delete --yes -- <name>` and a plain name. No record names today's `--force` over such a file; CLI-M53 gives up the same replace for dash-named symphonies. |
| Does the create rule live inside `DatasetStore::write`, as the investigation's settled fix put it? | `[default]` | No. It runs at the doors that name a new dataset in `training/datasets/`: `create_dataset` (both create doors), synth's first check on the CLI and over HTTP, and `prepare`'s derived name. `write` keeps today's character set and cap as its floor. Its other caller is a regime, which writes `<kit name>.jsonl` into its own work folder and runs its stages by path (Evidence 8). The first-character, `.eval` and `.jsonl` clauses guard a name that some verb reads, and nothing reads that file by name; only the device-name reason could apply there, and it is unmeasured. A refusal inside `write` would fail a regime after its teacher calls for a kit named `-x`. Found by reading while writing this spec; the investigation did not list that caller. |
| Which read rule existing datasets go through, with its words, and synth's first check | `(consumed decision)` | [CLI-M55](a-dataset-is-named-not-pathed-at-every-reader.md). This item's rule includes M55's read rule and replaces the rule in M55's first synth check with the create rule. |
| How the printed `Train on it with: … --dataset <name>` lines carry a name | `(consumed decision)` | [CLI-M48](a-suggested-command-runs-as-printed.md), which renders every printed command. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-26 | Specced from the made-thing-names investigation of the APP-M21 and APP-M22 probes' CLI asides, with two skeptics and a critic (Evidence). **The seam is one predicate serving as both rules**: `valid_dataset_name` is the create rule and the read gate, so it admits names some verb misreads and can't be tightened in place. | the spec |
| 2026-11-26 | Placed in **Maintenance** as M56, by the user's call to spec the APP-M21/M22 CLI asides as maintenance items from M48, grouped only where they share one seam. Its own item, not part of CLI-M55: its seam is the writers, not the readers. The create rule narrows which new names are accepted, so its compatibility and parity effects differ from M55's read rule, which is a safety fix with no regression and lands first. | the user (placement) · the spec (the split) |
| 2026-11-26 | **Blocked by CLI-M55.** `valid_dataset_name` gates `train run --dataset` (`train.cpp:1899`), `train eval --suite` (`:1952`), HTTP GET and DELETE (`admin_datasets.cpp:168`, `:179`) and `prepare`'s stem. Tightening it first would stop an existing `-h` from reading. M55 moves the readers to its read rule, which this item's rule includes. | the spec |
| 2026-11-26 | **Not gated on CLI-M46, M51, M48 or APP-M21.** M46 shares the shape and the device-name list, and whichever lands second reuses the first's. M51 makes the TUI's delete pass `--`, M48 renders the printed `--dataset` lines, and APP-M21 passes `--` in the app. Each stands alone. | the spec |
| 2026-11-26 | **No recorded contract is reversed.** MILESTONES' training-datasets entry (2026-09-19) and `training.md` record no dataset name rule (the entry's mutation list says only that the store validates the name); the only statement of one is the comment at `datasets.h:37`. The narrowing is recorded as new when it ships. Today's `--force` over an odd-named dataset, which no record names, is given up by the default above. | the spec |

## Out of scope

- **The read rule at every reader**, the path escape, HTTP GET and DELETE of listed names, `train run --dataset` and `train eval --suite` by name, and synth's order: [CLI-M55](a-dataset-is-named-not-pathed-at-every-reader.md).
- **`prepare --out` paths**: an explicit output path, by design. A paired device reaching it through CLI-59d's door needs 59d's own rule for path arguments (an aside of the investigation); see [reads and commands over the network](../v0.2.3/reads-and-commands-over-the-network.md).
- **The 128-byte cap**: unchanged.
- **The TUI's row actions** passing a listed name without `--`, its dataset delete among them: [CLI-M51](the-tuis-row-actions-reach-a-dash-led-row.md).
- **The Mac app** passing a typed or listed name before `--` at create, synth and delete: [APP-M21](../../../assistant/darwin/MILESTONES.md#2026-11-26--listed-name-reaches-the-verb-maintenance-item-m21-a-listed-name-reaches-the-verb-as-listed), and the app's other sites, [APP-M23](../../../assistant/darwin/MILESTONES.md#2026-11-27--every-name-the-app-passes-is-read-as-a-name-maintenance-item-m23-every-name-the-app-passes-is-read-as-a-name).
- **Printed commands** with the dash name bare (synth's and `prepare`'s `Train on it with:` lines, `presentation/cli/datasets.cpp:367`, `:520`): [CLI-M48](a-suggested-command-runs-as-printed.md).
- **Completion offering an existing `-h` before `--`.** No reader or offer changes here. An aside of the investigation asks for one settlement across sections, between ADR 0007's "a candidate the verb would refuse" and CLI-M44's recorded default.
- **Control characters echoed raw in a refusal** (an aside of the investigation). Such names stay refused; how the words draw them is not this item.
- **A dataset named `kits`** (by reading, not probed). `GET /v1/admin/datasets/kits` is the kits route, matched before `{id}` (`httpserver/mux.cpp:334-351`). So a dataset `kits` can be listed and deleted over HTTP but not read by name there. It is not in this rule; a follow-up may add the literal route names.
- **A prepared `x.jsonl.jsonl` already on disk** (by reading). It is listed as `x.jsonl` and read by that name as `x`. New ones are refused here; reaching an existing one by its listed name is a reader matter beside CLI-M55.
- **`train run --dataset` preferring a file in the current folder** (an aside of the investigation, `train.cpp:1896-1898`): a reader, not in this item.

## Links

- **Blocked by:** [CLI-M55](a-dataset-is-named-not-pathed-at-every-reader.md), whose read rule this item's create rule includes · **Blocks:** —
- **Relates to:**
  - [CLI-M46](../../../assistant/cli/MILESTONES.md#2026-11-27--a-collection-the-config-cannot-hold-is-refused-before-it-is-written-maintenance-item-m46-a-collection-the-config-cannot-hold-is-refused-before-it-is-written) (shipped 2026-11-27), the same create-rule shape for collections, sharing the device-name list;
  - [CLI-M45](../../../assistant/cli/MILESTONES.md#2026-11-27--a-collection-is-named-not-pathed-at-every-reader-maintenance-item-m45-a-collection-is-named-not-pathed-at-every-reader) (shipped 2026-11-27), the read and create split this item copies;
  - [CLI-M52](a-refused-scaffold-writes-nothing.md) and [CLI-M53](a-symphony-the-cli-writes-is-one-it-can-delete.md), the same seam's refusals for agents, MCP servers and dash-named symphonies;
  - [CLI-M51](the-tuis-row-actions-reach-a-dash-led-row.md), the TUI's delete of a dash-led row;
  - [CLI-M48](a-suggested-command-runs-as-printed.md), the printed `--dataset` lines;
  - [APP-M21](../../../assistant/darwin/MILESTONES.md#2026-11-26--listed-name-reaches-the-verb-maintenance-item-m21-a-listed-name-reaches-the-verb-as-listed), whose aside "datasets have no name rule" this item answers, and whose `--` lets the refusal reach the verb from the app; [APP-M23](../../../assistant/darwin/MILESTONES.md#2026-11-27--every-name-the-app-passes-is-read-as-a-name-maintenance-item-m23-every-name-the-app-passes-is-read-as-a-name), which measured a typed `-h` in the create sheet;
  - [ADR 0002](../../../adrs/cli/mode-parity.md), [ADR 0006](../../../adrs/cli/backwards-compatibility.md) and [ADR 0007](../../../adrs/cli/tab-completion.md);
  - [MILESTONES](../../../assistant/cli/MILESTONES.md)' training-datasets entry (2026-09-19), which records no name rule, and 55f (8d5aec51), the house's plain-name add rule.
