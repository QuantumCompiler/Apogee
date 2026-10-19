# MCP presets: the box says what it holds

**What / why.** A fresh install configures **zero MCP servers**, and the only guidance is one line ("Create one with: apogee mcp create <name>") pointing at a scaffold — measured 2026-10-16, the built-in-MCP parity spike's W1 — while `mcp create --command` can register anything and the box now ships a server worth registering (47b's code-execution server, shipped 2026-10-18 — [Milestone W](../../assistant/MILESTONES.md#milestone-w--the-mcp-client)). This item makes the built-ins discoverable and one command away: **`apogee mcp presets`** lists the shipped presets with what each provides (human table and JSON document, the 28h idiom), and **`apogee mcp create <name> --from <preset>`** registers one — the exact command-and-args the preset declares, through the existing create path, nothing bespoke. The first presets: **`code-exec`** (the bundled interpreter — the one-command registration 47b documents by hand) and **`self`** (the `apogee __mcp-tools` server — listed with its copy-paste registration *for another host*, since the spike measured it answering the handshake with 17 tools; it is never registered into Apogee itself, which already owns those tools natively). The `mcp list` empty state names `mcp presets`. The reference doc (`tools.md`) gains the chapter — including the SearXNG setup pointer, the spike's W5 setup half, since search stays user-owned by the standing default.

**Core constraint(s).** A preset is **data, compiled in** — name, description, the command and args it registers, what it provides — never a second registration path: `--from` resolves the preset and calls the same create core a typed `--command` calls, so the written config is byte-identical to the hand-typed twin (the composition-pin idiom). `self` is **informational by design**: shown with its registration line for other MCP hosts, refused with the circularity reason if someone tries to `--from self` into Apogee. Reads have twins ([ADR 0002](../../adrs/cli/mode-parity.md)): the presets listing's JSON document, documented in machine-mode.md; the exec line covers the verbs (ADR 0010); completion offers preset names from the binary (ADR 0007, free). No community-server curation ships here — the spike's recorded out: pointing at third-party `npx` servers bakes dependencies Apogee doesn't own.

**Seam + files.**
- `business/mcp/` (or `cli/`, the build's call per the layering) — the preset table as compiled-in data (name, description, command template, provides), the `--from` resolution.
- `presentation/cli/mcp_cmd.cpp` — `mcp presets` (+ `--output-format json`), `--from` on `create`, the empty-state line on `mcp list`, the circularity refusal for `self`.
- `lib/documentation/reference/machine-mode.md` — the document's row; `lib/documentation/reference/tools.md` — the presets chapter and the SearXNG pointer.
- Tests: the composition pin (`--from code-exec`'s config byte-equal to the hand-typed `--command` twin); the listing and its document; the `self` refusal; the empty state; completion of preset names; `cli.config_lifecycle` grown.

**Reference.** The spike (2026-10-16) — W1's zero-servers measurement and the 17-tool handshake; 47b's code-execution server ([Milestone W](../../assistant/MILESTONES.md#milestone-w--the-mcp-client)) — the first preset's server, `(consumed decision)` for everything about it: seeded at `training/scripts/code_exec_server.py`, its registration line composed by `cli/code_exec_server.h` (`code_exec_registration(home).mcp_create_arguments("code")` — the training environment's interpreter, else `python3` on PATH, else the reason there is none, which is this item's not-runnable detail); M12 — the composition-pin test style copied; 28h — reads as documents.

**Decisions made** (dated):
- 2026-10-16 — The set lands in v0.1.9 as track 47 (the user's placement call); SearXNG stays user-owned with the doc pointer easing setup (the standing default, confirmed by execution); connectors and image generation stay out (the spike's recorded outs).
- 2026-10-16 — Presets are compiled-in data over the one create path — never seeded files, never a second writer (the simplest ladder that cannot drift).
- 2026-10-16 — `self` is listed for other hosts and refused into Apogee, with the reason.
- 2026-10-18 — The first wave is exactly two presets (`code-exec`, `self`); a community-server catalog is a later deliberate question, recorded here (the default, confirmed 2026-10-18).
- 2026-10-18 — A preset whose command is not runnable on this machine still lists, its detail naming what is missing — the providers-scan honesty idiom (the default, confirmed 2026-10-18).


**Guardrail(s).** The composition pin; the document's conformance; the `self` circularity refusal; the empty-state line; the not-runnable detail against a faked environment; byte-stability of `mcp create` without `--from`.

**Acceptance criteria:**
- [ ] `apogee mcp presets` lists the two presets with documents; `mcp list`'s empty state names it.
- [ ] `mcp create code --from code-exec` writes a config byte-identical to the hand-typed registration and the server works end to end.
- [ ] `--from self` refuses with the circularity reason; the preset still lists its registration line for other hosts.
- [ ] tools.md carries the chapter and the SearXNG pointer; the full suite is green.

**Scope note.** Earmarked for v0.1.9; gated on 47b (the first preset's server) — **satisfied 2026-10-18** ([Milestone W](../../assistant/MILESTONES.md#milestone-w--the-mcp-client)). Out of scope: community-server curation, seeded preset files, any new server beyond the two listings.
