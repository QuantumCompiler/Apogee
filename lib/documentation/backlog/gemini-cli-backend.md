# Gemini CLI backend: Google's subscription path as a spawned child

**What / why.** The **subscription-plan** path for Google, beside the API-billing path already shipped in `backends/google.h` ([MILESTONES.md](../assistant/MILESTONES.md) → Milestone I). A user with a Google AI plan drives their models through the official `gemini` CLI they are already logged into, and Apogee spawns it rather than asking for an API key. Together with `codex-cli` this completes SPEC's dual-path claim for the two direct-API cloud vendors: **both plans work, and the choice is per backend entry**, so one config can hold `gem` (API key) and `gem-sub` (subscription) at once.

**This item begins with characterization, not code.** Nothing about the `gemini` CLI may be assumed from the `claude` CLI's shape, or from `codex`'s. Streaming-JSON support, session and resume semantics, the flag surface, auth modes, and structured-output support all differ per vendor and some may not exist. The first commit pins a `gemini` version, dumps real sessions to fixtures, and writes down what was observed — including where the CLI offers less than the family template assumes. Adapter code is written against that dump, not against this document's guesses.

**Core constraint(s).**
- **Never read the CLI's credentials.** Not its config directory, not a keychain entry, not a session file — and note that a Google CLI is more likely than most to hold an OAuth refresh token, which makes the rule matter more, not less. The CLI authenticates; Apogee only spawns it. Mechanically enforced by `cli.no_vendor_credentials`.
- **Never modify or bundle the vendor binary.** Resolve from `PATH` or an explicit `binary` config path; the user installs and logs in themselves; no built-in auth method is suppressed.
- **Environment inherited wholesale.** Never inject `GEMINI_API_KEY` (or `GOOGLE_API_KEY`) from harness config. If the CLI prefers an environment key over a subscription login, that precedence must be named in auth error messages — it is the same trap that silently moves a plan onto per-token billing.
- **The adapter boundary is total.** No vendor wire shape leaks past `backends/`; unknown wire event types are allowlist-ignored, never errors.
- **stderr never merges into stdout**, with a bounded tail surfaced on failure.
- A persistent child is not a listening socket; the interactive-never-listens invariant stays covered.

**Seam + files.** Reuse the family machinery — `source/platform/child_process.h/.cpp`, `source/backends/jsonl_framer.h/.cpp`, `source/backends/cli_event.h` — rather than rewriting it. New: `lib/src/cli/source/backends/gemini_cli.h/.cpp` plus a `gemini_cli_events.h/.cpp` mapping unit, a `gemini-cli` config type with `binary`/`mode`, `lib/src/cli/tests/backends/gemini_cli_test.cpp`, and `lib/src/cli/tests/fixtures/gemini_cli/*.jsonl` from the characterization.

**Reference (Ommi).** None — Ommi never carried this vendor. The in-repo reference is the **vendor-CLI design notes** in [MILESTONES.md](../assistant/MILESTONES.md#appendix--vendor-cli-design-notes-carried-forward-from-the-claude-cli-backend-item) plus **Milestone L** for the shipped divergences. **`codex-cli` has landed** (2026-09-06), so read its characterization in Milestone L too — three vendors' worth of observed behaviour is a far better guide to what varies than one, and the spread is already wide: token-level deltas and inline schemas on Claude, message-level events with a schema *file* and a pinned sandbox on codex, and no event stream at all on ollama. Assume nothing about gemini from any of them.

**Decisions made** (dated):
- 2026-08-24 — Cloud set widened to four vendors, each dual-path, by user decision. The claude-cli persistent-child pattern is the family template; per-CLI characterization is mandatory before building.
- 2026-09-06 — **Split from the `vendor-cli-backends` guard document** into one item per CLI, and placed **second in the family, after `codex-cli`** *(user call)*.
- 2026-09-06 — The `gemini` CLI **is now installed** on the dev host, so this item's practical gate is satisfied; building it needs a logged-in session for the characterization.

**Open calls:**
- [user] Nothing blocking the *specification*. Building requires `gemini` installed and logged in on the machine doing the work.
- [default: mirror `claude-cli`'s `subscription` | `bare` config shape] Per-vendor auth-mode configuration, **if** the CLI distinguishes modes. A mode that does not exist must not be invented for symmetry.
- [default: defer until characterization] Schema-constrained output, and therefore whether `complete_structured()` has a real implementation here.
- [default: defer until characterization] Whether a persistent child is possible at all. With no stdin-fed turn stream the honest fallback is a per-invocation spawn with message-granularity streaming, recorded as a limitation rather than hidden.

**Guardrail(s).** A recorded-fixture replay suite at adversarial chunk sizes, asserting an identical event sequence at every size; the pinned CLI version recorded alongside the fixtures so schema drift is detected rather than absorbed; a conformance-suite row for this backend; `cli.no_vendor_credentials` covering the credential rule automatically.

**Acceptance criteria:**
- [ ] A characterization commit lands first: pinned `gemini` version, dumped session fixtures, and a written account of streaming granularity, session/resume semantics, and auth modes — including where it offers less than the template assumes
- [ ] Google models answer in `chat` and `complete` by `-m` alone under a subscription login, with the best streaming granularity the CLI actually provides
- [ ] Typed events flow through the shared loop and thinking view with no special-casing above `backends/`; the conformance suite passes
- [ ] The fixture replay suite passes byte-for-byte identically at every chunk size
- [ ] The harness never touches the CLI's credential storage (test-locked)
- [ ] An API-key `google` backend and a subscription `gemini-cli` backend coexist in one config and are selectable per entry

**Scope note.** Gated ring, vendor-CLI family, **second position** (after `codex-cli`; no hard dependency on it, but building it second means one vendor's characterization already exists to compare against). Gate satisfied: `claude-cli` shipped 2026-09-02 ([MILESTONES.md](../assistant/MILESTONES.md) → Milestone L); ring convention also assumes the complete v0.1.0 set. **Additionally gated in practice on the `gemini` CLI being installed.** Out of scope: Google's direct-API backend (shipped, Milestone I).
