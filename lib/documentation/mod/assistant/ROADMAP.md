# Apogee Mod — Roadmap

The lifecycle is the house's: chat → here (and [SPEC.md](SPEC.md) on shape changes) → [`backlog/`](../backlog/README.md) → built from the document → [MILESTONES.md](MILESTONES.md). **Version scheme:** repo-wide (the CLI docs record the law); the mod's themes ride the releases.

---

## Shipped releases

*(Nothing shipped yet.)*

---

## In progress

*(Nothing in progress — v0.2.0 is the repo's in-development release; the mod's first track targets v0.2.1.)*

## Up next

### v0.2.1 — Track 1, the mod is born

Opened 2026-10-21 (the user's call, from that day's mod spike; home, layers and placement the user's same-day answers). The spike's ground: the MCP server already answers (17 read-only tools, the registration line printed by `mcp presets`), every pane datum is a JSON document, `complete -m` runs one-shot, and the plugin toolchain works on the dev machine — so the mod is presentation over existing contracts, with the CLI's track 54 (`mcp serve` public; the opt-in callable set) as its cross-queue sibling.

- [ ] **The mod scaffold** *([1a](../backlog/mod-scaffold.md))* — the plugin skeleton, the four layers with the layering check at birth, binary discovery and the not-installed guard, tsc/validate wiring, `changed.sh mod`.
- [ ] **Tools, slash commands, the status line** *([1b](../backlog/mod-tools-and-commands.md))* — the server's reads registered, `apogee_complete` (CLI now, 54b's served tool when present), the `/apogee` set over the JSON documents, the async long-run pattern.
- [ ] **The pane** *([1c](../backlog/mod-pane.md))* — backends, models, training and the ledger over the documents; promote/rollback with confirmation; the adapters column the follow-on (the CLI's track 53 shipped in v0.2.0).
- [ ] **The mod pipeline** *([1d](../backlog/mod-pipeline.md))* — its own workflow: `changed.sh`-gated, `tsc` the runner gate (validate where the CLI installs headlessly), validate/test the dev-host gates, the plugin folder the artifact.

## Fast follow

*(empty)*

## Unspecced ideas

- Adapters in the pane and an adapter argument on the tools — recorded with the spike; the CLI's track 53 (runtime LoRA), which they waited on, shipped in v0.2.0.

## Explicit non-goals

See [SPEC.md](SPEC.md) → Non-goals: **not Claude's chat model** · **no ports, no cloud sessions** · **no capability of its own**.
