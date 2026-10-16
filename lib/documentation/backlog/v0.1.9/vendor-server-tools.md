# Vendor server-side tools: the proprietary edge, behind consent

**What / why.** Most of Claude Desktop's and ChatGPT's capability edge is not MCP at all — it is the vendors' **server-side tools**: web search, code execution and their kin, enabled as request blocks on the vendor's own API and executed on the vendor's infrastructure (the built-in-MCP parity spike, 2026-10-16). Apogee's wires carry none of them today, and the spike's reframe holds the shape: MCP servers are provider-neutral by design (the host translates — Apogee's wire translators already do), so **the genuinely provider-specific work lives in the backends**, one vendor's blocks per `*_wire`. This item ships them: a capability fact per provider (which server-side tools its API offers), the request blocks in each wire — **Anthropic first** (its web search and code execution blocks), OpenAI and Google where their APIs carry equivalents — every one **off by default** and enabled per backend entry in config, each use narrated as a tool line like any other. The policy was the spike's W4, settled by the user (2026-10-16) and recorded as the dated SPEC principle: *enabling one is recorded consent for that vendor to browse or execute on the user's behalf under the user's key; the host gate's law is unchanged for every call Apogee itself makes* — because a vendor-side search reaches sites the gate never sees, and pretending otherwise would be the dishonesty the gate exists to prevent.

**Core constraint(s).** **Default-off is structural**: with nothing in the config, no wire emits a server-tool block — the golden suites pin the absent case byte for byte. **No type switches**: which tools a provider offers crosses as plain data / a capability fact (the house idiom), and the loop never names a vendor. **Consent is per backend entry**, written through the one editor (the `set-backend` field family, 46b's transforms — `(consumed decision)`: the verb and its rules are shipped); the enabling key is a visible config line a person can read. **Narrated, never silent**: a vendor-executed tool use and its result are said through the Reporter as tool lines in every front end, and machine mode carries them in the existing tool-event vocabulary (documented in the same change; `cli.machine_schema_conformance` holds it). **Carried vendor-tool lists are reviewed**, ADR 0009's discipline: each wire's known server-tool set carries its reviewed date, checked when the backend is touched. The secrets rules are untouched; nothing new is logged.

**Seam + files.**
- `data/contracts/` — the config unit (the per-entry server-tools list, parsed and written through the one editor) and the capability fact's crossing (plain data beside `ModelBehavior`, or a capability interface if the Harness must ask — the build's call within the probe idiom).
- `data/backends/anthropic_wire.cpp` (first), `openai_wire.cpp` / `google_wire.cpp` where their APIs carry equivalents — the blocks emitted only when the entry enables them; the streamed tool-use/result events mapped onto the existing tool narration.
- `presentation/cli/config_cmd.cpp` — the field on `set-backend` (46b's family) and its completion; `lib/documentation/reference/machine-mode.md` — the narration's note; `lib/documentation/assistant/CLAUDE.md` — the ⚠ host-gate section gains the consent sub-rule mirroring the SPEC principle (same change).
- Tests: FakeTransport goldens per wire (absent config → absent blocks, byte-stable; enabled → the vendor's exact block shape; the narration events in all four front ends; conformance green); the leak test unchanged; `cli.config_lifecycle` grown by an enable-and-say pass.

**Reference.** The spike (2026-10-16) — the reframe and W3/W4; the SPEC principle (the dated 2026-10-16 amendment this item enforces); ADR 0009 — the reviewed-list discipline copied; 46b — the field-edit family consumed; the capability-probe invariant — the crossing's shape.

**Decisions made** (dated):
- 2026-10-16 — The set lands in v0.1.9 as track 47 (the user's placement call); **vendor server-side tools are allowed, default-off, per backend entry** (the user's W4 answer), the SPEC principle recorded the same day.
- 2026-10-16 — Anthropic ships first; OpenAI and Google follow in the same item where their APIs carry equivalents, each wire's list carried with a reviewed date.
- 2026-10-16 — The reframe stands: provider-specific work is wire blocks in `backends/`; MCP servers stay generic (`(consumed decision)` — the siblings own that half).

**Open calls:**
- [default: the config spelling is a per-entry list (`server_tools: ["web_search", …]`), the exact key and values settled at build through the editor's golden suite; unknown names refused at parse naming the vendor's known set.]
- [default: a model-initiated server-tool use counts against the turn's budget as any tool round does; no separate cap in the first cut.]

**Guardrail(s).** The absent-case goldens (no config → no blocks, byte-identical requests); the enabled-case block shapes per vendor; narration in all four front ends plus machine-mode conformance; the reviewed-date check alongside ADR 0009's; the SPEC/CLAUDE consent wording present and linked; `cli.config_lifecycle`'s enable pass on the real binary.

**Acceptance criteria:**
- [ ] A fresh config sends no server-tool block to any vendor, byte-stable against today's goldens.
- [ ] `config set-backend <key> --server-tools …` (the 46b family) enables them per entry; the wire emits the vendor's exact blocks; uses narrate as tool lines everywhere, machine mode included.
- [ ] The SPEC principle and the ⚠ sub-rule are in place, dated; the carried lists bear reviewed dates.
- [ ] The full suite is green.

**Scope note.** Earmarked for v0.1.9; first of track 47, ungated. Out of scope: MCP anything (the siblings'), image generation and connectors (the spike's recorded outs), any change to the host gate's own law.
