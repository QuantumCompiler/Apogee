# Tool selection by relevance

**What / why.** Each turn advertises the tools that bear on the question, not every tool registered. The native toolsets alone are about twenty tools, and each MCP server adds its own; Ommi's Atlassian server alone brought dozens (MCP.md). Every definition costs prompt tokens on every step of the loop, which is ~100 tokens/s of reading on a local 27B model. And a small model chooses worse from a long menu: Ommi's own documentation advises routing tool-heavy sessions to a stronger backend by hand. Selecting the relevant few makes a small model both faster and more accurate, and lets a user connect many MCP servers without paying for all of them on every step.

**How it works.** Each tool's name and description is embedded once, by the `embedding` role, and cached by a hash of the definition. Per turn, the question (rewritten into a standalone form by the [utility model](helper-model-roles.md) when it depends on earlier turns) is compared with those vectors. The top few are advertised, alongside a small always-on core and any tool already used this turn. The model can widen its own view with a `find_tools` meta-tool, which searches the whole registry and adds what it returns to the next step. With no embedding model, BM25 over the descriptions does the ranking.

**Core constraint(s).**
- **One shared loop.** Selection happens in `agentloop/` for every surface; an agent's tool policy (Milestone X's filter: `read-only`, `all`, `none`) is applied first, and selection only ever narrows what the policy allows.
- **The gate is unaffected.** A selected tool is dispatched and gated exactly as before; a tool the model names without it being advertised is still dispatched if registered and allowed, and gated, rather than failing, because models remember tools from earlier steps.
- **Reported.** `--verbose` shows which tools were offered and why; machine mode's events are unchanged.
- **Off below a threshold.** With few tools the whole registry is cheaper than a selection pass, so nothing changes for a small registry.

**Seam + files.**
- `agent/tool.h`: `ToolRegistry` gains a definition hash per tool.
- `agentloop/tool_selection.h/.cpp` (new): ranking, the always-on core, and `find_tools`, with the embedder arriving as a closure (the guarded-package rule).
- `agentloop/loop.cpp`: `advertised_tools` narrows per step.
- `commands/helpers.cpp`: the embedder closure wired from the `embedding` role, and the cache under `cache/` (safe to delete, as that layout row promises).

**Reference (Ommi).** No mechanism; Ommi's MCP.md recommends a stronger backend for large tool surfaces.

**Decisions made:**
- 2026-09-25 — Asked for by the user ("Reliability").
- 2026-09-25 — After [local tool calling](../assistant/MILESTONES.md#milestone-j--local-inference) (shipped 2026-09-25): selection matters most where tools cost the most, and before that item local models see no tools at all.

**Open calls:**
- [default: selection starts above 16 registered tools, offering the top 8 plus the core] Tuned against the six-task battery on both acceptance models, with the numbers recorded.
- [default: the always-on core is `read_file`, `list_directory`, `run_command` and `find_tools`] The tools almost every task begins with.
- [default: `find_tools` returns definitions, and the loop adds them to the next step] Rather than dispatching them unseen.

**Guardrail(s).**
- Ranking table-tested against scripted embeddings.
- The policy filter is applied before selection.
- A named but unadvertised tool is still dispatched and gated.
- The threshold leaves a small registry untouched.
- The cache is invalidated when a definition changes.
- `find_tools` widens the next step.
- Each mutation-tested.

**Acceptance criteria:**
- [ ] With the native toolsets plus two MCP servers (40+ tools), the six-task battery passes on Qwen3-VL-8B, with fewer prompt tokens per step than advertising everything (measured and recorded).
- [ ] A question needing an MCP tool outside the top few is answered after one `find_tools` call.
- [ ] With 12 tools registered, requests are byte-identical to today's.

**Scope note.** Item **26g**; gated on nothing pending (25b shipped 2026-09-25). Out of scope: learning from which tools were actually used.
