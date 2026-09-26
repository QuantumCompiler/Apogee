# Machine-readable reads: `--output-format json` on the listing commands

**What / why.** Spike wall W7 ([Milestone M](../assistant/MILESTONES.md#2026-09-25--the-integration-spike-a-naive-host-embeds-the-binary-backlog-item-27-for-v014)): machine mode's own rule is "everything else is a CLI command" — a host performs mutations *and reads* by shelling out — but the read commands emit human prose, so a host UI populating a model picker screen-scrapes `apogee models` or re-reads Apogee's config files itself (which the contract has never promised as stable surface). This item gives the reads a machine face: **`--output-format json`** on the listing and status commands a host actually needs — `models` (list/info/status), `chats list`, `agents list`, `mcp list`, `check` — printing one JSON document of the same facts the human rendering shows. The seam already exists: machine mode's own events go through `render/json_report`, and the admin plane already serialises most of these listings for HTTP; this item routes the CLI's reads through the same source of truth so the JSON a host gets from the command line is the JSON a remote client gets from `/v1/admin` — parity structural, not audited.

**Core constraint(s).**
- **Parity is the product, third rendering, same facts.** The JSON output is a view over the exact data the human view prints — one gathering path per command, two renderers; a fact present in one and absent from the other is the drift this repo's parity tests exist to catch. Where an admin route already serves the listing, the CLI's JSON reuses that serialisation, not a rewrite of it.
- **Secrets stay unleakable by construction:** the reads reuse the existing view types (`backend_view` with `api_key_set` and structurally no key; credential *metadata* only). A new rendering must not create a new serialisation path around that discipline.
- **stdout carries only the document** — the machine-mode rule 3 discipline extends to the reads: diagnostics to stderr, exit codes meaning what they mean today (`check`'s especially).
- **Additive:** the default human output is byte-unchanged; the flag is opt-in. `--output-format json` on a command that has no JSON rendering yet is a clear refusal naming the supported ones, never silently-prose.
- **A stable contract once shipped:** these documents join the stability promise ([27a](machine-handshake.md)) — fields are added, not renamed; a host may build on them.

**Seam + files.**
- `commands/models.cpp`, `chat_history.cpp` (`chats list`), `agents_cmd.cpp`, `mcp_cmd.cpp`, `check.cpp`: the flag, each routing its existing gathered data through the JSON renderer instead of the table printer.
- `render/json_report.h/.cpp` and the `httpserver/` listing serialisers: the shared document shapes — whichever of the two already owns a listing's shape is the one the CLI rendering calls.
- [machine-mode.md](../reference/machine-mode.md): the "everything else is a CLI command" section gains the flag and an example, so an integrator finds the read contract where they found the write one.
- Tests: golden JSON per command (hermetic, mock-backed); a parity assertion per listing that the human and JSON views enumerate the same rows; `tests/naive_host_driver.py` phase 3 flips from wall to check — W7 closes.

**Reference (Ommi).** No analog for a JSON CLI read surface; Ommi's GUI story read everything over `/v1/admin`. Apogee's divergence (local front-ends are pipes, the CLI is the contract) is exactly why the CLI needs the machine face Ommi could leave to HTTP.

**Decisions made** (dated):
- 2026-09-25 — Split from the integration spike (item 27), wall W7.
- 2026-09-25 — Scope chosen by what a host UI needs to *render*, not every command: models, chats, agents, mcp, check. Others join by the same pattern when a host demonstrates the need.

**Open calls:**
- [default: `--output-format json` (matching machine mode's flag vocabulary) rather than a bare `--json`] Flag spelling.
- [default: `check` emits `{rows:[{name,status,detail}...], ok:bool}` with `skipped` a first-class status — a lie-shaped pass stays impossible in JSON too] The doctor's document.
- [default: one JSON document per invocation, not JSONL — these are reads, not streams] Framing.

**Guardrail(s).**
- Golden documents per command over a seeded sandbox install; mutation-tested where the repo's convention applies.
- The row-parity assertion: human and JSON renderings of one listing disagree → the test names the command.
- The secrets leak test extended over every new document (the distinctive-key sweep already in `tests/secrets/leak_test.cpp`'s pattern).
- `check --output-format json` exit code matches the human run's on the same install, healthy and broken.

**Acceptance criteria:**
- [ ] `apogee models --output-format json`, `chats list`, `agents list`, `mcp list`, `check` each print one valid JSON document carrying the same facts as their human output; a host populates a model picker with no screen-scraping.
- [ ] No document anywhere carries a key or token — the leak sweep passes over all five.
- [ ] An unsupported command given the flag refuses, naming the supported set.
- [ ] machine-mode.md's CLI-command section shows the read contract; the probe's phase 3 asserts JSON instead of recording the wall.

**Scope note.** Item **27e**, earmarked for **v0.1.4**; gated on nothing pending (independent of the other integration items — buildable first if convenient, listed last only because the protocol items carry more risk). Out of scope: JSON output for mutating commands (their contract is the exit code and the state change); `--output-format json` on `complete`/`chat` (that is machine mode itself); pagination (these listings are small; the flag can grow it later without breaking documents).
