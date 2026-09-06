# Codex CLI backend: OpenAI's subscription path as a spawned child

**What / why.** The **subscription-plan** path for OpenAI, beside the API-billing path already shipped in `backends/openai.h` ([MILESTONES.md](../assistant/MILESTONES.md) → Milestone I). A user with a ChatGPT plan drives their models through the official `codex` CLI they are already logged into, and Apogee spawns it rather than asking for an API key. This is the first extension of the vendor-CLI family after `claude-cli` (Milestone L), and it exists to make SPEC's dual-path claim true for a second vendor: **both plans must work, and the choice is per backend entry**, so one config can hold `gpt` (API key) and `gpt-sub` (subscription) side by side.

**This item begins with characterization, not code.** Nothing about the `codex` CLI may be assumed from the `claude` CLI's shape. Streaming-JSON support, session and resume semantics, the flag surface, auth modes, and structured-output support all differ per vendor, and some may not exist at all. The first commit of this item pins a `codex` version, dumps real sessions to fixtures, and writes down what was observed — including the honest answer where the CLI is *worse* than Claude's. Adapter code comes after, and is written against what the dump showed rather than what this document guesses.

**Core constraint(s).**
- **Never read the CLI's credentials.** Not its config directory, not a keychain entry, not a session file. The CLI authenticates; Apogee only spawns it. This is a SPEC principle binding the whole family, mechanically enforced by `cli.no_vendor_credentials`, and the tempting shortcut it forbids is real: when a resume fails, reading the vendor's session file *would* recover the conversation. Replaying Apogee's own transcript is the supported path.
- **Never modify or bundle the vendor binary.** Resolve from `PATH` or an explicit `binary` config path; the user installs and logs in themselves; no built-in auth method is suppressed.
- **Environment inherited wholesale.** Never inject `OPENAI_API_KEY` from harness config — the same trap the Claude backend documents for `ANTHROPIC_API_KEY`, where a stale key silently redirects a subscription to per-token billing. If the CLI has a precedence order between a key and a login, surface it by name in auth error messages.
- **The adapter boundary is total.** No vendor wire shape leaks past `backends/`. Unknown wire event types are allowlist-ignored (log-and-drop), never errors — an unknown-type crash turns a CLI upgrade into an outage.
- **stderr never merges into stdout**, with a bounded tail surfaced on failure.
- A persistent child is not a listening socket; the interactive-never-listens invariant stays covered by the existing checks.

**Seam + files.** The reusable machinery **already exists** and must not be rewritten: `source/platform/child_process.h/.cpp` (posix_spawn, three pipes, raw-fd reads, separated stderr), `source/backends/jsonl_framer.h/.cpp` (the carry buffer), and `source/backends/cli_event.h` (the typed union). Only the wire→event mapping is vendor-specific. New: `lib/src/cli/source/backends/codex_cli.h/.cpp` plus a `codex_cli_events.h/.cpp` mapping unit, a `codex-cli` config type with its `binary`/`mode` fields, `lib/src/cli/tests/backends/codex_cli_test.cpp`, and `lib/src/cli/tests/fixtures/codex_cli/*.jsonl` from the characterization.

**Reference (Ommi).** None — Ommi never carried this vendor. The in-repo reference is the **vendor-CLI design notes** in [MILESTONES.md](../assistant/MILESTONES.md#appendix--vendor-cli-design-notes-carried-forward-from-the-claude-cli-backend-item), whose process model, reader discipline, and fixture-replay shape are the template; read **Milestone L** alongside them for where the shipped `claude_cli` deliberately diverged from those notes (the event union lives in `backends/`, and a persistent child needed an event *queue* so a read carrying past a terminal event does not fold the next turn into this one).

**Decisions made** (dated):
- 2026-08-24 — Cloud set widened to four vendors, each dual-path, by user decision. The claude-cli persistent-child pattern is the family template; per-CLI characterization is mandatory before building.
- 2026-09-06 — **Split from the `vendor-cli-backends` guard document** into one item per CLI, and placed **first in the family** *(user call)*. Rationale: OpenAI's direct-API backend already ships, so completing its subscription half makes the dual-path claim demonstrable end to end for one vendor soonest.
- 2026-09-06 — **The `codex` CLI is not installed on the primary dev host.** That is a gate on building this item, not on specifying it: the acceptance criteria below require a real characterization, which needs the binary.

**Open calls:**
- [user] Nothing blocking the *specification*. Building requires `codex` installed and logged in on the machine doing the work.
- [default: mirror `claude-cli`'s `subscription` | `bare` config shape] Per-vendor auth-mode configuration, **if** the CLI distinguishes modes at all. Characterization decides whether the field is meaningful here; a mode that does not exist must not be invented for symmetry.
- [default: defer until characterization] Whether the CLI supports schema-constrained output, and therefore whether `complete_structured()` has an implementation on this backend or degrades to prose parsing.
- [default: defer until characterization] Whether a persistent child is possible at all. If the CLI has no stdin-fed turn stream, the honest fallback is a per-invocation spawn with message-granularity streaming, **recorded as a limitation rather than hidden** — the family template is a shape to aim at, not a promise to fake.

**Guardrail(s).** A recorded-fixture replay suite at adversarial chunk sizes (1/2/3/7/64/4096 and whole-file), asserting an identical event sequence at every size — the `claude_cli` pattern, which catches the whole framing bug class offline. The pinned CLI version is recorded in the fixture directory so schema drift is detected rather than silently absorbed. The loop-conformance suite (`tests/agentloop/conformance_test.cpp`) gains a row for this backend. `cli.no_vendor_credentials` already covers the credential rule and will cover this backend automatically.

**Acceptance criteria:**
- [ ] A characterization commit lands first: pinned `codex` version, dumped session fixtures, and a written account of the streaming granularity, session/resume semantics, and auth modes the CLI actually offers — including where it offers less than Claude's
- [ ] OpenAI models answer in `chat` and `complete` by `-m` alone under a subscription login, with the best streaming granularity the CLI actually provides (documented honestly)
- [ ] Typed events flow through the shared agent loop and the thinking view with no special-casing above `backends/`; the conformance suite passes for this backend
- [ ] The fixture replay suite passes byte-for-byte identically at every chunk size
- [ ] The harness never touches the CLI's credential storage (test-locked by `cli.no_vendor_credentials`)
- [ ] An API-key `openai` backend and a subscription `codex-cli` backend coexist in one config and are selectable per entry

**Scope note.** Gated ring, vendor-CLI family, **first position**. Gate satisfied: `claude-cli` shipped 2026-09-02 with the reusable child-process, framing, and typed-event machinery ([MILESTONES.md](../assistant/MILESTONES.md) → Milestone L); ring convention also assumes the complete v0.1.0 set. **Additionally gated in practice on the `codex` CLI being installed** for characterization. Out of scope: OpenAI's direct-API backend (shipped, Milestone I).
