# Shipped agents: four more in the box

**What / why.** Apogee ships three bundled agents today — `security-review`, `release-notes`, `merge-request`, each a compiled-in prompt + output schema with read-only tools (measured 2026-10-11: `agents list` in a sandboxed install names exactly those three, SOURCE `bundled`) — and all three are repo-and-release flavored. The definition surface already carries more than they use (measured from `agents create --help`: a model override, the `read-only|all|none` tool policy, `--collection` auto-RAG on every run, `--output-format`, `--save-dir`), and `analyze [INPUT]` takes text or piped stdin. This item widens the set with four bundled agents that cover the rest of what people actually hand a model: **`code-review`** — correctness and simplification findings over a diff or file (schema: findings with location, severity, claim, suggested fix) — `security-review`'s general sibling; **`test-gaps`** — the behaviors nothing tests, from a diff or file (schema: gaps with suggested test names); **`doc-drift`** — statements a change made stale, the first bundled agent to use `--collection` auto-RAG, run against an ingested docs corpus; **`meeting-minutes`** — a transcript into decisions, action items (owner, due) and open questions (schema) — the first non-developer bundled agent. Content only: no new surface, no new verbs. From the pre-shipped-library spike (2026-10-11), whose catalog this is.

**Core constraint(s).** Bundled agents stay **read-only tool policy** and default-model, like the shipped three. Every schema is a valid JSON Schema and rides the structured-output contract (Milestone X: validated client-side, one corrective retry, a second miss delivered raw and flagged). The **asset pipeline is the law**: each prompt and schema is compiled in via the generator scripts and byte-identity-tested against its `assets/` file, listed `bundled`, and never shadows a user agent of the same name (the existing source rules). `doc-drift` ships **without** a default collection — auto-RAG is pointed at an install's own corpus by the user, and a run against an absent collection degrades exactly as the existing auto-RAG behavior degrades, said, never invented. Prompts are model-agnostic: no vendor names, no tool assumptions, briefs a small local model can hold.

**Seam + files.**
- `lib/src/cli/assets/prompts/{code-review,test-gaps,doc-drift,meeting-minutes}.txt` and `assets/schemas/{code-review,test-gaps,doc-drift,meeting-minutes}-output.json` — the four definitions, beside the shipped three.
- The bundled-agent registry — wherever the three are declared for compile-in and seeding (the contracts' bundled assets, per the one seeding path) — gains four rows; the generator regenerates the compiled-in copies.
- Tests: `agents list` shows seven `bundled`; each new agent run through `analyze --agent` over the scripted mock backend produces schema-valid output; byte-identity of compiled-in copies vs. `assets/`; the user-shadowing rule holds for a new name.

**Reference.** The three shipped bundled agents (the exact shape copied: prompt file, `-output.json` schema, read-only, default model); the clerks (`assets/clerks/` — the older compiled-in prompt+schema precedent); Milestone X's structured-output contract; the pre-shipped-library spike (2026-10-11) for the catalog and measurements.

**Decisions made** (dated):
- 2026-10-11 — The set lands in v0.1.7 as track 40 (the user's placement call on the spike report; 40 by the ever-assigned rule — 38 and 39 were assigned in the same window by parallel tracks).
- 2026-10-11 — The four agents are the spike's recommended cut, standing (the user executed the spike without trims): `code-review`, `test-gaps`, `doc-drift`, `meeting-minutes`.
- 2026-10-11 — `doc-drift` demonstrates `--collection` auto-RAG and ships pointing at no collection; the prompt names the corpus it expects and the run says when there is none.
- 2026-10-13 — The catalog stays deliberately four; `issue-triage` and the other close seconds are added only on the user's word at build (the default, confirmed 2026-10-13).
- 2026-10-13 — Each schema's exact fields are the build's, within the sketched shapes — flat arrays of simple objects, no nesting a small model fumbles (the default, confirmed 2026-10-13).

**Guardrail(s).** Hermetic: each agent parses, lists as `bundled`, and produces schema-valid output over the mock backend (FakeTransport-scripted); compiled-in copies byte-identical to `assets/`; a same-named user agent still shadows cleanly. Optional real-weights pass per the model families — one family at a time, each agent once, result recorded; a family with no local model skipped and said.

**Acceptance criteria:**
- [ ] `agents list` on a fresh install shows seven bundled agents; each runs through `analyze --agent` with schema-valid output on the mock.
- [ ] `doc-drift` runs with and without its collection, saying which it did.
- [ ] Compiled-in copies are byte-identical to the asset files; the generator reproduces them.
- [ ] No surface change: every existing `agents`/`analyze` test green and byte-stable.

**Scope note.** Earmarked for v0.1.7; ungated — independently buildable. Content only; new verbs, surfaces or tool policies are out of scope.
