# Apogee Mod — Roadmap

The lifecycle is the house's: chat → here (and [SPEC.md](SPEC.md) on shape changes) → [`backlog/`](../../backlog/mod/README.md), written as a ticket → built from the document → [MILESTONES.md](MILESTONES.md). **Version scheme:** repo-wide (the CLI docs record the law); the mod's themes ride the releases.

---

## Shipped releases

*(Nothing shipped yet.)*

---

## In progress

### v0.2.1 — Track 1, the mod is born

Opened 2026-10-21 (the user's call, from that day's mod spike; home, layers and placement the user's same-day answers). The spike's ground: the MCP server already answers (17 read-only tools, the registration line printed by `mcp presets`), every pane datum is a JSON document, `complete -m` runs one-shot, and the plugin toolchain works on the dev machine — so the mod is presentation over existing contracts, with the CLI's track 54 (`mcp serve` public; the opt-in callable set) as its cross-queue sibling.

- [x] **The mod scaffold** *(1a, shipped 2026-11-21 — [Milestone A](MILESTONES.md#milestone-a--the-mod-is-born), [DEVELOPER.md](DEVELOPER.md))* — the plugin skeleton, the four layers with the layering check at birth, binary discovery and the not-installed guard, tsc/validate wiring, `changed.sh mod`. Your live look (`claude --plugin-dir lib/src/mod` on Claude Code 2.1.326 or newer) is still to come.
- [x] **Tools, slash commands, the status line** *(1b, shipped 2026-11-21 — [Milestone A](MILESTONES.md#milestone-a--the-mod-is-born), [DEVELOPER.md](DEVELOPER.md#the-surfaces-1b))* — Apogee's served tools registered into the session (one `apogee mcp serve` exchange a call), `apogee_complete` (the CLI's one-shot, or 54b's served tool where the person lists it), `apogee_job`, the `/apogee` set over the JSON documents with promote and rollback confirmed, the status line, and the async long-run pattern. Your live look (`claude --plugin-dir lib/src/mod` on Claude Code 2.1.326 or newer) is still to come.
- [x] **The pane** *(1c, shipped 2026-11-21 — [Milestone A](MILESTONES.md#2026-11-21--mod-pane-backlog-item-1c-the-pane-beside-the-chat), [DEVELOPER.md](DEVELOPER.md#the-pane-1c))* — backends, models, training and the ledger over the documents beside the chat (`/apogee pane`), read back after every confirmed command and polled only while shown; promote/rollback as buttons with the exact command confirmed; the adapters column the follow-on (the CLI's track 53 shipped in v0.2.0). Your live look (`claude --plugin-dir lib/src/mod` on Claude Code 2.1.326 or newer) is still to come.
- [x] **The mod pipeline** *(1d, shipped 2026-11-21 — [Milestone A](MILESTONES.md#2026-11-21--mod-pipeline-backlog-item-1d-the-mods-own-pipeline-honest-about-its-gates), [DEVELOPER.md](DEVELOPER.md#the-pipeline))* — its own workflow, `mod.yml`: `changed.sh`-gated, `tsc`, the layering check and the node tests on every runner, validate over the zipped plugin where the `claude` CLI installs headlessly at the floor (the skip said otherwise), `claude plugin test` a dev-host gate, the plugin folder zipped as the artifact `apogee-mod`. Its first run on GitHub is the release pull request's, and its two jobs join the required checks by your `required-checks.py --apply` after that run passes.

## Up next

### v0.2.2 — Track 2, Claude aware of Apogee

Opened 2026-11-07 from that day's spike (the CLI's track 58 carries the walls and the server's half). The user's call **both hosts**: Claude Code gets a plugin, and the desktop mod picks up the new reads.

- [ ] **Apogee as a Claude Code plugin** *([2a](../../backlog/mod/v0.2.2/claude-code-plugin.md), gated on CLI 58a and 58c)* — the server registered with the coding-host profile and a skill saying when to use Apogee, installed from this repository's marketplace.
- [ ] **The desktop mod shows graphs and knowledge records** *([2b](../../backlog/mod/v0.2.2/mod-graphs-and-knowledge.md), gated on CLI 58b; its 1c gate satisfied 2026-11-21)* — `/apogee graphs`, `/apogee knowledge` and the pane's Knowledge section.

## Fast follow

*(empty)*

## Unspecced ideas

- Adapters in the pane and an adapter argument on the tools — recorded with the spike; the CLI's track 53 (runtime LoRA), which they waited on, shipped in v0.2.0.

## Explicit non-goals

See [SPEC.md](SPEC.md) → Non-goals: **not Claude's chat model** · **no ports, no cloud sessions** · **no capability of its own**.
