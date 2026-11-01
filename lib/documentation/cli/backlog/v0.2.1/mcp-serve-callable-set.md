# The callable set: local model calls served, opt-in per tool

**What / why.** The served set is read-only by design — a model call is not a read, and any MCP host the user registers Apogee into could otherwise spend GPU time without a recorded consent. The mod's headline feature needs exactly one such call: **`complete`** — Claude handing a prompt to a local backend (`apogee complete -m <backend>`, measured one-shot by the mod spike, 2026-10-21). This item serves it **opt-in per tool** (the user's call, 2026-10-21, at the spike's question — the 47a consent discipline pointed outbound): a config unit listing which callable tools the server offers (empty by default; written through the one editor; `complete` the first), each eligible only when its backend's provider is **local and unmetered by its own word** (the orchestrate rule's test reused — never a type list), each call **bounded** (a served max-tokens default, named in the tool's description) and narrated on the server's captured-stderr discipline. The tool's description tells the host's model the truth: which backends it may name, the bound, and that spend is local. `search_documents` is verified in-item against the full retriever-resolver query (the spike left it an open verification) and grown to it if chunk-only.

**Core constraint(s).** **Consent is config, never a flag a host can pass**: the server offers only what the entry lists; an unlisted callable tool is absent from `tools/list`, not refused at call. **Eligibility is the provider's word** (metered or unknown = ineligible, said at config write). **The one calling path**: the served complete runs the same core `apogee complete` runs — no second inference path in the server. **Honesty in the tool contract**: bounds and backend set in the description, errors in the CLI's words.

**Seam + files.** `data/contracts/` + the editor (the serve-tools unit, 47a's shape mirrored), `business/mcp/serve_stdio.*` (the callable tools over the existing cores), `presentation/cli/` (the config verb's completion), tools.md's serve chapter grown. Tests: the opt-in matrix (absent by default; listed = served; a metered backend refused at write naming the rule); the bound; the call over the mock backend; the retriever-query verification.

**Reference.** The mod spike (2026-10-21) — the gap and the user's opt-in answer; 47a — the consent-as-config discipline, outbound; 27t's unmetered-by-the-provider's-word rule; Milestone W — the server grown, not forked.

**Decisions made** (dated):
- 2026-10-21 — **Opt-in per tool** (the user's call at the spike): callable tools are absent until config lists them; local-and-unmetered providers only.

**Open calls:**
- [default: the config unit's spelling and the served bound's default at build; `complete` the only callable in this item's scope.]

**Guardrail(s).** The opt-in matrix over the mock backend; the eligibility refusal; the bound pin; the description golden; no network, no weights.

**Acceptance criteria:**
- [ ] With nothing listed, `tools/list` is the read set; listing `complete` serves it against eligible backends only, bounded, the description honest.
- [ ] A metered or unknown-cost backend cannot be listed (refused at the editor, naming the rule); `search_documents`' retriever story verified; the full suite is green.

**Scope note.** Earmarked for v0.2.1 (track 54); its gate, 54a, shipped 2026-11-01 ([Milestone W](../../assistant/MILESTONES.md#milestone-w--the-mcp-client): `apogee mcp serve` public, the status reads served through `mcp::with_reads`). Out of scope: further callables (each a deliberate later listing), the forge tools, adapters arguments (the 53 follow-on, recorded).
