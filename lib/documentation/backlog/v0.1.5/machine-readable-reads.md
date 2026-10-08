# Machine-readable reads: `--output-format json` on the listing commands

**What / why.** Spike wall W7 ([Milestone M](../../assistant/MILESTONES.md#2026-09-25--the-integration-spike-a-naive-host-embeds-the-binary-backlog-item-28-for-v015)): machine mode's own rule is "everything else is a CLI command" — a host performs mutations *and reads* by shelling out — but the read commands emit human prose, so a host UI populating a model picker screen-scrapes `apogee models` or re-reads Apogee's config files itself (which the contract has never promised as stable surface). This item gives the reads a machine face: **`--output-format json`** on the listing and status commands a host actually needs — `models` (list/info/status), `chats list`, `agents list`, `mcp list`, `check` — printing one JSON document of the same facts the human rendering shows. The seam already exists: machine mode's own events go through `render/json_report`, and the admin plane already serialises most of these listings for HTTP; this item routes the CLI's reads through the same source of truth so the JSON a host gets from the command line is the JSON a remote client gets from `/v1/admin` — parity structural, not audited. *(2026-10-06, the pre-MR docs pass: 27j already shipped the read convention — `ReadFormat` and `write_document` in `machine/json_reporter.h`: `text`|`json`, one document on stdout byte-equal to the admin route's body, `stream-json` refused on a read — and the task, graph and symphony reads use it; the five reads above are not on it yet (`models list`'s older `--output-format stream-json` rows predate it), so this item extends that convention to them rather than inventing it.)*

**Core constraint(s).**
- **Parity is the product, third rendering, same facts.** The JSON output is a view over the exact data the human view prints — one gathering path per command, two renderers; a fact present in one and absent from the other is the drift this repo's parity tests exist to catch. Where an admin route already serves the listing, the CLI's JSON reuses that serialisation, not a rewrite of it.
- **Secrets stay unleakable by construction:** the reads reuse the existing view types (`backend_view` with `api_key_set` and structurally no key; credential *metadata* only). A new rendering must not create a new serialisation path around that discipline.
- **stdout carries only the document** — the machine-mode rule 3 discipline extends to the reads: diagnostics to stderr, exit codes meaning what they mean today (`check`'s especially).
- **Additive:** the default human output is byte-unchanged; the flag is opt-in. `--output-format json` on a command that has no JSON rendering yet is a clear refusal naming the supported ones, never silently-prose.
- **A stable contract once shipped:** these documents join the stability promise ([28d](../../assistant/MILESTONES.md#milestone-m--the-front-end-contract)) — fields are added, not renamed; a host may build on them.

**Seam + files.**
- `cli/models.cpp`, `chat_history.cpp` (`chats list`), `agents_cmd.cpp`, `mcp_cmd.cpp`, `check.cpp`: the flag, each routing its existing gathered data through the JSON renderer instead of the table printer.
- `machine/json_reporter.h` (27j's `ReadFormat`/`write_document`, shipped): the flag's words and the one-document write, reused as-is.
- `render/json_report.h/.cpp` and the `httpserver/` listing serialisers: the shared document shapes — whichever of the two already owns a listing's shape is the one the CLI rendering calls.
- [machine-mode.md](../../reference/machine-mode.md): the "everything else is a CLI command" section, which already states the read contract for the task, graph and symphony reads (27j–27q), gains these five beside them, so an integrator finds the read contract where they found the write one.
- Tests: golden JSON per command (hermetic, mock-backed); a parity assertion per listing that the human and JSON views enumerate the same rows; `tests/scripts/py/naive_host_driver.py` phase 3 flips from wall to check — W7 closes.

**Reference.** Apogee's local front-ends are pipes and the CLI is the contract — exactly why the CLI needs a machine face rather than leaving reads to HTTP.

**Decisions made** (dated):
- 2026-09-25 — Split from the integration spike (item 27), wall W7.
- 2026-09-25 — Scope chosen by what a host UI needs to *render*, not every command: models, chats, agents, mcp, check. Others join by the same pattern when a host demonstrates the need.
- 2026-10-03 — Confirmed (the default taken, the user's confirmation): the flag is `--output-format json`, matching machine mode's vocabulary.
- 2026-10-03 — Confirmed (the default taken, the user's confirmation): `check` emits {rows:[{name,status,detail}…], ok} with `skipped` first-class — no lie-shaped pass in JSON either.
- 2026-10-03 — Confirmed (the default taken, the user's confirmation): one JSON document per invocation — reads, not streams; JSONL stays machine mode's.

**Guardrail(s).**
- Golden documents per command over a seeded sandbox install; mutation-tested where the repo's convention applies.
- The row-parity assertion: human and JSON renderings of one listing disagree → the test names the command.
- The secrets leak test extended over every new document (the distinctive-key sweep already in `tests/presentation/cli/leak_test.cpp`'s pattern).
- `check --output-format json` exit code matches the human run's on the same install, healthy and broken.

**Acceptance criteria:**
- [ ] `apogee models --output-format json`, `chats list`, `agents list`, `mcp list`, `check` each print one valid JSON document carrying the same facts as their human output; a host populates a model picker with no screen-scraping.
- [ ] No document anywhere carries a key or token — the leak sweep passes over all five.
- [ ] An unsupported command given the flag refuses, naming the supported set.
- [ ] machine-mode.md's CLI-command section shows the read contract; the probe's phase 3 asserts JSON instead of recording the wall.

**Scope note.** Item **28h** (27e at birth; 28e, then 29e, through the day's renumbers; 28h since 2026-10-03's collapse into v0.1.5 — the user's calls), earmarked for **v0.1.5**; gated on nothing pending (independent of the other integration items — buildable first if convenient, listed last only because the protocol items carry more risk). Out of scope: JSON output for mutating commands (their contract is the exit code and the state change); `--output-format json` on `complete`/`chat` (that is machine mode itself); pagination (these listings are small; the flag can grow it later without breaking documents).
