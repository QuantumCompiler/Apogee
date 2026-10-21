# Agents, MCP and symphonies: the library resources' screens

**What / why.** Three resource families with no GUI place (the 2026-10-20 audit; the TUI has had all three as views since 37d/37g): **Agents** — the shipped seven and the user's own, listed and shown from their documents, created through the one scaffold; **MCP** — the servers, the presets (`mcp presets`, `create --from`), connect-state honesty; **Symphonies** — the catalog and shows, and **a play reaching the session** (the TUI's `p` key drives the session's own `/play`; the GUI's session window is the same analog — a play launched into the conversation, its stages as side activity). Editing is the one wrinkle: the CLI's `agents edit`/`symphonies edit` open `$EDITOR` (the TUI's recorded refusal); the GUI edits **natively** — the definition file opened in an in-app editor sheet, written back through the same file the CLI reads, validated by running the CLI's own show/validate after save (read back, don't mirror).

**Core constraint(s).** ADR 0001/0002; creates through the scaffold-backed commands, byte-identical to hand-typed ones; the in-app editor writes the file and then **asks the binary** whether it still parses (the show command's refusal relayed verbatim — never a GUI-side parser); plays run within a session's turn exactly as the protocol carries them; MCP keys/commands render metadata-only (the secrets law). **Design boards land with the screens.**

**Seam + files.** `source/presentation/agents/`, `source/presentation/mcp/`, `source/presentation/symphonies/` (+ view-models), the session module grown for plays, `source/business/reads/`; `lib/design/spec/` — the boards and contract rows.

**Reference.** The TUI's 37d/37g — the three views' cut, the play-through-the-session shape, the metadata-only keys; 47c — the presets consumed; the scaffold (one create core) — the byte-identity bar.

**Decisions made** (dated):
- 2026-10-20 — From the parity audit (the user's mandate); native in-app editing supersedes the `$EDITOR` carve for the GUI (a GUI has its own text surface — the file stays the contract, the binary stays the validator).

**Open calls:**
- [default: the editor sheet's scope (the definition file as text, no structured form yet) and the boards' layouts at build.]

**Guardrail(s).** View-model matrices over fixture documents; create byte-identity pins against hand-typed commands; the editor's save-then-validate loop over a scripted binary (a broken save's refusal shown verbatim, the file restorable); play flows over the scripted session; hermetic.

**Acceptance criteria:**
- [ ] The three screens list/show/create over the real commands; a definition edits in-app, validated by the binary, refusals verbatim.
- [ ] A symphony plays into the live session with its stages narrated; MCP shows presets and registers one byte-identically; the full suite is green.

**Scope note.** Earmarked for v0.2.0 (track 2); gated on [`parity-law-and-table.md`](parity-law-and-table.md) (2a). Out of scope: a structured definition form (text first), orchestrate mode (the execute item's), any scaffold change.
