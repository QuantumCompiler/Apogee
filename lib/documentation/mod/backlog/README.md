# Apogee Mod — Backlog

**The work queue: one document per work item** — the layer between [ROADMAP.md](../assistant/ROADMAP.md)'s board and [MILESTONES.md](../assistant/MILESTONES.md)'s record. An agent takes an item from the index and **implements it straight from its document**. (The CLI's queue is [`cli/backlog/`](../../cli/backlog/README.md), the GUI's [`gui/backlog/`](../../gui/backlog/README.md); an item needing a change in another application gets a sibling there, cross-gated by path.)

**Lifecycle, tags, gates and format** are the house's, exactly as the [GUI backlog](../../gui/backlog/README.md) states them: discussed → specced here (kebab-case, the standard sections) → taken (🚧) → shipped into MILESTONES and **deleted with its row**. Open calls are `[user]` (blocks the build) or `[default: …]`; owned-elsewhere decisions are `(consumed decision)`; Status is 🟢 / 🔒 *items* / 🚧 / ❌. Ids are ever-assigned: lettered tracks (`1a`), a new concern opening the next track number; a maintenance table is born with its first item, the CLI's conventions mirrored. Items live at the backlog root while the queue keeps one table; new tables get subdirectories with their first item.

## Index

Priority-ordered: the topmost item whose gate is satisfied is the next one to build.

**Track 1, the mod is born**, opened 2026-10-21 (the user's call, from that day's mod spike; home, the four-layer law and placement their same-day answers — `lib/src/mod/`, docs here, the skills grown to three homes the same day). The spike's measured ground: Apogee's MCP stdio server **already answers** (`apogee-tools 0.1.9`, 17 read-only tools incl. `search_documents` and the graph set; the host registration JSON printed by `apogee mcp presets`), every pane datum is a `--output-format json` document (`models list|status`, `train status`, `train versions`, `check` — exercised live), `complete -m` runs one-shot, and `claude plugin validate` works on the dev machine — so the mod is presentation over contracts that exist. The CLI's track 54 (the server made public; the opt-in callable set) is the cross-queue sibling, referenced, with fallbacks — never a gate. CI honesty is recorded in 1d: runners gate on `tsc` (validate where the `claude` CLI installs headlessly); `claude plugin validate`/`test` are dev-host gates. Gated 1a → 1b → 1c; 1d on 1a. No **[user]** call blocks any item.

| # | Item | Version | File | Status |
|---|---|---|---|---|
| 1a | The mod scaffold — the plugin skeleton with the four layers and the layering check at birth, binary discovery and the not-installed guard, tsc/validate wiring, `changed.sh mod` | v0.2.1 | [`mod-scaffold.md`](mod-scaffold.md) | 🟢 |
| 1b | Tools, slash commands, the status line — the MCP server's reads registered, `apogee_complete` (the CLI now, 54b's served tool when present), the `/apogee` set, the async long-run pattern | v0.2.1 | [`mod-tools-and-commands.md`](mod-tools-and-commands.md) | 🔒 1a |
| 1c | The pane — backends, models, training and the ledger over the documents; promote/rollback confirmed; adapters the follow-on (the CLI's track 53 shipped in v0.2.0) | v0.2.1 | [`mod-pane.md`](mod-pane.md) | 🔒 1b |
| 1d | The mod pipeline — its own `changed.sh`-gated workflow: `tsc` the runner gate, validate/test the dev-host gates, the plugin folder the artifact | v0.2.1 | [`mod-pipeline.md`](mod-pipeline.md) | 🔒 1a |
