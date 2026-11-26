# CLI-M36 · Grant and deny flags offer what they take

| Field | Value |
|---|---|
| **Type** | 🐞 Bug |
| **Project** | CLI |
| **Status** | 🟢 Ready |
| **Fix version** | Maintenance (any time) |
| **Component** | Shell completion (`__complete`) › the permission flags' name kinds |
| **Labels** | `completion` `permissions` `tasks` `adr-0007` |
| **Epic** | — (maintenance has no track) |
| **Blocked by** | — |
| **Blocks** | — |
| **Reporter** | v0.2.1's pre-merge docs pass (2026-11-22): its completion audit, the review of its fix, and two probes of the built binary |
| **Created** | 2026-11-22 |

---

## Summary

Tab completion still offers names the permission flags then refuse, and leaves out names they take. `task run --allow` offers every writing tool whatever the config denies, the agent allows or the suite pins. `complete`'s `--allow` and `--deny` ignore its suite's tool pin. `--deny` offers no website, though a website is half of what it takes, and `config delete-allowed-host` offers an entry it refuses. The item gives each flag a list held to the verb's own refusal, as ADR 0007 requires.

## Description

### User story

**As** a person who grants and refuses tools from the command line (`task run --allow`, `chat --deny`, …),
**I want** Tab to offer exactly the tools and websites the command will take, given what I have already typed and what my config says,
**so that** a name I pick from the list is never refused a second later, and the websites I can refuse are one Tab away.

### Steps to reproduce

From the probes of 2026-11-22: the binary `lib/src/cli/build/macos-arm64/source/apogee` (0.2.0, the v0.2.1 branch at 33ea685d with the docs pass's working tree), in a throwaway `APOGEE_HOME` and `HOME`, stdin from `/dev/null`, `APOGEE_COMPLETION_PROTOCOL=2`. The config:
- two mock backends, `m` (the default) and `helper`;
- `permissions: {write_file: deny}`;
- agents `narrow` (`tools: read-only`), `wide` (`tools: all`) and `mute` (`tools: none`);
- a suite `pinned` whose chat member is `helper` with `toolset: [notes]`;
- `tools.allowed_hosts: [docs.python.org, Example.COM., https://not-a-host.example/path, my_host]`.

1. `apogee __complete task run --tools --agent narrow --allow ""`.
2. `apogee task run "Write the report" --tools --agent narrow --allow edit_file`.
3. `apogee task run "Write the report" --tools --allow write_file`.
4. `apogee __complete chat --tools --deny ""`, then `apogee __complete chat --tools --deny docs`.
5. With `models.default_suite: pinned`: `apogee __complete complete -m helper --tools --deny ""`, then `apogee complete -m helper --tools --deny edit_file hi`.
6. `apogee __complete config delete-allowed-host ""`, then `apogee config delete-allowed-host https://not-a-host.example/path`.

### Actual result

1. `:values delete_file delete_note edit_file run_command write_file write_note`. It is the same six for `--agent mute`, `--agent wide`, `--suite pinned`, `-m helper --suite pinned`, `--recipe digest-folder`, and for no flags at all.
2. `apogee task: --allow edit_file: the agent 'narrow' runs with tools: read-only, which leaves out edit_file -- a task's grant is never wider than its agent's policy` (exit 1).
3. `apogee task: --allow write_file: the config says permissions.write_file: deny -- a task's grant is never wider than the config` (exit 1).
4. The six tools and no website. A typed `docs` gets `:hint --deny OFFERED_TOOL: Refuse a tool or website for this chat without asking (repeatable)`.
5. The six tools are offered, then `apogee complete: --deny: 'edit_file' is not a tool this chat asks about -- the ones it asks about: delete_note, write_note; or name a website, like docs.python.org` (exit 1).
6. `:values docs.python.org Example.COM. https://not-a-host.example/path my_host` is offered, then `apogee config: 'https://not-a-host.example/path' is not a host name: write the host alone, e.g. docs.python.org` (exit 1).

### Expected result

1. Nothing is offered under `narrow` (with a hint saying why). Under `wide` the config-allowed writers are offered (not `write_file`); under `--suite pinned` only `delete_note` and `write_note`.
2–3. Unchanged: the refusals stand, and Tab no longer offers those names.
4. The six tools plus `docs.python.org` and `example.com`, each a host the gate allows without asking. Any other host is typed, under a hint that names a tool or a website.
5. Under the pin, `delete_note` and `write_note` only.
6. `docs.python.org`, `example.com` and `my_host`, each once and canonical. Once [CLI-M40](delete-allowed-host-removes-what-the-list-holds.md) lets the verb remove an entry as written, the URL too, as written.

### Background

v0.2.1's pre-merge docs pass (2026-11-22) audited the branch's completion surface against ADR 0007. It found `train pipeline resume` offering runs it refuses, and `--allow`/`--deny` offering `run_command` where no program can start. It fixed both in the branch with two kinds, `RESUMABLE_PIPELINE_RUN` and `OFFERED_TOOL`; the review of that fix added `tools.disabled` and the snapshot check ([MILESTONES.md](../../../assistant/cli/MILESTONES.md#2026-11-22--the-completion-audit-at-v021s-docs-pass-resume-and-the-tool-grants-offer-what-they-take), the entry "the completion audit at v0.2.1's docs pass"). Two gaps were left, because each needs a design of its own:
- **a task's grant**, which refuses more than chat's `--allow` does;
- **`--deny`'s websites**, a gap older than the branch.

Two probes then measured both (Evidence). They found the suite pin's gap on `complete`'s flags, and the `delete-allowed-host` offer, the same contract one step over. The user placed the item in Maintenance (2026-11-22).

What each flag takes, as the verbs decide it:
- **`task run --allow`:** a writer the task's own registry offers, inside the agent's policy and not denied by the config (`tasks::grant_refusal` through `refuse_one`).
- **Chat-style `--allow`** (chat, execute, acp, tui, complete, the in-shell sessions): a writer the session's registry offers. On `complete` the suite's pin narrows it, since its pin is fixed for its one run. The session verbs check the unpinned registry, since `/suite` and `/model` can change the pin mid-session. A config deny narrows none of them, since the session still asks.
- **`--deny`:** the same tools, or any bare canonical host that is not tool-shaped (`name_gated` with `websites=true`).
- **`config delete-allowed-host`:** an entry whose canonical form `checked_host` accepts (today), or any entry by how it is written, once [CLI-M40](delete-allowed-host-removes-what-the-list-holds.md) ships.

### Evidence

All measured 2026-11-22, by reading the working tree and by the probes above (each refused run stopped before any task, session, ledger or model turn: `task list` stayed `no tasks yet`, and `home/tasks/` stayed empty).

1. **The task grant's refusals**, in the order the verb applies them (`task_cmd.cpp:1544-1575`, `compose_task_gate`; `business/tasks/policy.cpp:33-61`, `refuse_one`):
   - a shell or git tool where no program can be started (57k, `toolsets.cpp:164-176`), which `OFFERED_TOOL` already drops;
   - a tool not in the task's registry: `no tool named '<t>' in this task -- the ones that ask before they run: …` (`policy.cpp:33-39`). That registry is `pin_toolset(make_built_in_tools(…), config, session.backend)` (`task_cmd.cpp:785-796`, `helpers.cpp:125-130`), so `tools.disabled` and the suite's pin both land here;
   - a tool that does not write (`policy.cpp:40-49`), which no kind offers;
   - the agent's policy: `the agent '<a>' runs with tools: <policy>, which leaves out <t> -- a task's grant is never wider than its agent's policy` (`policy.cpp:50-56`). An agent has a policy word (`read-only`, `all`, `none`) and MCP server names, never a tool list (`config.h:766-772`), and every bundled agent is `read-only` (`assets.cpp:53-55`);
   - the config's deny: `the config says permissions.<t>: deny -- a task's grant is never wider than the config` (`policy.cpp:57-61`, a map lookup in `PermissionsConfig::level`, `config.cpp:1695-1698`).
2. **The probe**: completion offered all six writers for `task run --allow` under every agent, suite and config. The verb refused `write_file` under the deny (with no flags, with `--agent wide`, and with `--recipe digest-folder`). It refused every writer under `narrow` and `mute`, and `edit_file` and `run_command` under the pin (with `--suite pinned`, with `-m helper --suite pinned`, and under `models.default_suite: pinned` with no flags). `--suite off` lifted the pin.
3. **The task's backend** (which member's pin applies) is `-m` when given. Otherwise it is the chosen suite's chat member, or the resolve chain over it, the chosen suite being `--suite` or else `models.default_suite` (`task_cmd.cpp:702-715`, `730-735`, `752-763`).
4. **Chat-style flags take a config-denied writer**: `apogee complete --tools --allow write_file hi` and `--deny write_file hi` both answered `mock response`, exit 0. `name_gated` checks the session's gated set and never reads `permissions:` (`permissions.cpp:419-428`, `449-455`). So `OFFERED_TOOL` must not drop a config deny.
5. **The pin reaches chat-style flags too**: with `models.default_suite: pinned`, `complete -m helper --tools --allow edit_file hi` and `--deny edit_file hi` were refused as `not a tool this chat asks about`, while Tab offered all six. The pin is applied to `complete`'s registry (`helpers.cpp:125-130`, `complete.cpp:524`). The session verbs differ, measured the same day by CLI-M38's investigation: `chat --suite pinned -m helper --tools --deny edit_file` exits 0, because chat checks its flags against the unpinned registry (`chat_session.cpp:1594-1595`, `gated_tools(registry)`).
6. **What completion can see.** The protocol gets the words before the cursor and the word being typed (`complete_protocol.h:32-38`, `complete_protocol.cpp:660-672`). `context.flags` holds the command's own flags before the cursor: keyed by the spelling used (`-m` and `--model` apart), the last value winning, `--x=v` split, cleared on a descent into a subcommand (`complete_protocol.cpp:546-576`). The config is loaded once per keystroke and silently (`complete_protocol.cpp:683-689`). `PRECISION` already reads an earlier flag and records that a later one is invisible (`complete_sources.cpp:749-757`).
7. **The cost**: the agent (`resolve_agent`, the config's map and the compiled-in table, `assets.cpp:60-73`), the suite (`select_suite`, `helpers.cpp:96-108`), the backend (`roles.cpp:100-130`) and the pin (`config.cpp:1506-1536`, matched by the member's name, so no backend is built) are all reads of the loaded config. `register_native_toolsets` is already paid by `OFFERED_TOOL`. `apogee __complete task run --tools --agent narrow --suite pinned -m helper --allow ""` took 0.01–0.02 s real, three runs (`/usr/bin/time -p`).
8. **What `--deny` takes as a website** (`permissions.cpp:441-485`, `host.h:19-31`, `host.cpp:32-78`): a name the gated set lacks, "website-shaped" (a dot, brackets or a `:`, or one label with no `_`), that `canonical_host` accepts. That is a bare DNS name, IPv4 or IPv6, lowercased, one trailing dot dropped; the stored key is the canonical host. A URL, a wildcard, `host:port`, a space, a mistyped tool name and `fetch_url` were each refused in the probe, with `or name a website, like docs.python.org`.
9. **Where a denied website bites**: the session's denied hosts are read before `tools.allowed_hosts` and the search instance's host (`permissions.cpp:235-241`, `264-281`; `permissions_test.cpp:1471-1483`). So `--deny` is the one way to refuse, for a session, a host the config lets through without asking. The config holds no denied-hosts list (`config.h:1121-1138`).
10. **The raw allowed-hosts list is not a valid `--deny` list**: `https://not-a-host.example/path` (which `check` warns "allows nothing") and `my_host` (dotless with `_`, which `check` reports `ok`) were both refused by `--deny`. `Example.COM.` would be offered as written, not as `example.com`. A listed `my_host` is refused only by its own spelling; `my_host.` (dotted) denies it today, and [CLI-M39](deny-takes-every-host-a-yes-takes.md) makes every listed host deniable whatever its shape.
11. **`--allow` takes tools only, on purpose** (26o, recorded for veto, never vetoed; `permissions.cpp:516-523`): its refusal points to `--allow-host`, which is free text (`TEXT`) and stays so. In-chat `/allow` does take a website (`chat_session.cpp:2482-2499`, `websites` defaulting to true). In-chat `/allow` and `/deny` complete the gated tools alone; `/revoke` completes the session's tools and hosts (`chat_completer.cpp:112-120`, `chat_session.cpp:2175-2183`).
12. **`ALLOWED_HOST` offers what `delete-allowed-host` refuses** (`complete_sources.cpp:793-795`; `config_edit.cpp:2380-2386`, `checked_host`): `:values docs.python.org Example.COM. https://not-a-host.example/path my_host`, then the URL refused. The entry can't be removed through the verb either: [CLI-M40](delete-allowed-host-removes-what-the-list-holds.md). `my_host` is accepted by `checked_host`, and the verb removes it (`removed my_host`, CLI-M40's probe).
13. **Every `--deny` is one declaration away**: `bind_session_flags` (`chat_session.cpp:811-816`) serves chat, execute, acp, tui and the in-shell sessions, `complete.cpp:679-684` serves `complete`, and the shell's typed lines complete through the same `complete_words` (`tui_runner.cpp:95-116`). The tests pin each flag's kind (`complete_sources_test.cpp`, the declarations case), and the help goldens show it (`chat_session.golden`, `execute_session.golden`).
14. **What already works**:
    - `OFFERED_TOOL` drops a shell or git tool where no program can start, and every tool of a toolset `tools.disabled` switches off. The probe with `tools.disabled: [fs]` gave `delete_note run_command write_note`.
    - Every over-wide grant is refused with words naming the rule, before anything is written.
    - The neighbouring flags complete (`--agent` agents, `--suite` suites and `off`, `-m` backends, `--recipe` recipes).
    - A typed host that matches no tool falls back to the hint, so typing a website is never blocked; only the hint's type word is wrong.

## Acceptance criteria

- [ ] **Given** `permissions.write_file: deny`, **when** `task run --tools --allow <TAB>` is completed, **then** `write_file` is not offered, while chat-style `--allow` and `--deny` and `config set-permission` still offer it, and `complete --tools --allow write_file hi` still runs.
- [ ] **Given** `--agent <a>` before the cursor, **when** `task run … --allow <TAB>` is completed, **then** only the writers that agent's policy keeps are offered: none under `read-only` or `none`, with a hint naming the agent and its policy, and the config-allowed writers under `all`.
- [ ] **Given** `--recipe <r>` naming an agent before the cursor and no `--agent`, **then** the recipe's agent filters the offer; a typed `--agent` wins over it.
- [ ] **Given** a suite that pins its chat member's toolset, **when** the backend in play is that member (by `-m`/`--model`, by `--suite`/`--suite=`, or by `models.default_suite` with no flags), **then** `task run --allow` and `complete`'s `--allow` and `--deny` offer only the pinned toolsets' writers. `-m` naming a backend outside the suite, or `--suite off`, lifts the pin. The session verbs (chat, execute, acp, tui and the in-shell sessions) keep the unpinned list, as they take it.
- [ ] **Given** any line, **then** every candidate of each kind is accepted by its verb, and every writer or listed host the kind leaves out (and the line does not lift later) is refused by it. Each refused one is run in the test as the verb, which exits 1 with no task made.
- [ ] **Given** `tools.allowed_hosts: [docs.python.org, Example.COM., https://not-a-host.example/path, my_host]` and `tools.search` at `http://127.0.0.1:8888`, **when** `--deny <TAB>` is completed on any session verb, **then** the offered tools are listed with each listed host `name_gated` takes as a website, once and canonical: today `docs.python.org`, `example.com` and `127.0.0.1`, and `my_host` too once [CLI-M39](deny-takes-every-host-a-yes-takes.md) ships. Never the URL, nor `Example.COM.` as written.
- [ ] **Given** a typed host no list names (`wiki.internal`), **then** `--deny` gives a hint whose type names a tool or a website, never `OFFERED_TOOL`; `--allow` still offers tools only, and `--allow-host` stays a `TEXT` hint.
- [ ] **Given** a chat session, **when** `/deny <TAB>` is completed, **then** the gated tools, the listed hosts and the hosts this session allowed are offered; `/allow <TAB>` offers the gated tools and the hosts this session denied; `/revoke` is unchanged. Each offer is held to `name_gated`.
- [ ] **Given** the same config, **when** `config delete-allowed-host <TAB>` is completed, **then** what the verb removes is offered, by the verb's own match: each accepted entry canonical once (`docs.python.org`, `example.com`, `my_host`), and, once [CLI-M40](delete-allowed-host-removes-what-the-list-holds.md) ships, each malformed entry as written. Each offered one is removed by the verb.
- [ ] **Given** an unknown `--agent` or `--suite`, or a config that does not load, **then** completion offers the list without that filter (or nothing) and never prints an error.
- [ ] **Given** `context.child_processes = false`, **then** no grant or deny kind offers `run_command`, as today.
- [ ] `--help` shows each flag's kind, and the two session goldens change in exactly their `--allow`/`--deny` lines; `__complete`'s output changes only in its candidates and hints.

## Constraints

- **The offer is a contract, both ways** ([ADR 0007](../../../adrs/cli/tab-completion.md)). Nothing offered is refused, and nothing the verb takes from a closed set is left out. Hosts are an open set, so only those the gate already lists are offered; the rest are typed under the hint.
- **One resolver, shared with behaviour.** Every filter is the verb's own predicate, called from both places, never a copy:
  - the task grant: the ceiling `compose_task_gate` applies, taken out into one function `drive` and the source share (`tasks::grant_refusal` over the registry `pin_toolset` and `apply_tool_policy` give);
  - `name_gated` for every `--deny` candidate;
  - one `listed_hosts(config)`, taken out of `make_permission_checker`, for the hosts the gate lists;
  - for `delete-allowed-host`, the verb's own match: `checked_host` today, [CLI-M40](delete-allowed-host-removes-what-the-list-holds.md)'s `allowed_host_matches` once it ships.
- **The per-keystroke budget is absolute.** The filters read the loaded config, the compiled-in agent table and, for `--recipe`, the recipe book's directory record (the read `RECIPE` already makes). They never build a backend, load a model, dial an MCP server, start a process or touch the network. MCP tools therefore stay unoffered, though a task may be granted one.
- **The line is read up to the cursor**, as `PRECISION` reads `--mlx`. A flag typed after the cursor is not seen, so the offer may be narrower than the line finally allows (an under-offer), never wider than what is already typed allows. On PowerShell this holds once [CLI-M37](powershell-completes-the-word-at-the-cursor.md) ships; its stub sends the whole line today.
- **Completion never errors.** An unknown agent or suite, a missing member or a config that does not load drops that filter silently (`complete_protocol.cpp:683-689`).
- **A config deny is the task's alone.** Chat-style `--allow` and `--deny` take a config-denied writer (Evidence 4), so `OFFERED_TOOL` keeps it; `config set-permission` keeps `TOOL`, and `--allow-host` keeps `TEXT`.
- **`--allow` stays tools-only** (26o); this item adds no website to it.
- **`__complete` grows additively** ([ADR 0006](../../../adrs/cli/backwards-compatibility.md)): new kind names, the same directives.
- **The 57k seam stays**: `CompletionContext::child_processes`, so a Mac test sees the Windows side through every kind.
- **Code style**: the layers' include rules (the source may include `cli/task_cmd.h` and `cli/permissions.h`, as it does `task_cmd.h` today), clang-format from `/opt/homebrew/opt/llvm/bin`, and no new warnings on the CI compilers (the MinGW scan of the changed files).

## Technical approach

Paths under `lib/src/cli/`.

| File | Change |
|---|---|
| `source/presentation/cli/complete_protocol.h` | `CompletionContext` gains the command path the walk descended (`complete`, `task run`, …), so a kind can tell `complete`'s fixed pin from a session verb's. |
| `source/presentation/cli/command.h` | Two kinds beside `kOfferedToolValue`, in `kNameValues` (53 → 55), each with its doc comment: `kTaskGrantToolValue` (a writer a task's grant takes, by the task's own ceiling) and `kToolOrWebsiteValue` (what `--deny` takes from a closed set: the offered tools and the hosts the gate lists). Narrow `OFFERED_TOOL`'s comment to chat-style `--allow`, and the pin on `complete`. |
| `source/presentation/cli/task_cmd.h`, `task_cmd.cpp` | Take the per-grant ceiling out of `compose_task_gate` (`:1544-1575`: the 57k loop and `tasks::grant_refusal`) into a declared `task_grant_refusal(...)`. Take the backend choice `drive` makes (`:702-715`, `:752-763`) into a pure `task_chat_backend(config, model, suite)`. `drive` calls both, its refusals unchanged. `run`'s `--allow` declares `kTaskGrantToolValue` (`:1268`). |
| `source/presentation/cli/permissions.h`, `permissions.cpp` | `listed_hosts(config)`: `tools.allowed_hosts` and the search instance's host, now built inline in `make_permission_checker` (`:232-241`), which calls it. |
| `source/presentation/cli/complete_sources.h`, `complete_sources.cpp` | Four changes. (1) On `complete`, `OFFERED_TOOL` and `TOOL_OR_WEBSITE` gain the pin: the backend in play from `-m`/`--model`, `--suite`/`--suite=` and `models.default_suite`, and `pin_toolset`'s filter over the registry they already build. On the session verbs they stay unpinned. (2) A `TASK_GRANT_TOOL` branch: the agent (`--agent`, else the `--recipe`'s, else the default) and the task's backend through `task_chat_backend`, the registry pinned and policy-filtered, each writer kept when `task_grant_refusal` is empty, and a `none` naming why nothing is left. (3) A `TOOL_OR_WEBSITE` branch: `OFFERED_TOOL`'s list, then each `listed_hosts` entry that `name_gated(entry, offered, true, child_processes)` takes as a host, offered as its canonical key, once. (4) `ALLOWED_HOST` offers what the verb removes, by its own match: each accepted entry canonical once, and each malformed entry as written once CLI-M40 ships. |
| `source/presentation/cli/chat_session.cpp`, `complete.cpp` | `--deny` declares `kToolOrWebsiteValue` (`chat_session.cpp:813`, `complete.cpp:681`); `--allow` stays `kOfferedToolValue`. The in-chat sources (`:2175-2183`) gain the listed hosts and the session's allowed and denied hosts. |
| `source/presentation/cli/chat_completer.h`, `chat_completer.cpp` | `/deny` and `/allow` (`:112-120`) take argument sources that add hosts: the listed hosts and the session's allowed ones for `/deny`, the session's denied ones for `/allow`, each through `name_gated`. |
| `tests/presentation/cli/complete_sources_test.cpp` | The cases in Testing; the declarations case expects `TASK_GRANT_TOOL` on `task run --allow` and `TOOL_OR_WEBSITE` on every `--deny`. |
| `tests/presentation/cli/chat_completer_test.cpp`, `tests/presentation/cli/permissions_test.cpp` | The in-chat offers; `listed_hosts` as the checker's one list. |
| `tests/fixtures/cli/chat_session.golden`, `execute_session.golden` | The `--deny` help line (`--deny TOOL_OR_WEBSITE`). |

How the task's offer is computed, in order, each step skipped silently when its input is missing or refused:
1. The probe config is the loaded one, with `select_suite` applied when `--suite` is on the line.
2. The backend is `task_chat_backend(probe, -m or --model, suite)`.
3. The agent is `resolve_agent(probe, --agent)`, else the recipe's agent, else the default agent.
4. The registry is the native toolsets under `{context.child_processes, probe.tools.disabled}`, pinned by `pin_toolset(…, backend)` and filtered by `apply_tool_policy(…, agent.tools)`.
5. A writer is kept when `task_grant_refusal` is empty.

**Reference.** The 2026-10-06 completion audit (MILESTONES, "every offer something the verb takes"): `HALTABLE_TASK`, `RESUMABLE_TASK`, `SOURCED_GRAPH` and `PRECISION`, each filtering with its verb's own refusal; `PRECISION` reads an earlier flag. The 2026-11-22 audit's `OFFERED_TOOL` and `RESUMABLE_PIPELINE_RUN`, and its test seam, `CompletionContext::child_processes`. 26o's decision that `--allow` takes tools only, and 57o's rule that a tool-shaped name is never a website.

**Docs when it ships:** DEVELOPER.md's `command.h` row (the kinds list and their filters; `OFFERED_TOOL` no longer said to cover a task's grant), the `complete_sources`, `permissions`, `task_cmd` and `chat_completer` rows, and the `complete_sources_test` and `chat_completer_test` rows; CLAUDE.md's 57o rule (that `--deny` offers the listed hosts through `name_gated`, any other host typed); `reference/cli/tools.md` if it names the flags' completion; MILESTONES (a dated entry; the 2026-11-22 entry is history and stays).

## Testing

Hermetic and in process: `complete_sources_test`'s temp homes with scripted configs (mock backends, agents, a pinned suite, permissions, allowed hosts), the real `__complete` path, and each verb run in process on its refusal side only, as the 2026-11-22 cases do. No model, no network, no MCP server.

- **The task grant, both ways.** Under each case below, the offer equals the writers whose `task_grant_refusal` is empty over the same registry; every writer left out is run as `task run "<goal>" --tools … --allow <t>`, which exits 1 with its rule's words, and `tasks::list_tasks` stays empty. The cases:
  - `permissions.write_file: deny`;
  - each of `narrow`, `mute`, `wide`;
  - a recipe naming an agent, with and without a typed `--agent`;
  - the pin by `--suite pinned`, `--suite=pinned`, `-m helper --suite pinned`, `--model helper --suite pinned` and `models.default_suite: pinned`;
  - `--suite off`;
  - `-m` naming a backend outside the suite.
- **Chat-style flags under the pin.** `complete -m helper --tools --allow` and `--deny` offer `delete_note` and `write_note` under the pin. The left-out writers are refused by `complete -m helper --tools --allow|--deny <t> hi`. `chat --suite pinned -m helper --tools --deny <TAB>` keeps the unpinned list, and `chat … --deny edit_file` is still taken. A config-denied writer is still offered and still runs (exit 0).
- **`--deny`'s websites.** The allowed-hosts set above, with the search instance. The offered hosts are exactly the canonical, non-tool-shaped ones, once each. Every offered host is taken by `complete --tools --deny <h> hi` (exit 0), and every one left out is refused. An offered host, denied, is `Deny` in `make_permission_checker` though listed. `listed_hosts` is the checker's one list (`permissions_test`).
- **Hints.** `__complete chat --tools --deny wiki.internal` gives the `TOOL_OR_WEBSITE` hint; `--allow ` offers tools only; `--allow-host ` is still a `TEXT` hint.
- **In chat.** `/deny ` and `/allow ` offer as the acceptance criterion says, each candidate taken by the in-chat verb (`chat_completer_test`).
- **`delete-allowed-host`.** The offer is the canonical accepted entries, and each is removed by the verb.
- **The line up to the cursor.** `task run --tools --allow ` with an `--agent` typed only after the cursor is completed without the agent's filter (the `PRECISION` precedent, stated in the test's name).
- **Never an error.** An unknown `--agent`, an unknown `--suite` and an unreadable config each give a list (or nothing), no exception, no diagnostic.
- **The 57k side.** With `context.child_processes = false`, no kind offers `run_command`.
- **Declarations and kinds.** The bound CLI11 tree (`specs_from_app`): `task run --allow` is `TASK_GRANT_TOOL`, every `--deny` (chat, execute, acp, tui, complete, both in-shell modes) is `TOOL_OR_WEBSITE`, every chat-style `--allow` is `OFFERED_TOOL`, `config set-permission` is `TOOL`. "Every name kind has a source" and the coverage guard cover the new kinds.
- **Goldens:** the two session goldens, in exactly their `--deny` lines.
- **Gate:** `lib/scripts/cicd.sh --app cli --test`, clang-format, and the MinGW `-fsyntax-only -Wall -Wextra` scan of the changed files.

## Open questions

| Question | Status | Answer |
|---|---|---|
| What are the two kinds called (they show in `--help` and the goldens)? | `[default]` | `TASK_GRANT_TOOL` for `task run --allow`, and `TOOL_OR_WEBSITE` for `--deny`, matching the help's "tool or website" and in-chat `<tool\|website>`. |
| How does the source tell `complete` from a session verb? | `[default]` | `CompletionContext` gains the command path the walk already descends; the pin applies when it is `complete` (and through `task_chat_backend` for `task run`). |
| What does the hint say when nothing is left? | `[default]` | Under an agent: `the agent '<a>' runs with tools: <policy>, so a task under it is granted nothing`. Under a pin: `the suite '<s>' gives <backend> only <toolsets>`. |
| Does a missing `--tools` before the cursor empty the task's list? | `[default]` | No. `--recipe` can bring tools, and `--tools` may follow the cursor; the line-level refusal is the verb's to say. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-22 | Specced from v0.2.1's pre-merge docs pass: its completion audit, the review of its fix, and two probes of the built binary in throwaway homes (Evidence). Placed in **Maintenance** as M36. | the user |
| 2026-11-22 | **One item for every permission flag's offer**: the task grant, the pin on chat-style flags, `--deny`'s websites in the shell and in chat, and `delete-allowed-host`'s entries. They are one contract over one list of writers and one list of hosts, and each change reads or extends the same seams. | the spec |
| 2026-11-22 | **A config deny filters the task's offer only.** The probe shows chat-style `--allow` and `--deny` take a config-denied writer (Evidence 4), so filtering `OFFERED_TOOL` by it would leave out names those verbs take. | the spec |
| 2026-11-22 | **`--deny` offers only the hosts the gate already lists** (`tools.allowed_hosts`, the search instance), each through `name_gated` and canonical. Any other host is valid, but the set is open, so it is typed under the hint. Session history is not read: a transcript read per keystroke breaks the budget and would show private chat data at the prompt. | the spec |
| 2026-11-22 | **The recipe's agent filters when `--agent` is not typed**, as `run` itself applies it (`task_cmd.cpp:479-481`); it costs the recipe book's directory read `RECIPE` already makes. | the spec |
| 2026-11-22 | **`models.default_suite`'s pin applies with no flags on the line**, as the verb applies it. A `-m` or `--suite off` typed after the cursor then under-offers, never over-offers. | the spec |
| 2026-11-22 | **Amended after the probes' four side findings were investigated** (CLI-M37–M40). The pin narrows `complete`'s offer only: the session verbs check their flags against the unpinned registry, since `/suite` and `/model` can change the pin mid-session, so pinning their offer would leave out what they take. The `--resume` question is moot with it. `my_host` belongs in `delete-allowed-host`'s offer, since the verb removes it. And `--deny`'s and `delete-allowed-host`'s offers follow CLI-M39's `name_gated` and CLI-M40's predicate once those ship. | the spec |

## Out of scope

- **MCP tools in any offer.** Listing them would start the servers, which the budget forbids.
- **Reading words after the cursor**, which would change the protocol and all four stubs.
- **The PowerShell stub sends the whole line and ignores the cursor**: [CLI-M37](powershell-completes-the-word-at-the-cursor.md).
- **A refused run starts the configured MCP servers first**, in `task run` and every session verb: [CLI-M38](a-refused-run-starts-no-mcp-server.md).
- **`--deny shell` taken silently as a website, and a listed `my_host` refused by `--deny`**: [CLI-M39](deny-takes-every-host-a-yes-takes.md). Both are `name_gated`'s rules; this item's offers follow them.
- **A malformed `tools.allowed_hosts` entry can't be removed by `delete-allowed-host`**: [CLI-M40](delete-allowed-host-removes-what-the-list-holds.md).
- **`--allow` taking a website** (26o stands).

## Links

- **Blocked by:** — · **Blocks:** —
- **Follows from:** the 2026-11-22 completion audit at v0.2.1's docs pass ([MILESTONES.md](../../../assistant/cli/MILESTONES.md), its entry "the completion audit at v0.2.1's docs pass"); 57k (no shell or git tools where no program can start); 57o (a tool-shaped name is never a website).
- **Relates to:** [CLI-M37](powershell-completes-the-word-at-the-cursor.md) (the cursor on PowerShell), [CLI-M41](bash-3-2-completes-every-word.md) (on macOS's bash 3.2 these offers reach no one until it ships), [CLI-M42](stubs-read-the-words-as-the-shell-does.md) (a quoted word on the line), [CLI-M43](gate-edits-land-or-say-they-did-not.md) (on a compact YAML list, `delete-allowed-host` offers hosts the verb refuses until it ships), [CLI-M38](a-refused-run-starts-no-mcp-server.md) (it takes out the same `task_grant_refusal`; whichever ships second reuses it), [CLI-M39](deny-takes-every-host-a-yes-takes.md) and [CLI-M40](delete-allowed-host-removes-what-the-list-holds.md) (the rules `--deny`'s and `delete-allowed-host`'s offers follow); [ADR 0007](../../../adrs/cli/tab-completion.md) (tab completion); [ADR 0006](../../../adrs/cli/backwards-compatibility.md) (`__complete` grows additively); 26o (`--allow` takes tools only); the 2026-10-06 completion audit.
