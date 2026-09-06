# Vendor CLI backends: codex (OpenAI), gemini (Google), ollama (Ollama cloud)

**What / why.** Completes the dual-path cloud contract (SPEC → Background divergence 2, decided 2026-08-24): every cloud vendor must work under a **subscription plan** via its official CLI as well as an **API billing plan** via its direct API. The `claude-cli` backend (shipped 2026-09-02, [MILESTONES.md](../assistant/MILESTONES.md) → Milestone L) established the pattern — persistent child process, JSONL/streamed stdout turned into typed events at an adapter boundary, transcript-authoritative resume, never reading the CLI's credentials — and this item extends the family: **codex** (OpenAI subscription), **gemini** (Google subscription), and **ollama** (Ollama cloud models). MUST BE SPLIT into one backlog doc per CLI before an agent builds — and each split doc starts with an **empirical characterization phase**, exactly as the claude design notes were produced: pin a CLI version, dump real sessions, and read the bytes before writing adapter code. Nothing about another vendor's CLI may be assumed from the claude CLI's shape — streaming JSON support, session/resume semantics, flag surfaces, auth modes, and structured-output support all differ per vendor and some may not exist; where a CLI cannot support a persistent child or token-level streaming, the split doc records the honest fallback (per-request invocation, message-granularity streaming) rather than pretending. Each backend registers as an ordinary config type (`codex-cli`, `gemini-cli`, `ollama-cloud` — recorded enum-widening events per config-engine's constraint), emits the same typed events as every other provider, and must pass the shared loop-conformance suite.

**Core constraint(s).**
- Never read any vendor CLI's credential store, implement its OAuth flow, or parse its session files off disk — the CLI authenticates; Apogee only spawns it (the claude-cli rule, generalized to the whole family)
- Never modify or bundle a vendor binary; resolve from PATH or explicit config path; environment inherited wholesale
- The adapter boundary is total: no vendor wire format leaks past the backend layer; unknown wire event types are allowlist-ignored (log-and-drop), never errors
- stderr never merges into stdout; separate reader with a bounded tail surfaced on failure
- Per-CLI behavior is characterized against a real pinned CLI build before adapter code is written; the characterization (version, dumped fixtures) lands in the split doc and the fixture directory
- A persistent child is not a listening socket — the interactive-never-listens invariant stays covered

**Seam + files.** Shared, and **already built** by the claude-cli item: `source/platform/child_process.h/.cpp` (posix_spawn + three pipes + raw-fd reader), `source/backends/jsonl_framer.h/.cpp` (the carry buffer), and `source/backends/cli_event.h` (the typed union). Reuse those rather than rewriting them per vendor; only the wire→event mapping is vendor-specific. Per split doc: lib/src/cli/source/backends/codex_cli.h/.cpp, gemini_cli.h/.cpp, ollama_cloud.h/.cpp with per-vendor wire→event mapping units, config schema/template additions per type, lib/src/cli/tests/backends/ per-CLI fixture-replay suites (adversarial chunk sizes, per the claude-cli guardrail pattern).

**Reference (Ommi).** None — Ommi never carried these vendors; this is pure Apogee divergence. The in-repo reference is the **vendor-CLI design notes** in [MILESTONES.md](../assistant/MILESTONES.md#appendix--vendor-cli-design-notes-carried-forward-from-the-claude-cli-backend-item) — whose process model, reader discipline, wire-event mapping, and fixture-replay testing shape are the template for all three CLIs here. They were carried there when claude-cli-backend shipped (2026-09-02); read Milestone L alongside them for where the shipped code deliberately diverged.

**Decisions made** (dated):
- 2026-08-24 — Cloud set widened to four vendors, each dual-path (subscription via CLI / API billing), by user decision. The claude-cli persistent-child pattern is the family template; per-CLI characterization is mandatory before building.

**Open calls:**
- [user] Priority order within the family: codex vs gemini vs ollama first?
- [user] Ollama cloud: via the ollama CLI (as stated), or is its direct API the better sole path for cloud models? Characterize both before deciding — the CLI may just proxy the API
- [default: mirror claude-cli's subscription|bare config shape wherever a vendor CLI distinguishes auth modes] Per-vendor mode configuration
- [default: defer until characterization] Whether each CLI supports structured output / schema constraints, and what maps to `complete_structured`

**Guardrail(s).** Per-CLI recorded-fixture replay suites at adversarial chunk sizes (the claude-cli pattern); the loop-conformance suite green per backend; a grep-style test per vendor that no path reads its credential/session storage; CLI-version pinning noted in fixtures so schema drift is detected, not silently absorbed.

**Acceptance criteria:**
- [ ] Placeholder-level: split into one doc per CLI, each opening with a real characterization (pinned version, dumped session fixtures) before any adapter code
- [ ] Per split doc: the vendor's models work in `chat`/`complete` by `-m` alone under a subscription login, with the best streaming granularity the CLI actually offers (documented honestly); typed events flow through the shared loop and thinking view; the conformance suite passes; the harness never touches the vendor's credential store (test-locked)
- [ ] The API-billing path for the same vendor (direct backend where it exists) and the CLI path are selectable per backend entry and can coexist in one config

**Scope note.** Gate satisfied: the claude-cli backend shipped 2026-09-02 ([MILESTONES.md](../assistant/MILESTONES.md) → Milestone L), and with it the reusable child-process, framing, and typed-event machinery this item builds on (ring convention: also assumes the complete v0.1.0 set). Out of scope: the vendors' direct-API backends (anthropic-backend and openai-google-backends already shipped those); local Ollama model *pulls* (that is a model-source concern owned by the models area, not a backend).
