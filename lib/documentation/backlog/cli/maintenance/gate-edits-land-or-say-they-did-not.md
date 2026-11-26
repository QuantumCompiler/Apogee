# CLI-M43 · The gate's config edits land, or say they did not

| Field | Value |
|---|---|
| **Type** | 🐞 Bug |
| **Project** | CLI |
| **Status** | 🔒 Blocked |
| **Fix version** | Maintenance (any time) |
| **Component** | Config editing › `config add-allowed-host`, `delete-allowed-host`, `set-permission` and their HTTP twins |
| **Labels** | `config` `yaml` `hosts` `permissions` `safety` |
| **Epic** | — (maintenance has no track) |
| **Blocked by** | [M40](delete-allowed-host-removes-what-the-list-holds.md) |
| **Blocks** | [M44](yaml-editor-refuses-the-layouts-it-does-not-write.md) |
| **Reporter** | CLI-M40's skeptic (2026-11-22), then a sandboxed investigation and a skeptic's re-run the same day |
| **Created** | 2026-11-22 |

---

## Summary

On a YAML config whose host list is written compact (`allowed_hosts:` followed by `  - host` at the key's own indent, the form PyYAML and Ruby write), a website the gate reaches unasked can't be revoked. `delete-allowed-host` says it is not listed, and `add-allowed-host` calls the file "not valid YAML". On a few other shapes the loader reads, `set-permission run_command deny` prints success, and the model still runs commands unasked. The item makes the host scanner read compact lists, and has the three gate edits read their own result back through the loader, refusing with words when the change did not land.

## Description

### User story

**As** someone who keeps an older `config.yaml` that another tool or I rewrote,
**I want** revoking a website or denying a tool to take effect, or to say plainly that it did not,
**so that** "removed example.com" and "permissions.run_command = deny" are true when Apogee prints them.

### Steps to reproduce

The sandbox, 2026-11-22, with the built binary `apogee 0.2.0 (33ea685d, 2026-11-22, macos-arm64)`: a throwaway `APOGEE_HOME` and `HOME`, `env -i`, stdin from `/dev/null`, a mock backend. The config is `config.yaml`, written by hand or by `/usr/bin/ruby -ryaml` (Psych).

1. With `tools:\n  allowed_hosts:\n  - localhost\n`, and a mock backend whose script calls `fetch_url` on `http://localhost:18765/probe` (a loopback stub logs each GET), run a piped `apogee complete --tools 'fetch it'`. Then run `apogee config delete-allowed-host localhost`, then the run again.
2. With `tools:\n  allowed_hosts:\n  - docs.python.org\n  - example.com\n`, run `apogee config add-allowed-host pypi.org`.
3. With `"permissions":\n  run_command: allow\n`, and a mock script that calls `run_command` on `touch <work>/MARKER`, run `apogee config set-permission run_command deny`, `apogee config get permissions`, then a piped `apogee complete --tools 'run it'`.

### Actual result

1. The first run fetches unasked (`GET /probe`). The delete prints `apogee config: 'localhost' is not in tools.allowed_hosts` (exit 1), and the second run fetches unasked again.
2. `apogee config: <edited config>: not valid YAML: yaml-cpp: error at line 9, column 3: end of map not found` (exit 1), on a file that loads.
3. `permissions.run_command = deny` (exit 0), a second `permissions:` section appended. `config get permissions` prints `run_command: allow`, and the run creates `MARKER`.

### Expected result

1. The delete removes the `  - localhost` line, and the second run asks.
2. `  - pypi.org` is appended at the items' own indent; a remove then gives back the same bytes.
3. Either the deny lands, or the verb exits 1 with words saying it did not, and why, with the file unchanged. It never prints success while the loader still reads `allow`.

### Background

`find_host_list` (`config_edit.cpp:2493-2546`) is the scan that `add_allowed_host` and `remove_allowed_host` share. It was written once in 72c2d75a (2026-09-25, v0.1.3) with its own item scanner. Two fixes later taught the rest of the editor to read a list at its key's own indent, and neither reached it: `field_extent` (`:290-306`, 55f, 8d5aec51, "an indentless block list clears"), and 27f's `set_suite_consultable` (70efb72d).

The edits print success when the edited text parses: `edit_config_file` re-parses it (`config_edit.h:524-538`). That catches a file the edit would break. It can't catch an edit that parses and that the loader reads differently: yaml-cpp keeps the first of two `permissions` sections, `YAML::Load` reads only the first document (`config.cpp:1845`), and a duplicate key is tolerated. 57e's rule is "a printed success is a landed change" (CLAUDE.md → ⚠ One config mutation path). For the three edits that decide what the model may do unasked, that rule is broken in a safety-relevant way.

The population is narrow. Apogee's own YAML template always wrote the 2-space flow form (`  allowed_hosts: []`), and every install since v0.1.5 is JSONC, which is not affected. What is hit is a `config.yaml` from before v0.1.5 that another dumper rewrote, or a hand edit. For those people, the gate can't be closed through the verb, the HTTP twin or the Mac app.

The wider layout problem (a 4-space file, which 22 of 25 verbs refuse with misleading words) is CLI-M44's. This item is the part that must not wait.

### Evidence

All measured 2026-11-22 by the investigator, then again by a skeptic with its own fixtures, on the built binary. The working tree does not touch `config_edit.*`.

1. **The compact list, end to end.** The loader reads it: `config get`, `__complete config delete-allowed-host ''` and `check` (`ok allowed_hosts example.com`) all list the host, and the gate reaches it unasked. The delete is refused, and the gate still fetches. A nested list (`    - localhost`) stops the fetch after the same delete.
2. **Why the compact list reads as empty.** The block-item loop stops at `:2534`: `if (indent <= kEntryIndent || item_line.substr(indent, 1) != "-") break;`. Items at the key's own indent are never read, so remove reports `not in` (`:2637`). Add takes the null-key branch (`:2595-2603`): it rewrites the key line to `allowed_hosts: [x]` and leaves the compact items under it, which can't parse.
3. **The real producer.** `/usr/bin/ruby -ryaml`'s dump (`---` plus compact lists) gets the same two refusals, the add at line 10. So does a compact list with CRLF, with comments between items, or followed by another key.
4. **The HTTP twin agrees with the CLI, and contradicts itself.** On the compact list, `GET /v1/admin/allowed-hosts` lists `example.com`, `DELETE …/example.com` answers 400 `'example.com' is not in tools.allowed_hosts`, and `PUT …/pypi.org` answers 400 `config_error` `not valid YAML`.
5. **The same transform feeds every surface:**
   - the chat prompt's `[a]lways` answer, whose write fails with `could not write the config: <edited config>: not valid YAML` while the run itself is still allowed;
   - the Mac app's Settings › Permissions Add and Remove (`SettingsViewModel.swift:529`, `:539-543`);
   - the design prototype's add and remove.
6. **An item over two lines is split from its value.** With `    -\n      docs.python.org\n    - example.com`, the `:2534` break stops the scan at the continuation. `add-allowed-host pypi.org` printed `tools.allowed_hosts includes pypi.org`, and the loader then read `pypi.org docs.python.org`, `example.com` and an empty entry. docs.python.org is no longer allowed, and pypi.org is not allowed either (`check`: `'pypi.org docs.python.org' is not a host name, so it allows nothing`).
7. **A deny that never took effect, measured by the skeptic end to end.** With `"permissions":\n  run_command: allow`, `set-permission run_command deny` printed `permissions.run_command = deny` (exit 0). `config get permissions` printed `run_command: allow`, `check` printed `ok   permissions.run_command  allow`, and a piped `complete --tools 'run it'` ran the tool: `MARKER exists after the printed deny? YES`. The control with a bare `permissions:` header gave `deny`, and no marker. `permissions :` behaves the same as the quoted header.
   - The twin's `PUT /v1/admin/permissions/run_command {"level":"deny"}` answered `200 {"level":"allow",…}` and appended the same shadow section. It reads the level back (`admin_config.cpp:718-731`), so it reports the truth, but calls the call a success.
8. **Other printed successes that do not land**:
   - after a `...` document-end marker, `set-permission`, `add-allowed-host` and `config set ui.markdown` each printed success into a second document the loader never reads;
   - a quoted or spaced `allowed_hosts` key got a second key, the loader kept the old list, and a repeat add was a no-op;
   - in a 1-space file, `add-backend z` printed success and landed `z` as a field of another backend (`check`: `unknown key 'z'`).
9. **The read-back pattern exists.** The twin's permission route reads the level back after the write (`admin_config.cpp:718-731`). The 57e every-editor sweep can't catch these, since it checks only that no top-level key appears twice (`config_edit_test.cpp:2557-2560`).
10. **What already works**, and must stay:
    - a 2-space flow list and a nested block list, with comments, blank lines, CRLF, a leading `---`, or a tab after the colon or dash: delete exit 0 with one line changed, and add then remove byte-identical;
    - `allowed_hosts: []`, a missing key and a missing `tools:` section;
    - tabs as indentation, which the loader refuses before any editor runs;
    - a flow list over several lines, refused by name;
    - every verb on a JSONC config of any indentation;
    - `config migrate`, which carries the compact, Psych, item-over-two-lines and quoted-header files losslessly. After it, every verb lands.

## Acceptance criteria

- [ ] **Given** a compact host list (`allowed_hosts:` then `  - host` lines at the key's indent), **when** `config delete-allowed-host example.com` runs, **then** exactly the `  - example.com` line goes, every other line and comment stays, and a fetch to it asks again.
- [ ] **Given** the same list, **when** `config add-allowed-host pypi.org` runs, **then** `  - pypi.org` is appended after the last item at the items' indent, and a remove gives back the same bytes. The same holds for Ruby Psych's exact dump, with CRLF, with comments between items, and with another key after the list.
- [ ] **Given** an item whose value continues on the next line, **when** either host verb runs, **then** it exits 1 naming that shape ("write each `- host` on one line"), the file unchanged.
- [ ] **Given** any config on which `set-permission`, `add-allowed-host` or `delete-allowed-host` would produce text that parses but that the loader reads without the change (a quoted or spaced `permissions` header, a `...` marker, a quoted key), **when** the verb runs, **then** it exits 1, says the change would not take effect and names `apogee config migrate`, and the file is byte-identical.
- [ ] **Given** the HTTP twin on the compact list, **then** `DELETE` answers 200 and `PUT` answers 200, with the CLI's bytes. **Given** the twin on the quoted `permissions` header, **then** `PUT …/permissions/run_command` answers a 400 with the same words, never a 200.
- [ ] **Given** a nested or flow list, or a JSONC config, **then** every host and permission edit behaves byte for byte as today.

## Constraints

- **One config mutation path** (CLAUDE.md → ⚠): the transforms stay pure, text in and text out. The read-back is `parse_config` over the transform's own input and output, never a load from disk.
- **A printed success is a landed change** (57e). An edit that would not change what the loader reads is refused, never written.
- **Byte preservation** stays: every accepted edit changes only the lines it names, and add then remove is the exact inverse on a list whose key already existed.
- **No config key changes shape or meaning** ([ADR 0006](../../../adrs/cli/backwards-compatibility.md)). A file the editor edited before is edited the same way.
- **Mode parity** ([ADR 0002](../../../adrs/cli/mode-parity.md)): the twin answers the transform's refusal as it answers its others, and a client gets the same words the CLI prints.

## Technical approach

Paths under `lib/src/cli/`.

| File | Change |
|---|---|
| `source/data/contracts/config_edit.cpp` | `find_host_list` (`:2493-2546`): the block-item loop takes `- ` lines at the key's own indent, by `field_extent`'s rule (`:299`), and ends at the first content line at or above the key's indent that is not a dash. A deeper line that is not a dash, after an item, throws by name ("tools.allowed_hosts holds an item written over more than one line -- write each `- host` on one line") instead of ending the scan. `add_allowed_host` (`:2552-2607`) takes the null-key branch only when no item was read. |
| `source/data/contracts/config_edit.cpp` | The format wrappers `set_permission`, `add_allowed_host` and `remove_allowed_host` (`:3526-3540`) read their effect back. They `parse_config` the input and the result, and throw `ConfigEditError` when the result's reading lacks the change: the tool's level is not the one asked; the host is not listed (`host_listed`); or an entry CLI-M40's `allowed_host_matches` names is still listed. The words say the edit would not take effect in this file's layout, that nothing was changed, and name `apogee config migrate`. A result that does not parse is left to `edit_config_file`'s re-parse, as today. |
| `source/data/contracts/config_edit.h` | The doc comments of the three transforms (`:427-445`) name the layouts read (flow, nested and compact block) and the read-back refusal. |
| `tests/data/contracts/config_edit_test.cpp` | The fixtures under Testing. |
| `tests/presentation/httpserver/admin_config_test.cpp` | The twin on the compact list (`DELETE` and `PUT` 200, the CLI's bytes), and on the quoted `permissions` header (`PUT` 400, the transform's words). |
| `tests/scripts/cmake/config_e2e.cmake` | Ruby Psych's exact dump written as `config.yaml`: `delete-allowed-host example.com` exits 0 with one line gone, add then delete `pypi.org` gives back the bytes, and a quoted `permissions` header's `set-permission run_command deny` exits 1 with the bytes unchanged. |

**Reference.** `field_extent` (`:290-306`, 55f) and 27f's `set_suite_consultable`, the compact-list rule this mirrors. The twin's permission read-back (`admin_config.cpp:718-731`). 57e's "never shadowed" refusals, the words' precedent.

**Docs when it ships:**
- CLAUDE.md's ⚠ One config mutation path: a sub-rule that the gate's three edits read their effect back and refuse when it did not land.
- `reference/cli/tools.md`'s line on the host verbs and `set-permission`.
- DEVELOPER.md's `config_edit` row.
- MILESTONES: a dated entry.

## Testing

Hermetic and in process: the transforms over YAML and JSONC text, the twin through its handler, the end-to-end script in a temp home. Each fixture fails on today's binary (measured 2026-11-22).

- **The bug as a fixture: compact lists.** For each case, add `pypi.org` then remove it gives back the bytes. `parse_config` of the add lists `docs.python.org`, `example.com` and `pypi.org`; of a remove of `example.com`, `docs.python.org` alone.
  - compact: `tools:\n  allowed_hosts:\n  - docs.python.org\n  - example.com\n`;
  - Psych's exact dump: `---\nbackends:\n  m:\n    type: mock\n    model: m\nmodels:\n  default: m\ntools:\n  allowed_hosts:\n  - docs.python.org\n  - example.com\n`;
  - compact with a comment between items and one after an item (each kept; a removed item goes with its own comment only);
  - compact followed by `  disabled:\n  - shell\n`, which stays untouched;
  - compact with CRLF, the added line ending in CRLF;
  - compact with a tab after the dash.
- **The split item**: `add_allowed_host("tools:\n  allowed_hosts:\n    -\n      docs.python.org\n    - example.com\n", "pypi.org")` throws by name. Today it returns text the loader reads as `["", "pypi.org docs.python.org", "example.com"]`.
- **The read-back.** Each throws `ConfigEditError`, its `what()` naming `apogee config migrate` and holding neither `not valid YAML` nor `is not in tools.allowed_hosts`:
  - `set_permission("\"permissions\":\n  run_command: allow\n", "run_command", "deny")`, and the same with `permissions :`;
  - `set_permission("backends:\n  m:\n    type: mock\n...\n", "run_command", "deny")`;
  - `add_allowed_host("tools:\n  \"allowed_hosts\": [docs.python.org]\n", "pypi.org")`, and the same with `allowed_hosts :`;
  - `add_allowed_host("\"tools\":\n  allowed_hosts: [a.example]\n", "b.example")`.
- **Unchanged**: today's nested, flow, `[]` and JSONC cases (`config_edit_test.cpp:1105-1214`, `config_jsonc_test.cpp`) stay as they are.
- **Gate:** `lib/scripts/cicd.sh --app cli --test`, clang-format, and the MinGW scan of the changed files.

## Open questions

| Question | Status | Answer |
|---|---|---|
| Which edits read their effect back: the three gate edits, or every editor? | `[default]` | The three gate edits: they decide what the model may do unasked, and each has a one-line effect (a level, a host listed, a host gone). The other editors' shapes are refused by name in CLI-M44, which keeps this read-back. |
| An item over two lines: read it, or refuse it? | `[default]` | Refuse it by name. No mainstream dumper writes it, and reading it means rewriting the item's two lines. `config migrate` carries it. |
| Build order relative to CLI-M40, which rewrites `remove_allowed_host` (`:2610-2638`) beside this scan, in the same `[hosts]` tests. | `[default]` | Serially, M40 first: its predicate defines what "removed" means, which this item's read-back checks. This item's compact cases then also run M40's raw-entry and duplicate-spelling cases. Never in parallel. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-22 | Specced from CLI-M40's Out of scope, a sandboxed investigation and a skeptic's re-run (Evidence). Placed in **Maintenance** as M43. | the spec |
| 2026-11-22 | **Split from the wider layout fix (CLI-M44)**, on the skeptic's recommendation. A website that can't be revoked and a deny that never took effect are small fixes in two functions, and shouldn't wait on refusing every layout in 25 verbs. The read-back also catches shapes no one has listed. | the spec |

## Out of scope

- **A config indented other than 2 spaces**, which 22 of 25 verbs refuse with misleading words, and the quoted-header, `...` and 1-space shapes in every other editor: [CLI-M44](yaml-editor-refuses-the-layouts-it-does-not-write.md).
- **`edit_config_file`'s `<edited config>: not valid YAML` wording**: CLI-M44.
- **`config migrate`'s own gaps**, among them a `--- # comment` line, which it refuses with "the conversion did not carry the file exactly": [CLI-M47](config-migrate-carries-every-line-or-names-it.md).

## Links

- **Blocked by:** [CLI-M40](delete-allowed-host-removes-what-the-list-holds.md) · **Blocks:** [CLI-M44](yaml-editor-refuses-the-layouts-it-does-not-write.md)
- **Relates to:**
  - [CLI-M36](grant-and-deny-flags-offer-what-they-take.md): its `delete-allowed-host` offer comes from the loader (`complete_sources.cpp:793-795`), so on a compact list it offers hosts the verb refuses today. After this item, the verb takes them.
  - [CLI-M39](deny-takes-every-host-a-yes-takes.md): the same list, `--deny`'s side.
  - 55f and 27f, the compact-list fixes that never reached this scan; 57e, "a printed success is a landed change"; 72c2d75a, the scan's first commit.
