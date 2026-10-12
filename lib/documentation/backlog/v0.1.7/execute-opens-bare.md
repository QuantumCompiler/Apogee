# Execute opens bare: one strong model and its tools

**What / why.** `apogee execute` is the coding mode's front door, and today it refuses to open without a configured suite — measured 2026-10-11 on the installed binary in a sandbox: *"execute opens a session with a suite, and none is configured — add one: apogee config add-suite \<name\> --chat \<backend\> […], then run it with --suite \<name\>, or make it the default"*. A Claude-Code-class session is **one strong model plus tools** — on a cloud provider or a local backend alike — and demanding suite ceremony first is friction the comparison cannot carry (the execute-mode spike's W1, 2026-10-11). This item relaxes the insistence: `apogee execute -m <backend-or-model>` opens the session on that backend, bare `apogee execute` opens on `models.default`, and the refusal remains only when neither a suite, a `-m`, nor a default exists. Everything suite-flavored is untouched when a suite **is** given (`--suite`, the default suite, `--orchestrate`, warmup, residency); a bare session simply has none, exactly as `chat` runs today.

**Core constraint(s).** **One role resolver, no new rung**: a bare execute resolves roles through the existing chain (feature pin → suite rung, absent → role pointer → the conversation's backend) — nothing synthesizes a fake suite into the resolver. `/play` in a bare session works through that same chain (a stage's role resolves as any member call does); `--orchestrate` stays the suite's alone, refused bare in the command's own words (27t's validation is suite-scoped). The TUI's execute door (37d) follows for free only if it reuses execute's own open path — one door logic, never a second. Non-TTY and machine-mode execute stay byte-identical for the suite-given case; the bare case joins the same conformance pins.

**Seam + files.**
- `lib/src/cli/source/presentation/cli/execute.h/.cpp` — the open path: the insistence check becomes the three-rung door (`--suite`/default suite → `-m` → `models.default`), the banner saying which it opened on; the refusal only when all three miss, keeping today's wording for the suite half.
- `cli/tui_session.cpp` — the picker's execute door takes the same rungs (a suite when one exists, else the bare door), through execute's own open function.
- Tests: `tests/presentation/cli/execute_test.cpp` — bare open on `-m` and on the default; `/play` resolving through the chain in a bare session; `--orchestrate` refused bare with the command's words; the no-backend-at-all refusal; machine-mode execute conformance over a bare session.

**Reference.** 27s — the mode-parameterized session core (`chat` and `execute` thin faces over it; `chat` already runs suite-less, which is the whole mechanism); the one role resolver (`harness/roles.h`, six roles, the suite rung 27d); the execute-mode spike (2026-10-11) for W1's measured refusal.

**Decisions made** (dated):
- 2026-10-11 — The set lands in v0.1.7 as track 41 (the user's placement call on the spike report; 41 by the ever-assigned rule).
- 2026-10-11 — Bare execute runs **suite-less through the resolver's existing rungs** rather than synthesizing a one-member suite — `chat`'s own mechanism, no new resolver state (the spike's design, standing).
- 2026-10-11 — `--orchestrate` remains the suite's: a bare session has no symphony-offer validation surface, and the refusal names the suite as the way in.

**Open calls:**
- [default: the banner's bare-mode line says `execute — <backend> (no suite)`, the suite line unchanged when one is given; exact wording at build.]

**Guardrail(s).** The refusal matrix pinned (suite / `-m` / default / none × terminal / pipe / machine mode); a bare session's `/play` golden over mock members through the resolver chain; `--orchestrate` bare-refusal; the suite-given path byte-stable against today's goldens.

**Acceptance criteria:**
- [ ] `apogee execute -m <backend>` and bare `apogee execute` (with a default backend) open sessions; the old refusal appears only with nothing to open on.
- [ ] `/play` works in a bare session; `--orchestrate` without a suite refuses in the command's words.
- [ ] The TUI's execute door opens bare the same way, through the one open path.
- [ ] Every suite-given golden byte-stable; machine-mode conformance green.

**Scope note.** Earmarked for v0.1.7; first of track 41, ungated. Out of scope: the project opening ([`execute-project-opening.md`](execute-project-opening.md)), the persona, any resolver change.
