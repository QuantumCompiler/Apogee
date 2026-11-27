# CLI-M54 · Rename and HTTP GET find a config entry by its case-folded name

| Field | Value |
|---|---|
| **Type** | 🐞 Bug |
| **Project** | CLI |
| **Status** | 🟢 Ready |
| **Fix version** | Maintenance (any time) |
| **Component** | Config › `config rename-backend`, its HTTP twin and the saved-chat sweep (`operations/renames`, the editor's `rename_backend`, `logger::rename_session_backend`); the admin plane's `GET /v1/admin/agents/{id}` |
| **Labels** | `config` `rename` `case-folding` `admin-plane` |
| **Epic** | — (maintenance has no track) |
| **Blocked by** | — |
| **Blocks** | — |
| **Reporter** | The investigation of the APP-M21 and APP-M22 probes' CLI asides (2026-11-26): found by its sandboxed investigator, then two skeptics and a critic the same day |
| **Created** | 2026-11-26 |

---

## Summary

`apogee config rename-backend foo bar` says it renamed the backend "in …, every reference to it with it". But a role pointer written `FOO` is left naming a backend that no longer exists, and `check` fails. The rename finds the backend case-folded, as every config lookup does, then hands the name as typed to an editor that matches the key and every reference exactly. So `rename-backend Foo bar` is refused outright, and the saved-chat sweep would miss a chat on `FOO`. Beside it, `GET /v1/admin/agents/{id}` is the one single-entry GET that matches exactly: `Code-Review` and `FOO2` answer 404. The item carries the entry's stored key through the rename and folds every match after it. It also folds the agent GET inside the loop it already runs, so the bundled agents keep answering.

## Description

### User story

**As** someone renaming a backend, or a client reading one agent over HTTP,
**I want** Apogee to find the entry by its name in any case, as every other verb does, and a rename to rewrite every reference however it is spelled,
**so that** "every reference to it with it" is true, `check` passes after a rename, and an agent the list shows answers its GET.

### Steps to reproduce

The sandbox, 2026-11-26, with the built binary `apogee 0.2.0 (33ea685d, 2026-11-22, macos-arm64)`: a throwaway `APOGEE_HOME` and `HOME` three levels deep, `env -i PATH=/usr/bin:/bin`, stdin from `/dev/null`, mock backends only.

1. In a fresh home, run `config init` and `config add-backend foo --type mock`.
2. Run `config set-default FOO`, then `config rename-backend foo bar`. Run `grep -n` on `config.json`, then `models status` and `check`.
3. In a second fresh home, run the same `set-default`, then `config delete-backend foo` instead.
4. With backend `foo`, run `config rename-backend Foo renamed3`.
5. With a config agent `foo2` (made by `agents create foo2`), start `serve --bind 127.0.0.1` with the home's admin token and send `GET /v1/admin/agents/code-review`, `/Code-Review`, `/foo2` and `/FOO2`.

### Actual result

2. `models.default = FOO`, exit 0. Then `renamed backend 'foo' to 'bar' in <home>/config/config.json, every reference to it with it`, exit 0. `config.json:69` still holds `"default": "FOO"`, beside `529:    "bar": {`. `models status` shows `chat: FOO   [not configured]`, and `check` gives `FAIL  default_backend  names a backend that is not configured: 'FOO'`, exit 1.
3. `removed backend 'foo' from …` and `models.default cleared: it named 'FOO'`, exit 0: the delete clears the same pointer the rename leaves behind.
4. `apogee config: no backend named 'Foo'`, exit 1.
5. `code-review` and `foo2` answer 200. `Code-Review` answers `{"error":{"message":"no agent named 'Code-Review'","type":"not_found_error"}}` with HTTP 404, and `FOO2` answers 404.

### Expected result

2. `models.default` reads `bar`, `models status` no longer says `[not configured]`, and `check` has no `default_backend` failure. The line's "every reference" is true.
3. As today.
4. Exit 0: the key `foo` is renamed `bar`, and every reference to it, in any case, names `bar`. A case-only rename (`foo` to `Foo`) stays allowed.
5. All four answer 200, as `DELETE /v1/admin/agents/{id}` and every other section's GET do. The view carries the entry's own name.

### Background

`config rename-backend` arrived with 46a (MILESTONES, 2026-10-14, `rename-propagation`; commit 20f6b859 per the investigator), with its HTTP twin `POST /v1/admin/backends/{id}/rename` through the one core. The editor's transform renames the key where it stands. It then rewrites every value naming the key at a reference path (`backend_reference_paths`), and the operation sweeps the saved chats. Its record is firm about the reach: "Every reference, not only the listed three … leaving those would be the silent half-rename the item forbids." Its refusals already fold: "a taken one (folded, as `add-backend` folds it, a change of case allowed)".

Config entry names compare case-insensitively everywhere else:
- The loaded maps are keyed by `CaseInsensitiveLess` (`config.h:1150-1169`, `:1176-1227`). Two keys differing only by case are refused at load, so a name that folds to a key can only mean that entry.
- `find_backend`, `find_agent` and the other `find_*` fold, and so do the line editor's `find_entry_line` (`config_edit.cpp:629-650`) and `find_bundled_agent` (`assets.cpp:21-29`).
- `delete-backend` clears the role pointers that name the entry "case aside, as the roles resolve" (`backend_fields.cpp:403-416`).

The rename is the exception. Its operation checks the name folded, then passes the name as typed to every exact match that follows. A reference in another case is easy to write, since `config set-default FOO` stores what was typed. So the half-rename 46a forbade happens for any reference spelled differently from the key, and the success line still claims every reference.

`GET /v1/admin/agents/{id}` arrived with `analyze-agents` (MILESTONES, 2026-09-13; commit f37cf4ad per the investigator). It walks `all_agents`, the bundled seven plus the config's entries, and compares each name exactly. Its own DELETE uses the folded `find_agent`.

How it was found: the investigation of the CLI asides from the [APP-M21](../../../assistant/darwin/MILESTONES.md#2026-11-26--listed-name-reaches-the-verb-maintenance-item-m21-a-listed-name-reaches-the-verb-as-listed) and [APP-M22](../../../assistant/darwin/MILESTONES.md#2026-11-26--jobs-notes-reach-the-person-maintenance-item-m22-a-jobs-notes-reach-the-person) probes took the made-thing names as one seam. Checking APP-M21's aside that `symphonies delete -- -x` says `not found in config` while `show` finds it (now [CLI-M53](a-symphony-the-cli-writes-is-one-it-can-delete.md)), the investigation swept every show, delete and rename pair over six config sections. The sweep found two more lookups that do not fold: rename-backend's old name and the HTTP agent GET. Its review found the deeper half, the references compared exactly, and reproduced the dangling pointer (Evidence 1). The two sites are one item because both are case-folded reads of a config entry by name.

The design: resolve the entry once, by the folded lookup the rename already does, and carry its stored key into every step after it. Each match in the editor and the chat sweep compares folded, as `delete-backend`'s pointer clearing does. The agent GET compares folded inside the loop it already runs.

### Evidence

All measured 2026-11-26 on `apogee 0.2.0 (33ea685d, 2026-11-22, macos-arm64)`, channel release, by an investigator, two skeptics and a critic, each in its own sandbox: `env -i PATH=/usr/bin:/bin`, `APOGEE_HOME` and `HOME` three levels deep, stdin from `/dev/null`, mock backends only, stdout, stderr and exit recorded apart. The one commit after 33ea685d, HEAD 5b8927c9, touches 11 CLI files and none in this item's code. `strings` finds HEAD's new completion text once, so the binary carries HEAD's CLI code where it matters. Probe labels: `i-` the investigator's sandboxes, `s1-` the cause skeptic's, `s2-` the fix skeptic's, `c-` the critic's.

1. **A rename leaves a reference in another case dangling, and says it didn't** (`c-r`, the JSONC config `config init` writes):
   - `config set-default FOO` (backend `foo`) gives `models.default = FOO`, exit 0.
   - `config rename-backend foo bar` gives `renamed backend 'foo' to 'bar' in <home>/config/config.json, every reference to it with it`, exit 0.
   - `grep -n` gives `69:    "default": "FOO"` and `529:    "bar": {`.
   - `models status` gives `chat: FOO   [not configured]`. `check` gives `FAIL  default_backend  names a backend that is not configured: 'FOO'`, exit 1.

   Reproduced again in `s1-r` and `s2-r`.
2. **`delete-backend` clears the same pointer** (`c-r2`, a fresh home, the same `set-default`). `config delete-backend foo` gives `removed backend 'foo' from …` and `models.default cleared: it named 'FOO'`, exit 0. So the two verbs that act on a backend's references read the same file two ways.
3. **The old name must be spelled as the file spells it** (`i-d`, a sweep over hand-written JSONC entries in six sections, each run on a fresh copy):
   - `config rename-backend Foo renamed3` beside `foo` gives `apogee config: no backend named 'Foo'`, exit 1. Beside it, `config delete-backend -- FOO` gives `removed backend 'FOO' …`, exit 0, and `models info -- FOO` shows `foo`.
   - For a hand-written `Bar`, `config rename-backend -- bar renamed` gives `no backend named 'bar'`, while `config delete-backend -- bar` gives `removed backend 'bar'`.
   - Every other delete, enable and disable in the six sections folds case, symphonies aside ([CLI-M53](a-symphony-the-cli-writes-is-one-it-can-delete.md)).

   The first was reproduced again in `s1-d`.
4. **The HTTP agent GET is the one exact single-entry GET** (`c-g`, `serve --bind 127.0.0.1 --port 18457` with the home's `--print-admin-token`, stopped afterwards):
   - `GET /v1/admin/agents/code-review` answers 200. `GET /v1/admin/agents/Code-Review` answers `{"error":{"message":"no agent named 'Code-Review'","type":"not_found_error"}}` with HTTP 404.
   - `GET …/foo2` answers 200, and `GET …/FOO2` answers 404.
   - On the investigator's serve (`i-g`), `GET /v1/admin/backends/FOO`, `mcp-servers/FOO`, `symphonies/FOO` and `suites/FOO` each answer 200. `DELETE /v1/admin/agents/FOO` answers 200 `{"deleted":"FOO","files_removed":[]}`.
5. **Root cause: the rename finds the entry folded, then hands on the typed name.** `rename_backend_everywhere` (`presentation/operations/renames.cpp:265-368`) checks `config.find_backend(old_name)` (`:276`), which folds. Every step after it takes `old_name` as typed:
   - the store-follow loop (`:288-293`), `name != old_name` (`:289`), and `derived_level(old_name, model)` (`:294`);
   - `repointed` (`:343`) and the editor, `harness::rename_backend(text, old_name, new_name)` (`:348`);
   - the success line (`:355-357`);
   - the chat sweep, `say_sweep` (`:366`).

   The editor (`data/contracts/config_edit.cpp`) matches exactly in both halves:
   - **YAML:** the key line, `token_text_of(…) == old_name` (`:2836`, in the key loop `:2823-2840`); the block walk's references (`:2920`); a flow map's references (`rename_in_flow_map`, `:2795`).
   - **JSONC:** the key, `std::ranges::find(backends.keys, old_name)` (`:3024`); the references, `rename_references` (`:2989`).

   The chat sweep, `rename_session_backend` (`data/logger/session.cpp:677-721`), compares `session.backend != old_name && session.rerank != old_name` and replaces each only on an exact match (`:703-711`). This dates from 46a (MILESTONES, 2026-10-14), commit 20f6b859 per the investigator. The same-name and collision checks (`config_edit.cpp:3058-3070`) are why a case-only rename works today: their own-entry test, `name != old_name` (`:3064`), sees the backend's own key when the old name is spelled as stored.
6. **Root cause: the agent GET.** `admin_get_agent` (`presentation/httpserver/admin_agents.cpp:178-210`) walks `harness::all_agents(*loaded.config)` (`:183`) and skips every agent whose `agent.name != name` (`:184`). `all_agents` (`data/contracts/assets.cpp:75-96`) lists the seven bundled agents under their own names, then the config's entries. The DELETE beside it uses `find_agent` (`admin_agents.cpp:223`), which folds but sees config entries only. A fix through `find_agent` alone would turn the bundled `code-review`'s measured 200 (`c-g`, point 4) into a 404. The route dates from `analyze-agents` (MILESTONES, 2026-09-13), commit f37cf4ad per the investigator.
7. **By reading, not probed.**
   - **The YAML half.** Every rename probe ran on the JSONC config `config init` writes. The YAML half's exact matches are the lines in point 5.
   - **The chat sweep.** No saved chat on a case variant was probed. The exact compare is `session.cpp:703-711`.
   - **The store-follow loop.** Today the editor's refusal comes after the store decision, so a typed `Foo` does no harm there. But folding the key alone would let a rename typed `Foo` read the backend itself as another one pointing at its store (`renames.cpp:288`). It would keep the store and say `'foo' still points at it`.
   - **The HTTP rename on a case variant.** `POST /v1/admin/backends/Foo/rename` passes the route's own folded check (`admin_config.cpp:431-433`). The core then refuses with `no backend named 'Foo'`, and the route maps that to 400 (`:434-437`).
   - **`models rename`.** Its family and folder renames call the editor's `rename_backend` with each backend's stored key (`renames.cpp:485`, `:691`). They print the same "every reference to it with it" (`:495-496`, `:716-717`) and sweep the same chats (`:510`, `:723`). So they too leave a reference in another case.
8. **How a reference in another case is written.**
   - `config set-default FOO` stores what was typed (`point_role`, `presentation/cli/config_cmd.cpp:1532-1539`, measured `models.default = FOO`). The other role setters go through the same `bind_set_role` (`:1243-1253`).
   - Hand edits, and any verb that writes a backend name as typed.

   Every reader folds, so such a config works until the rename.
9. **Not pinned.** Every rename test passes the old name as the file spells it, and every reference in the goldens is spelled as the key (`config_edit_test.cpp:1708-1809`, `config_jsonc_test.cpp:475-542`, `renames_test.cpp:101-149`, `admin_config_test.cpp:639-677`). The agent GET is tested only by the stored spelling (`admin_agents_test.cpp:142`, `:157`) and an unknown name (`nope`, `:163`).
10. **Blast radius.**
    - **One core, three doors.** `config rename-backend` (`config_cmd.cpp:1212-1240`), `POST /v1/admin/backends/{id}/rename` (`admin_config.cpp:413-448`) and the Mac app's Models › Rename the backend… (`ModelsViewModel.swift:69`, `config rename-backend <listed key> <new>`). The app passes the key as listed, so it never meets the refusal. It meets the dangling reference.
    - **`models rename`** reaches the same editor and sweep (point 7). The Mac app's model rows run it (`ModelsViewModel.swift:69`).
    - **18 reference paths** (`backend_reference_paths`, `config_edit.cpp:2963-2981`), plus the saved chats' `backend` and `rerank`. The paths are the six role pointers, a suite member written as the role's value or as a mapping's `backend`, a collection's `backend`, `rerank` and graph `extract_backend`, a named graph's `extract_backend`, an agent's `model`, the training judge, a regime's `teacher`, and the cycle's backend, judge and sources' `backend`.
    - **The agent GET** is one route with no CLI twin: the parity table pairs `agents create`, `edit` and `delete` only (`parity_test.cpp:104-106`). Its bundled branch serves the seven bundled agents.
    - **No client parses the words that change.** A search of `lib/src/darwin`, `lib/design/prototype` and `lib/src/mod` finds no `admin/agents/` call and no reading of `renamed backend '`.
11. **What already works**, and must stay:
    - **The delete:** `delete-backend` clears role pointers in any case (`backend_fields.cpp:403-416`, `c-r2`).
    - **55f's way out:** `config rename-backend -- -x renamed` renames an odd existing key (`i-d`).
    - **An exact old name** rewrites every reference spelled the same way, in both formats, with comments kept and the traps left alone: a same-named MCP server and collection key, a prompt that says the name, and `gemma-old` (the goldens, `config_edit_test.cpp:1772-1809`, `config_jsonc_test.cpp:512-542`). Through the real command, the sweep keeps each chat's `updated_at` (`renames_test.cpp:101-149`).
    - **A case-only rename** is allowed (`rename_backend(kRenameYaml, "gemma", "Gemma")`, `config_edit_test.cpp:1834`).
    - **The other HTTP lookups:** every other single-entry GET folds, and so does `DELETE /v1/admin/agents/{id}` (`i-g`). `find_bundled_agent` folds (`assets.cpp:21-29`).

## Acceptance criteria

- [ ] **Given** backend `foo` and `models.default` written `FOO` (by `config set-default FOO`), in the JSONC config `config init` writes and in a YAML config, **when** `config rename-backend foo bar` runs, **then** `models.default` reads `bar`, `models status` no longer shows `[not configured]` for chat, and `check` reports no `default_backend` failure. Today the pointer stays `FOO`, `check` fails `default_backend` with exit 1, and the line claims every reference.
- [ ] **Given** backend `foo`, **when** `config rename-backend Foo bar` runs, **then** it exits 0, the key reads `bar`, and every reference at a reference path written `foo`, `Foo` or `FOO` reads `bar`. Today it says `apogee config: no backend named 'Foo'`, exit 1. Likewise a hand-written `Bar` is renamed by `config rename-backend -- bar renamed`.
- [ ] **Given** a saved chat whose `backend` is `FOO` and whose `rerank` is `Foo`, **when** the rename runs, **then** both read `bar`, its `updated_at` is kept, and the output says `1 saved chat now names 'bar'`.
- [ ] **Given** a backend that alone runs its stored model under the M3 spelling, **when** it is renamed by its name in another case, **then** the store follows exactly as for the stored spelling, and no line says the backend itself still points at it.
- [ ] **Given** `models.default` written `FOO`, **when** `POST /v1/admin/backends/foo/rename {"name":"bar"}` is sent, **then** it answers 200, `models.default` reads `bar`, and the config is byte for byte the CLI's. `POST /v1/admin/backends/Foo/rename` answers 200 too, where today the core refuses it with 400.
- [ ] **Given** config agent `foo2`, **when** `GET /v1/admin/agents/FOO2` is sent, **then** it answers 200 with foo2's view, its `name` reading `foo2`. Today it answers 404 `no agent named 'FOO2'`.
- [ ] **Given** the bundled `code-review`, **when** `GET /v1/admin/agents/Code-Review` is sent, **then** it answers 200 with the bundled view (`"bundled": true`, the compiled-in bodies while unseeded). Today it answers 404.
- [ ] **Given** backend `foo` with `models.default` written `FOO`, config agent `foo` and the bundled `code-review`, **when** `config rename-backend foo Foo` (a change of case only), `GET /v1/admin/agents/code-review`, `DELETE /v1/admin/agents/FOO` and `config delete-backend FOO` each run on a fresh copy, **then** each behaves as today: the case-only rename lands, both HTTP calls answer 200, and the delete exits 0 clearing the pointer.
- [ ] **Given** a config whose references are spelled as the backend's key, **when** a rename runs with the old name spelled as the key, **then** its bytes, lines and HTTP body are what they are today (the editor goldens, `renames_test`, and the HTTP twin's parity case), and the traps keep their bytes in any case: an MCP server or collection keyed like the backend, a prompt that says the name, and a key that only begins with it.

## Constraints

- **One config mutation path** (CLAUDE.md → ⚠ One config mutation path). Both halves stay in the editor's `rename_backend`, pure text in and text out. The JSONC half keeps splicing in place through `jsonc::patch`, so every comment and every byte the rename does not touch stays (`config_jsonc_test`, `config_edit_test`).
- **A fold matches at reference paths only.** The walk, its paths and its traps are unchanged. Only the equality changes, from exact to folded, and it stays a whole-value compare, so a key that only begins with the name never matches.
- **Config entry names compare case-insensitively everywhere** (`CaseInsensitiveLess`, `config.h:1150-1227`). A case-only rename stays allowed (46a's "a change of case allowed"; `config_edit_test.cpp:1834`). Since the loader refuses two keys that differ only by case, a folded reference can name no other backend.
- **The fold is the one the config already uses**: `equals_folded` inside the editor (`config_edit.cpp:199`), and elsewhere `CaseInsensitiveLess` both ways, as `find_bundled_agent` does (`assets.cpp:24`). No new hand-written fold.
- **Mode parity** ([ADR 0002](../../../adrs/cli/mode-parity.md)). The HTTP rename shares the core and stays byte for byte the CLI's (`admin_config_test.cpp:639`, `parity_test.cpp:84`). The agent GET folds as its own DELETE and every other section's GET do.
- **The bundled agents keep answering.** The GET's fold sits inside the `all_agents` loop, never in a lookup through `find_agent` alone.
- **Older configs and chats keep working** ([ADR 0006](../../../adrs/cli/backwards-compatibility.md)). No reference that resolves today stops resolving, and every config that loads still loads. No response gains, loses or changes a field: the rename's `{renamed, to, said, restart_required}` and the agent view keep their shape.
- **The house voice holds.** Refusals keep their words (`no backend named '<name>'`, `no agent named '<name>'`), and the success lines stay on stdout as today.

## Technical approach

Paths under `lib/src/cli/`.

| File | Change |
|---|---|
| `source/presentation/operations/renames.cpp` | Right after the folded lookup (`:276`), `old_name` becomes the entry's stored key, the key of `config.backends.find(old_name)`. The store-follow loop (`:288`), `derived_level` (`:294`), `repointed` (`:343`), the editor (`:348`), the success line (`:355-357`) and the sweep (`:366`) then all take the name as the file writes it. |
| `source/data/contracts/config_edit.cpp` | `rename_backend` matches the key and every reference folded: the YAML key line (`:2836`), the block walk (`:2920`), a flow map (`:2795`), the JSONC key (`:3024`, a folded find over `backends.keys`) and the JSONC references (`rename_references`, `:2989`). The own-entry test at `:3064` folds too, so a caller passing a case variant (`FOO` to `Foo`) still counts `foo` as its own entry. The same-name and collision checks (`:3058-3070`) otherwise run as they do, and a case-only rename passes them. |
| `source/data/logger/session.cpp` | `rename_session_backend` compares a chat's `backend` and `rerank` with the old name folded, in the test and in each replacement (`:703-711`). |
| `source/presentation/httpserver/admin_agents.cpp` | `admin_get_agent` compares folded inside the `all_agents` loop (`:184`). The view keeps the agent's own `name`, and `find_bundled_agent(name)` (`:189`) already folds. |
| `tests/data/contracts/config_edit_test.cpp`, `config_jsonc_test.cpp`, `tests/presentation/operations/renames_test.cpp`, `tests/data/logger/session_test.cpp`, `tests/presentation/httpserver/admin_agents_test.cpp`, `admin_config_test.cpp` | The fixtures under Testing. |

The fix takes the folded lookup the rename already makes at its door and makes it the only reading of the name. The operation resolves the stored key once, so every later step works on the file's own spelling, and the success line names the backend as the file wrote it. In the editor, the key match folds for a caller that passes a case variant, and the reference matches fold for references written in another case, whoever the caller. Because `models rename` reaches the same editor and sweep (`renames.cpp:485`, `:691`, `:510`, `:723`), its backend renames gain the fold with no change of their own. The agent GET keeps its loop, so a bundled agent and an entry overriding one are found as today, in any case.

[CLI-M44](yaml-editor-refuses-the-layouts-it-does-not-write.md) rewrites the YAML key loop (`:2823-2840`) to recognise a quoted key and refuse it by name. Whichever item lands second merges the folded match into the other's loop. No other item owns a decision this one takes.

**Reference.** `delete_backend_text` (`backend_fields.cpp:401-422`), which clears role pointers "case aside, as the roles resolve". `find_entry_line` (`config_edit.cpp:629-650`), the editor's own folded entry lookup. `find_bundled_agent` (`assets.cpp:21-29`), a folded compare inside a loop over named entries. 46a's goldens and their traps (`config_edit_test.cpp:1708-1809`), the shape the case-variant goldens copy.

**Docs when it ships:**
- DEVELOPER.md: the `operations/` row's `renames.h/.cpp` (the key found in any case, every reference matched case aside, as `delete-backend`'s pointers are), and the `logger/` table's `session.h/.cpp` row, its `rename_session_backend` (a chat naming the backend in any case).
- `reference/cli/http-api.md`: `POST /v1/admin/backends/{id}/rename` (the old name and its references in any case) and `GET /v1/admin/agents/{id}` (any case, bundled agents included).
- MILESTONES: a dated entry, saying 46a's "every reference" now holds in any case.

## Testing

Each fixture fails on today's binary unless marked as a guard. Every case runs hermetically in its own temporary home (`CliHome`), with mock backends and no network. A manual check runs on the dev channel (`apogee-dev`) or a scratch `APOGEE_HOME`, never the release install.

- **The bug as a fixture**, in `renames_test.cpp` through the real commands, once on a JSONC config and once on a YAML one, with mock backend `foo`:
  - `config set-default FOO`, then `config rename-backend foo bar`: `models.default` reads `bar`, and `check` has no `default_backend` FAIL.
  - `config rename-backend Foo bar` exits 0. References written `foo` and `FOO` at `models.default`, `default_utility`, a suite member and an agent's `model` all read `bar`.
  - A saved chat written with `backend: FOO` and `rerank: Foo` reads `bar` in both, its `updated_at` kept, and the output says `1 saved chat now names 'bar'`.
  - The coupling case (`gemma-Q4KM` to `mine-Q4KM`) run with the old name typed `GEMMA-q4km`: the store follows as it does for the stored spelling.
- **The editor's goldens**, in `config_edit_test.cpp` and `config_jsonc_test.cpp`:
  - A copy of `kRenameYaml` and of `kRenameJsonc` with the references written in mixed case (`GEMMA`, `Gemma`). Renamed by `gemma`, and again by `GEMMA`, each gives the same expected bytes with every reference renamed.
  - The traps in other cases keep their bytes: an MCP server keyed `GEMMA`, a prompt saying `GEMMA`, and a key `GEMMA-old`.
  - A direct `rename_backend(text, "GEMMA", "Gemma")` is a case-only rename, not a collision with `gemma`.
  - Guard: the existing goldens, both directions, byte-identical.
- **The sweep**, in `session_test.cpp`: `rename_session_backend("foo", "bar")` over chats on `FOO`, `Foo` and `foo`, and a rerank on `FOO`, rewrites all four. A chat on `food` is left alone.
- **HTTP**:
  - In `admin_config_test.cpp`, `admin_rename_backend(context, "Foo", {"name":"bar"})` answers 200 and leaves the config byte for byte as the CLI's `config rename-backend Foo bar`. With `models.default` `FOO`, the route through `foo` repoints it.
  - In `admin_agents_test.cpp`, `admin_get_agent(context, "MINE")` answers 200 with `name` `mine`, and `admin_get_agent(context, "Release-Notes")` answers 200 with `bundled: true` and the compiled-in bodies.
  - Guards: `nope` answers 404, and `DELETE` of a case variant stays 200.
- **Guards**: `config rename-backend foo Foo` exits 0, and the key reads `Foo`. `config delete-backend FOO` exits 0 and clears the pointer.
- **Gate:** `lib/scripts/cicd.sh --app cli --test`, clang-format, and the MinGW scan of the changed files.

## Open questions

| Question | Status | Answer |
|---|---|---|
| Should `config set-default` (and the other role setters) also store the entry's own spelling, so no new references in another case get written? | `[default]` | Not in this item. Rename and delete fold, which covers every way such a reference is written, the hand edit included. Storing the canonical spelling at the setters is a separate polish, left as an aside. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-26 | Diagnosed from the investigation, two skeptics and a critic (Evidence). **The defect is the name's reading, not the editor's walk**: the rename finds the entry folded, then hands the typed name to matches that compare exactly. The fix carries the stored key and folds every match. | the spec |
| 2026-11-26 | Placed in **Maintenance** as M54, by the user's call to spec the APP-M21/M22 CLI asides as maintenance items from M48, grouped only where they share one seam. This item's seam is one folded read of a config entry by name, at the rename core and the agent GET. It is not a write-order problem like [CLI-M52](a-refused-scaffold-writes-nothing.md)'s, and not the symphony branch of the editor that [CLI-M53](a-symphony-the-cli-writes-is-one-it-can-delete.md) fixes. | the user |
| 2026-11-26 | **The references fold, not only the key.** Folding the key alone would make `rename-backend Foo bar` rename the key and miss every reference written `foo`, which is worse than today's refusal. The dangling pointer is measured in `c-r`, `s1-r` and `s2-r` (Evidence 1). | the spec |
| 2026-11-26 | **The agent GET folds inside its loop, not through `find_agent`**, which sees config entries only and would turn the bundled agents' GET into a 404. | the spec |
| 2026-11-26 | **The symphony delete's trim went to CLI-M53**, though the investigator first grouped it here with the other lookups. It is the same verb and fixture as M53's delete. | the spec |
| 2026-11-26 | **Not gated on CLI-M44, nor it on this.** Both edit the YAML key loop of `rename_backend` (`:2823-2840`). Either order works, and whichever lands second merges the folded match into M44's quoted-key lookup. | the spec |
| 2026-11-26 | **No recorded intent is reversed.** This completes 46a's "every reference, not only the listed three" (MILESTONES, 2026-10-14) and keeps its folded collision check and case-only rename. | the spec |

## Out of scope

- **The symphony delete's trim and its typed echo** (`symphonies delete -- " first"` says `no symphony named ' first'` while `show` finds `first`): [CLI-M53](a-symphony-the-cli-writes-is-one-it-can-delete.md).
- **The role setters storing the typed spelling** (`config set-default FOO` writes `FOO`): the open question above, not this item.
- **Commands echoing the typed spelling instead of the entry's.** `GET /v1/admin/mcp-servers/FOO` returns `"name":"FOO"`, and `config delete-backend FOO` says `removed backend 'FOO'`. The HTTP rename's `renamed` field likewise echoes the path as sent. An aside, not in this item.
- **`config get` covering only some sections.** `config get agents.bar` and `config get symphonies.-x` answer `no such config key`, while `backends.*` works and folds. An aside.
- **`config delete-backend -- " foo"` for an absent name** pointing at a rename of nothing (57e's recorded words, `missing_entry`, `config_edit.cpp:682-699`). An aside.
- **Quoted keys in the YAML key lookup**: [CLI-M44](yaml-editor-refuses-the-layouts-it-does-not-write.md).
- **The agent create's refusals and their HTTP mapping**, in the same `admin_agents.cpp`: [CLI-M52](a-refused-scaffold-writes-nothing.md).
- **The Mac app passing `--` before a backend in its rename row**: [APP-M21](../../../assistant/darwin/MILESTONES.md#2026-11-26--listed-name-reaches-the-verb-maintenance-item-m21-a-listed-name-reaches-the-verb-as-listed).

## Links

- **Blocked by:** — · **Blocks:** —
- **Relates to:**
  - [CLI-M44](yaml-editor-refuses-the-layouts-it-does-not-write.md), which rewrites the same YAML key loop: whichever lands second merges;
  - [CLI-M53](a-symphony-the-cli-writes-is-one-it-can-delete.md), the symphony delete's lookup, found by the same sweep, and [CLI-M52](a-refused-scaffold-writes-nothing.md), which edits the agent create beside this item's GET;
  - [CLI-M55](a-dataset-is-named-not-pathed-at-every-reader.md), [CLI-M56](a-new-dataset-name-is-one-every-verb-reads.md) and [CLI-M57](a-saved-chat-names-only-its-own-files.md), the other made-thing-name items from the same investigation; M57 changes the logger's loader and path join in the same `session.cpp` as this item's sweep, with no gate either way;
  - [CLI-M50](a-refused-backend-name-gets-the-rename-that-reaches-it.md), whose rename door runs this verb on the stored key, which works today and keeps working;
  - [CLI-M45](a-collection-is-named-not-pathed-at-every-reader.md), the same shape: one rule at every reader;
  - [APP-M21](../../../assistant/darwin/MILESTONES.md#2026-11-26--listed-name-reaches-the-verb-maintenance-item-m21-a-listed-name-reaches-the-verb-as-listed), whose Models row runs `config rename-backend` on the listed key and so meets this item's references, not its refusal; [APP-M22](../../../assistant/darwin/MILESTONES.md#2026-11-26--jobs-notes-reach-the-person-maintenance-item-m22-a-jobs-notes-reach-the-person), the other half of the probes whose asides led here;
  - CLI-59d (v0.2.3, [reads and commands over the network](../v0.2.3/reads-and-commands-over-the-network.md)), whose door would run `config rename-backend` from a paired device through this core, and [darwin 7c](../../darwin/v0.2.4/every-place-against-a-server.md), whose Models place renames against a server the same way;
  - CLI-63b (v0.2.4, [personal data has an owner](../v0.2.4/personal-data-has-an-owner.md)), which moves `sessions/` per person: the sweep this item folds reads `sessions_dir()` (`session.cpp:680`), so which people's chats a rename sweeps is 63b's to decide;
  - 46a (MILESTONES, 2026-10-14, `rename-propagation`), the record this item completes; 55f, the add rule a rename's new name follows; [ADR 0002](../../../adrs/cli/mode-parity.md) and [ADR 0006](../../../adrs/cli/backwards-compatibility.md).
