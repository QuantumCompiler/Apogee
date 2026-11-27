# CLI-M40 · Delete-allowed-host removes what the list holds

| Field | Value |
|---|---|
| **Type** | 🐞 Bug |
| **Project** | CLI |
| **Status** | 🟢 Ready |
| **Fix version** | Maintenance (any time) |
| **Component** | Config editing › `config delete-allowed-host` and its HTTP twin |
| **Labels** | `config` `hosts` `allowed-hosts` |
| **Epic** | — (maintenance has no track) |
| **Blocked by** | — |
| **Blocks** | [M43](gate-edits-land-or-say-they-did-not.md) |
| **Reporter** | CLI-M36's probes (2026-11-22), then a sandboxed investigation and a skeptic's re-run the same day |
| **Created** | 2026-11-22 |

---

## Summary

`config delete-allowed-host` can't remove a `tools.allowed_hosts` entry that isn't a bare host. When one host is listed under two spellings, it removes the other spelling and says it removed the host, while the gate still lets that host through without asking. Both faults come from one transform, shared by the CLI and the HTTP admin route. The item makes the verb remove any entry by how it is written, and every spelling of a host by its name, and say what it removed.

## Description

### User story

**As** a person tidying which websites Apogee reaches without asking,
**I want** `config delete-allowed-host` to remove whatever entry my list holds, and to make the host really ask again,
**so that** `check`'s warning about a pasted URL can be cleared with the verb, and "removed example.com" means example.com now asks.

### Steps to reproduce

The sandbox, 2026-11-22, with the built binary `apogee 0.2.0 (33ea685d, 2026-11-22, macos-arm64)`: throwaway `APOGEE_HOME` and `HOME`, `env -i`, stdin from `/dev/null`, a mock backend. The config is written by hand (Evidence 3: no verb can write such an entry).

1. With `tools.allowed_hosts: [docs.python.org, Example.COM., https://not-a-host.example/path, my_host]`, run `apogee config delete-allowed-host https://not-a-host.example/path`.
2. With `tools.allowed_hosts: [Example.COM., example.com, https://not-a-host.example/path]`, run `apogee config delete-allowed-host example.com`, then `apogee check`.

### Actual result

1. `apogee config: 'https://not-a-host.example/path' is not a host name: write the host alone, e.g. docs.python.org` (exit 1), the file byte-identical. `apogee check` keeps warning `'https://not-a-host.example/path' is not a host name, so it allows nothing`, and `check --fix` leaves the file unchanged.
2. `removed example.com from tools.allowed_hosts` (exit 0), but the file now reads `[example.com, "https://not-a-host.example/path"]`: the first spelling went, the typed one stayed. `check` still says `ok   allowed_hosts  example.com`, and the gate still reaches it unasked.

### Expected result

1. The entry is removed as written; the line says so, and that it allowed nothing.
2. Both spellings are removed, so example.com asks again; the line names the host.

### Background

`yaml_text::remove_allowed_host` (`config_edit.cpp:2610-2638`) is the one transform the CLI verb and the HTTP twin share; JSONC reaches it through `in_format` (`:2936-2958`). It has three faults:
- it runs its argument through `checked_host` before reading the list (`:2611`, `:2381-2388`), which throws for anything `canonical_host` refuses;
- it matches by canonical form only (`:2618`), and a malformed entry has none;
- it returns at the first match (`:2623`, `:2635`).

The key, `checked_host`, both verbs, the loader's "kept as written" rule and `check`'s warning all arrived in one commit (72c2d75a, 2026-09-25), and the transform's body has not changed since. So no version ever wrote a malformed entry: it is always a hand edit, which `config migrate` carries as written. CLI-M36's probes listed the malformed case as this verb's to fix.

### Evidence

All measured 2026-11-22, by the investigator and again by a skeptic with its own sandbox, on YAML flow lists, YAML block lists with a comment, and JSONC.

1. **The refusal comes before the list is read**, and no argument can name a malformed entry: `const std::string canonical = checked_host(host); … if (canonical_host(item.value) != canonical) { continue; }` (`config_edit.cpp:2611`, `:2618`).
2. **The same words on every format**: YAML flow, YAML block (`- "https://…"   # comment`) and JSONC each refuse the entry, exit 1, the file byte-identical. `delete-allowed-host not-a-host.example` answers `'not-a-host.example' is not in tools.allowed_hosts`.
3. **No verb writes such an entry**:
   - `add-allowed-host` refuses a URL and `*.example.com`;
   - `config set tools.allowed_hosts` and `config set 'tools.allowed_hosts[0]'` are refused, pointing to the host verbs;
   - the prompt's `always` answer and the admin `PUT` both go through `add_allowed_host` (`permissions.cpp:83-87`, `admin_config.cpp:783-790`).
4. **The entry is inert.** The loader keeps it ("refusing the whole config over one pasted URL would take every other command down with it", `config.cpp:2323-2333`), and `host_listed` skips it (`host.cpp:80-93`). So it allows nothing and opens nothing.
5. **Duplicate spellings.**
   - With `[Example.COM., example.com, URL]`, deleting `example.com` removed `Example.COM.`, printed `removed example.com`, and left `example.com`, which `check` calls `ok`.
   - The skeptic measured the gate end to end. With `["LocalHost.", "localhost"]`, `delete-allowed-host localhost` printed `removed localhost`, and a piped `complete --tools` still fetched a loopback stub unasked (the stub logged `GET /probe`). Only a second delete stopped it.
   - With five spellings, repeated deletes walk through them one at a time, each printing the canonical name.
6. **`check` sends the person to a hand edit**: `run: write the host alone, e.g. docs.python.org, not a URL or a pattern` (`check.cpp:1421-1433`). `check --fix` "never touches an existing config" (`check.cpp:3192-3195`, `:2985-2990`, M11), and left it byte-identical.
7. **The HTTP twin** (`admin_config.cpp:807-830`), measured with `apogee serve` on the mock backend:
   - `DELETE /v1/admin/allowed-hosts/*.example.com` answers `404 'not in tools.allowed_hosts'`, while its own `GET` lists the entry as `valid:false` (`:771-781`).
   - An entry holding `/` can't be routed at all (`no route for DELETE …`): httplib decodes `%2F` and the mux splits on `/` (`mux.cpp:584-608`).
   - Over duplicates it answers `200 {"deleted":"example.com","restart_required":true}` while the file still holds `example.com`.
8. **Two places dereference the canonical host**, safe today only because the transform throws first: the CLI's success line (`config_cmd.cpp:1455`) and the twin's `deleted` and `restart_required` (`admin_config.cpp:826-830`).
9. **The GUIs inherit it.**
   - The Mac app's Settings › Permissions has a Remove button per raw entry that runs this verb with the entry as written (`SettingsView.swift:460-465`, `SettingsViewModel.swift:539-543`). It always fails on a malformed entry, and takes out the wrong row on duplicates.
   - The design prototype's `removeSite` does the same (`SettingsViewModel.ts:702-705`), so every later GUI app inherits it.
10. **Completion offers the raw entries** (`complete_sources.cpp:793-795`), the malformed one included, and each spelling apart. `my_host` is accepted by `checked_host` and removed by the verb (`removed my_host`).
11. **Not pinned.** No test removes a malformed entry or a duplicate spelling, and `check`'s warning row has no test (`config_edit_test.cpp:1105-1214`, `config_jsonc_test.cpp`, `config_cmd_test.cpp:869-886`, `admin_config_test.cpp:495-540`, `config_e2e.cmake:668-686`).
12. **What already works**:
    - `add-allowed-host` refuses a URL or a wildcard, writes the canonical form, and is a no-op on any listed spelling (`config_edit.cpp:2575-2579`);
    - a delete on any spelling of a valid entry removes one entry and keeps every other line and comment;
    - add then remove leaves the file byte-identical;
    - an argument that matches nothing deletes nothing and exits 1 with words.

## Acceptance criteria

- [ ] **Given** a hand-written `https://not-a-host.example/path` in `tools.allowed_hosts` (YAML flow, YAML block or JSONC), **when** `config delete-allowed-host https://not-a-host.example/path` runs, **then** it exits 0, the entry is gone, every other line and comment is unchanged, and the line says the entry was removed and that it allowed nothing.
- [ ] **Given** `[Example.COM., example.com]`, **when** `config delete-allowed-host example.com` runs, **then** both entries are gone, the line names `example.com`, and a fetch to it asks again.
- [ ] **Given** an argument that matches no entry, written that way or as the same host (`https://typo.example/`, `pypi.org`), **then** nothing is deleted, the file is byte-identical, and it exits 1 with today's words.
- [ ] **Given** `add-allowed-host <h>` then `delete-allowed-host <h>`, **then** the file is byte-identical to before, as today.
- [ ] **Given** the HTTP twin, **when** `DELETE /v1/admin/allowed-hosts/*.example.com` is sent with that entry listed, **then** it answers 200 and leaves the same bytes the CLI leaves. Over duplicates it removes every spelling, and `deleted` and `restart_required` describe what was removed. An entry no match names is a 404, and the transform's other refusals (a list that does not close on its own line, a non-list) stay 400.
- [ ] **Given** a malformed entry, **then** `check`'s warning row names `apogee config delete-allowed-host '<entry>'` as its remedy, and `check --fix` still leaves the config byte-identical.

## Constraints

- **One transform, one predicate.** The match is one pure function in contracts, beside `host_listed`: `allowed_host_matches(entries, argument)`. An entry matches when it equals the argument as written, or when both are hosts with the same canonical form. The CLI verb, the HTTP twin and CLI-M36's completion all call it.
- **Typo protection stays.** Nothing is deleted unless something matches, and a miss is an exit 1 with words, never a quiet no-op (26o).
- **`check --fix` never touches an existing config** (M11).
- **No config key changes shape or meaning** ([ADR 0006](../../../adrs/cli/backwards-compatibility.md)). The twin's `DELETE` answering 200 for a raw entry is growth, recorded with a dated revision of http-api.md and of the "exact inverses" record (MILESTONES).
- **The twin's 404 comes from a pre-check through the predicate** over the loaded list, never from reading the transform's error text, so its other refusals stay 400 ([ADR 0002](../../../adrs/cli/mode-parity.md)).

## Technical approach

Paths under `lib/src/cli/`.

| File | Change |
|---|---|
| `source/data/contracts/host.h`, `host.cpp` | `allowed_host_matches(std::span<const std::string> entries, std::string_view argument)`: the entries the argument names, as written or as the same host. |
| `source/data/contracts/config_edit.h`, `config_edit.cpp` | `remove_allowed_host` (`:2610-2638`): no `checked_host` first, the match through the predicate, and every match removed (in descending order, so a flow list's offsets stay valid). It refuses when nothing matches, in today's words. The doc comment (`:442-445`) says it removes every entry the argument names. `checked_host` stays for add. |
| `source/presentation/cli/config_cmd.cpp` | `bind_delete_allowed_host` (`:1443-1457`): the line names what was removed (the host, or the entry as written with "it allowed nothing", and the count when there were several spellings). It never dereferences `canonical_host`. The positional's help: "a host in tools.allowed_hosts, or an entry as written there". |
| `source/presentation/httpserver/admin_config.cpp` | `admin_delete_allowed_host` (`:807-830`): a pre-check through the predicate over the loaded list (none is a 404), with `deleted` and `restart_required` computed from the matched entries. |
| `source/presentation/cli/check.cpp` | The malformed entry's warning row (`:1429-1431`) names the delete verb as its remedy. |
| `tests/data/contracts/config_edit_test.cpp`, `config_jsonc_test.cpp` | A flow-list URL removed by its spelling, a quoted block-list entry with a comment removed, duplicate spellings all removed by one host, and a raw typo and an unlisted host still refused with the bytes unchanged. Add then remove stays byte-identical. |
| `tests/presentation/cli/config_cmd_test.cpp`, `check_test.cpp`, `tests/presentation/httpserver/admin_config_test.cpp`, `tests/scripts/cmake/config_e2e.cmake` | The verb on a hand-written entry, `check`'s row and `--fix`'s bytes, and the twin's malformed and duplicate cases. The end-to-end script hand-writes a URL entry, deletes it, and checks the file is back to its bytes. |

**Reference.** ADR 0007's own precedent: when the offer and the verb disagreed (`models delete <TAB>` offering a backend's name), the verb was made to take the name. The 2026-09-25 tool-safety work (72c2d75a), which made the loader keep a bad entry rather than fail the config.

**Docs when it ships:** `reference/cli/http-api.md`'s `DELETE /v1/admin/allowed-hosts/{id}` (`:1526-1529`): it removes an entry as written or every spelling of a host, and an entry holding `/` is removed through the CLI or the apps. Also `reference/cli/tools.md`'s line on the verb, DEVELOPER.md's `config_edit`, `config_cmd` and `admin_config` rows, and MILESTONES (a dated entry, with a dated revision of the "exact inverses" record).

## Testing

Hermetic and in process: the transform over YAML and JSONC text, the verb and `check` in a temp home, the twin through its handler. No network.

- **The bug as a fixture.**
  - `remove_allowed_host("tools:\n  allowed_hosts: [docs.python.org, https://not-a-host.example/path]\n", "https://not-a-host.example/path")` must equal `"tools:\n  allowed_hosts: [docs.python.org]\n"`; today it throws.
  - `remove_allowed_host("tools:\n  allowed_hosts: [Example.COM., example.com]\n", "example.com")` must equal `"tools:\n  allowed_hosts: []\n"`; today it returns `[example.com]`.
- **Typo guards**: `https://typo.example/` and `pypi.org` still throw, with the bytes unchanged.
- **The twin**: `*.example.com` gets 200 and the CLI's bytes, and the duplicates leave no `example.com`.
- **`check`**: the row's remedy, and `--fix`'s unchanged bytes.
- **Gate:** `lib/scripts/cicd.sh --app cli --test`, clang-format, and the MinGW scan of the changed files.

## Open questions

| Question | Status | Answer |
|---|---|---|
| How does the twin remove an entry holding `/`, which can't travel in `{id}`? | `[default]` | It isn't: http-api.md names it as removed through the CLI or the apps (ADR 0002: skipped and said). An additive `?entry=` form can come when a client needs it. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-22 | Specced from CLI-M36's probes and a sandboxed investigation with a skeptic's re-run (Evidence); placed in **Maintenance** as M40, its own item: a different seam from CLI-M39's `name_gated`. | the spec |
| 2026-11-22 | **One delete removes every spelling of a host**, so it really asks again, as the verb's help ("Make fetch_url ask again") promises. Add never writes a second spelling, so add then remove stays byte-identical. | the spec |
| 2026-11-22 | **The verb takes the raw entry, not `check --fix`.** `--fix` never touches an existing config, and rewriting a pasted URL to its host would widen the gate unasked. | the spec |

## Out of scope

- **YAML layouts the line editor misreads.** `find_host_list` (`config_edit.cpp:2493-2546`) assumes a 2-space entry indent (`kEntryIndent`, `:46`) and block items deeper than their key. So a compact block list (`allowed_hosts:\n  - example.com`) or a 4-space-indented file is loaded, offered and allowed, yet the verb says the host is not listed, and `add-allowed-host` refuses the file as not valid YAML. The skeptic measured it, and the predicate does not cure it. Its own items: [CLI-M43](gate-edits-land-or-say-they-did-not.md) (the compact host list, and the gate edits' read-back) and [CLI-M44](yaml-editor-refuses-the-layouts-it-does-not-write.md) (every other layout, in every editor).
- **An entry padded with spaces inside quotes**, which the Mac app's trim can't name: the GUI's own small item if it turns up.
- **The HTTP `?entry=` form** (above).

## Links

- **Blocked by:** — · **Blocks:** [CLI-M43](gate-edits-land-or-say-they-did-not.md), whose read-back checks this item's predicate
- **Relates to:** [CLI-M42](stubs-read-the-words-as-the-shell-does.md) (bash inserts an entry like `*.example.com` raw until it ships); [CLI-M36](../../../assistant/cli/MILESTONES.md#2026-11-27--grant-and-deny-flags-offer-what-they-take-maintenance-item-m36-the-permission-flags-offer-what-their-verbs-take) (shipped 2026-11-27): its `delete-allowed-host` offer (`ALLOWED_HOST`, `complete_sources.cpp`) matches by `canonical_host` today, and this item moves it to this predicate: the accepted entries canonical once, and each malformed one as written. [CLI-M39](deny-takes-every-host-a-yes-takes.md) (the same list, `--deny`'s side). [CLI-M48](a-suggested-command-runs-as-printed.md): `check`'s `delete-allowed-host '<entry>'` remedy is built through its renderer once it ships. Also 72c2d75a (the key's first commit); M11 (`check --fix` never touches an existing config); ADR 0007 (the offer obliges the verb).
