# Xcode's tools in an ACP session

**What / why.** [56a](acp-agent-mode.md) makes `apogee acp` an agent that Xcode can run ([SPEC.md](../../assistant/SPEC.md):32, the fifth core surface). An agent in Xcode is only worth choosing if it can build and test the open project. Apple says an agent can: "When you choose an agent in the coding assistant, it automatically has access to Xcode capabilities, such as building and testing" (Apple, *Setting up coding intelligence*). This item is Apogee's half of how that access reaches an ACP agent. It removes **wall W2**: Xcode's own tools do not reach an Apogee session. The facts come from the 2026-10-31 Xcode research, where a second agent checked each claim; the ones marked re-checked were run again while writing this:

- **The protocol has a slot for it.** ACP's session setup (agentclientprotocol.com/protocol/session-setup, re-read 2026-10-31) lets a client hand MCP servers to the agent in `session/new`'s `mcpServers`. Sending them is a MAY for the client. Agents "SHOULD connect to all MCP servers specified by the Client". "All Agents MUST support connecting to MCP servers via stdio". http and sse are optional, offered only when the agent's `initialize` answer sets `mcpCapabilities.http` / `.sse`, and sse is deprecated. A stdio entry is `name`, `command` (an absolute path), `args`, and an optional `env` given as `[{name, value}]`. `session/load` carries its own `mcpServers` too. The page names this very use: "Clients MAY use this ability to provide tools directly to the underlying language model by including their own MCP server". It also says that the `env` and `headers` values "are credentials", and that agents SHOULD launch `command` with `args` "as separate arguments, without a shell".
- **Xcode probably uses that slot, but this is not yet proven.** `strings -a` on Xcode 27.1's (27A9275) `IDEIntelligenceACP` finds `mcpServers`, `MCPServer`, `mcpCapabilities`, `stdio`, `xcode-tools` and `MCP_XCODE_SESSION_ID`. The Add-an-Agent sheet reads "Agents have full use of Xcode, and can access your project context." (re-checked 2026-10-31). These are strings, not a captured message: nobody has logged a `session/new` from Xcode yet, and 56a's fixture will be the first.
- **The likely server is `xcrun mcpbridge`.** Since Xcode 26.3 it has been the stdio bridge to Xcode's tool service (Apple, *Giving external agents access to Xcode*: `claude mcp add --transport stdio xcode -- xcrun mcpbridge`). Its help lists `MCP_XCODE_SESSION_ID`, "A UUID identifying an Xcode tool session", and `MCP_XCODE_PID` (`xcrun mcpbridge --help`, re-run 2026-10-31). Cursor's docs give the same entry the key `xcode-tools`. So the likely shape is a stdio `xcode-tools` server, `xcrun mcpbridge`, with a session id in its `env`.
- **What it serves.** `BuildProject`, `GetBuildLog`, `RunAllTests`, `RunSomeTests`, `RenderPreview`, `DocumentationSearch`, `XcodeRead` / `XcodeWrite` / `XcodeGrep` and the issue navigator. Xcode 27 adds debugger, scheme, run-destination, build-settings, simulator and String Catalog tools (from `IDEIntelligenceChat` strings and the Xcode 27 release notes). The live `tools/list` has not been queried, because querying it would raise Xcode's consent prompt.
- **Xcode asks its own question.** Access is gated by Xcode: a per-agent prompt ("Do you want to allow the agent …"), and a fresh one after "The agent binary has been updated since you last approved it" The prompts are `IDEIntelligenceChat` strings. The binary is hashed with SHA-256: `ProcessSecurityBinaryHasher` is in `IDEIntelligenceFoundation` (re-checked 2026-10-31). Still unverified: whether the "Allow external agents to use Xcode tools" setting also gates a custom ACP agent.
- **Apogee cannot take these servers today.** Its MCP servers come from config alone. `make_built_in_tools` dials `config->mcp_servers` and nothing else, and only when that list is non-empty (`lib/src/cli/source/presentation/cli/helpers.cpp:268-302`). Its client is stdio-only ("`StdioTransport` is the one that ships", `business/mcp/transport.h:26`) and speaks protocol `2025-03-26` (`business/mcp/types.h:23`). It has never been tried against `mcpbridge`.

This item closes that gap. The stdio servers a host names in `session/new` join **that session's** MCP registry for the session's life. They connect through the client Apogee already has, and every call they serve goes through the gate every MCP tool goes through. The config file is never written.

**Core constraint(s).**
- **Pipes, never a port.** SPEC.md:66: "local front-ends are pipes"; CLI CLAUDE.md: "⚠ Interactive turns never open a listening socket". A host's server is a child on pipes, as a config server is. An http or sse server is never dialled, because the client has no remote transport (`transport.h:21-24` keeps one out of scope).
- **stdout is the protocol.** Under `apogee acp`, stdout carries Xcode's JSON-RPC. A host server's stderr is **captured, never inherited** (CLI CLAUDE.md "⚠ A child's stderr is captured, never inherited"). Its bounded tail rides the skip notice. `[mcp]` progress goes to stderr, as it does in machine mode (`presentation/cli/complete.cpp:321-328`). Nothing a server prints reaches stdout.
- **One MCP client** (SPEC.md:70, "One source of truth per concern"). Host servers go through `mcp::Registry::connect_all` and `register_into` and get the same handling as config servers: spawn, handshake, connect bound, `mcp__<server>__<tool>` names and `writes` rule. There is no second client and no ACP-only tool wrapper.
- **The gate is Apogee's law.** A host's tool is `writes` unless its server marks it `readOnlyHint` (`business/mcp/registry.cpp:104`), and a `writes` tool asks through the permission gate (`business/agent/tool.h:90-92`). Accept-edits mode never covers an MCP tool (`tool.h:101-105`). A host handing over a server is not the user consenting to its calls. How the gate's question reaches Xcode is 56a's work.
- **The session's, not the user's.** The config file is never written: no `contracts/config_edit.h` call, no `mcp create`, and the file is byte-identical before and after (CLI CLAUDE.md "⚠ One config mutation path"). A host's servers are never saved with the session either. A resumed session ([56c](acp-sessions-saved.md)) connects what its own request hands over. No other session ever sees them.
- **Never fatal.** A server that will not start, dies, or never finishes its handshake is skipped and said; `session/new` still answers (`business/mcp/registry.h:18-25`).
- **Secrets stay unleakable** (CLI CLAUDE.md "⚠ Secrets are unleakable by construction"). ACP itself calls these `env` values credentials. The request's `env` values reach the child and nothing else. A log line, notice or event names servers and env keys, never values.
- **Additive** ([ADR 0006](../../adrs/backwards-compatibility.md)). The `initialize` answer states `mcpCapabilities: {http: false, sse: false}`. Turning either on later is an addition.

**Seam + files.**
- `presentation/cli/helpers.h/.cpp`: `BuiltInToolOptions` gains `session_mcp_servers` (`std::vector<mcp::ServerSpec>`, the host's).
  - `make_built_in_tools` appends them to the same `specs` list, after the config entries. The one `connect_all` and `register_into` (`helpers.cpp:299-300`) then take both, on the same `options.mcp` registry with the same `RegistryOptions`.
  - Never a second pass: `register_into` adds every connected client's tools (`business/mcp/registry.cpp:96-120`), so a second call would re-add the config's tools and throw.
  - The guard at `helpers.cpp:270-271` skips MCP entirely when the config lists no servers. It is widened so a host server connects even with an empty config.
  - `BuiltInToolOptions` also gains an optional `mcp_connect_timeout`, defaulting to the 20 s bound, so the slow-server test can shorten it. `RegistryOptions` is built inside the helper today, and nothing above it can set the bound.
  - `apply_tool_policy` (`helpers.cpp:306`) still runs last, over these tools as well.
- 56a's ACP adapter: `session/new` (and `session/load`, with 56c) maps `mcpServers` to `ServerSpec`s and hands them to the session's `execute` child (open calls: across 56a's process line).
  - A stdio entry becomes `name`, `command`, `args`, and `env` turned into `name=value` strings. `spawn_stdio` refuses an empty key (`business/mcp/transport.cpp:166-171`), and that server is skipped with the reason. It splits each entry at the first `=`. So the adapter itself refuses an env name that contains `=`, and skips that server the same way.
  - Values pass as given. Config expands `${VAR}` and `~` at load (`data/contracts/config.h:711-713`), but a host's values are the host's.
  - http and sse entries are set aside for the notice.
  - The `initialize` answer's `agentCapabilities.mcpCapabilities` stays as 56a states it, `{http: false, sse: false}`.
- **One registry per session**: a `shared_ptr<mcp::Registry>` per ACP session, as `chat_session.cpp:1159` makes one per chat, never a process-wide one. Closing the session drops it. `~Registry` calls `close_all` (`registry.cpp:9-11`), which closes each child: stdin closed, 500 ms, then terminate (`transport.cpp:144-157`).
- **The session's notice.** The session's `mcp_status` (in its `execute` child; open calls), which the helper hands on as `RegistryOptions.status`, sends progress to stderr and collects the `[mcp] warning:` lines. The collected lines and any http/sse skips go out as **one** `session/update` notice once the session has its id, in the form 56a uses for Apogee's own notes. `server_log` is null or stderr, never stdout.
- **Names and collisions.** A host's server name goes through `scaffold::sanitize_server_name` (`business/scaffold/mcp_server.cpp:212`). That is `mcp create`'s rule, and its header calls it what a config entry name may be:
  - `.` and `:` become `-`;
  - anything but letters, digits, `_` and `-` is refused;
  - `__` is refused too, because it delimits `mcp__<server>__<tool>`.

  It throws, so the adapter catches the error and skips the server. A collision has to be settled before the dial:
  - `Registry::clients_` is a case-sensitive map keyed by name (`registry.h:97`). Two specs with one name in one `connect_all` both spawn, and the last one silently wins (`registry.cpp:83`).
  - Config names are compared case-insensitively (`data/contracts/config.cpp:2142-2146`).
  - `ToolRegistry::add` throws on a duplicate tool name (`business/agent/tool.cpp:28-29`). Any path that registers a name twice ends the session.
- **Docs.** `lib/documentation/cli/reference/tools.md` gains "Servers an ACP host hands a session", after "MCP presets" (line 160). It covers what is taken and skipped, the gate, Xcode's consent prompt, and the fallback. 56a's `lib/documentation/cli/reference/acp.md` points to it where it lists what `session/new` takes.
- **Tests.**
  - The helper over the fake fleet (`tests/support/fake_mcp_server.h`: `well`, `slow`, `dying`; `echo` is read-only and `write` has no annotation, lines 43-44).
  - 56a's adapter tests.
  - An end-to-end run over 56a's scripted ACP client on the real binary.

**Reference.**
- **The config fleet**, `Registry::connect_all` (`registry.cpp:13-86`): `[mcp] connecting:` goes out before each dial, a bad server is skipped, and a 20 s bound covers connect and handshake (`registry.h:48`). This item reuses it whole. The only change is where the specs come from.
- **`tests/scripts/sh/mcp_e2e.sh`** has the two real-process fixtures. One is the binary's own server registered as a server (lines 56-58: `--args __mcp-tools`, or `mcp serve` once [54a](mcp-serve-public.md) lands). The other is a `/bin/sh` server that dies, whose stderr is folded once (lines 64-71). This item passes both in `session/new` instead of writing them to config.
- **The mock backend's script** (`data/backends/mock.h:66-72`): `tool_calls` lets a test make a turn call a host server's tool without a model.
- **Prior art.**
  - ACP's session setup: Apogee follows it on stdio. It departs from it on http, which "new Agents SHOULD support": not until the client grows a remote transport.
  - Claude Code's and Codex's documented `mcpbridge` registrations: the config route that [56d](xcode-setup-from-apogee.md)'s `xcode` preset follows.

**Decisions made** (dated):
- 2026-10-31 — Specced from the day's Xcode research, under the user's call that made `apogee acp` the fifth core surface (SPEC.md:32). This item covers wall W2; the agent mode is 56a's.
- 2026-10-31 — A host's servers are the session's: joined for its life, never written to config, never saved with it, never seen by another session.
- 2026-10-31 — Every host-supplied tool passes the gate as any MCP tool does. A host handing over a server is not the user's consent to its calls.
- 2026-10-31 — Xcode's own consent prompt for `mcpbridge` belongs to Xcode. It shows there and is answered there; Apogee never answers or suppresses it. It is expected to return after each `apogee-dev` install, because Xcode ties approval to the binary's hash. That comes from binary strings, and the live pass confirms it. This is wall W3, which cannot be fixed without Developer ID signing; [56d](xcode-setup-from-apogee.md) states it to the user, and `reference/tools.md` repeats it, as 56a's `reference/acp.md` does.
- 2026-10-31 — The subscription backends are offered through `apogee acp` (the user's call). A host server's tools reach the session's backend the same way config servers' tools do. This item adds no per-backend path.

**Open calls:**
- [default: http and sse entries are skipped and named once in the session's notice ("Apogee connects stdio MCP servers only; skipped: <names>"), since the client is stdio-only and `initialize` advertised neither. Recommendation: take it. An http transport is its own item, if a host ever needs one.]
- [default: if 56a's captured `session/new` from Xcode carries no `mcpServers`, Xcode's tools come through 56d's `xcode` preset (`xcrun mcpbridge` under `mcp_servers`), which every session then connects from config. `reference/tools.md`'s new section names the preset and what it needs: "Allow external agents to use Xcode tools" turned on (Settings › Intelligence › Model Context Protocol) and the project open in Xcode. This item's code still ships, because it is ACP's path for any host. Recommendation: take it.]
- [default: Xcode's `fs/*` and `terminal/*` client capabilities are not used. Apogee's native tools run in its own process, and Xcode's file tools arrive through its MCP server. Recommendation: take it; revisit only if the live pass finds Xcode's MCP tools absent.]
- [default: a host server displaces a config entry for that session only, when the two share a name (compared case-insensitively, as config compares them) or the same command and arguments, and the notice says so. A bare command is resolved on `PATH` before the comparison. The host's copy carries the session's binding (`MCP_XCODE_SESSION_ID`), and two `mcpbridge`s would show the model every Xcode tool twice. A name that `sanitize_server_name` refuses is skipped and said. Recommendation: take it.]
- [default: host servers connect inside `session/new`, before it answers, each under the config fleet's 20 s bound. The live pass records how long `session/new` took. Recommendation: take it, and move the dial to the first prompt only if Xcode is seen timing out.]
- (consumed decision) From 56a's seam: A host server's child starts in the session's `cwd`, which ACP makes "the base for relative-path resolution". 56a starts the session's `execute` child there, through the optional working directory it adds to `platform::ChildCommand` (`infrastructure/platform/child_process.h:31-41`, applied beside the spawn's file actions, `child_process.cpp:423-427`), and every child that process starts, a host server included, inherits the folder. This item adds no working directory of its own.
- [default: **across 56a's process line.** Under 56a's translator shape, the session core, and with it `make_built_in_tools` (`chat_session.cpp:1159-1172`), runs in the session's `execute` child, not in `acp`. So the adapter's `ServerSpec`s cross to that child. `acp` writes them as JSON to an owner-only (0600) file in a fresh temporary directory and names the file with a hidden `execute` option; the child reads it, deletes it, and fills `session_mcp_servers` from it. A hidden option (`group("")`, as `config`'s refused `--api-key` is, `config_cmd.cpp:1162-1164`) is never completed (`complete_protocol.cpp:233-234`). The `env` values never ride argv, which `ps` shows, or the child's environment, which every server and vendor CLI it starts would inherit. The `[mcp]` warnings are then the child's: they come back as machine-mode `notice` events (`presentation/machine/json_reporter.h:153`; an additive growth if the build finds they reach only stderr today), and `acp` folds them into the one notice. Recommendation: take it.]
- [default: ACP-only. `chat`, machine mode and `serve` keep config-only servers. MCP tools already reach every surface through the one registry path ([ADR 0002](../../adrs/mode-parity.md)); a host handing over its own servers exists only on ACP's wire. No new verb or visible flag (the hidden option above is plumbing, never completed), so ADR 0007 and ADR 0010 are untouched. The servers attach inside the one session opening 56a defines ([ADR 0011](../../adrs/one-session-opening.md)). Recommendation: take it.]

**Guardrail(s).** Each runs on the real binary or the fake fleet, with a temporary `APOGEE_HOME` and a mock backend:
- **Joined.** 56a's scripted ACP client passes the binary's own server as a stdio entry named `self` in `session/new`. A scripted turn calling `mcp__self__list_notes` gets its answer in that session.
- **Only that session.** A second `session/new` in the same process, with no servers: the same scripted call is refused as an unknown tool.
- **Config untouched.** The config file's SHA-256 is equal before and after both sessions, and `apogee mcp list` run from another process does not name `self`.
- **Dying server.** `/bin/sh -c 'echo "fatal: no upstream" >&2; exit 1'` in `session/new`: `session/new` still answers, and the notice carries "fatal: no upstream" once, folded. Every stdout line parses as JSON-RPC.
- **Slow server.** The fake `slow` personality, under a shortened bound: skipped with "timed out", and `session/new` answers within the bound.
- **http and stdio together.** The stdio server connects. The http one is never spawned and is named in the one notice. The test's `mcp_spawn` wraps `fake_fleet` and records each `ServerSpec` it is handed. The fleet itself keeps only the name (`tests/support/fake_mcp_server.cpp:142-185`).
- **Env.** A sentinel value in `env` reaches the spawn: the same recording wrapper sees it, and so does a `/bin/sh` server on the real binary that writes it to a temp file. The value appears in no stderr line, log line or notice, and on no process's argv.
- **Gate.** Calling the fake's unannotated `write` asks through the gate; a refusal means the server never sees `tools/call`. Calling `echo` (`readOnlyHint`) does not ask.
- **Collision.** A config entry `self` plus a host server `self`: one connection, the host's, a notice, and no throw.
- **Cleanup.** After the session ends (a second `session/new`, which ends the one before under 56a, or `session/close` once [56c](acp-sessions-saved.md) advertises it), or after stdin reaches EOF, the server's child is gone from the process tree.
- **Standing checks** stay green and unchanged: `cli.no_listen_symbols`, `cli.complete_opens_no_listening_socket` and `cli.mcp_server_stderr_stays_off_the_terminal`.

**Acceptance criteria:**
- [ ] The stdio servers in `session/new` connect for that session alone. Their tools are named `mcp__<server>__<tool>` and gated like any MCP tool's. The config file is byte-identical afterwards, and a second session sees none of them.
- [ ] http and sse servers are skipped with one notice. A failing server is skipped with its stderr tail and is never fatal. Nothing a server prints reaches stdout.
- [ ] `initialize` advertises `mcpCapabilities: {http: false, sse: false}`.
- [ ] `reference/tools.md` documents host servers: what they get, the skip rules, Xcode's consent prompt (shown in Xcode, asked again after each install) and the preset fallback. The full suite is green.
- [ ] **The user's live pass**, on the dev channel ([Development runs on the dev channel](../../assistant/CLAUDE.md#development-runs-on-the-dev-channel)). The user registers `apogee-dev` in Xcode 27.1's Add an Agent sheet by its absolute path, with the argument `acp`, opens a project, and asks the agent to build it. The build runs through Xcode's tools: a call to `BuildProject`, as `mcp__<server>__BuildProject` under whatever name Xcode's entry carries, and its result appear in the transcript. MILESTONES records:
  - whether Xcode sent `mcpServers`, with their names, commands and env keys (never the values);
  - the consent prompt seen;
  - whether "Allow external agents to use Xcode tools" had to be on (the research left open whether it gates a custom ACP agent);
  - how many Xcode tools carried `readOnlyHint`;
  - how long `session/new` took.

  If Xcode sent no servers, the same prompt is run through the `xcode` preset instead. The filesystem layer (wall W4) is in whatever state 56a's pass recorded.

**Scope note.** Status: earmarked for v0.2.1, track 56 ("Apogee in Xcode"). Gated 🔒 on [56a](acp-agent-mode.md), since there is no ACP session to join without it. Removes **wall W2**.

Out of scope:
- http and sse MCP transports;
- Xcode's `fs/*` and `terminal/*` capabilities;
- the `xcode` preset, the check row and the Add-an-Agent fields ([56d](xcode-setup-from-apogee.md), wall W6, which also states W3);
- saving and resuming ACP sessions ([56c](acp-sessions-saved.md), wall W5);
- the filesystem security layer (56a's, wall W4);
- signing (W3, not fixable here);
- the Xcode plug-in (a ROADMAP idea, the user's call);
- Apogee's own MCP server ([54a](mcp-serve-public.md)).

The app's side of the track is [5a](../../../gui/backlog/v0.2.1/xcode-in-settings.md), gated on 56d. This item needs nothing from the app.
