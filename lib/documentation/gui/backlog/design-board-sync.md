# Design board corrections: the contract ledger catches up with the repo

**What / why.** The GUI spike (2026-10-20) verified the design's contract board against the shipped repo and found the board itself needs three corrections, by its own update law (`lib/design/spec/CLAUDE.md`: every change lands in the repo files and the canvas together): **(1)** the board's decision 8 says "macOS 14 or later" — superseded by the shipped, groomed decision (macOS 27+, Apple silicon only, 1a); **(2)** the spike resolved two of the board's four "verify" rows into **new gaps** the ledger should carry: no forget-recall command (G16) and the untyped `{name, fields[]}` model-set document with no window/cache fields on a safetensors set (G17) — both now owned by the CLI's [`doctor-config-growths.md`](../../cli/backlog/v0.2.0/doctor-config-growths.md); **(3)** the "verify" rows that passed (suites readable; lineage present as prose) flip to their settled statuses. This item lands those edits in `html/BuildContract.dc.html` (the decision line, the gap ledger G16/G17 with their fallbacks and owners, the verify rows settled) and republishes the changed files to the canvas per the board's own repo↔canvas law.

**Core constraint(s).** The design CLAUDE's laws whole: edit here then publish; `canvas.json` only if boards change (they don't — content edits only); never send `support.js` or the `.md` files; the board's honesty bar (gaps carry fallbacks and the app's behavior until they land). Nothing else on the board moves — gap statuses flip to "landed" only when their CLI items actually ship, each in that ship's own docs pass.

**Seam + files.** `lib/design/spec/html/BuildContract.dc.html` (the three corrections); the canvas publish of that one file. Tests: the design's own node health check still passes (the board has no logic, but the law is run-it-anyway); the repo↔canvas byte-identity by the publish flow.

**Reference.** The GUI spike's probes (the G16 sweep, the G17 measurement); 1a's shipped macOS decision; `lib/design/spec/CLAUDE.md` — the update law this follows.

**Decisions made** (dated):
- 2026-10-20 — macOS 27+/arm64 stands (the shipped decision); the board's 14 is corrected, not revisited (the standing default, veto-able).
- 2026-10-20 — G16 and G17 join the ledger with their CLI owners named; verify rows settle to what the probes measured.

**Open calls:**
- [default: the gap rows' wording in the board's own voice; the canvas publish per its flow.]

**Guardrail(s).** The node health check green after the edit; the corrected lines quoted in the ship record; no other board content moved.

**Acceptance criteria:**
- [ ] The board says macOS 27+, carries G16/G17 with fallbacks and owners, and its settled verify rows; the canvas matches the repo byte for byte.
- [ ] Nothing else on the board changed; the health check passes.

**Scope note.** Earmarked for v0.2.0; ungated, small. Out of scope: flipping gap statuses as CLI items ship (each ship's own docs pass), any design change beyond the corrections.
