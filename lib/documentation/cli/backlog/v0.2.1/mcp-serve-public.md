# `mcp serve`: the hidden server made a public mode

**What / why.** Apogee's MCP stdio server exists and works — the mod spike (2026-10-21) handshook `apogee __mcp-tools` and it answered as `apogee-tools 0.1.9` with **17 read-only tools** (file, git and notes reads; `search_documents` and the collection reads; the four graph-navigation tools), and `apogee mcp presets`' `self` entry already prints the host registration JSON — but it is **hidden plumbing**: classified "a protocol, not a page" in the parity table, no help, no completion, no documentation beyond the presets line, and the served set lacks **the status reads** another host's user would want (the models, train-status, train-versions and check documents — each measured live as `--output-format json`, none served as a tool). This item makes it a mode: **`apogee mcp serve`** — the public verb (the hidden spelling kept as an alias for existing registrations), with help, completion (ADR 0007) and its parity classification; **the status reads join the served set** as read tools returning the CLI's own documents byte for byte (the 27l discipline: served with no code of their own); `mcp presets`' `self` repoints to the public spelling; and the documentation lands where integrators read — `tools.md` gains the serve chapter, the reference notes the tool set and its additive growth (ADR 0006's law restated for the server's tools).

**Core constraint(s).** **Still pipes, never a port** (the never-listens law — stdio only; the standing scans already cover the binary). **Read-only stays the default truth**: this item adds reads alone; anything model-calling is [`mcp-serve-callable-set.md`](mcp-serve-callable-set.md)'s consented territory. **One implementation**: `serve_stdio` grows; the new tools are the existing read cores served byte-for-byte — a second rendering of a document is the drift 27l's shape exists to prevent. **Additive for existing hosts**: the hidden spelling keeps working; tools only added, never renamed.

**Seam + files.** `business/mcp/serve_stdio.*` (the read tools added), `presentation/cli/` (the public verb registration, help, the parity row and refusal text updated from "hidden plumbing" to the mode's), `operations/mcp_presets` (`self` repointed), `lib/documentation/cli/reference/tools.md` (the serve chapter). Tests: the handshake suite grown (the new tools listed and answering with the documents byte-equal to the CLI's own), the alias pin, the presets golden, completion.

**Reference.** The mod spike (2026-10-21) — the live handshake, the 17-tool list, the measured documents; Milestone W — `serve_stdio` and the 27l served-byte-for-byte discipline; ROADMAP's Forge-integration idea — the recorded anticipation of this server growing beyond Apogee's own tools.

**Decisions made** (dated):
- 2026-10-21 — Specced from the mod spike (the user's call); the server stays read-only here, the callable set its own consented sibling.

**Open calls:**
- [default: the status-read tool names (`models_status`, `train_status`, …) in the server's naming style; the alias kept indefinitely.]

**Guardrail(s).** The handshake suite (list + call per new tool, documents byte-equal); the alias and presets pins; the parity row; no listening socket (the standing pair of scans unchanged).

**Acceptance criteria:**
- [ ] `apogee mcp serve` answers the handshake with the grown set; the hidden spelling still works; `mcp presets` prints the public line.
- [ ] Each status read returns the CLI's own document byte for byte; tools.md documents the mode; completion and classification land; the full suite is green.

**Scope note.** Earmarked for v0.2.1 (track 54, the mod's CLI siblings); ungated. Out of scope: model-calling tools (54b), the forge tools (the recorded ROADMAP idea), any transport beyond stdio.
