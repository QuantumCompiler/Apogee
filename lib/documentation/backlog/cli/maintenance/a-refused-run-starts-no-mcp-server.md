# CLI-M38 · A refused run starts no MCP server

| Field | Value |
|---|---|
| **Type** | 🐞 Bug |
| **Project** | CLI |
| **Status** | 🟢 Ready |
| **Fix version** | Maintenance (any time) |
| **Component** | Tool registry assembly (`make_built_in_tools`) › every verb that refuses a grant or deny flag |
| **Labels** | `mcp` `permissions` `tasks` `startup-order` |
| **Epic** | — (maintenance has no track) |
| **Blocked by** | — |
| **Blocks** | — |
| **Reporter** | CLI-M36's probes (2026-11-22), then a sandboxed investigation and a skeptic's re-run the same day |
| **Created** | 2026-11-22 |

---

## Summary

Every verb that refuses a bad `--allow`, `--deny` or `--allow-host` starts the person's configured MCP servers first. It gives each a full handshake, and waits up to 20 s for one that never answers, before saying a word the flags alone decided. The item checks every flag that names no MCP tool against the registry built without dialling, before the dial, so a refused run starts nothing. A grant that names an `mcp__` tool still waits for the servers, the only place that can judge it.

## Description

### User story

**As** a person with MCP servers configured,
**I want** a run I typed wrong to be refused straight away, without starting my servers,
**so that** a typo costs nothing: no program of mine launched in my working folder, no 20-second wait, no prompt in Xcode for a run that was never going to start.

### Steps to reproduce

The sandbox, 2026-11-22, with the built binary `apogee 0.2.0 (33ea685d, 2026-11-22, macos-arm64)`: a throwaway `APOGEE_HOME` and `HOME`, `env -i`, stdin from `/dev/null`, mock backends. The config:
- `permissions: {write_file: deny}`;
- an agent `narrowmcp` (`tools: read-only`, `mcp: [stub]`);
- one MCP server, `stub`: a Python script that writes a line to a launch log the moment it starts, then answers `initialize` and `tools/list`.

1. `apogee task run x --tools --agent narrowmcp --allow edit_file`.
2. `apogee task run x --tools --allow write_file`.
3. The same as step 2, with a `stub` that never answers.

### Actual result

1. `apogee task: --allow edit_file: the agent 'narrowmcp' runs with tools: read-only, which leaves out edit_file -- a task's grant is never wider than its agent's policy` (exit 1). The launch log shows `START … cwd=…/work`, `RECV initialize`, `RECV notifications/initialized`, `RECV tools/list`, `EOF exit`.
2. `apogee task: --allow write_file: the config says permissions.write_file: deny -- a task's grant is never wider than the config` (exit 1), after the same start and handshake.
3. `[mcp] warning: slow: connect timed out after 20s (skipped)`, then the same refusal, real 20.02 s. With two silent servers it took 40.03 s, one after the other.

### Expected result

The same refusals, word for word, with exit 1, and nothing in the launch log. Step 3 answers in the time a refusal takes without servers (0.02 s).

### Background

Each verb builds its tool registry with `make_built_in_tools`, and that call both registers the native toolsets and dials every configured MCP server (`helpers.cpp:252-355`; the dial is `connect_all`, one server after another, 20 s each, `registry.cpp:19-85`, `registry.h:48`). The grant and deny flags are checked against that registry afterwards. For any name that does not start with `mcp__`, the verdict cannot depend on the servers: every MCP tool is named `mcp__<server>__<tool>` (`types.cpp:122-124`, `registry.cpp:96-113`). `make_built_in_tools` with `.mcp = nullptr` already builds the native registry with no dial (`helpers.cpp:293`, `helpers.h:105-108`). The order dates from v0.1.4 (011cec7a, 2ccfe9e1). CLI-M36's probes listed it as its own item.

### Evidence

All measured 2026-11-22 in sandboxes like the one above, by the investigator and again by a skeptic with its own stub and drivers.

1. **`task run` and `task resume`** (one function, `drive`, `task_cmd.cpp:528`). The registry is `pin_toolset(make_built_in_tools({… .mcp = mcp_registry …}), config, session.backend)` at `task_cmd.cpp:785-796`; `compose_task_gate` refuses at `:810-817`. The comment at `:767-769` ("Composed before anything is written") holds for the ledger, not for child processes.
2. **Every `compose_task_gate` refusal dials first.** That covers the config's deny, the agent's policy, an unknown tool, a non-writer (`fetch_url`), the suite's pin (by `--suite` and by `models.default_suite`), a recipe run, an MCP read-only tool, and an `mcp__` name whose server is not configured. Which servers: every configured one with no agent; the agent's `mcp:` list when it has one.
3. **Earlier refusals start nothing.** An unknown `--agent` (`task_cmd.cpp:583-584`), an unknown `--suite` (`:571-576`), a recipe missing its input, an unknown `-m` (`:668`), an exhausted task's resume (`:612`), and `--allow` without `--tools`. An agent with `tools: none` dials nothing (`:780-783`).
4. **chat, execute, acp and tui** (the session opening, `chat_session.cpp`). The dial is at `:1421-1435`, `seed_approvals` refuses after it at `:1594-1609`, and the flag-only format check comes after that at `:1745-1754`. Probes:
   - `chat --tools --allow no_such_tool`, `chat --tools --allow-host https://x.example/y`, `chat --tools --input-format stream-json` and `execute --tools --allow no_such_tool` each started the stub, then refused.
   - `apogee acp --allow no_such_tool` is accepted at launch; each `session/new` then starts the config's server and the host's own, and fails with `-32603` (`acp_cmd.cpp:120-162`).
   - `tui --allow no_such_tool` dials, then refuses on the notice row.
5. **complete.** It dials at `complete.cpp:320-326` (machine mode) and `:500-511` (text mode), after attachments (`:421-457`) and retrieval (`:459-486`); `seeded_approvals` refuses at `:342-343` and `:533`.
   - `complete --tools --allow no_such_tool hi`, `--deny edit_file` under a pin, and `--allow-host https://x.example/y` each started the stub, then refused.
   - Under a pin that leaves out `mcp`, it dials even on a run that succeeds, and `pin_toolset` throws the server's tools away (`complete -m helper --tools hi`, exit 0, the stub started).
6. **The verdict does not depend on the servers for other names.** With `mcp_servers: {}`, every refusal reads word for word the same. The one exception: the two listings of askable tools lose `mcp__stub__probe_write`, in `no tool named … -- the ones that ask before they run:` (`policy.cpp:33-39`) and `not a tool this chat asks about -- the ones it asks about:` (`permissions.cpp:475-482`).
7. **An `mcp__` name needs the dial**: whether it exists comes from `tools/list`, and whether it writes from `readOnlyHint`, or a config deny or ask naming it (`registry.cpp:108-109`). `--allow mcp__stub__probe_write --rounds 1` went ahead; `--allow mcp__stub__probe_read` was refused as never asking.
8. **The cost.**
   - Each refusal starts the person's program in the working folder with a full handshake.
   - The shipped `xcode` preset asks in Xcode on its first connection after each install or update (tools.md).
   - A silent server holds the refusal for 20 s, serially (20.02 s for one, 40.03 s for two).
   - A missing server's command prints `[mcp] warning: gone: connect failed (skipped): …` before the refusal.
   - A fast stub still doubles the refusal's time (0.02 s to 0.04 s, three runs each).
   - On a pipe the dial is silent: `mcp_status_line` sends its non-warning lines to the status line (`helpers.cpp:402-410`), so a script sees a pause, then the refusal.
9. **Order of several bad grants.** Today a task's grants are sorted, then refused in order (`policy.cpp:103-108`), and `seed_approvals` checks allows, then hosts, then denies (`permissions.cpp:514-540`). `task run x --tools --allow mcp__nosrv__x --allow wrte_file` refuses the `mcp__` name today. A check of the native names first would refuse `wrte_file` instead. Each refusal keeps its words; only which of several is reported changes.
10. **A test depends on the order.** `task_test.cpp:620-641`'s [57u] case ("a run refused after a stop reached its setup leaves no request or directory") uses a slow MCP server (`sleep 3`) and `--allow delete_file` under a deny. It relies on the refusal coming after the connection, so a stop lands in that window.
11. **The ACP handover.** `host_mcp_servers` reads the ACP host's 0600 handover file, which holds the servers' env credentials, and deletes it on read (`chat_session.cpp:911-913`, `acp/host_servers.cpp:253-254`). A check moved above that point would leave a refused child with the file unread.
12. **Platforms**: Windows starts no child (`child_process.cpp:286-292`), so there the dial is a failed spawn and its warning line. macOS and Linux start the person's program.
13. **What already works**:
    - every refusal still stands and still leaves no task, ledger, session or model turn; only the lock's empty `tasks/` directory is made, dial or not (`task_cmd.cpp:861-866`);
    - the earlier refusals (Evidence 3) start nothing;
    - `[mcp] connecting:` is said before each dial on a terminal, and a failed server is skipped, never fatal (CLAUDE.md).

## Acceptance criteria

- [ ] **Given** a configured MCP server that records its launch, **when** any of these is refused, **then** it exits 1 with today's words and the server never starts:
  - `task run` (and `task resume`) with a grant the config denies, the agent leaves out, the pin removes, a typo, or a non-writer;
  - `chat`, `execute` or `tui` with a bad `--allow`, `--deny` or `--allow-host`;
  - `complete` with any of those;
  - `chat --tools --input-format stream-json` with no output format.
- [ ] **Given** a server that never answers, **then** each of those refusals takes about as long as with no server configured, not 20 s.
- [ ] **Given** an ACP `session/new` whose child is refused for its flags, **then** neither the config's servers nor the host's start, and the host's handover file has been read and deleted.
- [ ] **Given** `task run … --allow mcp__<server>__<tool>`, **then** the servers are dialled and the grant is judged after the dial, as today: accepted for a writer, refused as never asking for a read-only tool, refused as `no tool named` for an unknown one.
- [ ] **Given** a run that goes ahead, **then** it dials and registers the servers exactly as today.
- [ ] **Given** `task run` or `complete` under a fixed pin that leaves out the `mcp` toolset, **then** no server is started, even for a run that succeeds.
- [ ] **Given** a refusal of a name that is not an `mcp__` name, **then** its listing of askable tools names the built-in tools, then each configured server's tools by their prefix (`mcp__<server>__…`), saying they were not started for a refused run.
- [ ] **Given** `complete --output-format stream-json` with a bad flag, **then** the refusal comes before the `session` event, as `--allow` without `--tools` already does.

## Constraints

- **One checker, run twice.** The task's ceiling (`compose_task_gate`'s 57k loop and `tasks::grant_refusal`) and chat's and complete's `seed_approvals` are the checks. The new early call is the same function over the native registry, never a copy, and every `mcp__` name keeps today's check after the dial.
- **Only names that cannot depend on the servers are decided early.** A name that starts with `mcp__` always waits for the servers. That is the only place that knows whether it exists and writes, and whether 57o's config deny or ask overrides its `readOnlyHint`.
- **A refused run still writes nothing.** No task, ledger or session, as today, and no child process now.
- **The ACP handover is read first.** The early check sits after `chat_session.cpp:911-913`, so a refused child has still taken and deleted the credentials file.
- **Today's words stay**, except the askable-tools listing (above), and except which of several bad flags is reported (Evidence 9), which is stated in the reference.
- **`serve` and `analyze` are unchanged**, since they take no grant flags; `make_built_in_tools` keeps its whole behaviour for them.
- **Machine mode** ([ADR 0002](../../../adrs/cli/mode-parity.md)): a refusal that now comes earlier still ends with the same `error` line on the same exit code.

## Technical approach

Paths under `lib/src/cli/source/presentation/cli/`.

| File | Change |
|---|---|
| `helpers.h`, `helpers.cpp` | `make_built_in_tools` split where its native phase ends and its MCP block begins (`:287`, `:292`). `make_native_tools(options)` holds fetch_url, web_search and the native toolsets, with no child. `connect_mcp_tools(registry, options)` dials, registers and applies the policy. `make_built_in_tools` composes the two. |
| `task_cmd.h`, `task_cmd.cpp` | The ceiling `task_grant_refusal(...)`, which CLI-M36 took out of `compose_task_gate` (shipped 2026-11-27, declared in `task_cmd.h`; the line numbers here predate it). `drive` calls it between `:764` and `:785` over `pin_toolset(native, config, session.backend)`, the agent's policy and the non-`mcp__` grants, so run and resume share it. When the pin leaves out `mcp`, `drive` skips the MCP phase. `compose_task_gate` still runs over the full registry for `mcp__` grants. The comment at `:767-769` says no server starts before a refusal. |
| `permissions.h`, `permissions.cpp` | `preflight_presets(presets, gated_native)`: `seed_approvals` into scratch approvals over every `--allow-host` and the non-`mcp__` `--allow` and `--deny` names, returning `seed_approvals`' own refusal. The askable-tools listing gains the servers' prefixes. |
| `chat_session.cpp` | `preflight_presets` called before `make_built_in_tools` (`:1421`), after the ACP handover read (`:911-913`), when `--tools` is on. The input/output format agreement (`:1745-1754`) moves above `build_providers` (`:1022`). The full seed (`:1606`) stays for `mcp__` names. |
| `complete.cpp` | `preflight_presets` right after `:261-266`, over `pin_toolset(native, config, model)`, before attachments, retrieval and both dials. When the run's pin leaves out `mcp`, no MCP phase. `seeded_approvals` (`:342-343`, `:533`) stays for `mcp__` names. |
| `../../tests/presentation/cli/task_test.cpp` | The refusal table at `:723` gains a server whose command is a missing file and an agent naming it. Every refused row checks no `[mcp]` line, with a control row (`--allow mcp__gone__x`) that still shows it. The [57u] case at `:620-641` is re-keyed to `--allow mcp__slow__x`, refused after the dial as `no tool named`, so it still holds the setup window. |
| `../../tests/presentation/cli/chat_session_test.cpp` (or `permissions_test.cpp`) | complete, chat and execute with a bad `--allow`, `--deny` or `--allow-host`, and chat with `--input-format stream-json`, over the same config: each exits 1 with its words and no `[mcp]` line. The control `complete --tools hi` exits 0 and still dials. |

**Reference.** The `.mcp = nullptr` path, already the native registry with no dial. CLI-M36's `task_grant_refusal`, shipped 2026-11-27, which this item reuses. The 2026-10-31 [57u] work, which placed a run's refusals between the lock and the run.

**Docs when it ships:** `reference/cli/tools.md` and CLAUDE.md's MCP startup rule (a run refused for its flags starts no server, and only an `mcp__` name waits for them); DEVELOPER.md's rows for `helpers`, `task_cmd`, `permissions`, `chat_session` and `complete`; MILESTONES (a dated entry).

## Testing

Hermetic and cross-platform, in process, with no child started. A server whose command is a missing file shows the dial as its `[mcp] warning: … was not found on PATH` line, on every platform. That warning is the fixture: today it comes before each refusal, after the fix it does not.

- **The refusal table** (`task_test.cpp`). Each row expects exit 1, its words, `tasks::list_tasks` empty and no `[mcp]` line:
  - `task run` with `--allow delete_file` (denied), `wrte_file`, `read_file` or `fetch_url`, and `--agent <the gone agent> --allow write_file`;
  - `complete --tools --allow wrte_file hi`, `--deny wrte_file hi` and `--allow-host https://x.example/y hi`;
  - `chat --tools --deny wrte_file`, `execute --tools --allow wrte_file` and `chat --tools --input-format stream-json`.
- **Controls that keep dialling**: `task run … --allow mcp__gone__x` still shows the warning, then `no tool named`; `complete --tools hi` exits 0 with the warning.
- **Rows that already pass**, kept so the order stays honest: `--agent nope`, `--allow` without `--tools`.
- **The pin**: `models.default_suite: pinned` with `toolset: [notes]`, then `complete -m helper --tools hi`, shows no `[mcp]` line.
- **Optional POSIX launch log**, gated on `platform::supports_child_processes()`: a `#!/bin/sh` stub that writes `<home>/launched` and exits. After each refusal, `launched` must not exist.
- **The [57u] case** keeps its window through the re-keyed `mcp__` grant.
- **Gate:** `lib/scripts/cicd.sh --app cli --test`, clang-format, and the MinGW scan of the changed files.

## Open questions

| Question | Status | Answer |
|---|---|---|
| A misspelled tool is now refused before any server starts, so its listing can't name the servers' tools. What does it say? | `[default]` | The built-in tools that ask, as today, then one clause naming each configured server's tools by prefix (`mcp__<server>__…`), not started for a refused run. |
| Should an `mcp__` name that is wrong without asking a server (an unconfigured or disabled server, one the agent doesn't name, a config deny) also be refused before the dial? | `[default]` | No. Every `mcp__` name keeps today's check after the dial, exact and in the same words; such grants are rare, and the smaller change is the safer one. |
| Does `apogee acp` refuse a bad flag at launch, instead of at every `session/new`? | `[default]` | No change at launch. Each session's own refusal now starts no server; the host's servers arrive per session, so an `mcp__` name can only be checked there. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-22 | Specced from CLI-M36's probes and a sandboxed investigation with a skeptic's re-run (Evidence); placed in **Maintenance** as M38. | the spec |
| 2026-11-22 | **The task's check goes just before the dial**, in `drive`, one place for run and resume. The lock's empty `tasks/` directory and the provider objects come earlier and cost little, as today. | the spec |
| 2026-11-22 | **A fixed pin that leaves out `mcp` dials nothing** in `task run` and `complete`. chat keeps dialling, since `/suite` and `/model` can change the pin mid-session. | the spec |
| 2026-11-22 | **When several flags are bad and one is an `mcp__` name, a non-`mcp__` refusal is reported first.** That is the cost of deciding it without the servers, and the reference says so. | the spec |

## Out of scope

- **Completion**, which never dials (CLI-M36).
- **`apogee acp`'s launch-time check of its flags** (above).
- **`serve` and `analyze`**, which take no grant flags.

## Links

- **Blocked by:** — · **Blocks:** —
- **Relates to:** [CLI-M36](../../../assistant/cli/MILESTONES.md#2026-11-27--grant-and-deny-flags-offer-what-they-take-maintenance-item-m36-the-permission-flags-offer-what-their-verbs-take) (shipped 2026-11-27: it took the task's ceiling out of `compose_task_gate` as `task_grant_refusal`, `task_cmd.h`, which this item reuses); 57u (the refusals between the lock and the run); 56b (the ACP host's servers and their handover); ADR 0002 (mode parity).
