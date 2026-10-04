# The configuration file moves to JSON

**What / why.** `~/.apogee/config/config.yaml` becomes `config.json` (the user's call, 2026-10-03): one obvious format across the product — the machine protocol, the store records, the sidecars and the admin plane already speak JSON, and the parser is already in the binary (nlohmann::json ships with the backends; the YAML engine is the odd one out). The change is honest about its one real cost up front: **Apogee's config editor is comment-preserving by design** — the one-config-mutation-path invariant exists so hand-written comments and layout survive every `config set`/`add-backend`/admin edit, and the bundled template (`config_template.cpp`) teaches through its comments — while **strict JSON has no comments at all**. That collision is **decided: JSONC** (the user's call, 2026-10-03) — `//` comments are read, kept through every edit exactly as the YAML editor keeps them today, and carried across migration, so the commented template keeps teaching in-file and nothing a user wrote is lost. Everything else follows the house rules: the switch rides [ADR backwards-compatibility](../../adrs/cli/backwards-compatibility.md) — an existing `config.yaml` keeps loading with a one-line notice, `apogee config migrate` converts it (original backed up beside it), fresh installs write `config.json` from day one, and dropping the YAML read path is a *later, deliberate* act, not a rider on this one.

**Core constraint(s).**
- **One mutation path, carried whole:** the editor is reimplemented for the new format with the same contract — an edit touches its key and nothing else, byte-stable outside the edited span, ordering, whitespace **and comments** preserved — and every writer (CLI verbs, the HTTP admin plane's config edits, chained registration) goes through it exactly as today. Byte-identical CLI↔HTTP mutations stay the law ([ADR mode-parity](../../adrs/cli/mode-parity.md)).
- **`${ENV}` semantics unchanged:** references stored literally, expanded on read; migration never bakes a secret into the file (secrets hygiene — the 0600 store is untouched by this item).
- **Migration is loud and lossless:** the converter round-trips every key **and every comment**, preserves entry order, backs up the original (`config.yaml.bak`), and prints what it did.
- **One layout declaration:** the config path changes in `layout.h` alone; `check`, `config init`, the uninstall plan and every reader follow it.
- **No new dependency:** the JSON engine is the vendored one already linked; the YAML engine remains only while the compat read and the training spec files need it.
- **Blast radius scoped:** pipeline/regime spec files and every other YAML consumer are out of scope — the config file and its template only (the node-parser seam they share is the reason to say this out loud).
- Code style carries: `.h`/`.cpp` pairs, smart pointers only; tests in the layer of what they test ([ADR](../../adrs/cli/tests-mirror-architecture.md)).

**Seam + files.**
- `source/data/contracts/config.h/.cpp`: the loader — format by filename (the layout row's `config.json`, the compat read for `config.yaml` with its notice); typed mapping, validation and `${ENV}` expansion unchanged above the parse.
- `source/data/contracts/config_edit.h/.cpp`: the format-preserving JSON editor — the same exported functions, so every caller (CLI, admin plane, the registration chain) recompiles onto it without signature churn.
- `source/data/contracts/config_template.cpp`: the `config init` template re-authored in JSONC, its teaching comments kept in-file.
- `source/presentation/*/` config verbs + `httpserver` admin config routes: unchanged in interface — they call the one editor; `commands/config_cmd` gains `config migrate`.
- Tests: `tests/data/contracts/` — editor byte-stability mutation tests re-pointed at JSON fixtures; migration goldens (a commented, env-referencing, multi-backend `config.yaml` fixture → exact expected output + backup); dual-read notice; CLI↔HTTP byte-identity on the new format.
- Consumes: the layout contract (shipped), the editor's callers as A1 carved them (shipped, Milestone AA); [ADR backwards-compatibility](../../adrs/cli/backwards-compatibility.md) and [mode-parity](../../adrs/cli/mode-parity.md) as the governing rules.

**Reference (Ommi).** Ommi's config is YAML too (`lib/cli/config.yaml`) — Apogee inherited the format with the shape. The deliberate divergence is this item; the thing **not** diverging is the lesson behind the editor: config edits that rewrite the user's file lose the user's trust, so the format-preserving contract survives the format.

**Decisions made** (dated):
- 2026-10-03 — Asked for by the user, placed in **v0.1.5** (their call), at the table's end as 28i.
- 2026-10-03 — No new dependency: the vendored JSON engine already in the binary does the parsing; this item removes YAML from the config path and leaves the YAML engine to its remaining consumers (spec files) until their own item.
- 2026-10-03 — Compatibility per the ADR: dual-read with a notice plus a named `config migrate`, new installs on JSON immediately; retiring the YAML read is explicitly deferred to a future deliberate item.
- 2026-10-03 — **The comments question, answered the same day (the user's call): JSONC, keep the comments.** `//` comments are accepted on read, preserved through every edit, carried across migration; the template keeps teaching in-file. The trade accepted with it: the file is JSONC rather than strict JSON, so a strict external parser reads it only after comment-stripping — worth one line in the user docs.
- 2026-10-03 — Confirmed (the default taken, the user's confirmation): the compat read sits behind the loader seam with its notice, and `check` names the old file and the migrate command while `config.yaml` still loads.
- 2026-10-03 — Confirmed (the default taken, the user's confirmation): migrate refuses when `config.json` exists — names both files, asks; never guesses which is current.
- 2026-10-03 — Confirmed (the default taken, the user's confirmation): two-space pretty-print, insertion order preserved, trailing newline — the diffable house shape.

**Guardrail(s).**
- The editor's mutation tests: every verb's edit against the JSONC fixtures is byte-identical outside the edited span — comments included.
- The migration golden: the worst-case fixture (comments, `${ENV}` references, every section populated) converts to the exact expected file with every comment carried over, backup byte-equal to the original.
- Dual-read: `config.yaml` alone → loads, one notice, everything green; both files → the refusal naming them; `config.json` alone → silent, normal.
- CLI↔HTTP: the same edit through both planes produces byte-identical files (the existing parity test, re-pointed).

**Acceptance criteria:**
- [ ] A fresh `apogee config init` writes `config.json`; every config verb, the registration chain, and the admin plane's edits work on it with the editor's byte-stability contract observed.
- [ ] An install with only `config.yaml` keeps working with a one-line notice; `apogee config migrate` produces the JSON equivalent, backs up the original, and the next run is silent.
- [ ] `${ENV}` references survive migration literally and expand on read exactly as before.
- [ ] Comments survive end to end: a commented `config.yaml` migrates with its comments intact, the commented template ships in `config init`'s output, and an edit through any verb leaves every comment byte-identical — demonstrated in the goldens.

**Scope note.** Item **28i**, earmarked for **v0.1.5** (the table's end, after 28h); gated on nothing pending. Out of scope: converting pipeline/regime spec files or any other YAML consumer; removing the YAML engine or the compat read (each a later deliberate item); changing any key's meaning or layout beyond format (backwards compatibility is the point, not a renovation).
