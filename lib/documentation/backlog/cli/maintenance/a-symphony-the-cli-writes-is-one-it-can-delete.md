# CLI-M53 · A symphony the CLI writes is one it can delete

| Field | Value |
|---|---|
| **Type** | 🐞 Bug |
| **Project** | CLI |
| **Status** | 🟢 Ready |
| **Fix version** | Maintenance (any time) |
| **Component** | Symphonies › `symphonies create`, `edit` and `delete`, the config editor's symphony branch (`contracts/config_edit.cpp`), and the `/v1/admin/symphonies` write and delete routes |
| **Labels** | `symphonies` `config` `names` |
| **Epic** | — (maintenance has no track) |
| **Blocked by** | — |
| **Blocks** | — |
| **Reporter** | The APP-M21 and APP-M22 probes' CLI asides (2026-11-26), then a sandboxed investigation, two skeptics and a critic the same day |
| **Created** | 2026-11-26 |

---

## Summary

Once a config has a `symphonies` section, `apogee symphonies create … -- -y` writes `"-y"` into `config.json` and exits 0. The symphony shows and plays, but `apogee symphonies delete -- -y` says `symphony '-y' not found in config`, and so does the HTTP delete. Every other config section refuses such a name when it is added, and refuses it by name when it is deleted. The symphony branch of the config editor missed both shared rules: 55f's add rule and 57e's `missing_entry`. The item gives that branch both rules, so the CLI and HTTP refuse a new dash name before writing it and refuse an existing one by name. It also makes `symphonies delete` find a symphony the way `show` does, and gives the HTTP write refusals their documented error type.

## Description

### User story

**As** someone who names, replaces and deletes symphonies from the command line, over HTTP or from the Mac app,
**I want** Apogee to refuse a symphony name it can't edit later, and to say why when I try to delete one that is already there,
**so that** I never end up with a symphony I can play but can't remove, or a "not found" for an entry `show` has just shown me.

### Steps to reproduce

The sandbox, 2026-11-26, with the built binary `apogee 0.2.0 (33ea685d, 2026-11-22, macos-arm64)`. It used a throwaway `APOGEE_HOME` and `HOME` three folders deep, `env -i PATH=/usr/bin:/bin`, stdin from `/dev/null`, `config init` (the shipped JSONC `config.json`) and `config add-backend echo --type mock`.

1. Run `apogee symphonies create --stage 's:chat:Echo: {{input}}' first`, then `apogee symphonies create --stage 's:chat:Echo: {{input}}' -- -y`. Then look for `"-y"` in `config.json`.
2. Run `apogee symphonies play --input hello -- -y`, then `apogee symphonies create --force --stage 's:chat:Changed: {{input}}' -- -y`.
3. Run `apogee __complete symphonies delete ''`, then `apogee symphonies delete -- -y`.
4. For comparison, hand-write `"-x": {"type": "mock"}` under `backends`, run `apogee config add-backend --force --type mock -- -x`, and `cmp` the config with its earlier copy.
5. Start `apogee serve --bind 127.0.0.1` with the home's admin token, after the CLI has written a `-z` symphony the same way. Send `PUT /v1/admin/symphonies/-z` with a body, then `DELETE /v1/admin/symphonies/-z` and `DELETE /v1/admin/symphonies/nothere`.
6. Run `apogee symphonies show -- " first"`, then `apogee symphonies delete -- " first"`.

### Actual result

1. `created symphony '-y' (chat)`, exit 0, and `config.json` holds `"-y": {`.
2. `mock response`, exit 0. Then `replaced symphony '-y' (chat)`, exit 0, and the prompt is now `Changed: {{input}}`.
3. Completion offers `-y` and `first`. The delete says `apogee symphonies: symphony '-y' not found in config`, exit 1.
4. `apogee config: backend name '-x' is not one both config formats can edit -- …; a backend already named otherwise is reached by 'apogee config rename-backend'`, exit 1, with the config unchanged.
5. The PUT answers HTTP 200 and the edit is in `config.json`. `DELETE …/-z` answers `{"error":{"message":"symphony '-z' not found in config","type":"invalid_request_error"}}`, HTTP 400. `DELETE …/nothere` answers `{"error":{"message":"no symphony entry named 'nothere'","type":"not_found_error"}}`, HTTP 404.
6. `show` prints `first  ·  config`, exit 0. `delete` says `apogee symphonies: no symphony named ' first'`, exit 1.

### Expected result

1. `apogee symphonies: symphony name '-y' is not one both config formats can edit -- a name is letters, digits, '_', '-', '.' and '/', first neither '-' nor '.', and not null`, exit 1, with `config.json` byte-identical. This holds whether or not a `symphonies` section exists. `POST /v1/admin/symphonies` with the same name answers 400 `invalid_request_error` with the same words.
2. For a dash-named symphony already in the config (hand-written, or written by an older binary), `play` works as today. `create --force`, `symphonies edit` and `PUT` are refused with the words in 1, and the config stays byte-identical.
3. `apogee symphonies: symphony '-y' is not one both config formats can edit -- …`, exit 1, with the config unchanged. Completion keeps its offer (Open questions).
4. Unchanged. This is the precedent the symphony branch follows.
5. The PUT answers 400 `invalid_request_error` with the words in 1. `DELETE …/-z` answers 400 `invalid_request_error` with the words in 3. `DELETE …/nothere` still answers 404.
6. `delete` removes `first` and prints `removed symphony 'first' from …`. `delete FIRST` prints `removed symphony 'first'` too, the entry's own key rather than the spelling typed.

### Background

Symphonies arrived with 27q (7e19c394, 2026-10-06). `append_symphony` was written beside the generic `append_entry`, not on it, because a symphony entry needs its own extent: a prompt line that begins with `#` belongs to the entry (`symphony_extent`). It calls `append_entry` only when the section doesn't exist yet, so that `append_entry` creates it. `delete_symphony` was written the same way, with its own "not found" refusal.

Two later items moved every other section onto shared rules, and both left symphonies out on purpose:
- **55f** (8d5aec51, 2026-10-25) put the add rule, `yaml_plain_key`, into `append_entry`. MILESTONES records its default: "names are restricted at add to the set both formats edit, with existing odd names reached by `rename-backend`". It also records the gap: "**Not fixed: symphony names.** `is_symphony_name` allows a leading `-`, which the JSONC bridge would quote, and `append_symphony` into an existing section skips the shared name check."
- **57e** (the 2026-11-01 entry) moved every other not-found refusal onto `missing_entry`. That function's comment (`config_edit.cpp:682-686`) explains why: "a JSONC config's quoted key is one the editor never finds". The entry lists symphony names under "Out by the document's scope".

So the gap is real, recorded twice, and owned by no item. The Mac app's investigation (APP-M21, APP-M22, 2026-11-26) ran into it from outside. Its sweep put a hand-written `-x` entry in each config section. After `--`, every section's delete refused it with 57e's words except symphonies, which said "not found in config" while `show` found the entry. APP-M21's Out of scope sent that aside here. This run's investigation then found the cause: the CLI writes such an entry itself.

How the name gets lost: a JSONC config reaches the line editor through a bridge. The section is rendered as YAML, the transform runs on that text, and the result is read back into JSON. When the bridge renders a key that `yaml_plain_key` refuses, it writes the key quoted. The symphony branch writes `  -y:` plain into an existing section, because it has no add rule, and that reads back as the JSON key `"-y"`. On the next edit the bridge renders it as `"-y":`, quotes included. `entry_name` returns that quoted text, so `find_entry_line` never matches `-y`.

The design: the symphony branch takes the two shared rules it missed, at the editor, so the CLI verbs and the HTTP routes inherit them through the one mutation path.
- The add rule goes at the top of `append_symphony`, so both of its paths hold it.
- `delete_symphony` uses `missing_entry`.

A symphony then behaves as every other section does. A new odd name is refused before anything is written. An existing one stays readable, and a write or delete of it is refused by name, with a hand edit as the way out. `symphonies delete` also resolves its name the way `show` does, then deletes and echoes the entry's own key. The HTTP write route drops its explicit empty error type.

### Evidence

All measured 2026-11-26 by an investigator (probe homes `i-h`, `i-d`, `i-l`), the fix skeptic (`f-s`, `f-g2`) and the critic (`c-s`, `c-g`, `c-v`), each in its own sandbox. Every run used `env -i PATH=/usr/bin:/bin`, with `APOGEE_HOME` and `HOME` three folders deep, stdin from `/dev/null` and mock backends only. Stdout, stderr and the exit code were recorded separately. The HTTP probes used a loopback `serve --bind 127.0.0.1` with that home's `--print-admin-token`, and stopped it afterwards.

1. **The binary.** `c-v`: `apogee version` gives `apogee 0.2.0 (33ea685d, 2026-11-22, macos-arm64)` and `channel: release`. The one later commit, 5b8927c9 (2026-11-26), changes 11 CLI files. None of them is the config editor, a symphony verb or an HTTP route. One, `complete_sources.cpp`, holds the completion source Evidence 10 cites, and `strings` finds 5b8927c9's new completion text once in the binary. So, by the diff list and that check, the binary carries HEAD's code for every file cited here.
2. **The CLI writes a dash name once the section exists.**
   - `i-h`, no `symphonies` section: `symphonies create --from digest -- -y` gives `apogee symphonies: symphony name '-y' is not one both config formats can edit -- …`, exit 1.
   - After `symphonies create --from digest first`, the same command gives `created symphony '-y' (play:summarize-verify → extraction)`, exit 0, and `config.json` holds `    "-y": {`.
   - `c-s`, with `--stage 's:chat:Echo: {{input}}'` next to `first`: `created symphony '-y' (chat)`, exit 0. The success lines print `Play it:  apogee symphonies play -y --input "…"` and `Edit it:  apogee symphonies edit -y` (CLI-M48's, Out of scope).
3. **It reads, plays and can be replaced.**
   - `i-h`: `symphonies show -- -y` gives `-y  ·  config`, exit 0.
   - `c-s`: `symphonies play --input hello -- -y` gives `mock response` on stdout, `stage 1/1 s — asking chat (echo): Echo: hello · 0.0 s` on stderr, exit 0.
   - `c-s`: `symphonies create --force --stage 's:chat:Changed: {{input}}' -- -y` gives `replaced symphony '-y' (chat)`, exit 0. `config.json` holds one `"-y": {` entry, with `"prompt": "Changed: {{input}}"`. `f-s` measured the same create, play and replace, and `i-h`'s `--force` over its `-y` also left one key.
   - **Why the replace works was not probed.** By reading, `find_entry_line` misses the bridge's quoted key on the replace path too (`config_edit.cpp:1553`), so a second `-y:` is appended, and the read back into JSON keeps one key. Only the outcome (one key, the new prompt) is measured. Today's replace works by accident, not by design.
4. **It can't be deleted.**
   - `c-s`: `__complete symphonies delete ''` offers `-y` and `first`.
   - `symphonies delete -- -y` gives `apogee symphonies: symphony '-y' not found in config`, exit 1. `i-h` gave the same.
5. **Over HTTP.**
   - `i-l`: `POST /v1/admin/symphonies {"name":"-z",…}` before any symphony answers 400 `symphony name '-z' is not one both config formats can edit …`. `POST {"name":"first",…}` answers 201. `POST {"name":"-z",…}` again answers 201 with `"name":"-z"` and `"source":"config"`. `GET …/-z` answers 200, and `DELETE …/-z` answers 400 `symphony '-z' not found in config`.
   - `c-g`, for a `-z` the CLI wrote: `PUT /v1/admin/symphonies/-z` answers `[HTTP 200]`, and `Edited over PUT` appears once in `config.json`. `f-g2` measured the same (`"prompt": "Edited over PUT: {{input}}"`).
   - `c-g`: `DELETE /v1/admin/symphonies/-z` answers `{"error":{"message":"symphony '-z' not found in config","type":"invalid_request_error"}} [HTTP 400]`. `DELETE /v1/admin/symphonies/nothere` answers `{"error":{"message":"no symphony entry named 'nothere'","type":"not_found_error"}} [HTTP 404]`.
6. **Every other section refuses by name.** `i-d` is a sweep over a config with a hand-written `-x` in six sections, each run on a fresh copy:

   ```text
   agents delete --keep-files -- -x   exit 1 | agent '-x' is not one both config formats can edit -- …
   config delete-mcp-server -- -x     exit 1 | MCP server '-x' is not one both config formats can edit -- …
   review targets delete -- -x        exit 1 | review target '-x' is not one both config formats can edit -- …
   config delete-suite -- -x          exit 1 | suite '-x' is not one both config formats can edit -- …
   config delete-backend -- -x        exit 1 | backend '-x' is not one both config formats can edit -- …
   symphonies delete -- -x            exit 1 | symphony '-x' not found in config
   ```

   The same sweep found that every other section's delete, enable and disable fold case. `c-s`: `config add-backend --force --type mock -- -x` over a hand-written `-x` is refused with 55f's words (step 4 above), and the config is unchanged. So no other section can replace an odd-named entry either.
7. **`show` trims and `delete` doesn't.** In `i-h`:
   - `symphonies show -- " first"` gives `first  ·  config`, exit 0.
   - `symphonies delete -- " first"` gives `apogee symphonies: no symphony named ' first'`, exit 1.
   - `symphonies delete -- FIRST` removes `first`. Its success line echoes the spelling typed, `removed symphony 'FIRST'`, as the investigation's asides record and `:542` below shows.

   The cause is that `find_definition` trims its argument (`business/symphony/definition.cpp:459`). `show` resolves through it (`find_or_fail`, `symphonies_cmd.cpp:80-89`). The delete callback first asks `config.find_symphony(*name)` with the raw name (`symphonies_cmd.cpp:521`). When that misses, it calls `find_definition`, which does find `first` as a config entry. But only the starter branch (`:524-528`) and the spec-file branch (`:529-532`) act on what it finds, so the callback falls through to `no symphony named` at `:533`. The success line echoes `*name` (`:542`), and the "plays again" lookup after it also uses the typed name (`:543-544`).
8. **The HTTP write refusals carry an empty type.** `admin_symphonies.cpp:154-158` passes `std::string_view{}` as the type for every refusal `create_symphony` throws that isn't a collision: a name the editor refuses, a stage whose `role` is a backend, a definition its validation refuses. The body checks before it (`:135-148`: a malformed body, a `backend` key on a stage, a missing name) already take the default. `http_types.h:83-84` defaults the type to `invalid_request_error`, which is what `http-api.md`'s error table documents for a 400. The empty type was measured on the agents twin, whose code has the same shape: `c-g`, `POST /v1/admin/agents {"name":"-x"}` answers `…","type":""}} [HTTP 400]`. For the symphony route it is by reading only. The agents and MCP routes are CLI-M52's.
9. **Root cause.**
   - `append_symphony` (`data/contracts/config_edit.cpp:1528-1589`) checks only `is_symphony_name` (`:1530-1533`). That rule (`data/contracts/config.cpp:1453-1466`) allows letters, digits, `_` and `-`, refuses a YAML boolean or null word, and allows a leading `-`.
   - The function reaches `append_entry`, and with it the add rule (`:715-725`), only when there is no section (`:1540-1544`). The existing-section path (`:1545-1588`) writes the name as given.
   - On the next edit the bridge writes that key quoted (`config_yaml.cpp:97-98`). `yaml_plain_key` (`config_yaml.cpp:206-217`) refuses a first `-` or `.`.
   - `entry_name` (`config_edit.cpp:174-197`) returns the quoted text, so `find_entry_line` (`:629-650`) never matches.
   - `delete_symphony` (`:1591-1609`) throws its own `not found in config` (`:1597-1599`). `missing_entry` (`:682-699`), which `delete_entry` uses (`:828-830`), would give 57e's words.
   - Both functions date from 27q (7e19c394, 2026-10-06). The gap is recorded in MILESTONES twice: the 55f entry's "Not fixed: symphony names" (`MILESTONES.md:11236`) and the 57e entry's "Out by the document's scope: … 55f's name rule, symphony names" (`:11926`).
10. **Blast radius.**
    - Two editor functions: `append_symphony`'s existing-section path and `delete_symphony`. One CLI callback (`symphonies delete`) and one HTTP mapping.
    - Six surfaces: `symphonies create`, `symphonies edit` (which writes through `scaffold::create_symphony` with `force`, `symphonies_cmd.cpp:490`), `symphonies delete`, and `POST`, `PUT` and `DELETE /v1/admin/symphonies`. All the writers go through `scaffold::create_symphony` (`business/scaffold/symphony.cpp:67-73`).
    - **The Mac app's** Library delete builds `["symphonies", "delete", symphony.name]` with no `--` (`LibraryViewModels.swift:598`). So the parser reads a listed `-x` as a flag before the verb runs (APP-M23's seam). Once the app passes `--`, the person gets this item's words.
    - **Completion** offers every config key for `symphonies delete` (`complete_sources.cpp:515-520`), dash names included.
    - Of the six config sections compared, five already answer an odd name with 57e's words.
    - **Existing dash-named symphonies keep loading.** The loader checks a symphony's name with `is_symphony_name` (`config.cpp:1205`), as it does a play stage's target (`:1052`) and a stage's name (`:1140`). After the fix they keep showing and playing. Replacing, editing and PUT-ing them becomes a refusal.
    - **Only JSONC configs lose the delete.** By reading, not measured: in a YAML config the written `  -y:` stays plain, so `entry_name` returns `-y` and today's delete finds the entry. Every config `config init` writes is JSONC (28i).
    - **No client parses the words that change.** In `lib/src/darwin`, `lib/src/mod` and `lib/design/prototype`, `not found in config` appears only in the app's MCP fixture (`mcp-delete-missing.err`) and the test that expects it (`MCPRemoveTests.swift:78`), both for an absent `notes`, and that refusal keeps its words. Machine mode plays and lists symphonies but writes none (`machine-mode.md`, Playing a symphony).
11. **What already works**, and must stay:
    - With no `symphonies` section, `create` refuses a dash name through `append_entry` (`i-h`).
    - `create_symphony` holds a definition to everything before a byte is written (`business/scaffold/symphony.cpp:42-65`; DEVELOPER.md's `scaffold/` row).
    - Every other section's delete, enable and disable refuse a hand-written `-x` with 57e's words and fold case (`i-d`). `config add-backend --force` over a hand-written `-x` is refused with the config unchanged (`c-s`).
    - `show` and `play` of a dash-named symphony work (`c-s`, `i-h`). `delete FIRST` removes `first`, because the config's symphony map folds case (`config.h:1227`) and so does `find_entry_line`.
    - An absent name's HTTP delete answers 404 `not_found_error` (`c-g`).
12. **Not pinned.** Every symphony test that runs a verb, a route or the scaffold writes a `config.yaml`: `tests/presentation/cli/symphonies_test.cpp`, `tests/presentation/httpserver/admin_symphonies_test.cpp`, `tests/business/scaffold/symphony_test.cpp` and `tests/scripts/sh/symphonies_e2e.sh`. The editor's own symphony units (`tests/data/contracts/symphony_spec_test.cpp`, its `append_symphony` and `delete_symphony` calls at `:441-582`) run on YAML text only, and `:491` checks only that an absent name throws. The only JSONC symphony appends are of a plain `duo` (`tests/data/contracts/config_jsonc_test.cpp:315-325`, `:370`). So the JSONC path that loses the name is never run, and no test greps for `not found in config` on a symphony.

## Acceptance criteria

- [ ] **Given** a JSONC config with a `symphonies` section, **when** `symphonies create --stage 's:chat:Echo: {{input}}' -- -y` runs, **then** it exits 1. Its last stderr line is `apogee symphonies: symphony name '-y' is not one both config formats can edit -- a name is letters, digits, '_', '-', '.' and '/', first neither '-' nor '.', and not null`, and `config.json` is byte-identical. Today it exits 0 and writes `"-y": {`.
- [ ] **Given** a config with a `symphonies` section, and one without, **when** `POST /v1/admin/symphonies {"name":"-z",…}` is sent to each, **then** both answer 400 `invalid_request_error` with the same words, and the file is unchanged. Today the first answers 201.
- [ ] **Given** the config today's binary leaves (`first`, then `-y` written by `symphonies create -- -y`), **when** `symphonies delete -- -y` runs, **then** it exits 1 with `apogee symphonies: symphony '-y' is not one both config formats can edit -- …`, never `not found in config`, and the config is unchanged. `symphonies show -- -y` still finds the entry.
- [ ] **Given** a hand-written JSONC `"-x"` symphony, **when** `symphonies delete -- -x` runs, **then** it exits 1 with `symphony '-x' is not one both config formats can edit -- …`. Today it says `symphony '-x' not found in config`.
- [ ] **Given** the same entry, **when** `DELETE /v1/admin/symphonies/-x` is sent, **then** it answers 400 `invalid_request_error` with the same words. `DELETE /v1/admin/symphonies/nothere` still answers 404 `not_found_error` with `no symphony entry named 'nothere'`.
- [ ] **Given** the same entry, **when** `symphonies create --force --stage 's:chat:Changed: {{input}}' -- -x` runs, or `PUT /v1/admin/symphonies/-x` is sent with a body, **then** each is refused with `symphony name '-x' is not one both config formats can edit -- …` (exit 1, or 400 `invalid_request_error`), and the config is byte-identical. The same holds for `create_symphony` with `force`, which `symphonies edit` writes through. Today the replace and the PUT land.
- [ ] **Given** the same entry, **when** `symphonies show -- -x` or `symphonies play --input hi -- -x` runs, **then** each still exits 0.
- [ ] **Given** `first`, **when** `symphonies delete -- " first"` runs, **then** `first` is removed and stdout says `removed symphony 'first' from …`. **When** `symphonies delete FIRST` runs instead, **then** it also says `removed symphony 'first' from …`. Today the first exits 1 with `no symphony named ' first'`, and the second echoes `'FIRST'`.
- [ ] **Given** any symphony write over HTTP that `create_symphony` refuses (a dash name, a stage whose `role` is a backend, a definition that would loop), **then** the error type is `invalid_request_error`, never `""`. The body checks that answer 400 before it keep their type, and a collision keeps 409 `conflict`.
- [ ] **Given** a shipped starter's name with no entry, or a spec file's name, **when** `symphonies delete` runs, **then** it keeps today's words: `'<name>' is a shipped starter, with no config entry to delete -- it stays`, or `'<name>' is a spec file, with no config entry to delete -- remove <path> yourself`. An entry standing in for a starter is still removed with `the shipped symphony of that name plays again`.
- [ ] **Given** the `-y` config, **when** `__complete symphonies delete ''` runs, **then** it still offers `-y` (the recorded default, Open questions).
- [ ] **Given** a plain name, **then** every symphony verb and route behaves byte for byte as today. The existing CLI ↔ HTTP parity tests pass unchanged.

## Constraints

- **One config mutation path** (CLAUDE.md → ⚠ One config mutation path). Both rules live in the shared editor (`append_symphony`, `delete_symphony`), so the CLI verbs and the HTTP routes refuse with the same words through `edit_config_file`. No check goes in a verb or a route alone ([ADR 0002](../../../adrs/cli/mode-parity.md)).
- **57e's words and status.** An odd name's delete is refused by name (exit 1, or 400 over HTTP), never called "not found". A truly absent entry keeps its own words, and its 404.
- **The add words are `append_entry`'s.** A new or replaced odd name gets `symphony name '<n>' is not one both config formats can edit -- <kEntryNameRule>`, and a delete gets `missing_entry`'s `symphony '<n>' is not one …`. That is the same pair every other section uses, built from the one `kEntryNameRule` (`config_edit.cpp:33-34`).
- **`is_symphony_name` is not narrowed.** It stays the stricter character set and the load-time rule. It guards a symphony's name, a play stage's target and a stage's name at load (`config.cpp:1205`, `:1052`, `:1140`), template variables (`definition.cpp:144`) and the HTTP GET (`admin_symphonies.cpp:194`). The editor adds `yaml_plain_key` beside it.
- **Older configs keep working** ([ADR 0006](../../../adrs/cli/backwards-compatibility.md)). An existing dash-named symphony keeps loading, showing and playing. Taking away replace, edit and PUT on it is a deliberate change, recorded as a dated MILESTONES revision with a hand edit named as the way out.
- **CLI-M44 keeps JSONC byte for byte** (its last acceptance line). Its quoted-key refusal ("Keys as YAML reads them", its Technical approach) must not replace these words on a key the JSONC bridge quoted. A bridged `"-x"` symphony gets 57e's refusal, whichever item lands first.
- **The refusal is one line, and the last on stderr**, in the house voice (`apogee symphonies: …`), exit 1. The Mac app shows that line as the failure once a name reaches the verb.
- **The HTTP error envelope is the documented one.** A 400 is `invalid_request_error` (`http-api.md`'s error table; `http_types.h:83-84`).
- **Hermetic tests.** A temporary `APOGEE_HOME` and mock backends, never `~/.apogee` or `~/.apogee-dev`. A live pass names `apogee-dev` (CLAUDE.md → Development runs on the dev channel).

## Technical approach

Paths under `lib/src/cli/`.

| File | Change |
|---|---|
| `source/data/contracts/config_edit.cpp` | **`append_symphony`** (`:1528-1589`): after the `is_symphony_name` check (`:1530-1533`) and before the section lookup (`:1540`), refuse a name `yaml_plain_key` refuses, in `append_entry`'s words (`:715-725`), so both paths hold the rule. **`delete_symphony`** (`:1597-1599`): throw `missing_entry("symphony", "symphonies", name)` in place of its own `not found in config`, as `delete_entry` does (`:828-830`). The missing-section refusal (`:1594-1596`) stays, as it does in `delete_entry`. |
| `source/data/contracts/config_edit.h` | `append_symphony`'s comment (`:281-284`) names the add rule beside `is_symphony_name`. `delete_symphony`'s comment (`:288-289`) says an odd name is refused by name. |
| `source/presentation/cli/symphonies_cmd.cpp` | **`symphonies delete`** (callback `:518-549`) resolves through `symphony::find_definition`, as `show` does through `find_or_fail` (`:80-89`). A config-sourced definition is deleted by its own key (`definition->spec.name`). That key is echoed in the success line (`:542`) and used for the "plays again" lookup (`:543-544`). The starter and spec-file refusals (`:524-532`) and the absent-name refusal (`:533`) stay as they are. `find_definition` itself is untouched. |
| `source/presentation/httpserver/admin_symphonies.cpp` | `write`'s refusal mapping (`:157-158`) drops the explicit empty type. A collision keeps `409` `conflict`, and every other refusal takes the default `invalid_request_error`. `DELETE` (`:224-232`) needs no change, since the editor's new words reach it through its existing 400. |
| `tests/data/contracts/symphony_spec_test.cpp`, `config_edit_test.cpp`, `config_jsonc_test.cpp` | The editor units under Testing. Today's `append_symphony` and `delete_symphony` units sit in `symphony_spec_test.cpp`, so the YAML cases go beside them. |
| `tests/business/scaffold/symphony_test.cpp` | `create_symphony` with `force` over a JSONC `"-x"` entry is refused, with the bytes unchanged. That is the write `create --force`, `edit` and `PUT` share. |
| `tests/presentation/cli/symphonies_test.cpp`, `tests/presentation/httpserver/admin_symphonies_test.cpp` | The fixture under Testing, on a JSONC home. |

`missing_entry` already does what the delete needs. Its comment says it refuses an odd name "whether or not the config holds it, since a JSONC config's quoted key is one the editor never finds". So `delete_symphony` gains 57e's words with no new helper. The add check reuses `kEntryNameRule`, and its words match `append_entry`'s for the noun `symphony`. In the no-section path `append_entry` then checks the same name again, a check that can no longer fail.

The delete callback's new order:
1. `find_definition(config, symphonies_dir_for(config_path), *name)`.
2. A shipped definition gets today's starter refusal, and a spec file gets today's spec-file refusal.
3. No definition gets today's `found.error`, or `no symphony named '<name>'`.
4. A config definition is deleted through `delete_symphony(content, key)`, where `key` is the definition's `spec.name`, which is the config map's key (`config.cpp:1201`).

This takes the trim from `find_definition` and the case from the config's own key, so `" first"` and `FIRST` both delete and name `first`.

The item consumes no other item's decision. It finishes 55f's add rule and 57e's sweep for the one section both left out.

**Reference.**
- `missing_entry` (`config_edit.cpp:682-699`) and `append_entry`'s 55f rule (`:715-725`): the two rules this item applies.
- `delete_entry` (`:820-830`): the shape `delete_symphony` follows.
- `config_jsonc_test.cpp:761`, "every set and delete on an odd JSONC name names rename-backend, never 'not found'": the precedent test for a JSONC odd name's delete.
- `config_edit_test.cpp:2305`, "an entry name both formats cannot edit is refused at add, naming the rule": the precedent for the add.
- `admin_agents.cpp:239-240`: an editor refusal on a delete answered 400 with the default type.
- `config_lifecycle_test.cpp:92-98`: a `CliHome` over `config.json` holding `config_template()`.

**Docs when it ships:**
- MILESTONES: a dated entry, with a dated revision that closes 55f's "Not fixed: symphony names" and 57e's scope line. It states that replace, edit and PUT of an existing dash-named symphony are now refused, as in every other section, with a hand edit as the way out.
- DEVELOPER.md: the `symphonies_cmd.h/.cpp` row (`:319`), where `delete` resolves as `show` does and names the entry's key; the `config_edit.h/.cpp` row (`:368`), where the symphony branch holds 55f's add rule and 57e's `missing_entry`; and the `admin_symphonies.h/.cpp` row (`:583`).
- `reference/cli/http-api.md`: the `DELETE /v1/admin/symphonies/{id}` row (`:617-621`) states the odd-name 400, and the `POST` row's refusal list (`:588-602`) adds the name rule.

## Testing

Every case runs in a temporary home, with mock backends. Each fails on today's binary unless marked as a guard (measured 2026-11-26).

- **The bug as a fixture**, in `symphonies_test.cpp`. The existing `Home` writes `config.yaml`, so these cases use a `CliHome` over `config.json` holding `config_template()` plus a mock backend `echo`, as `config_lifecycle_test.cpp:92-98` does.
  1. `symphonies create --stage 's:chat:Echo: {{input}}' first`, then `… -- -y`, exits 1 with `is not one both config formats can edit`, and the config is byte-identical.
  2. With `"-x"` written by hand into the JSONC `symphonies` section, `symphonies delete -- -x` exits 1 with `symphony '-x' is not one both config formats can edit -- `, and `create --force --stage 's:chat:Changed: {{input}}' -- -x` exits 1 with `symphony name '-x' is not one`, the config unchanged both times.
  3. Guard: `symphonies show -- -x` and `symphonies play --input hi -- -x` exit 0.
  4. `symphonies delete -- " first"` removes `first` and prints `removed symphony 'first' from`. After `first` is created again, `symphonies delete FIRST` prints the same.
  5. Guard: deleting a shipped starter's name (`summarize-verify`) and a spec file's name keeps today's words.
- **The HTTP twin**, in `admin_symphonies_test.cpp`, on a JSONC copy of its fixture:
  - `POST {"name":"-z",…}` answers 400 with type `invalid_request_error`, both with and without a `symphonies` section.
  - With a hand-written `"-x"`, `DELETE …/-x` answers 400 with the delete words, and `PUT …/-x` answers 400 with the add words, the file unchanged.
  - `DELETE …/nothere` answers 404 (guard).
  - The existing "what the plane refuses" case checks that its 400s carry `invalid_request_error`, and that its 409 stays `conflict`.
- **The editor**, in `symphony_spec_test.cpp` (or `config_edit_test.cpp`) and `config_jsonc_test.cpp`:
  - `append_symphony` refuses `-y`, `-` and `--x`, with and without a section, in a YAML and a JSONC text, with `append_entry`'s words.
  - `delete_symphony` on a JSONC `"-x"` throws `missing_entry`'s words.
  - On an absent plain name it still says `symphony 'nope' not found in config` (guard).
  - A plain name's append and delete round-trip byte for byte (guard).
- **The scaffold**, in `business/scaffold/symphony_test.cpp`: `create_symphony(path, spec named -x, true)` over a JSONC `"-x"` entry throws with the add words, and the file's bytes are unchanged. This covers the write behind `edit` and `PUT` without a child editor.
- **Unchanged**: the CLI ↔ HTTP parity cases and `symphonies_e2e.sh` pass as they are.
- **Gate:** `lib/scripts/cicd.sh --app cli --test`, clang-format, and the MinGW scan of the changed files.

## Open questions

| Question | Status | Answer |
|---|---|---|
| Should replace, edit and PUT of an existing dash-named symphony become refusals? | `[default]` | Yes. Every other section already refuses a write to an odd name (55f and 57e; measured with `config add-backend --force -- -x`), and today's replace works by accident. The way out is a hand edit, recorded as a dated MILESTONES revision. Show and play keep working (ADR 0006). |
| Should completion keep offering `-y` for `symphonies delete`, which the verb will refuse? | `[default]` | Yes. CLI-M44's default (its Open questions, the completion row) keeps offering the names the loader reads. The refusal's words say why, and `config delete-backend` offers a hand-written `-x` the same way today. Settling ADR 0007's "a candidate the verb would refuse" once, across sections, is out of scope. |
| Trim in `delete`, or stop trimming in `find_definition`? | `[default]` | `delete` resolves through `find_definition` and deletes by the entry's own key. `find_definition` is shared by chat's `/play` (`chat_play.cpp:72-73`), `play`, `show`, `edit` and the HTTP GET and PUT, so it stays as it is. This also fixes `delete` echoing the typed spelling. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-26 | Specced from APP-M21's Out of scope (the CLI asides its probes found), then a sandboxed investigation, two skeptics and a critic (Evidence). | the spec |
| 2026-11-26 | Placed in **Maintenance** as M53, by the user's call to spec the APP-M21/M22 CLI asides as maintenance items from M48, grouped only where they share one seam. This item's seam is the config editor's symphony branch (`append_symphony`'s existing-section path and `delete_symphony`), plus the one verb that deletes through it. The same investigation's other seams are separate items: the scaffolds' write order (CLI-M52), the unfolded lookups in rename and the HTTP agent GET (CLI-M54), the dataset readers and the dataset create rule (CLI-M55, CLI-M56), and saved chats (CLI-M57). | the user (placement) · the spec (grouping) |
| 2026-11-26 | **The delete's trim and echo ride here, not in CLI-M54.** It is the same verb and the same fixture. The narrower fix (delete resolves as `show` does) wins over "neither trims", because `find_definition` serves every play path. | the spec |
| 2026-11-26 | **The empty error type is fixed here for the symphony routes only.** The agents and MCP routes are fixed with their scaffolds in CLI-M52. | the spec |
| 2026-11-26 | **Not gated on CLI-M44, but related.** M44's quoted-key work edits the same `entry_name` and `find_entry_line` (its Background and Technical approach). Either order works. Whichever lands second keeps this item's JSONC fixture passing, and keeps 57e's words for a bridged key. | the spec |
| 2026-11-26 | **Not gated on CLI-M48, but related.** M48's acceptance prints the remedies of a symphony `-q` created after `s1`, which `create` can't make once this item lands. Whichever lands second seeds that case by hand or drops it. Neither gates the other. | the spec |
| 2026-11-26 | **The item states the behaviour change it carries: replace, edit and PUT of an existing dash-named symphony become refusals.** Today they work (Evidence 3, 5), by accident rather than by any recorded intent. The change closes 55f's recorded "Not fixed: symphony names" and 57e's scope exclusion, and follows 55f's default that names are restricted at add to the set both formats edit. Symphonies have no rename verb, so the way out is a hand edit. Whether to make it is the first open question, whose default is yes; the build records the change as a dated MILESTONES revision. | the spec |

## Out of scope

- **A rename verb for symphonies.** The way out stays a hand edit, as the refusal implies.
- **The remedies in `symphonies create`'s success lines**, which print a dash name with no `--` (`Play it:  apogee symphonies play -y …`, `Edit it:  apogee symphonies edit -y`, `symphonies_cmd.cpp:392-396`), and `edit`'s rename door: [CLI-M48](a-suggested-command-runs-as-printed.md). Once this item lands, `create` never prints a dash-led name there.
- **The Mac app passing `--`** before a symphony's name in Library's delete (`LibraryViewModels.swift:598`): [APP-M23](../../darwin/macos/maintenance/every-name-the-app-passes-is-read-as-a-name.md).
- **Completion offering odd names that a verb refuses, across sections.** `__complete config delete-backend ''` offers a hand-written `-x` too. That tension with ADR 0007 is an aside of the investigation, to settle once for every section ([CLI-M45](a-collection-is-named-not-pathed-at-every-reader.md) settles a neighbouring question for collections: a dash-led name is offered only after `--`). This item keeps today's offer.
- **Echoing the typed spelling elsewhere**: `config delete-backend FOO` says `removed backend 'FOO'`, and `GET /v1/admin/mcp-servers/FOO` returns `"name":"FOO"`. By reading, not measured, `DELETE /v1/admin/symphonies/FIRST` answers `{"deleted":"FIRST"}` (`admin_symphonies.cpp:233`). This item fixes only the CLI symphony delete's echo.
- **Rename's and the HTTP agent GET's unfolded lookups**: [CLI-M54](rename-and-get-find-an-entry-by-its-folded-name.md).
- **The agents and MCP routes' empty error type, and the scaffolds' write order**: [CLI-M52](a-refused-scaffold-writes-nothing.md).
- **`symphonies edit` launching `vi`** when `EDITOR` is unset and stdin is not a terminal (`symphonies_cmd.cpp:424`; `agents edit` does the same). It then fails with `the editor exited with status 1`. An aside of the investigation.
- **`config get symphonies.-x`** answering `no such config key`, while `backends.*` works and folds case. Also an aside of the investigation.
- **Quoted or spaced keys in a YAML file a person wrote**: [CLI-M44](yaml-editor-refuses-the-layouts-it-does-not-write.md).

## Links

- **Blocked by:** — · **Blocks:** —
- **Sibling** (the same investigation, 2026-11-26): [CLI-M52](a-refused-scaffold-writes-nothing.md), [CLI-M54](rename-and-get-find-an-entry-by-its-folded-name.md), [CLI-M55](a-dataset-is-named-not-pathed-at-every-reader.md), [CLI-M56](a-new-dataset-name-is-one-every-verb-reads.md), [CLI-M57](a-saved-chat-names-only-its-own-files.md).
- **Relates to:**
  - [CLI-M44](yaml-editor-refuses-the-layouts-it-does-not-write.md), which edits the same `entry_name` and `find_entry_line`. Either order works, and its JSONC-unchanged rule keeps these words for a bridged key.
  - [CLI-M48](a-suggested-command-runs-as-printed.md), which quotes `create`'s success lines. Its symphony `-q` case, created after `s1`, can't be reached by `create` once this item lands, so whichever lands second seeds that case by hand or drops it. Neither gates the other.
  - [APP-M21](../../darwin/macos/maintenance/a-listed-name-reaches-the-verb-as-listed.md), whose probes raised this aside, and [APP-M23](../../darwin/macos/maintenance/every-name-the-app-passes-is-read-as-a-name.md), which moves Library's symphony delete onto `--`.
  - MILESTONES' 55f entry (2026-10-25, the add rule and "Not fixed: symphony names") and 57e entry (2026-11-01, `missing_entry`, symphony names out of scope). This item closes both records.
  - 27q (7e19c394, 2026-10-06), where `append_symphony` and `delete_symphony` began.
  - [ADR 0002](../../../adrs/cli/mode-parity.md), [ADR 0006](../../../adrs/cli/backwards-compatibility.md) and [ADR 0007](../../../adrs/cli/tab-completion.md).
