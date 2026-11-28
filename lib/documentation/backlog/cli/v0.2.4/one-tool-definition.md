# CLI-60a · One tool definition

| Field | Value |
|---|---|
| **Type** | ✨ Feature |
| **Project** | CLI |
| **Status** | 🟢 Ready |
| **Fix version** | v0.2.4 |
| **Component** | Tools: the native registry, MCP import and serve, ACP, the machine schema |
| **Labels** | `tool-definition` `mcp` `acp` `additive` |
| **Epic** | Track 60 · one tool definition and device tools |
| **Blocked by** | — |
| **Blocks** | [60b](device-tools.md), [60c](app-tools-on-every-door.md) |
| **Reporter** | The Apple-platforms spike and brainstorm (2026-11-07), from the user's vision |
| **Created** | 2026-11-07 |

---

## Summary

A tool takes four shapes in Apogee today (the native registry, MCP import, MCP serve, the ACP mapping), plus a fifth hand-kept list in `check`, and each keeps a different part of it. This item adds one type, **`agent::ToolDefinition`**, that says for every tool what it is, what it may do, what the gate does with it, and where it runs; every surface reads it and the device tools ([60b](device-tools.md)) carry it over the network. No tool is renamed, and what a model is shown does not change.

## Description

### User story

**As** a person using Apogee across the devices I own,
**I want** every tool described once (name, description, input schema, hints, permission class, ACP kind, and where it runs),
**so that** MCP hosts get honest hints, ACP's kinds come from the tool rather than a side table, and a connected desktop can later offer its own files and shell to a server session in that same description.

### Background

The user's vision, 2026-11-07: Apogee is a private, self-hosted harness that feels like Claude on every device the person owns. A strong machine serves the person's own desktops and phones, and a connected desktop offers its own files and shell to a server session, as Claude Code does. To offer a tool from a device, there has to be one way to describe a tool. The Apple-platforms spike the same day found none.

**The shape of the design.** This item adds **`agent::ToolDefinition`**. It holds a tool's name, description, input schema, its hints (read-only, destructive, reaches off the machine), its permission class, its ACP kind, and where it runs (in the harness that holds the loop, or on a connected device). It also has a JSON form, declared in the machine protocol's schema. The native registry, MCP import, MCP serve and ACP read it, and the device tools ([60b](device-tools.md)) carry it over the network. It is documented in tools.md. No tool is renamed, and what a model is shown does not change.

### Evidence

Re-measured at HEAD `4f92316d`, 2026-11-07: the source, and a scratch `APOGEE_HOME` with a raw JSON-RPC client playing the MCP host.

**W7 — a tool takes four shapes, and each shape keeps a different part of it.**

1. **The native registry.** `agent::Tool` (`business/agent/tool.h:110-166`) has `name`, `description` and `parameters_schema` as text, plus three gate flags: `writes`, `outbound`, `edits` (`edits` implies `writes`). The flags are set where each tool is made (`tools/fs.cpp:564-565`, `:621-622`, `:667-668`; `tools/notes.cpp:59-60`, `:155-156`; `tools/shell.cpp:213`; `agent/fetch_url.cpp:303`; `agent/web_search.cpp:386`). The provider sees only the IR projection, `harness::Tool` (`data/contracts/types.h:108-115`): name, description and schema.
2. **MCP import.** `mcp::ToolInfo` (`business/mcp/types.h:68-75`) keeps one hint, `read_only_hint`. The parser reads `annotations.readOnlyHint` and nothing else (`types.cpp:93-97`), so a server's `destructiveHint`, `openWorldHint` and `title` are dropped. `Registry::register_into` turns the hint into `writes = !(readOnlyHint == true)` (`registry.cpp:104`).
3. **MCP serve.** `mcp::ServedTool` (`serve_stdio.h:23-28`; name, description, JSON schema, `read_only`) for the registry's read-only tools (`serve_stdio.cpp:88-106`, which drops every `writes` tool). `ServedRead` (`serve_stdio.h:48-53`) is a fourth struct with its own name, description and schema, and `ServedCall` is the same struct under another name (`:74`). A call differs from a read only by the `read_only` boolean `with_calls` hands the shared body, `grown` (`serve_stdio.cpp:136-137`, passed `false` at `:186-188`). `tools/list` writes `annotations` with `readOnlyHint` only (`serve_stdio.cpp:58`).
   - **The probe:** `tools/list` returned 22 tools, each with keys `annotations`, `description`, `inputSchema`, `name` and annotations exactly `{"readOnlyHint": true}`. `complete` (54b) is listed with `readOnlyHint: false` and no `destructiveHint`. MCP's default for a missing `destructiveHint` is `true`, so a host reads a bounded local model call as possibly destructive. Apogee's own import would gate it as a writer (`registry.cpp:104`).
4. **The ACP mapping.** A separate table, `KindRow` (`presentation/acp/translate.h:73-80`; rows at `translate.cpp:61-90`). It keys each native tool's ACP kind, and whether its target is a path, on the tool's name. A unit test keeps the table in step with the registry (`tests/presentation/acp/translate_test.cpp:60-81`). A tool with no row is `other`, so every MCP tool is `other` even when its server says it only reads (`translate_test.cpp:88`). The kind is looked up by name in three places: live cards (`translate.cpp:274`, `:277`) and a resumed session's replay (`:732`).
5. **A fifth list, in `check`.** Beyond the four shapes, `check` keeps a fifth list by name: its `permissions.<tool>` rows come from `tools::destructive_tool_names()`, a hand-kept array of six names (`tools/toolsets.cpp:28-29`, read at `presentation/cli/check.cpp:1374`, `:1401-1405`), each `ask (not listed; the default)` when the config names none. A unit test holds the array to the `writes` flags (`tests/business/tools/gate_test.cpp:170-191`).
6. **The served status reads** (`models_list` … `check`) have no gate flags at all.
7. **The training kit** `tool-calling` keeps its own copies of the fs, shell and git declarations, held to the registry by `tests/business/training/role_kits_test.cpp`.
8. **The gap.** No one type says, for any tool: what it is, what it may do, what the gate does with it, and where it runs.

## Acceptance criteria

- [ ] **Given** a scratch home, **when** `apogee mcp serve` answers `tools/list`, **then** it gives the same 22 tools in the same order with the same `readOnlyHint`, and each now carries `destructiveHint` and `openWorldHint`; a listed `complete` reads `readOnlyHint: false, destructiveHint: false`.
- [ ] **Given** an `apogee acp` session, **when** it draws tool cards, **then** every native tool has the kind it has today, and `translate.cpp` has no `KindRow` table.
- [ ] **Given** a scripted MCP server, **when** its tools are imported, **then** they come in with their four hints and title; a tool marked read-only is ungated, and one unmarked, or marked `destructiveHint: false` only, still asks.
- [ ] **Given** the change, **when** `apogee __machine-schema` runs, **then** it carries `$defs/tool_definition` under a new schema version, and every native tool's definition validates against it.
- [ ] **Given** the mock backend, **when** a hermetic `chat --tools` turn runs before and after the change, **then** the provider is sent byte-identical tool definitions, and no pairing, network or server is touched.
- [ ] tools.md has the chapter; the full suite is green.
- [ ] Optional, and said if skipped: in Claude Code, a registered `apogee-dev mcp serve` lists the same tools with the new hints.

## Constraints

- **The anti-goal: nothing here makes standalone use need a server.** The CLI alone and the Mac app over pipes stay complete and stay the default (the user's call, 2026-11-07). Every tool that exists today is defined as running in **the harness**: the process whose loop calls it. That is the CLI itself when it runs alone. Building a definition reads no network, no pairing and no account. Nothing in this item dispatches anywhere new.
- **Additive** ([ADR 0006](../../../adrs/cli/backwards-compatibility.md)). No tool is renamed, reordered, removed or re-described. The new MCP annotation keys and the new `$defs` entry in the machine schema are fields an older peer ignores. A saved chat's tool names resolve as they do now.
- **What a model is shown does not change.** The IR projection (name, description, schema) stays exactly as it is, and `agent::definition_hash` (`tool.h:175`, 26g) still hashes only those three. Cached tool vectors stay valid.
- **The gate decides as it does today.** The permission class is computed from the same facts `agent::gated` and `GateRequest`'s `outbound`/`edits` read. A table test proves the class matches the gate for every native tool. For an imported MCP tool, a missing or false `readOnlyHint` still means gated.
- **Secrets are unleakable** ([CLI CLAUDE.md](../../../assistant/cli/CLAUDE.md)). A definition describes a tool and has no field for how to start one. An MCP server's `command`, `args` and `env`, and a backend entry with its key, live in the config. A definition made from them has no slot to hold them, so serving it or sending it to a device cannot leak them.
- **Never listens; serving is the server's.** This item is plain data. It opens nothing, and a definition marked `device` is a declaration only. 60b, over [59b](../v0.2.3/machine-mode-over-the-network.md)'s authenticated connection, is the first to dispatch one.
- **Parity** ([ADR 0002](../../../adrs/cli/mode-parity.md)). One definition, read by every surface (chat, machine mode, `serve`, `mcp serve`, `acp`). No surface keeps its own table.
- **The four layers** ([ADR 0001](../../../adrs/cli/layer-enforcement.md)). The type sits in `agent/` (Business), which `mcp/`, `tools/` and the loop already link. `acp/` and `machine/` (Presentation) read it from above. `agent/` stays guarded: no backend, no surface.
- **UTF-8 where it enters** (CLI CLAUDE.md). An imported server's description and title are made valid UTF-8 as they are parsed.
- **The dev channel** ([CLI CLAUDE.md → Development runs on the dev channel](../../../assistant/cli/CLAUDE.md#development-runs-on-the-dev-channel)). Any live pass uses `apogee-dev` over `~/.apogee-dev`.

## Technical approach

Source paths under `lib/src/cli/source/`; test paths (`tests/…`) under `lib/src/cli/`.

| File | Change |
|---|---|
| `business/agent/tool_definition.h/.cpp` (new) | `ToolDefinition`, its fields, `to_json`/`from_json` and `validate` (below) |
| `business/agent/tool.h/.cpp` | `struct Tool : ToolDefinition`: the run closures stay on `Tool`, and `tool.name`-style code compiles unchanged. `ToolRegistry::tool_definitions()` sits beside `definitions()`, the IR projection, which is unchanged. `gated()` reads the class. |
| `business/tools/fs.cpp`, `notes.cpp`, `shell.cpp`, `git.cpp`, `graph_nav.cpp`, `rag_query.cpp`, `todo.cpp`, `consult.cpp`, `delegate.cpp`, `agent/fetch_url.cpp`, `agent/web_search.cpp`, `symphony/tools.cpp` | Each tool states its kind and path flag where it is made. The class and hints are derived from the flags it already sets, so no tool is classified twice. |
| `business/mcp/types.h/.cpp` | `ToolInfo` keeps all four MCP hints and `title`. |
| `business/mcp/registry.cpp` | `register_into` builds the definition (origin `mcp`, server named). |
| `business/mcp/serve_stdio.h/.cpp` | `ServedTool` becomes a `ToolDefinition`. `ServedRead`/`ServedCall` each carry a definition beside `answer`. `tools/list` writes `title` when one is set, and `readOnlyHint`, `destructiveHint` and `openWorldHint` from the hints. |
| `presentation/acp/translate.h/.cpp` | The `KindRow` table is retired. `Translator` is handed the session's definitions, and `tool_kind`/`tool_takes_path` read them, `other` for a name none holds (a replayed call to a tool no longer offered). |
| `presentation/machine/protocol.h/.cpp` | `$defs/tool_definition` joins `machine_schema()`, and `kMachineSchemaVersion` (`protocol.h:47`, `2026-11-01`) moves on. No event carries a definition yet; 60b's offer line is the first. |
| `business/tools/toolsets.cpp` | `destructive_tool_names()` keeps its signature and its six names, now read off the native definitions whose class is `edit` or `write` rather than a hand-kept array, so `check`'s rows come from the one type. |
| `presentation/cli/mcp_serve.cpp` | The status reads and calls (54a, 54b, and v0.2.2's 58b/58d as they land) are declared as definitions. |
| `lib/documentation/reference/cli/tools.md` ([tools.md](../../../reference/cli/tools.md)) | A new chapter, "One definition for every tool": the fields, the derivation table, and how each field maps to the IR, MCP's annotations, ACP's kind and the machine schema. |
| `machine-mode.md`, `acp.md` | A line each pointing at the new tools.md chapter. |

**`ToolDefinition`'s fields** (`business/agent/tool_definition.h/.cpp`):

- `name`, `title` (optional), `description`, `input_schema` (text, as today);
- `hints` {`read_only`, `destructive`, `open_world`};
- `permission` (`read` · `edit` · `write` · `outbound` · `listed`);
- `kind` (ACP's ToolKind words) and `target_is_path`;
- `runs` (`harness` · `device`);
- `origin` (`native` · `mcp` + server name · `served` · `symphony` · `device`).

It also has `to_json`/`from_json` (snake_case, Apogee's own JSON spelling) and `validate`, which checks the name and schema; 60b runs it on a device's offer.

**Reference.**
- In-house precedents: 54a/54b (`with_reads`, `with_calls`, the tool set that only grows); 56a (`KindRow`, `ToolCallReport`); 39b (`edits`, the accept-edits mode); 26g (`definition_hash`); 28g (the protocol vocabulary declared as data and printed by `__machine-schema`). The v0.2.2 siblings that add served reads and calls before this item lands: [58b](../../../assistant/cli/MILESTONES.md#2026-11-28--mcp-serve-graphs-and-knowledge-backlog-item-58b-the-server-serves-graphs-and-knowledge-records) (shipped 2026-11-28) and [58d](../v0.2.2/mcp-serve-opt-in-refresh.md).
- Downstream: [60b](device-tools.md) sends definitions marked `device` over the network. GUI [7b](../../darwin/macos/v0.2.4/mac-offers-its-files-and-shell.md) writes the Mac's in Swift against the schema `$def`. [61a](../v0.2.6/sessions-as-training-data.md) can read a recorded call's class and hints from the definition, not from a name.
- External prior art: MCP's tool annotations at `2025-03-26`, the version the client speaks (`mcp/types.h:23`): `title`, `readOnlyHint`, `destructiveHint`, `idempotentHint`, `openWorldHint`, all hints and never trusted for safety. The definition keeps three of the four hints. `idempotentHint` is left out, because no Apogee rule reads it. ACP's ToolKind words are the set `translate_test.cpp:69-70` holds.

## Testing

Hermetic: a temp `APOGEE_HOME`, the mock backend, scripted MCP servers.

- **`tests/business/agent/tool_definition_test.cpp`** — the JSON round trip, `validate`, and the derivation table.
- **`tests/business/agent/tool_test.cpp`** — `definition_hash` pinned to its pre-change value for a planted tool, so the model's view cannot drift.
- **`tests/business/tools/toolsets_test.cpp`** — the definition table over every native tool (with `child_processes` on): each has a kind from ACP's set; its class matches `gated()`, `edits` and `outbound`; each is `runs: harness`. A new tool with no kind fails, as a missing `KindRow` does today.
- **`tests/business/mcp/types_test.cpp` and `registry_test.cpp`** — the four hints parsed, and the class for true, false and missing.
- **`tests/business/mcp/serve_stdio_test.cpp`** — the annotations written.
- **A `tools/list` golden** over the probe's scratch home — 22 names in the same order, each `readOnlyHint` unchanged.
- **`tests/presentation/acp/translate_test.cpp`** — today's kind expectations at `:82-88`, unchanged.
- **`gate_test.cpp`** — its "every destructive native tool is gated" case unchanged, and `check`'s `permissions.<tool>` rows the same six names.
- **`tests/presentation/machine/machine_mode_test.cpp` and `cli.machine_schema_conformance`** — the new `$def`, and every native definition validating against it (the schema conformance case).
- **`cli.mcp_lifecycle`** — an external client reads the new hints.
- **A secret never leaks** — a planted MCP server whose `env` holds a secret: the secret never appears in any definition's JSON or in `tools/list`.
- **`harness.layering`** — green, with `agent/` still guarded.

## Open questions

| Question | Status | Answer |
|---|---|---|
| Does `Tool` inherit `ToolDefinition` or hold it as a member? | `[default]` | `Tool` inherits `ToolDefinition` rather than holding it as a member. Every `tool.name`, `tool.description` and `tool.parameters_schema` keeps compiling, and the diff is the new fields alone. |
| What values does `runs` take? | `[default]` | `runs` is `harness` or `device`, not `server`. A tool runs in the process whose loop calls it: the CLI alone, `serve`, or `acp`. "Server" would wrongly suggest that today's tools need one, which is the anti-goal. |
| Are the hints declared or derived? | `[default]` | The hints are derived, not declared. For a native tool, `read_only = !writes`, `destructive = writes` and `open_world = outbound`. A served read is read-only. A served call is `read_only: false, destructive: false, open_world: false`, which fixes `complete`'s implied destructiveness. An imported tool keeps what its server said, with MCP's defaults where it said nothing. |
| What are the permission classes, and when does each apply? | `[default]` | `read` (never asks), `edit` (`edits`), `write` (any other writer, and any imported tool not marked read-only), `outbound` (asked per host) or `listed` (a served call: allowed only where the config lists it, 54b's rule, never prompted). An imported tool's `destructiveHint: false` does not lift its gate. |
| What ACP kind does an imported MCP tool get? | `[default]` | An imported MCP tool's kind stays `other`, as today. MCP has no kind, and guessing one from a hint would change cards ACP hosts already draw. |
| How is the JSON form spelled, and what does `tools/list` gain? | `[default]` | The JSON form is snake_case like the machine protocol. The MCP edge maps it to camelCase. `tools/list` keeps its names, order and descriptions, and gains `destructiveHint` and `openWorldHint` (and `title` where one is set). |
| Is there a new verb to list the definitions? | `[default]` | No new verb. The published form is the `$def` in `apogee __machine-schema`, and the definitions show up through `mcp serve`'s `tools/list` and `acp`'s cards. A user-facing "list my tools" read would be its own item, with its shell classification (ADR 0010). |
| Who owns the device's offer line, the dispatch to a device, and a device tool's gate? | `(consumed decision)` | [60b](device-tools.md) |
| Who owns the Mac's definitions in Swift? | `(consumed decision)` | GUI [7b](../../darwin/macos/v0.2.4/mac-offers-its-files-and-shell.md) |
| Who owns the training kit's copied declarations, and any training use of the class? | `(consumed decision)` | [61a](../v0.2.6/sessions-as-training-data.md) |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-07 | Specced from the Apple-platforms spike and brainstorm. The user's calls: (1) the vision: Apogee as a private, self-hosted harness that feels like Claude on every device the person owns, mimicking frontier harnesses with local models; (2) no customer accounts, billing or metering; (3) where the work happens is option C: server tools by default, and a connected desktop offers its own files and shell. When the Mac points at a server and is asked to change a project on the Mac, the Mac's files are the ones touched; (4) remote use is an add-on, never the only way Apogee runs. The CLI alone, and the Mac app over pipes to a local CLI, stay complete and the default; anything that makes either need a server, a network or an account is a failure; (5) iPhone and iPad are clients of the person's Apogee server only, with no on-device core; the Mac and the CLI stay standalone. So the definitions marked `device` that 60b carries are a connected desktop's own files and shell, touched on that desktop; iPhone and iPad offer none; (6) the common tool definition lands before v0.2.5. Track 60, v0.2.4. | the user |
| 2026-11-07 | Ungated, and first in track 60. The definition improves today's surfaces alone: MCP hosts get honest hints, and ACP's kinds come from the tool rather than a side table. 60b, the first dispatch to a device, needs it, so it sits in v0.2.4 beside the device tools, not in v0.2.3's network track. | the spec |

## Out of scope

- Any dispatch to a device.
- Any new tool.
- A tool list in the machine mode's `session` event.
- Migrating the training kit's copied declarations (61a).
- The served HTTP plane's refusal of client-side tools (`httpserver/handler.cpp:648-652`), which stays as it is.

## Links

- **Blocked by:** — · **Blocks:** [60b](device-tools.md) (🔒 60a, 59b; builds on this item), [60c](app-tools-on-every-door.md)
- **Follow-on:** [60b](device-tools.md) (device dispatch), GUI [7b](../../darwin/macos/v0.2.4/mac-offers-its-files-and-shell.md) (the Mac's definitions in Swift), [61a](../v0.2.6/sessions-as-training-data.md) (class and hints for recorded calls)
- **Relates to:** [59b](../v0.2.3/machine-mode-over-the-network.md) (the authenticated connection 60b dispatches over); [58b](../../../assistant/cli/MILESTONES.md#2026-11-28--mcp-serve-graphs-and-knowledge-backlog-item-58b-the-server-serves-graphs-and-knowledge-records) (shipped 2026-11-28) and [58d](../v0.2.2/mcp-serve-opt-in-refresh.md) (served reads and calls); 54a/54b, 56a, 39b, 26g, 28g (precedents); [ADR 0001](../../../adrs/cli/layer-enforcement.md), [ADR 0002](../../../adrs/cli/mode-parity.md), [ADR 0006](../../../adrs/cli/backwards-compatibility.md), ADR 0010; [tools.md](../../../reference/cli/tools.md)
