# Shipped symphonies: four more starters, and the first chain in the box

**What / why.** Apogee ships three symphony starters today — `describe-answer` (vision → chat, an image input), `extract-facts` (one schema-held extraction stage), `summarize-verify` (utility → chat, stage-output threading) — measured 2026-10-11 (`symphonies list` in a sandboxed install), and two shipped capabilities go undemonstrated: **chains** (27r — a stage that plays another symphony) and, by extension, what the **Orchestrator** (27t) gets offered, since every shipped definition is a `play_` tool the moment an orchestrating session opens. This item adds four starters: **`redact-text`** — the utility member lists PII and secrets as a schema stage, the chat member rewrites the text with placeholders — the privacy story local models exist for; **`extract-actions`** — one schema stage pulling action items (owner, verb, due) out of notes, `extract-facts`' sibling on a different document; **`critique-revise`** — the utility member critiques the input draft against its stated intent, the chat member revises — the generic quality loop; **`digest`** — **the first shipped chain**: a stage that `play:`s `summarize-verify` on the input, then a schema stage producing title, tags and a one-line summary over its output, inside the depth caps. From the pre-shipped-library spike (2026-10-11), whose catalog this is.

**Core constraint(s).** The 27q grammar is the whole language: stages name **roles, never backends** (refused at parse by name), templates touch only `{{input}}` and earlier stages' names, `answer_tokens` caps every stage, and every definition reads through the one parser (`parse_symphony_spec`). **Stages are tool-free and text(+image)-in** — the spike's W2, carried on this item's face: no retrieval inside a stage, and no audio-fed stage until one is proven (`describe-answer` proves image; nothing proves audio — audio starters are out of scope). `digest`'s chain validates through the one static walk (`contracts/symphony_walk`) within `symphony_caps.depth` and spends one budget across the whole play (27r). The asset pipeline is the law: `scripts/generate_symphony_assets.py` regenerates the compiled-in copies, seeded skip-if-present, byte-identity-tested. The Orchestrator needs no code here — new starters are offered as `play_` tools by the existing registration.

**Seam + files.**
- `lib/src/cli/assets/symphonies/{redact-text,extract-actions,critique-revise,digest}.yaml` — beside the shipped three, each with the starters' header style (what it shows, the play line, the `--from` line).
- `scripts/generate_symphony_assets.py` run — the compiled-in copies; the seeding list follows the existing rows.
- Tests: each starter parses and validates on load; a play over mock members holds each schema stage to its schema; `digest` walks as a chain within the caps and its narration names positions (`digest → summarize-verify, stage 1/2 …`); byte-identity; `symphonies create <name> --from <starter>` works for each.

**Reference.** The three shipped starters (shape, header style, the `--from` door); 27q (grammar, validation), 27r (chains — `digest` is its first shipped exercise), 27t (the automatic `play_` offer); the pre-shipped-library spike (2026-10-11).

**Decisions made** (dated):
- 2026-10-11 — The set lands in v0.1.7 as track 40 (the user's placement call on the spike report).
- 2026-10-11 — The four starters are the spike's recommended cut, standing (the user executed without trims): `redact-text`, `extract-actions`, `critique-revise`, `digest`.
- 2026-10-11 — `digest` plays `summarize-verify` rather than inlining its stages: the point is a shipped, visible chain exercising 27r in the box.
- 2026-10-11 — Audio symphonies are out until a stage can provably take audio (W2); the transcription role stays chat's media machinery's for now.
- 2026-10-13 — `translate-check` stays out: translation quality is too model-dependent for a shipped starter to promise; added only on the user's word at build (the default, confirmed 2026-10-13).
- 2026-10-13 — `redact-text`'s category list is finalized at build in its schema, starting from the sketched six (emails, phones, people, addresses, credentials/keys, ids) — flat, enumerable, nothing a small model fumbles (the default, confirmed 2026-10-13).

**Guardrail(s).** Parse-and-validate on load for all seven starters; mock-member plays with every schema stage held to its schema and `digest`'s chain depth, budget and position-narration asserted; compiled-in byte-identity; `--from` copies editable. Optional real-weights pass per the model families, one family at a time, each starter once, recorded.

**Acceptance criteria:**
- [ ] `symphonies list` on a fresh install shows seven starters; each plays over mock members, schema stages held.
- [ ] `digest` plays as a chain — `summarize-verify` recursed on the same turn, caps enforced, positions narrated.
- [ ] Compiled-in copies byte-identical to the asset files; seeded skip-if-present.
- [ ] No grammar, walk, cap or surface change: the existing symphony suites green and byte-stable.

**Scope note.** Earmarked for v0.1.7; ungated — independently buildable. Out of scope: audio-input stages, retrieval in stages, any runner/walk change, Orchestrator changes.
