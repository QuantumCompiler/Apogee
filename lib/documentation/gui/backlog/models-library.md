# The models library and roles

**What / why.** The design's Models screens (`ScreenModels`, `ScreenModelsRoles`): the library rows and details — pipelines and voice models included — from `models list` and `models info` (ready, per the contract board), delete/rename/repair/check (ready), and the roles screen over `config set-default-*` and `models status` (ready). Two measured caveats shape the details pane (the GUI spike, 2026-10-20): `models info` takes **one set of weights**, not a bare model name (the app names sets), and its JSON is `{name, fields[]}` display strings — lineage present as prose, **no window/cache/byte fields on a safetensors set** — so "memory at a window" and structured made-from render from the typed `data` object once the CLI's 52e lands, and until then the pane shows the prose fields as the board's fallback. This item builds the library (rows, details, actions with confirm-and-read-back), and the roles screen (each role's pointer moved through its verb, read back through `models status`).

**Core constraint(s).** ADR 0001/0002; reads through the reads runner; every mutation is the command a person would type, read back after. Destructive actions confirm. Unknowns said, never guessed (the store's honesty carried to the screen).

**Seam + files.** `source/presentation/models/` (library, details, roles; view-models), `source/business/reads/` (the models/config commands). Tests: view-models over scripted documents (rows, details with and without typed `data`, action→read-back, role moves), hermetic.

**Reference.** `ScreenModels.dc.html`, `ScreenModelsRoles.dc.html`, `ComponentsModel.dc.html`; the contract board's MODELS rows; [52e](../../cli/assistant/MILESTONES.md#milestone-ao--the-desktop-clients-cli-growths) (shipped 2026-10-20) (the typed document upgrade, cross-referenced, not gated); the spike's `models info` measurements.

**Decisions made** (dated):
- 2026-10-20 — From the design-completion spike (the user's call); prose-fields fallback until 52e, upgrade noted.

**Open calls:**
- [default: row grouping (configured backends, then stored-unconfigured) per the prototype; details at build.]

**Guardrail(s).** View-model matrices over fixture documents of each format row; the set-not-model addressing pinned; confirm/read-back pins; hermetic.

**Acceptance criteria:**
- [ ] The library lists every stored kind with details; delete/rename/repair run with confirmation and read-back; roles move through their verbs and show through `models status`.
- [ ] The details pane renders prose fields today and the typed `data` when present; the full suite is green.

**Scope note.** Earmarked for v0.2.0; gated on 1e, the app shell (shipped 2026-10-20: [MILESTONES → Milestone B](../assistant/MILESTONES.md#milestone-b--the-designs-screens)). Out of scope: downloads ([`downloads-jobs.md`](downloads-jobs.md)), suites (1k), any CLI change.
