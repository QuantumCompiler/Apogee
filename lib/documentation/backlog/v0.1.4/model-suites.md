# Suites: a named bundle of models

**What / why.** Apogee already runs an *implicit* suite — the spike (2026-10-03, sandboxed probe on the shipped binary) made it visible in one command: six roles (chat, embedding, extraction, vision, transcription, utility) resolved by **one chain** (`harness/roles.h` — override > per-feature backend > role pointer > conversation > default), each with a `config set-default-<role>` pointer, and `apogee models status` printing every role, its backend, and *which rung chose it* ("extraction: root (via models.default)"). What does not exist is the **name**: the pointers are one global set, so a second configuration — a research suite on the 12B with a 3B utility, a fast suite all on the 3B — means rewriting `models.default_*` by hand each time. This item adds **suites as a config unit**: a `suites:` block naming members per role (root/chat, utility, embedding, extraction, vision, transcription — any subset; unnamed roles fall through the existing chain), with per-member overrides for the two knobs that make small helpers *small* — a context window (the per-backend `--context-size` exists; this pins it per suite membership) and a toolset (riding 26g's shipped selection machinery, which already ranks and trims per model). Selection: `--suite <name>` at launch and `/suite` mid-chat, completable through the one command table; no suite selected means exactly today's behavior, byte for byte. **Nothing here depends on MLX** (the user's question, answered 2026-10-03): a member is any configured backend — the shipped llama.cpp stack is the whole story today, and an MLX backend (27a) simply becomes eligible when it exists. It closes the spike's **W1**; the siblings build on it: [suite-residency.md](suite-residency.md) (27e), [suite-consult.md](suite-consult.md) (27f), [suite-validation.md](suite-validation.md) (27g).

**Core constraint(s).**
- **One resolver, one new rung.** The chain in `harness/roles.h` exists because Ommi shipped resolution twice and the copies disagreed; the suite rung is added **inside that one function**, reported in `ResolvedFrom` like every other rung, and `models status` keeps answering "why this backend" for suites too. No surface grows its own suite lookup.
- **One config-mutation path:** the `suites:` block is written only through the comment-preserving editor; `config add-suite` / `set-suite` verbs compose onto the same writer add-backend uses.
- **No suite, no change:** with nothing selected the chain collapses to today's exactly (the roles.h contract — "nothing that works today changes behaviour" — extends to this rung).
- **A member is a backend key, validated in the caller's idiom** (roles.h deliberately doesn't validate): a suite naming a missing backend fails at use with the existing wording, and `check` gains the row per its conventions.
- **`serve` parity:** requests resolve through the same chain; the suite a server session uses is config's named default, never per-request state that could leak between clients. Never-listens is untouched — suites are resolution, not transport.
- Code style carries: `.h`/`.cpp` pairs, smart pointers only; the harness never includes backends.

**Seam + files.**
- `harness/roles.h/.cpp`: the suite rung (`RoleRequest` gains the active suite's name; `ResolvedFrom::Suite`), between the per-feature backend and the global role pointer — a session's suite speaks for its roles, an explicit `-m` and a feature's pinned backend still win.
- `contracts/config.h/.cpp` + `config_edit.cpp`: the `suites:` block (members per role; optional per-member `context_size`, `toolset`) and its editor entries.
- `commands/chat.cpp` (+ the one command table / `chat_completer`): `--suite`, `/suite` with completion (the 24/26o precedents), the banner naming the active suite.
- `commands/models_cmd` (`status`): the suite column/rung in the existing output.
- Tests: `tests/business/harness/roles` chain tables extended (suite set/unset × every rung); config round-trips with comments; `models status` goldens.
- Consumes: 26g (shipped) for toolset trimming; 26k's `draft:<backend>` precedent for backend references in config; the shipped shell-completion resolver (`__complete`, [Milestone G](../../assistant/MILESTONES.md#milestone-g--the-terminal-ux-layer)) picks up suite names as a free rider.

**Reference (Ommi).** The role pointers and the one-resolver discipline are the ported analog — `roles.h`'s own header records Ommi's duplicated-resolver bug as its reason to exist. **A named suite unit has no Ommi analog**; Ommi's nearest shape is training's per-run judge/teacher providers (`initSingleBackend`), task-scoped model bundles that never reached chat.

**Decisions made** (dated):
- 2026-10-03 — Asked for by the user ("Suites"); placed in **v0.1.8** beside the MLX track at their direction ("Add this v0.1.8"), taking 27d–27g per the release-prefix rule. Independence from MLX is explicit: members are backends, llama.cpp today.
- 2026-10-03 — The spike's finding that shaped the shape: the role system *is* a suite without a name, so this item is a config unit and a resolver rung, not a new execution path.

**Open calls:**
- [default: the suite rung sits between the per-feature backend and the global role pointer — a feature that pins a backend (a collection's `backend:`) keeps winning, since it pinned for a reason] Rung position.
- [default: `/suite off` returns to the global pointers mid-chat; the session's suite is session state, saved with the chat like `/model`'s choice] Session semantics.
- [default: per-member knobs start at exactly two — `context_size` and `toolset` — and grow only when an item needs a third] Member schema.

**Guardrail(s).**
- The chain tables: every rung × suite present/absent, including the no-suite collapse to today's behavior (golden against the pre-change tables).
- Config editor round-trip with comments preserved; a suite naming a missing backend surfaces in `check` and at use with the existing error idiom.
- `models status` golden with a suite active: every role shows the suite rung where it answered.
- Toolset and window pins observed at the wire (the request recorder shows the member's window and trimmed tools).

**Acceptance criteria:**
- [ ] `apogee config add-suite research --chat root --utility helper --embedding embedder` writes the block through the editor; `apogee chat --suite research` resolves utility to `helper`, and `models status` says so via the suite rung.
- [ ] `/suite fast` mid-chat switches the bundle, completable; `/suite off` restores the global pointers; both survive resume.
- [ ] With no suite configured or selected, the resolution tables are byte-identical to today's.
- [ ] A suite member with `context_size: 4096` runs its calls at that window (wire-recorded), independent of the backend's own default.

**Scope note.** Item **27d**, earmarked for **v0.1.4**; gated on nothing pending — **not** on the MLX items it shares the table with. Out of scope: residency management ([27e](suite-residency.md)); model-initiated delegation ([27f](suite-consult.md)); validation policy ([27g](suite-validation.md)); any new execution path (suites choose backends; they do not call them differently).
