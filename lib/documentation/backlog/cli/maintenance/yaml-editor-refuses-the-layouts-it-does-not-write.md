# CLI-M44 · The YAML editor refuses, by name, the layouts it does not write

| Field | Value |
|---|---|
| **Type** | 🐞 Bug |
| **Project** | CLI |
| **Status** | 🟢 Ready |
| **Fix version** | Maintenance (any time) |
| **Component** | Config editing › the YAML line editor (`contracts/config_edit.cpp`), every `config` verb and its HTTP twin |
| **Labels** | `config` `yaml` |
| **Epic** | — (maintenance has no track) |
| **Blocked by** | — |
| **Blocks** | — |
| **Reporter** | CLI-M40's skeptic (2026-11-22), then a sandboxed investigation and a skeptic's re-run the same day |
| **Created** | 2026-11-22 |

---

## Summary

The YAML line editor finds a key only when it is spelled bare at the one column Apogee writes. On a valid `config.yaml` in any other layout, it misreads the file. A 4-space file, the form Go's yaml.v3 writes, gets 22 of 25 verbs refusing it: 12 call it "not valid YAML", and 10 say an entry `config get` lists is missing. A 1-space file takes an `add-backend` as a field of another backend, and prints success. A quoted section header or a `...` end marker sends an edit where the loader never reads it. The item makes every editor refuse such a layout by name, naming the indent it measured and `apogee config migrate`, and makes a would-not-load edit say so instead of calling the file broken.

## Description

### User story

**As** someone whose older `config.yaml` was written by another tool, or by hand in another indent,
**I want** every `config` verb either to make its edit as it says, or to tell me plainly why it won't and what to run,
**so that** I am never told my working file is "not valid YAML", or that a backend I can see is "not found", and a printed success always means the change landed.

### Steps to reproduce

The sandbox, 2026-11-22, with the built binary `apogee 0.2.0 (33ea685d, 2026-11-22, macos-arm64)`: a throwaway `APOGEE_HOME` and `HOME`, `env -i`, stdin from `/dev/null`, a mock backend, `config.yaml` written by hand. A matrix script runs 25 verbs, each on a fresh copy, and snapshots 20 `config get` reads before and after.

1. With the whole config at 4 spaces per level (`backends:\n    m:\n        type: mock\n        model: m\n…`), run `apogee config set-default m` and `apogee config set-backend m --model q`.
2. With the whole config at 1 space per level (`backends:\n m:\n  type: mock\n  model: m\n…`), run `apogee config add-backend z --type mock --model zz`, then `apogee config get backends`.
3. With `"backends":\n  m:\n    type: mock\n`, run `apogee config add-backend z --type mock`, then `apogee config get backends`.
4. With a `...` line ending the file, run `apogee config set ui.markdown false`, then `apogee config get ui.markdown`.

### Actual result

1. `apogee config: <edited config>: not valid YAML: …` and `backend 'm' not found in config`, both exit 1, on a file `config get` reads.
2. `added backend 'z' to …/config.yaml` (exit 0), the lines `  z:\n    type: mock\n    model: zz` landing under `m`. `config get backends` prints `m` alone, and `check` warns `backend: m  unknown key 'z'`.
3. `added backend 'z'` (exit 0) into a second `backends:` section. `config get backends` lists `m` alone.
4. Success printed, a `ui:` section written after `...`. `config get ui.markdown` prints `true`.

### Expected result

1. Both exit 1 with the file unchanged and words that name the layout: the `backends:` section is indented 4 spaces; the editor edits the 2-space layout Apogee writes; re-indent it, or carry the file to JSONC with `apogee config migrate`.
2. The same refusal, naming 1 space, the file unchanged.
3. A refusal naming the quoted header and `apogee config migrate`, the file unchanged.
4. `ui.markdown` set in the document the loader reads, so `config get` prints `false`.

### Background

The editor's lookups assume Apogee's own layout: section headers bare at column 0, entries at `kEntryIndent` (2, `config_edit.cpp:45-46`, "Entries under a map section sit at exactly this indent"), and fields at `kFieldIndent`:
- `is_section_header` (`:150-155`) matches a literal `section:` at column 0, and `find_section` (`:221-252`) uses it;
- `entry_name` (`:178`), `find_entry_line` (`:638`) and `entry_extent` (`:615`) look at those columns;
- so do `set_section_rendered` (`:2181-2196`), `set_subsection_rendered` (`:2241`, `:2273`), `set_mcp_server_enabled` (`:982`) and `rename_backend`'s key lookup (`:2825`);
- the section-append paths (`:2564-2571`, `:2208-2220`, `:2250-2262`, `append_entry` `:803-816`) write at the end of the file.

`edit_config_file` re-parses the result (`config_edit.h:524-538`), so most misplaced lines are rejected and the file is never damaged. The damage is in the words, and in what parses but reads differently.

YAML has been the legacy format since 28i (v0.1.5). It still loads, with a notice naming `config migrate`, and `config migrate` carries every layout measured here losslessly; after it, every verb lands. (`migrate` itself refuses or half-carries a few other shapes: a comment on a line where no node starts, a multi-line last value followed by comments, a BOM or UTF-16 file, a later document. That is [CLI-M47](config-migrate-carries-every-line-or-names-it.md).) Apogee's own YAML template always wrote the 2-space layout, so only a file another tool rewrote, or a hand edit, is hit. JSONC configs are edited through `yaml_of_json`, which writes 2-space mappings, so they are not affected in any indent.

CLI-M43 fixes the safety-relevant part first: the compact host list, and a read-back on the three gate edits. This item is the rest, across every editor.

### Evidence

All measured 2026-11-22 by the investigator, then again by a skeptic with its own fixtures, on the built binary.

1. **The 4-space file, 25 verbs:**
   - **12 say "not valid YAML"**: add-backend, set-default, set-default-utility, set-permission (replace and insert), add-suite, set-default-suite, `config set` (`memory.recall`, `tools.search.url`, `tools.search.results`), `mcp create`, add-allowed-host.
   - **10 say an entry `config get` lists is missing**: `backend 'm' not found in config` (set-backend three ways, delete-backend), `no backend named 'n'` (rename-backend), `suite 's' not found` (set-suite, delete-suite), `MCP server 'srv' not found` (`mcp enable`, `delete-mcp-server`), and `is not in tools.allowed_hosts`.
   - **Only edits that create a new section land** (`config set ui.markdown`, add-graph), written at 2 spaces beside the 4-space ones. The file stays valid and reads as meant.

   The skeptic spot-checked 8 of them.
2. **A 1-space file is corrupted, and the re-parse passes it** (the skeptic, `one.yaml`). `append_entry` writes at `kEntryIndent`, deeper than this file's entries, so `z` becomes a field of `m`.
3. **Entries at 2 with fields at 3 or 6 get misleading words too.** `set-backend m --model q` on 6 gives `<edited config>: not valid YAML: … illegal map value`. `delete-backend m` on 3 gives `<edited config>: backends.type: expected a block of settings`. So a refusal that checks only a section's direct children misses these. The re-parse also catches the typed loader's failures, not only YAML syntax.
4. **A quoted or spaced section header reaches every editor.** `"backends":`, `models :`, `"tools":` and `tools :` all read as absent to `is_section_header`, so each edit appends a second section. yaml-cpp tolerates the duplicate and keeps the first. `add-backend z` printed `added backend 'z'`, and `config get backends` still lists `m` alone. A quoted entry key (`  "write_file": allow`) gets a duplicate entry the same way.
5. **A `...` document-end marker.** Every edit that appends a section printed success into a second YAML document. `YAML::Load` reads the first only (`config.cpp:1845`). Measured: `ui.markdown = false` (get prints `true`), `permissions.run_command = deny` (get prints nothing), and `tools.allowed_hosts includes pypi.org` (get prints nothing).
6. **The words contradict the file.** `config_edit.h:512-513` promises that "already broken" and "this edit would break it" are different messages. They are literally different (the original is parsed under its path, the result under `"<edited config>"`), but `<edited config>: not valid YAML` reads as "your file is broken" to anyone who did not write the editor.
7. **The HTTP twin answers the same.** On the 4-space file, `PUT …/allowed-hosts/pypi.org` answers 400 `config_error` `not valid YAML`. Nothing in `lib/src/darwin`, `lib/design/prototype` or the reference docs reads `config_error` or `not valid YAML`, so changing either breaks no client.
8. **What a per-section refusal leaves working.** On a mixed file (backends at 2, models and tools at 4), rename-backend, delete-backend of an unreferenced backend, set-backend and add-backend all land today. A refusal raised when a section is looked up does not touch them: rename's reference walk does not call `find_section`, and `delete_backend_text` calls `set_models_role` only when a pointer names the backend (`backend_fields.cpp:401-421`).
9. **What already works**, and must stay:
   - every verb on the compact layout except the two host verbs, which CLI-M43 fixes (`set-backend --server-tools` over a compact list, `mcp enable` over a compact `args:`, rename-backend, set-permission, add-suite, set-suite, `config set`, `mcp create`, add-graph);
   - every verb on a JSONC config of any indent;
   - tabs as indentation, refused by the loader first;
   - a flow list over several lines, and `tools: {…}`, refused by name since 57e;
   - `config format`, which never parses structure.
10. **A UTF-16 config takes an edit it never reads back** (CLI-M47's critic). `config set ui.markdown false` printed `ui.markdown = false` (exit 0), and `get` then printed `true`. `config add-allowed-host pypi.org` printed `tools.allowed_hosts includes pypi.org`, and `get` printed `docs.python.org`. The file ends in UTF-8 bytes appended to the UTF-16 text (`… 616c 6c6f 7765 645f 686f 7374 733a 205b 7079 7069 2e6f 7267 5d0a`). yaml-cpp decodes UTF-16 itself, so the file loads, while `read_config_file` hands the editor raw bytes (`config_edit.cpp:3548-3560`).
11. **The pattern to copy.** `rename_backend`'s reference walk (`:2853-2900`) is an indent stack that already handles any indent, a list at its key's own indent, and quoted keys (`key_token_end`, `token_text_of`).

## Acceptance criteria

- [ ] **Given** a YAML config whose section, entry or field indent is not the one the editor writes (4 spaces, 1, 3 or 6), **when** any `config` verb edits that section, **then** it exits 1 with the file byte-identical, and the words name the section, the indent measured, the 2-space layout the editor edits and `apogee config migrate`. They never say `not valid YAML`, `not found` or `is not in`.
- [ ] **Given** a section the verb does not edit is in another indent, **then** the verb behaves as today (the mixed-file cases in Evidence 8 land).
- [ ] **Given** a compact list (`- item` at its key's own indent) anywhere the editor reads, **then** it is not taken for a layout fault, and every verb that lands on it today still lands.
- [ ] **Given** a quoted or spaced section header or entry key the verb is looking for (`"backends":`, `models :`, `"write_file":`), **when** the verb runs, **then** it exits 1 naming that key and `apogee config migrate`, and no second section or key is ever written.
- [ ] **Given** a config ending in a `...` marker, **when** an edit adds a section, **then** the section is inserted before the `...`, and `config get` reads the change. A file whose second document holds content is refused by name, naming the hand edit (`config migrate` refuses that shape too, CLI-M47).
- [ ] **Given** a config that is not UTF-8 (a UTF-16 or UTF-32 BOM, or a NUL byte), **when** any `config` verb edits it, **then** it exits 1 naming the encoding ("save it as UTF-8"), the file byte-identical.
- [ ] **Given** an edit whose result would not load, on a config that loads, **when** the verb runs, **then** it says the edit was not applied because its result would not load (with the loader's detail after), and that nothing was changed. It never says `<edited config>: not valid YAML`. The twin answers 400 `invalid_request_error` with the same words.
- [ ] **Given** the template's 2-space layout, a JSONC config, or any shape today's tests and the 57e sweep accept, **then** every verb behaves byte for byte as today.

## Constraints

- **One config mutation path** (CLAUDE.md → ⚠): pure transforms, every edit re-parsed before it lands, written through temp-file-then-rename.
- **A printed success is a landed change** (57e): a shape the editor does not see into is refused by name by every editor, never shadowed by a block appended beside it.
- **Refuse, don't re-indent.** The editor edits the 2-space layout it writes. Another layout is refused with `apogee config migrate` named as the way out ([ADR 0006](../../../adrs/cli/backwards-compatibility.md): refuse by name, offer the migration).
- **Byte preservation** on every layout the editor accepts.
- **CLI-M43's read-back on the three gate edits stays**, as defense behind these refusals.
- **Mode parity** ([ADR 0002](../../../adrs/cli/mode-parity.md)): the twin and the GUIs get the CLI's words.

## Technical approach

Paths under `lib/src/cli/`.

| File | Change |
|---|---|
| `source/data/contracts/config_edit.cpp` | **A layout refusal**, one helper beside `inline_value_refusal` (`:164-170`). `find_section` (`:221-252`) calls it once the header is found, and the entry and field lookups call it for the levels they read: entries at `kEntryIndent`, an entry's fields at `kFieldIndent`, suite members and `kGraphFieldIndent` where those are written. It throws when the first content line of a level sits at another indent, naming the section, the indent measured and `apogee config migrate`. A `- ` line at its key's own indent is a compact list item, never a fault. The `kEntryIndent` comment (`:45`) states the rule. |
| `source/data/contracts/config_edit.cpp` | **Keys as YAML reads them.** `is_section_header` (`:150-155`) and the entry-key lookups (`entry_name` `:178`, `find_entry_line` `:638`, `rename_backend` `:2825`) recognise a quoted or spaced key by `key_token_end` / `token_text_of` (as `:2828-2840` already do), and refuse it by name rather than reading it as absent. |
| `source/data/contracts/config_edit.cpp` | **The document end.** The section-append paths (`:2564-2571`, `:2208-2220`, `:2250-2262`, `append_entry` `:803-816`) insert before a final `...` line. A file holding a second YAML document with content is refused by name, naming the hand edit. |
| `source/data/contracts/config_edit.cpp` | **The encoding.** `in_format`, the one entry every exported transform goes through, refuses a text holding a NUL byte or starting with a UTF-16/32 BOM, naming the encoding, in either format. |
| `source/data/contracts/config_edit.h` | `edit_config_file` (`:524-538`): when the original parsed and the result does not, throw `ConfigEditError` saying the edit was not applied because its result would not load, then the loader's detail, and that nothing was changed. The header comment (`:503-517`) and the transforms' comments name the layouts edited and refused. |
| `source/presentation/httpserver/admin_config.cpp` | No new mapping: the refusals are `ConfigEditError`, answered 400 `invalid_request_error` as today's are, no longer the parse path's `config_error`. |
| `tests/data/contracts/config_edit_test.cpp` | The 57e every-editor sweep (`:2532-2575`) and the cases under Testing. |
| `tests/presentation/httpserver/admin_config_test.cpp` | The twin on a 4-space file: 400 `invalid_request_error` with the layout words, never `config_error` or a 404. |
| `tests/scripts/cmake/config_e2e.cmake` | A 4-space file's `add-allowed-host` refused naming migrate, the bytes unchanged. Then `config migrate`, and the same edit exits 0. |

**Rejected: detect the file's indent and write at it.** It would re-indent every renderer: `format_backend_entry` and its siblings at `kEntryIndent` and `kFieldIndent` (`:507`, `:578`, `:883`, `:918`, `:1016`, `:1079`, `:1246`), `kGraphFieldIndent`, and suite members at 6. That cost is out of proportion for the legacy format, which `config migrate` already carries losslessly. Doing it for some verbs only would have one verb edit a file another refuses.

**Reference.** 57e's refusals (`inline_value_refusal`, "never shadowed") and their words. `rename_backend`'s indent-agnostic reference walk (`:2853-2900`). `field_extent` (55f), the compact-list rule.

**Docs when it ships:**
- CLAUDE.md's ⚠ One config mutation path: a sub-rule that the YAML editor edits the 2-space layout it writes, reads a list at its key's own indent, and refuses any other indent and a quoted or spaced key it looks for, naming `config migrate`, and a second document or a non-UTF-8 file, naming the hand edit. Its Enforcement line names the new sweep shapes.
- DEVELOPER.md's `config_edit.h/.cpp` row (`:368`) and `config_edit_test` row (`:658`).
- `reference/cli/tools.md`'s line on the config verbs.
- MILESTONES: a dated entry.

## Testing

Hermetic and in process. Each refused case fails on today's binary (measured 2026-11-22).

- **The bug as a fixture: the every-editor sweep** (`config_edit_test.cpp:2532-2575`) gains these shapes:
  - `k4Space`, a whole 4-space config (the matrix's `verbs/four.yaml`);
  - `k1Space`;
  - `kField3` and `kField6`, entries at 2 with fields at 3 or 6;
  - `kQuotedHeader` (`"backends":\n  a:\n    type: mock\n`) and `kSpacedHeader` (`models :`);
  - `kDocEnd` (a config followed by `...`);
  - `kCompact` (the matrix's `verbs/compact.yaml`), which must be accepted.

  Two rules change. A landed edit must change `parse_config`'s reading by exactly the verb's effect, not merely parse. A refusal may say `not found` or `is not in` only when `parse_config` does not list the name.
- **The words**: for each refused shape, `what()` names the indent or key and `apogee config migrate`, and holds no `not valid YAML`.
- **The document end**: `set_section_rendered` for `ui.markdown` on `kDocEnd` gives text whose `parse_config` reads `false`. A second document with content throws by name.
- **The encoding**: every exported transform on the legacy template saved as UTF-16LE or UTF-16BE (built in code) throws naming UTF-16, the text unchanged.
- **The file layer**: `edit_config_file` on a 4-space config leaves the bytes unchanged, and its message holds no `<edited config>: not valid YAML`. An edit whose result fails the typed loader says the edit was not applied.
- **Unchanged**: every case today's suite accepts gives the same bytes.
- **Gate:** `lib/scripts/cicd.sh --app cli --test`, clang-format, and the MinGW scan of the changed files.

## Open questions

| Question | Status | Answer |
|---|---|---|
| Edit another indent in place (detect it and write at it), or refuse it naming `config migrate`? | `[default]` | Refuse, naming migrate. That is 57e's rule for shapes the editor does not see into, and ADR 0006's. migrate carries every measured layout losslessly. Detecting would re-indent every renderer, for the legacy format only. |
| A quoted or spaced header or key: read it and edit where it stands, or refuse it by name? | `[default]` | Refuse by name, naming migrate (57e's precedent; migrate carries those files). Reading them means every lookup learns YAML's key grammar for a rare shape. |
| A `...` end marker: insert before it, or refuse the file? | `[default]` | Insert new sections before a final `...`, so the edit lands in the document the loader reads, byte-preserving and still the exact inverse. Refuse a file whose second document holds content, naming the hand edit, since `config migrate` refuses it too (CLI-M47). |
| Should completion offer nothing on a file whose layout every editor refuses, since ADR 0007 calls "a candidate the verb would refuse" a completion bug? | `[default]` | No. Completion keeps offering the names the loader reads. The verb refuses the file's layout, not the name, and its words name the way out; Tab offering nothing would give the person no words at all (bash can't show a hint). Recorded as a decision when this ships, in the Decision log and DEVELOPER.md's completion notes. |
| Should `check` grow a row warning that the YAML layout is one the editor refuses? | `[default]` | No. The compat-read row already says `the older YAML format, still read` with `run: apogee config migrate`, which is the same remedy. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-22 | Specced from CLI-M40's Out of scope, a sandboxed investigation and a skeptic's re-run (Evidence). Placed in **Maintenance** as M44, after CLI-M43, which takes the safety-relevant part first. | the spec |
| 2026-11-22 | **The refusal covers every level the editor writes, not only a section's direct children**, after the skeptic measured the 1-space corruption and the misleading words at field indents 3 and 6. It names the indent it measured, not a fixed "4 spaces". | the spec |
| 2026-11-22 | **A non-UTF-8 config is one more layout refused by name**, folded in from CLI-M47's investigation (Evidence 10): the editor appends UTF-8 to a UTF-16 file and prints success. The second-document refusal names the hand edit, not `config migrate`, which CLI-M47 makes refuse that shape too. | the spec |
| 2026-11-22 | **The would-not-load words name no single cause**, since the re-parse catches the typed loader's failures as well as YAML syntax (the skeptic's `backends.type: expected a block of settings`). | the spec |

## Out of scope

- **The compact host list and the gate edits' read-back**: [CLI-M43](../../../assistant/cli/MILESTONES.md#2026-11-27--gate-edits-land-or-say-they-did-not-maintenance-item-m43-the-gates-config-edits-land-or-say-they-did-not) (shipped 2026-11-27), which this builds on.
- **`config migrate`'s own gaps** (a `--- # comment` line, a multi-line last value followed by comments, a BOM, UTF-16, a later document): [CLI-M47](config-migrate-carries-every-line-or-names-it.md). Best built before or with this item, since this item's refusals send people to `migrate`.
- **Re-indenting a file** for the person: migrate is the way out.

## Links

- **Blocked by:** — · **Blocks:** —
- **Relates to:**
  - [CLI-M43](../../../assistant/cli/MILESTONES.md#2026-11-27--gate-edits-land-or-say-they-did-not-maintenance-item-m43-the-gates-config-edits-land-or-say-they-did-not) (shipped 2026-11-27): the compact host list and the three gate edits' read-back, which this item keeps behind its refusals.
  - [CLI-M40](../../../assistant/cli/MILESTONES.md#2026-11-27--delete-allowed-host-removes-what-the-list-holds-maintenance-item-m40-the-verb-removes-an-entry-as-written-and-every-spelling-of-a-host) (shipped 2026-11-27), whose Out of scope named these layouts.
  - [CLI-M36](../../../assistant/cli/MILESTONES.md#2026-11-27--grant-and-deny-flags-offer-what-they-take-maintenance-item-m36-the-permission-flags-offer-what-their-verbs-take): its offers come from the loader, and they stay so on a refused layout (the open question above).
  - [CLI-M46](a-collection-the-config-cannot-hold-is-refused-before-it-is-written.md), whose dry run asks this item's refusals before an ingest writes anything.
  - [CLI-M47](config-migrate-carries-every-line-or-names-it.md), the way out these refusals name, made to carry what it refuses today.
  - [CLI-M52](a-refused-scaffold-writes-nothing.md): `agents create` and `mcp create` dry-run their entry before the first file, so this item's layout refusals leave no scaffold behind once it ships.
  - [CLI-M53](a-symphony-the-cli-writes-is-one-it-can-delete.md) and [CLI-M54](rename-and-get-find-an-entry-by-its-folded-name.md), which edit the same `find_entry_line`, `entry_name` and `rename_backend` key lookup: either order works, and whichever lands second merges the other's change.
  - 57e, "a printed success is a landed change" and its refusals; 28i, JSONC and `config migrate`; 55f and 27f, the compact-list rule.
