# The workbench views: the rest of the product on screen

**What / why.** With the shell ([32b](../../assistant/MILESTONES.md#milestone-ah--the-full-screen-tui)) and the conversation ([32c](../../assistant/MILESTONES.md#milestone-ah--the-full-screen-tui)) in place, this item adds the working views around them — **deliberately few at first**: models, chats, suites and config as switchable full-screen views. This is the item that must carry the recorded anti-pattern on its face: a TUI of this shape dies *here* — eight hand-built views, each a second implementation of an action the CLI already had, each needing to be kept honest by hand: the per-view CLI-parity maintenance burden. The counter-design is structural and already built elsewhere in the product: since A4, `operations/` holds the cores the CLI and the HTTP server both run precisely "so neither surface includes the other" — the TUI joins as the third reader of those cores, and 28h's machine-readable reads (shipped with v0.1.5) define the data every listing view renders. **A view renders a read and calls a core; it computes nothing of its own.** A models view paints what `models list`'s core returns; a config edit made in the TUI goes through the one comment-preserving editor and is byte-identical to the CLI's — the mode-parity law extended to a third mode, with the same byte-equality test shape that already holds CLI↔HTTP.

**Core constraint(s).**
- **Render reads, call cores — nothing else.** Every view's data comes from an `operations/` core or an existing read; every action calls the same core the command calls. The layering scan extends to pin it: `tui/` includes `operations/` and the seams, never a business package directly where a core exists.
- **Parity is tested, not promised:** a mutation from a TUI view (config set, backend add, suite select) produces a byte-identical file to the CLI's same mutation — the existing CLI↔HTTP parity test shape, re-pointed as CLI↔TUI.
- **Few views, each earning its place:** the first cut is models, chats, suites, config. A new view after that needs what these have — an existing core to render and an action set that is calls, not code.
- **Permission and secrets discipline unchanged:** the config view shows what `config show` shows (secrets as `api_key_set`, never a key — the view type structurally cannot carry one); destructive actions (delete a chat, remove a backend) confirm with the same wording the CLI confirms with.
- **The screen rules are 32b's:** one owner, restore on exit, honest Windows story — inherited, not restated.
- Code style carries: `.h`/`.cpp` pairs, smart pointers only; tests mirror the module.

**Seam + files.**
- `presentation/tui/models_view.h/.cpp`, `chats_view.h/.cpp`, `suites_view.h/.cpp`, `config_view.h/.cpp`: each a renderer over its core's read plus a small action map onto core calls; registered with 32b's view registry, switchable from the shell and the palette.
- `operations/` (shipped): consumed as-is; a read a view needs that only exists as painted output gets its core carved *there* (the A4 pattern), never implemented in `tui/`.
- Tests: `tests/presentation/tui/` — per-view headless goldens over scripted core results; the CLI↔TUI byte-parity cases for every mutation a view offers; the secrets-shape pin (the view's data type has no key field); the confirm-wording goldens.
- Consumes: [32b](../../assistant/MILESTONES.md#milestone-ah--the-full-screen-tui), [32c](../../assistant/MILESTONES.md#milestone-ah--the-full-screen-tui) (the shell and the session view it sits beside), `operations/` (A4, shipped), 28h's read shapes (shipped, v0.1.5 — [Milestone M](../../assistant/MILESTONES.md#milestone-m--the-front-end-contract)), the one config editor (shipped), 27d's suites unit (shipped, v0.1.4).

**Reference.** The anti-pattern, carried on this item by design: a TUI of eight hand-built views, shipped across two releases and removed whole under its per-view parity burden. The in-house counter-precedents: `operations/` (one core, two surfaces — now three), the CLI↔HTTP byte-parity tests (the shape this item re-points), and 28h's `{rows, ok}` reads (the data contract the views render).

**Decisions made** (dated):
- 2026-10-04 — Split as the set's closer: views are additive one by one once the stage and the session exist; none of them blocks the entry experience.
- 2026-10-04 — First cut is exactly four views (models, chats, suites, config) — the ones whose cores and reads exist today or earlier in this queue; growth is deliberate, per the earning rule above.
- 2026-10-04 — Parity by test, the third mode: every TUI mutation gets a CLI↔TUI byte-equality case, the existing parity shape re-pointed — the lesson the old non-goal encoded, kept as engineering instead of prohibition.

**Open calls:**
- [default: view switching is the palette plus number keys (`1`–`4`), claude-adjacent and shell-registered; veto to a tab strip if the frame should always show what exists]

**Guardrail(s).**
- Per-view goldens over scripted cores: the models view renders exactly the rows `models list`'s core returns (planted store), the config view the editor's view type.
- The byte-parity battery: each view action vs its CLI twin, file-equal; a view action with no CLI twin is a build error by review (the earning rule).
- The secrets pin: the config view's type carries `api_key_set` only; the leak test's distinctive-key sweep extends to TUI renders.
- The layering scan: `tui/` held to its allow-list; a view including a business package a core already fronts fails.

**Acceptance criteria:**
- [ ] From the shell: the models view lists the real store (read-parity with `models list` asserted), the chats view opens a session into 32c's view, the suites view selects a suite for it, and the config view edits through the one editor — each mutation byte-identical to its CLI twin.
- [ ] Deleting a chat or removing a backend confirms with the CLI's wording and refuses the same cases.
- [ ] No secret renders anywhere in any view (the extended leak sweep passes).
- [ ] The layering and parity batteries pass; the raw CLI remains byte-identical everywhere.

**Scope note.** Item **32d**, earmarked for **v0.1.6**; **gated on nothing pending** — 32b and 32c shipped 2026-10-10 ([Milestone AH](../../assistant/MILESTONES.md#milestone-ah--the-full-screen-tui): the shell, and the session view a chat opens into, `TuiSessionDriver` opening a saved chat by id). Out of scope: views beyond the four (each later one is its own small item under the earning rule); training/graph/knowledge screens (cores exist, but they wait for a demonstrated need); any raw-CLI change; forms that bypass a core.
