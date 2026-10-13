# Task recipes: pre-shipped goals with their checks baked in

**What / why.** Agents and symphonies ship in the box; tasks cannot — a task is a free-text goal on argv and nothing more (measured 2026-10-11: `task run --help` — `goal TEXT REQUIRED`, with `--require`, `--require-file`, `--rounds`, `--allow`, `--on-question`, `--agent`; no list, no name, no template — the pre-shipped-library spike's W1). This item gives tasks their shipped form: a **recipe** — a named goal template with its acceptance checks and suggested grants baked in. `apogee task recipes` lists them and `task recipes show <name>` prints one whole; `apogee task run --recipe <name> [--input <text>]` expands it — the goal rendered from its template, its `--require`/`--require-file` checks attached, its **suggested** `--allow` grants printed for consent at launch, never auto-granted — and runs exactly as the same invocation typed by hand. Three starters ship: **`digest-folder`** — read a named folder and write `DIGEST.md` (`--require-file DIGEST.md`, read-only); **`changelog-draft`** — a `git log` excerpt in, a CHANGELOG section out (`--require` on the heading); **`capture-decisions`** — sweep a transcript into knowledge records, suggesting the knowledge write grant. From the pre-shipped-library spike (2026-10-11), the split's one capability item.

**Core constraint(s).** **The policy ceiling is untouched** (27i): a recipe *suggests* grants; the launch consents to them exactly as `--allow` typed by hand, a grant wider than the config or the agent allows refused naming the rule, and with nobody at the terminal nothing is granted that was not given on the line. **Expansion is identity**: a recipe-run's record is byte-identical to the hand-typed equivalent — the ledger carries the recipe's name as provenance, nothing else differs; transitions, checks and the one task view (27j) unchanged. The **source ladder is the symphonies'**: a user's recipe (a file under the data directory's recipes row) over the seeded file over the compiled-in copy, same name wins nearest; seeded skip-if-present through the one seeding path, which means **the layout declaration gains its row in the same commit** (the one-layout-declaration invariant — installers, `check`, `reset --keep <TAB>` all learn it from the row). Reads have twins (ADR 0002): `task recipes --output-format json` ships with the verb, documented in machine-mode.md (the 28h idiom). The shell needs no new work: `task` is already a classified view and the exec line runs the new verbs (ADR 0010's floor).

**Seam + files.**
- `lib/src/cli/source/business/tasks/recipe.h/.cpp` — the definition (name, description, goal template over `{{input}}`, requires, require-files, suggested grants, suggested agent), the one parser, the source ladder; validation before anything runs (a template variable that isn't `{{input}}` refused at parse).
- `lib/src/cli/source/presentation/cli/task_cmd.cpp` — `task recipes [show <name>]` (+ the JSON document), `task run --recipe/--input`, the expansion into the existing run path, the consent print of suggested grants.
- `lib/src/cli/source/data/contracts/layout.h` — the recipes row (seeded directory), same commit as the seeding.
- `lib/src/cli/assets/recipes/{digest-folder,changelog-draft,capture-decisions}.yaml` + generator regeneration; `lib/documentation/reference/machine-mode.md` — the recipes read's row.
- Tests: `tests/business/tasks/recipe_test.cpp` (parser, ladder precedence, validation), `tests/presentation/cli/task_cmd_test` additions (expansion golden: a recipe run's ledger byte-equal to the hand-typed twin; the ceiling refusal unchanged; the consent print; the JSON document), byte-identity of compiled-in starters.

**Reference.** The symphonies' source ladder and seeding (27q — the shape copied whole: config/user over seeded over compiled-in, `--from`-style editability via the seeded file); 27h–27j (the record, ledger, one view — a recipe adds provenance, never a second shape); 27i (the ceiling the suggestions bow to); 28h (reads as documents); the pre-shipped-library spike (2026-10-11) for W1's evidence.

**Decisions made** (dated):
- 2026-10-11 — The set lands in v0.1.7 as track 40 (the user's placement call on the spike report).
- 2026-10-11 — The **full `--recipe` surface** ships, not documentation-only examples (the spike's recommendation, standing — the user executed without a trim): what makes a task pre-shipped is a name the binary knows, lists, and expands.
- 2026-10-11 — Grants are **suggested, printed, consented** — never auto-granted; the recipe is convenience, the ceiling is law.
- 2026-10-11 — The three starters are the spike's recommended cut, standing: `digest-folder`, `changelog-draft`, `capture-decisions`.
- 2026-10-13 — A recipe's template takes exactly one `{{input}}` (from `--input`, or omitted where the recipe needs none); multi-argument templating is out of the first cut — a second variable is a second item's evidence (the default, confirmed 2026-10-13).
- 2026-10-13 — The Task view offers no recipe picker yet: the exec line launches `task run --recipe …` in the shell, and a curated picker is a later call (the default, confirmed 2026-10-13).

**Guardrail(s).** The expansion golden (recipe run vs. hand-typed twin: ledger and view byte-equal but the provenance name); ladder precedence exhaustively (user over seeded over compiled-in, nearest name wins); a suggested grant wider than the config refused at launch naming the rule, recipe or no recipe; the parser's refusals (unknown variable, missing goal, empty requires allowed); `task recipes` document conformance; compiled-in byte-identity; `cli.install_parity` green with the new layout row.

**Acceptance criteria:**
- [ ] `task recipes` lists the three starters (human table + JSON document, documented); `show` prints one whole.
- [ ] `task run --recipe digest-folder --input <dir>` runs byte-identical to its hand-typed twin, ledger carrying the recipe's name; the suggested grants are printed and consented, never assumed.
- [ ] A user recipe file shadows a seeded one, which shadows the compiled-in copy.
- [ ] The layout row seeds, checks, resets and keeps like every other row; install parity holds.
- [ ] The ceiling's refusals are unchanged in every mode.

**Scope note.** Earmarked for v0.1.7; ungated — independently buildable. Out of scope: multi-variable templates, a Task-view recipe picker, scheduled or recurring runs, any runner/ledger shape change.
