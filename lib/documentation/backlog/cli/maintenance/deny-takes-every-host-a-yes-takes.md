# CLI-M39 · Deny takes every host a yes takes, and says when a tool's name became a website

| Field | Value |
|---|---|
| **Type** | 🐞 Bug |
| **Project** | CLI |
| **Status** | 🟢 Ready |
| **Fix version** | Maintenance (any time) |
| **Component** | Permissions › `name_gated` (`--deny`, `/deny`, `/allow`, `/revoke`) |
| **Labels** | `permissions` `hosts` `deny` |
| **Epic** | — (maintenance has no track) |
| **Blocked by** | — |
| **Blocks** | — |
| **Reporter** | CLI-M36's probes (2026-11-22), then a sandboxed investigation and a skeptic's re-run the same day |
| **Created** | 2026-11-22 |

---

## Summary

`--deny` decides whether a name is a website by its shape alone, and the rule is wrong both ways.
- A host the config lists, or the session allowed, but shaped like a tool (`my_host`) can't be denied or revoked, though tools.md promises every host `--allow-host` takes.
- A toolset's or tool's own name (`--deny shell`, `--deny git`, `--deny run_command.`) is taken silently as a website, and the tool the person meant still runs.

The item makes a known host deniable whatever its shape. It keeps a tool's name as a website deny, by the user's call, but says so, with the way to refuse the tool.

## Description

### User story

**As** a person who refuses things for one session with `--deny` or `/deny`,
**I want** every website I allowed to be refusable, and to be told when a name I gave was taken as a website rather than a tool,
**so that** `--deny my_host` refuses `my_host`, and `--permission-mode bypass --deny shell` doesn't quietly leave the shell running.

### Steps to reproduce

The sandbox, 2026-11-22, with the built binary `apogee 0.2.0 (33ea685d, 2026-11-22, macos-arm64)`: throwaway `APOGEE_HOME` and `HOME`, `env -i`, stdin from `/dev/null`. Mock backends whose scripts call one tool each (`run_command echo RAN_SHELL_MARKER`, a git read, `write_file`, `write_note`), and `permissions: {run_command: allow, write_file: allow, write_note: allow}`.

1. `apogee complete --tools --deny shell hi`, then the same with `--deny run_command`.
2. `apogee config add-allowed-host my_host`, `apogee check`, then `apogee complete --tools --deny my_host hi`.

### Actual result

1. `--deny shell` answers `DONE<RAN_SHELL_MARKER [exit 0]>` and exits 0, and in stream-json `run_command` goes pending, running, done. `--deny run_command` answers `DONE<Error: the user denied permission to run 'run_command'. …>`.
2. `tools.allowed_hosts includes my_host`, then `ok   allowed_hosts  my_host`, then `apogee complete: --deny: 'my_host' is not a tool this chat asks about -- the ones it asks about: delete_file, delete_note, edit_file, run_command, write_file, write_note; or name a website, like docs.python.org` (exit 1).

### Expected result

1. `--deny shell` is still taken as a deny of the website `shell`, and says so: a note on stderr (a `notice` in machine mode) that it is a website deny, and that the shell's tool is refused with `--deny run_command` or left out with `tools.disabled: [shell]`. The tool's behaviour is unchanged.
2. `--deny my_host` is taken. A fetch of `http://my_host/` is then refused unsent for the session, though the config lists it.

### Background

`name_gated` (`permissions.cpp:449-485`) is the one function the "no" paths share: `--deny` (through `seed_approvals`, `:533-538`) and in-chat `/deny`, `/allow` and `/revoke` (`chat_session.cpp:2493`). After the gated tools and 57k's refusal, a name is a website when it is "website-shaped" (`website_shaped`, `:441-447`): a dot, brackets or a `:`, or one label with no `_`. The "yes" paths (`--allow-host`, the prompt's `session` and `always` answers, `config add-allowed-host`, `check`, the checker's `host_listed`) go by `canonical_host` alone, and `canonical_host` keeps `_` on purpose (`host.cpp:52`, `host_test.cpp:21`).

The record:
- 26o (MILESTONES) made `--allow` tools-only, since "a typo'd tool name that parses as a host would otherwise become a website grant. A `--deny` that lands on a host by mistake only tightens".
- 57o (2026-11-01) restored dotless hosts, and recorded "a dotless typo such as `shel` becomes a website deny" for veto.

The user's call of 2026-11-22 keeps that: a tool's or toolset's name stays a website deny, because `--deny web` and `--deny git` aimed at intranet hosts of those names work and matter today. What changes is that it is no longer silent.

### Evidence

All measured 2026-11-22 in the sandbox above, by the investigator and again by a skeptic with its own sandbox.

1. **Every dotless word with no `_` is taken as a website.** 14 of 14 tried exited 0 and refused no tool:
   - the eight toolset words `fs shell git notes rag graph web mcp`;
   - `todo` and `delegate`;
   - the case and dot variants `Shell`, `SHELL` and `shell.`;
   - the typo `shel`.

   `--deny run_command.` too (a gated tool's name with a trailing dot). 6 of 6 tool-shaped words were refused as typos: `fetch_url read_file git_log git_status todo_write write_flie`.
2. **The tool still runs.** `complete -m write --tools --deny fs hi` wrote `out.txt`; `-m note --tools --deny notes hi` saved the note; `-m git --tools --deny git hi` printed the log.
3. **It lands as a website deny.** With `shell` in `tools.allowed_hosts`, `complete --tools -m fshell --deny SHELL hi` answered `permission to reach shell was not given, so nothing was sent to it`. So the deny is real, on the host `shell`.
4. **Such host denies are load-bearing.** `chat --tools --permission-mode bypass --deny web` and `--deny git` exit 0 today and refuse a host of that name, which bypass would otherwise reach (`permissions.cpp:249-251`). That is 57o's `intranet` case, pinned in `permissions_test.cpp:743-755`.
5. **The launch flags are silent; the slash verbs are not.** In stream-json, no event mentions the deny. In chat, `/deny shell` answers `website shell denied for this chat` (`chat_session.cpp:2498`, `:2513`), and `/allow shell` grants the website `shell` (`:2509`).
6. **A listed `my_host` can't be denied by its own spelling.** `config add-allowed-host my_host` and `My_Host.` are accepted (stored `my_host`), and `check` says `ok`. `--allow-host my_host` exits 0. But `--deny my_host` and `chat --tools --allow-host my_host --deny my_host` exit 1 as above.
7. **The checker allows a listed host before any session yes** (`permissions.cpp:264-281`: the session's denies at `:271`, `host_listed` at `:274`). So a listed `my_host` is reached unasked. The spelling `my_host.` is dotted, so it passes the shape test and denies it today (the skeptic measured `--deny my_host.` refusing it, even beside `--allow-host my_host`); nothing tells the person so.
8. **`/revoke`'s completion offers every session host** (`chat_session.cpp:2176-2182`), `my_host` included, which `/revoke` then refuses.
9. **The promise broken.** "`--deny`, `/deny` and `/revoke` take every host `--allow-host` takes" (`reference/cli/tools.md:137`), and the headline of CLAUDE.md's 57o rule. The rule's body and 57o's decision row ("a single label with no `_`") are what the code does, so the reference and the headline are what (6) breaks ([ADR 0006](../../../adrs/cli/backwards-compatibility.md): the reference's behaviour contracts hold).
10. **Not pinned.** The [57o] case (`permissions_test.cpp:706-770`) has no toolset word and no listed `_` host.
11. **Who passes `name_gated` its inputs**: `seed_approvals` (`chat_session.cpp:1606`, `complete.cpp:101-113`, and `task_cmd.cpp:1570` with allows only), and the slash verbs at `chat_session.cpp:2493`. The darwin app passes no `--deny` or `/deny`.
12. **What already works**:
    - `--deny run_command`, `write_file` and `write_note` refuse the tool even over a config `allow`;
    - tool-shaped typos are refused, naming the gated set;
    - `--deny SHELL` is stored as `shell`, one canonical host rule;
    - `--allow shell` is refused and points at `--allow-host` (26o);
    - an unlisted host is denied unsent where nobody can answer.

## Acceptance criteria

- [ ] **Given** `tools.allowed_hosts: [my_host]`, **when** `complete --tools --deny my_host hi` runs and the model fetches `http://my_host/`, **then** it exits 0 and the fetch is refused unsent: `permission to reach my_host was not given, so nothing was sent to it`.
- [ ] **Given** `--allow-host my_host`, or a prompt answered `session` for it, **then** `/deny my_host` and `/revoke my_host` take it, and `/revoke`'s offer of it is one the verb accepts.
- [ ] **Given** `permissions.run_command: allow`, **when** `complete --tools --deny shell hi` runs, **then** it still exits 0, the deny is stored as the website `shell`, and stderr carries a note in the words the Answer below sets. In stream-json the note is a `notice` event.
- [ ] **Given** each toolset word (`fs shell git notes rag graph web mcp`) and a gated tool's name with a trailing dot (`run_command.`), compared in canonical form (`Shell`, `SHELL`, `shell.` too), **then** the deny is a website deny with its note, and the note names what refuses the tool: `--deny <writer>`, `tools.disabled: [<set>]`, or for `web` and `mcp` the per-website and per-tool ways.
- [ ] **Given** `/deny shell` or `/allow shell` in chat, **then** the reply says the website was denied or allowed, as today, and adds the same pointer.
- [ ] **Given** an unlisted tool-shaped host (`--deny my_host` with nothing listing it), **then** it is refused as today, and the refusal names the spelling that works, `my_host.`.
- [ ] **Given** `write_flie`, `run_comand` and `mcp__srv__echo`, **then** they are still refused as typos; `intranet`, `shel` and `localhost` are still taken as hosts.

## Constraints

- **One function decides**: `name_gated`, for every no path. The fix gives it the run's known hosts (the checker's listed hosts and the session's allowed and denied ones), never a second rule beside it.
- **A known host wins over the shape test**, after the exact gated-tool match and 57k's refusal. A known host spelled like a toolset word (an intranet `git` the config lists) is that host, with no note.
- **`--allow` stays tools-only** (26o): the known-host branch is behind `websites`, so `--allow my_host` is still refused and points to `--allow-host`.
- **Nothing that works today is refused** (the user's call, 2026-11-22). A toolset's or tool's name stays a website deny, so scripts and ACP editor configs passing `--deny shell` or `--deny web` keep running; only the note is new. `run_command.` keeps its deny too.
- **The note is said in every mode** ([ADR 0002](../../../adrs/cli/mode-parity.md)): a stderr line in text, a `notice` in machine mode, the reply's words in chat.
- **`canonical_host` is unchanged**: `_` stays a host character (`host_test.cpp:21`), and `add-allowed-host` and `check` keep accepting `my_host`.

## Technical approach

Paths under `lib/src/cli/source/presentation/cli/`.

| File | Change |
|---|---|
| `permissions.h`, `permissions.cpp` | `name_gated` takes the run's known hosts. Its order: a gated tool is a tool; 57k's refusal; a name whose canonical form `host_listed` finds among the known hosts is that website; a website-shaped name is a website (57o), and when its canonical form is a toolset word or a tool's name, the result carries a note; anything else is the typo, its refusal naming `<name>.` when `canonical_host` accepts it. `listed_hosts(config)` is taken out of `make_permission_checker` (`:232-241`), the list CLI-M36 also reads. `seed_approvals` takes the config's listed hosts, seeds `--allow-host` before the denies, and returns the notes beside its refusal. |
| `chat_session.cpp` | `seed_approvals` (`:1606`) gets the config; its notes are said where the session's notices are (stderr, or `notice`). The slash verbs (`:2493`) pass the listed hosts and the session's allowed and denied ones, and add the note's pointer to their replies. |
| `complete.cpp` | `seeded_approvals` (`:101-113`) takes the config and says the notes; its callers (`:343`, `:534`) hold it already. |
| `task_cmd.cpp` | `seed_approvals` at `:1570` gets the new argument (allows only; behaviour unchanged). |
| `../../tests/presentation/cli/permissions_test.cpp` | Beside the [57o] case: each toolset word, `Shell`/`SHELL`/`shell.` and `run_command.` taken as a website with its note; `my_host` taken when listed or answered, refused naming `my_host.` when not; a listed `my_host` under `--deny` is `Deny` in `make_permission_checker`; `--deny my_host.` still passing; the typos and hosts of the last criterion unchanged. |
| `../../tests/presentation/cli/chat_test.cpp` | On the command tree: `--deny shell` exits 0 with its note on stderr; `--deny my_host` with it listed is accepted and a scripted fetch to `http://my_host/` is refused unsent; `/deny my_host` and `/revoke my_host` after `--allow-host my_host`. |

**Reference.** The 57o rule and its test, and 26o's one vocabulary for both kinds of ask (MILESTONES). `host_listed`, the checker's own match, reused as the known-host test. CLI-M36 reads the same `listed_hosts` for `--deny`'s offer.

**Docs when it ships:** `reference/cli/tools.md:137-138` (every host the config lists or the session answered is deniable whatever its shape; a tool's name is a website deny, said); CLAUDE.md's 57o rule; DEVELOPER.md's `permissions` and `permissions_test` rows; MILESTONES (a dated entry with a dated revision of the 57o row).

## Testing

Hermetic: `permissions_test`'s unit cases over `name_gated`, `seed_approvals` and `make_permission_checker`, and the command-tree cases on a mock backend whose script calls the tool or fetch. No network: a refused fetch is refused before anything is sent.

- **The bug as a fixture.**
  - With `permissions.run_command: allow` and a script calling `run_command`, `complete --tools --deny shell hi` must exit 0 with the note on stderr; today it has none.
  - With `tools.allowed_hosts: [my_host]` and a script fetching `http://my_host/`, `complete --tools --deny my_host hi` must exit 0 and answer `permission to reach my_host was not given`; today it exits 1.
- **Mutation checks**: dropping the known-host branch fails the `my_host` case; dropping the note fails the `shell` case; letting the known-host branch take any canonical name fails the `write_flie` case.
- **Gate:** `lib/scripts/cicd.sh --app cli --test`, clang-format, and the MinGW scan of the changed files.

## Open questions

| Question | Status | Answer |
|---|---|---|
| What does the note say? | `[default]` | For a toolset that has an asking tool: `'shell' is taken as a website; to refuse the shell's tool, --deny run_command, or leave the toolset out with tools.disabled: [shell]`. For `git`: `'git' is taken as a website; git's tools only read and never ask -- leave them out with tools.disabled: [git]`. For `web`: `'web' is taken as a website; fetch_url and web_search are asked about per website -- name the website, like docs.python.org`. For `mcp`: `'mcp' is taken as a website; refuse an MCP tool by its name, mcp__<server>__<tool>`. For a tool's name with a dot: `'run_command.' is taken as the website run_command; to refuse the tool, --deny run_command`. |
| Is a host that is both known and a toolset word noted? | `[default]` | No. A listed or answered host is the person's own, so it is that host with no note. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-22 | Specced from CLI-M36's probes and a sandboxed investigation with a skeptic's re-run (Evidence); placed in **Maintenance** as M39. | the spec |
| 2026-11-22 | **A toolset's or tool's name stays a website deny, and says so.** Refusing it would break `--deny web` and `--deny git` aimed at intranet hosts, and the scripts and ACP configs that pass them. Leaving it silent leaves `--permission-mode bypass --deny shell` running the shell unasked. The note keeps every working deny and ends the silence. This is a dated revision of 57o's veto-flagged decision. | the user |
| 2026-11-22 | **A known host is deniable whatever its shape**: the checker's listed hosts and the session's answers, matched by `host_listed`. That keeps tools.md's promise, and keeps 57o's typo protection for every unknown name. | the spec |
| 2026-11-22 | **No warning in `check` or `add-allowed-host` for a `_` host.** With the known-host branch such a host can be denied like any other, and `canonical_host` keeps `_` on purpose. | the spec |

## Out of scope

- **A malformed `tools.allowed_hosts` entry `delete-allowed-host` can't remove**: [CLI-M40](delete-allowed-host-removes-what-the-list-holds.md), a different seam (the config editor).
- **Completion**: `--deny`'s offer of the listed hosts is [CLI-M36](../../../assistant/cli/MILESTONES.md#2026-11-27--grant-and-deny-flags-offer-what-they-take-maintenance-item-m36-the-permission-flags-offer-what-their-verbs-take)'s (`TOOL_OR_WEBSITE`, and `/deny`'s in chat), through this `name_gated`.
- **Narrowing `canonical_host`**, which would make today's `ok` entries malformed.

## Links

- **Blocked by:** — · **Blocks:** —
- **Relates to:** [CLI-M36](../../../assistant/cli/MILESTONES.md#2026-11-27--grant-and-deny-flags-offer-what-they-take-maintenance-item-m36-the-permission-flags-offer-what-their-verbs-take): its `--deny` offer is defined by `name_gated`, so once this ships a listed `my_host` is offered. CLI-M36 shipped first (2026-11-27), so this item updates its `name_gated` calls to the new signature: `tool_or_website_names` in `complete_sources.cpp` and the `/deny` and `/allow` sources in `chat_completer.cpp`. Each keeps a listed host only when its canonical name is taken too, so `side_door.` joins the offer once this item takes it. Also [CLI-M40](delete-allowed-host-removes-what-the-list-holds.md) (the same list of hosts, another verb); 57o (dotless hosts, the typo rule); 26o (`--allow` takes tools only); 57k (no shell or git tools where no program can start).
