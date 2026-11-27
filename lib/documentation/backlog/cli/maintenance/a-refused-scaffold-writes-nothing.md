# CLI-M52 · A refused agent or MCP scaffold writes nothing

| Field | Value |
|---|---|
| **Type** | 🐞 Bug |
| **Project** | CLI |
| **Status** | 🟢 Ready |
| **Fix version** | Maintenance (any time) |
| **Component** | Scaffolds › `agents create`, `mcp create` in scaffold mode, and their HTTP twins (`business/scaffold`) |
| **Labels** | `scaffold` `agents` `mcp` `config` |
| **Epic** | — (maintenance has no track) |
| **Blocked by** | — |
| **Blocks** | — |
| **Reporter** | The APP-M21 and APP-M22 probes' CLI asides (2026-11-26), then a sandboxed investigation, two skeptics and a critic the same day |
| **Created** | 2026-11-26 |

---

## Summary

`apogee agents create -- -x` writes `prompts/-x.txt` and `schemas/-x-output.json`, then the config refuses the name, and the command exits 1 with both files left behind. Every refusal the config is certain to give comes after the files: the name rule, a case-folded collision, an existing entry, and a missing or broken config. `mcp create` does the same with a three-file folder, and so do the HTTP twins. A retry then loops on "already exists". On a case-insensitive disk, following that message's `--force` advice replaced another agent. The item runs the config's own add on its text before the first file, as `models pull` already does, so a refused scaffold writes nothing.

## Description

### User story

**As** someone making an agent or an MCP server, on the command line, over HTTP or from the Mac app's Library,
**I want** a name or a config Apogee can't take to be refused before anything is written,
**so that** a refusal leaves my home as it was, the same command gives the same answer twice, and following a printed remedy never replaces something else.

### Steps to reproduce

The sandbox, 2026-11-26, with the built binary `apogee 0.2.0 (33ea685d, 2026-11-22, macos-arm64)`, which carries HEAD 5b8927c9's code in every file this item touches. A throwaway home three folders deep (`<sandbox>/l1/l2/l3/home`), `env -i PATH=/usr/bin:/bin` with `APOGEE_HOME` and `HOME` set to it, the working folder a sibling, stdin from `/dev/null`. Then `apogee config init` and `apogee config add-backend echo --type mock`, with a copy of `config/config.json` kept.

1. Run `apogee agents create -- -x`. List the files newer than a stamp, and `cmp` the config with its copy.
2. Run `apogee agents create -- .x`, then `apogee agents create --force -- -x`.
3. Run `apogee agents create foo --description 'the original'`, then `apogee agents create FOO`, then `apogee agents create --force FOO`. Grep `config.json` and read `prompts/foo.txt`.
4. Run `apogee mcp create -- -m` three times, the third with `--force`.
5. Start `apogee serve --bind 127.0.0.1 --port 18457` with the home's `--print-admin-token`. Create agent `foo2`, then remove its two files. Send `POST /v1/admin/agents {"name":"FOO2"}` and `{"name":"-x"}`, and `POST /v1/admin/mcp-servers {"name":"-y"}`.

### Actual result

1. Exit 1, stdout empty, stderr `apogee agents: updating config: agent name '-x' is not one both config formats can edit -- a name is letters, digits, '_', '-', '.' and '/', first neither '-' nor '.', and not null (the prompt was written to …/home/prompts/-x.txt)`. The new files are `prompts/-x.txt` and `schemas/-x-output.json`. The config is byte-identical.
2. `.x`: `apogee agents: prompt file already exists: …/home/prompts/-x.txt (use --force to overwrite)`, exit 1. `--force -- -x` overwrites both files and gives the first refusal again, exit 1.
3. `FOO`: `apogee agents: prompt file already exists: …/home/prompts/FOO.txt (use --force to overwrite)`, exit 1, config unchanged. `--force FOO`: `created agent 'FOO'`, exit 0. `config.json` now holds `"FOO": {` at foo's place, without foo's description `the original`. `prompts/foo.txt` begins `You are FOO -- a focused assistant.`
4. The first run: `apogee mcp: updating config: MCP server name '-m' is not one both config formats can edit -- … (the scaffold was written to …/home/mcp/-m)`, exit 1, with `mcp/-m/` holding `server.py`, `test_server.py` and `README.md`. The second: `apogee mcp: MCP server directory already exists: …/home/mcp/-m (use --force to overwrite)`, exit 1. The third writes again and gives the first refusal again, exit 1.
5. `FOO2`: `{"error":{"message":"updating config: agent 'FOO2' collides with existing 'foo2' -- agent names are compared case-insensitively, … (the prompt was written to …/home/prompts/FOO2.txt)","type":"conflict"}}`, HTTP 409, with `FOO2.txt` and `FOO2-output.json` left. `-x`: the name rule with `"type":""`, HTTP 400, both files left. `-y`: HTTP 400, `mcp/-y/` left.

No reader shows the leftovers: not `agents list`, `mcp list`, `__complete`, the HTTP lists or `check`.

### Expected result

1. Exit 1 with one stderr line beginning `apogee agents: ` that names the rule. Nothing is on disk, and the config is byte-identical.
2. `.x` is refused naming both `'.x'` and the `'-x'` it maps to, and nothing is written. Any rerun, with or without `--force`, gives the same refusal.
3. `FOO` is refused with `collides with existing 'foo'`, on every disk, and nothing is written. `--force FOO` still replaces foo's entry in place, since the fold rule makes them one agent (Open questions). The change is that the person is told first.
4. Every run exits 1 with the name rule, and `mcp/-m` never exists.
5. `FOO2` answers 409 conflict with the collision and writes no file. `-x` answers 400 with type `invalid_request_error` and writes no file, and a second POST answers the same. `-y` answers 400 with type `invalid_request_error`, and `mcp/-y` never exists.

Files are kept only after trouble that comes later: a lock timeout, a raced edit, or a failed second write. The words then name every file written.

### Background

The two scaffolds were born on 2026-09-13: `create_mcp_server` with [Milestone W](../../../assistant/cli/MILESTONES.md#milestone-w--the-mcp-client) (2fe0f9b2) and `create_agent` with [Milestone X](../../../assistant/cli/MILESTONES.md#milestone-x--agents-as-data) (f37cf4ad). Each is the one core its CLI verb and its admin route call. Each writes its files first, then appends the config entry through the config editor, and its catch block says why: "The files were written; say so rather than hiding it". At birth the shared add (`append_entry`) already refused a case-folded collision and an existing entry without `--force`, so those two refusals have always come after the files.

On 2026-10-25, 55f (8d5aec51) put one name rule in that shared add, `yaml_plain_key`'s set. Its record says the rule covers "agents, MCP servers, collections, graphs, suites and review targets". That is true of the config entry. But the scaffolds check names first with their own sanitizers. Those allow a leading `-`, map `.` and `:` to `-`, and have no null words and no length rule. So 55f's refusals reach the person after the files are written, worded as a config failure.

One part of today's behaviour is pinned intent: `agent_test.cpp:147-160`, "a config failure leaves the files on disk and says so", over a broken config. The two cores' header comments describe the same behaviour for any config failure (`agent.h:56-57`, `mcp_server.h:36-37`). No reference doc or MILESTONES entry records it. This item reverses it as a dated decision, not as a bug fix.

The cure is already in the repository, twice:
- `models pull`'s `check_registrations` refuses a missing or broken config before its first stage, then runs the one add that will register the name on the config's text and keeps the result nowhere: "A name the config cannot hold is refused now by the one edit that will add it".
- `create_symphony` holds a definition "to everything before a byte is written" (DEVELOPER.md's `scaffold/` row).

[CLI-M46](a-collection-the-config-cannot-hold-is-refused-before-it-is-written.md) specs the same pattern for collections.

The probes behind [APP-M21](../../../assistant/darwin/MILESTONES.md#2026-11-26--listed-name-reaches-the-verb-maintenance-item-m21-a-listed-name-reaches-the-verb-as-listed) listed this among their CLI asides: "`agents create` leaving `prompts/-x.txt` behind on a refused name". The investigation that followed the APP-M21 and APP-M22 probes measured it, and found the case-fold harm and the other refusal classes. The Mac app's Library › New agent passes the typed name without `--`, so a dash name never reaches the scaffold from the app; that is APP-M21's and APP-M23's seam. But `.x`, `null` and a case variant do reach it.

The design, in prose. Each core, after its sanitizer and before its existing-file checks, loads the config and runs the add it will make on the config's text. A refusal there is said and nothing is written. Then the files are written and the locked edit runs as today. Only trouble after that point can still leave files, and the words name each one.

### Evidence

All measured 2026-11-26 by an investigator, two skeptics and a critic, each in its own sandbox, with the built binary `apogee 0.2.0 (33ea685d, 2026-11-22, macos-arm64)`. Every run used `env -i PATH=/usr/bin:/bin`, a home three folders deep, stdin from `/dev/null` and mock backends only, with stdout, stderr and exit recorded separately. Probes are labelled by who ran them and their sandbox: `i-` the investigator, `sc-` the cause skeptic, `sf-` the fix skeptic, `c-` the critic.

1. **The binary carries HEAD's code here** (`c-v`). `apogee version` printed `apogee 0.2.0 (33ea685d, 2026-11-22, macos-arm64)` / `channel: release`. The one commit after it, 5b8927c9, changes 11 CLI files, none of them in this item, and `strings` finds its new completion text once.
2. **The core case** (`c-k`, reproduced by `i-a` and `sc-a`). `agents create -- -x` gave the refusal in Actual result 1, stdout empty, exit 1. `find . -newer stamp -type f` listed `./prompts/-x.txt` and `./schemas/-x-output.json`. `cmp` found the config byte-identical.
3. **The retry loop** (`c-k`, `i-a`).
   - `agents create -- .x` gave `prompt file already exists: …/prompts/-x.txt (use --force to overwrite)`, exit 1. `--force -- -x` overwrote both files and gave the name refusal again.
   - `-- :y` was refused as `-y` and left both files. `null` left both files, and `Null` then gave `prompt file already exists: …/prompts/Null.txt`, because APFS folds case.
   - `true` and `a.b` were created (`a.b` as `a-b`).
   - `agents delete --purge -- -x` gave `no agent named '-x'`. No verb removes a leftover.
4. **On APFS, the file check speaks before the collision, and its remedy replaces the other agent** (`c-a`).
   - `agents create FOO` beside `foo` gave `prompt file already exists: …/prompts/FOO.txt (use --force to overwrite)`, exit 1, config unchanged.
   - `agents create --force FOO` gave `created agent 'FOO'`, exit 0. Afterwards `config.json:534` held `"FOO": {` with `the original` gone, `ls prompts` showed only `foo.txt`, and its first line read `You are FOO -- a focused assistant.`
   - Two rules combine: `append_entry` skips the fold check under force (`config_edit.cpp:731-741`), and replaces an entry in place through the case-folded `find_entry_line` (`:745-760`, `:629-650`).
   - Over HTTP (`sf-g`): `POST /v1/admin/agents {"name":"FOO"}` beside `foo` answered `prompt file already exists: …/prompts/FOO.txt (use --force to overwrite)` with HTTP 409.
   - Where the variant's files are absent, the run reaches the config and leaves files (Evidence 6). On a case-sensitive disk that is every collision, by reading; no Linux host was run.
5. **MCP** (`c-a`, `i-a`).
   - `mcp create -- -m` gave the refusal in Actual result 4 and left `mcp/-m/` with 3 files. The second run gave `MCP server directory already exists`, and `--force` gave the name refusal again.
   - `-- .z` and `null` each left 3 files, and `mcp list` said none were configured.
6. **HTTP** (`c-g`, `i-g`), on a loopback serve with the home's admin token, stopped afterwards.
   - `POST /v1/admin/agents {"name":"FOO2"}`, with `foo2`'s files removed, gave `{"error":{"message":"updating config: agent 'FOO2' collides with existing 'foo2' -- agent names are compared case-insensitively, … (the prompt was written to …/home/prompts/FOO2.txt)","type":"conflict"}}` with HTTP 409. `FOO2.txt` and `FOO2-output.json` were left.
   - `POST {"name":"-x"}` gave `{"error":{"message":"updating config: agent name '-x' is not one both config formats can edit -- … (the prompt was written to …/home/prompts/-x.txt)","type":""}}` with HTTP 400, and both files were left. The same POST again answered 409 (`i-g`).
   - `POST /v1/admin/mcp-servers {"name":"-y"}` answered 400 and left `mcp/-y/` (`i-g`, `sc-g`).
   - The cause: both routes send any message containing `already exists` or `collides` to 409, and otherwise pass an explicit empty type (`admin_agents.cpp:123-127`, `admin_config.cpp:597-601`). `error_response` defaults to `invalid_request_error` (`http_types.h:83-84`), which is what [http-api.md's error table](../../../reference/cli/http-api.md#errors) documents for a 400 (`:103`).
7. **A config-less home** (`i-j`, reproduced by `sc-j`). `agents create plain` and `mcp create plain` each said `cannot open config file (run 'apogee config init' to create one)`, after the files. Together they left 5 files: `prompts/plain.txt`, `schemas/plain-output.json` and `mcp/plain/`'s three. A broken config takes the same path, as `agent_test.cpp:147-160` pins over `backends: [not, a, mapping]` (by reading).
8. **Long names** (`i-k`).
   - 245- and 300-byte agent names failed at the prompt's temporary file (`File name too long`) and left nothing.
   - A 232-byte name wrote `prompts/<232>.txt` (a 236-byte file name). The schema's temporary name, `<name>-output.json.tmp.<10 digits>`, is 259 bytes, past NAME_MAX's 255, so the schema write threw. That write runs before the `try` (`agent.cpp:139-147`), so no word names the prompt left behind. The config held no entry.
   - A 200-byte name was created. By the same arithmetic (`write_file_atomically`'s `.tmp.` plus up to 10 digits, `config_edit.cpp:3619-3620`), names of 229 to 236 bytes leave a prompt this way.
9. **The root cause, read at HEAD.**
   - `scaffold::create_agent` (`business/scaffold/agent.cpp:79-160`) sanitizes the name (`:81`; `sanitize_agent_name`, `:12-29`). It checks the enums (`:87-104`) and the existing files (`:117-128`), and writes the prompt and the schema (`:139-147`). Only then does it run the config's add, `append_agent` under `edit_config_file` (`:149-158`). The catch is commented "The files were written; say so rather than hiding it" (`:154`), and its words name the prompt only (`:155-157`).
   - `scaffold::create_mcp_server` (`business/scaffold/mcp_server.cpp:249-296`) has the same order: `sanitize_server_name` (`:212-235`), the directory check (`:268-271`), the directory and three files (`:272-280`), then `append_mcp_server` (`:284-294`). Its catch has the same comment (`:289`) and names the directory (`:290-293`). The register-only branch (`:257-260`) writes no files.
   - The certain refusals live in the shared add, `append_entry` (`data/contracts/config_edit.cpp:707-760`): the name rule (`yaml_plain_key`, `:715-725`; `config_yaml.cpp:206-216`), the fold collision (`:731-741`, only when not forced) and the existing entry (`:745-760`, replaced in place when forced).
10. **History.**
    - At f37cf4ad (2026-09-13), the agent scaffold already called `edit_config_file` after its writes (`git show f37cf4ad:lib/src/cli/source/scaffold/agent.cpp`, lines 150 and 156, `the prompt was written to`). `append_entry` already threw `collides with existing` and `already exists; pass --force to replace it` (`git show f37cf4ad:lib/src/cli/source/harness/config_edit.cpp`, lines 369 and 384); both refusals date from 96bb082d (2026-08-25). The MCP scaffold has had the same shape since 2fe0f9b2, the same day.
    - 55f (8d5aec51, 2026-10-25) added only the leading-dash, leading-dot and null class. Its MILESTONES entry (`config-editor-correctness`) puts the rule "in the shared add path, so agents, MCP servers, collections, graphs, suites and review targets are covered too".
11. **The intent on record.**
    - `agent_test.cpp:128-145` pins "names are sanitised or refused, and bad enums are refused before any file is written".
    - `agent_test.cpp:147-160` pins "a config failure leaves the files on disk and says so", for a broken config only. No test pins it for the MCP scaffold.
    - The headers describe it in prose for any config failure: "On a config failure the scaffold files are left on disk and the error says so" (`business/scaffold/agent.h:56-57`; `mcp_server.h:36-37` says the same).
    - `grep` for `prompt was written` or `scaffold was written` in `reference/cli`, MILESTONES and DEVELOPER.md finds no contract. Only `agent_test.cpp:157` pins the words.
12. **The precedents, read at HEAD.**
    - `check_registrations` (`presentation/cli/models_pull.cpp:2814-2856`) refuses a missing config naming `apogee config init` (`:2817-2821`) and a broken one (`:2822-2827`). It then runs `append_backend` on the config's text (`:2846-2856`).
    - `create_symphony` loads the config (`business/scaffold/symphony.cpp:36-41`) and validates the definition (`:42-65`) before its edit.
13. **Blast radius.**
    - **Two cores**: `create_agent`, and `create_mcp_server` in scaffold mode.
    - **Five surfaces**: `agents create`; `mcp create` without `--command` or `--from`; `POST /v1/admin/agents`; `PUT /v1/admin/agents/{id}`, which implies force (`admin_agents.cpp:212-215`) but still meets the name rule; `POST /v1/admin/mcp-servers` without `command` or `from`.
    - **The Mac app**: Library › New agent builds `["agents", "create", name]` (`LibraryViewModels.swift:232`). `.x`, `null` and a case variant reach the scaffold; a dash name is APP-M21's and APP-M23's seam. The app shows a refusal as the last stderr line (`CommandRunning.swift:104-111`). Its MCP creates pass `--from` and write no files (`Library.swift:121`, `LibraryViewModels.swift:685`).
    - **The full-screen shell** has no create action: its agents and MCP rows act only with delete, test, enable and disable (`tui_agents.cpp:127-143`, `:201-233`).
    - **What a refusal leaves**: 2 files for an agent, 3 for an MCP server. No reader shows them, and only `rm` cleans them.
    - **Measured to leave files**: a leading `-`; a `.` or `:` mapped to `-`; the null words; a fold collision; a missing config; a 232-byte agent name (229 to 236 bytes by the arithmetic in Evidence 8); the `--force` retry.
    - **By reading**: a broken config (the unit test), an existing entry whose files are gone, a lock timeout, and [CLI-M44](yaml-editor-refuses-the-layouts-it-does-not-write.md)'s layout refusals once it ships.
    - **Who reads the words**: no client parses the words that change (a grep of `lib/src/darwin/source`, `lib/design/prototype` and `lib/src/mod`). The Mac app's Xcode setting holds the register-only words in a recorded refusal (`XcodeSettingsTests.swift:272-274`, `xcode-mcp-create-taken.err`: `apogee mcp: updating config: MCP server 'xcode' already exists; pass --force to replace it`). That path is not changed here.
    - **Who is hit next**: CLI-59d's command door runs `agents create` and `mcp create` from a paired device, since neither is on its refusal list (by reading its Open questions). Today's leftovers would land on the server's disk.
14. **What already works**, and must stay:
    - An invalid `--tools` or `--output-format` is refused before any file (`agent.cpp:87-104`, `agent_test.cpp:128-145`).
    - A seeded or edited prompt with no config entry is never overwritten without `--force`. `agents create security-review` over a seeded prompt is refused `prompt file already exists` (`agent_test.cpp:178-204`; Milestone X's "two survivors" record).
    - A plain new name creates both files and the entry, and a second create without `--force` is refused `already exists` (`admin_agents_test.cpp:111-113` answers 409).
    - `mcp create --command` and `--from` only register and write no files (`mcp_server.cpp:257-260`). A duplicate registration answers 409 (`admin_config_test.cpp:561`).
    - `config add-backend`, `add-suite` and `add-graph`, and `review targets add`, are config-only, so a refusal writes nothing.
15. **Not measured.** The held-lock path after the write (a `config.json.lock` held past `kConfigLockWait`) was read, not probed; it is a unit case under Testing. Windows was not run: with no `longPathAware` in `source/apogee.manifest`, MAX_PATH (260 for the whole path) may bite before the 200-byte cap, and device names (`CON`, `NUL`) pass both the sanitizer and `yaml_plain_key`.

## Acceptance criteria

- [ ] **Given** a fresh home with the shipped JSONC config and a mock backend, **when** `agents create -- -x` runs, **then** it exits 1 with one stderr line beginning `apogee agents: ` that names the rule. `prompts/-x.txt` and `schemas/-x-output.json` do not exist, and the config is byte-identical.
- [ ] **Given** that refusal, **when** the same command runs again, with or without `--force`, **then** it gives the same refusal, never `prompt file already exists … (use --force to overwrite)`.
- [ ] **Given** `agents create -- .x`, **then** the refusal names `'.x'` and `'-x'`, and nothing is written.
- [ ] **Given** agent `foo`, with its files present on APFS or absent, **when** `agents create FOO` runs, **then** it exits 1 saying `collides with existing 'foo'`, nothing is written, and foo's entry and prompt are unchanged.
- [ ] **Given** an agent `foo` whose files are gone, **when** `agents create foo` runs without `--force`, **then** it exits 1 with `already exists; pass --force to replace it`, and nothing is written.
- [ ] **Given** no config, or a broken config, **when** `agents create plain` or `mcp create plain` runs, **then** it exits 1 (no config naming `apogee config init`), and `prompts/`, `schemas/` and `mcp/plain/` hold nothing new.
- [ ] **Given** `mcp create -- -m`, `-- .z` or `null`, **then** it exits 1 with one stderr line beginning `apogee mcp: `, and `mcp/<n>/` does not exist, run after run, `--force` included.
- [ ] **Given** a loopback serve, **when** `POST /v1/admin/agents {"name":"-x"}` is sent twice, **then** both answer 400 with type `invalid_request_error`, and no file exists. `PUT /v1/admin/agents/-x` answers the same, and `POST /v1/admin/mcp-servers {"name":"-y"}` answers 400 `invalid_request_error` with no `mcp/-y`.
- [ ] **Given** a loopback serve whose `config.json` is then broken by hand, **when** `POST /v1/admin/agents {"name":"plain"}` or `POST /v1/admin/mcp-servers {"name":"plain"}` is sent, **then** it answers 500 `config_error` (`the config cannot be read: …`), and `prompts/`, `schemas/` and `mcp/plain/` hold nothing new. Today the agent POST writes both files and answers 400 with an empty type.
- [ ] **Given** a loopback serve and agent `foo2` with its files removed, **when** `POST /v1/admin/agents {"name":"FOO2"}` is sent, **then** it answers 409 with type `conflict` naming the collision, and no file is written.
- [ ] **Given** a 201-byte new agent or MCP server name, **then** it is refused before any file. A hand-written existing entry with a longer name still lists and runs.
- [ ] **Given** a held config lock (unit), **when** `create_agent` runs, **then** the files are written, and the words name both the prompt and the schema. `create_mcp_server` names its directory and its three files.
- [ ] **Given** a plain new name, `--force` on an existing agent, a seeded prompt with no entry, or `mcp create x --command /bin/echo` (or `--from`), **then** each behaves as today, register-only words byte for byte.

## Constraints

- **One config mutation path** (CLAUDE.md's ⚠ One config mutation path). The dry run is the same `append_agent` or `append_mcp_server` transform run on the text, never a load-modify-save, and only `edit_config_file` writes. The read before the lock answers one read-only question (would this add refuse?), as 57f's sub-rule allows. The locked edit computes from the text it reads under the lock.
- **Refuse by the one edit that will add it** (`check_registrations`'s precedent). The scaffolds don't copy `yaml_plain_key` or the fold check into their sanitizers. Whatever the add refuses, CLI-M44's layout refusals included, is said by the dry run.
- **Every refusal keeps the house voice, one line, the last on stderr**: `apogee agents: ` or `apogee mcp: `, stdout empty (CLI-M46's constraint). The Mac app shows a failure as the stderr's last line (`CommandRunning.swift:104-111`), and APP-M22 keeps exactly those lines.
- **Mode parity** ([ADR 0002](../../../adrs/cli/mode-parity.md)). The CLI verbs and their HTTP twins share the cores, so they refuse with the same words before any write. Over HTTP, a collision or an existing entry or file is 409 `conflict`, as the two routes' paragraphs in http-api.md say, and every other refusal is 400 `invalid_request_error`, the type its error table gives a 400. A config the route cannot read is answered as the rest of the admin plane answers it, 500 `config_error` from `load_now`, before the core runs (Open questions).
- **Older versions keep working** ([ADR 0006](../../../adrs/cli/backwards-compatibility.md)). Existing agents and MCP servers keep loading, listing, running and deleting, names longer than the cap included. The cap is for new names only. The broken-config intent pinned by `agent_test.cpp:147-160` is a behaviour contract, so it changes by a dated revision.
- **The sanitizer's mapping stays**: `.` and `:` become `-`, as shipped (`agent_test.cpp:130`, `mcp_server_test.cpp:99`).
- **A seeded prompt is never clobbered** (Milestone X's record). The existing-file checks stay, after the dry run.
- **Register-only stays as it is.** `mcp create --command` and `--from`, and their HTTP twins, write no files and take no dry run. Their words stay byte for byte, as the Mac app's recorded refusal holds them.
- **Files are kept only after later trouble, and named.** After a lock timeout, a raced edit or a failed second write, the files stay, and the words name every one written.

## Technical approach

Paths under `lib/src/cli/`.

| File | Change |
|---|---|
| `source/business/scaffold/agent.cpp` | After the sanitizer and the enum checks (`:81-104`), and before the file checks (`:113-128`), three steps. First, a 200-byte cap on the name. Second, the config loaded: a missing one refused naming `apogee config init`, as `check_registrations` words it, and a broken one refused with its parse error. Third, `append_agent(text, result.name, entry, spec.force)`, the entry as it will be written (its `prompts` and `schemas` set, which today happens only after the writes, `:142`, `:146`), run on the text of the file the edit will read, its result parsed as `edit_config_file` parses it (`config_edit.h:529`, `:535`) and kept nowhere. When the sanitizer changed the name, the refusal names both forms: `agent name '.x' (as '-x') is not one both config formats can edit -- …`. The writes and the locked edit then run as today, inside one reporting scope: a failure after the first write (a lock timeout, a raced edit, a failed schema write) names every file written, the schema included. |
| `source/business/scaffold/mcp_server.cpp` | The same cap, load and dry run of `append_mcp_server`, before `:268`, in scaffold mode only (`spec.command` empty). After the write, the words name the directory and its three files. The register-only branch (`:257-260`) and its words don't change. |
| `source/business/scaffold/agent.h`, `mcp_server.h` | `create_agent` and `create_mcp_server` take an optional lock wait (default `harness::kConfigLockWait`), passed on to `edit_config_file`, so the held-lock unit case runs in milliseconds. The doc comments (`agent.h:54-57`, `mcp_server.h:34-37`) are revised: a refusal the config is certain to give writes nothing, and a later failure names every file it leaves. |
| `source/presentation/httpserver/admin_agents.cpp` | At `:126-127`, a refusal that is not a collision or an existing entry or file takes the default type, `invalid_request_error`. 409 `conflict` stays for those. `scaffold_agent` reads the config with `load_now` (`:31-40`) before it calls `create_agent`, as the route's GET and DELETE already do (`:166`, `:179`, `:219`), so a config it cannot read answers 500 `config_error` with nothing written. |
| `source/presentation/httpserver/admin_config.cpp` | The same at `:600-601`, for `POST /v1/admin/mcp-servers`, with the same `load_now` read (`:34`) before the core. |
| `tests/business/scaffold/agent_test.cpp`, `mcp_server_test.cpp` | Refused names, a collision, a missing config, a broken config and the cap write nothing. A held `config.json.lock` keeps the files and names them all. `:147-160` is revised with a dated note. |
| `tests/presentation/cli/agents_test.cpp` | CLI refusals write nothing, and a second run gives the same refusal. |
| `tests/presentation/httpserver/admin_agents_test.cpp`, `admin_config_test.cpp` | 400 `invalid_request_error` with no file, twice. `FOO2` beside `foo2` answers 409 with no file. `PUT …/agents/-x` answers 400 with no file. The existing 409 cases (`admin_agents_test.cpp:111-113`, `admin_config_test.cpp:561`) still hold. |
| `tests/scripts/cmake/config_e2e.cmake` | The bug as a fixture, on the built binary (Testing). |

The interfaces this extends are the two scaffold cores' signatures (one defaulted parameter), and their order. Nothing new enters contracts: the dry run calls `harness::load_config` and what `edit_config_file` itself calls (`config_edit_target`, `read_config_file`, `parse_config`), then `harness::append_agent` or `harness::append_mcp_server`, all as they stand, in both config formats through their bridge. The order inside each core becomes: sanitize, check the enums, cap, load, dry run, check the files, write, edit under the lock.

One set of words changes for a plain duplicate: a second `agents create foo` beside a whole `foo` is refused by the dry run as `agent 'foo' already exists; pass --force to replace it`, where today the file check says `prompt file already exists: … (use --force to overwrite)`. `mcp create` likewise says `MCP server 'x' already exists; pass --force to replace it` instead of `MCP server directory already exists`. Both still carry `already exists`, so HTTP keeps its 409, and no test or client pins the old words (a grep of the tests, `reference/cli`, `lib/src/darwin`, `lib/src/mod` and `lib/design/prototype`).

It consumes decisions from three items:
- **[CLI-M46](a-collection-the-config-cannot-hold-is-refused-before-it-is-written.md)**: the 200-byte cap, and the dry-run-first shape.
- **[CLI-M44](yaml-editor-refuses-the-layouts-it-does-not-write.md)**: its layout refusals come out of `append_agent` and `append_mcp_server`, so they reach the person through this dry run before any file.
- **[CLI-M45](a-collection-is-named-not-pathed-at-every-reader.md) and CLI-M46**: their collection refusals for `agents create --collection` and the POST's `collection` sit in `agents_cmd.cpp` and `admin_agents.cpp`, ahead of `create_agent`, so they write nothing as placed. Whichever of them lands after this item keeps them there, or inside the dry run, never after the first write.

**Reference.**
- `check_registrations` (`models_pull.cpp:2814-2856`): refuse a missing or broken config, then run the one add on the text and keep it nowhere, before the first byte.
- `create_symphony` (`scaffold/symphony.cpp:36-65`): the scaffold that already validates before it writes.
- `config_edit_test.cpp:2412-2438`: the held-lock pattern (`lock_config_file`, a 150 ms wait) for the post-write unit case.
- `agent_test.cpp:178-204`: the seeded-prompt guard this order keeps.

**Docs when it ships:**
- DEVELOPER.md's `scaffold/` row (`:235`): both cores refuse before the first byte, and name every file a later failure leaves.
- `reference/cli/http-api.md`'s [POST /v1/admin/mcp-servers](../../../reference/cli/http-api.md#post-v1adminmcp-servers), [POST /v1/admin/agents](../../../reference/cli/http-api.md#post-v1adminagents) and [PUT /v1/admin/agents/{id}](../../../reference/cli/http-api.md#put-v1adminagentsid) paragraphs: 409 for a collision or an existing entry or file, said before anything is written; 400 `invalid_request_error` for the rest.
- MILESTONES: a dated entry that closes the scaffold half of 55f's add rule, and revises the broken-config intent.

## Testing

Every case runs in a throwaway home on mock backends. No model is called.

- **The bug as a fixture**, in `config_e2e.cmake`, on the built binary with the shipped JSONC config and a mock backend. Each case fails on today's binary unless marked as a guard.
  - For `-- -x`, `-- .x`, `-- :y`, `null`, a 201-byte name, and `FOO` after `foo`: `agents create` and `mcp create` exit 1. The last stderr line begins `apogee agents: ` or `apogee mcp: ` and names the rule or the collision. `prompts/<n>.txt`, `schemas/<n>-output.json` and `mcp/<n>/` are absent, and `config.json`'s bytes are unchanged.
  - Each command run twice gives the same refusal both times.
  - A config-less home, and a broken config: refused, with nothing written.
- **HTTP**, in `admin_agents_test.cpp` and `admin_config_test.cpp`:
  - `POST /v1/admin/agents {"name":"-x"}` twice: 400 with type `invalid_request_error` both times, and no file. `PUT /v1/admin/agents/-x` the same.
  - `POST {"name":"FOO2"}` beside `foo2`, its files removed: 409 `conflict` and no file.
  - `POST /v1/admin/mcp-servers {"name":"-y"}`: 400 `invalid_request_error`, and no `mcp/-y`.
  - A config broken after `serve` started: each POST answers 500 `config_error`, and no file is written.
- **Unit**, in `agent_test.cpp` and `mcp_server_test.cpp`:
  - refused names, a collision, a missing config, a broken config (the revised `:147-160`) and the cap (201 bytes refused, 200 created) write nothing;
  - a held `config.json.lock` (taken with `lock_config_file`, the core given a 150 ms wait) still writes the files, and the words name the prompt and the schema, or the MCP directory and its three files.
- **Guards**, passing today and after:
  - a plain name creates both files and the entry;
  - `--force` on an existing agent replaces it in place;
  - `mcp create x --command /bin/echo` registers, writes no files, and refuses a duplicate with today's words;
  - `agent_test.cpp:178-204`, the seeded prompt not clobbered;
  - a hand-written entry longer than 200 bytes still lists and runs.
- **The house voice**: each refusal is one stderr line, the last, beginning `apogee agents: ` or `apogee mcp: `, with stdout empty.
- **By hand**, on the dev channel: `apogee-dev` over `~/.apogee-dev`, never the release install.
- **Gate:** `lib/scripts/cicd.sh --app cli --test`, clang-format, and the MinGW scan of the changed files.

## Open questions

| Question | Status | Answer |
|---|---|---|
| Should a missing or broken config now write nothing, reversing `agent_test.cpp:147-160`'s "a config failure leaves the files on disk and says so"? | `[default]` | Yes, as `check_registrations` does (`models_pull.cpp:2817-2827`). Only trouble after the dry run keeps files, and then the words name each one. Recorded as a dated revision, with the two headers' doc comments revised. CLI-M46 is not the precedent here: it keeps a config-less ingest going, because a collection's registration is recorded as never fatal, and no such record covers the scaffolds (Evidence 11). |
| Over HTTP, what does a missing or broken config answer? | `[default]` | 500 `config_error`, `the config cannot be read: …`, as `load_now` answers every admin read and write for a config it cannot read (`admin_agents.cpp:31-40`, `admin_config.cpp:34`). The scaffold routes call it before the core, so the answer comes before any write. Today such a POST reaches `create_agent`, writes the files, then answers 400 with an empty type. On the CLI the core's own dry run refuses it (exit 1). Every refusal of a readable config stays 400 `invalid_request_error` or 409 `conflict`. |
| What length cap for new agent and MCP server names? | `[default]` | 200 bytes, as CLI-M46 chose: `<name>-output.json.tmp.<10 digits>` then fits NAME_MAX's 255. Windows MAX_PATH is not measured (there is no `longPathAware` in `source/apogee.manifest`), as in CLI-M46. |
| Keep mapping `.` and `:` to `-` (`agents create a.b` makes `a-b`)? | `[default]` | Keep it, since it shipped. A refusal names both the typed name and the mapped one. |
| After the fix, should `agents create --force FOO` next to `foo` still replace foo? | `[default]` | Yes. By the fold rule they are one agent, and `--force` means replace. What changes is that without `--force` the person is told `collides with existing 'foo'` first, on every disk. On a case-sensitive disk, foo's old files stay beside FOO's new ones (by reading). |
| Should the dry run also refuse a collection the read or create rule refuses (`agents create --collection`, the POST's `collection`)? | `(consumed decision)` | Owned by [CLI-M45](a-collection-is-named-not-pathed-at-every-reader.md) (the read rule at add) and [CLI-M46](a-collection-the-config-cannot-hold-is-refused-before-it-is-written.md) (the create rule at add). This item only requires that their refusal lands before the first write, wherever they place it. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-26 | **The diagnosis.** Specced from the APP-M21 and APP-M22 probes' CLI asides, a sandboxed investigation, two skeptics and a critic (Evidence). The seam is each scaffold's order: the refusals the config is certain to give come after the files. The history is corrected from the investigation's: the fold-collision and existing-entry refusals have come after the files since the scaffolds were born (2026-09-13), and 55f added only the name class. | the spec |
| 2026-11-26 | **Placed in Maintenance as M52**, by the user's call to spec the APP-M21/M22 CLI asides as maintenance items from M48, grouped only where they share one seam. This one is its own item because its seam is the two scaffold cores in `business/scaffold`. That is not the config editor's symphony branch ([CLI-M53](a-symphony-the-cli-writes-is-one-it-can-delete.md)), the folded lookups ([CLI-M54](rename-and-get-find-an-entry-by-its-folded-name.md)), the dataset store ([CLI-M55](a-dataset-is-named-not-pathed-at-every-reader.md), [CLI-M56](a-new-dataset-name-is-one-every-verb-reads.md)) or the chat logger ([CLI-M57](a-saved-chat-names-only-its-own-files.md)). Nor is it CLI-M46's collection chains, which share the pattern but not the code. | the user · the spec |
| 2026-11-26 | **The dry run comes before the existing-file checks** (Evidence 4). On APFS the file check spoke first, and its `--force` remedy replaced the other agent. The existing-file checks stay after it, so a seeded prompt with no entry is still never overwritten. | the spec |
| 2026-11-26 | **No gate.** It relates to CLI-M44, CLI-M45 and CLI-M46. Best built before or with CLI-M44, so that M44's layout refusals leave no scaffold. Whichever of CLI-M45, CLI-M46 and this item lands later keeps the collection refusal before the first write. | the spec |
| 2026-11-26 | **A deliberate change of recorded intent.** A missing or broken config now writes nothing, reversing `agent_test.cpp:147-160` ("a config failure leaves the files on disk and says so") and the two headers' doc comments. The test is revised with a dated note, the comments are rewritten, and MILESTONES records the revision. | default |
| 2026-11-26 | **The HTTP error type is corrected, not changed.** The explicit empty type becomes the default `invalid_request_error` that http-api.md's error table documents for a 400. | the spec |

## Out of scope

- **Leftovers from earlier versions.** No verb or check names them, and `rm` removes them. After this item a leftover no longer causes the retry loop, since the dry run speaks first.
- **The Mac app passing a dash name without `--`**: [APP-M21](../../../assistant/darwin/MILESTONES.md#2026-11-26--listed-name-reaches-the-verb-maintenance-item-m21-a-listed-name-reaches-the-verb-as-listed) for listed names, and [APP-M23](../../darwin/macos/maintenance/every-name-the-app-passes-is-read-as-a-name.md) for the create forms, New agent included.
- **Remedy lines that print a dash name without `--`**: [CLI-M48](a-suggested-command-runs-as-printed.md).
- **The symphony routes' empty error type** (`admin_symphonies.cpp:154-158`): [CLI-M53](a-symphony-the-cli-writes-is-one-it-can-delete.md).
- **The HTTP agent GET's exact-case match** (`admin_agents.cpp:183-184`), in the same file as this item's change: [CLI-M54](rename-and-get-find-an-entry-by-its-folded-name.md).
- **The full-screen shell's agents and MCP row actions** passing a listed name without `--` (`tui_agents.cpp:143`, `:227`, `:233`): [CLI-M51](the-tuis-row-actions-reach-a-dash-led-row.md).
- **Windows device names** (`CON`, `NUL`) as agent or MCP names, and Windows MAX_PATH: not measured; an aside.
- **Adjacent oddities the same probes found**, left as asides:
  - `GET /v1/admin/mcp-servers/FOO` echoes the typed `"name":"FOO"`;
  - `agents edit` runs `vi` when `EDITOR` is unset and stdin is not a terminal, then fails (`agents_cmd.cpp:249-250`).

## Links

- **Blocked by:** — · **Blocks:** —
- **Relates to:**
  - [CLI-M46](a-collection-the-config-cannot-hold-is-refused-before-it-is-written.md), the same dry-run-first cure for collections, whose collection refusal at `agents create` must land before this item's first write, and whose 200-byte cap this item takes;
  - [CLI-M45](a-collection-is-named-not-pathed-at-every-reader.md), whose read rule joins `agents create --collection` and the POST's `collection`, ahead of `create_agent`;
  - [CLI-M44](yaml-editor-refuses-the-layouts-it-does-not-write.md), whose layout refusals come out of `append_agent` and leave a scaffold until this item ships;
  - [CLI-M42](stubs-read-the-words-as-the-shell-does.md): independent;
  - the siblings from the same probes: [CLI-M48](a-suggested-command-runs-as-printed.md), [CLI-M51](the-tuis-row-actions-reach-a-dash-led-row.md), [CLI-M53](a-symphony-the-cli-writes-is-one-it-can-delete.md) and [CLI-M54](rename-and-get-find-an-entry-by-its-folded-name.md);
  - [APP-M21](../../../assistant/darwin/MILESTONES.md#2026-11-26--listed-name-reaches-the-verb-maintenance-item-m21-a-listed-name-reaches-the-verb-as-listed), whose probes listed this aside; [APP-M22](../../../assistant/darwin/MILESTONES.md#2026-11-26--jobs-notes-reach-the-person-maintenance-item-m22-a-jobs-notes-reach-the-person), which keeps the house-voice lines; [APP-M23](../../darwin/macos/maintenance/every-name-the-app-passes-is-read-as-a-name.md), the app's create forms;
  - CLI-59d (v0.2.3, [reads and commands over the network](../v0.2.3/reads-and-commands-over-the-network.md)), whose door would run today's `agents create` and `mcp create` on the server's disk.
- **Revises:** the intent pinned by `agent_test.cpp:147-160` and described by `agent.h:56-57` and `mcp_server.h:36-37`. **Follows from:** 55f (8d5aec51), the add rule, whose scaffold half this closes; [ADR 0002](../../../adrs/cli/mode-parity.md) and [ADR 0006](../../../adrs/cli/backwards-compatibility.md); [Milestone W](../../../assistant/cli/MILESTONES.md#milestone-w--the-mcp-client) and [Milestone X](../../../assistant/cli/MILESTONES.md#milestone-x--agents-as-data), the scaffolds' records.
