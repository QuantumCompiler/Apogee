# Developer Guide

This document describes the layout of the Apogee repository and what each file does. For user-facing documentation, see [`README.md`](../../../README.md) (not yet written); for how to work in the repo, see [CLAUDE.md](CLAUDE.md).

---

## Quick start

```bash
lib/scripts/cicd.sh --test
```

The repo-wide entry point, and the one every CI runner calls: it builds each application for the host's native target and runs its suite. For the CLI alone, `make -C lib/src/cli test` does the same thing; `make -C lib/src/cli help` lists every target.

Requirements: CMake ≥ 3.25, a C++20 compiler, and git. `format` / `lint` additionally need `clang-format` / `clang-tidy` (Homebrew LLVM on macOS — Apple's toolchain ships no clang-tidy).

---

## One application, one build

**Each application under `lib/src/<app>/` is a self-contained CMake project.** Its presets, cmake helpers, pinned third-party code, style config, and build output all live beside its source. Nothing above `lib/src/<app>/` needs to know how that application compiles.

Today that is `lib/src/cli`. The GUI applications land later as siblings (`lib/src/darwin|linux|windows`), each owning its build the same way, and each joining the `APPS` list in `lib/scripts/cicd.sh`.

The one build-related file outside an app directory is `.github/workflows/ci.yml`, because GitHub requires workflows at `.github/workflows/`. It is deliberately a thin caller into `cicd.sh` for exactly that reason.

---

## Directory Layout

```
Apogee/
├── .github/
│   └── workflows/ci.yml     — CI: six-target matrix + lint + llama proof + clean-room build
│                              (thin caller into cicd.sh; here only because GitHub requires it)
├── .claude/
│   └── skills/
│       ├── apogee-backlog-item/        — Repo-local skill: take the next backlog item per the docs-first process
│       ├── apogee-create-backlog-item/ — Repo-local skill: spec a new item into the backlog + index + roadmap
│       ├── apogee-document-update/     — Repo-local skill: pre-MR docs pass — reconcile every doc against the branch diff
│       └── apogee-pull-request/        — Repo-local skill: draft the MR description from the branch's docs evidence
├── .gitignore
└── lib/
    ├── documentation/       — Project documentation
    │   ├── assistant/       — Contributor docs (CLAUDE, SPEC, ROADMAP, MILESTONES, this file)
    │   └── backlog/         — The work queue: pending work, one document per item (see its README)
    ├── scripts/             — Repo scripts
    │   ├── cicd.sh          — CI/CD entry point: builds every app for any of the six release
    │   │                      targets (--platform linux|macos|windows × x64|arm64, or all;
    │   │                      non-native targets defer to the CI matrix), --fresh = clean-room
    │   │                      clone of github.com/QuantumCompiler/Apogee at that branch; --test, --clean
    │   └── cicd-completion.bash — Tab completion for cicd.sh's flags (source from your shell rc)
    └── src/                 — Application source, one self-contained project per app
        ├── cli/             — The CLI application
        │   ├── CMakeLists.txt   — Build root: standard, options, target wiring, link-policy assertion
        │   ├── CMakePresets.json— One preset per release target + `default` (host native)
        │   ├── Makefile         — Thin wrapper (build/install/test/lint/format/clean/fresh)
        │   ├── .clang-format    — Formatting rules (make format / make format-check)
        │   ├── .clang-tidy      — Static analysis, incl. the smart-pointer ownership gate
        │   ├── cmake/
        │   │   ├── ApogeeDependencies.cmake — FetchContent declarations + how to add a dependency
        │   │   ├── ApogeeLinkPolicy.cmake   — Configure-time assertion that nothing links the CLI executable
        │   │   └── ApogeeWarnings.cmake     — Shared warning flags for first-party targets
        │   ├── assets/          — Shipped data files: config.yaml (the starter config, held
        │   │                      byte-identical to the embedded template by a test)
        │   ├── third_party/     — Vendored/pinned code, never edited in-tree (see its README)
        │   ├── build/           — Build output, one dir per preset (git-ignored)
        │   ├── source/          — main.cpp + one package dir per concern
        │   └── tests/           — Test dirs mirroring source/'s packages
        └── <darwin|linux|windows>/ — GUI applications, one per platform (future — down the road)
```

---

## `lib/src/cli/source/` — the CLI application

Two CMake targets, and the split between them is load-bearing:

| Target | Contents |
|---|---|
| `apogee_core` (static library) | Every capability. Tests link this; the future HTTP server will link this. It is the whole product minus an entry point. |
| `apogee` (executable) | A thin face: argv in, exit code out. Nothing else. |

That split is what makes surface parity structural rather than something a reviewer has to notice ([SPEC.md](SPEC.md) → Principles). It is enforced at configure time — `apogee_assert_link_policy()` in `lib/src/cli/CMakeLists.txt` fails the build if any target ever links `apogee`.

### Packages

| Path | Role |
|---|---|
| `main.cpp` | Entry point. Constructs a `RootCommand` and returns its exit code. |
| `version/` | Build identity — semantic version, git commit, build date, target — stamped in at configure time. `version.h/.cpp` |
| `platform/` | **The portability seam.** The one place platform `#ifdef`s are expected; everything else asks this header. `platform.h/.cpp` |
| `ansi/` | Colour and styling, and the one function that decides whether to emit any (`resolve_color`). `ansi.h/.cpp` |
| `logger/` | Persisted chat sessions and the daily operational log. A session records *what was said*; the log records *what the program did*. |
| `commands/` | The CLI scaffold: the `Command` interface, the registry, the root command, and the built-in commands. |
| `harness/` | **The core.** The config engine (typed loader, `${ENV}` expansion, template, comment-preserving edits, on-disk paths), the **layout contract** (`layout.h` — the single declaration of what `~/.apogee/` contains), and the provider interface, canonical message IR, router, and context-window table. Includes nothing from `backends/` — enforced. |
| `backends/` | Every provider plus the infrastructure they share: `mock`, `anthropic`, `openai`, `google`, and `llamacpp`; the HTTP client and SSE parser for the cloud ones; the `llama_runtime` seam, chat templates, and token arithmetic for the local one. |
| `agentloop/` | **The shared loop.** `run()` drives model→tool→model behind a Reporter, plus `ask_user`, history compaction, and transient splicing. Includes nothing from `backends/` — enforced. |
| `agent/` | Tools: the registry, dispatch with the permission gate, and `fetch_url`. Separate from the loop because a registry needs no loop; MCP and native toolsets register here later. |
| `embedstore/` | *Reserved* — chunk storage and retrieval (`embedstore-lexical-rag` item). |
| `httpserver/` | *Reserved* — `apogee serve` (`serve-public-plane` item). Server deployments only. |
| `mcp/` | *Reserved* — MCP client and tool plumbing (`mcp-client-tools-agents` item). |

Reserved packages carry a documented header and no code. They exist so every later backlog item has one obvious home, and `tests/packages_test.cpp` includes each one so a broken include path is caught the day it breaks rather than months later.

### `commands/` — the subcommand scaffold

| File | Purpose |
|---|---|
| `command.h` | The `Command` interface (`name`, `summary`, `bind`) and `RootContext`, the root-level state every command can read. |
| `registry.h/.cpp` | `CommandRegistry` — owns commands, rejects duplicate names, binds them all to an app. `default_registry()` is the built-in set. |
| `root.h/.cpp` | `RootCommand` — persistent flags (`--config`), the version flag, and `run(argc, argv)` → exit code. |
| `version_command.h/.cpp` | `apogee version`. The first real subcommand, proving the path end to end. |
| `complete.h/.cpp` | `apogee complete` — one-shot prompt in, answer out. The walking skeleton, and the first consumer of the agent loop (`--tools`). |
| `ask_prompt.h/.cpp` | The terminal `ask_user` implementation. Prompts through the status line, reads stdin, and returns a **null** AskFn when there is no terminal — which is what stops the tool being advertised. |
| `terminal.h/.cpp` | `TerminalWriter` — the single mutex every terminal write goes through. |
| `status_line.h/.cpp` | The self-overwriting status line and the spinner, with the generation counter that invalidates an in-flight repaint. |
| `thinking_view.h/.cpp` | The rolling reasoning window and its collapse-to-summary. |
| `cli_reporter.h/.cpp` | **The** `agentloop::Reporter` adapter. One implementation, shared by every interactive surface. |
| `chat.h/.cpp` | `apogee chat` — the REPL, slash dispatch, and context monitoring. |
| `chat_history.h/.cpp` | `apogee chats` — list, info, title, delete — and the auto-title prompt. |
| `input_gate.h/.cpp` | The startup typeahead flush. Called once, immediately before the first prompt. |
| `line_reader.h/.cpp` | `LineReader` and its two implementations — `PlainLineReader` (getline, for a pipe) and `EditingLineReader` (replxx, for a terminal). |
| `helpers.h/.cpp` | Shared command plumbing: flag/config resolution, stdin, base64, image parts, message assembly, and the exit codes. |
| `config_cmd.h/.cpp` | `apogee config` and its nine subcommands. A thin caller — it formats no YAML of its own; every mutation goes through `harness/config_edit.h`. |

**Adding a subcommand** touches exactly two places: the command's own `.h`/`.cpp` pair, and one `registry.add(...)` line in `default_registry()`. It then appears in `apogee --help` with no other edit — `main.cpp` never grows.

A command signals failure by throwing `CLI::RuntimeError(code)`; `RootCommand::run` turns that into the process exit code. Read `RootContext` inside your callback, never at bind time — parsing has not happened yet at bind time.

### The platform seam

`platform/` exposes the build's OS, architecture, and release-target name. The target name is one of the exact six strings shared by `cicd.sh --platform`, the CMake presets, the CI matrix, and the binary itself; `tests/platform/platform_test.cpp` is what keeps them one vocabulary.

Mechanisms that land here as later items need them: process spawning (vendor-CLI backends), PTY and terminal control (the chat UX layer), file-mode enforcement (the `0600` secrets rule), and socket peer checks (the admin plane). Each arrives as a declaration in `platform.h` with one definition per platform. **Where a mechanism has no Windows equivalent, the owning item records the skip** rather than leaving the gap silent.

---

### `harness/` — the config engine

| File | Purpose |
|---|---|
| `paths.h/.cpp` | Apogee's on-disk layout, rooted at `~/.apogee` and relocatable with `APOGEE_HOME`. `default_config_path()` is what a command uses when `--config` is absent. |
| `config.h/.cpp` | The **read path**: `Config`/`BackendConfig` structs, `parse_config`, `load_config`, `expand_env`, and the backend-type table. The only code that uses yaml-cpp. |
| `config_template.cpp` | The starter config, embedded as a raw string literal **generated from `assets/config.yaml`**. A test holds the two byte-identical. |
| `config_edit.h/.cpp` | The **write path**: pure text-surgery transforms plus `edit_config_file`, the only thing in Apogee that writes a config. |

**The rule that governs this package** is in [CLAUDE.md](CLAUDE.md) → *⚠ One config mutation path*: every config change goes through `config_edit.h`, and nothing ever serializes a `Config` back to disk. Reading is `load_config`; writing is text surgery. They are separate paths on purpose.

**Why text surgery.** yaml-cpp — like every mainstream YAML library — discards comments on parse and reorders keys on emit. A config file's comments are most of its documentation, so load-modify-save would silently delete the user's content. The helpers instead locate the affected lines and splice them, leaving every other byte alone: CRLF stays CRLF, a missing final newline stays missing, and a comment block documenting the *next* entry survives deleting this one.

**Adding an edit helper.** Write it as a pure `std::string(std::string_view, …)` transform in `config_edit.cpp`, next to its siblings. Give it a golden-file test in `tests/harness/config_edit_test.cpp` that asserts a byte-identical round trip against the comment-dense fixture, and route the command layer through `edit_config_file` so it inherits re-parse validation and the atomic write for free. Do not add a second function that opens the file itself.

**Widening the backend type set.** Add a row to `kBackendTypeNames` in `config.cpp` and an enumerator in `config.h`. The loader dispatches through that table, so nothing else changes — and `every backend type round-trips through its name` fails if the two ever disagree. Each widening is a recorded decision in the item that makes it, never a silent edit.

### `harness/` — the provider interface, IR, and router

| File | Purpose |
|---|---|
| `types.h/.cpp` | The canonical IR: `ChatMessage`, dual-mode `MessageContent`, `Tool`/`ToolCall`/`ToolResult`, `ChatRequest`/`ChatResponse`, `Usage`, `ModelInfo`, `RAGMeta`, `StatusEvent`, and their JSON conversions. |
| `provider.h/.cpp` | `LLMProvider` (four methods), `TokenSink`/`StreamOptions`, and the optional capability interfaces. |
| `harness.h/.cpp` | `Harness` (registry + capability probes) and `SimpleRouter` (the three-rung precedence). |
| `behavior.h/.cpp` | `ModelBehavior` — plain data, the layering seam. |
| `cancellation.h/.cpp` | `CancellationToken` — a shared atomic flag, cancellable from any thread. |
| `context_windows.h/.cpp` | The compiled model → window fallback table. Single owner. |
| `errors.h/.cpp` | Typed errors: `NoAvailableBackendError`, `ProviderNotRegisteredError`, `ProviderError`, `CancelledError`, `InvalidRequestError`. |

**Two rules govern this package**, both in [CLAUDE.md](CLAUDE.md) → Invariants: *⚠ The harness never includes backends* and *⚠ Capability probes never leak a cast*.

**The IR never grows provider-specific fields.** When a provider needs something nothing else has, it belongs in that backend's config entry or its adapter. The moment one vendor's concept appears in `types.h`, every other backend has to decide what to do about it.

**The transient region.** `ChatRequest::Transient` carries per-turn state that must never reach the wire or a session file — a RAG span, the side-request flag, a response schema. It is a nested struct with **no `to_json` anywhere**, so serializing it does not compile. Use `durable_messages()` for the history view. If a RAG blob reaches persisted history it is re-sent on every later turn, growing the prompt without bound — silent, and expensive.

**Routing** resolves in three rungs, each trying the literal name then the normalized form (lowercased, `.`/`:` → `-`): exact backend key, then a backend entry's `model:` field, then `models.default`. A backend key beats another entry's model field, or `-m <key>` would be ambiguous.

**Adding a backend.** Implement `harness::LLMProvider` in `backends/`, translating to and from the IR. Inherit a capability interface only if you have that capability — the Harness discovers it. Add a row to `kBackendTypeNames` in `harness/config.cpp` for the config `type:`. Model the shape on `backends/mock.h`, and honour cancellation *between chunks*, not just at entry.

### `backends/` — provider implementations

| File | Purpose |
|---|---|
| `mock.h/.cpp` | `MockProvider` (scripted turns, configurable chunk size, recorded requests) and `MockEmbeddingProvider`. No network, no model. |
| `http_client.h/.cpp` | `HttpTransport` (one request; `CurlTransport` is the real one) and `HttpClient` (retry/backoff over a transport). Shared by every cloud backend. |
| `sse_parser.h/.cpp` | A byte-fed Server-Sent Events state machine. Shared by every streaming cloud backend. |
| `anthropic_wire.h/.cpp` | IR ↔ Anthropic Messages API translation, and the extended-thinking replay cache. |
| `anthropic.h/.cpp` | The Anthropic provider: streaming and non-streaming chat, `list_models`, `count_tokens`. |
| `openai_wire.h/.cpp` | IR ↔ OpenAI **Responses API** translation. `input_items()` returns an array: one IR assistant turn with tool calls becomes a message item plus a top-level `function_call` item per call. |
| `openai.h/.cpp` | The OpenAI provider: POST `/v1/responses` with a bearer token; maps `response.*` semantic events onto the sinks. |
| `google_wire.h/.cpp` | IR ↔ Gemini `generateContent` translation. The assistant role is `model`; a tool result is a `functionResponse` part in a **user** turn, matched by name. |
| `google.h/.cpp` | The Gemini provider: model in the URL path, key in the `x-goog-api-key` header. Re-parses a whole response per chunk, since Gemini streams full objects rather than typed deltas. |
| `llama_runtime.h` | The slice of llama.cpp the local backend needs, behind an interface — model load, tokenize, decode-at-position, sample, trim, capacity, batch limit. |
| `llama_real.cpp` | The real runtime over llama.cpp's C API, compiled only under `APOGEE_ENABLE_LLAMA`; otherwise the "not built in" answer. Wraps every raw handle in a `unique_ptr` with a custom deleter. |
| `chat_template.h/.cpp` | Prompt framing for local models: ChatML, Llama 3, Mistral, a narrow name-matching registry, and ChatML as the documented fallback. A GGUF's own template always wins over this. |
| `llamacpp_tokens.h/.cpp` | Exact prompt counting and `common_prefix_length` — the KV cache's entire decision. |
| `llamacpp.h/.cpp` | The local provider: model lifecycle, one live context per conversation, a throwaway context per side request, streaming, idle unload. Also `TokenCounting`, `VisionCapable`, and `StatusReporting`. |
| `factory.h/.cpp` | Config `type:` → provider, and `build_providers()` which registers every entry on a Harness and installs the router. Lives here, not in a command, because every surface needs it. |

**The transport is injected**, which is what makes every cloud-backend test hermetic: no network, no API key, no charges — and failure modes a live endpoint will not produce on demand (a 529, a body delivered one byte at a time, a connection dropping mid-frame). `tests/support/fake_transport.h` is the scripted implementation.

**Two contracts that are easy to get wrong:**

*An error response is never streamed to the sink.* `HttpTransport` accumulates a non-2xx body into `HttpResponse::body` instead. Beyond the obvious (an error body is not what the sink was written to parse), delivering it makes the request look partly answered to `HttpClient`, which then refuses to retry a perfectly retryable 429. `FakeTransport` honours the same rule, or the fake would hide the bug.

*A stream is not retried once bytes have reached the caller.* Replaying would deliver the first half of an answer twice, with no way for the caller to tell.

**SSE never arrives one-event-per-read.** Chunk boundaries land mid-frame, mid-line, and mid-UTF-8-sequence. `SseParser` is a state machine with a carry buffer for exactly that reason, and its test replays a recorded stream at *every* chunk size from one byte up — a parser that assumes one-read-one-event passes every test written on a fast local connection and drops tokens on a slow one.

**Adding a local-inference capability.** Extend `LlamaRuntime`/`LlamaModel`/`LlamaContext` in `llama_runtime.h`, implement it in `llama_real.cpp` under the `APOGEE_ENABLE_LLAMA` guard, and add it to `tests/support/fake_llama.h`. The merge-blocking target has no llama.cpp in it, so the fake is not a convenience — it is the only place these behaviours are tested at all. Be aware of what it cannot model: it has no allocator, so batch and context limits are honoured by asking the seam for them rather than by assuming, and both of those rules exist because a real GGUF broke a build the fake called green.

**Adding a cloud backend.** Reuse `HttpClient` and `SseParser`; put the dialect in its own `*_wire.h/.cpp` so it is testable with no transport at all. Take an injected `HttpClient` in the constructor. Honour cancellation between chunks. Keep the API key in a header and out of every error message — `tests/backends/anthropic_test.cpp` → `[backends][anthropic][secrets]` is the pattern for pinning that. Then add a row to `providers()` in `tests/agentloop/conformance_test.cpp`: a backend is not done until it drives the shared loop identically to every other.

### `agentloop/` — the shared loop

| File | Purpose |
|---|---|
| `loop.h/.cpp` | `run()`, `Options`, `RunResult`. The model→tool→model cycle. |
| `reporter.h` | The observer every surface adapts. `NullReporter` discards everything. |
| `content.h/.cpp` | `TokenCount`, `splice_transient`, `compact_history`. |
| `question.h/.cpp` | `ask_user`: schema, validation, answer encoding. |

**Why it exists before the surfaces.** Extracting the loop behind an observer *before* front-ends multiply is Ommi's most load-bearing sequencing lesson — it is what kept four surfaces consistent there and made deleting one a local change. A surface is a thin adapter over `Reporter`; the loop knows nothing about terminals or HTTP.

**Three contracts worth knowing before you touch it:**

*`ask_user` is advertised if and only if there is someone to answer it.* A null `AskFn` means the tool is **absent from the request**, not advertised-and-refused. A model told it may ask questions on a surface with nobody attached will ask one, then hang or invent the answer. `advertised_tools()` is public so the rule is testable as a contract.

*Tool problems are results, never exceptions.* An unknown tool, a bad argument, a tool that threw — all come back as error results the model reads and reacts to. Aborting the turn would throw away the conversation over a hallucinated name.

*An aborted tool phase rolls back.* History resizes to the mark taken before the phase, because an assistant message whose tool calls were never answered is rejected outright by several providers.

**Transient content** rides `splice_transient` into the outgoing request and never into history. If it landed there it would be re-sent every later turn, growing the prompt without bound and feeding the model material it was told applied to one question.

### `agent/` — tools

| File | Purpose |
|---|---|
| `tool.h/.cpp` | `Tool`, `ToolRegistry`, `dispatch()`, and the ask/allow/deny permission gate. |
| `fetch_url.h/.cpp` | The `fetch_url` tool and its HTML-to-text stripper, behind an injected `UrlFetcher`. |

**Adding a tool.** Construct a `Tool`, set `writes` if it is destructive, and register it. A `writes` tool goes through the permission gate; a read-only one does not, because prompting for every read trains the user to approve without looking. `Ask` with no confirm function resolves to **deny** — a pipe or a cron job has nobody to ask, and allowing because no one objected is the wrong direction to fail.

**There is no local web-search tool, by decision** (2026-08-26). Search comes from the providers' own server-side tools; Ommi's DuckDuckGo scraper is not ported because a results-page parser breaks silently and returns nothing rather than erroring. `fetch_url` — reading a URL you were *given* — carries none of that fragility.

### The terminal UX layer

`CliReporter` is the only `agentloop::Reporter` implementation in the tree, and it should stay that way — a second one drifts from the first, and a capability that reaches one surface but not another is the parity bug the interface exists to prevent. A new interactive surface constructs one; it does not write its own.

**Answer to stdout, progress to stderr.** They are separate streams and separate objects on `CliReporter::Options` for that reason. Conflating them puts spinner frames into `apogee complete "…" | jq`.

**Everything transient goes through `StatusLine`,** including startup notices — "startup speaks on one line" is an invariant Ommi retrofitted (OMMI-14) and this project designs in. Do not reach for `std::cerr` on an interactive path; call `reporter.status().print_line()`. A permanent print bumps a generation counter so an in-flight spinner repaint drops its frame instead of landing on top of the text.

**The thinking view's erase arithmetic is exact only because nothing painted may wrap.** `repaint()` leaves the cursor on the last painted row with no trailing newline, and `erase()`'s row count depends on that. Pre-wrap to `width - indent` by codepoint. If you change one, a test asserting the exact count of `\033[A\033[2K` pairs will tell you.

**Colour is decided once,** in `ansi::resolve_color()`, across three independent inputs: `NO_COLOR` (by presence, whatever its value), an explicit `--no-color`, and whether stdout is a terminal. A disabled `Style` returns its input unchanged so no caller branches on it.

### `logger/` — sessions and the operational log

| File | Purpose |
|---|---|
| `session.h/.cpp` | `Session`, serialize/deserialize with resume warnings, and the save/load/list filesystem layer. |
| `operational.h/.cpp` | The daily append-only log. One greppable line per event. |

**The session file is rewritten after every completed turn.** Crash safety is the requirement — a `kill -9` mid-conversation must leave every finished turn on disk — and that is a property of *when* the write happens, which is why persistence was specced with the REPL rather than added afterwards. The write goes through `harness::write_file_atomically`, so an interrupted write leaves the previous session intact.

**Resume degrades, never fails.** A legacy schema, a vanished backend, a field of the wrong shape, an unreadable message — each produces a `ResumeWarning` and a working session. A conversation that cannot be reopened because one config key moved is worse than one that reopens with a note. `deserialize` throws for exactly one thing: JSON that will not parse at all.

**`chat_id` is immutable.** Renaming sets `custom_name`. An id that changed would break every reference to the session that already exists.

**The operational log never throws.** A full disk degrades to a missing log line, never to a failed conversation.

### `commands/chat.cpp` — the REPL

**Context is measured against the request about to be sent** — the saved history *plus* the incoming turn — and compaction folds only the prior history. Measuring the saved history alone means the first turn always reads as empty and a single large prompt never trips the threshold; that bug shipped once and `context is measured against the message about to be sent` now pins it.

**Every configured backend is constructed at startup.** That is what makes `/model` an instant switch with history carried over, and it only works because history is neutral IR rather than a vendor transcript.

**Input is read through `LineReader`,** which has two implementations rather than one reader with a conditional: `make_line_reader` builds the replxx editor only when **both** stdin and stdout are terminals. Both, because replxx draws on stdout — a terminal stdin with a redirected stdout would write escape sequences into the redirect, and that redirect is the answer the user asked for. A piped conversation is a first-class way to use `apogee chat` and gets `std::getline`, tested on its own.

**Beware when writing a PTY test against the REPL: replxx puts the terminal in raw mode, where Enter is `\r`, not `\n`.** The kernel performs no translation, so a bare `\n` lands in the edit buffer as literal text rather than submitting the line. Both PTY harnesses send `\r`, and both were briefly wrong about this when line editing landed.

**The typeahead flush runs exactly once, before the first prompt.** Never between turns — typing a follow-up while the model generates is legitimate typeahead, and eating it would be worse than the problem the gate fixes.

---

## `lib/src/cli/tests/` — tests

Catch2 v3, discovered into ctest by `catch_discover_tests`. The directory mirrors `source/`'s packages.

| Path | Covers |
|---|---|
| `smoke_test.cpp` | Version stamping and that dependencies are linked and usable. |
| `packages_test.cpp` | Every reserved package header compiles and is includable. |
| `platform/platform_test.cpp` | Host detection and the six-target name vocabulary. |
| `commands/registry_test.cpp` | Registration, lookup, ordering, duplicate and null rejection. |
| `commands/root_test.cpp` | The real parsing path: help, subcommand dispatch, `--config`, unknown commands. |
| `harness/config_test.cpp` | The loader: typed parsing, `${ENV}` expansion, case-collision rejection, the template byte-match, and a garbage-input battery asserting a message rather than a crash. |
| `harness/config_edit_test.cpp` | The golden-file suite: byte-identical round trips over a comment-dense fixture, comment ownership on delete, CRLF and final-newline handling, and the atomic-failure paths. |
| `harness/paths_test.cpp` | `APOGEE_HOME` resolution, the `~/.apogee` default, and the no-home error. |
| `harness/types_test.cpp` | The IR: both content shapes round-tripping, tool calls, typed errors on bad input, and the transient-exclusion contract. |
| `harness/harness_test.cpp` | Router precedence table-tested across all three rungs, cancellation mid-stream, capability probes, `ModelBehavior`, and the context-window table. |
| `backends/mock_test.cpp` | MockProvider contract tests — they pin the `LLMProvider` interface itself, so a later item changing it breaks them. |
| `backends/sse_parser_test.cpp` | The SSE state machine, replayed at every chunk size from one byte up. |
| `backends/http_client_test.cpp` | Retry/backoff, `retry-after`, the no-retry-after-delivery guard, and the error-bodies-are-not-streamed contract. |
| `backends/anthropic_test.cpp` | Fixture-replayed streams (seven chunk sizes), the wire mapping, thinking replay, error shapes, and the API-key secrets guardrail. |
| `backends/factory_test.cpp` | Construction per type, that a keyless cloud type names **its own** environment variable, and that one unbuildable backend does not stop the others. |
| `backends/llamacpp_test.cpp` | The local backend against a scripted runtime: KV reuse as an exact token count, side-request isolation, batch chunking, the context wall, idle unload, load failure, and the vision capability. |
| `backends/chat_template_test.cpp` | Local prompt framing, the conservative name-matching registry, and that a model's own template wins. |
| `backends/openai_test.cpp` | The Responses dialect, split-boundary streaming, tool-call reassembly from item + argument deltas, effort banding, error shapes, and the API-key secrets guardrail. |
| `backends/google_test.cpp` | The Gemini dialect, thought-vs-answer separation, split-boundary streaming, `functionResponse` round-trip, and the API-key secrets guardrail. |
| `agentloop/conformance_test.cpp` | **The cross-provider table.** The same two scripted turns in four dialects driving the same loop: identical answer, iterations, history shape, tool linkage, and usage. Also pins that a mid-session `/model` switch carries a full history — tool call and result included — across every translator. |
| `commands/helpers_test.cpp` | Flag/config resolution order, base64 padding, image parts, message assembly. |
| `agentloop/loop_test.cpp` | **The conformance suite** — multi-tool turns, tool errors, unknown tools, the iteration bound, `ask_user` advertisement and rollback, the permission gate, transient exclusion, and the Reporter sequence. Every later provider must pass it. |
| `agentloop/anthropic_loop_test.cpp` | The real Anthropic provider driven through the real loop on recorded SSE — the join the unit suites do not cover. |
| `agentloop/content_test.cpp` | Token estimation, splicing, compaction (including that a failed compaction returns history unchanged). |
| `agentloop/question_test.cpp` | `ask_user` schema, validation messages written for the model, answer encoding. |
| `agent/tool_test.cpp` | Registry, permission resolution, dispatch, and `fetch_url` including its HTML stripper. |
| `ansi/ansi_test.cpp` | The colour mode matrix, table-tested across all eight combinations. |
| `commands/thinking_view_test.cpp` | Byte-level: wrapping, the rolling window, the erase arithmetic, and that nothing but the summary survives `finish()`. |
| `commands/status_line_test.cpp` | Overwrite, clear, the generation counter, verbosity modes, and the spinner frame format. |
| `commands/cli_reporter_test.cpp` | The stdout/stderr split and that thinking never reaches the answer stream. |
| `commands/chat_test.cpp` | Slash parsing, the 80/90 thresholds and what they are measured against, title sanitising, and the log line format. |
| `logger/session_test.cpp` | Round trips, every resume-warning path, tool-call survival, and that no thinking is persisted. |
| `commands/line_reader_test.cpp` | The non-TTY reader: CRLF, a final line with no newline, blanks vs EOF, and that a piped run never constructs the editor. |
| `commands/check_test.cpp` | The doctor against a matrix of deliberately broken installs — and the **severity** each gets, which is the whole product: a keyless, modelless install must PASS. Plus that `--fix` never rewrites config, and that the key itself never reaches the output. |
| `commands/lifecycle_test.cpp` | The completion protocol (live backend names, prefix-not-substring matching, surviving a broken config) and uninstall's plan (which user data it names, `--keep-data`, an already-removed install). |
| `support/fake_transport.h/.cpp` | The scripted `HttpTransport` — the seam that makes cloud-backend tests hermetic. |
| `support/fake_command.h/.cpp` | A `Command` defined in test code — the injectable seam, exercised. |
| `support/env_guard.h/.cpp` | RAII environment-variable and temp-directory guards. Config resolution reads the environment, so exercising it means mutating the environment — and a leaked change would steer every test after it. |

Beyond those, `tests/CMakeLists.txt` registers `cli.*` ctest cases that run the built `apogee` binary as a subprocess, covering the contract as a user meets it (bare invocation prints help and exits 0; `--version`; unknown subcommand fails). Running a target is not linking it, so the link policy still holds.

`cli.install_parity` (`tests/install_parity.sh`) installs twice into throwaway roots and requires identical trees *and modes*, refuses to pass on fewer than five directories, and requires a freshly seeded install to pass `apogee check`. It is the enforcement behind CLAUDE.md → *One layout declaration*.

`cli.test_names` (`tests/test_names.cmake`) refuses a `TEST_CASE` name beginning with a dash. ctest hands each test's name to Catch2 as its filter argument, so such a name is parsed as an option: the test passes alone and fails under ctest with "Unrecognised token", which reads like a broken test rather than a broken name. It cost time twice before this existed.

`harness.layering` is a `cmake -P` check that no file under `source/harness/` includes `backends/`. C++ cannot enforce this the way Go's import cycles do — the include would compile fine and the layering would be silently gone — so it is checked mechanically, and it refuses to run against an empty source list so it cannot pass vacuously.

`cli.complete_lifecycle` ([`tests/complete_e2e.cmake`](../../src/cli/tests/complete_e2e.cmake)) runs the walking skeleton end to end against the mock backend — prompts, stdin, flags, images, `--all-backends`, and every exit code — fully offline. **Every invocation gets an explicit stdin**: without one the child inherits ctest's, which may be a pipe that never delivers EOF, and a command falling through to reading stdin hangs the whole suite instead of failing.

`cli.startup_speaks_on_one_line` ([`tests/pty_startup_check.py`](../../src/cli/tests/pty_startup_check.py)) drives the real binary under a **pseudo-terminal**. A pipe-based test proves nothing about the interactive path — `apogee` asks whether stdout is a terminal before rendering anything, so piping exercises the branch that deliberately emits nothing, and would pass on a build that rendered garbage interactively. The script replays the escape codes to reconstruct the final screen, then asserts the startup notice appears exactly once on one row and that no spinner frame survived. POSIX only.

`cli.chat_line_editing` ([`tests/pty_lineedit_check.py`](../../src/cli/tests/pty_lineedit_check.py)) drives the editor under a PTY: an arrow key must RECALL previous input rather than arrive as a literal escape sequence in the message, and the history file must persist. None of it reproduces on a pipe, which is the point.

`cli.chat_typeahead_and_crash_safety` ([`tests/pty_chat_check.py`](../../src/cli/tests/pty_chat_check.py)) covers the two chat behaviours no unit test can reach: text typed **before** the first prompt is discarded (needs a real terminal — `tcflush` applies to a terminal input queue, and on a pipe the code deliberately does nothing), and a `kill -9` mid-conversation leaves every completed turn on disk (needs a real SIGKILL). Both were verified against the mutation each exists to catch.

The interactive-never-listens invariant has two locks, `cli.no_listen_symbols` and `cli.complete_opens_no_listening_socket`. Read [CLAUDE.md](CLAUDE.md) → *⚠ Interactive turns never open a listening socket* before touching either — the reason there are two is not obvious, and removing one leaves a real gap.

The largest of those is `cli.config_lifecycle`, driven by [`tests/config_e2e.cmake`](../../src/cli/tests/config_e2e.cmake): it runs the real binary through init → add-backend → roles → get → delete and asserts the file came back byte-identical, all under a throwaway `APOGEE_HOME`. It is a `cmake -P` script rather than a shell script so it runs on all six targets — a `.sh` would silently skip on the Windows runners, which is exactly where a path or line-ending bug would surface.

**Tests must stay hermetic:** no network, no models, no writes outside the test's own temp directory. A test that needs a live provider is not a unit test. Fixtures go through the injectable seams — that discipline starts at the first interface because retrofitting it in C++ is far more painful than in Go.

Two traps worth knowing:

- **Test names must not begin with `-`.** ctest invokes each case by passing its name as an argument, and Catch2's own parser would read it as a flag.
- **`#` is not a comment inside `.clang-tidy`'s `Checks:` block.** It is a YAML folded scalar; a `#` line silently becomes a bogus check name and breaks every entry after it. Rationale goes above the block.

---

## Build system

CMake ≥ 3.25, C++20, one static library plus one executable. Build root: `lib/src/cli/CMakeLists.txt`.

### Presets

Seven configure presets: `default` (host native — the developer bootstrap) plus one named after each release target. Each builds into `lib/src/cli/build/<preset>/`, which is the path `cicd.sh` and the Makefile expect.

```
linux-x64   linux-arm64   macos-x64   macos-arm64   windows-x64   windows-arm64
```

macOS presets select the architecture via `CMAKE_OSX_ARCHITECTURES`, so a Mac host builds both Mac targets. Windows presets use the Visual Studio 17 2022 generator; CMake hides them on hosts without it, and `cicd.sh` refuses non-native targets anyway, deferring them to the CI matrix.

### Commands

Run from `lib/src/cli`, or with `make -C lib/src/cli <target>` from anywhere.

| Command | Does |
|---|---|
| `lib/scripts/cicd.sh --test` | **The repo-wide entry point.** Builds every app for the host target and runs its suite — what CI runs. `--platform`, `--fresh`, `--clean`, `--jobs` too. |
| `make test` | The same thing for the CLI alone (it calls `cicd.sh`). |
| `make build [PRESET=…]` | Configure and build one preset. |
| `make install [PREFIX=…]` | Build, then install the binary to `$PREFIX/bin` (default `~/.local`, so no sudo). |
| `make format` / `make format-check` | Apply / verify formatting across `source/` and `tests/`. |
| `make lint` | clang-tidy over `source/` and `tests/`, including the ownership gate. |
| `make llama` | Build with the pinned llama.cpp target enabled (slow). |
| `make clean` / `make fresh` | Drop a build dir / clean-room clone-and-build. |

`make install` is a thin caller into the `install()` rule in `source/CMakeLists.txt`. It installs the executable (`apogee_core` is an implementation detail and is deliberately not installed), then the shell completions, and then runs **`apogee check --fix`** so the binary creates and verifies its own data directory. That last step is the parity rule in one line: the Makefile does not know what `~/.apogee/` contains, and neither installer does — all three reach the single declaration in `source/harness/layout.h`. See CLAUDE.md → *One layout declaration, and every install path reads it*.

`make lint` configures its own `build/lint` directory using the compiler from clang-tidy's own directory. That is not incidental: on macOS the normal build uses Apple clang, whose libc++ headers Homebrew's clang-tidy cannot find, and every file fails to parse with a misleading `'cstddef' file not found`.

### Options

| Option | Default | Meaning |
|---|---|---|
| `APOGEE_BUILD_TESTS` | `ON` | Build the Catch2 suite. |
| `APOGEE_ENABLE_LLAMA` | `OFF` | Build the pinned llama.cpp and the real `LlamaRuntime` behind it. Heavy (GPU kernels), so it stays off the merge-blocking path and a dedicated non-blocking CI job builds and tests it. With it OFF, `backends/llama_real.cpp` compiles to the "not built in" answer and the llamacpp backend refuses construction with a message saying how to enable it — everything else about the provider is still built and tested. |

---

## Dependencies

**Strategy: FetchContent**, every declaration carrying `FIND_PACKAGE_ARGS` so a system-installed copy wins and the network fetch is the fallback, and `SYSTEM` so third-party headers are not held to Apogee's warning bar. All declarations live in one file: [`lib/src/cli/cmake/ApogeeDependencies.cmake`](../../src/cli/cmake/ApogeeDependencies.cmake).

| Concern | Pick | Status |
|---|---|---|
| JSON | nlohmann/json `v3.11.3` | Wired |
| YAML | yaml-cpp `0.8.0` | Wired — **read path only.** The config engine never serializes through it (see `harness/`). Its CMakeLists predates a CMake policy removal, so `CMAKE_POLICY_VERSION_MINIMUM` is raised around its `FetchContent_MakeAvailable` and restored immediately; revisit when it cuts a release past 0.8.0. |
| CLI parsing + completions | CLI11 `v2.4.2` | Wired |
| Chat line editing | replxx `release-0.0.4` | Wired. Used only by the chat REPL's interactive path; a piped run never constructs it. GNU readline was ruled out on licence grounds (GPL). |
| Tests | Catch2 `v3.7.1` | Wired (only when `APOGEE_BUILD_TESTS`) |
| HTTP client | libcurl (system) | Wired 2026-08-26. **Found, never fetched** — building it from source would mean choosing a TLS stack too, and the point of the platform-trust-store decision is to use the one the OS already manages. Note the trap recorded in `ApogeeDependencies.cmake`: `CURL_INCLUDE_DIRS` is the macOS SDK's own `/usr/include`, and propagating it as `-isystem` breaks any mixed-toolchain build (i.e. `make lint`); the redundant entry is stripped from the imported target. |
| Local inference | llama.cpp, pinned to `549b9d84` | `third_party/`, off by default |

These picks were standardized once, project-wide. **Downstream work consumes them rather than reopening them** — a second JSON library or a second CLI parser is a bug, not a preference.

### Adding a dependency

1. Declare it in `lib/src/cli/cmake/ApogeeDependencies.cmake` with a **pinned** `GIT_TAG` (never a branch), plus `SYSTEM` and `FIND_PACKAGE_ARGS`.
2. `FetchContent_MakeAvailable` it there.
3. `target_link_libraries` in the consuming target's `CMakeLists.txt`.
4. Record it in the table above.

Vendored code goes under `lib/src/cli/third_party/` instead, and is **never edited in-tree** — see [its README](../../src/cli/third_party/README.md). Fix upstream and move the pin, or wrap it in a first-party boundary class.

---

## Testing

`make -C lib/src/cli test`, or `ctest --test-dir lib/src/cli/build/<preset> --output-on-failure` against an existing build. A single case: add `-R "<name regex>"`, or run the `apogee_tests` binary directly with a Catch2 test name or `[tag]`.

---

## Adding a new extension point

- **Adding a subcommand** — see [`commands/` above](#commands--the-subcommand-scaffold).
- **Adding a provider backend** — recipe lands with the `harness-core` item, once that seam exists.

---

## Cutting a release

See [CLAUDE.md](CLAUDE.md) → **Release and Install Infrastructure**: development happens on a version-named branch (currently `v0.1.0`) and merges into `stable`; the tagging/packaging procedure is still a `_TODO:_` there. The version itself is set by the `project()` call in `lib/src/cli/CMakeLists.txt` and flows into `apogee --version` from there.
