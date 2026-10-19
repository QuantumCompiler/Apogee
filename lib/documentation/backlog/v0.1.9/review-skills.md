# Review skills: local lenses, one finding contract

**What / why.** A good automated review is not one prompt — it is a set of **lenses**, each hunting one domain's failure modes, selectable per repository and extendable by the user without hosting anything. Apogee already owns every piece of that shape (the review-agent spike, 2026-10-18): the **source ladder** (a user's file over the seeded file over the compiled-in copy — symphonies', recipes' and regimes' exact mechanism) and a **shared finding contract** already shipped and measured — the `code-review` agent's schema carries `severity, kind, file, location, claim, suggested_fix, confidence`, fields shaped for inline-comment mapping. This item defines the **review skill**: a markdown document with a name, a description, *applies-to* hints (path globs, extensions, languages) and the hunt prompt — every skill's output held to the **one finding schema**, so the engine ([`review-core.md`](review-core.md)) runs any of them identically and a user adds one by dropping a file. Shipped: **seven skills** (the user's catalog, 2026-10-18) — `general-correctness` (always-on: wrong operators, off-by-one, swallowed errors, null/lifetime slips), `front-end` (XSS sinks, state races, a11y regressions, uncontrolled re-renders), `back-end` (missing auth checks, N+1 queries, migration safety, contract drift, boundary validation), `embedded` (ISR safety, fixed-width types, `volatile` misuse, stack/heap discipline, blocking in tight loops), `ai-ml` (data leakage across splits, unseeded nondeterminism, tensor-shape mismatches, eval hygiene, prompt-injection surfaces), `concurrency` (races, lock ordering, TOCTOU, async lifetimes), `security` (secrets in diffs, injection, path traversal — the bundled `security-review` agent's substance re-cut as a lens so it rides the same machinery). Every skill carries the track's prime directive on its face: **silence is correct** — an empty findings array is a valid pass, and a false positive is the worse failure.

**Core constraint(s).** One output contract: every skill's hunt pass is held to the shared finding schema (26f's structured-output machinery; the schema is the `code-review` agent's, owned once — a skill never declares its own). The **ladder is the law**: a config/user file under the data directory's review-skills row over the seeded file over the compiled-in text, seeded skip-if-present through the one seeding path, the layout row landing in the same commit; the asset pipeline (generators, byte-identity tests) as every shipped definition before it. **Applies-to hints are data, matched deterministically** (globs and extensions against the diff's file list — the engine's plan stage consumes them; `(consumed decision)`: the picking itself is 48c's). Skills are prompts, not agents: no tools, no model pins, no schema of their own — model-agnostic text a Q4 local member or a cloud model reads alike. Reads have twins: `review skills list|show` with JSON documents (the 28h idiom), the exec line covering them (ADR 0010).

**Seam + files.**
- `data/contracts/` — the skill's parse (name, description, applies globs/langs, the prompt body) through one parser, the shared finding schema's home named once.
- `lib/src/cli/assets/review-skills/{general-correctness,front-end,back-end,embedded,ai-ml,concurrency,security}.md` — the seven, each with the starters' header style; the generator regenerates compiled-in copies; `contracts/layout.h` gains the seeded row (same commit).
- `presentation/cli/review_cmd.cpp` (first half) — `review skills list|show` (+ `--output-format json`), machine-mode.md's rows.
- Tests: the parser's table (hints, body, refusals on malformed); every shipped skill parses and lists; ladder precedence (user over seeded over compiled-in); byte-identity; `cli.install_parity` green with the new row; the leak and parity tables updated.

**Reference.** 27q/40c/42a — the source ladder copied whole; 40a — the `code-review` schema (the one finding contract, measured fields) and `security-review` (the substance the security skill re-cuts); 26f — the structured-output hold; the review-agent spike (2026-10-18).

**Decisions made** (dated):
- 2026-10-18 — The set lands in v0.1.9 as track 48 (the user's placement call); **skills are local** on the ladder — nothing hosted, a user skill is a dropped file (the user's call, from the spike's refinement).
- 2026-10-18 — The catalog is the seven above (the user's four — front-end, back-end, embedded, ai-ml — plus general-correctness, concurrency and security as the proposed others, standing).
- 2026-10-18 — One finding schema for every skill, the `code-review` agent's — a skill is a lens, never a new contract.
- 2026-10-18 — **Silence is correct**: the precision bias is a core constraint of every shipped skill's prompt and the engine's goldens, not a tunable.

**Open calls:**
- [default: each skill's exact prompt text is the build's craft within its one-line charter above, reviewed against one local Q4 model and one cloud model on a fixture diff before ship (the persona item's discipline).]
- [default: `applies` hints ship conservative (extensions and well-known paths); a skill with no hints applies everywhere, like `general-correctness`.]

**Guardrail(s).** The parser and ladder suites; every shipped skill held to the schema on a scripted model (a fixture diff, a canned response validated); byte-identity; the fixture-diff pass where each skill finds its planted defect **and stays silent on the clean twin** — the precision golden, both directions.

**Acceptance criteria:**
- [ ] `review skills list` shows the seven on a fresh install with documents; `show` prints one whole; a dropped user file shadows a shipped name.
- [ ] Every skill parses, applies by its hints, and its canned hunt output validates against the one schema.
- [ ] The planted-defect/clean-twin precision goldens hold for all seven; the full suite is green.

**Scope note.** Earmarked for v0.1.9; ungated — definitions and surfaces first, the engine consumes them ([`review-core.md`](review-core.md)). Out of scope: the plan/hunt machinery, per-repo selection ([`review-sweep.md`](review-sweep.md)'s config), any learning from feedback (a recorded later question).
