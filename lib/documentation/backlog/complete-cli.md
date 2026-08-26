# apogee complete — one-shot CLI (the walking-skeleton milestone)

**What / why.** The first end-to-end user-visible path: `apogee complete "prompt"` reads config, routes through the Harness to the Anthropic backend, and streams the answer to stdout. Flags: -m/--model, -s/--system, -t/--temperature, -n/--max-tokens, --quiet/--verbose, --image/--file attachments (cloud image parts through the IR the Anthropic item already maps; local backends reject with a clear not-yet-supported error), --context (extra context injection), and --all-backends (run one prompt against every configured backend — Ommi parity); reads stdin when piped; TTY detection gates any decoration; exit codes distinguish user error / backend error / success. This closes the skeleton — config → harness → backend → terminal — and is the moment the architecture is proven or falsified. Tool use is deliberately excluded (arrives with agentloop-core as `complete --tools`); the rich terminal UX layer arrives with chat-cli and retrofits here. Establishes the command-registration pattern and the exec-style test harness (run the binary against a mock-backend config) all later CLI tests reuse, and lands the interactive-never-listens invariant's first test-lock.

**Core constraint(s).**
- Interactive = pipes/HTTPS, never a server: lsof-style test asserts no LISTEN sockets mid-turn from day one (Ommi retrofitted this as OMMI-11; Apogee designs it in)
- Command files are thin: flag parsing + rendering over library calls only — the split that makes HTTP parity and exec-style testing possible
- Piped/non-TTY runs print the answer only, no decoration

**Seam + files.** lib/src/cli/source/commands/registry.cpp (one `registry.add(...)` line in `default_registry()` — `main.cpp` does not change), lib/src/cli/source/commands/complete.cpp, lib/src/cli/source/commands/helpers.cpp (initializeBackends analog: backend construction from config, resolveSystemPrompt/Temperature/MaxTokens), lib/src/cli/tests/commands/complete_test.cpp (exec-style: run the binary with a mock-backend config; lsof assertion).

**Reference (Ommi).** cmd/ommi complete.go + helpers.go. Divergence: one streaming HTTPS request per turn instead of a `claude -p` or ommi-completion spawn; the stateless nature that made complete Ommi's natural first slice (its own cpp_notes call it 'the natural first vertical slice') applies identically here.

**Decisions made** (dated):
- 2026-08-24 — Planned from Ommi's documentation (parallel digest → two planning lenses → merge → adversarial verify). Position in the queue: A's walking-skeleton closer: the shortest end-to-end path proves config→harness→backend→terminal before the loop's complexity, exactly where a falsified architecture bet is cheapest to fix.

**Open calls:**
- (consumed decision) CLI framework is **CLI11**, standardized 2026-08-25; the scaffold exists at `lib/src/cli/source/commands/` (`Command` interface + `CommandRegistry` + `RootCommand`, with `version_command.h/.cpp` as the worked example). Register by adding one `registry.add(...)` line to `default_registry()`. This item exercises the subcommand pattern end to end — completions stubs themselves land in install-check-lifecycle

**Guardrail(s).** Exec-style CLI tests with a mock-backend config (fake providers, no network) — the pattern all later CLI tests reuse; the lsof no-LISTEN assertion runs in CI.

**Acceptance criteria:**
- [ ] `apogee complete "hi" -m <anthropic-backend>` streams tokens live to a TTY and prints plain text when piped
- [ ] `echo prompt | apogee complete` works; exit codes distinguish user error / backend error / success
- [ ] Runs fully offline against the mock backend via a test config (exec-style test in CI)
- [ ] Zero listening sockets during a turn (lsof assertion in the exec test — the invariant's first test-lock)
- [ ] `apogee complete --image photo.png "describe"` sends an image part to a vision-capable cloud backend; a local backend rejects it with a clear message

**Scope note.** earmarked for v0.1.0. Gate satisfied: the Anthropic backend shipped 2026-08-26 ([MILESTONES.md](../assistant/MILESTONES.md) → Milestone D) — this is now the topmost claimable item. Construct providers via `AnthropicProvider::from_config` (`lib/src/cli/source/backends/anthropic.h`), register them on a `harness::Harness`, and stream through `harness.stream_chat` with a `TokenSink`. A `ThinkingSink` also exists on `StreamOptions`; thinking must never reach stdout on a pipe.
