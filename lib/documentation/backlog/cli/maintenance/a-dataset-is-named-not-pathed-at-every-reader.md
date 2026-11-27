# CLI-M55 · A dataset is named, not pathed, at every reader

| Field | Value |
|---|---|
| **Type** | 🐞 Bug |
| **Project** | CLI |
| **Status** | 🟢 Ready |
| **Fix version** | Maintenance (any time) |
| **Component** | Training › dataset names at `datasets info`, `delete` and `synth`, `train run --dataset`, `train eval --suite`, the HTTP datasets routes and completion |
| **Labels** | `datasets` `safety` `mode-parity` `completion` |
| **Epic** | — (maintenance has no track) |
| **Blocked by** | — |
| **Blocks** | [M56](a-new-dataset-name-is-one-every-verb-reads.md) |
| **Reporter** | The APP-M21 and APP-M22 probes' CLI asides (2026-11-26), then a sandboxed investigation, two skeptics and a critic the same day |
| **Created** | 2026-11-26 |

---

## Summary

`apogee datasets delete --yes -- ../../../victim` removes `victim.jsonl` from outside Apogee's home. The dataset store joins any string to its folder, and only its writer checks a name. The HTTP twins of `datasets info` and `delete` refuse every path, but also listed names the CLI reads: `GET /v1/admin/datasets/my%20set` answers 400 while the list shows `my set`. The item adds one read rule that refuses only what can leave the folder, applies it at every dataset reader on both doors, and moves synth's name check ahead of the teacher.

## Description

### User story

**As** someone who names a dataset at the prompt, in the full-screen shell, in the Mac app or over HTTP,
**I want** every dataset verb to take a name only, and the same names on every door,
**so that** a typo or a pasted path can never read or delete a file outside the datasets folder, and every dataset the list shows can be read and deleted wherever I am.

### Steps to reproduce

The sandbox, 2026-11-26, with the built binary `apogee 0.2.0 (33ea685d, 2026-11-22, macos-arm64)`, channel release: `env -i PATH=/usr/bin:/bin`, `APOGEE_HOME` and `HOME` set to a home three levels deep (`<sandbox>/<probe>/l1/l2/l3/home`, with `$S` and `$M` the sandboxes named under Evidence), the working folder inside the sandbox, stdin from `/dev/null`, mock backends only, and stdout, stderr and exit recorded apart.

1. Make a fresh home at `$S/k2/l1/l2/l3/home`, run `config init`, and put a victim at `l3/victim.jsonl`, in the home's parent folder. Run `apogee datasets delete --yes -- ../../../victim` and look for the victim. Then `mkdir -p home/training/datasets`, run `apogee datasets info -- ../../../victim` and the same delete again, and look for the victim.
2. In a second home (`$S/c/…`), hand-place `my set`, `café`, `.hidden`, a 130-byte name, `a:b`, `back\slash`, `-h` and `x.eval` as `.jsonl` files beside a plain `ok`. Run `apogee datasets list`, `apogee __complete datasets delete ''`, `apogee datasets info -- "my set"`, and `apogee datasets delete --yes` on `café`, `.hidden` and `back\slash`, each after `--`.
3. Under `apogee serve --bind 127.0.0.1` over a third home (`$S/g/…`) holding a hand-placed `my set.jsonl`, with that home's admin token: `GET /v1/admin/datasets`, then `GET` and `DELETE /v1/admin/datasets/my%20set`.
4. In `$M/f`, with a scripted mock teacher and a tiny kit: `apogee datasets synth --teacher teacher --kit <kit.yaml> -- "my set"`.

### Actual result

1. `info` prints `name:   victim` / `path:   $S/k2/l1/l2/l3/home/training/datasets/../../../victim.jsonl` / `shape:  chat`, exit 0. `delete --yes` prints `will remove:` / `  $S/k2/l1/l2/l3/home/training/datasets/../../../victim.jsonl` / `removed.`, exit 0, and `l3/victim.jsonl` is gone. Before the `mkdir`, the same delete says `apogee datasets: no dataset named '../../../victim' in $S/k2/l1/l2/l3/home/training/datasets`, exit 1, and the victim stays.
2. `list` and completion show all nine names. `info -- "my set"` prints `path:   $S/c/l1/l2/l3/home/training/datasets/my set.jsonl`, exit 0. Each of the three deletes prints `removed.`, exit 0.
3. The list answers `{"data":[{"bytes":16,"lines":1,"name":"my set",…}],"object":"list"}` [HTTP 200]. `GET` and `DELETE` of `my%20set` each answer `{"error":{"message":"'my set' is not a dataset name","type":"invalid_request_error"}}` [HTTP 400].
4. stderr says `[synth] teacher teacher, kit tiny, 1 batch(es) in flight`, then `apogee datasets: 'my set' is not a dataset name: letters, digits, '-', '_' and '.', not starting with a dot`, exit 1. The teacher was called before the name was checked.

### Expected result

1. `info` and `delete` each exit 1 with one stderr line, `apogee datasets: '../../../victim' is not a dataset name -- a name, never a path`, before any file is looked at, with or without the datasets folder. `delete` prints no `will remove:`, and the victim keeps its bytes.
2. Every listed name still reads and deletes, a dash-led one after `--`, except `back\slash`: it is refused with the rule's words (the open question on `\`), and `rm` removes it.
3. `GET` answers 200 with the bytes `datasets info -- "my set" --output-format json` prints, and `DELETE` answers 200 `{"deleted":"my set"}`.
4. Exit 1 with the name rule's words before the existence check, the kit and the teacher: no `[synth]` line, and no teacher call.

### Background

The dataset store arrived on 2026-09-19 (MILESTONES, Milestone Z's `training-datasets` record; 7973da67, per the investigation): one folder of `<name>.jsonl` files and one writer. Its header calls it "deliberately dumb: a directory of files" (`datasets.h:23-24`). That writer checks the name with `valid_dataset_name`: letters, digits, `-`, `_` and `.`, not starting with a dot, at most 128 bytes. The readers check nothing, so each one passes its caller's string straight to the folder join. The HTTP routes, added the same day, check every name with the writer's rule. So the HTTP side has a guard, though the wrong one for reads, and the CLI side has none.

Two later doors delete by the listed name through the CLI:
- the full-screen shell's Datasets view (37f, 2026-10-10), as `datasets delete <name> --yes` (`tui_training.cpp:278-279`);
- the Mac app's Training › Datasets, as `datasets delete <name> --yes` (`TrainingViewModel.swift:257`).

Both pass top-level names they read from `datasets list`.

The APP-M21 probes found the Mac app's dataset delete reading `-h` as a flag. They recorded an aside for the CLI: "datasets have no name rule". This run checked it. There is a create rule, and it admits a leading `-` (CLI-M56). But there is no read rule at all, and that is the more serious half. This item answers the aside's reader side.

The design is CLI-M45's, applied to datasets. Every reader takes a name, and one read rule, `dataset_read_error`, refuses only what can leave the folder: the empty name, `..` as the whole name, a `/`, a `\`, a NUL, and a Windows root name (`C:x`). The rule strips one `.jsonl` first, as the writer does. It shares one escape check with CLI-M45's collection rule in contracts.

The rest of the design:
- The create rule stays the write gate (`write`, POST create and synth, prepare's derived stem), until CLI-M56 narrows it.
- HTTP GET and DELETE stop using the create rule as a read rule.
- synth asks the write rule before anything else.
- `train run --dataset` and `train eval --suite` resolve a name through the read rule, after their path branch, as today.

### Evidence

Measured 2026-11-26 by an investigator, two skeptics and a critic, each in its own sandbox. A probe is named by its sandbox and folder: `$M` is the first investigation's sandbox, with `$M/c` (the datasets matrix) and `$M/f` (synth), and `$S` is the settling run's, with `$S/k2` (the traversal core), `$S/c` (listed names) and `$S/g` (HTTP). The binary is `apogee 0.2.0 (33ea685d, 2026-11-22, macos-arm64)`. Its stamp predates HEAD (5b8927c9, 2026-11-26), but `strings` finds that commit's new completion text, so it carries HEAD's CLI code. Every `file:line` below was read at HEAD.

1. **A delete removes a file outside the home** (`$S/k2`, with `training/datasets/` present):
   - `datasets info -- ../../../victim` gives `name:   victim` / `path:   $S/k2/l1/l2/l3/home/training/datasets/../../../victim.jsonl` / `shape:  chat`, exit 0;
   - `datasets delete --yes -- ../../../victim` gives `will remove:` / `  $S/k2/l1/l2/l3/home/training/datasets/../../../victim.jsonl` / `removed.`, exit 0, and `l3/victim.jsonl` is gone.

   `$M/c` measured the same, and one rerun in a third sandbox reproduced it along with Evidence 3's victims.
2. **A relative name needs the folder; an absolute one, by reading, does not.** In `$S/k2` before the `mkdir`, the delete gave `apogee datasets: no dataset named '../../../victim' in …/training/datasets`, exit 1, and the victim was present: the path climbs through a folder that does not exist. An absolute name replaces the folder under `std::filesystem`'s append rule, whether the folder exists or not. Without the folder, that case was not measured.
3. **Absolute names and names below the folder** (`$M/c`). `datasets delete --yes` removed:
   - `home/victim2.jsonl`, named `-- ../../victim2.jsonl`;
   - `c/abs/victim3.jsonl`, named by its absolute path;
   - `home/training/datasets/sub/inner.jsonl`, named `-- sub/inner`.

   `datasets info` on the absolute name read `abs/victim3.jsonl` (`shape: flat`). `c/abs/victim4`, which has no extension, survived with `no dataset named`, because `.jsonl` is appended.
4. **The root cause.**
   - `DatasetStore::path_for` (`business/training/datasets.cpp:98-104`) returns `dir_ / (name + ".jsonl")` for any string.
   - `exists` (`:106-109`), `info` (`:137-149`) and `remove` (`:195-206`) check nothing. Only `write` applies `valid_dataset_name` (`:42-49`, at `:153-161`).
   - The CLI's `info` calls the reader raw (`presentation/cli/datasets.cpp:580`). So does `delete` (`:599-606`), which prints the joined path at `:602` before it removes it.
   - The investigation dates all of it to 7973da67 (2026-09-19). The HTTP routes were guarded from that day, and the CLI's readers never were.
5. **synth calls the teacher before it checks the name** (`$M/f`): stderr `[synth] teacher teacher, kit tiny, 1 batch(es) in flight`, then `apogee datasets: 'my set' is not a dataset name: …`, exit 1. The order in `presentation/cli/datasets.cpp`:
   - `store.exists` (`:451-453`);
   - the kit (`:455-468`) and the teacher (`:469-478`);
   - the `[synth]` line (`:492-493`) and `training::synthesize` (`:494-495`);
   - `store.write` (`:506`), the first place the rule runs.

   By reading, the existence check reads the raw name, so `-- ../../../victim` with the victim present would answer `dataset already exists: …/training/datasets/../../../victim.jsonl` without `--force`. With `--force`, `write` refuses the name after the teacher has run, so nothing outside is overwritten. The HTTP twin checks the name first (`admin_datasets.cpp:228`).
6. **Listed names the create rule refuses read on the CLI** (`$S/c`, hand-placed files):
   - `datasets list` and `__complete datasets delete ''` give `-h`, `.hidden`, `a:b`, `back\slash`, `café`, `my set`, the 130-byte name, `ok` and `x.eval`;
   - `datasets info -- "my set"` gives `path:   …/training/datasets/my set.jsonl`, exit 0;
   - `datasets delete --yes -- café`, `-- .hidden` and `-- 'back\slash'` each give `removed.`, exit 0.

   `list` applies no rule (`datasets.cpp:111-135`). The CLI can make such names itself: `datasets prepare --out` takes any path (`presentation/cli/datasets.cpp:303-305`; training.md:226).
7. **HTTP refuses the same listed names** (`$S/g`):
   - `GET` and `DELETE /v1/admin/datasets/my%20set` each answer `{"error":{"message":"'my set' is not a dataset name","type":"invalid_request_error"}} [HTTP 400]`;
   - `GET /v1/admin/datasets` answers `{"data":[{"bytes":16,"lines":1,"name":"my set",…}],"object":"list"} [HTTP 200]`.

   `plain_name` is `valid_dataset_name` (`admin_datasets.cpp:26-28`), applied on every route (`:131`, `:168`, `:179`, `:228`). So parity is broken both ways (ADR 0002). The CLI reads names HTTP refuses, and HTTP refuses paths the CLI reads. The docs claim twins: `datasets info <name> --output-format json   # byte for byte GET /v1/admin/datasets/{name}` (machine-mode.md:1434), and "The twin of `apogee datasets delete`" (http-api.md:1302). training.md:299 says `delete <name> -y`. http-api.md documents only a 404 for the GET and DELETE routes, not today's 400.
8. **The train readers use the create rule as their read gate.**
   - `resolve_dataset` (`presentation/cli/train.cpp:1899`) serves `train run --dataset` (`:2410`) and a pipeline's stages (`:1667`).
   - `resolve_suite` (`:1952`) serves `train eval --suite`.

   Each takes a path first (`:1896`, `:1947`). For the name branch, a listed `my set` fails the predicate and gets `no dataset named 'my set' and no such file`. **Read only, not measured**: probing it needs a snapshot.
9. **`valid_dataset_name` can be neither the read rule nor tightened in place.** It was the first proposed read rule. The probes of Evidence 6 and 7 overturned that:
   - As the read rule, it would leave listed datasets (`my set`, `café`, `.hidden`, the 130-byte name) that no door can read or delete. The full-screen shell and the Mac app delete by the listed name through the CLI.
   - The predicate is also the read gate at `train.cpp:1899` and `:1952` and on HTTP, and it is prepare's stem check (`presentation/cli/datasets.cpp:311`). Narrowing it would stop an existing `-h` from reading.

   So reads and creates need two predicates, as CLI-M45 and CLI-M46 split them. Since `.jsonl` is always appended, `..` as a whole name cannot leave the folder by itself. The rule refuses it anyway, as CLI-M45's does.
10. **Completion offers file names to verbs that will refuse them.** Found while writing this ticket, by reading. The `DATASET` kind sets `paths = true` (`complete_sources.cpp:413-418`). When no dataset name matches what is typed, the shell's file names become the answer (`complete_protocol.cpp:146-147`; the kind's comment, `command.h:47-49`). `datasets info` (`presentation/cli/datasets.cpp:576`), `datasets delete` (`:595`) and `train run --dataset` (`train.cpp:2383`) all register that kind. Once `info` and `delete` take names only, those offers break ADR 0007's "a candidate the verb would refuse is a completion bug".
11. **HTTP already stops the encoded traversal** (the first investigation's HTTP probe): `DELETE /v1/admin/datasets/..%2F..%2F..%2Fvictim` answers `404 no route for DELETE /v1/admin/datasets/../../../victim`, and the victim stays.
12. **Not pinned.**
    - Tests pin the writer's refusal (`tests/business/training/datasets_test.cpp:85`, `tests/presentation/cli/datasets_test.cpp:156`) and an HTTP GET of `../x` (`admin_datasets_test.cpp:220`).
    - No test passes a path to `exists`, `info`, `remove`, `datasets info` or `datasets delete`.
    - No test reads a listed name the create rule refuses, on either door.
13. **Severity.** Today this is a destructive contract and mode-parity bug, not an exploitable one:
    - the name comes from the person running the CLI, and the path is printed before the delete;
    - a relative escape needs the datasets folder to exist;
    - no current door passes an untrusted name: HTTP guards every route, and the full-screen shell, the Mac app and completion list only the folder's top level.

    CLI-59d's door would run these verbs from a paired device. This item narrows what that door can reach, but does not make it safe for paths (Out of scope).
14. **Blast radius**:
    - **the store**: four `DatasetStore` readers (`path_for`, `exists`, `info`, `remove`), reached raw by two CLI verbs (`datasets info`, `datasets delete`), plus synth's late check;
    - **train**: two readers that gate on the create rule (`train.cpp:1899`, `:1952`);
    - **HTTP**: two routes that use the create rule as their read rule (`GET` and `DELETE /v1/admin/datasets/{id}`);
    - **callers by listed name**: the full-screen shell's delete (`tui_training.cpp:269-280`), the Mac app's delete and synth (`TrainingViewModel.swift:257`, `:218`) and completion (`complete_sources.cpp:413-418`). All pass top-level names, which the read rule keeps;
    - **docs**: two parity claims (machine-mode.md:1434, http-api.md:1302);
    - **not affected**: no MCP tool reads datasets (`mcp_serve.cpp:40-41`), and machine mode carries the reads' documents only.
15. **What already works**, and must stay:
    - HTTP refuses every path and the encoded traversal;
    - `datasets create` and synth's write refuse `/`, a leading dot (`.`, `..`, `.x`), spaces, control characters, non-ASCII and more than 128 bytes (`datasets.cpp:42-49`), and write through a temp file and a rename;
    - `exists("a.jsonl")` finds dataset `a` (`tests/business/training/datasets_test.cpp:76-77`);
    - `datasets delete --yes -- -h` removes a dash-named dataset, the path APP-M21 moves the Mac app onto;
    - `datasets delete` without `--yes` deletes nothing, the house preview pattern (`presentation/cli/datasets.cpp:177-183`, `models_pull.cpp:4108-4111`, `embed.cpp:608-611`);
    - `train run --dataset <path>` takes a `.jsonl` path by design (training.md:459), checking `is_regular_file` first (`train.cpp:1896-1898`);
    - task and run ids are guarded at every store access (`valid_task_id`, `ledger.cpp:86-94`; `valid_run_id`, `manifest.cpp:179-191`), the pattern to copy, though with an escape-only rule here.

## Acceptance criteria

- [ ] **Given** `training/datasets/` exists and `l3/victim.jsonl` sits in the home's parent folder, **when** `datasets delete --yes -- ../../../victim` runs, **then** it exits 1 with one stderr line, `apogee datasets: '../../../victim' is not a dataset name -- a name, never a path`. It prints no `will remove:`, and the victim keeps its bytes. Today it prints `removed.`, exits 0, and the file is gone.
- [ ] **Given** an absolute name (`<dir>/abs/victim`) or `sub/inner`, with or without the datasets folder, **when** `datasets info` or `datasets delete --yes` runs, **then** it exits 1 with the same words, and nothing is read or removed.
- [ ] **Given** a listed `my set`, **when** `datasets info -- "my set"`, `GET /v1/admin/datasets/my%20set` or `DELETE /v1/admin/datasets/my%20set` runs, **then** each succeeds (exit 0, 200, 200), and the GET body is byte for byte what `datasets info --output-format json` prints. Today HTTP answers 400.
- [ ] **Given** `a..b`, `-h`, `.hidden`, `café`, `a:b` (not on Windows) or a 130-byte name in the folder, **then** `datasets info` and `datasets delete --yes` take each one, a dash-led one after `--`. So the full-screen shell's and the Mac app's deletes, which pass the listed name, reach every name they reach today.
- [ ] **Given** a name the read rule refuses, **when** `GET` or `DELETE /v1/admin/datasets/{id}` receives it, **then** it answers 400 with the rule's words, the CLI's line without its `apogee datasets:` prefix.
- [ ] **Given** `datasets synth --teacher <t> --kit <k> -- "my set"`, **then** it exits 1 with the write rule's words before the existence check, the kit and the teacher: no `[synth] teacher` line, no teacher call, no file.
- [ ] **Given** `train run --dataset "my set"`, with that dataset listed and no file named `my set` in the working folder, **then** it resolves the dataset by name. This is checked at `resolve_dataset`; the run itself is not part of the fixture. `train eval --suite "my set"` finds `my set.eval.jsonl`, and a `.jsonl` path keeps resolving as a path.
- [ ] **Given** `datasets info ` or `datasets delete ` at Tab, with no dataset matching what is typed, **then** no file names are offered. `train run --dataset ` still offers them.
- [ ] **Given** `datasets create --from empty ok` and `POST /v1/admin/datasets {"name":"my set"}`, **then** each behaves as today (exit 0, and 400). An id holding `/` is still refused: 404 `no route` at the router, and 400 at the handler.

## Constraints

- **A dataset is named, not pathed.** One read rule, `dataset_read_error`, in `business/training`, reached alike by the CLI, operations and httpserver. No reader joins an unchecked name to the folder.
- **One escape check, in contracts.** The rule's escape check is the one CLI-M45's collection rule uses, in `data/contracts`. The layer law ([ADR 0001](../../../adrs/cli/layer-enforcement.md)) lets business include data, never the reverse, so the shared part sits low enough for both.
- **The read rule accepts every listed dataset** ([ADR 0006](../../../adrs/cli/backwards-compatibility.md)). It refuses only what can leave the folder, with no character set or cap below what exists. Two hand-placed names that `list` shows are refused. No Apogee writer makes either by itself; only `datasets prepare --out` pointed at that exact file could:
  - a name holding `\` (the open question below);
  - a file named `...jsonl`, which lists as `..` (by reading `list`, `datasets.cpp:121-127`; not measured).
- **Mode parity** ([ADR 0002](../../../adrs/cli/mode-parity.md)). `datasets info --output-format json` and `GET` stay byte for byte. `delete` and `DELETE` refuse and accept the same names, with the same words.
- **The documents keep their shape** (ADR 0006's additive rule). `datasets list`'s and `info`'s JSON and the HTTP bodies are unchanged, and nothing reaches machine mode. A refusal is an exit 1 on the CLI and a 400 over HTTP, like the verbs' other refusals.
- **The offer is a contract** ([ADR 0007](../../../adrs/cli/tab-completion.md)). Completion keeps offering the folder's top-level names, all of which the verbs accept, and stops offering file names where the verb refuses a path.
- **`train run --dataset` keeps taking a `.jsonl` path**, by design (training.md:459), with its path branch first.
- **The create rule stays the write gate.** `valid_dataset_name` keeps guarding `write`, POST create, synth and prepare's derived stem. Narrowing it is CLI-M56, after this item.
- **The refusal is one line, the last on stderr, in the house voice**: `apogee datasets: '<name>' is not a dataset name -- a name, never a path`, said before any file is looked at.
- **Development runs on the dev channel** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md#development-runs-on-the-dev-channel)). Probes and tests use a temporary home under the scratchpad, never `~/.apogee` or `~/.apogee-dev`. A live check names `apogee-dev`.

## Technical approach

Paths under `lib/src/cli/`.

| File | Change |
|---|---|
| `source/data/contracts/collection_name.h`, `.cpp` (CLI-M45's) | One escape check both read rules call. It is true for the empty name, `..`, a `/`, a `\`, a NUL, and `std::filesystem::path(name).has_root_name()`, which fires only on Windows, so a macOS `a:b` keeps reading. CLI-M45 landed it (2026-11-27) as `harness::name_leaves_folder` in `contracts/collection_name.h`, the predicate `collection_read_error` asks, so this item calls it as it is. |
| `source/business/training/datasets.h`, `.cpp` | `dataset_read_error(std::string_view name)` strips one `.jsonl`, as `write` does (`:153-156`), then applies the escape check. It answers `'<name>' is not a dataset name -- a name, never a path`, or empty. For a refused name, `exists` answers false, `info` nullopt and `remove` those words, so `path_for` is never handed one. The check `write` runs (`:153-161`) is lifted into a function synth can call first, with today's words. |
| `source/presentation/cli/datasets.cpp` | `info` (`:580`) and `delete` (`:599`) call `dataset_read_error` first and fail with its words, before any existence check or `path_for`. synth runs the write check as its first step, before `store.exists` (`:451`), the kit (`:455`) and the teacher (`:469`). `info` and `delete` register their name with the names-only kind. |
| `source/presentation/httpserver/admin_datasets.cpp` | `GET` (`:168`) and `DELETE` (`:179`) take `dataset_read_error`, answering 400 with its words. POST create (`:131`) and synth (`:228`) keep `plain_name`, the write rule. |
| `source/presentation/cli/train.cpp` | `resolve_dataset` (`:1899`) and `resolve_suite` (`:1952`) take the read rule for the name branch. The path branches (`:1896`, `:1947`) stay first. |
| `source/presentation/cli/command.h`, `complete_sources.cpp` | A names-only kind for `datasets info` and `delete`. It lists the same `DatasetStore::list()` names as `DATASET` (`:413-418`), with `paths` false, and joins `kNameValues`. `train run --dataset` keeps `DATASET` and its file fallback, and `DATASET`'s comment (`command.h:64`) keeps saying "or a `.jsonl` path". |
| `tests/business/training/datasets_test.cpp`, `tests/presentation/cli/datasets_test.cpp`, `tests/presentation/cli/train_test.cpp`, `tests/presentation/cli/complete_sources_test.cpp`, `tests/presentation/httpserver/admin_datasets_test.cpp`, and `tests/data/contracts/` for the shared check | The fixtures under Testing. |

The item extends `DatasetStore`'s readers with a refusal. Every caller already handles a reader that answers no (`exists` false, `info` nullopt, `remove` with words), so no caller needs a new error path. The CLI's two verbs and the two HTTP routes call the rule first only to say its words rather than `no dataset named`.

It consumes CLI-M45's decisions on the rule's shape:
- escape-only;
- `has_root_name` rather than a list of drive spellings;
- `\` refused on every platform.

It hands CLI-M56 a reader that no longer depends on `valid_dataset_name`, so M56 can narrow the create rule without stopping an existing name from reading.

**Reference.**
- `valid_task_id` (`ledger.cpp:86-94`, documented at `ledger.h:43-45`: "an id is a directory name and never reaches outside `tasks/`") and `valid_run_id` (`manifest.cpp:179-191`). Both are checked at every store access, which is the pattern here. This item's rule is escape-only, because dataset names already exist in shapes their character sets would refuse.
- CLI-M45's collection read rule and its constraint, "The read rule accepts every store that exists".
- HTTP's own `plain_name` at every route (`admin_datasets.cpp:26-28`), the guard the CLI never had, now given the right rule.

**Docs when it ships:**
- training.md's `datasets kits`, `list`, `info`, `delete` section (:296-300): a dataset is named, not pathed; `train run --dataset` stays the one dataset verb that takes a path.
- http-api.md's `GET` and `DELETE /v1/admin/datasets/{id}` (:1296-1303): any listed name, and 400 for a name that could leave the folder.
- DEVELOPER.md's `training/` row (`datasets.h/.cpp`: one read rule at every reader), and its `training/datasets_test.cpp`, `cli/datasets_test.cpp` and `httpserver/admin_datasets_test.cpp` rows.
- MILESTONES: a dated entry.

## Testing

Every fixture is hermetic: homes, victims and hand-placed files inside the test's own temp directory, mock backends only. Each fails on today's code unless marked as a guard.

- **The bug as a fixture: the traversal** (`tests/presentation/cli/datasets_test.cpp`).
  - The home sits three levels deep inside the temp directory (`<tmp>/l1/l2/l3/home`), with `training/datasets/` present, so `../../../victim` is `<tmp>/l1/l2/l3/victim.jsonl`, and a climb that goes too far still lands inside the temp directory.
  - Victims: that file, an absolute `<tmp>/abs/victim.jsonl` and `<home>/training/datasets/sub/inner.jsonl`.
  - For `../../../victim`, `<tmp>/abs/victim` and `sub/inner`, each after `--`, `datasets info` and `datasets delete --yes` exit 1. stderr's last line holds `is not a dataset name -- a name, never a path`, stdout holds no `will remove:`, and every victim's bytes are unchanged.
  - The same again with no `training/datasets/` folder.
- **Listed names** (the same file), hand-placed: `my set`, `café`, `.hidden`, `a..b`, `-h`, `a:b` (not on Windows) and a 130-byte name.
  - `datasets info -- <n>` exits 0 for each, and `datasets delete --yes -- <n>` removes each.
  - A hand-placed `back\slash.jsonl` (not on Windows): `info` exits 1 with the rule's words, and the file stays.
- **synth before the teacher** (the same file). `datasets synth --teacher prose --kit reasoning --count 1 -- "my set"` uses the fixture's teacher that yields nothing usable. Today it ends `no usable examples` (the existing case at `:251-255` shows those words), proof the teacher ran. After the fix it exits 1 with `is not a dataset name`, stderr holds no `[synth] teacher` line, and no file exists.
- **The store** (`tests/business/training/datasets_test.cpp`):
  - `dataset_read_error`'s table. Refused: `""`, `..`, `.jsonl`, `a/b`, `../x`, `/abs/x`, `a\b` and a name holding a NUL, plus `C:x` and `D:notes` under `_WIN32`. Accepted: `my set`, `café`, `.hidden`, `a..b`, `-h`, `x.eval`, `a.jsonl` and a 130-byte name.
  - `exists`, `info` and `remove` on `../escape`, with a file at that path, answer false, nullopt and the words, and the file is untouched.
  - The writer's refusals (`:85-86`) hold (guard).
- **HTTP** (`tests/presentation/httpserver/admin_datasets_test.cpp`), hand-placed `my set` and `café`:
  - `GET` of `my set` answers 200, byte for byte the CLI's `datasets info --output-format json`, which extends the file's parity proof;
  - `DELETE` of `café` answers 200 and removes the file;
  - a `GET` of `../x` stays 400 (guard, `:220`), and `POST {"name":"my set"}` stays 400 (guard).
- **The train readers** (`tests/presentation/cli/train_test.cpp`):
  - `resolve_dataset(<datasets>, "my set")` returns the listed file;
  - `resolve_suite(<training>, "my set")` finds `my set.eval.jsonl`;
  - `"../x"` with no such file stays `no dataset named`;
  - a `.jsonl` path keeps resolving (guard, `:449-454`).
- **Completion** (`tests/presentation/cli/complete_sources_test.cpp`): the names-only kind lists `alpha` with `paths` false, and `DATASET` keeps `paths` true (guard, `:259-261`).
- **Guards**:
  - `datasets delete --yes -- -h` on an existing `-h.jsonl` exits 0;
  - `datasets create --from empty ok` exits 0;
  - `datasets delete one` without `--yes` deletes nothing (`:284-286`).
- **Gate:** `lib/scripts/cicd.sh --app cli --test`, clang-format, and the MinGW scan of the changed files.

## Open questions

| Question | Status | Answer |
|---|---|---|
| Refuse `\` on every platform, as CLI-M45 does, even though a macOS name holding `\` cannot escape? | `[default]` | Yes, as M45 does, so a name means the same thing on every platform. A hand-placed macOS `back\slash.jsonl` then stops reading through the CLI. No Apogee writer makes one by itself (the create rule refuses `\`; only `datasets prepare --out` pointed at that exact file could), and `rm` removes it. |
| Should `datasets info` and `delete` keep accepting a path as a convenience? | `[default]` | No. They are documented twins of HTTP routes that take names (machine-mode.md:1434, http-api.md:1302), and training.md:299 says `delete <name>`. `train run --dataset` stays the one dataset verb that takes a path. |
| Land before CLI-59d? | `[default]` | Yes, as a priority, with a Relates-to link. It does not make 59d's door safe for paths: `datasets prepare --out <any path>`, `embed ingest <path>`, `train run --dataset <path>` and `--kit <path>` still take paths through it, so 59d needs its own rule for path arguments. |
| Should `datasets info` and `delete` stop offering the shell's file names at Tab? | `[default]` | Yes. They get a names-only kind listing the same names, because a file name offered to a verb that refuses paths is a completion bug (ADR 0007). `train run --dataset` keeps `DATASET` and its file fallback. |
| Should a new dataset name refuse a leading `-`, a Windows device name, a `.eval` suffix or a second `.jsonl`? | `(consumed decision)` | [CLI-M56](a-new-dataset-name-is-one-every-verb-reads.md) owns the create rule, after this item. |
| Should the full-screen shell pass `--` before a listed dataset's name? | `(consumed decision)` | [CLI-M51](the-tuis-row-actions-reach-a-dash-led-row.md) owns it. Until it ships, the shell's delete of `-h` prints the verb's help, as today. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-26 | **Specced** from APP-M21's CLI aside ("datasets have no name rule"), a sandboxed investigation, two skeptics and a critic (Evidence). The defect: the store's readers join any string to the folder, while HTTP uses the create rule as its read rule. | the spec |
| 2026-11-26 | **Placed in Maintenance as M55.** The user's call was to spec the APP-M21 and APP-M22 CLI asides as maintenance items from M48, grouped only where they share one seam. This item's seam is the dataset store's readers and the one predicate `train.cpp` and HTTP share with the writer. The create rule is split off as CLI-M56. It narrows which new names are accepted, so its compatibility and parity effects differ, and it can only move once this item has moved the readers off `valid_dataset_name`. | the user |
| 2026-11-26 | **An escape-only read rule, not `valid_dataset_name`.** The first proposal, the create rule as the read rule, was overturned by probe (Evidence 9): a listed dataset must stay readable and deletable through every door, and the full-screen shell and the Mac app delete by the listed name. | the spec |
| 2026-11-26 | **Blocks CLI-M56.** M56 narrows the create predicate, which is also the read gate in `train.cpp` and on HTTP today. Narrowed before this item, it would stop existing names like `-h` from reading. | the spec |
| 2026-11-26 | **Relates to CLI-M45, no gate.** It is the same rule's shape. The escape check lives in `data/contracts`, which the layer law keeps reachable from both, so whichever item lands second builds on the other's check. | the spec |
| 2026-11-26 | **Relates to CLI-59d, not a block, but best landed first.** 59d's door would run these verbs from a paired device. This item doesn't make the door safe for path arguments, which needs 59d's own rule, so a block would promise what this item cannot give. | the spec |
| 2026-11-26 | **A contract and parity bug, not an exploitable one today** (Evidence 13). Every input that reaches the unchecked readers is the person's own typing, and HTTP guards its routes. | the spec |
| 2026-11-26 | **Three deliberate changes to today's behaviour**, none reversing a recorded intent. `datasets info` and `delete` stop taking a path; no record promises it, and the docs call them name verbs and HTTP twins. HTTP `GET` and `DELETE` accept listed names the create rule refuses; their 400 for such names is undocumented and contradicts the twin claim. A hand-placed macOS name holding `\` stops reading (the open question). | the spec |
| 2026-11-26 | **The completion kind is part of this item.** It was found while writing this ticket (Evidence 10). Without it, the change would leave Tab offering file names that `info` and `delete` now refuse. | the spec |

## Out of scope

- **The create rule for new names**: a leading `-`, Windows device names, the `.eval` suffix and `x.jsonl.jsonl` landing on `x`. [CLI-M56](a-new-dataset-name-is-one-every-verb-reads.md) owns it.
- **`datasets prepare --out <any path>`**, by design and documented (training.md:226).
- **The full-screen shell passing a listed name without `--`** (`tui_training.cpp:278-279`): [CLI-M51](the-tuis-row-actions-reach-a-dash-led-row.md).
- **The Mac app passing `--` before a dataset's name**: [APP-M21](../../../assistant/darwin/MILESTONES.md#2026-11-26--listed-name-reaches-the-verb-maintenance-item-m21-a-listed-name-reaches-the-verb-as-listed).
- **synth's printed next step**, `Train on it with:  apogee train run <snapshot> --dataset <name>` (`presentation/cli/datasets.cpp:520`), which prints a dash-led name bare: [CLI-M48](a-suggested-command-runs-as-printed.md).
- **A rule for path arguments on CLI-59d's door** ([reads and commands over the network](../v0.2.3/reads-and-commands-over-the-network.md)): 59d's own.
- **Adjacent oddities the investigation left as asides**, not in this item:
  - `train run --dataset notes` prefers a file named `notes` in the working folder over the dataset (`train.cpp:1896-1898`), an order training.md:459 does not state;
  - dataset refusals echo control characters raw on stderr (`'a\x01b' is not a dataset name`);
  - symlinks inside the datasets folder.
- **Windows**, not measured: no Windows host was run. A `\` and a root name (`C:x`) as dataset escapes are covered by the rule by reading only.

## Links

- **Blocked by:** — · **Blocks:** [CLI-M56](a-new-dataset-name-is-one-every-verb-reads.md), which narrows the create rule once this item has moved the readers off it
- **Relates to:**
  - [CLI-M45](../../../assistant/cli/MILESTONES.md#2026-11-27--a-collection-is-named-not-pathed-at-every-reader-maintenance-item-m45-a-collection-is-named-not-pathed-at-every-reader) (shipped 2026-11-27), the same read rule for collections: whichever lands second shares the escape check;
  - CLI-59d (v0.2.3, [reads and commands over the network](../v0.2.3/reads-and-commands-over-the-network.md)), whose door would run these verbs: best landed first, though this item does not make that door safe for paths;
  - [CLI-M57](a-saved-chat-names-only-its-own-files.md), the same before-59d priority for a saved chat's id;
  - [CLI-M51](the-tuis-row-actions-reach-a-dash-led-row.md), the full-screen shell's delete of a dash-led dataset;
  - [CLI-M48](a-suggested-command-runs-as-printed.md), synth's printed `train run … --dataset <name>` line;
  - [APP-M21](../../../assistant/darwin/MILESTONES.md#2026-11-26--listed-name-reaches-the-verb-maintenance-item-m21-a-listed-name-reaches-the-verb-as-listed), whose aside this item answers and whose `datasets delete --yes -- -h` keeps working;
  - [ADR 0002](../../../adrs/cli/mode-parity.md), [ADR 0006](../../../adrs/cli/backwards-compatibility.md), [ADR 0007](../../../adrs/cli/tab-completion.md).
- **Follows from:** MILESTONES' `training-datasets` record (Milestone Z, 2026-09-19), whose parity table calls `datasets delete` a twin and `info` read-only. This item makes the twin claim true.
