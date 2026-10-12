# Execute's project opening: the context file read, the folder attached

**What / why.** Claude Code's signature is that it *arrives knowing the project*: its context file is read at session start and the codebase is reachable without ceremony. Apogee has neither ritual — measured 2026-10-11 (the execute-mode spike, W2 + W3): **zero runtime reads** of `AGENTS.md`/`CLAUDE.md`/`APOGEE.md` anywhere in `source/` (the grep's only hits are the A5 layer cards — documentation, not code), and codebase awareness needs a manual `--attach`/`/attach` (the flags are the only doors). This item gives `execute` the opening ritual: opened inside a project, it **reads the project's context file** — folded into the system context beside the environment note, said on the banner — and **attaches the working folder** — the existing attach machinery whole: indexed, the code graph per `attachments.graph` (27p's one resolution function), the map card (26q), the graph toolset scoped to it (27o), the budget's attachment share (26c/26d) untouched. Skippable and overridable; `chat` is unchanged.

**Core constraint(s).** **One attach driver**: the opening attach goes through `chat_attachments` exactly as a typed `--attach .` would — no second indexer, no special-cased budget; a re-open re-attaches through the same update path (27n). The context file is **transient system context**, like the environment note: it rides every request, is never written into the session's saved user content, and is **bounded** — read through the UTF-8 door (text mended at entry) and capped, a truncation said, never silent. The detection is cheap and local (no probes beyond the directory itself). `CLAUDE.md` is **honored read-only and never written** by anything in Apogee. ADR 0002: machine-mode execute carries the same opening (the attach line and context are already in the protocol's vocabulary — the attach events exist; the banner facts ride the existing header events), documented where machine-mode.md names the session's opening.

**Seam + files.**
- `lib/src/cli/source/presentation/cli/execute.cpp` — the project detection (the working directory is a project when it is a git repository or holds the context file), the context-file read and fold, the opening attach (default on in a project, `--attach ""` skips it, explicit `--attach` wins), the banner lines.
- `cli/chat_attachments` — consumed as-is (the one driver); `contracts/` — the context-file read helper beside the bundled-assets reads if one is needed.
- `lib/documentation/reference/machine-mode.md` — the opening's one new sentence where the session start is documented.
- Tests: `tests/presentation/cli/execute_test.cpp` — the ritual in a fixture repo (context file folded and said; the folder attached with its graph per the config; `--attach ""` skipping; a non-project directory opening bare; the cap's truncation said); the context never in saved user content (session file inspected); UTF-8 mending on a Latin-1 context file.

**Reference.** 26d/26q/27n/27o/27p — the attach machinery this composes and does not extend; the environment note (the registry's transient context — the fold's exact precedent); the execute-mode spike (2026-10-11) for W2/W3's evidence; external prior art: Claude Code's `CLAUDE.md` and the open `AGENTS.md` convention.

**Decisions made** (dated):
- 2026-10-11 — The set lands in v0.1.7 as track 41 (the user's placement call on the spike report).
- 2026-10-11 — The context file is **`AGENTS.md`** first, `CLAUDE.md` honored read-if-present (never written), in that order, first found wins (the spike's default, standing).
- 2026-10-11 — The opening attach is on in a project (a git repository, or a directory holding the context file), off elsewhere, always skippable (the spike's default, standing).

**Open calls:**
- [default: the context cap is a named constant around a few thousand words; past it the head is kept, the truncation said on the banner.]
- [default: nested context files (a subdirectory's AGENTS.md) are out of the first cut — one file at the root the session opened in.]

**Guardrail(s).** The ritual's matrix pinned in fixture repos (project/non-project × context file present/absent × skip/explicit attach); the fold golden over FakeTransport (the request carries the context beside the environment note; `-s` and the context coexist); the session file never holding the context; byte-stability of `chat` and of execute outside a project.

**Acceptance criteria:**
- [ ] `apogee execute` in a project reads the context file, attaches the folder with its graph, and says both on the banner.
- [ ] `--attach ""` opens without the folder; an explicit `--attach` replaces the default; a non-project open is today's.
- [ ] The context rides requests as transient system context, capped and mended, never saved as user content.
- [ ] `chat` byte-stable; machine-mode conformance green with the documented opening.

**Scope note.** Earmarked for v0.1.7; gated on [41a](execute-opens-bare.md) (the ritual assumes the bare door). Out of scope: writing any context file, nested context files, the persona ([`execute-coder-persona.md`](execute-coder-persona.md)).
