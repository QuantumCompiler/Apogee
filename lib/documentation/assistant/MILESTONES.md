# Apogee — Milestones

The **record of finished work** — completed items move here out of [`backlog/`](../backlog/README.md) (the work queue) so the backlog holds only what's left. This is history and reference: what shipped and how it was built. The release-level board (which version each theme landed in) is in [ROADMAP.md](ROADMAP.md); the architecture reference is in [DEVELOPER.md](DEVELOPER.md).

Milestones are grouped by feature area and lettered. Each entry gives the user-facing **Goal**, a checklist of what was built, and a **Notes** line for trade-offs.

When a backlog item ships, **fold** it into the matching milestone here (extend the checklist, or add a dated `###` subsection for a substantial addition) rather than starting a new one, unless it's a genuinely new area — and delete its backlog document per the docs flow. Detail is welcome — this is where "how it was built" is preserved alongside the code and git history.

---

## Milestone A — Project foundation

**Goal.** Turn a docs-only repository into a buildable, testable C++ project, so every later item lands inside a working skeleton instead of inventing one. Nothing about the toolchain had been decided; this milestone forces those decisions where they are cheapest and wires the disciplines that only work if they exist from the first line of code.

### 2026-08-25 — C++ project skeleton (CMake, tests, CI, CLI scaffold)

**What was built**

- [x] **App-owned CMake build.** `lib/src/cli/` is a self-contained CMake project: build root, presets, `Makefile`, `cmake/` helpers, `third_party/`, and style config all live beside the code. Nothing above `lib/src/<app>/` knows how that application compiles, so the GUI apps can land later as siblings without touching a shared build.
- [x] **Library-first split, enforced.** `apogee_core` (static library — every capability) and `apogee` (a thin argv→exit-code face). `cmake/ApogeeLinkPolicy.cmake` walks every target at configure time and fails the build if anything links the executable. Tests link the library only.
- [x] **Seven CMake presets** — `default` plus one named exactly after each of the six release targets, building into `build/<preset>/`, the path `cicd.sh` selects by.
- [x] **`lib/scripts/cicd.sh` builds for real.** It gained an `APPS` list and now drives each application's own CMake project; the pre-existing docs-only notice is gone. CI invokes this same script per native runner, so there is one build path everywhere.
- [x] **Catch2 v3 suite, 30 tests via ctest** — version stamping, dependency wiring, the platform seam, command registration, and the real parsing path. Plus `cli.*` cases that run the built binary as a subprocess, covering the contract as a user meets it.
- [x] **CLI scaffold with self-registration.** `Command` interface + `CommandRegistry` + `RootCommand`, persistent `--config`, and `apogee version` as the first real subcommand. Adding a command touches its own two files and one line in `default_registry()`; `main.cpp` never grows.
- [x] **The portability seam** (`source/platform/`) — the one place platform `#ifdef`s are expected, exposing the build's OS, architecture, and release-target name. An unrecognized platform is a compile error, not a silent "unknown".
- [x] **Reserved package headers** for `harness/`, `backends/`, `agentloop/`, `embedstore/`, `httpserver/`, `mcp/` — each documenting what lands there and which item fills it, each included by `packages_test.cpp` so a broken include path surfaces immediately.
- [x] **llama.cpp pinned** to commit `549b9d84`, off by default, verified to compile as a linked library target with Metal on macOS arm64.
- [x] **Formatting and the ownership gate.** `.clang-format` + `.clang-tidy`; `make lint` fails on a raw `new`/`delete`, an owning raw pointer, or `malloc`. Verified in both directions with a deliberately violating fixture (exit code 2 with it, 0 without), then removed.
- [x] **CI** (`.github/workflows/ci.yml`) — six-target matrix, a format+lint job, a non-blocking llama.cpp compile proof, and a clean-room fresh-clone build.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| C++ standard | **C++20** | The backlog item defaulted to "C++23 if the toolchain proves stable" on the strength of `#embed` — but `#embed` is a **C23/C++26** feature, not C++23, so the rationale did not hold. C++20 is uniformly supported across all six targets and is a one-line promotion later; retreating from C++23 after Windows CI turns red would not be. |
| Dependencies | FetchContent + `FIND_PACKAGE_ARGS` + `SYSTEM` | One mechanism. System copies win when present, network fetch is the fallback, and third-party headers are not held to Apogee's warning bar. |
| Test framework | Catch2 v3 | Item default. |
| JSON / CLI / HTTP | nlohmann/json, CLI11, **libcurl** | Standardized once, project-wide. JSON and CLI11 are wired; libcurl is the standing pick but deliberately **not** wired until `anthropic-backend` needs it, so the skeleton builds on hosts without a curl development package. |
| Makefile | Kept, thin | Delegates to `cicd.sh`; it may never become load-bearing. |
| CI gating | **macos-arm64 blocking; the other five informational** | User decision. The codebase is one commit old and Windows portability has not started; targets get promoted to blocking by deleting a `continue-on-error` line. |

**Notes.** The `apogee` binary prints help on a bare invocation and exits 0 — running it with no arguments is a question, not a usage error. Two traps cost real time and are now recorded in DEVELOPER.md: a Catch2 test name may not begin with `-` (ctest passes it as an argument), and `#` is not a comment inside `.clang-tidy`'s `Checks:` folded block — it silently becomes a bogus check name and disables every entry after it. `make lint` also configures its own build directory using the compiler from clang-tidy's own directory, because Homebrew's clang-tidy cannot find Apple clang's libc++ and fails every file with a misleading `'cstddef' file not found`.

Two things this milestone deliberately did **not** do: wire libcurl (above), and enable llama.cpp on the per-push path — its Metal/CUDA build is slow, and a separate non-blocking job catches a pin that stops compiling without taxing every change.

---

## Milestone B — The config engine

**Goal.** One config file for the whole harness, and — more importantly — one *mutation path* for it. Every surface Apogee will ever grow (the CLI today, the admin plane later) edits config through the same helpers, so an edit made over HTTP is byte-identical to the same edit made from the terminal. That parity is only cheap because it was built before the surfaces that depend on it.

### 2026-08-25 — Typed loader, template, and comment-preserving mutation

**What was built**

- [x] **Typed loader** (`source/harness/config.h/.cpp`) — `backends:` (five types with api_key/model/model_path/context_size/max_tokens/temperature/system_prompt), `models:` role pointers, `paths:`, `status_mode`, and `color`, parsed into structs with a clear error naming the offending key. Type dispatch runs through one table, so widening the enum later is a single row rather than a scattered switch.
- [x] **`${ENV_VAR}` expansion** on read, with the literal text left on disk — an api_key never has to appear in the file. An undefined variable expands to empty rather than failing, because a config naming a key you have not set must still load.
- [x] **Case-insensitive backend names with collision rejection.** The `backends` map is keyed by a case-folding comparator, which turns a silent merge of two case-variant names into a detectable error: a second key differing only by case fails to insert and both names are reported. Rejected at load *and* at add.
- [x] **Comment-preserving text surgery** (`source/harness/config_edit.h/.cpp`) — `append_backend`, `delete_backend`, `set_models_role`, `format_config`, plus section scanning and fold-collision detection. Line-oriented splicing that leaves every untouched byte alone, including CRLF terminators and a missing final newline.
- [x] **Atomic, validated writes.** `edit_config_file` parses the file before the transform, re-parses its output, and only then writes — via a temp file in the same directory, renamed into place. A transform that produced invalid YAML cannot corrupt the config, and a failed edit leaves the file untouched.
- [x] **The starter config** (`assets/config.yaml`) and the template compiled into the binary, generated from that same file and held byte-identical by a test.
- [x] **`~/.apogee/` + `APOGEE_HOME`** (`source/harness/paths.h/.cpp`), over a new `platform::home_directory()` so Windows resolves it correctly. The override relocates the whole tree, which is what makes config tests hermetic.
- [x] **The `apogee config` command family** — `init`, `path`, `add-backend`, `delete-backend`, `set-default`, `set-default-embedding`, `set-default-extraction`, `get`, `format`. Nine subcommands, registered by one line in `default_registry()`, exactly as the skeleton's scaffold promised.
- [x] **51 new tests** (81 total) — the golden-file byte-diff suite, a garbage-input battery, and a `cmake -P` end-to-end test driving the real binary through init → add → roles → delete → byte-identical round trip under a throwaway `APOGEE_HOME`.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| Config format | **YAML** | The item's open call framed this as YAML-needs-text-surgery vs TOML-round-trips-natively. That premise is false: comment preservation in toml++ is an open, unimplemented request ([#28](https://github.com/marzer/tomlplusplus/issues/28)) and it reorders keys alphabetically on serialize ([#62](https://github.com/marzer/tomlplusplus/issues/62)). Text surgery is mandatory either way, which removes TOML's only claimed advantage — so YAML. |
| Parse library | yaml-cpp 0.8.0, **read path only** | Never used to write. Marshaling a struct back to YAML strips comments and reorders keys, which is the whole failure this module exists to prevent. |
| Data directory | `~/.apogee/` + `APOGEE_HOME` | User decision. One identical layout on all six targets; the override is what lets tests write nowhere near a real home directory. |
| Transform shape | **Pure text in, text out** | Not a helper that reads, edits, and writes in one function. Separating I/O is what makes the golden suite filesystem-free and what makes re-parse validation and atomic writes possible at all. |
| `config get` on an api_key | Redacted unless `--reveal` | `get` output lands in terminals, screenshots, and shell history. Keeping secrets out of logs and off HTTP, then printing one on a bare `get`, would be the same mistake by a shorter path. |
| `format` scope | Whitespace only | Fields inside an entry are never sorted: moving a field line moves it out from under the comment explaining it. |

**Notes.** Two bugs worth recording, both caught by the tests rather than by review. **Insertion point:** appending at the end of a section put the new backend *below* the file's trailing comments — a comment does not terminate a YAML block, so `backends:` ran to end-of-file in the shipped template. Entries now go after the section's last *content* line. **Comment ownership on delete:** scanning forward to the next sibling key swallows the blank line and comment header that document the *following* entry. Blank and comment lines are now tentative — they extend an entry only when a deeper-indented field follows. The fixture test `deleting an entry does not swallow the next entry's comment header` pins the fix.

The one documented exception to byte-exactness: appending to a file with no final newline gives its last line one, since nothing can follow an unterminated line. Delete cannot know to take it back. Both directions are test-pinned.

yaml-cpp 0.8.0 opens with `cmake_minimum_required(VERSION 3.4)`, which CMake ≥ 4.0 refuses. `CMAKE_POLICY_VERSION_MINIMUM` is raised around that one `FetchContent_MakeAvailable` and restored immediately, so no other target inherits it. Revisit when yaml-cpp cuts a release past 0.8.0 — the fix is already on master.

`--config` no longer carries CLI11's `ExistingFile` check: `apogee config init --config <new path>` must be able to name a file it is about to create. A command needing an existing config reports the miss itself, with a message that names the fix.

---

## Milestone C — The harness core

**Goal.** The interface layer everything else is written against: one provider interface, one canonical message IR, one router. After this, "add a backend" means implementing an interface, and no surface, loop, or session file learns that a fifth vendor exists. Streaming, cancellation, multimodal content, and tool structures have to be in the interface on day one — adding any of them later means touching every implementation and every caller at once.

### 2026-08-25 — LLMProvider, message IR, router, and MockProvider

**What was built**

- [x] **The canonical IR** (`source/harness/types.h/.cpp`) — `ChatMessage` with dual-mode `MessageContent` (plain string *or* typed parts), `Tool`/`ToolCall`/`ToolResult`, `ChatRequest`/`ChatResponse`, `Usage`, `ModelInfo`, plus `RAGMeta` and `StatusEvent` as the shared observability vocabulary. Hand-written nlohmann conversions: plain text serializes as a bare JSON string, and both shapes are accepted on input, so a session file written before images existed still loads.
- [x] **The transient region, un-serializable by construction.** `ChatRequest::Transient` (RAG span, `side_request`, `response_schema`) is a nested struct with **no `to_json` anywhere** — serializing it does not compile. `durable_messages()` is the history view.
- [x] **`LLMProvider`** (`source/harness/provider.h/.cpp`) — four methods (`chat`, `stream_chat`, `complete`, `list_models`), streaming through a `TokenSink` callback with a `CancellationToken`. `complete` defaults to `chat` on the base class.
- [x] **Capability interfaces, discovered not required** — `EmbeddingCapable`, `StatusReporting`, `InTextToolCalling`, `ModelBehaviorReporting`. The **Harness** performs the discovery; callers ask plain typed questions (`can_embed(model)`, `model_behavior_for(model)`) and never write a `dynamic_cast`.
- [x] **`CancellationToken`** (`source/harness/cancellation.h/.cpp`) — a shared atomic flag, cancellable from any thread, cooperative by design. Copies share one flag; the default-constructed token can never cancel and costs no allocation.
- [x] **`ModelBehavior`** (`source/harness/behavior.h/.cpp`) — plain data, the layering seam that lets the agent loop ask about a model family without the harness including backends. The zero value means *unknown*, and unknown means **permissive**.
- [x] **`Harness` + `SimpleRouter`** (`source/harness/harness.h/.cpp`) — registry plus the three-rung precedence, each rung trying the literal name then the normalized form (lowercased, `.`/`:` → `-`). Typed errors throughout; a routing failure names the configured backends and the fix.
- [x] **The context-window table** (`source/harness/context_windows.h/.cpp`) — single owner, prefix-matched longest-first so a dated pin resolves through its family. An explicit `context_size` always wins. **0 means unknown, never unlimited.**
- [x] **`MockProvider` + `MockEmbeddingProvider`** (`source/backends/mock.h/.cpp`) — scripted turns, configurable chunk size, recorded requests, and a real `type: mock` backend. This is what makes every downstream item testable with no network and no model.
- [x] **55 new tests** (136 total) — the router precedence table across all three rungs, cancellation mid-stream, IR round trips in both content shapes, the transient-exclusion contract, capability probes, and MockProvider contract tests. Plus `harness.layering`, a `cmake -P` check that the harness never includes backends.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| Streaming shape | **`std::function` token sinks** | The item's stated default. Simple in every backend, and it does not force the whole call stack to become coroutines. |
| Capability discovery | Interfaces + `dynamic_cast` **inside the Harness** | "Discovered rather than required" as the item asks, but the cast lives in one file. Callers get `can_embed(model)`, so a capability check cannot decay into a type-switch over backends. |
| Router rungs | The three named rungs only | No fourth — "if exactly one backend is registered, use it" — and deliberately: it papers over an unset `models.default` in a way that stops working the moment a second backend is added, which is exactly when the user has least idea why routing changed. A clear "set models.default" is the better failure. |
| Index collisions | **First registration wins** | Two entries may declare the same `model:`. Letting the later one win makes routing depend on map iteration order — a bug that reproduces on one machine and not another. |
| `Usage` zero | "Not reported", not "zero tokens" | Different facts. A surface that renders "0 tokens" for a silent provider is lying, so `reported()` distinguishes them and `to_json` omits the block entirely. |
| Provider `complete` | Defaults to `chat` | Most providers have no cheaper single-turn endpoint; duplicating the call in each would only let the two paths drift. |

**Notes.** The transient-region contract is the one worth understanding. If a per-turn RAG blob reaches persisted history it is re-sent on every later turn, growing the prompt without bound and feeding the model context it was told was for one question only — silent, and expensive. Excluding three fields in every serializer anyone ever writes is the kind of rule that holds until it doesn't, so the fields live in a nested struct with no JSON conversion at all: the mistake does not compile.

The layering check needed a real guardrail rather than a convention. In Go the reverse edge is an import cycle and the build fails; in C++ a `#include "backends/…"` in the harness compiles fine and the layering is quietly gone. `harness.layering` greps for that edge, and it refuses to run against an empty source list so it cannot pass vacuously. It was verified in all three directions — clean tree passes, a violating tree fails with the offending file named, an empty tree errors.

`MockProvider` holds itself to the same cancellation contract real providers must honour — checking between chunks, not just at entry — because otherwise tests of cancellation prove nothing about the interface they are supposed to pin.

---

## Milestone D — The first real backend

**Goal.** Prove the hardest new C++ ground — streaming HTTPS, SSE framing, provider dialect mapping — on exactly one provider before tripling the surface. Everything built here is infrastructure the OpenAI and Google backends reuse, and the thinking stream, native tool use and web search come from native API features rather than being rented from the `claude` CLI.

### 2026-08-26 — Anthropic Messages API backend

**What was built**

- [x] **The shared HTTP client** (`source/backends/http_client.h/.cpp`) — split into `HttpTransport` (one request; `CurlTransport` is the real one) and `HttpClient` (retry/backoff over a transport). Exponential backoff on 429/5xx, `retry-after` honoured and capped, and an injected sleeper so retry tests run instantly. Every curl handle is a `unique_ptr` with a custom deleter; no raw handle escapes.
- [x] **The SSE parser** (`source/backends/sse_parser.h/.cpp`) — a byte-fed state machine, shared by every streaming cloud backend. Handles repeated `data:` fields, comment/keep-alive lines, CRLF, unknown fields, and an unterminated trailing event.
- [x] **IR ↔ Anthropic translation** (`source/backends/anthropic_wire.h/.cpp`) — system-prompt lifting, image source blocks (base64 and URL), `input_schema` naming, tool results as user-turn `tool_result` blocks, server-side `web_search`, and stop-reason mapping.
- [x] **The provider** (`source/backends/anthropic.h/.cpp`) — streaming and non-streaming chat, `list_models`, and `count_tokens` for exact context accounting. A `StreamAccumulator` tracks indexed content blocks so interleaved text, thinking, and tool-argument deltas each land in the right place.
- [x] **The typed thinking seam** — `harness::ThinkingSink` added to `StreamOptions`. Thinking reaches its own sink and never the token stream, the returned message, or persisted history.
- [x] **Extended-thinking replay** — an Anthropic requirement. A turn that thought and then called a tool must have its thinking block, signature included, sent back on the next request. Since thinking must not enter the IR, the provider caches the raw blocks (`ThinkingCache`) and splices them back itself.
- [x] **libcurl wired** — `find_package(CURL REQUIRED)`, found not fetched, using the platform's own trust store.
- [x] **64 new tests** (200 total) — the SSE suite replays a recorded stream at *every* chunk size from 1 byte up; the provider suite replays fixtures at seven chunk sizes; retry, cancellation, error shapes, and the wire mapping are all covered, plus a secrets guardrail asserting no error path can emit the API key.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| TLS / certificates | **Platform trust store** | The item's stated default. curl is built against Secure Transport / OpenSSL / Schannel per platform, so one code path sits over three native stores. Bundling a CA list means shipping a revocation problem and re-releasing to fix it. |
| Context-window rows | **Compiled in, config overrides** | The item's stated default. The Anthropic rows already landed with the harness-core table; a backend entry's `context_size` still wins. |
| Transport seam | `HttpTransport` interface, injected | Every cloud-backend test runs against a scripted transport: no network, no key, no charges — and failure modes a live endpoint will not produce on demand (a 529, a mid-frame disconnect, a one-byte-at-a-time body). |
| Error bodies | **Never streamed to the sink** | See Notes — this one was a real bug. |
| `list_models` | The configured model only | A backend entry pins one model. Listing the vendor's catalogue would report models this entry cannot actually serve. |
| Request timeout | None; connect timeout bounded | A long generation is not a hung connection, and a timeout that cannot tell them apart truncates real answers. |

**Notes.** Two bugs worth recording, both caught by tests rather than review.

**Retry vs. streaming.** `HttpClient` refuses to retry once bytes have reached the caller — replaying would deliver the first half of an answer twice with no way for the caller to tell. But a 429's *error body* was also going through the sink, so a perfectly retryable rate-limit looked "already answered" and retry silently stopped working on exactly the requests that need it most. The fix is in the transport contract: **an error response is accumulated into `HttpResponse::body`, never streamed.** `curl` runs the header callback before the first write, so the status is known in time to decide. `FakeTransport` honours the same contract, or the fake would hide the bug. The guard's real case — a connection dropping mid-*successful*-stream — now has its own test, which needed a `fail_after_bytes` capability in the fake because a live endpoint will not do it on request.

**`find_package(CURL)` breaks the lint job.** On macOS, `CURL_INCLUDE_DIRS` is the SDK's own `/usr/include`, already searched implicitly. Propagating it as an explicit `-isystem` puts Apple's C headers ahead of the compiler's own, so a toolchain whose `stddef.h` lives elsewhere — Homebrew LLVM, which is what `make lint` runs — never defines `size_t`, and every system header using it fails to parse. The symptom is hundreds of `unknown type name 'size_t'` errors inside Apple's `_stdio.h`, pointing nowhere near the cause. `ApogeeDependencies.cmake` now strips any `/usr/include` entry from curl's imported target. Compiling was never affected, because the build uses Apple clang whose headers are in that same SDK — only the mixed-toolchain case breaks, which is precisely the lint job, and it would have failed in CI.

The SSE parser's every-chunk-size test is the one to keep. An SSE event does not arrive in one read: chunk boundaries land mid-frame, mid-line, and mid-UTF-8-sequence whenever the network feels like it. A parser that assumes one-read-one-event works perfectly on a fast local connection and drops tokens on a slow one — the worst possible failure distribution, because it passes every test written on a developer's machine.

---

## Milestone E — The walking skeleton closes

**Goal.** The first end-to-end user-visible path: config → harness → backend → terminal. This is the moment the architecture is proven or falsified, and it is deliberately the shortest possible slice — a wrong design bet is cheapest to fix here, before the agent loop and the chat REPL are built on top.

### 2026-08-26 — `apogee complete`

**What was built**

- [x] **`apogee complete <prompt>`** (`source/commands/complete.h/.cpp`) — reads config, routes through the Harness, streams to stdout. Flags: `-m/--model`, `-s/--system`, `-t/--temperature`, `-n/--max-tokens`, `--context`, `--image` (repeatable), `-q/--quiet`, `-v/--verbose`, `--all-backends`. Reads the prompt from stdin when no argument is given.
- [x] **The provider factory** (`source/backends/factory.h/.cpp`) — config `type:` → provider, with per-entry status. A backend that cannot be built is **skipped with a recorded reason**, not fatal: one unconfigured cloud entry must not take down a working local one.
- [x] **Command helpers** (`source/commands/helpers.h/.cpp`) — flag/config/default resolution, stdin reading, base64, image-part loading, and the fixed message order (system → context → prompt).
- [x] **TTY detection in the platform seam** — `platform::is_terminal(StandardStream)`. Decoration is gated per-stream, because `apogee complete x > out.txt` has a redirected stdout and a terminal stderr and only the first should go plain.
- [x] **Typed exit codes** — 0 success, 1 user error, 2 backend error, 130 cancelled. A pipeline needs to tell "your prompt was wrong" from "the API was down".
- [x] **The interactive-never-listens invariant, test-locked** — two complementary checks (see Notes).
- [x] **18 new tests** (218 total) — helper and factory units, a `cmake -P` end-to-end suite driving the real binary offline against the mock backend, plus both invariant locks.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| Factory location | `backends/`, not `commands/` | The item's Seam put it in `commands/helpers.cpp`. Moved: `serve` and the admin plane will need the same construction, and a factory duplicated per surface is exactly how two surfaces come to disagree about what a config entry means. |
| Explicit `-m` | Validated before routing | See Notes — this was a real usability bug. |
| Unbuildable backend | Skipped with a reason, not fatal | A missing API key on one entry must not stop the others. The reason is surfaced when it turns out to be the backend the user asked for. |
| `--all-backends` headers | Printed even on a pipe | Every other decoration is suppressed when piped. With several answers concatenated the labels are structure, not ornament — without them the output cannot be attributed. |
| Thinking in `complete` | Dropped | It is display metadata and `complete` has no display layer yet; the rich terminal UX arrives with chat-cli and retrofits here. Critically it must never reach stdout — a pipe receives exactly the answer. |
| Image capability | Config-type check, marked stopgap | The only image-incapable type is `llamacpp`, which has no implementation yet, so there is no object to ask. When it lands it should carry a `VisionCapable` capability and this should become a Harness probe — a type switch is the shape the capability rule exists to avoid, and the comment in `complete.cpp` says so. |

**Notes.** Three bugs, all caught by testing rather than review.

**`--image` swallowed the prompt.** A CLI11 vector option is **greedy** by default, so `--image pic.png "my prompt"` put *both* into the image list and left the positional prompt empty — after which the command fell through to reading a stdin that never arrived and hung. It surfaced as a 600-second test timeout, not as a parse error. `->allow_extra_args(false)` gives one value per occurrence, still repeatable.

**A typo'd `-m` silently answered from the wrong backend.** The router's third rung falls back to `models.default`, which is right for an unspecified model and wrong for an explicit one: `apogee complete -m sonnnet` would return a real answer from a different model with nothing to indicate it. The command now validates an explicit `-m` against the config — mirroring the router's first two rungs and deliberately not its third — before the router ever sees it.

**The invariant lock passed on a deliberately violating build.** The first version sampled `lsof` once while the process blocked on stdin — which happens *before* the turn, so a socket opened while talking to the backend went unseen. Continuous sampling did not fix it either: the mutation's socket lived about a millisecond, far below what one `lsof` call can resolve. The answer is two checks, and **neither is sufficient alone**:

- `cli.no_listen_symbols` — `nm -u` on `apogee_core` must show no `listen`/`accept`. Deterministic, no timing. Catches our own code however brief the window. Cannot see a child process.
- `cli.complete_opens_no_listening_socket` — polls `lsof` across the turn. Catches a *spawned* server holding a port (a local inference server, a proxy), which the symbol check cannot. POSIX only; Windows is a recorded skip.

Both were verified against a build that deliberately calls `listen()`. The symbol check failed it; the runtime check did not, which is precisely why both exist. When `serve` lands it will need an explicit exclusion from the symbol check — and that should be a deliberate, reviewed edit.

---

## Milestone F — The shared agent loop

**Goal.** Extract the model→tool→model loop behind an observer **before** the surfaces multiply, not after. That ordering is load-bearing: one loop is what keeps several front-ends consistent, and what makes deleting an entire front-end a local change rather than a rewrite. `apogee complete --tools` is the first consumer; chat and serve become thin adapters over the same `run()`. From 2026-09-28 (26c), every request `run()` sends is assembled against the model's window. From 2026-10-03 (26g), past 16 registered tools a turn offers the tools its question needs, not every one.

### 2026-08-26 — `agentloop::run`, the tool registry, and `ask_user`

**What was built**

- [x] **The loop** (`source/agentloop/loop.h/.cpp`) — `run()` drives model→tool→model until the model stops asking for tools, extending history in place. Streams the answer through the Reporter, intercepts `ask_user` before dispatch, rolls the half-turn back on an aborted tool phase, and bounds itself with `max_iterations`.
- [x] **The Reporter** (`source/agentloop/reporter.h`) — `on_thinking`, `on_thinking_token`, `on_tool_status`, `on_clear_status`, `on_answer_start/token/end`. The loop knows nothing about terminals or HTTP; `NullReporter` is the default.
- [x] **The tool registry** (`source/agent/tool.h/.cpp`) — registration, IR tool definitions, and `dispatch()` with the ask/allow/deny permission gate. Native filesystem toolsets and the MCP client register here later without the loop changing.
- [x] **`ask_user`** (`source/agentloop/question.h/.cpp`) — schema, validation, answer encoding. Works uniformly on every provider, because Apogee owns the loop everywhere rather than delegating it to a vendor CLI on one backend.
- [x] **Content helpers** (`source/agentloop/content.h/.cpp`) — `TokenCount` (always flagged estimated), `splice_transient`, and `compact_history` summarising into an authoritative **system** message.
- [x] **`fetch_url`** (`source/agent/fetch_url.h/.cpp`) — behind an injected `UrlFetcher`, so the tool is testable with no network. Includes a crude HTML-to-text stripper.
- [x] **`apogee complete --tools` / `--search`** plus a terminal `ask_user` prompt (`source/commands/ask_prompt.h/.cpp`) that writes to stderr and reads stdin, so stdout still carries exactly the answer.
- [x] **68 new tests** (268 total) — the scripted-provider conformance suite the item calls for, plus an Anthropic-through-the-loop integration test.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| Web search | **Provider server-side tools only** | User decision. A local search would mean scraping DuckDuckGo's HTML results page with regexes; the markup changes and the tool returns *nothing* rather than erroring. `fetch_url` — the durable half — ships; searching is the vendors' job. Local models get `fetch_url` but no search in v0.1.0, and the registry seam stays open for a pluggable one. **Revised 2026-09-28** (25e): `web_search` over a SearXNG the user runs, a JSON API rather than a page — see [Milestone V](#milestone-v--the-native-toolsets). |
| GBNF grammar sampling | Deferred | The stated default. Cloud tool calls arrive structured, so nothing here depends on it; `model-profiles` decides. |
| `agent/` as its own package | Split from `agentloop/` | The loop needs a registry; a registry needs no loop. MCP and native toolsets land in a third package and register into the same place. |
| Iteration bound | 12, then answer with tools withdrawn | A model can call the same tool forever, and the only symptom is a request that never returns while spending money. Withdrawing the tools forces a text answer, so the user gets something usable rather than an error. |
| `--search` | A per-run flag, not a config key | It applies to one invocation. A `web_search:` key is something you have to remember to turn off again. Threaded through `backends::BuildOptions`. |
| Tool errors | Results, never exceptions | An unknown tool, a bad argument, a tool that threw — all come back as error results the model reads and reacts to. Aborting the turn would throw away the conversation over a hallucinated name. |

**Notes.** The availability rule for `ask_user` is the part worth understanding: **a null `AskFn` means the tool is never advertised — absent from the request, not advertised-and-refused.** A model told it may ask questions on a surface with nobody attached will eventually ask one, and then either hang or invent the answer. `advertised_tools()` is exposed specifically so that rule is testable as a contract rather than an implementation detail.

Rollback is the other subtle piece. An assistant message whose tool calls were never answered is rejected outright by several providers, so an aborted `ask_user` resizes history back to the mark taken before the tool phase. The test asserts history is byte-for-byte what it was, not merely "shorter".

One bug, caught by the tests: `strip_html` ran words together across a skipped `<script>` or `<style>` element — `a<script>…</script>b` became `ab`. Normal tags already inserted a word boundary; the skip path did not. Two unrelated words merging into one is exactly the kind of thing a model then treats as a term.

The layering check was **extended to cover `agentloop/` and `agent/`**, which CLAUDE.md said it would when the loop landed. A loop that includes a backend starts special-casing one vendor's tool dialect, and "one shared loop for all surfaces" quietly becomes "one loop with an Anthropic branch". `commands/` is deliberately not checked — it is the composition root and assembling providers is its job. Verified against a synthetic tree with a violating `agentloop` source.

**Not done: the live-API half** of the Anthropic acceptance criterion. The fixture-automated half is covered by `agentloop/anthropic_loop_test.cpp`, which drives the real provider through the real loop on recorded SSE. Running it against the live API needs a key and a human — worth doing once before the release closes.

---

### 2026-09-28 — `context-budget` (backlog item 26c): what is sent, sized to the window

**Why.** Every source that added to a request picked its own size. Retrieval injected `--rag-limit` chunks. The tools capped their own output (64 KiB, 16 KiB, 8 KiB). A tool result stayed in history, whole, for the rest of the chat. The history was measured only to warn at 80% and compact at 90%. On a cloud model that is waste. On a local one it is time -- every token is read at about a hundred a second on a 27B -- and crowding, because a small model's attention degrades as unrelated text piles up. The attachments and recall this track adds would make fixed caps untenable.

**What was built**

- [x] **`agentloop/budget.h/.cpp`**, used by every surface through the one loop.
  - `ContextBudget` is the model's window and the answer's reserve: the request's `max_tokens`, else the backend's, else 4,096, and never more than half the window.
  - It divides what is left into shares: attachments 30%, retrieval and recall 20%, tool results 25%, history the rest.
  - `TurnBudget` adds the counting. It is exact where the provider can count (a loaded local model renders and tokenizes the whole request, tool definitions included). Elsewhere it estimates messages, tool calls and tool definitions at four characters a token, and says it did.
- [x] **A finished turn's tool results are sent as stubs** (`stub_tool_results`). Each is one line: `[read_file({"path":"big.txt"}) returned 57 KB; not kept after its turn -- call it again if you need it.]`
  - It keeps the link to its call, so the request stays well formed.
  - Only what is **sent** changes: the saved transcript keeps every result whole.
  - A result of 512 bytes or less is kept whole, since a stub would be no smaller. So is every `ask_user` answer, since those are the user's own words.
  - It needs no window, so it applies to every backend. Said once a turn under `--verbose`: `earlier turns' tool results sent as 1 stub (57 KB not re-sent)`.
- [x] **Retrieval asks for its share.** `RagTurn::budget`, passed by chat, `complete`, `analyze` and `serve` through `retrieve_for_collection`, injects the leading chunks that fit `share(Retrieval)`. `fitting_prefix` finds how many by bisection, counting exactly where it can. The retrieval line says what was left out: `5 of 12 chunks fit the context budget`. A graph section that cannot fit even alone is dropped, and said.
- [x] **An overflow is trimmed in reverse priority** (`assemble_request`), only when the whole request would not fit the window after the reserve. Each source goes first down to its share, then below it:
  1. earlier exchanges, oldest first, to none, each with its calls and their results;
  2. this turn's tool results, oldest first, to their share, then to the newest alone;
  3. the injected context, from its end.

  The system prompt, the tools' environment note, the question and the newest result are never trimmed. Every trim is said as a notice, once while it holds: `context budget: 2 earlier exchanges not sent; 1 of this turn's tool results sent as a stub`. That is `[warn]` on the terminal, and a `notice` event in machine mode and on `serve`. A request that still cannot fit is sent, and said to be over.
- [x] **An unknown window never reads as room.** Nothing is sized or trimmed by it, and the fixed caps stand. `share()` is 0 there, so a caller must ask `known()` rather than read 0 as "nothing fits".
- [x] **A request whose bytes fit is never counted.** A token covers at least a byte, so this shortcut cannot be mistaken, where an estimate could be, several times over, on text that tokenizes densely. Only a request near its window is rendered and tokenized.
- [x] **Measured and compacted as sent.** `measure_context` counts the conversation with its stubs, so a chat that read a big file is not compacted for what it no longer sends. Compaction shows the summariser the stubs, so tool output is condensed before conversation.

**On real weights** (greedy, `--verbose` lines, against a build of `ea75dea` -- the installed binary, which predates this item and sends the same requests otherwise):

- **A five-turn chat on Qwen3.8-27B** (Q4_K_M, a 32K window) that reads a 58 KB file on its first turn. The answers were word for word the same on both builds.

  | | before | with the budget |
  |---|---|---|
  | turn 1, after the read | 18,833 | 18,833 |
  | turn 2 | 18,907 | 4,137 |
  | turn 3 | 18,935 | 4,165 |
  | turn 4 | 18,961 | 4,191 |
  | turn 5 | 19,005 | **4,235** |

  Prompt tokens per request. Turn five is 78% smaller, and the window 13% used instead of 58%. Turn two read just 150 new tokens: the stub changes the prompt inside turn one, and 25c's checkpoint at turn one's question covers exactly that.
- **Time was not a clean measure.** Other work on the machine moved an identical first turn between 186 and 282 seconds. Turns two to five took about 44 seconds on both builds, since this hybrid model's generation barely depends on its context length. The gain here is room and attention, not seconds.
- **`--rag-limit 12` against a small window.** Qwen3-VL-8B on a 4,096-token window with `max_tokens: 512`, over the backlog's 18 documents (258 chunks). The new build said `5 of 12 chunks fit the context budget` and sent a 607-token prompt; the old one injected all 12 in 1,571. Both answers were right.
- **A large cloud window** was not called. That behaviour is pinned instead by a test: a short conversation with retrieval on a 200,000-token window is sent exactly as before.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| The shares | Attachments 30%, retrieval and recall 20%, tool results 25%, history the rest *(default taken)* | Each a field of `BudgetShares`, not a constant in the loop; tuning them is a one-line change. |
| Finished turns' tool results | Sent as stubs, on every backend *(defaults taken)* | The answer that used a result carries what mattered, and the model can call the tool again. A cloud window gains the same discipline; it only trims less. |
| How the shares act | Retrieval capped when it asks; everything else only on overflow, in reverse priority, each to its share and then below | On a window with room nothing changes, so the loop's conformance suite passes untouched. |
| The reserve | The request's `max_tokens`, else the backend's, else 4,096, at most half the window | 4,096 is the larger of the providers' own defaults; past half the window, the question would have no room. |
| Kept whole | A result of 512 bytes or less, and every `ask_user` answer | A stub would be no smaller; the answers are the user's own words. |
| Counting | Exact where the provider counts, else four characters a token; a request whose bytes fit is not counted | Exact where possible, never mistaken: the byte ceiling cannot be exceeded, where an estimate on dense text can be several times off. |
| Reporting | Stubs under `--verbose`, every trim as a notice once while it holds | Stubs happen every turn after tools, and would be noise; a trim changes what the model sees, and is never silent. |

**Guardrails, each mutation-tested (56 mutants, all caught), run in separate git worktrees against the whole unit suite.** 48 of the first 55 were caught on the first pass. The seven that survived were caught once tests were added for what they exposed, and a 56th, for an oversized graph section, was caught by the test written for it.
- **The arithmetic:** the reserve not held back, uncapped, or taken from neither the request nor the backend; an unknown window read as room; a share of the window instead of what is left, or another source's share; the estimate missing tool calls or definitions; the exact count never asked, or marked estimated.
- **The stubs:** the turn's start taken from its first user message; this turn's results, the small ones or `ask_user`'s answers stubbed (the answer recognised by its call only); none sent, or none counted; the call unnamed, its arguments unclipped or clipped inside a character; the size in bytes.
- **The assembly:** no stubs sent; the byte shortcut taken always; exchanges never dropped, the system prompt dropped with them, or only an exchange's question dropped, leaving its answer; this turn's results never stubbed, or the newest too; the injected context never dropped, or dropped before this turn's results; each trim unsaid, a request over its window said nowhere, or an unknown window trimmed anyway.
- **Retrieval:** never fitted, fitted but unsaid, the count not cut, another source's share, `fitting_prefix` fitting all, one too many or, on an unknown window, none; a graph section too big even alone kept.
- **The loop and its surfaces:** the assembly unused, its markers not the request's, the stubs or a trim unsaid, a trim said at every step, the request's own `max_tokens` ignored; `measure_context` measuring the transcript whole; compaction shown whole tool output; the budget not passed on by `retrieve_for_collection`, or not built by chat, `complete` or `serve`.

**Not verified, and found on the way.**
- **Nothing used the attachments share yet.** [26d](#milestone-h--apogee-chat) did, the next day. An attachment is inlined only while it fits the share, so trimming to it takes nothing; past the injected context, an inlined attachment is stripped last.
- **One turn's tool results are not capped by their share** unless the whole request overflows. A single 60 KB read on a 32K window still goes in whole on its own turn, and becomes a stub on the next. Cutting what a model has just asked for, while it fits, was judged worse than the crowding.
- **A retrieval turn still re-reads a local model's whole prompt.** The injected block opens the request, so it changes every turn. 26c sizes that block and leaves where it goes alone; moving it is its own change.
- **A cloud model's overflow is judged on the estimate**, four characters a token. On text that tokenizes densely it can be under by several times. A cloud window is large, so trimming there needs the request to be near a window that big.

---

### 2026-10-03 — `tool-selection` (backlog item 26g): the tools a question needs, and a server's tools together

**Why.** Every request with tools listed every tool registered. The native toolsets alone are about twenty, and each MCP server adds its own: with two servers of a dozen tools each, a local model's prompt was 3,200 to 6,700 tokens before the conversation started, depending on how its template writes tools, and most of it the definitions. That costs reading time on a cold prompt and room in the window on every one. A small model also chooses from the whole menu, and connecting another server made every request bigger.

**What was built**

- [x] **`agentloop/tool_selection.h/.cpp`**, used by every surface through the one loop.
  - **Selection starts above 16 registered tools.** At or below that, every tool is offered and the request is byte for byte what it was.
  - **A turn offers** the core (`read_file`, `list_directory`, `run_command`, those the registry has), the eight tools its question ranks highest, and `find_tools`.
  - **An MCP tool comes with the rest of its server**, whether it was ranked, found or named (the user's call). Native tools are ranked one by one.
  - **The ranking is by meaning** when the `embedding` role resolves to a model that costs nothing to call: each tool's split name and description is embedded once, and the vectors are cached in `cache/tool-vectors.json` under the model and a hash of the definition (`agent::definition_hash`), so a changed definition is embedded again and the file is safe to delete. A metered embedder is never called for this (the spend rule); with none, or one that fails, **BM25** over the same words ranks instead, and stays the ranking for that conversation rather than retrying the embedder every turn.
  - **A question is ranked whole and by each of its clauses** (`ranking_queries`), each tool taking its best score. "APG-42's fix needs Sam's eyes: add Sam to Tuesday's design review" blended into one vector ranked only the tracker; ranked by its clauses it finds the calendar too.
  - **`find_tools`** searches what was not offered and returns up to five definitions, name, description and arguments, which are offered from the next step. Its own description names the tools not shown yet (up to 100), as Claude Code lists its deferred tools.
- [x] **The loop** (`agentloop/loop.cpp`) ranks once per turn and keeps the offered set for the turn's steps, in the registry's order. **A new turn keeps the last turn's set** while it already offers the new question's top three, because a different tool list is a different prompt prefix and a model with a prompt cache reads the whole conversation again. `find_tools` is answered in the loop. A registered tool called without being offered is dispatched and gated exactly as before, and offered from the next step. The final pass still withdraws every tool.
- [x] **Every surface.** Chat, `complete`, `analyze` and `serve` build the selection after the agent's tool policy has filtered the registry, so selection only narrows what the policy allows. A follow-up in chat is ranked by the utility model's standalone restatement: the retrieval rewrite when there is one, else its own (`tools ranked for, by <utility>: ...`). `serve` shares one ranker across requests and gives each request its own selection.
- [x] **Said.** `--verbose` prints once `[tools] 45 registered: each turn offers the ones its question needs, ranked by meaning, by <backend>`, and per turn `tools: 21 of 45 offered, and find_tools, ranked by meaning -- for this question: list_notes, read_note, ...`, with `(the last turn's tools kept)` when kept, a line for each `find_tools` call, and one for a tool called without being offered. Machine mode's events are unchanged.

**On real weights** (the model families, DEVELOPER.md → On real weights; greedy). Each family ran its Q4_K_M build, except gpt-oss, whose only installed build is F16. The four families ran side by side, each loading its model again for each of its four runs, so the times are not comparisons. That was before the user's rule later the same day: one family at a time, its model loaded once. The registry held the 21 native tools and two MCP servers written for this, an issue tracker and a calendar of 12 tools each: 45 tools. The calendar refuses an event id that does not exist, as a real one would. Ranked by Embedding-Gemma-300M.

- **The battery**, eight tasks, each its own conversation: read a file, write one, count lines with the shell, find a file in a folder, read a link and fetch it, arithmetic, open a ticket, and add someone to a meeting found through the calendar. A task passes when its tools were called and worked, or, for the arithmetic, when the answer is right.

  | Family (model) | Every tool | Selected | Prompt per step, every tool → selected |
  |---|---|---|---|
  | OpenAI (gpt-oss-20b, F16) | 8/8 | 8/8 | 3,253 → 1,597 |
  | Google (Gemma 4 12B, Q4_K_M) | 8/8 | 8/8 | 4,698 → 2,257 |
  | Qwen (Qwen3-VL-8B, Q4_K_M) | 8/8 | 8/8 | 5,306 → 2,549 |
  | Meta (Llama 3.1 8B, Q4_K_M) | 6/8 | 6/8 | 6,660 → 3,199 |

  The first acceptance criterion holds on every family: the battery passes as it did with every tool, at about half the prompt. Llama 3.1 8B failed the same two tasks both ways: it fetched an address it made up instead of reading the file, and linked tickets instead of using the calendar. A turn offered 8 to 30 of the 45.
- **A four-turn chat** (read a file; count lines; read the budget file; open a ticket). Every tool: 4/4 on every family. Selected: 4/4 on gpt-oss, Gemma and Qwen; Llama 3.1 8B 2/4, answering two turns by describing the call it had made ("This is the response from the `run_command` function...") rather than giving its result.
- **What a prompt cache changes.** With every tool, the tools are the same prefix on every request, and a local model's cache reads them once: after the first turn, a step read 20 to 100 new tokens. A selection that changes reads its prompt again, about 1,500 to 4,000 tokens. Over the four-turn chat the selected runs read more new tokens in all: gpt-oss 5,473 against 3,635, Qwen 7,271 against 5,598, Gemma 7,237 against 5,186, Llama 9,051 against 6,896. Keeping the set saved one read in each chat (the ticket turn, after the budget turn had offered both servers). What selection buys is a prompt half the size on every step, and a cold start half as long (a new chat, a `complete`, a request a server has not seen); not fewer tokens read in one long chat.
- **`find_tools`, the second criterion, met on gpt-oss-20b only.** It was measured before servers were offered whole, when "add Sam to Tuesday's design review" was offered the tracker and not the calendar's `list_events`. gpt-oss searched in all three runs of it (`find_tools "list calendar events"`) and was offered `list_events`. It finished twice: once after two searches and no refused call, once after six refused invites and one search. The third time it searched after five, then broke off with a malformed call. Qwen3-VL-8B, Gemma 4 12B and Llama 3.1 8B never called `find_tools` in any run, with or without the hidden tools named in its description: they used the wrong tool, or invented the event id and were refused, some until the loop's bound. With servers whole, no task in the battery or the chat needed a search, and none was made.
- **The third criterion** (12 tools: byte-identical requests) is a test, not a run.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| Threshold and size | Above 16 registered tools; the top 8 plus the core *(default taken)* | The native toolsets are about twenty, so a chat with tools selects even before any server is connected. Eight covered every battery task's tools on every family. |
| The core | `read_file`, `list_directory`, `run_command`, `find_tools` *(default taken)* | The tools almost every task begins with; ranked out, a model cannot even look around. |
| `find_tools` | Returns definitions, offered from the next step *(default taken)* | A tool dispatched unseen would be called with arguments the model guessed. |
| MCP servers | **Offered whole** — the user's call | A server's tools need each other: an invite needs the event id `list_events` finds. On a local model a tool not offered cannot be called at all, because its name is outside the grammar, and small models do not search. In the last run with single tools the cross-server task failed on every family; whole, it passed on three of four, as with every tool. |
| Families | One Meta, Qwen, Google and OpenAI model each, optional, never DeepSeek; Qwen3.8-27B excluded until 26i; later the same day, each on its Q4_K_M build when one is installed, one family at a time with its model loaded once — the user's calls | Replaces the document's two acceptance models; recorded in DEVELOPER.md → On real weights for every later item. |
| The ranking | Embedding role when free, else BM25; best of the question and its clauses | The spend rule; one vector for a two-part question goes to the stronger part. |
| Kept sets | A turn keeps the last set while it offers the new top three | A changed tool list re-reads the whole prompt on a cached model. |
| Unoffered calls | Dispatched and gated, then offered | Models remember tools from earlier turns; failing the call would punish that. |

**Guardrails, each mutation-tested (37 mutants, all caught), run in separate git worktrees against the whole unit suite.** 34 were caught on the first pass. The three that survived were caught once tests were added for what they exposed. Vectors remade on every ranking went unseen because the remake read them back from the cache: a second ranking now must not read the cache, and an embedder that failed must not be asked again. The restated question ignored, by the loop or by chat, went unseen because the tests checked a tool the server rule offered anyway, and a progress line printed before the restatement was used: the tests now check what the turn was ranked for.
- **The selection:** the threshold off by one, the core dropped or offered without being registered, the top count, the set never or always kept, a server split or its prefix wrong, a question's clauses or its best clause ignored.
- **`find_tools`:** returning tools already offered, or ones sharing no word of the query; offering nothing it found; more than five; the hidden names uncapped.
- **The vectors:** remade on every ranking, the cache key without the model, the cache never written; the definition hash without the description, or without separators between fields; the cache file not merged with another writer's, its version ignored, or kept outside `cache/`.
- **The loop and its surfaces:** the selection unused, `find_tools` not offered, the hidden tools unnamed, `ask_user` dropped, a named tool not offered next, `find_tools` not answered, the restated question ignored, the offer unsaid; a metered embedder called; the surface's threshold off by one; selection not passed by chat, `complete` or `serve`, or the chat's restatement unused.

**Not verified, and found on the way.**
- **A selection reads more in a long local chat than offering everything** (above). The prompt is half the size on every step; the re-reads are the price, and the keep rule only limits them. A persistent prompt cache (26j) would make every tool's prefix cheaper still across processes; whether selection should then stay on for a local model with few servers is worth measuring there.
- **A question that needs no tool still pulls in servers.** For "What is 17 * 23?" the top eight are whatever ranks least badly, MCP tools among them, so both servers came with them: 28 of 45 offered. A floor on the score before a server is pulled in whole would stop that.
- **Embedding-Gemma is used without its task prefixes** (`task: search result | query:` and `title: none | text:`), as retrieval uses it too. "The budget file in the docs folder" ranked the tracker's sprint tools above `read_file`. The prefixes belong in the embedding clients, for retrieval and here alike.
- **Small models do not search.** `find_tools` helps a model that reasons about what it lacks (gpt-oss); for the others, offering a server whole is what works.
- **A cloud model** was not run. Its requests go through the same loop and the same tests; a cloud prompt cache is keyed on the prompt's start too, so the same trade-off should hold there.

## Milestone G — The terminal UX layer

**Goal.** Build the presentation layer once, so every interactive surface renders identically and the loop stays I/O-free. `apogee complete` is retrofitted onto it in the same change — two Reporter implementations that drift is precisely the parity bug the interface exists to prevent, and the retrofit is what makes "one shared loop, thin adapters" true rather than aspirational.

### 2026-08-26 — Status line, thinking view, ansi modes, and `cliReporter`

**What was built**

- [x] **The ansi layer** (`source/ansi/ansi.h/.cpp`) — `Style`, `Role` tags, and `resolve_color()`, which is the single place the three independent colour switches (`NO_COLOR`, `--no-color`, non-TTY) are honoured. A disabled `Style` returns its input unchanged, so no caller branches on colour.
- [x] **`TerminalWriter`** (`source/commands/terminal.h/.cpp`) — the one serialization point. A spinner repainting at 120 ms and a token stream from the provider's reader thread otherwise interleave mid-escape-sequence and tear the line.
- [x] **The thinking view** (`source/commands/thinking_view.h/.cpp`) — the rolling two-line window and collapse-to-summary, per the item's appendix. `wrap_tail` wraps **by codepoint**, drops blank rows, and keeps the last N; the retained tail is bounded and trimmed on a codepoint boundary.
- [x] **The status line** (`source/commands/status_line.h/.cpp`) — self-overwriting transient line, the animated spinner with its elapsed/token frame, and the **generation counter** that invalidates an in-flight repaint when something prints permanently.
- [x] **`CliReporter`** (`source/commands/cli_reporter.h/.cpp`) — the `agentloop::Reporter` adapter. **Answer to stdout, progress to stderr**, which is what keeps `apogee complete "…" | jq` working.
- [x] **The retrofit** — `complete.cpp`'s inline `CompleteReporter` is gone; there is exactly one `Reporter` implementation in the tree. `complete` gained `--no-color`, and its verbose notice now goes through the status line rather than raw stderr.
- [x] **`ask_prompt` routed through the status line** — the prompt is a permanent print that bumps the generation counter, so it cannot land on top of a spinner frame.
- [x] **`platform::terminal_width()`** added to the portability seam.
- [x] **45 new tests** (313 total) — byte-level assertions over an injected stream, plus a PTY check driving the real binary.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| Thinking window height | **Fixed at 2 lines** | The stated default. Two is the Claude CLI's shape and needs no explanation; a setting invites someone to set it to 40 and recreate the scrollback problem the view exists to solve. `kTailLines` is a compile-time constant, so changing it is a deliberate edit. |
| `complete` retrofit | **In this item, not deferred** | Leaving the inline adapter would have meant two Reporter implementations drifting. The whole point of the interface is that a capability reaches every surface. |
| Answer vs progress | Separate streams, separate objects | Conflating them is how decoration ends up in a pipe. `CliReporter` takes the status writer and the answer stream as distinct things. |
| Colour resolution | One function, three inputs | Scattering the logic is how one output path eventually forgets a case and writes escape codes into a pipe. `resolve_color()` is table-tested across all eight combinations. |
| Quiet | Suppresses status, **not** warnings | Suppressing those would make a failed run look like a successful silent one. |

**Notes.** The appendix's central claim held up: **the erase arithmetic only works because nothing painted is ever allowed to wrap.** `repaint()` deliberately leaves the cursor on the last painted row with no trailing newline, because `erase()`'s row count depends on it — that coupling is now pinned by a test asserting the exact number of `\033[A\033[2K` pairs a repaint emits.

The assertion worth keeping is *after `finish()`, the bytes past the last erase are exactly the collapsed summary*. It is what proves no reasoning survived into scrollback, and nothing weaker does — the reasoning text is present in the byte stream either way; what matters is that it has been erased from the screen.

Two test-side bugs of my own, both caught by running them. A UTF-8 assertion I wrote as `CHECK(cond ? true : true)` asserted nothing; it is now a real completeness check, with its own test proving the checker rejects a split sequence rather than passing vacuously. And the summary assertion searched for the last `\r\033[2K`, but an erase sequence *ends* with cursor-up-and-erase pairs which also end in `\033[2K` — the product was right, the test's premise was not.

The PTY check (`tests/pty_startup_check.py`) is the only test here that sees what the user sees. `apogee` asks whether stdout is a terminal before rendering anything, so a pipe-based test exercises the branch that deliberately emits nothing — it would pass on a build that rendered garbage interactively. The script replays the escape codes to reconstruct the final screen, then asserts the startup notice appears exactly once on one row and that **no spinner frame survived**. Verified against a build that also wrote the notice raw to stderr: the check failed it, naming both rows.


### 2026-09-25 — Terminal Markdown rendering (backlog item 23)

Asked for directly (Taylor, 2026-09-23, with a Qwen3.5 transcript): "the formatting here is HORRIBLE, and we need to plan out a markdown interpreter for the terminal view like this." Specced that day. Built 2026-09-25 on the v0.1.2 branch, after the user's calls: a hand-written parser rather than md4c; no syntax highlighting in the first cut; the recorded house style kept.

**What was built**

- [x] **`source/markdown/`**, a new package that includes only `ansi/` (a new `harness.layering` rule, mutation-checked). `StreamRenderer` produces **render operations** -- rows to commit, and the open area as it now stands -- never bytes.
  - `render_inline` (`inline.h/.cpp`): emphasis by CommonMark's delimiter-run algorithm, so `snake_case_name` and `2 * 3 * 4` stay as written; code spans; links (`[text](url)`, `<url>`, bare URLs without their closing punctuation); escapes; images as `[image: alt]`; `~~strike~~`.
  - `wrap` and `hard_wrap` (`layout.h/.cpp`): words, a first-row prefix and a hanging indent; code is cut at the width, never reflowed.
  - The block state machine: ATX headings (bold, levels 1–2 cyan), lists with bullets `•`/`◦`/`▪` by depth, their numbers, task boxes, and hanging indents; nested lists under their parent's text; lazy continuation; quotes behind `│ `; fenced code as a dim block labelled with its language; rules; tables laid out with their alignment; blank lines collapsed, trimmed at both ends.
- [x] **Line-at-a-time commits, the open line redrawn** (the 2026-09-23 decision): a line is final when its newline arrives, and only the line still arriving is repainted, with anything unclosed in it shown as written.
- [x] **`commands/answer_view.h/.cpp`**, the painter.
  - Committed rows are written once. The open area is erased by counted rows and repainted.
  - Rows wrap one short of the width, measured at every paint.
  - The open area never paints more rows than the screen holds (`platform::terminal_height`, new), so an erase never reaches scrollback; a single line taller than the screen shows its last rows until its newline commits it whole.
  - A chunk that changes nothing visible costs no repaint.
  - Links are OSC 8 hyperlinks on the terminals known to support them (`ansi::hyperlinks_supported`, an allow-list), and `text (url)` elsewhere.
- [x] **`CliReporter`** renders through the view when it decorates and `markdown` is set. `chat` and `complete` opt in; `analyze` and the others do not yet. A pipe, `> file`, machine mode and the transcript receive the model's text byte for byte.
  - **An answer still open is committed before any status takes the terminal**, so the spinner or a tool line no longer paints over "Let me look at that file." when a model writes before calling a tool.
- [x] **`--raw`** on `chat` and `complete`, and **`ui.markdown: false`** (new section, gettable, in the template), switch rendering off. Off, the terminal shows the text with the layout it had before.
- [x] **Type-ahead hidden during a turn**, which the item carried from the same report (the duplicated question).
  - `platform::TypeaheadGuard` turns input echo off for the turn. Keystrokes typed while the model answers stay queued and appear once, at the next prompt.
  - Ctrl-C, Ctrl-\, a hang-up and a termination restore the terminal before the signal's own action runs; Ctrl-Z restores it and hides it again on resume. All of this happens in async-signal-safe handlers.
  - `platform::EchoPause` turns echo back on while a turn asks something (the permission prompt, `ask_user`), so the answer is seen as it is typed.
- [x] **Shared width arithmetic.** `display_width`, `codepoint_cells` and `wrap_tail` moved from the thinking view to `ansi/text_width.h/.cpp`, and the wide table gained the emoji with default emoji presentation (✅ ❌ ⚡ ⭐ ✨ 🚀 …). Counted as one cell, every table holding a check mark went out of line.

**What the tests found.** The terminal model the item asked for (`tests/support/terminal_model`: printable cells, `\r`, `\n`, erase-line, cursor-up, deferred wrap, and a record of the widest column and of any climb into scrollback) caught one real bug before any user could. **A chunk ending inside a UTF-8 sequence** put a fragment in the open area; the renderer counted it as one cell, the terminal did not, the row wrapped, and the erase left a line behind. Chunking invariance failed at every width, and the last column was reached. The open area now holds back an incomplete character until its remaining bytes arrive, as a terminal does.

**What the real answers changed.** The item's bar was a corpus of real answers. Four were recorded with `apogee complete --raw` from the three local models: Qwen3.8-27B (twice: one of them its violin answer from the replayed four-question chat), Qwen3-VL-8B and Llama 3.2 3B. Two decisions moved because of them:
- **A table wider than the screen now wraps its cells within narrower columns** (revising the 2026-09-23 "falls back to its raw rows", which was mine). Qwen3.8-27B's comparison table is about 160 columns wide, so it would have printed as raw pipes at every width, 120 included. Columns narrower than an even share keep their width, the wide ones divide the rest, and only a table that cannot fit even 8-column columns falls back to its rows as written.
- **A setext underline is a rule.** Llama 3.2 writes its headings as `Title` / `=====`; line at a time, the title is committed before its underline arrives, so `===` draws a double rule `═` rather than a row of equals signs.

**Known limits, recorded.**
- An emoji ZWJ sequence (🧑‍💻) is counted as its parts (four cells) where terminals draw one glyph (two). Overcounting only wraps a row early, so it is safe for the erase arithmetic.
- An open line taller than the screen shows only its tail until it ends.
- Code blocks are not highlighted (the user's call).
- HTML is shown as written.

**Verification.**
- New tests:
  - inline (the flanking rules, unclosed markers, links, escapes);
  - one renderer case per construct, plus a split UTF-8 character;
  - chunking invariance: whole, per character and random splits, for the renderer and for the painted screen;
  - the view: last column, a line taller than the screen, a resize mid-answer, hyperlinks;
  - the reporter: the rendered path, the pipe path byte-identical with markdown on, and a status committing the open line;
  - the corpus at 40, 80 and 120 columns on a 24-row screen: 2,333 assertions;
  - emoji widths.
- `cli.chat_typeahead_and_crash_safety` gains three PTY checks: `markdown` (no `**` around rendered bold, and `**bold**` with `--raw`), `typeahead-hidden` (words typed mid-reply absent from the reply, present at the prompt) and `interrupt` (echo off mid-turn, on after SIGINT). A build without the guard fails the second and third, and one whose signal handler does not restore echo fails the third.
- On the real binary under a 100-column PTY, the Qwen3.8 fixture rendered with its table wrapped in columns.


### 2026-10-03 — `cli-busy-line` (maintenance item M1): every slow command speaks on one line

**Why.** `apogee models list` went silent for seconds (the user's report, 2026-09-30). On this machine's store it made 31 reads in about 15 seconds, and `check`, which reads most of them twice, took about 30, with nothing on screen. Chat solved this in the 2026-08-26 entry above: the status line, one repainted line with a spinner, active only on a terminal. But only a conversation ever made one, so an ordinary command had no line to speak on. This item carries the same painter across the application.

**What was built**

- [x] **The general frame** (`spinner_frame`, a second overload beside chat's), for example `✻ reading model headers: gemma-4-31B-it-F16.gguf (6/31 · 7s)`:
  - the spinner, the label, then `(done/total)` when the sweep knows its total;
  - the elapsed time from two seconds;
  - never the last column: the frame is cut to the width less one, the label giving way first, and the count and time never cut.
- [x] **`StatusLine` grew a busy mode** (`start_busy`), rather than a second painter being written:
  - its first frame comes only after a delay, so a fast command paints nothing;
  - the label and count change from any thread (`set_spinner_label`);
  - stderr's width is asked at every repaint;
  - `print_above` writes output meant to stay with the line out of its way, and the line repaints below it.
  - Stopping now wakes a waiting spinner instead of sleeping out its interval, chat's spinner included, so a command that finishes early is never kept waiting.
- [x] **`BusyLine`**, the scope a command opens around slow work:
  - constructing it means the line may appear; destroying it (or `finish`) clears it, so what the command prints next starts on a clean row;
  - `report(label, done, total)` and `set(label)`, and `sink()`, the same as a `BusyProgress` callback for a sweep to report through;
  - `above(write)` for a line that stays.
  - `busy_options(quiet)` makes it active only when stderr is a terminal and nothing asked for silence. Otherwise it writes no byte and starts no thread.
- [x] **`platform::terminal_width(StandardStream)`.** The line paints on stderr, so it is measured on stderr. Under `apogee models list | grep x`, stdout is the pipe and stderr the terminal.
- [x] **The first consumers**, each reporting through a plain callback from the layer doing the work. `models/` is untouched: the header reads are in `commands/`.
  - `models list` counts its reads across the whole sweep before the first: configured local files, stored GGUFs no backend points at, and SafeTensors snapshots. `models info` and `models status` name the header they read, without a count, since neither knows a total worth claiming.
  - `check` says each section as it starts, and counts the headers in Config and in Models.
  - `graph build` and `graph communities` move their per-chunk and per-community lines onto the busy line on a terminal. On a pipe those lines stay as they were, one per step, a log a script can read. A failed chunk and a dry run's extraction print above the line.
  - `embed ingest` counts its files: the walk is counted before the first is read (`embedstore::IngestProgress`, an optional parameter with an empty default).
  - There is no `graph update` yet; it adopts the line when it is written.
- [x] **`-q, --quiet`** on every one of them: no busy line, and for the graph commands no per-step line on a pipe either. Results, warnings and failures still print. `embed ingest --graph --quiet` carries it into the build it chains. `models list --output-format stream-json` is silent the same way.

**On the real store** (this machine's, read only: 31 stored models and 20 local backends, a 100-column pseudo-terminal, built without llama.cpp, with three mutation builds running beside it, so the times are long):
- `models list` showed 178 frames over 22 seconds, from `(1/31)` to `(31/31)`, then the table on a clean first row.
- `check` showed 353 frames over 44 seconds, counting Config's reads to 20 and then Models' to 31.
- No frame reached the last column (the widest was 78 of 100), and no residue was left.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| The frame | Spinner · label · `(done/total)` when known · elapsed from 2 s; the label cut to the live width *(default taken)* | The count and the time are what tell a stalled sweep from a working one, so they are never cut. |
| The gate | 150 ms before the first frame *(default taken)* | A fast command never flickers. Elapsed still counts from the start of the work. |
| The consumers | `models list/info/status`, `check`, `graph build` (and `communities`), `embed ingest` *(default taken)* | The known slow spots; a later slow command adopts the same scope as it is touched. |
| Quiet | `--quiet` silences the busy line, and on a pipe the graph commands' progress lines too *(default taken)* | One verbosity model: what silences the status line silences this. |
| One painter | `StatusLine` extended, never a second | Decided when the item was specced (2026-09-30): a second transient line is the erase-arithmetic bug class the thinking view closed once. |
| Pipes | Graph progress kept a line per step; the other consumers print nothing | The line is for terminals; a log needs lines it can read, and the commands that printed none print none. |

**Guardrails, each mutation-tested (38 mutants, all caught)**, run in separate git worktrees against the whole unit suite, the PTY check and the graph end-to-end on the mutated binary. 37 were caught on the first pass, 14 of them only by the binary-level checks. The survivor, a stop that never woke the spinner, went unseen because the test stopped the spinner before its thread had reached its wait; the test now waits for the first frame first.
- **The frame:** the time from one second, the count dropped, the last column used, the label or a narrow frame not cut, the ellipsis not counted, a count shown with no total.
- **The line:** no delay, a stop not woken, the width measured once, stderr's width asked of stdout (in `status_line` and in `platform`), the line painting on a pipe or ignoring `--quiet`, output above it not erased first, `finish` leaving the line, a label change never shown.
- **The consumers:** each sweep's report unsaid or uncounted (configured reads, snapshots, `info`, `status`, Config's and Models' reads, an ingest's files), the sink never passed by `models list` or `check`, JSON output painting, the graph's and the ingest's lines unused, and every `--quiet` ignored, the one `embed ingest --graph` carries included.

**Tests.**
- `status_line_test`: the frame as goldens, including the shapes with no total and under two seconds; every width from 1 to 80, wide characters included; an inactive line writes nothing; a delay outlasted by the work paints nothing and is not waited out; the line repaints in place and clears; output above it; the width asked at every repaint; a stop that wakes the spinner.
- `models_test`, `check_test`, `ingest_test`: what each sweep reports, and that reporting changes nothing printed. `graph_test`: `embed ingest --graph --quiet` is quiet in the build it chains.
- **`cli.busy_line`** (`tests/pty_busy_check.py`), on the real binary. A model's sidecar and an agent's schema are named pipes, so the sweep is held, with no test seam in the product, until the script has seen the line on the terminal; the graph extractor and summariser are the mock, slowed by `delay_ms`; an ingest of 8,000 files is slow by being big. For `models list`, `check`, `graph build`, `graph communities` and `embed ingest` it checks that:
  - the line repaints naming its phase and count, and no frame reaches the last column;
  - the final screen holds only the results;
  - piped, `models list` writes nothing to stderr, and its stdout is byte-identical whether stderr is a terminal or a pipe;
  - `--quiet` and `stream-json` paint nothing, and a sweep under 150 ms paints nothing.

  Run against the installed build from before this item, it fails on every frame it never painted.
- `graph_e2e.sh`: on a pipe, the build keeps a line per chunk and writes no escape byte, and `--quiet` keeps only the summary.

**Not verified, and found on the way.**
- **Why `models list` is slow.** Sampling it showed nearly all the time inside `inspect_gguf`'s `skip_value`. Each string of a tokenizer's vocabulary is skipped with its own `seekg`, which discards the stream's buffer, so a 150,000-token vocabulary costs some 300,000 system calls per file: about 10 of the 15 seconds were system time. That is the read itself, outside this item. Reading the vocabulary in buffered blocks could make the header cache (M2) unnecessary, and is worth trying first. It did: M2 shipped that fix instead of the cache, the same day ([Milestone N](#milestone-n--model-operations)).
- **A failed chunk printed above the line** is covered by the code path, not a test: no fixture makes a chunk fail on a terminal.
- **Windows** builds the stream-aware width but runs no PTY check, the recorded per-item skip.

---

### 2026-10-04 — `thinking-side-calls` (backlog item 26n): a turn's other model calls, in the thinking block

**Why.** A turn is no longer one model call. Around the chat model a question can run the embedder, the utility model -- restating a follow-up, summarising a tool result, compacting -- the rerank judge, and on `/capture` the clerk. That work was invisible, or a status-line blip, while the chat model's own reasoning had a home. The user asked for one story in one place.

**What was built**

- [x] **One Reporter event** (`agentloop/side_call`, `Reporter::on_side_call`): a side call's role and what it is doing, said when it starts and again when it is over, with its elapsed time. `SideCallScope` says both; a null sink says nothing; no number is ever estimated.
- [x] **Said where the calls are made**:
  - retrieval: the question's embedding and the rerank judge (`RagTurn::on_side_call`), so `auto_rag`, a chat's attachments and recall all narrate them;
  - the loop: a tool result's summary;
  - `chat`: the follow-up rewrite (only when there is an earlier turn), mid-turn compaction, and the `/capture` clerk, whose block then collapses instead of printing a bare status line -- the clerk is named in a failed capture's line instead (`capture by prose failed: …`), which a pipe still shows.
- [x] **Drawn inside the thinking block** (`ThinkingView::side_call`, `side_call_done`):
  - `· <role> — <what it is doing>`, dim, each on a line of its own;
  - cut to the width, interleaved with the reasoning in arrival order;
  - completed in place with ` · 0.6 s`.

  A block of side calls alone stays open when the step begins, so its reasoning joins it; one with no reasoning collapses to `✻ Worked for Ns`.
- [x] **Everywhere else, nothing new.** Machine mode says a side call as the existing `tool_status` event at its start, and `cli.machine_schema_conformance` passes unchanged. `serve` gets no frame, a pipe draws nothing, and no transcript, `result` or answer carries any of it.
- [x] **Lines said during a turn land above the block** (`ThinkingView::print_above`, `CliReporter::keep_line`) -- found on a real terminal, where a retrieval line printed into a block of side calls and the block's next erase missed by a row.

**On a real terminal** (Gemma 4 12B, a `notes` collection embedded by Embedding-Gemma, `--rerank on`):
- the block read `· embedding — the question → Embedding-Gemma-300M · 0.4 s`, then `· rerank — judging 2 results with Gemma4-12B-Q4KM · 8.7 s`, then Gemma's own reasoning, in one block that collapsed to `✻ Thought for`;
- the PTY chat check drives the same story with mock models on every run: a follow-up's rewrite narrated, `✻ Worked for`, and a clean saved chat.

**Tests**: 14 new cases -- the scope and the suffix; the view (arrival order, own lines, completion in place, `Worked for`, the cut, an inactive view); the reporter (one block across the step, a pipe, lines above the block against the terminal model); machine mode's `tool_status`; retrieval's embedding and judge; the loop's summary; and in `chat`, machine mode hearing the tool summary, compaction, the rewrite once and the judge, while a pipe and the saved chat hear nothing. The PTY chat check gained an eighth case.

**Guardrails, each mutation-tested (20 mutants, all caught -- three only once their tests were sharpened: the cut, the judge, `retrieve_for_collection`'s sink), in a separate git worktree:** the scope's end, time and tokens; the view's opening, own line, completion, wording, reasoning flag and cut; the reporter's open block, both draws and lines above the block; machine mode's single line; each emission site.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| The line | `· <role> — <what it is doing>`, completed with the time *(confirmed by the user)* | |
| A block with no reasoning | Still appears, and collapses *(confirmed by the user)*, to `Worked for` *(for veto)* | `Thought for` would say it thought. |
| The first-cut set | Embedding, rerank, rewrite, summary, compaction, the clerk *(confirmed by the user)*; vision and transcription stay with their attachment lines *(for veto)* | Those run in the indexer's background worker, which the item keeps off the block. |
| `auto_rag`'s choice | A status-line notice *(confirmed by the user)*, now above an open block | One-shot state, not a side call. |
| Machine mode | `tool_status`, at a call's start | The existing display-prose event; no vocabulary change. |
| `serve` | No frame *(for veto)* | Its retrieval has `rag_search`/`rag_result`; progress on `notice` would change that frame's meaning. |
| Tokens | Not yet reported by any call site | Shown only when truly known. |

## Milestone H — `apogee chat`

**Goal.** The richest v0.1.0 surface, and the one that proves the harness holds together over a conversation rather than a single turn: a persistent, resumable REPL where switching models mid-session is a lookup, not a reconstruction.

### 2026-08-26 — The REPL, sessions, and the logging subsystem

**What was built**

- [x] **The REPL** (`source/commands/chat.h/.cpp`) — slash commands (`/help /model /models /system /temperature /max-tokens /compact /title /exit`), `--tools`/`--search`, `--image` attachments, `--resume`/`--continue`. Every configured backend is constructed up front, so `/model` switches instantly with history carried over — uniform across providers because history is neutral IR, not a vendor transcript.
- [x] **Session persistence** (`source/logger/session.h/.cpp`) — a versioned JSON file per conversation, **rewritten after every completed turn** through the config engine's temp-file-then-rename path. `chat_id` is immutable; renaming sets `custom_name`.
- [x] **Resume that degrades, never fails** — a legacy schema, a vanished backend, a field of the wrong shape, an unreadable message: each produces a `[resume]` warning and a working session.
- [x] **`apogee chats`** (`source/commands/chat_history.h/.cpp`) — `list`, `info`, `title`, `delete`, plus background auto-titling after the first exchange as a **side request** so it never enters the conversation's own history.
- [x] **Context monitoring** — warn at 80%, auto-compact at 90%, `/compact` on demand. Covers every backend, because Apogee owns the transcript everywhere.
- [x] **The operational log** (`source/logger/operational.h/.cpp`) — one file per day, append-only, one greppable line per event. A session records *what was said*; this records *what the program did*.
- [x] **The typeahead gate** (`source/commands/input_gate.h/.cpp` over `platform::discard_pending_input()`) — flushed once immediately before the first prompt, never between turns.
- [x] **33 new tests** (346 total), including a PTY + SIGKILL harness for the two behaviours no unit test can reach.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| Line editing | **replxx** (recorded) | The stated default. *Not yet wired* — see Notes. |
| Session format | A single JSON file rewritten per turn | The stated default, crash-safe through the temp-file-then-rename write. Append-only JSONL survives a crash equally well but turns "read the session" and "rewrite after compaction" into a replay. |
| Typeahead | **Discard, never toggle ECHO** | Suppressing echo needs restorable terminal state across a stretch of code with many exit paths, and an exit that skips the restore strands the user's shell with echo off. Discarding needs no state to restore. |
| Auto-titling | A side request | It must never enter the conversation it is titling, and a failed title is cosmetic — it never costs a turn. |
| Corrupt session files | Skipped in listings, not fatal | One bad file must not make `apogee chats list` unusable. |

**Notes.** One real bug, and it is the kind a unit test would not have found because the unit was correct: **context was measured against the saved history alone, before the incoming message was appended.** Every first turn therefore read as empty, and a single large prompt never tripped the threshold it should. It surfaced in the manual acceptance sweep, not the suite. The fix measures the *prospective* request — history plus this turn — and compacts only the prior history, since folding the message the user just typed into a summary of the conversation would summarise away the question being asked. `context is measured against the message about to be sent` now pins it.

Both PTY-only guardrails were verified against the mutation each exists to catch. Removing `discard_startup_typeahead()` made the pre-prompt keystroke become the session's first message; removing the per-turn `save()` left only one turn on disk after a `kill -9`. Neither reproduces without a real terminal and a real SIGKILL — `tcflush` applies to a terminal input queue, and on a pipe the code deliberately does nothing.

**`replxx` was not wired when this shipped** — the REPL read with `std::getline`, so arrow keys arrived as escape sequences. Filed as its own item and completed the same day; see the subsection below.

### 2026-08-26 — Line editing

- [x] **replxx pinned** (`release-0.0.4`, FetchContent with `FIND_PACKAGE_ARGS`) and linked privately into `apogee_core`.
- [x] **`LineReader`** (`source/commands/line_reader.h/.cpp`) — an interface with two implementations: `PlainLineReader` (`std::getline`) and `EditingLineReader` (replxx). `make_line_reader` picks by whether **both** stdin and stdout are terminals.
- [x] **Input history** at `<APOGEE_HOME>/chat_history`, capped at 1000 entries, loaded at start and synced at exit. Per-user, not per-session.
- [x] **Tab completion** over the slash commands and the configured backend names, completing only the last token so `/model cla<Tab>` completes the model rather than the whole line.
- [x] **One vocabulary for `/help` and completion** — `slash_commands()`, so a command cannot be offered on Tab and then rejected.
- [x] **8 unit tests + a PTY check** (355 total).

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| Input history | `<APOGEE_HOME>/chat_history`, 1000 entries | The stated default. Per-user because recall across sessions is the point; never in the session file, which records the conversation rather than the keystrokes. |
| Reader selection | **Both** stdin and stdout must be terminals | replxx draws on stdout. A terminal stdin with a redirected stdout would write escape sequences into the redirect — and that redirect is the answer the user asked for. |
| Two implementations, one interface | Rather than a conditional inside one reader | The non-TTY path is a *deliberate implementation* with its own tests, not an untested fallback branch. A piped conversation is how the crash-safety suite drives chat. |

**Notes.** The interesting part was in the tests, not the code. replxx puts the terminal in **raw mode, where Enter is `\r`, not `\n`** — the kernel performs no translation, so a bare `\n` lands in the edit buffer as literal text instead of submitting the line. Both PTY harnesses were sending `\n`, which had been correct while the REPL used `std::getline` in canonical mode. The line-editing check failed on it, and so did the *existing* `cli.chat_typeahead_and_crash_safety` — a real regression in the harness, caught only because the new check forced the question. Both now send `\r` at an interactive prompt, with the reason recorded where the bytes are written.

The typeahead check also needed restructuring to drain the PTY **while** waiting rather than only at the end: the buffer is small, and a child blocked writing into a full one never gets round to reading the next line typed at it.

Verified against a build forced to always use the plain reader: the check failed with `an arrow key reached the message as text: ['remember this line', '\x1b[A']` — exactly the symptom this item existed to fix.

### 2026-09-25 — Chat input completion (backlog item 24)

Asked for directly (Taylor, 2026-09-25): Claude Code's `/` command list and `@` file mentions, in `apogee chat`. Specced and pulled to the top of v0.1.2 the same day, and built that day after the user's one call: until the attachments item ([26d](#milestone-h--apogee-chat), shipped 2026-09-29) lands, a sent `@file` mention stays plain text, with no stopgap that pastes the file's contents.

**What was built**

- [x] **Suggestions drawn as you type.** replxx's own hint rows sit under the input line (at most five, then a `+9 more — type to narrow` row), each a label and a one-line description in a shared column.
  - `/` at the start of a line lists the commands, narrowing per keystroke.
  - After a command, its own values are listed: backends after `/model` (each described by type and model), `auto`/`lexical`/`vector`/`hybrid` after `/retriever`, `off`/`auto`/backends after `/rerank`, statuses after `/capture`.
  - `@` at the start of any word lists files and folders from the working directory. Folders get a trailing `/`, hidden entries appear only for a leading `.`, a name matches ignoring case unless it has a capital, and a path with a space completes quoted (a folder's quote stays open to go further). `me@example.com` never triggers it.
  - Nothing is offered in the middle of prose, so backend names no longer complete there as they did.
- [x] **Tab takes the top row.** On `/mo` it inserts `/model ` with the space, so the next rows are already the backends.
- [x] **One command table** (`commands/chat_completer.h/.cpp`, new): verb, argument shape, description and argument values, declared once. `/help` prints it, completion offers it, and the REPL dispatches through it by a `switch` compiled with `-Werror=switch` for that block. A row without a handler fails the build (mutation-checked: deleting the `/title` case is a compile error). `/retriever` and `/rerank`, dispatched but in neither the old completion list nor `/help`, are in all three now. `/help` prints one command per row, its description wrapped under a shared column and short of the last column, or beneath the command on a terminal too narrow for a column.
- [x] **One suggestion protocol for the line reader** (`commands/line_reader.h/.cpp`). `Options.completions`, a flat word list, became `Options.suggest`: text before the cursor in; the span to replace and the candidates, each with text, label and description, out. Both of replxx's mechanisms are wired to it.
  - `layout_hints` lays the rows out, and `apply_suggestion` is what Tab does. Both are pure and tested with no terminal.
  - `analyze`, the second consumer, keeps its behaviour through `word_suggester` and shows no rows, so its Tab still completes like a shell.
- [x] **The pipe contract held.** Only `EditingLineReader` suggests; the plain reader is untouched.

**What the real terminal found.** Every bug here was found under a pseudo-terminal replayed through a small screen model, not in the unit tests:
- **replxx's own Tab prints a shell-style list into the scrollback** when completion is ambiguous, and that cannot be switched off in 0.0.4. With the rows already on screen, that was a second copy of them left in the transcript. In chat, Tab is bound to Apogee's own handler. replxx's row browsing (Ctrl-↑/↓) is off there too, because it marked a row that Tab would not take.
- **A line sent faster than replxx repaints left its suggestion behind.** "Faster" means type-ahead released at the prompt, or a paste. replxx commits with one last repaint, and when earlier repaints were skipped, that repaint draws the suggestions afresh: `You: /exit  Save and leave` stayed in the transcript. Enter and Ctrl-C now mark the line as finishing, and the suggestion callback answers nothing. A repaint from replxx's cache never asks the callback; that case is Left, Right, Enter arriving in one burst over visible rows. For it, the reader clears the screen below each sent line. Without that clear, the stale row merged into the next prompt.
- **A row that reaches the last column** leaves the cursor in the terminal's deferred-wrap state, and replxx's row count is then one short. Every row is measured against the live width (read on each keystroke, so a resize is honoured). The measure is the larger of cells and codepoints, which are replxx's two counters, and the cut leaves room for the `…`. A line whose `@` sits near the edge gets no rows rather than rows reading only `@li…`.

**Verification.**
- `chat_completer_test` (19 cases):
  - goldens per context: bare `/`, `/mo`, `/model ` with a prefix and extra spaces, `/retriever `, `/rerank `, `/capture `, `@`, `@.`, `@src/`, a mid-line `@`, `me@li`, a lone `@` before a space, smart case, and quoting both typed and untyped;
  - the one-table rule both ways (every row parses, resolves, is offered and is in `/help`; every handler has a row);
  - `/help` at every width from 26 to 120;
  - the real lister on a temporary directory.
- `line_reader_test` (8 new cases): the protocol, the layout goldens, no row reaching the last column at any width from 4 to 90, the `N more` row, no room meaning no rows, and Tab's replacement.
- **`cli.chat_line_editing` drives the real binary** in a 44-column PTY, replayed through a screen model that remembers every column a row ever touched:
  - `/mo` draws described rows;
  - Tab completes `/model ` and then the backend (`switched to mock`);
  - `@li` lists a real folder and a real file, Tab picks the folder, and the message is saved as typed (`@library/`);
  - nothing under the prompt touches the last column while typing;
  - a burst over visible rows leaves none behind, and `/exit` sent in one burst keeps no suggestion on its line;
  - a piped chat writes no escape sequence and no prompt, and its `/help` lists `/retriever`, described.
  - **Mutation-checked:** without the clear below the line, the burst check fails; without the finishing flag, the `/exit` check fails; with rows allowed to reach the full width, the edge check and 910 unit assertions fail.

### 2026-09-29 — `attachments-documents` (backlog item 26d): documents, code and folders, attached to a chat

**Why.** Until now a chat could take an image on its first message and nothing else. A small local model with a 32K window cannot read a 300-page PDF or a repository by having it pasted in. It can when the document is indexed and the parts that bear on each question are handed to it, cited by page or line. Asked for by the user on 2026-09-25, with three calls of theirs: attachments kept with their chat and cached by file hash, helper models used automatically, and external converters on `PATH`.

**What was built**

- [x] **Attaching.**
  - `chat --attach <path>` (repeatable), and mid-chat `/attach <path|folder|glob>`, `/attachments` and `/detach <name>`, in chat's command table, so `/help`, completion and dispatch have them.
  - `/attach` completes paths as `@` does, and `/detach` completes what is attached.
  - `complete --attach`, and a machine-mode `{"type":"attach","path":…}` line whose outcome arrives as `notice` events.
  - A sent message's `@path` or `@"path with spaces"` attaches that path exactly as `/attach` would, the message kept as typed. A mention naming nothing stays text, with a dim note.
- [x] **Reading** (`agentloop/attachments`, one core for every surface).
  - Text and code are read as they are, a binary one refused. A PDF goes through `pdftotext` with its page breaks kept, and HTML through `fetch_url`'s reader, its links resolved against the file's own `file://` address.
  - Word, Excel and PowerPoint files are refused by name, and so are images, audio and video, each with its reason.
  - A folder is walked recursively, hidden entries left out and, inside a git repository, what git ignores (`git ls-files`).
  - A glob matches `*` and `?` within a name and `**` across folders.
  - Over 500 files or 50 MB asks on a terminal and is refused on a pipe.
- [x] **Indexed, always, into the chat's own store**, `attachments/<chat id>.db`, under a new private layout row, and deleted with the chat by `chats delete`.
  - A file's chunks are stored under its content, `sha256:<hex>`, each with its name, byte offsets, and line or page range in its metadata. `chunk_spans` gives the chunker's spans, and `PositionIndex` numbers them.
  - Embedded by the embedding model **only when one is named** and not billed per call; otherwise searched by its words, and said so. The chat model is never drafted in through the role's fallback.
  - Indexing runs on a worker thread while the user types, and settles before the next turn, as the title does. A turn that needs it waits with its progress on the status line, and Ctrl-C keeps what is ready.
- [x] **The hash cache.** Before anything is read or embedded, the other chats' indexes are searched for the same content under the same embedding model (or lexical beside lexical). A match is copied, vectors and all, and cited by this chat's name for it.
- [x] **Inlined when it fits.** An attachment whose text fits the budget's attachment share (26c), beside the ones already inlined, rides the user message it was attached with, whole. That is in what is **sent** only.
  - The transcript keeps the message as typed, and the session records the attachment by reference: path, sha256, reader, size, and the message it rides. That is the session's schema version 2.
  - The text is rebuilt exactly from the chunks' byte offsets.
  - The budget trims an inlined attachment last, or with the exchange it rode. Either way it is named, and retrieved from then on. So is every one when compaction folds the messages they rode.
- [x] **Retrieved every turn** from the chat's index, through the one retriever resolver, with inlined attachments left out.
  - Excerpts are labelled `ledger.pdf p. 187` or `budget.cpp:477–487`, adjacent chunks of one file merged without their overlap, and the model asked to cite the label.
  - A question naming code -- `fitting_prefix`, `parseConfig`, `Store::search`, `run()` -- is searched by its words for those names alone. Any other is searched by words and meaning together (a `hybrid` pin) when the index has vectors. `/retriever` overrides both.
  - The attachments go first; an `auto_rag` or `--rag` collection gets what they leave of the retrieval share (`share_used`), each reported on its own line.
  - The follow-up is restated once, by 26b's rewrite, for both.
- [x] **`check`** has an Attachments section: whether `pdftotext` and `git` are found (optional, so never a fault), and how many chat indexes there are and their size. The folder's mode is the filesystem check's, as a layout row.

**On real weights** (Qwen3.8-27B Q4_K_M at its 32K default, greedy; embeddinggemma-300M as the embedding model; `--verbose` lines):

- **A 300-page PDF** (generated, with real cross-references, one detail on page 187), attached with `/attach`. It was read and 612 chunks embedded in 5 seconds. Asked how many crates were in the Tromso warehouse, the 27B answered "4,812 crates of cloudberry jam (ledger.pdf p. 187)" from a 750-token prompt.
- **`summarize @ledger.pdf` in a second chat** attached it as `/attach` would, and copied it from the first chat's index in 0.7 seconds instead of embedding it again. The summary cited merged ranges (`pp. 82–83`).
- **Resumed**, the first chat attached and embedded nothing. It restated "which page mentions the cloudberry jam?" as a standalone query and answered "Page 187." `chats delete` removed its chat's index.
- **A 6 KB source file** was inlined whole. Both questions about it were answered right with no retrieval, the second reading 54 new tokens with 1,633 from the cache.
- **A folder of 30 source files** inside the repository: asked where `fitting_prefix` is defined, the 27B named `budget.cpp` from line 483, the definition, and `budget.h:181–187`, the declaration. Re-attached after two of its files changed, it copied 28 and read and embedded just those two.

**Found on the way, and settled.**
- **Meaning missed a name.** The folder question first went to vector search, which did not find the definition: an embedding captures meaning, and an exact name carries little. Hybrid search, tried next, dropped it too. Its rank fusion rewards a chunk middling in both lists over one strong in only one, and the definition was first by its name and nowhere by meaning. Searching the name alone, by its words, put the declaration first and the definition fourth. Hence the code-name rule above.
- **One run answered nothing.** The 27B spent its default 2,048-token budget reasoning and returned an empty answer. With `-n 8192` it answered. That is the older empty-answer problem, flagged separately during 26b, not this item's.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| `complete --attach` | A temporary store of its own, removed at exit, the hash cache still searched *(default taken)* | A one-shot has no chat to keep it with. |
| A large folder | Over 500 files or 50 MB asks on a terminal, refused on a pipe *(default taken)* | Attaching a home directory by accident. |
| `.gitignore` | Honoured inside a repository, through `git ls-files` *(default taken)* | The same `git` the git toolset uses. |
| Office files | Refused by name *(default taken)* | Each needs a converter decision of its own. |
| Retrieval | Up to the budget's retrieval share, labelled, adjacent chunks merged *(default taken)* | Contiguous text reads better than fragments. |
| How it is keyed | By content, `sha256:<hex>`, with name and range per chunk | The hash cache is one lookup per other chat, and a file attached twice is indexed once. |
| How an inlined one reaches the model | On the message it was attached with, in what is sent only; saved by reference, rebuilt from the index | The message stays as typed, and the text is never saved twice. |
| The embedder | Only the one named, never a billed one | The chat model standing in through the fallback would embed hundreds of chunks with a 27B; and nothing is vectorised through a metered embedder on Apogee's initiative. |
| Retrieval's order | The attachments first, then `auto_rag` with what they leave | The user's own documents come first. |
| A local HTML file's links | Resolved against its own `file://` address | The reader resolves against a web address only. |
| A question naming code | Searched by its words for those names; anything else, words and meaning together | Found on real weights, above. |

**Guardrails, each mutation-tested (68 mutants, all caught), run in three git worktrees against the whole unit suite.** 59 were caught on the first pass. The nine that survived were caught once tests were added for what they exposed: a `?` that matched a folder separator; cancellation ignored while embedding; the retrieval share not handed on to `auto_rag`; `complete --attach` inlining nothing; inlined attachments searched as well; the cost of those already inlined ignored; a dropped exchange or a compaction left unsaid; and a code name searched along with the rest of its question.
- **Finding and reading:** hidden entries kept, or kept from git's list; git's list never used; names cited absolute; `*` across folders, `**` needing a folder, `?` matching `/`; the size guard at its limit rather than past it; Office files or images read; a local HTML file's links left as they were; a PDF read as text.
- **Chunks and excerpts:** a span a byte short; line or page numbers counting the separator, or from zero; page breaks kept in the stored text; the last line past the end; excerpts never merged, their overlap repeated, unlabelled, or not best first.
- **The index and the hash cache:** content already held indexed again; a copy cited by the other chat's name; a copy across embedding models, or from a vector index into a lexical one; an embedding failure unsaid; another model's vectors mixed in; cancellation ignored.
- **Inlining and the budget:** an attachment exactly at the share refused; the inlined text never sent, never stripped, kept when its exchange is dropped, or its missing message unsaid; the loop not passing it on, or not collecting what was dropped; the cost of those already inlined ignored; a dropped or compacted one kept inline, or not said.
- **Retrieval:** inlined attachments searched too; the share not passed on to a collection, or not split; excerpts unlabelled; code names not recognised (snake case, camel case, calls), searched by meaning, or with the rest of the question; no hybrid pin when the index has vectors.
- **The embedder:** the chat model drafted in through the role's fallback; a billed embedder used.
- **The surfaces:** a large folder attached without asking; an attachment never anchored to its message, or never inlined; content another attachment still holds removed by `/detach`; an email address read as a mention, or a mention's trailing punctuation kept; a mention already attached, attached again; machine mode's `attach` line unknown; the session's `inline_at` not saved or not read; the index not removed with its chat; the layout row not private; `check` counting an index's side files as indexes.

**Not verified.**
- **Office files have no item.** Word, Excel and PowerPoint are refused by name, and nothing in the backlog converts them yet. Images, audio and video were [26e](#milestone-h--apogee-chat)'s, shipped the next day.
- **A PDF without `pdftotext`.** It is skipped with its reason, and the test for that runs only where `pdftotext` is missing. This machine has it, so that test skipped.
- **macOS only.** `git ls-files`, `pdftotext` and the private folder were not tried on the Linux or Windows builds.
- **A billed embedder** is refused by the provider's own flag. That was tested on a mock; no hosted embedder was tried.

### 2026-09-30 — `attachments-media` (backlog item 26e): images, audio and video, attached to a chat

**Why.** 26d let a chat hold documents. A picture could still reach a model only through `--image` on a chat's first message, and only a vision model could read it. Audio had no way in at all. Video was refused, although the pinned llama.cpp decodes it. Yet the common case on a laptop is a small text-only model, beside a helper that can see or hear and could turn either into text it reads. Asked for by the user on 2026-09-25, with two calls of theirs: helper models used automatically, and `ffmpeg` on `PATH`.

**What was built**

- [x] **The same `/attach`.** `--attach`, `/attach`, `@path`, `complete --attach` and a machine-mode `attach` line take an image, a recording or a video, and so does `--image` on `chat` and `complete`.
  - Anything nothing configured can read is refused by the one guard, `attachment_refusal`. Its message names the role to set, or that role's own model when it is set and cannot read the medium either.
  - `complete --image` still fails a one-shot whose picture is missing, is not an image, or that nothing can read.
- [x] **As it is, once.** A chat model that can read the medium is sent it with the next message, ahead of the text, in what is sent only:
  - an image, on a vision model;
  - a sound up to a minute, on a model whose projector hears;
  - a clip up to a minute, as its frames (one a second, at 640 pixels, with the time every five frames), on a local model with a vision projector, plus its sound for one that hears.
- [x] **As text, from then on.** Every medium is also read into text and indexed with the chat's documents, inlined when it fits and retrieved when not. That text stands in for the pixels and samples on every later turn, so a vision chat stops re-encoding its images every turn.
  - An image is described by the `vision` role: what it shows, then its text copied as written.
  - Audio is transcribed by the `transcription` role in 30-second windows, each line stamped `[m:ss–m:ss]`.
  - A video becomes a **timeline**: a frame every five seconds and at each scene change, at most 240, each one described, merged in time order with what was said. A frame identical to one already described keeps its line but is not described again.
  - With no helper set, the chat model reads its own media when it can *(default taken)*. A helper is used automatically, and the status line names the model reading each file and how long it has been at it.
- [x] **Found by moment.** A transcript's or timeline's chunks carry the times their lines cover, and are cited by them (`standup.mp4 6:30–7:05`). When a question names a moment (`at 4:30`), the chunks covering it are looked up by time and go first, whatever the search found.
- [x] **ffmpeg, run by Apogee.** `platform/ffmpeg` runs `ffprobe` and `ffmpeg` as Apogee's own children, with a deadline, an output cap and Ctrl-C.
  - Their stderr is a pipe Apogee drains, never the terminal.
  - Frames go to a private folder beside the chat's index, removed after each file.
  - mtmd's own video helper is not used: it spawns ffmpeg from code whose output Apogee does not control.
  - Without ffmpeg, audio and video are refused by name.
- [x] **The local backend reads it.** A multimodal request carries images, WAV audio and frames to mtmd (`MediaInput`).
  - Each marker sits where its part sat in its message. They had all been stacked at the top of the prompt.
  - Frames are marked mergeable, for Qwen-VL's temporal merge.
  - Audio on a projector without an audio encoder is refused.
  - `accepts_video` answers from the projector header, `audio_sample_rate` once the model is loaded, and `--verbose` says what a turn encoded and how long it took.
- [x] **Capabilities, asked.** `Harness::can_read(model, medium)` answers over `accepts_images`, `accepts_audio` and the new `VideoCapable`. The IR gains an `InputAudio` part (OpenAI's `input_audio` shape) and a `video_frame` flag on image parts.
- [x] **The budget sizes it.** An image is allowed 1,024 tokens, a frame 256, a second of audio 25. The allowance is added to the estimate, the byte ceiling and the exact count, all of which see only words.
- [x] **`check`** says whether `ffmpeg` is installed, and which models read images, audio and video for the default chat.

**On real weights** (Q4_K_M weights; `--verbose` lines; Qwen3-VL-8B as the `vision` role, and Gemma 4 12B, whose projector has an audio encoder, as the `transcription` role):

- **A text-only Llama 3.1 8B**, given an invoice image with `complete --image`: Qwen3-VL-8B described it, and the 8B named the customer, the total and the due date. Ten seconds in all.
- **A vision chat on Qwen3-VL-8B** with `--image`: the first turn read the image as it is; the second read 159 tokens of text, its description included, with no image encoded.
- **A 16-second voice note** (recorded with `say`), attached to the 8B, was transcribed by Gemma 4 12B word for word. Asked when the delivery now was and what had to be paid first, the 8B answered both. Attached to a chat on Gemma 4 12B itself, it was heard as it is: 439 positions with 1 sound encoded, in 1.2 s.
- **A 25-second clip of four slides** on Qwen3-VL-8B was read as its frames (25 frames, 405 positions, 8.8 s), and the steps and their times came back right. The next turn read its timeline.
- **A 10-minute screen recording with narration**: nine screens, the deadline spoken at 6:30 over a calendar. Attached to a chat on Qwen3.8-27B, its timeline was built in the background into 131 chunks: 121 frames described by Qwen3-VL-8B (about 9 s each) and 20 windows transcribed by Gemma 4 12B (about 4 s each). That took 23 minutes, two answers included.
  - Asked what was on screen when they mentioned the deadline, the 27B answered "At 6:30, the screen showed a November calendar view", with the Friday 14 November proposal due at 17:00.
  - Asked what was typed into the terminal, it quoted the command and its output, at 5:30–5:55.
  - **Re-run with identical frames reused**, only 20 of the 121 frames needed describing: nine screens, plus the frames where the encoder's output differed. The frames took 2 minutes instead of 18, the whole run 5.5 minutes, and the answer was the same, now also listing the dry-dock inspection the day before.

**Found on the way, and settled.**
- **Gemma 4 answers nothing about audio with its thinking off.** Transcription with the reasoning skipped, as every helper chore runs, came back empty, and the note was indexed as "nothing was said". With its reasoning on, the model transcribed it word for word. A media request now asks again with the reasoning on when a reply comes back empty, and keeps it on for the rest of that file.
- **That empty transcript was copied to the next chat** by the hash cache, as if it were true. A recording none of whose windows could be transcribed is now refused, not indexed as silence.
- **Frame descriptions lost their colours.** With "This is one frame of a video, at 0:10" on its own line after the prompt, Qwen3-VL-8B copied only the text, and a question about a slide's colour was answered wrong. With that line opening the prompt instead, it described the screen.
- **"No vision model is set" was said when one was.** The configured vision model's Q4 entry has no `mmproj_path`. The refusal now names the role's model when it is set and cannot read the medium either.
- **A `.jpg` slipped past the refusal.** Read on its own as a file name, `.jpg` is a hidden file with no extension. The test that pinned the refusal found it.
- **The image path stacked every marker at the top of the prompt**, before the whole rendered conversation. A clip's frames with their times between them, or a sound on the third message, need each marker where its part sits, which is llama-server's way.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| A clip read natively | Up to 60 s; longer, its timeline *(default taken)* | A minute of frames at mtmd's default rate is already thousands of tokens for an 8B model. |
| A timeline's frames | One every 5 s and at each scene change, at most 240, thinned evenly *(default taken)* | Enough for a screen recording; the cap bounds the helper's time. At most 960 are extracted before thinning. |
| An image attached again | Read again as it is, by a chat model that can see *(default taken)* | The way back to the pixels when the description missed something. A model that cannot see reads the same description, stored once. |
| No helper set | The chat model reads its own media when it can *(default taken)* | One model's view is better than none, and it is already loaded. |
| Audio read natively | Up to a minute too, as video | The same bound, for the same reason. |
| A native clip | 1 frame a second at 640 px, the time every five frames, its sound for a model that hears | 60 frames of Qwen3-VL fit a 32K window; mtmd's own default is 4 a second. |
| `accepts_video` | From the projector header (a vision encoder), not `mtmd_helper_support_video` | A clip reaches mtmd as frames Apogee's own runner extracted. The helper's flag says only whether mtmd's own ffmpeg decoding was built, which Apogee does not use. |
| Audio's sample rate | The model's own once it is loaded; 16 kHz before, which mtmd converts | The rate is fixed per projector inside mtmd and known only after a load. |
| When media goes as it is | Once, with the message it was attached with; its text from then on | The point of the item: a vision chat stops re-encoding its images every turn. |
| `--image` | An attachment, on `chat` and `complete` alike | Otherwise `--attach` would be described for a text-only model while `--image` was refused, which is a parity bug. |
| Many billed descriptions | More than 12 ask on a terminal, refused on a pipe | The spend rule's spirit: nothing is spent at scale on Apogee's initiative. One image is one call. |
| A frame identical to one described | Keeps its line, is not described again | A slide left up costs one description, and a moment there is still found. |
| A moment a question names | Looked up by the chunks' times, first | Searching "4:30" by its digits finds every line with a 4 or a 30. |

**Guardrails, each mutation-tested (77 mutants, all caught), run in three git worktrees against the whole unit suite.** 74 were caught on the first pass. The three that survived were caught once their tests were fixed:
- A native clip left uncapped: the test found the sound's `-t 60` rather than the frames'.
- Twelve billed descriptions asked about: the test changed its hooks after they had been copied.
- `check` never falling back to the chat model: only a role set to a model that cannot read reaches that fallback.

What the mutants covered:
- **The ffmpeg runner:**
  - a span's start not added back to its frames' times, or a span read from the start;
  - the output cap, the stop or the deadline ignored, and a failure left unsaid;
  - `-fps_mode` not asked for, and a progress line's `pts_time` taken for a frame's;
  - a WAV's rate field wrong, or an odd byte kept;
  - an audio stream missed, and audio decoded at a fixed rate.
- **Who reads what:**
  - the role's model passed over, no fallback to the chat model, nothing read natively, and a video's sound or frames never read;
  - in the harness: video assumed of a provider that declares nothing, video read as images, and a rate asked of a model that cannot hear.
- **Reading media:**
  - no retry with the reasoning on, or a retry that still skips it;
  - transcription at a fixed rate, and a recording never transcribed indexed as silence;
  - what was said sorted after the screen at the same moment;
  - 240 frames not thinned, an identical frame described again, frames sparser, or no scene changes;
  - the frame's context put after the prompt;
  - hours dropped from a clock, a time read inside a longer number, and a span dated from its first byte rather than its line;
  - the scratch folder kept.
- **What is read as it is:**
  - a clip's frames unmarked, without their times, without its sound, or uncapped;
  - never built, or built for a recording over a minute;
  - sent every turn, the text sent beside the image on its first turn, or not kept for the next message;
  - a dropped one left unsaid.
- **The index and retrieval:**
  - chunks without times, labels without them, every chunk taken for a moment, and an excluded one kept;
  - media read as text, a reader's notes dropped, and `.jpg` read as a file name;
  - moments ignored, and a moment's score shown in place of the search's.
- **The budget:** media left out of the estimate, a frame sized as an image, audio counted as free, and native parts dropped.
- **The local backend:** frames set apart, the frame flag lost, audio unread, audio sent to a projector that cannot hear, a remote image decoded, the media left unreported, and a rate claimed before the load.
- **The surfaces:**
  - the refusal skipped, helpers ignored by it, and the role's model unnamed;
  - billed descriptions not asked about, or asked about at twelve;
  - `check` saying any chat model reads clips, ffmpeg always found, and no fallback to the chat model;
  - `complete --image` unchecked, and `chat --image` ignored;
  - the IR's frame flag unread or unwritten, and audio parsed as text.

The two lines in `llama_real.cpp` (WAV decoding and mergeable frames) and the header-based `accepts_video` are not in the count: the worktrees build without llama.cpp, where neither can run. They were verified on real weights above.

**Not verified.**
- **macOS only.** On Windows, child processes are not supported yet, so audio and video fail there with the reason. Linux was not tried.
- **Cloud helpers.** Only local models described and transcribed. A cloud vision role should work, since cloud models read images, but none was tried. No cloud backend is sent audio, and none is sent a clip as frames.
- **A real screen recording.** The recording above was generated: static slides and a synthetic voice. A real one has a moving cursor and changing content, so fewer of its frames are identical and fewer descriptions are saved.
- **HEIC and TIFF** are converted to JPEG through ffmpeg, but no real file of either was tried.
- **Uploads over `serve`** are out of scope. A served request's `input_audio` part now parses as audio, though, and a local backend that hears would read it.

### 2026-10-04 — `recall-across-chats` (backlog item 26l): what earlier chats established, recalled

**Why.** A small model has no memory beyond its window: whatever a user established last week -- the database, the conventions, a decision -- they had to say again. The knowledge layer keeps what is captured on purpose; recall covers everything else, the way `auto_rag` covers a document collection.

**What was built**

- [x] **A summary per finished chat** (`agentloop/recall`). A chat of two turns or more is summarised once at a clean exit: what was asked, what was decided, the facts and preferences stated, the files involved, in at most 120 words. The request is greedy, a side request, with thinking off. The summary goes into a private index under `memory/`, one source per chat id: `0600`, its folder `0700`, a layout row of its own.
- [x] **Never billed.** The utility model summarises when one is named; else the chat's own backend, only when it costs nothing per call; else the exit says why not. Summaries get vectors only from a free embedder.
- [x] **Recalled per turn, transient and said.** After a chat's attachments and `auto_rag`, a turn recalls at most three items within what the retrieval share has left:
  - past chats' summaries, through `retrieve_for_turn`, introduced as notes on earlier conversations and never the asking chat's own;
  - then decisions from the knowledge collection, unless `auto_rag` searched it.

  The terminal says `[memory] 1 past chat`; machine mode sends a `memory` event (`Reporter::on_recall`). Nothing recalled ever enters the transcript.
- [x] **Controllable**:
  - `--no-recall` for a run, `/recall off` for a session;
  - `/private`, which keeps a chat from ever being summarised and takes back a summary already kept;
  - `memory.recall: false` for everything.

  `chats delete` forgets the chat's summary.
- [x] **An open chat is never summarised.** A chat that becomes due is marked under `memory/pending/` with the process that has it open. A process that died leaves its marker, and the next `chat` start catches up on at most three. A chat whose process still runs is left alone. A resumed chat is summarised again only when this run added to it.
- [x] **Never on `serve`, by construction.** The layering check refuses any include of the recall code from `httpserver/` or `operations/`. Recall is off in `complete` and agents too: one-shots stay reproducible from their inputs.
- [x] **`check`** counts the summaries and fails when others can read the index.

**On real weights** (the families, each summarising its own chat -- no utility model set -- with the local Embedding-Gemma for search; separate processes, as a chat ending and a new one starting are):

| | Qwen3-VL-8B | Gemma 4 12B | gpt-oss-20b | Llama 3.1 8B |
|---|---|---|---|---|
| "Our project uses Postgres 16…", two turns | summarised by itself | summarised | summarised | summarised |
| a new chat: "Which database version should this migration target?" | `[memory] 1 past chat`; **PostgreSQL 16** | `[memory] 1 past chat`; **"you are currently using PostgreSQL 16"** | `[memory] 1 past chat`; knew the project is on 16, and advised moving to 17 | `[memory] 1 past chat`; "based on the earlier conversation about Postgres 16 … a suitable target" |
| `/recall off` | asked for context | asked for context | answered in general | asked for context |
| `serve` | asked for context, as with recall off | asked for context | asked which database | asked for context |
| after `chats delete` | asked for context | asked for context | answered in general | asked for context |

**Tests**: 20 new cases -- the recall module (the request, the summary, the chunk, the counts, the index, retrieval with its header and exclusion); recall end to end in `chat` over two mock backends (summarised, recalled transient, stopped three ways, private, single-turn, `complete`, catch-up and an open chat, self-recall, continued and unchanged resumes, a recorded decision, the billed summariser); the config, the session, the completer, both reporters and `check`.

**Guardrails, each mutation-tested (29 mutants, all caught -- one only once a test wrote a `memory:` section without `recall`), in a separate git worktree:** the summariser's request (side request, thinking, budget, roles), a blank summary, the chunk's date, the counts' wording, a billed embedder, the index's privacy and removal, what is due (private, turns), a billed summariser, self-recall, decisions, an unchanged chat, an open chat, `/private`, the marker, the `[memory]` line, `/recall`, `--no-recall`, the exit summary, `chats delete`, `check`, the machine event, the config default, the session's flag.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| Where recall runs | `chat` only *(confirmed by the user)* | One-shots and agent runs stay reproducible from their inputs; `serve` never, by construction. |
| What is summarised | Chats of two turns or more *(confirmed by the user)* | A single question rarely establishes anything. |
| How much a turn recalls | At most 3 items, inside the retrieval share *(confirmed by the user)* | Recall supports the question, never crowds it. |
| A continued chat | Summarised again *(confirmed by the user)*, and only then | The summary describes the chat as it stands. |
| The summariser | The utility model, else the chat's own only when unbilled *(for veto)* | A summary is Apogee's idea, never billed for it. |
| When | At a clean exit, on a line of its own; catch-up at the next start, three at most *(for veto)* | A thread the exiting process would kill loses the summary. Old chats are never summarised wholesale. |
| The controls | `--no-recall` and `/recall off` stop recalling; `/private` stops summarising *(for veto)* | Each does what its name says. |
| Decisions | From the knowledge collection after past chats, not twice with `auto_rag` *(for veto)* | The deliberate record beside the automatic one. |
| Reporting | `Reporter::on_recall`: a `[memory]` line, a `memory` event | One event every surface adapts; the server's adapter never sees it. |
| The acceptance models | The families *(the standing rule)* | |

### 2026-10-04 — `attachment-map-card` (backlog item 26q): a folder attached with a map of itself

**Why.** A folder attach gave the model excerpts and no map. In the attachment-representation spike (2026-10-03), a model given this whole repository and asked how its RAG works invented `lib/src/core/`, failed, guessed again, and cycled until the turn died. Excerpts answer "what does this code say". Nothing answered "where is anything".

**What was built**

- [x] **The card** (`agentloop/attachments`, `render_map_card`), built from the names the attach already found: no disk read and no model call.
  - **What it says:** the prefix every attached path starts with; the directories two levels deep, each with the files under it; the files at the root; the extension mix; and the totals the user was told.
  - **Bounded:** at most 30 tree lines (`kMapCardDepth`, `kMapCardLines`, `MapCardCaps`), eight extensions named and the rest counted as `other`.
  - **Folding:** every top directory gets its line before any is opened. What does not fit folds into a counted line (`... 21 more directories, 630 files`).
  - The same names always render the same bytes.
- [x] **It rides like an inlined file** (`cli/chat_attachments`):
  - A folder or glob of two files or more is given a card when the card fits the attachment share beside what is inlined already. It is counted in that share, so a file that fits alone is retrieved beside a big map.
  - Where the window is unknown nothing is inlined, so nothing is mapped either.
  - The attach line says which: `attached proj: 3 files, 3 chunks, with a map of its folders -- …`, or `no map: …` with why.
  - The card is anchored on the next user message (`Attachment::map_at`, saved as `map_at`) and sent there on every request, rebuilt from the attachment's names and the index's chunk count (`AttachmentIndex::chunks_of`). A resumed chat sends the same bytes, and the saved message stays as typed.
  - It is sent after its attachment's own text on that message, so the model reads the map first and a trim takes the text before the map.
  - A trimmed map is dropped from then on, and said.
  - After compaction the map rides the next message, once.
  - `complete` and machine mode get it through the same class.
- [x] **Two attachments on one message, trimmed one at a time** (`agentloop/budget`). Before, taking one off restored the message as it had been before *that* one, so a second strip could put the first back. That was rare while one message rarely carried two attachments; the map makes it common. Each attachment on a message now remembers the message as it was before the first of them, and a strip rebuilds the message from the rest.
- [x] **Tests**:
  - **Goldens:** the card for a project's names, byte-exact, including the folds at four and two lines, one level deep, and names with no common prefix.
  - **Bounds:** a deep tree (50 × 10), a wide one sharing its inner lines, a flat thousand files, a glob, and the extension fold.
  - **A real folder walk** feeding the card.
  - **In chat:** the attach line; the card after its text on the message; none for one file; a glob rooted where its files start; the same bytes when resumed; after compaction on the next message once; a trimmed map said and gone; the share counted; none on an unknown window.
  - **On the surfaces:** `complete --attach` sending it on the prompt; a chat's saved message as typed.
  - **The budget's two-attachment case**, and `map_at` round-tripped.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| Depth and size | 2 levels, 30 lines, named constants *(default, confirmed)* | A map that scrolls is a context bomb. |
| The mix | Extension counts, no content sniffing *(default, confirmed)* | Instant and deterministic. |
| Kept how | **Anchored like an inlined file and rebuilt each request, never saved as text** *(recorded 2026-10-03: "persisted history, not a transient prefix")* | It rides the same message every turn, so a cached prompt holds, and the transcript keeps the message as typed, the rule inlined files already follow. |
| The root | **The directories every attached name starts with** *(group run, flagged for veto)* | It is the prefix a citation needs, always true of every name. A glob is named as spelled and rooted where its files start. |
| What gets a card | **Two files or more** *(group run)* | One file is its own map, even when it is a folder's only file. |
| An unknown window | **No card, and said** *(group run)* | The item says "never unconditionally", and an unknown window never reads as room for anything inlined. |
| Its place on the message | **After its text: read first, trimmed last** *(group run)* | It is the smallest and most useful part of an attachment. |
| A trimmed map | **Dropped from then on, and said** *(group run)* | As an inlined file is. |
| The chunk total | **Counted from the index** *(group run)* | The same on resume, so the card's bytes never change under a cached prompt. |
| The strip fix | **Built here** *(group run)* | The card makes two attachments on one message the normal case, and without the fix a trim could quietly bring one back. |

**Verified on real weights.** Each family ran in one `chat --tools` process, with `lib/src/cli/source` (467 files) attached and indexed by Embedding-Gemma. Each was asked "What are the top-level directories of the attached source tree, and what does each one hold? Give each directory's path, and two of the directories inside each.", on the binary before this item and after it:

| Family | Before | After |
|---|---|---|
| Qwen3-VL-8B | `business`, `agent`, `agentloop` from the excerpts, and no more | `business/` (with `agentloop/`, 40 files, and `knowledge/`, 12, both right), `data/`, … |
| Llama 3.1 8B | Answered with raw tool calls on invented paths (`lib/src/business`, `lib/src/tools`) | `business`, `data`, `infrastructure`, `presentation`: all four, and only those |
| Gemma 4 12B | Described a different tree (`src/cli`, with `assets/` and `build/`) | `business/` (`agent/`, `agentloop/`), `data/` (`backends/`, `contracts/`), … |
| gpt-oss 20B | 21 paths cited, 12 of them invented (`completions/bash/`, `ApogeeDependencies.cmake`) | 4 paths cited, all real |

A retrieval question ("where is the logic that decides which retriever a turn uses?") was asked first: every family found `agentloop/retriever.cpp` with or without the card. Retrieval answers *what*; the card is for *where*.

**Guardrails, each mutation-tested (18 mutants, all caught).**
- **The card:** deeper than the cap; no fold line; top directories not reserved; no common prefix; every extension named; the root's files unsaid.
- **The budget:** a strip dropping the others; the base taken as the message stands.
- **In chat:** one file mapped; the map costing nothing; mapped past the share; the map before its text; compaction forgetting it; the map following every message; a trimmed map kept; no chunks counted.
- **The session:** `map_at` not written; `map_at` not read.

**Not verified.**
- **Cloud backends** get the card through the same request assembly; none was run.
- **The answers' directory names** were checked by hand against the tree. The script flagged only slash-separated paths that do not exist.

## Milestone I — The full cloud set

**Goal.** Widen cloud coverage from one vendor to three, and in doing so settle the question the `LLMProvider` seam was built to answer: is a backend really just a translator? The answer is a cross-provider conformance table in which the loop, the tools, and the assertions are shared and only the wire fixture differs.

### 2026-08-26 — OpenAI and Google Gemini backends

**What was built**

- [x] **OpenAI over the Responses API** (`source/backends/openai_wire.h/.cpp`, `source/backends/openai.h/.cpp`) — POST `/v1/responses` with a bearer token. `input_items()` returns an *array* per IR message, because an assistant turn with tool calls becomes a message item plus one top-level `function_call` item per call, and a tool result is a `function_call_output` item rather than a message with a role. The system prompt becomes `instructions`, and tools are flat (`{type, name, description, parameters}`), not nested under a `function` key.
- [x] **Google Gemini over `generateContent`** (`source/backends/google_wire.h/.cpp`, `source/backends/google.h/.cpp`) — the model rides the URL path (`/v1beta/models/<model>:streamGenerateContent?alt=sse`) and the key rides the `x-goog-api-key` header, never the query string. The assistant role is `model`; a tool result is a `functionResponse` part inside a **user** turn, matched by name rather than by id.
- [x] **Both registered as ordinary config types** (`source/backends/factory.cpp`) — `apogee config add-backend <name> --type openai|google` and `-m <name>` are all it takes. No surface, command, or loop path knows a vendor by name.
- [x] **Thinking through the existing seam** — OpenAI's `response.reasoning_summary_text.delta` and Gemini's `thought`-flagged parts both reach `ThinkingSink`, so the thinking display works identically across all three without learning a third dialect.
- [x] **The cross-provider conformance table** (`tests/agentloop/conformance_test.cpp`) — the regression net the item names as its guardrail. The *same* two scripted turns, recorded in four dialects (mock/anthropic/openai/google), drive the same loop against the same tool registry: identical answer, identical iteration count, identical history shape, identical tool-call linkage, exact usage from the three real providers, estimate fallback for the mock, uniform `ask_user` advertisement.
- [x] **43 new tests** (398 total).

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| Split per provider? | **No** | The item offered the split. Every shared component it would have duplicated already existed (HTTP client, SSE parser, retry, the IR); each provider reduces to a wire translator plus a thin provider class. The conformance table is also far easier to write with both dialects in hand than to write once and extend. |
| OpenAI surface | **Responses API**, confirmed at build time | The stated default, and the item asked for confirmation rather than assumption. Reasoning summaries and the server-side `web_search` tool exist *only* there — Chat Completions exposes no reasoning summary at all. |
| Gemini auth | **API key only** for v0.1.0 | The stated default. Vertex-style credentials are a separate auth story and nothing in v0.1.0 needs them. |
| Thinking knob | One `thinking_budget_tokens` per provider's options | The stated default. It maps to Anthropic `budget_tokens`, OpenAI `reasoning.effort` (via a token→band mapping), Gemini `thinkingConfig.thinkingBudget`. The **IR carries no thinking field**, so the loop and the surfaces never learn that vendors spell it three ways. |

**Notes.** Gemini's translator has one check that carries more weight than its size suggests: `part.value("thought", false)`. Unlike the other two vendors, Gemini streams reasoning in the *same* `parts[]` array as the answer, so the flag is the only thing separating the model's private thinking from the text the user is shown. Dropping it does not fail loudly — it silently prints the reasoning as the answer. `a thought part never reaches the answer` pins it.

The retry contract from Milestone D pays off here for free: both providers issue through the shared `HttpClient`, so a 429 is retried without either translator containing the word "retry". `a 429 is retried` is asserted per provider anyway, because "it's shared" is a claim about today's wiring.

One stale test surfaced rather than one bug: `an unimplemented backend type names itself in the reason` still asserted that OpenAI and Google return "has not landed yet". They now land, so the assertion narrowed to llamacpp and grew two companions — every cloud type builds when it has a key, and a keyless one names **its own** environment variable (telling an OpenAI user to set `ANTHROPIC_API_KEY` would be worse than saying nothing).

The `/model`-carries-history claim was verified rather than assumed. `/model` only assigns `session.backend` — the claim rests entirely on every translator rendering a history it did not produce, which is exactly where the three dialects diverge most. `a mid-session /model switch carries the whole history` builds one transcript containing a tool call and its result and requires all three translators to render every turn of it. Mutation-tested: dropping the `function_call_output` branch from the OpenAI translator turns it red. The first draft of that test did *not* bite — it asserted on the tool result `"42"`, which also matched the assistant's "The answer is 42". The result value is now a token that appears nowhere else.

**Standing caveat, unchanged.** The live-API half of these acceptance criteria is unverified for all three vendors — every test here runs against recorded fixtures through `FakeTransport`. Confirming real traffic needs the user's keys.

## Milestone J — Local inference

**Goal.** Link llama.cpp into the process and let the KV cache simply *stay alive* between turns. This is the largest single simplification in the design: out of process, keeping KV state across turns can take an entire on-disk prompt-cache apparatus — cache files, fingerprinting, an M-RoPE replay self-heal, rules about what may never be written into a cache — whose only job is to move KV state between processes that cannot share memory. In-process, all of it collapses into arithmetic over a token prefix.

### 2026-08-31 — `LlamaCppProvider`, KV sessions, and the capability probes

**What was built**

- [x] **The runtime seam** (`source/backends/llama_runtime.h`, `llama_real.cpp`) — the slice of llama.cpp the provider needs, behind an interface. `llama_real.cpp` is one `#if`: the real C-API implementation when `APOGEE_ENABLE_LLAMA=ON`, and a "not built in" answer otherwise. Every raw handle is wrapped in a `unique_ptr` with a custom deleter at that boundary and never escapes it.
- [x] **`LlamaCppProvider`** (`source/backends/llamacpp.h/.cpp`) — model lifecycle, one live context per conversation, a throwaway context per side request, streaming with cancellation between tokens, exact usage on both sides, and configurable idle unload.
- [x] **KV reuse as prefix arithmetic** (`source/backends/llamacpp_tokens.h/.cpp`) — the cache's entire decision is the longest common token prefix between what is decoded and what the next turn sends. That is also what makes it **self-correcting**: a compaction, a `/model` switch, or a resumed session each simply yield a shorter prefix, with no special case anywhere.
- [x] **Chat templates** (`source/backends/chat_template.h/.cpp`) — ChatML, Llama 3, and Mistral, with a deliberately narrow name-matching registry and ChatML as the documented fallback. **A GGUF's own template always wins**, applied through llama.cpp.
- [x] **Two capabilities the harness was missing** (`harness/provider.h`) — `TokenCounting` and `VisionCapable`, both discovered by the Harness. `commands/complete.cpp`'s backend-type switch for image support is **gone**, and `commands/chat.cpp` now shows exact context usage instead of "(estimated)" whenever the backend owns a tokenizer.
- [x] **`idle_unload_seconds`** on a backend entry, and a rewritten local-inference block in the config template — the old one claimed nothing stays resident between turns, which in-process linking makes exactly wrong.
- [x] **34 new tests** (434 total), plus the no-listen symbol scan widened to the linked executable.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| Crash model | **In-process only**, no spawn-isolation mode *(user call)* | A second generation path would re-introduce the apparatus the divergence exists to delete, and would have to be kept at parity forever. The exposure is narrower than "a crash kills the binary": a load failure returns a clear error, so it is an abort *during generation*, bounded to one in-flight turn by per-turn session save. |
| Local vision | **Deferred to multimodal-vision** *(user call)* | An mmproj path is a per-family model property. That item was split four ways on 2026-09-06; local vision is now its own document. `VisionCapable` still landed here, so the type switch died now rather than later. |
| Linking / GPU | Static, Metal on macOS; llama.cpp stays behind `APOGEE_ENABLE_LLAMA` | The stated default. The merge-blocking target must not pay for kernel compilation. |
| KV across restarts | Not persisted; a resumed session re-ingests | The stated default. `llama_state_save_file` is the recorded later upgrade. |
| Pin cadence | Manual, deliberate bumps | The stated default. |
| In-text tool calls | **Capability deliberately not declared** | A local model does put tool calls in its text — but nothing parses them yet, and the dialects belong to item 14. Claiming the capability would advertise a parser that does not exist. |

**Notes — the seam earned itself twice, and then real hardware earned its keep.**

The provider is tested against a *scripted* runtime, because the merge-blocking target has no llama.cpp in it: a test needing the real thing would not run where it matters, and the acceptance criteria here are counting claims ("turn two decoded only the new tokens") that a fake answers exactly and hermetically. Every guardrail was mutation-tested — disabling KV reuse turns the reuse test red with the exact `8 < 8` signature its comment predicts, and running a side request on the session context fails the isolation test.

**One guardrail was removed for failing that bar.** An explicit "forget the transient region" step was written to satisfy the constraint that RAG content must never contaminate the reusable prefix — and it passed with the code disabled. It was redundant: the prefix match is token-for-token, so a turn can only reuse a cached token its own prompt contains, and the moment the injected block stops being sent the prefix ends there. The code came out and the test was rewritten to assert the real mechanism, which does bite. Belt-and-braces code with a test that cannot fail is worse than neither.

**Then a real GGUF found two bugs the fake could not.** Running an actual model (a 260K TinyStories GGUF) through `apogee chat` crashed on turn two: `decode: failed to find a memory slot for batch of size 1`. The cause was `chat`'s background title request, which carries no `max_tokens` of its own — it ran to the provider default against a model that never emits end-of-generation, filled the KV cache, and **threw**, taking the turn down. Generation now stops at the context wall and reports truncation. The same session exposed the second: submitting a whole prompt in one `llama_decode` works right up until someone pastes a long file, so prompts are now chunked to the context's own batch limit. A first attempt at that — setting `n_batch = n_ctx` — was itself wrong, because llama.cpp reserves batch-sized headroom inside the cache and leaves no slot for the first generated token. **No scripted runtime models an allocator**; only real hardware was going to say so.

Two smaller corrections came from the same run: llama.cpp's own logging is now routed through `llama_log_set` and silenced below WARN, because ~20 lines of Metal device capabilities were burying the error message the acceptance criteria ask to be clear; and a context now defaults to the model's own trained length rather than a fixed 4096, which was wrong in both directions.

**A routing hole closed on the way past.** `-m local` on a configured-but-unbuildable backend fell through to the default and answered from it — a real answer from a model the user did not choose. The typo guard from Milestone I only covered names that were not configured at all. `complete` now reports that backend's own reason.

**Verified end to end against a real model:** load, chat-template rendering, tokenization, KV reuse across turns, side-request isolation on its own context, graceful truncation, and session save — plus both no-listen checks passing with llama.cpp linked into the binary, which is the configuration the invariant is actually about.

**Not verified:** performance. The KV cache is asserted by token counts, never by wall-clock, and no large model was run.

### 2026-09-25 — `local-tool-calling` (backlog item 25b): tools through each model's own template

**The gap.** The local-tools spike (2026-09-25) found that no local model had ever been shown a tool. `LlamaCppProvider` rendered its prompt with `llama_chat_apply_template`, llama.cpp's legacy template function, which has no tools input, so `request.tools` was dropped. Asked to use `read_file`, Qwen3.8-27B replied that it had no such tool. The one local call path was Milestone P's gpt-oss parser, and even gpt-oss was never shown the list. The spike measured the fix outside Apogee: llama.cpp's own chat layer (`common/chat.h`, which llama-server runs) passed six tasks out of six on Qwen3.8-27B and on Qwen3-VL-8B.

**What was built**

- [x] **`backends/llama_chat.h/.cpp`**: llama.cpp's chat layer behind an interface of standard types. It loads a model's templates once per load and renders messages and tools through the template. It converts the grammar triggers the way llama-server does and resolves the preserved special tokens. Its parser returns content, reasoning and calls, and gives `false` where llama.cpp would throw.
  - It lives in **a library of its own**, `apogee_llama_chat`, because `llama-common` carries its own nlohmann/json at another version (3.12 against Apogee's pinned 3.11.3), and the two share an include guard. This is the one translation unit that sees those headers.
  - `common`'s own logger, which writes straight to stderr, is silenced once.
- [x] **The seam** (`llama_runtime.h`).
  - `LlamaModel::render_chat` returns a `ChatRendering`: the prompt, a `SamplingGrammar`, the preserved tokens, the stop strings, and a `ReplyReader`.
  - `special_token_text` renders a special token as text: a call's opener (`<tool_call>` on Qwen) is a special token, and without this the reader would see the call as bare JSON.
  - `LlamaContext::set_grammar` installs the grammar for one generation.
  - The defaults refuse, so a runtime without the layer falls back honestly.
- [x] **The real runtime** (`llama_real.cpp`) builds a fresh sampler chain per generation (`make_sampler`): the lazy tool grammar when there is one, then greedy selection. It also **no longer accepts a token twice**: `llama_sampler_sample` already accepts, and the extra `llama_sampler_accept` had been harmless only because greedy selection keeps no state. A grammar does keep state, so it would have advanced twice per token.
- [x] **The provider** (`llamacpp`). Every request renders through one function, `render_request`, whether it is a text turn, an image turn (tools included, per the default) or the token count (which now counts the tool definitions).
  - With the model's own template, `TemplateReply` re-reads the whole reply after every token, as llama-server does. It emits only the difference: reasoning to the thinking sink, content to the answer, and a call held back entirely.
  - Stop strings end the reply. A call the format left without an id gets a nine-character one, the shape Mistral's template insists on.
  - The grammar is set for every generation and cleared on a request without tools, because the session's context outlives any one request.
  - **The fallback** (the GGUF ships no template, or its template cannot render the request) is the path from before 25b: the name-matched registry and the profile filters. `common` would otherwise have rendered a template-less model with a generic ChatML template, and a guessed format is worse than the registry. When a request carried tools, the fallback says so in one line.
- [x] **A notice channel.** `StatusEvent::Type::Notice`, forwarded by the loop to a new `Reporter::on_notice`. The terminal shows it as a lasting `[warn]` line, machine mode as a `notice` event, and `serve` as a `notice` meta-frame. It is used for the dropped-tools line, a grammar that did not compile (the turn then runs unconstrained), and a reply that did not match its format (kept as text, with no call run).
- [x] **The repeated-call guard** (`agentloop/loop.cpp`). The same tool with the same arguments, compared as JSON, a third time in one turn gets a tool result saying it already has that answer (`kRepeatedCallLimit`).
- [x] **The link**. `llama-common` is linked into `apogee_llama_chat` with its vendored httplib cut from its link interface (`third_party/CMakeLists.txt`), because the downloader that needs httplib is never reached. That is now structural: if a repin made the chat code need httplib, the link would fail by name. The no-listen check's link-graph walk was made exact to match: for a static library it follows `INTERFACE_LINK_LIBRARIES`, which is what reaches the final link, because its private `LINK_LIBRARIES` never do. The walk had been scanning an archive the linker never saw, so the change scans exactly what is linked, no less. The executable's link line and `nm` both confirm that no downloader or httplib code is in the binary. `cli.no_listen_symbols` now scans 16 libraries.
- [x] **Tests**: 14 new cases -- the provider over the scripted runtime (which models the seam at word level: a template, a reader, special words, grammars), in both builds, and the loop's guard and notice.

**On real weights** (`apogee chat --tools` in machine mode, a driver answering each permission prompt "yes" and recording it; a throwaway home; `context_size` 32768; Q4_K_M):

| Task | Qwen3-VL-8B | Qwen3.8-27B | gpt-oss-20b (F16) | Llama-3.2-3B |
|---|---|---|---|---|
| read a file | pass, 6.5 s | pass, 75 s | pass | pass |
| write a file (gate prompted) | pass, 7.1 s | pass, 124 s | pass | pass |
| count lines with the shell (gate prompted) | pass, 7.1 s | pass, 121 s | right answer, read the file instead of using the shell | looped, and printed a call as text |
| list a folder, then read | pass, 24 s | pass, 242 s | pass | reply matched no format: noticed |
| find a URL, then fetch it (asked per website) | pass, 17 s | pass, 189 s | pass | wandered through twelve calls |
| arithmetic, no tool | pass, 5.7 s | pass, 31 s | pass | used the shell for `17 * 3` |
| | **6/6** | **6/6** | 5/6 | 2/6 (the spike measured 3/6) |

Every saved transcript held its calls and results as IR, with no `<tool_call>`, `<think>` or template markup. `apogee complete --tools -m qwen8b "What does notes.txt say?"` called `read_file` and answered from the file. `serve -m qwen8b --tools` answered the same question over HTTP with `apogee_tool_calls: ["read_file"]`. `analyze --agent security-review -m qwen8b --branch feature` found the hardcoded password in a branch it had not checked out, so it had called `git_diff`. The 27B's times are the hybrid-model re-read that [25c](#milestone-j--local-inference) exists for. Below 8B there is no special effort (the user's call); the repeated-call guard is the only concession.

**Found on the way.**
- **An empty answer.** When a reply matched no format and nothing of it had been shown, the turn ended with a notice and an empty answer. Found on Llama 3.2 3B. The raw reply now comes out as the text it was, the same safety net the fallback's gate keeps; the exception is a model that was still thinking, whose reasoning never becomes the answer.
- **A template-less GGUF.** `common` defaults such a model to ChatML. Found reading `common_chat_templates_init`, and answered by keeping the registry for it.
- **The link graph.** The no-listen scan read an archive the linker never sees. Found on the first link.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| Whose format | **Each model's own template, through `common`** (spike) | A model trained on tool tokens ignores an injected prose protocol; `common` is maintained upstream with the pin. |
| Link or copy | **Link `llama-common`**, never copy it (spike) | Part of the pinned tree; the link map pulls only chat, parser, Jinja, grammar and sampling objects. |
| Acceptance models | **8B-class and up** (the user's call) | Qwen3-VL-8B and Qwen3.8-27B. |
| Profile filters | Replaced by `common`'s parser wherever Jinja renders *(default taken)* | One parser per template, maintained upstream; the filters stay for the fallback. |
| Streaming | Re-read per token, emit the difference *(default taken)* | llama-server's method; quadratic, and nothing at chat lengths. |
| Sampling | Greedy, plus the lazy grammar *(default taken)* | Per-family sampling is [26h](#milestone-j--local-inference); the chain it will extend is `make_sampler`. |
| Tool choice | `auto`, parallel calls off *(default taken)* | One call per step is easier to gate and to show. |
| Repeated calls | The third identical call is answered unrun *(default taken)* | The spike's 3B read one file three times and ran the shell eight. |
| Image turns | Carry tools too *(default taken)* | A model asked about a picture can act on it (the Milestone O rule). |
| `common`'s JSON | Isolated in `apogee_llama_chat` | Two nlohmann/json versions behind one include guard compile silently against whichever came first. |
| httplib | Cut from `llama-common`'s link interface | Its only user is the downloader, never reached; cut, the link proves it. |
| A template-less GGUF | The registry, not `common`'s ChatML | The model's own template when it has one, the name-matched guess before a generic one. |
| An unmatched reply | Shown text stands; nothing shown → the raw text; no call runs | Never a turn with nothing in it; never a call run on a guess. |

**Guardrails, each mutation-tested (20 mutants: 19 caught outright, 1 once its test was strengthened), run in a separate git worktree.**
- **Rendering:** the tools dropped from the template (the guardrail's first named mutant); the template ignored for the fallback; the thinking switch ignored.
- **Streaming:** a call printed as text, with the preserved opener not rendered (the second); reasoning in the answer (the third); content never shown; a stop string kept, or not stopping.
- **The grammar:** never set; not cleared.
- **The rest:** a call left without an id; tools dropped silently, or a notice with no tools to drop; an unshown reply left empty, or a thinking model's reasoning made the answer; the repeated-call guard off, early, or comparing arguments as text; notices dropped by the loop; and httplib put back on `llama-common`'s link interface, which `cli.no_listen_symbols` fails naming `libcpp-httplib.a`.

**The survivor.** "Not cleared" survived because its test ran a templated request with no tools, whose grammar is empty anyway. The case that matters is a tool request followed by one that falls back, and the test now runs it.

**Not verified.**
- `common` on Linux and Windows. It builds on every target (LLAMA_BUILD_COMMON was already on), but the real-weights runs were on macOS only.
- An image turn with tools on real weights; the scripted runtime covers the shared path.
- The fallback's notice, on a real GGUF without a template: none is on this machine.

### 2026-09-28 — `hybrid-prompt-checkpoints` (backlog item 25c): a hybrid model reads only what is new

**The re-read.** Qwen3.5 and 3.8 mix attention layers with linear-attention layers, whose running state llama.cpp cannot rewind. A thinking model's template re-renders the last answer without its reasoning, so every turn's prompt parts from the cache inside that answer. The trim to the shared prefix was refused, the cache cleared, and the whole conversation read again. That has been correct since 2026-09-23, and slow: at 25b, Qwen3.8-27B spent 31–242 s per tool task, most of it re-reading.

**What was built**

- [x] **Checkpoints, as llama-server takes them.** The seam is in `llama_runtime.h`.
  - `LlamaContext::checkpoint(position)` saves the part of the cache that cannot be rewound, as it stands after exactly `position` tokens. It uses `llama_state_seq_get_data_ext` with `LLAMA_STATE_SEQ_FLAGS_PARTIAL_ONLY`: the running state only, while the attention half stays in the cache and trims like any other.
  - `trim_to`, when refused, restores the newest checkpoint at or before the position and trims to it. The restored state ends exactly there, so the cut succeeds. It returns where the cache really ends. With no checkpoint to go back to, it clears as before.
  - `needs_checkpoints()` answers true only for a recurrent or hybrid model (`llama_model_is_recurrent/hybrid`). A sliding-window model would qualify, but contexts keep a full-size window cache by default (`swa_full`), which trims.
- [x] **One policy, shared** (`keep_checkpoint`, `checkpoint_for`, `forget_checkpoints_after`: templates in `llama_runtime.h`). A checkpoint at a held position replaces it. Past `kMaxCheckpoints` (8) the oldest goes. A restore takes the newest at or before the divergence, never one past it, and a trim drops every checkpoint past where the cache ends. The real runtime and the scripted one both use it, so the tests hold the real rules.
- [x] **Where they are taken** (`LlamaCppProvider::checkpoint_marks`). `decode_in_batches` ends a batch at each mark and saves there.
  - **Four tokens short of the prompt's end**, llama-server's offset. The next prompt diverges inside the generation prompt that opens this answer (`<|im_start|>assistant\n<think>\n` on Qwen), so a checkpoint at the very end would lie past the divergence.
  - **Where the last user message starts.** It is found by rendering the conversation before it alone (`render_chat`, and the fallback's `render_prompt`, gained `add_generation_prompt`). It is kept only if it really is a token prefix of the prompt, because a template that renders earlier messages differently once they are not last gives no position.
  - None on a side request's context, an image turn's, or a pure-attention model's, where finding the marks is skipped entirely.
- [x] **The report.** A `PromptCache` status event per turn gives the prompt, the tokens reused and read, and the checkpoints held with their size. The loop hands it to a new `Reporter::on_progress`, and the terminal prints it under `--verbose` only; machine mode and `serve` drop it.
- [x] **Tests**: 10 new cases, over the scripted runtime (`rewindable = false` now restores through the shared policy) and the policy itself, plus the loop's progress hand-off and the verbose-only print. The old "reads everything again" case now holds the invariant it was written for: decoding resumes exactly where the cache really ends, never past an uncut prefix.

**On real weights** (Qwen3.8-27B Q4_K_M, `context_size` 32768, greedy; `--verbose` lines):

| | prompt | from the cache | read | checkpoints held |
|---|---|---|---|---|
| chat, turn 1 | 61 | 0 | 61 | 1 (150 MiB) |
| turn 2 | 115 | 57 | 58 | 3 |
| turn 3 | 136 | 111 | 25 | 5 |
| turn 4 | 168 | 132 | 36 | 7 (1047 MiB) |
| tool loop, step 1 | 2752 | 0 | 2752 | 1 |
| steps 2–6 | 2860–3142 | all but the new | 112, 140, 53, 51, 54 | 2–6 |

- The four-turn chat's answers, and the tool loop's (list a folder, read its three files, name the color), were **byte-identical** to a build without checkpoints (25b, `1aa702d`) on the same prompts.
- Wall time was 21.6 s against 33.7 s for the chat, and **46.5 s against 156.3 s** for the tool loop.
- Qwen3-VL-8B (pure attention) took no checkpoints, and its turns reused the cache by trimming, as before.
- **Measured: one checkpoint of Qwen3.8-27B's state is 149.6 MiB**, so the cap of 8 is about 1.2 GiB of host memory at most. A chat adds two per turn, the user-start one and the end one, and a tool step adds one.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| How | **Checkpoints, as llama-server does them** | Keeping reasoning in the IR so the prompt never diverges would break "thinking is never persisted", and would not help a template that changes earlier text for another reason. |
| How many | 8 per context, the newest kept *(default taken)* | A chat needs the one before its latest answer. llama-server's 32 is for a server's slots; at 150 MiB each, 8 is already 1.2 GiB. |
| Where | Four tokens short of the prompt's end, and the last user message's start *(default taken; the end one placed as llama-server places it)* | The two divergence points a chat and a tool loop produce. A checkpoint at the exact end would be past the thinking model's divergence and never usable. |
| Image turns | None *(default taken)* | They run on a throwaway context. |
| Which models | Recurrent or hybrid only | A pure-attention cache trims; `swa_full` makes a sliding-window one trim too. |
| The policy's home | Templates in `llama_runtime.h`, shared by both runtimes | The cap and "never past the divergence" are tested where they run, not on a copy. |
| A user-start mark that is not a prefix | Not taken | A checkpoint at a position the prompt never passed through holds a state no later prompt shares. |
| The report | `--verbose` only | Memory the user never asked for is visible where they look for it, and silent otherwise. |

**Guardrails, each mutation-tested (17 mutants, all caught), run in a separate git worktree.**
- **The restore:** a checkpoint past the divergence restored (the guardrail's first named mutant); the restore skipped (the second).
- **The policy:** the cap never evicting, or off by one; a same-position checkpoint doubled; stale checkpoints kept after a trim.
- **The marks:** no checkpoint near the end, or one at the very end; the user-start mark taken without the prefix check (caught by a case added for it before the run: a template whose conversation-so-far renders differently), or not at all.
- **The decode:** no batch split at a mark; a mark passed without a checkpoint.
- **The rest:** a pure-attention model paying to find marks; a side request taking checkpoints; the report never sent; the loop dropping progress; the terminal printing it without `--verbose`.

**Not verified.**
- A Mamba or RWKV model: none is on this machine. They are recurrent, so they take checkpoints the same way.
- The retrieval block at the conversation's start (`transient_at` 0, the default) still re-reads from 0 on a hybrid model whenever it changes: no checkpoint can be before position 0. Moving that block later is its own question.

### 2026-09-28 — `context-fit-defaults` (backlog item 26a): a window sized to the machine

**The memory nobody asked for.** A local backend with no `context_size` got its model's whole trained window, and llama.cpp allocates a context's attention cache up front. Qwen3.8-27B was trained for 262,144 positions: 16 GiB of cache at `f16` beside 16 GB of weights, before the first word. That is the memory helper models and attachments need next. And without a `context_size` the chat never knew the window at all: a local entry has no row in the fallback table, so a long local chat was never warned or compacted, only run into the wall.

**What was built**

- [x] **The default window** (`models/kv_cache.h`). Unset, a local backend's window is 32,768 tokens, or the trained window when that is smaller, or what free memory holds when that is smaller still (`default_local_window`). A `context_size` is used exactly as written, past the trained window too, and is never fitted.
- [x] **Fitted only when it has to be** (`llama_real.cpp`, `fit_default_window`). llama.cpp's own fitter (`common/fit.h`, `common_fit_params`, the one llama-server runs) is the lowering step, behind `llama_chat::fit_window`. It projects the model onto each device's free memory by reading it over, which took 0.13–0.72 s on the models measured and, on a machine that holds the default, only ever answers "it fits". So a check that costs nothing goes first: the weights, the default window's cache from the header, the projector and llama-server's 1 GiB margin against what the GPU has free. Only a model that fails it, or whose cache the header cannot size, is handed to the fitter.
- [x] **An 8-bit cache** (`cache_type` on a llamacpp backend: `f16`, `q8_0`, `q4_0`; unset is `q8_0`). A quantized cache needs flash attention, so it is turned on for one rather than left to detection, which on a device without the kernel would turn it off and fail the context. An `f16` cache leaves it to llama.cpp as before. When the default `q8_0` cannot be made (a head width its blocks of 32 do not divide, say), the context is made at `f16` instead; a type the config names is used or refused, with the way out in the message. `config add-backend --cache-type` and its admin twin write it, byte-identical; `config get` reads it.
- [x] **The cost, stated** (`models/kv_cache.h`, `gguf_inspect`). The header reader now keeps the attention geometry, and `kv_values_per_position` counts what the cache keeps per position over the layers llama.cpp gives one: a Qwen3.5 or 3.8 every `full_attention_interval` layers of the main stack (its prediction layers excluded), none for a layer with no key-value heads or one sharing an earlier layer's cache (Gemma's `shared_kv_layers`), a sliding layer at its own widths. Latent attention (DeepSeek's) is unknown, never a guess. `models info` prints `window:` and `cache:`; `check`'s backend row ends with the same. `--verbose`'s per-turn cache line now ends with the window and how the cache is kept.
- [x] **The window the chat measures against** (`ContextWindowReporting` in `provider.h`). `Harness::context_window_for_model` asks the entry's `context_size`, then the backend, then the table. The llamacpp provider answers with its session window once loaded, and before that with the default from the header, read once. Side contexts, image turns and the session all stay inside it; a side request could previously ask for up to the trained window.
- [x] **Tests**: 24 new cases: the arithmetic rule by rule and against llama.cpp's own sizes, the header read, the provider's window and cache over the scripted runtime (which now has a trained length, a fitted window, a cache type and a vision switch), the harness asking it, a local chat at 90% flagged for compaction, the config, `check`, `models info`, the admin parity, and `config_lifecycle` on the real binary.

**Checked against llama.cpp** (its own `llama_kv_cache: size` line creating a 32,768-position context at the pinned `b11151`, and `kv_values_per_position` on each model's header; every one equal to the MiB):

| Model | `q8_0` | `f16` | Layers with a cache |
|---|---|---|---|
| Qwen3.8-27B | 1,088 MiB | 2,048 MiB | 16 of 64, plus 150 MiB of fixed recurrent state |
| Qwen3-VL-8B | 2,448 MiB | 4,608 MiB | 36 |
| Gemma 4 12B | 5,712 MiB | 10,752 MiB | 8 full, 40 sliding |
| Gemma 4 31B | 14,960 MiB | 28,160 MiB | 10 full, 50 sliding |
| Llama 3.1 8B | 2,176 MiB | 4,096 MiB | 32 |
| Llama 3.2 3B | 1,904 MiB | 3,584 MiB | 28 |
| gpt-oss-20b | 816 MiB | 1,536 MiB | 12 full, 12 sliding |

**On real weights**

- **Qwen3.8-27B, no `context_size`**: peak memory footprint (`/usr/bin/time -l`) **16.7 GiB before, 1.7 GiB after**, for the same one-word answer. The weights are memory-mapped and not counted, so the difference is the cache: 16 GiB at the trained window in `f16`, 1.06 GiB at 32K in `q8_0`. `--verbose` reads `window 32768, q8_0 cache`; `check` reads `32768-token window, 1088 MiB q8_0 cache`.
- **The spike's six tasks** (`apogee complete --tools`, greedy, a throwaway home with writes and the shell allowed, both at the new 32K default): **6/6 on Qwen3-VL-8B and 6/6 on Qwen3.8-27B with the `q8_0` cache, and 6/6 on each with `f16`** -- read a file, write one, count lines with the shell, list a folder and read from it, find a URL and fetch it (a website asked about and allowed), and `17 * 3` without a tool. **Every answer was byte-identical between the two caches**, on both models. Peak memory per run: 2.8 GiB against 4.9 GiB on the 8B, 2.0 against 2.9 GiB on the 27B (which also holds its checkpoints).
- **Speed** (llama-bench at the pin, flash attention on, 512-token prompt and 128 generated, at an empty cache and at 4,096 tokens deep; each cache type run twice, interleaved `q8_0`, `f16`, `f16`, `q8_0` so the GPU's heat falls on both alike -- it was hot, after an hour of runs, so every absolute number is below the quiet ones):

  | | prompt, empty | generation, empty | prompt at 4K | generation at 4K |
  |---|---|---|---|---|
  | Qwen3-VL-8B, `q8_0` | 376.9 t/s | 32.1 | 297.6 | 28.8 |
  | Qwen3-VL-8B, `f16` | 373.0 | 32.4 | 301.2 | 29.4 |
  | Qwen3.8-27B, `q8_0` | 97.6 | 9.62 | 92.0 | 9.04 |
  | Qwen3.8-27B, `f16` | 95.0 | 9.47 | 86.6 | 9.07 |

  Within 2% either way, which is the noise between the two passes of one type, except that the 27B reads a prompt 6% faster at depth with the smaller cache. The six tasks' wall times favoured `q8_0` by more (60.9 s against 83.7 s on the 8B, 328 s against 404 s on the 27B), but those ran first, on a cooler GPU, so they are not the measurement.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| The unset window | 32,768, or the trained window when smaller, lowered only when free memory cannot hold it *(default taken)* | Predictable beats clever: a long chat compacts at 90% rather than the cache taking gigabytes. |
| Who lowers it | llama.cpp's fitter, as the item said — **asked only when a free check fails** *(refinement of the default)* | The fitter reads the model over (0.13–0.72 s a load) and, on this machine, never changed an answer. The free check is the same arithmetic `check` shows. |
| The cache | `q8_0` keys and values *(default taken)*, measured against `f16` below | Half the memory; the answers and speed below. |
| Flash attention | On for a quantized cache, llama.cpp's choice for `f16` *(the default, made precise)* | A quantized cache needs it, and auto-detection would fail such a context on a device without the kernel. For `f16` it is not needed, and forcing it on such a device would run attention on the CPU. Auto turned it on for every model measured on Metal. |
| A model that cannot take `q8_0` | The default falls back to `f16`; a named type is refused | The default is ours to adjust; a named one is the user's. |
| Showing the cost | `models info`, `check`, and `--verbose` *(default taken)* | Memory a user never asked for is visible where they look. |
| The window the chat measures | Asked of the backend, after the entry's `context_size` | A local window is fitted at load; no table of names can hold it. |
| Sliding-window layers | Kept at full size (`swa_full`), as before | Out of scope; see below. |

**Guardrails, each mutation-tested (44 mutants, all caught, one only after its test was strengthened), run in a separate git worktree against the whole unit suite.**
- **The window:** the trained window, or what fits, ignored; what fits raising the window; positions not padded; an explicit `context_size` fitted, or ignored for `models info`; the session, a side request or an image turn at the trained window; the fitted window ignored after a load; the window before a load taken from the trained one, or not asked of a loaded model.
- **The layers:** a hybrid's prediction layers kept; no default interval; Qwen3.5 MoE and Qwen3-Next not hybrids; shared layers kept; an absent key-value head count unknown; sliding widths ignored; a sliding period off by one; latent attention sized; the pattern or the whole geometry dropped from the header read, and latent attention unnoticed there.
- **The bytes:** `q8_0` at one byte a value; `q4_0` at `q8_0`'s size.
- **The cache type:** any value accepted at load, or dropped; not written by the entry writer, `add-backend` or the admin twin, or missing from the view; not mapped from the config, not marked named, or not passed to the load.
- **The window the chat measures:** the backend not asked; the backend asked before the entry's `context_size`.
- **The display:** `models info` without its cache line or its "the default"; `check` without the window; an unsized cache shown as a size; the verbose line without the window.
- **The survivor:** a width fallback that overrode a width the header did give. The test's value width equalled the fallback's by chance; it now differs.

**Not verified, and found on the way.**
- **The lowering itself** has not run on real hardware: this machine has 128 GB, and no model here fails the free check. The fitter was run directly: at the trained window it lowered Gemma 4 31B at F16 (62 GB of weights) to 105,728 positions, which the default then caps at 32K anyway.
- **The `f16` fallback** has not met a model that needs it: every head width here divides into 32.
- **Gemma 4's sliding layers are most of its cache**: 13.3 of Gemma 4 31B's 14.6 GiB at 32K, because contexts keep a full-size sliding-window cache (`swa_full`) so they can trim. A window-sized one would be about 0.6 GiB, but it cannot be rewound, so it needs the checkpoints 25c built for hybrid models. That became 26m, below, shipped the same day.

### 2026-09-28 — `sliding-window-cache` (backlog item 26m): a window-sized cache for sliding-window models

**The cache that was mostly window.** Gemma 4 alternates five sliding-window layers (a 1,024-token window) with one full one; gpt-oss alternates one to one (a 128-token window). A sliding layer only ever looks back its window, but Apogee's contexts kept every sliding layer at the conversation's full length (llama.cpp's `swa_full`), because a full-length cache can be cut back anywhere and a chat's next turn depends on that cut. Found shipping 26a: at the new 32K default that was **13.3 of Gemma 4 31B's 14.6 GiB** of cache. llama-server's own default is the other way round -- a window-sized cache, checkpoints, and a check before any cut is trusted -- and that is what this item ports.

**What was built**

- [x] **A window-sized sliding cache** (`llama_real.cpp`, `create`: `swa_full = false`). A sliding layer keeps its window and a batch -- 1,536 positions on Gemma 4, 768 on gpt-oss -- and llama.cpp's fitter projects the same (`llama_chat::fit_window`). A model with no sliding window is unaffected.
- [x] **Checkpoints for sliding models** (`make_context`): a context needs them when `llama_model_n_swa` is set, as well as for a recurrent or hybrid model. They are 25c's: the same marks (four tokens short of the prompt's end, and the last user message's start), the same shared policy, the same `PARTIAL_ONLY` save -- which for a sliding cache holds just its sliding part (`llama_kv_cache_iswa::state_write`).
- [x] **Every cut checked** (`window_intact` in `llama_runtime.h`, shared by both runtimes). After a cut to `p`, the cache must still hold the window before `p`: its oldest position (`llama_memory_seq_pos_min`) is 0 or lies before `p` minus the window -- llama-server's test and margin (`pos_min_thold`). A cut that fails it is refused as a hybrid model's is: the newest checkpoint at or before `p` is restored, else the prompt is read from 0. A restored checkpoint is checked the same way, though one taken from a cache llama.cpp builds always holds its window.
- [x] **The stated cost follows** (`models/kv_cache.h`). `cache_shape` splits the cache into the layers that keep the whole context and the sliding ones, with the window; `sliding_positions` is llama.cpp's rule (the window and a batch of 512, padded to 256, never past the context); `cache_values` sums them. A layer slides only in a family llama.cpp runs with a sliding window -- Gemma 2, 3 and 4 and gpt-oss -- by the header's pattern or, where the header names none, llama.cpp's for the family (every other layer on gpt-oss and Gemma 2; every sixth full on Gemma 3; Gemma 2's window 4,096 when unstated). `gguf_inspect` reads `attention.sliding_window`. `models info` adds "sliding layers at 1536 positions".
- [x] **The scripted runtime slides too** (`tests/support/fake_llama.h`): a context with a window drops positions older than what it keeps, refuses a cut that leaves the window short, and **fails any test that decodes with the window broken** -- `window_intact` asserted on every decode.
- [x] **Tests**: 7 new cases and three extended: the rule itself at its edges, a sliding model's chat whose cut stands and one whose cut is refused and restored, a tool step, a cut with no checkpoint behind it read from 0, the sliding sizes against llama.cpp's own, the families (a Qwen2 header's window ignored, Gemma 2's default window, Gemma 3's default period, a pattern with nothing sliding), the padding, and `models info`.

**Sizes** (llama.cpp's own `llama_kv_cache: size` lines at the pin, a 32,768-position context at `q8_0`; `models info` now states each to the MiB):

| Model | Full-length sliding layers | Window-sized |
|---|---|---|
| Gemma 4 31B | 14,960 MiB | 1,998 MiB |
| Gemma 4 12B | 5,712 MiB | 527 MiB |
| gpt-oss-20b | 816 MiB | 418 MiB |

**On real weights** (greedy, the new binary against a build of the 26a commit, which keeps full-length sliding layers; `--verbose` lines):

- **Gemma 4 31B** (the Q4_K_M in the store, a base model -- see below): a one-shot completion's peak memory footprint went from **14.96 GiB to 2.29 GiB**, output identical. A four-turn chat: **16.80 GiB to 4.22 GiB**, including seven checkpoints at 1,978 MiB, with byte-identical answers and the same reuse every turn (432, 871, 1,309 tokens from the cache).
- **gpt-oss-20b, a cut past the window**: a three-turn chat whose first answer lists 300 squares (about 1,500 tokens). Its template drops the reasoning from history, so turn two parts from the cache at position 88, over 1,400 tokens back -- far past the 768 positions the sliding layers still held. **The cut was refused, the checkpoint at 83 restored**, and 1,497 tokens read where the full-length cache read 1,492. All three answers byte-identical to the full-length cache's.
- **gpt-oss-20b, a six-step tool loop** (read five files one at a time, name the one that mentions blue): byte-identical, each step reading only the call and its result (41, 42, 43, 40, 42 tokens), peak 0.91 GiB against 1.30.
- **gpt-oss-20b, a four-turn chat**: the same words, but turn one's first answer ended two lines with two spaces (Markdown line breaks) -- a near-tie tipped on a fresh context, before any cut; the reuse (77, 103, 124 tokens) was the same.
- **Gemma 4 12B, a four-turn chat**: peak 6.63 GiB to 1.82. Its answers parted inside turn one's 800-token repetition, again before any cut ("one per line." against "one per.").
- **Without the check** (a build with it removed), the long gpt-oss run cut at 88 with the start of its window gone from half the model's layers -- and happened to give the same answers. The harm of a short window is subtle, which is why it is held by a rule and a test that fails any decode across it, not by comparing answers.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| When | Always window-sized for a sliding-window model, no setting *(default taken)* | llama-server's default; the answers matched wherever a cut was involved. |
| The check | llama-server's `pos_min` test and margin, on every cut and every restore | It errs safe; a restore passing it is the normal case. |
| How many checkpoints | 25c's 8, no byte cap *(default taken)* | Measured: seven held 1,978 MiB on Gemma 4 31B in a short chat; at most 8 × 637.5 MiB once the window fills, against the 12.6 GiB saved. |
| Which layers slide in the stated cost | The four families llama.cpp runs sliding; any other family's layers full *(the default, refined)* | Converters write `sliding_window` for every model whose config has one -- Qwen2 and Mistral included -- and llama.cpp ignores it for them; "not known" would have hidden their cost. A sliding family not on the list is over-stated, as it was before. |
| The scripted runtime | Asserts the window on every decode | The property the item exists for holds in every test that uses a sliding model, not only in the ones written for it. |

**Guardrails, each mutation-tested (19 mutants, all caught), run in a separate git worktree against the whole unit suite.**
- **The rule:** the window ignored; an emptied cache counted as intact; a cache held from 0 counted as short; llama-server's margin dropped.
- **The sliding size:** the whole context instead of the window; no batch past the window; not padded; past the context; the values summed at the whole context.
- **The families:** any family sliding; Gemma 2's window, Gemma 3's period or gpt-oss's pattern not assumed; the header's pattern or window ignored; a window reported with nothing sliding; the header's window not read.
- **What is said:** `models info` without the sliding line, and the sliding positions never set.
- **Not mutated, and why:** the real runtime's calls (`swa_full`, the checkpoints for a sliding model, the check after a cut and after a restore) compile only with llama.cpp and have no fake beneath them. They were exercised on real weights instead, above, including a build with the check removed.

**Not verified, and found on the way.**
- **Byte-identical answers are not guaranteed in general.** Twice, a near-tie tipped before any cut: gpt-oss's first answer on a fresh context, and Gemma 4 12B deep in a repetition loop. Two things differ there: the sliding layers attend over a 768- or 1,536-position cache rather than 32,768, and a prompt is now decoded in two batches, split at the checkpoint mark. Either can move a near-tie. Wherever a cut was refused or stood, the answers matched.
- **The Gemma 4 GGUFs in this store are base models without a chat template.** Their snapshots carry none (`tokenizer_config.json` has no `chat_template`, and there is no `chat_template.jinja`), so they render through the ChatML fallback and continue text rather than answer. The measurements above used them as they are; a tool loop on Gemma 4 was not possible.
- **Chunked attention** (Llama 4) also reports a window, so it gets the window-sized cache and checkpoints too; the check errs safe for a chunk as well. No such model is on this machine.

### 2026-09-28 — A model file with no chat template: said, and stopped (a fix)

**Found in use.** Two chats with tools, on backends named `Gemma4-12B-Q4KM` and `Llama3.1-8B-Q4KM`, printed "is answering without tools: the model ships no chat template" and then went wrong. Gemma 4 12B wrote an answer, then `<|im_start|>system` and an invented rest of the transcript, to the 4,096-token cap. Llama 3.1 8B answered, then repeated "(in Kelvin)" to its cap. Both files were converted from the **base (pretrained) releases**, `google/gemma-4-12B` and `meta-llama/Llama-3.1-8B`, which ship no chat template on Hugging Face; the chat releases are `google/gemma-4-12B-it` and `meta-llama/Llama-3.1-8B-Instruct`. `models pull` had said so at the time, and the chat never did. Three things in Apogee made it worse than it had to be:

- **The warning named a symptom.** "Answering without tools" reads like a bug in Apogee's tool support, and it was the only thing said.
- **A guessed framing had no end.** With no template, the prompt is rendered in a guessed format -- ChatML for Gemma, Llama 3's for Llama. A model that does not know that format's turn markers writes them out as text, never produces an end-of-generation token, and goes on to invent the rest of the conversation.
- **A latent cache gap, found on the way.** The token that completes a stop string is never fed back into the cache, but it was kept in the conversation's cached tokens. A next prompt sharing that token would have decoded one position past the cache's end.

**What was built**

- [x] **Said once a conversation** (`LlamaCppProvider::notice_if_toolless`): a model file whose header has no `tokenizer.chat_template` gets, in place of the tools line, "*file* ships no chat template, so it is most likely a base (pretrained) model: it continues text rather than answering, and cannot use tools. For chat, use its instruction-tuned release, usually named '-it' or '-Instruct'". Named by its file, not its path. `gguf_inspect` notes the key's presence without reading it (`has_chat_template`), and `models info` adds a `template:` line.
- [x] **A guessed framing's markers end the reply** (`RenderedPrompt::stops`, `StopWatch`). ChatML stops at `<|im_end|>` and `<|im_start|>`, Llama 3 at `<|eot_id|>` and `<|start_header_id|>`, Mistral at `</s>` and `[INST]`; the model's own template carries none, since its end is a real end-of-generation token. Text that could still become a marker is held back, so a marker never reaches the screen in part, and text that only looked like one is released.
- [x] **A stop's token is not claimed by the cache** (`Generation::last_unfed`): it still counts as generated, and the conversation's cached tokens stop before it. The scripted context now refuses any decode past its end, so the gap cannot come back quietly.
- [x] **Tests**: 7 new cases: the marker ending the reply unseen, a near-marker kept and a held one flushed, a stop's token and the next turn's decode, the notice once and in place of the tools line, the template noticed in the header, both `template:` lines, and every guessed framing's stops.

**On real weights** (the new binary, both files as they are): Gemma 4 12B gave the notice once, answered, and stopped at its guessed format's marker after 6 s instead of running to its cap. Llama 3.1 8B gave the notice and still loops to its cap, repeating itself rather than writing any marker; a base model under greedy sampling does that, and taming it belongs to sampling ([26h](#milestone-j--local-inference)). The fix that makes those chats work is the instruct release.

**Guardrails, each mutation-tested (13 mutants, all caught), run in a separate git worktree against the whole unit suite:** a marker never ending the reply, nothing held back, held text lost at the end, the reply going on past a marker; the stop's token claimed, or never marked; the notice every turn, never, or beside the tools line; the fallback with no stops; the template unnoticed or its value not stepped over; `models info` inverted.

### 2026-10-03 — `sampling-profiles` (backlog item 26h): sampled the way the model's authors ask

**Why.** The llama.cpp backend sampled greedily whatever was asked: `chat -t 0.7`, `/temperature` and a backend's `temperature:` were accepted and silently ignored on every local model, since 25b. Greedy decoding is also what Qwen advises against for its thinking models, which loop under it.

**What was built**

- [x] **One ladder, per knob** (`backends/sampling.h/.cpp`). Each of temperature, top-p, top-k, min-p, repeat penalty and presence penalty takes the first rung that sets it:
  1. the request's own -- `-t`, `/temperature`;
  2. the backend's config;
  3. the model file's `general.sampling.*` -- the authors' recommendation, which a conversion writes from `generation_config.json`;
  4. the family's published default, split by thinking on and off;
  5. llama.cpp's neutral value, which is greedy.

  Per knob, not per rung: a file that names a temperature and no top-k leaves the top-k to its family.
- [x] **The file's recommendation is read from the header** (`GgufInfo::sampling`): `temp`, `top_p`, `top_k`, `min_p` and `penalty_repeat`, any number type, a wrongly typed key stepped over. No weights are loaded. Every installed Gemma 4, Qwen3-VL, Qwen3.8 and Llama 3.x carries some of these; gpt-oss and Qwen3-Omni carry none.
- [x] **Family defaults with their source** (`ModelProfile::sampling_thinking`, `sampling_answering`, `sampling_source`):
  - Qwen3: 0.6, top-p 0.95, top-k 20, min-p 0 when thinking; 0.7, 0.8, 20, 0 when not.
  - Gemma: 1.0, top-k 64, top-p 0.95, min-p 0.
  - gpt-oss: 1.0, top-p 1.0.
  - Llama 3.x: 0.6, top-p 0.9.
  - DeepSeek-R1: 0.6, top-p 0.95.

  `chatml` names none. A request that skips reasoning gets the family's no-thinking values.
- [x] **One sampler chain** (`make_sampler`, `llama_real.cpp`): the grammar first, as before -- the schema grammar's prefill step kept -- then the penalties, the top-k, top-p and min-p cuts, the temperature and a seeded draw, llama-server's order.
  - Temperature 0 is greedy, byte for byte reproducible.
  - The penalties apply under greedy too, being deterministic.
  - `LlamaContext::set_grammar` became `set_sampling(grammar, settings)`, rebuilt every generation, so one answer's seed is not the next one's.
- [x] **The knobs in the config** (`top_p`, `top_k`, `min_p`, `repeat_penalty`, `presence_penalty`, `seed`), each range-checked at load and named in a refusal:
  - written by the one editor;
  - settable by `config add-backend` and its admin twin -- the config's own rules check both, since every edit re-parses what it writes;
  - shown by the admin view, completed by `config get`, documented in the starter template.
- [x] **`models info` shows what is in force and where each value came from**, the family's card named when it supplied one. On a cloud backend, it names any of the knobs that are set but not sent: the vendor samples with the temperature alone.

**On real weights** (the family models, one at a time, each loaded once; no Qwen3.8-27B inference -- the user's call):
- **`apogee complete -m Llama3.2-3B-Q4KM`:** `-t 0.9` twice gave two different sentences; `-t 0` twice gave byte-identical ones. Before this, all four would have been the same.
- **`models info`, header only:**
  - Qwen3.8-27B reads 1.0, 0.95, 20 from its file, min-p 0 from the Qwen3 card;
  - Qwen3-VL-8B reads 0.7, 0.8, 20 from its file;
  - Gemma 4 12B reads 1.0, 0.95, 64 from its file;
  - Llama 3.1 8B reads 0.6, 0.9 from its file;
  - gpt-oss-20b reads 1.0, 1.0 from its card, its file naming none.
- **The spike's six tasks, sampled by the ladder** (the production loop and registry, each task its own conversation, a driver allowing every prompt):

| Task | Qwen3-VL-8B (Q4_K_M) | Gemma 4 12B (Q4_K_M) | gpt-oss-20b (F16) | Llama 3.1 8B (Q4_K_M) |
|---|---|---|---|---|
| read a file | pass | pass | pass | pass |
| write a file | pass | pass | pass | pass |
| count lines with the shell | pass | pass | pass | pass |
| list a folder, then read | pass | pass | pass | pass |
| find a URL, then fetch it | pass | pass | pass | made up an address and a title |
| `17 * 23`, no tool | pass | pass | pass | right answer, through the shell |
| | **6/6** | **6/6** | **6/6** | 5/6 |

The defaults lost no task: Llama 3.1 8B failed the URL task greedily too (26g's runs looped twelve calls on an invented address).

**Tests**: 27 new cases, 230 assertions:
- the ladder rung by rung and per knob;
- the family source credited only when used;
- the seed;
- the header's five keys across number types;
- `-t` reaching the sampler -- the regression;
- greedy at 0 over a file that suggests otherwise;
- the file, family (thinking or not), unprofiled and config rungs through the provider;
- a side request's own temperature;
- the config's bounds and the editor's round trip;
- `add-backend` and the admin create byte-identical, both refusing a bad value;
- `models info` local and cloud.

**Guardrails, each mutation-tested (18 mutants, all caught), in a separate git worktree:** the ladder reordered twice, a default changed, the family credited wrongly, thinking ignored or inverted, a knob or the seed dropped at every hop (resolver, provider, header, editor, CLI, admin), the request's temperature dropped, a bound loosened.

**Found on the way.** The test binary linked the SQLite amalgamation twice since A4 -- once by name, once through `embedstore`, which already hands it on -- and the linker said so on every build. Now once.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| A GGUF's sampling against its family's | The file's first *(confirmed by the user)* | It is specific to the model, and most files carry it. |
| Tests | Scripted and recorded goldens pin temperature 0 *(confirmed by the user)* | Deterministic; every side request already asks for 0 (titles, the clerk, rerank, rewrites, summaries, media). |
| Cloud backends | Only the temperature is sent; `models info` names the rest as unsent | The item's seam is the local backend. Each vendor's own top-p and top-k mapping would be wire changes of their own. |
| The request rung | The temperature alone | `-t` and `/temperature` already existed. The rest are per backend, as the item's "as needed" left them. |
| The seed | Config only; unset draws one per answer | "A seed is settable", and per-backend is where it repeats. |
| Penalties under greedy | Applied | Deterministic, so `-t 0` stays reproducible with a configured penalty. |
| The acceptance models | The model families, replacing the doc's two | The standing real-weights rule. The 27B named in the criterion was read header-only, never run. |

### 2026-10-03 — `thinking-control` (backlog item 26i): on, off, automatic, and a budget

**Why.** Reasoning was most of the time a thinking model took to answer -- 26 of 57 seconds of one ordinary answer on the reference machine (2026-09-25) -- and the same cost for a capital city as for a proof. Nothing could turn it down: the only switch was a Qwen-only `skip_reasoning` the title request used.

**What was built**

- [x] **One setting on every request** (`harness::Thinking` on `ChatRequest`): a mode -- `on`, `off` or `auto` -- and an optional budget in tokens. It replaces `transient.skip_reasoning`, which was its `off`; every side request that skipped reasoning now asks for `off`.
- [x] **Set where every other run setting is set.**
  - `--think on|off|auto` and `--think-budget N` on `chat` and `complete`.
  - `/think` in chat, a row in the one command table with its three values completing: bare, it says what the next question gets; with a mode, it sets it. A chat saves both in its session (`think`, `think_budget`), so a resumed chat thinks as it did.
  - A backend's `thinking:` and `thinking_budget:` in the config, settable by `config add-backend` and its admin twin, read by `config get`, documented in the starter template.
  - `serve`, `analyze` and the legacy completion route apply the backend's setting too.
- [x] **`auto`, decided once per question** (`agentloop/thinking`). The utility model, when the config names one, is asked one word -- greedy, a side request, its own thinking off -- and its yes or no decides. Without one, or when it fails or says anything else, a rule decides: think for a question over 200 characters, or one holding code, arithmetic, or the words why or how. The decision is made once per turn, so every step of a tool loop agrees, and `--verbose` says what decided it.
- [x] **The local model's own switch** (`llamacpp`). `off` renders the template's `enable_thinking=false` (Qwen3, Gemma 4) and asks `reasoning_effort=low` (gpt-oss's least: it has no off).
- [x] **The budget is a sampler in the one chain** -- llama.cpp's reasoning-budget sampler, first in `make_sampler`. It counts between the format's own reasoning tags, from inside the block when the template opens it, and forces the close tag at N. A format with no reasoning tags gets no budget, and the conversation is told once.
- [x] **Every vendor's own control** (`*_wire`):
  - Anthropic: off sends no thinking; a budget is sent at the API's 1024 floor or above, with `max_tokens` kept 1024 above it and no temperature while it thinks.
  - OpenAI: off is each model's least effort -- `none` from gpt-5.1, `minimal` on gpt-5, `low` on the o-series -- and a model that does not reason is sent no effort at all.
  - Gemini: off is `thinkingBudget` 0, or 128 on a Pro model, which cannot stop; models before 2.5 are sent none.
- [x] **The display says what happened.** A budget that cut reasoning short reads `✻ Thought for 20s (budget reached)`, or ends a verbose transcript; machine mode sends a second `thinking` event with `"budget_reached": true`; `serve` a `thinking_budget` meta-frame. Thinking still never reaches history.
- [x] **`models info` says what control there is.** A `thinking:` line on every backend: the mode and budget and where each came from, and what this backend does with them. A local model's template is read for whether it has a switch, names reasoning without one, or names none; a vendor CLI or the mock says it has no control here.

**On real weights** (the families, one at a time, each loaded once, through the production loop; no Qwen3.8-27B -- the user's call, "only where nothing else works"; the mutation build ran beside gpt-oss, Qwen and Llama, whose times are therefore rough):

| | Gemma 4 12B (Q4_K_M) | gpt-oss-20b (F16) | Qwen3-VL-8B (Q4_K_M) | Llama 3.1 8B (Q4_K_M) |
|---|---|---|---|---|
| on: thinking, first answer token | 1,176 chars, 35 s (the load included) | 1,101 chars, 11 s | none: it does not think | none |
| off: thinking, first answer token | **none**, 0.32 s | 139 chars (`low`), 2.0 s | none, 0.05 s | none, 0.03 s |
| budget 256 | **reached and said**: 584 chars, 9.8 s | **reached and said**: 572 chars | said not applied: no reasoning tags | said not applied |
| ten chat questions, on / auto | 217 s / 150 s | 105 s / 116 s | no difference | no difference |
| the six-task battery, on / auto | 6/6 / 6/6 | 6/6 / 6/6 | 6/6 / 6/6 | 6/6 / 4/6 |

- **The arithmetic question answered with thinking off** on Gemma 4 ("A train leaves at 09:40…") began in 0.32 s with no thinking block.
- **`auto` on the chat set, by the rule:** Gemma answered "Is 221 a prime number?" right in 3 s instead of 38, and the lookups in under a second. Its tool battery under `auto` was slower (83 s against 62): flipping the switch between tasks re-reads the whole tools prompt, because Gemma's template places the switch at the top. That is why `auto` stays opt-in.
- **Llama 3.1 8B's two misses under `auto`** are sampling, not thinking: the template has no switch, and the first prompt of every task was the same size either way. It invented a call as text, and globbed `17 * 23` in the shell, as it has before (26g, 26h).
- **The rule's literal "how"** sends small talk like "Hello! How are you today?" to thinking -- a known cost of the confirmed rule, measured, not changed.

**Tests**: 37 new cases:
- `auto`: the rule's table and the judge, with yes, no, anything else, unreachable, and no judge named;
- the loop: deciding once and setting every request, and a backend's budget status reaching the reporter;
- the local backend's switch, its budget, both notices;
- each vendor's table and its wire body;
- the config, the editor, `add-backend`, the admin create and the session round trip;
- `/think`, `--think` end to end through the mock's new `{{thinking}}` placeholder, and `serve` applying the backend's default;
- the three reporters, the view, the template reading, `models info`.

**Guardrails, each mutation-tested (36 mutants, all caught), in a separate git worktree:** the rule's boundary, words and operators; the judge's verdict, failure and own thinking; the budget dropped, kept when off, its prefill, its once-only notice and status; each vendor mapping; the config bound and refusal; the editor, the session, `/think`, each reporter, the view, `models info`, `serve`'s default.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| The default | `on` unless configured; `auto` opt-in *(confirmed by the user)* | Today's behaviour. The measurement above keeps `auto` opt-in. |
| Judge-less `auto` | Over 200 characters, or code, maths, why or how *(confirmed by the user)* | Measured against the battery and ten chat questions, above. |
| A budget | None unless chosen *(confirmed by the user)* | A budget changes answers. |
| Qwen3.8-27B | Only where nothing else works *(the user's call)* | Gemma 4 and gpt-oss carry the switch the acceptance needed. |
| Anthropic | Off sends nothing; a budget at the 1024 floor, room above it, no temperature | The API's own rules. |
| OpenAI | Off is each model's least effort; nothing to a model that does not reason | The API refuses an effort a model does not take. |
| Gemini | Off is 0, 128 on Pro; nothing before 2.5 | Pro cannot stop thinking; older models take no thinking config. |
| A format with no reasoning tags | No budget, said once a conversation | Nothing to count; every step saying it would be noise. |
| `serve`, `analyze`, legacy completions | The backend's setting; no per-request override | Mode parity. An override is named, not built. |
| The session's keys | `think`, `think_budget` | A setting, not reasoning: the cleanliness check still finds no `thinking` in a saved file. |

### 2026-10-04 — `persistent-prompt-cache` (backlog item 26j): a prompt cache that survives the process

**Why.** The KV cache lived only as long as the process. `chat --resume` on a long conversation read the whole transcript before its first token -- 10,000 tokens was about 100 s on the reference machine's 27B -- and every new `--tools` chat read the same system prompt and tool definitions again. llama.cpp can save a sequence's state to a file and restore it, hybrid running state included.

**What was built**

- [x] **Two caches under `cache/prompt/`** (`backends/prompt_cache.h/.cpp`, `contracts/layout`'s `prompt_cache_dir()`):
  - **the prefix cache**, per model: the state after everything before the first user message -- the system prompt, the environment note and the tools offered -- named by those tokens, the cache type and the window, kept from 512 tokens;
  - **the chat cache**, per chat: the state a chat reached, saved at a clean exit (terminal and machine mode, never Ctrl-C) and after the turn that reads a compacted history, from 2,000 tokens.
- [x] **The seam** (`LlamaContext::save_state`/`load_state`, over `llama_state_seq_save_file`/`load_file`). A load clears the context first, and is kept only when the sequence ends where its tokens do with its window whole.
- [x] **A fresh session context starts from disk** (`LlamaCppProvider::restore_session`): the resumed chat's state, else the prefix; an unsaved prefix is read on its own and kept. The `--verbose` cache line says where the kept tokens came from: `11482 from the saved chat, 27 read`.
- [x] **A chat is saved where its next prompt will agree with it**: `kCheckpointTail` tokens short of its last prompt's end -- a thinking model's next prompt re-renders the answer, and a restored state has no checkpoints to go back to.
- [x] **A cache that cannot be used is discarded, never trusted**, each with one line:
  - one made with another model file -- its path, size and modification time;
  - one made with another cache type or window;
  - one llama.cpp refuses;
  - a saved chat that matches only in part, after what matched is used.

  A model file that changed clears its whole prefix directory.
- [x] **Bounded and private**: one 4 GiB cap across both, the least recently used evicted first and never the file just kept; every file `0600`, every directory `0700`. `check` reports the total against the cap.
- [x] **Only where a restore is exact** (`LlamaContext::restores_exactly`): not a sliding-window model, said once.
- [x] **Through the Harness as a capability** (`ConversationCaching`): `chat` names its conversation at the start and on `/model`, and saves it. Side requests never read or write the cache. The mock takes part as a test vehicle: `{{conversation}}`, and a save that says it kept nothing.

**On real weights** (separate `apogee` processes, one family at a time -- surviving the process is the point; the mutation build ran beside the last checks):

| | Qwen3-VL-8B (Q4_K_M) | Llama 3.1 8B (Q4_K_M) | Gemma 4 12B (Q4_K_M) | gpt-oss-20b (F16) |
|---|---|---|---|---|
| a new `--tools` chat, its second launch | 1,615 of 1,628 tokens from disk; first byte 0.9 s, was 4.2 s | no prefix file: the template writes the tools into the first user message | 1,408 of 1,425; 1.3 s, was 4.4 s | 1,097 of 1,108; 1.5 s, was 2.5 s |
| greedy answer, restored against read | identical | identical | identical | identical |
| an 11,500-token chat resumed | **0.8 s to the first byte, was 28.5 s** | **0.8 s, was 103.5 s** | 1.2 s, was 39.6 s | 2.6 s, was 26.8 s |
| greedy answer after the resume | identical | identical | identical | **"Nonsense" against "Nonsense."** |
| logits after a restore (llama.cpp's own API) | identical to the last bit | identical | up to 0.16 apart | up to 0.06 apart |
| now | both caches | the chat cache | **none, said** | **none, said** |

- **The rule that came of it.** A restored hybrid -- Qwen3.8-27B, the only hybrid installed, run once for this ("only where nothing else works") -- is bit-exact like a pure-attention model. A sliding-window cache is written as its window alone and laid out afresh, so its sums run in another order: deterministic on each side, never equal. Correctness first, so Gemma 4 and gpt-oss keep no cache on disk; the Gemma and gpt-oss times above were measured before that rule, and lifting it is one line.
- **Replacing the model file** (an APFS clone of Qwen3-VL-8B, retouched): the next resume said both lines, once each -- the saved chat discarded and the prompt cache cleared -- read the conversation again, and saved it anew.
- **With tool selection (26g)** the tools offered depend on the question, so a prefix is shared by questions that rank the same tools.

**Tests**: 27 new cases:
- the cache's files: fingerprints, a changed model's directory, names, ids that cannot escape, records, privacy, eviction order;
- the provider across "processes": the prefix read once and restored; a short prefix not kept; a replaced file, a refused file and one holding other tokens each discarded with its line, a refused one gone even when it cannot be written again; side requests untouched; a resumed chat restored privately, a short one not saved, and one from another file or window, or no longer matching, discarded; the save point; the cap; sliding windows; the config mapping;
- the Harness reaching the capability, and nothing where there is none;
- `chat` naming, resuming and saving at exit and after compaction, end to end through the mock;
- `check`'s row.

**Guardrails, each mutation-tested (29 mutants, all caught -- three only once tests were added for them: the eviction order, a saved chat's window, a refused prefix that cannot be written again), in a separate git worktree:** the model check and its directory, names by cache type, ids, eviction order and the kept file, a chat's record, privacy, the modification time; the chat's model, window and restore, the prefix floor, its tokens, a refused file, a partial chat, the prefix save, the chat floor and save point, both sliding-window rules, the config mapping; `chat`'s naming and both saves; the Harness both ways; `check`'s row; the mock's placeholder.

**Found on the way.** A restored sliding-window state was refused by our own window check: llama.cpp keeps exactly the window behind the last position, and llama-server's threshold, which `window_intact` follows, is one position stricter. A restore is now judged as of its last position, and a trim that cuts nothing returns at once.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| The two caches | Prefix on; the chat cache from 2,000 tokens *(confirmed by the user)* | Short chats re-read in a second or two. |
| The cap | 4 GiB across both, oldest first *(confirmed by the user)* | |
| When a chat is saved | Clean exit and after compaction, never per turn *(confirmed by the user)* | The per-turn save already keeps the transcript. |
| The acceptance models | The families, not Qwen3.8-27B *(the user's call)* | The 27B ran once, as the only hybrid, for the exactness probe. |
| The model file's identity | Path, size and modification time | Hashing gigabytes per load would cost more than the cache saves. |
| The prefix | Everything before the first user message, from 512 tokens | Shorter reads in under a second. |
| The save point | `kCheckpointTail` short of the last prompt's end | Where the next prompt is sure to agree. |
| A partial match | Used for what matched, then discarded with a line | The file no longer describes the chat. |
| Sliding-window models | No cache on disk, said once *(for veto)* | Their restore is not exact; the item puts correctness first. |
| The mock | Takes part as a test vehicle | So `chat`'s naming and saving are tested end to end. |

### 2026-10-04 — `speculative-decoding` (backlog item 26k): measured, and not built

**Why.** Generation is the slowest part of a local answer, and llama.cpp at the pin drafts several tokens cheaply and verifies them in one pass of the large model: a model's own multi-token-prediction head (MTP), a small draft model of the same family, or n-grams from the text already in context. The first look (2026-09-25) showed no win, so this item was a measurement first, built only on a clean 1.3× on one acceptance model across prose, code and editing.

**What was done.** `tests/scripts/py/speculative_bench.py` -- run by hand, never by the suite -- drives a `llama-server` built from the pinned llama.cpp:
- each method on its own server, the same three tasks: prose, code, and a copy-heavy edit;
- greedy, thinking off, one short warm-up request first;
- one run per task -- a second would let an n-gram drafter copy the first run's answer, which a first version of the script did, and measured the repeat instead of the method;
- generation tokens per second, the share of drafted tokens accepted, and whether the text matched the run with no speculation.

**The measurement** (2026-10-04, M3 Max; the GPU was not idle -- the desktop's own apps drew on it -- so each method was run against its own fresh baseline, minutes apart):

| Model | Method | Prose | Code | Edit |
|---|---|---|---|---|
| Qwen3-VL-8B Q4_K_M | n-gram | 0.84× | 0.85× | 1.84× (91% accepted) |
| Llama 3.1 8B Q4_K_M | n-gram | 0.94× | 0.90× | 1.87× (91%) |
| | draft: Llama 3.2 1B | 0.45×, **output differs** | 0.61× | 0.64× |
| Gemma 4 12B Q4_K_M | n-gram | 1.00× | 1.09× | 2.23× (88%) |
| | draft: Gemma 4 E4B | 0.30×, **output differs** | 0.63× | 0.73× |
| gpt-oss-20b F16 | n-gram | 1.07× | 1.00× | 2.17× (93%) |
| Qwen3.8-27B Q4_K_M | MTP | 0.80×, **output differs** | 1.08× (88%) | 1.14× (99%) |
| | n-gram | 0.88× | 0.78× | 1.73× (91%) |

- **No method clears 1.3× on all three tasks on any model**, so the build half is not done: no `speculative:` setting, no draft-verify loop. The item is closed as measured.
- **Copying is the one win.** N-gram drafting nearly doubles an edit's speed on every family -- 1.7× to 2.2× -- and costs prose and code 0 to 22%. A small draft model costs everywhere: on Apple silicon the draft's own passes are not cheap enough.
- **MTP held its 2026-09-25 shape**: 99% accepted on the edit and still only 1.14×. A hybrid model rolls back its recurrent state for every rejected draft.
- **Greedy output was not always unchanged.** Three methods changed a prose answer under greedy sampling: the verifier checks drafts in a batch, and a batch's arithmetic is not one token's. The item required unchanged output, so a build would have had a second problem besides speed.
- The draft models were the families' installed small models: no Qwen3 small enough is installed, and none was downloaded (the standing rule). Qwen3.8-27B ran once, for its MTP head -- "only where nothing else works" (the user's call).

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| The ship bar | A clean 1.3× on one acceptance model across all three tasks *(confirmed by the user)* | Below that the complexity is not repaid. |
| The methods | MTP, a small same-family draft, `ngram-mod` *(confirmed by the user)* | The pin's three that need nothing new. The draft is the family's installed small model -- Llama 3.2 1B, Gemma 4 E4B -- since no Qwen3-0.6B is installed and none is downloaded. |
| The models | The families, and Qwen3.8-27B for MTP alone *(the user's calls)* | The 27B is the only installed model with an MTP head. |
| The GPU | Not idle; each method against its own baseline, minutes apart | Nothing here controls the desktop's apps. No method came near the bar, so the noise does not change the answer. |
| The outcome | Closed as measured, not built; the script kept for the next pin | A later llama.cpp, or an edit-only mode, may change this. |

### 2026-10-04 — `base-model-sessions` (backlog item 26r): a base model's session, honest and clean

**Why.** The user's transcript (2026-10-03, `Gemma4-E4B-Q4KM`, a base model pulled that day) showed a session the product *knew* was compromised and let limp anyway. There was one dim warning at the top. Then came a fabricated temperature, a Bitcoin price ending `67,200.000000000005`, and invented playoff results, each presented like a real answer. Turn-marker fragments (`<|end|`, `<|end|><|im|`) spilled onto the screen, and `--tools` stayed offered to a model the warning itself said cannot use them. The 2026-09-28 fix ("Base models said, and stopped", above) said the warning once and stopped the guessed framing's *own* markers. A base model writes every family's markers, whole or cut short, and one warning scrolls away. Framing, never a gate: the session still runs.

**What was built**

- [x] **The fact, as plain data** (`ModelBehavior::base_model`). It is set by the local backend from the file's header (no chat template), so asking needs no model load.
- [x] **The spill, closed at the source** (`backends/markup_filter`, `TurnMarkerFilter`). It runs in the backend's one funnel, after the guessed framing's stops, for a model with no template only. The screen, the returned answer, the saved session and machine mode therefore see the same bytes.
  - A known family's marker ends the reply, as the guessed framing's own do. That holds whole (`<|im_end|>`) or cut short with two letters at least (`<|end|`, `<|im|`).
  - The families live in the profile registry (`base_turn_markers()`): ChatML's `<|im_*|>`, the `<|end|>` style and the `<|eot_*|>` style.
  - A marker of no known family (`<|fiap|`, seen on this model) is dropped where it stands, and the reply goes on.
  - A `<|` fragment still open at end of stream is never emitted. A `<|` that starts no name (`a <| b`) is text.
  - A model with a template is never filtered, so its literal `<|end|>` stands.
- [x] **Tools honestly off** (`agentloop/loop`). A base model's turns run with no tools, no tool selection and no `ask_user`, on every surface, from one rule in the loop.
  - `chat --tools` and `complete --tools` say once, at the start: `tools off: <model> is a base model, with no tool format to call them in -- it answers without them`.
  - Machine mode adds nothing to its stream; the backend's own notice already says "cannot use tools".
- [x] **The state, all session.**
  - The banner says `<model>  ·  base model  ·  chat …`.
  - The spinner says `Thinking… · base model` while a step waits (`CliReporter::set_resting_label`).
  - `/model` to a base model says `switched to X -- a base model` and, with tools, the tools-off line.
  - No answer is decorated.
- [x] **One wording** (`models::base_model_note()`) shared by `models info` and the conversation's warning. It gains the missing sentence: what a base model says "can be confidently wrong".
- [x] **Tests**:
  - **The filter's replay fixtures:** the motivating patterns, each family, cut-short markers followed by more text, every two-point split of the stream, the end-of-stream fragment, unknown markers, and the nameless `<|`.
  - **The backend:** the spill reaches neither the answer nor the stream, and the two match; a templated model's `<|end|>` stands.
  - **The loop:** no tools and no `ask_user` for a base model; both for an instruct one.
  - **`chat_test`:** `chat --tools` and `complete --tools` with a base model, with zero tools in every request and `tools off` said once; an instruct model keeps its tools, unsaid.
  - **The spinner's label**, and the shared wording in `models info`.
  - **The PTY check's `base-model` case:** the banner, the spinner and the one line on a real terminal.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| Framing, not gating | **The session runs, ungated** *(recorded 2026-10-03)* | The open-models principle. |
| The indicator | Banner plus the spinner's resting state *(default, confirmed)* | Visible all session; never on an answer. |
| Where the families live | The profile registry *(default, confirmed)* | New ones join the registry, not the filter. |
| Machine mode | **No new events or fields**; the answer it streams is the backend's one answer *(default confirmed, read this way: flagged for veto)* | The filter is in the backend's one funnel, as the item asks ("one filter home"), so every surface and the transcript see the same bytes. Machine mode's protocol is unchanged; the text it carries no longer holds the spill. |
| The warning's sentence | Added *(default, confirmed)* | A base model's answers are continuations. |
| A marker of no family | **Dropped where it stands, the reply going on** *(group run, flagged for veto)* | The guardrail says no `<|` fragment reaches the screen. Gemma 4 E4B wrote `<|fiap|` mid-answer. Ending the reply there would cut text the user may want. |
| Cut short | **Two letters at least** *(group run)* | `<|im|` is ChatML's; `<|e` could be anything, and is dropped as noise rather than ending the reply. |
| Where tools are withheld | **The loop, for every surface** *(group run)* | Mode parity: the rule lives once. Each surface says it where it starts; machine mode already hears the backend's notice. |
| What counts as a base model | **No chat template in the header** *(group run)* | The fact the 2026-09-28 fix already used; never a guess from a name. |

**Verified on real weights.** Google's Gemma 4 E4B base (Q4_K_M) is the transcript's own model. It ran a four-question `chat --tools` session (temperature, Bitcoin, the NBA finals, what to wear) on the binary before this item and after it, twice each:

| | Before | After |
|---|---|---|
| `<\|` fragments on screen and in the saved session | 5, then 3 (`<\|fiap\|`, `<\|and Mark`, …) | **0 and 0** |
| Tool calls | none (the model imitated, never called) | none, and none offered; `tools off` said once |
| The warning | without the confidently-wrong sentence | `base_model_note()`'s words |
| Saved answers equal to what was printed | yes | yes |

The answers stayed what a base model writes: continuations, often confidently wrong, streamed ungated. Meta, Qwen and OpenAI have no base build installed, so those families were skipped.

**Guardrails, each mutation-tested (17 mutants, all caught).**
- **The filter:** no family known; cut-short markers unknown; one letter enough; a lone `<` not held; an open fragment flushed; a family-less marker kept; a nameless `<|` dropped.
- **The families:** no ChatML.
- **The backend:** the filter never built; the end of stream unfiltered; never a base model.
- **The loop:** tools offered to a base model; `ask_user` kept.
- **The surfaces:** the spinner's label unused; `tools off` unsaid in `chat`; unsaid in `complete`.
- **The wording:** no confidently-wrong sentence.

**Not verified.**
- **The banner and spinner on real weights:** checked on a real terminal with a mock base model (the PTY case), not with Gemma, which ran on a pipe.
- **A base model with a forced `chat_template:`** is still treated as base, since the header still has no template. That case was not exercised.

## Milestone K — The install contract

**Goal.** Make v0.1.0 shippable, and do it by closing a whole bug class rather than by documenting it: *silent install drift*, where `make install` seeds one tree, `install.sh` another, the updater a third, and `check` validates a fourth — each list correct when written, diverging one commit at a time, and never failing loudly. The fix adopted here is structural: one layout declaration, and every install path reads it.

### 2026-09-19 — CI trimmed to the builds, and the first pull request's runner fixes

The first PR into `stable` was the first time CI ran the five non-macOS targets at all (the 2026-08-31 trigger change had made runs PR-only), and most of the checks failed or sat queued. Two decisions and four fixes came out of it, the user's call on the shape: **CI builds the executable for each platform, plus the llama.cpp job that ships with the tool, and nothing else** -- the format+clang-tidy job and both install-parity jobs are gone; formatting and lint are local gates (`make format-check`, `make lint`), and `cli.install_parity` was already a case in the suite every platform build runs. The fixes, each reproduced or confirmed against the runner images' own manifests: the Linux images ship no libcurl headers, so `find_package(CURL REQUIRED)` -- the first hard lookup -- killed the configure five seconds in (reproduced in an Ubuntu 24.04 container; `libcurl4-openssl-dev` fixes it); the Windows images ship no libcurl at all, so it comes from the image's vcpkg as a static library on the `*-windows-static-md` triplet, handed to the script through the new `APOGEE_CMAKE_ARGS` hook; `macos-13` was retired, so the x64 Mac job queued forever and now runs on `macos-15-intel`; and the Windows ARM job had "passed" in twenty seconds because Git for Windows' x64 bash reports the shell's architecture from `uname -m`, the script took the host for x64, deferred the ARM target "to CI" and exited 0 -- the script now reads the machine from the processor variables on Windows, and every runner passes the new `--no-defer`, which turns a deferral into a failure. The release workflow carries the same runner and prerequisite changes. **Running the suite on Linux for the first time found four things macOS had hidden**, each fixed here: GCC enforces C++20's designated-initializer order, which Clang only warns about (five initializers of `BuiltInToolOptions` in `analyze.cpp` and the permissions test were out of order); the Hugging Face byte source captured its cancellation token *by reference* in the closure it returned, so a caller's temporary token dangled -- glibc's allocator reuses the memory at once and every download read as "cancelled" (`source_hf.cpp` now captures by value; the token is a shared flag); the retrieval turn built its graph query as `cond ? "" : turn.question`, whose common type is `std::string` -- a temporary the `string_view` dangled on, so the entity search received garbage and the graph expansion was silently empty on Linux (`rag.cpp` now binds views on both arms); and two shell checks used BSD `stat -f` with a GNU fallback that also *prints* on GNU, so the mode comparison could never match (`mode_of` picks the flavour). The HTTP-API conformance script also needed `cmake_policy(SET CMP0057 NEW)` for `IN_LIST` under CMake 3.28 in script mode. **And the Intel Mac is gone** (the user's call, the same day, in any capacity): `macos-13` had been retired under it, and rather than move it to `macos-15-intel` the target was dropped from the matrix, the presets, the script's target list, the platform vocabulary and the installer, leaving five targets -- Linux and Windows on both architectures, macOS on Apple silicon.

**The second push, the same day, and the pipeline's real shape.** That first round of fixes went up and the merge-blocking job died in seven seconds, both Windows jobs in one, and both Linux jobs at the end of their suites; the user set the shape of CI in four calls while it was being taken apart. **macOS:** the runner's bash is 3.2 (the image manifest says so), and under `set -u` bash 3.2 treats an empty array's `"${a[@]}"` as an unbound variable -- the new `env_args` expansion was empty on every non-Windows runner and killed the script before configure. Linux (bash 5) and MSYS2 never see it; `cicd.sh` now expands its arrays as `${a[@]+"${a[@]}"}`. **Windows builds with MinGW-w64** (user decision): the Visual Studio generator and the vcpkg curl are gone; `msys2/setup-msys2` prepares the MSYS2 that both Windows images ship off PATH -- GCC in UCRT64 on x64, clang in CLANGARM64 on arm64, with cmake, ninja, pkgconf, MSYS2's git and the Schannel-built `curl-winssl` -- and the build and test steps run in that shell, which `cicd.sh` now recognises as a Windows host. The Windows presets are Ninja, linked `-static` against a static libcurl so the executable ships alone; FindCURL's imported target names only libcurl on MinGW, so `ApogeeDependencies.cmake` reads curl's static dependency closure from pkg-config and puts it on the target, resolving the alias curl's own CMake config makes of `CURL::libcurl` first. Cross-compiling the x64 target on macOS with Homebrew's mingw-w64 (GCC 16) against a from-source static Schannel curl found two dependency age marks before the runner could: yaml-cpp 0.8.0's `emitterutils.cpp` uses `uint16_t` without `<cstdint>` (the vendored target is compiled with `-include cstdint`), and cpp-httplib's non-blocking resolver calls `GetAddrInfoExCancel`, which the mingw-w64 headers do not declare -- that option is now off on every target, since httplib is only ever the server and Apogee's client is curl. Every target in the tree, tests included, compiles and links under that cross-compile; the ARM64 clang build and the suite's run on Windows are the runner's to show. **The clean-room job is gone** (user decision); `cicd.sh --fresh` remains a local command. **Three stages** (user decision): `clone llama.cpp` proves the pin resolves (`cicd.sh --clone-llama` reads it from `third_party/CMakeLists.txt`, the one place it lives, and fetches that commit at depth one), then `build <platform>` -- one row per target -- each packing its build tree into a tar artifact (tar because `upload-artifact` drops file modes; object files and clone histories stay behind), then `test <platform>`, the same rows running `cicd.sh --test-only` on that tree with nothing rebuilt. The two matrices are the same list on purpose; GitHub offers no way to declare one. **And every one of those jobs is required** (user decision, 2026-09-19, superseding 2026-08-24's "macos-arm64 only" gate): no `continue-on-error` anywhere in the file -- a platform that cannot pass is a platform to fix -- with the branch protection rule on `stable` listing the checks by job name. **llama.cpp is in every build** (user decision, the same evening, on asking why a separate `macos-arm64-llama` row existed at all): `APOGEE_ENABLE_LLAMA` had defaulted to OFF since the pin landed, with one CI job carrying it ON -- which meant the release workflow, building through the presets, would have shipped executables whose local inference answered "not built in". The default is ON now, every platform build and test run links it, the extra row is gone, and `make no-llama` is the developer's fast build. Turning it on everywhere exposed two more things a single opt-in job had hidden: llama.cpp's own `BUILD_SHARED_LIBS` default leaks ON into the cache for every subproject configured after it (the llama-enabled executable needed libllama, libggml, libmtmd, replxx and the schema validator as dylibs beside it), so the top-level build now forces it OFF before any dependency; and ggml builds `-march=native` by default, so a release built on a runner with AVX-512 would die on a user's older CPU -- `GGML_NATIVE` is off (AVX2/FMA/F16C on x86-64, armv8-a on arm64, armv8.2-a+fp16+dotprod on Apple silicon), OpenMP is off in favour of ggml's own pool, and the Metal library stays embedded. Cross-compiling it for Windows found one more: upstream's `mtmd-audio.cpp` uses `M_PI`, which MinGW's math header hides under strict `-std=c++20`, so the `mtmd` target gets `_USE_MATH_DEFINES` on Windows. With the option on by default: the macOS suite passes in full with llama.cpp linked (a 17 MB executable with no library beside it, the Metal library embedded), the Ubuntu container passes in full, and the MinGW x64 cross-build links an executable carrying the ggml and llama symbols and importing only Windows system libraries. A lesson from that last check, recorded in the presets: a preset's pinned compiler is a cache entry and beats a cross toolchain file's, so the cross-compile must name its compilers on the command line, or `gcc` on a Mac quietly becomes Apple clang and the "Windows" build is a macOS one. **And a local build fix found by the user's `make fresh`:** Apple clang, given no sysroot, falls back to the Command Line Tools' `MacOSX.sdk` symlink even when xcode-select names an Xcode, and a CLT newer than that Xcode (27.0 against Xcode 26.6) ships stubs its linker cannot read -- every link failed with "tapi error: malformed file". The CLI build root now sets `CMAKE_OSX_SYSROOT` to `macosx` before `project()` when nothing else names an SDK, so CMake asks xcrun and gets the selected toolchain's own SDK; a toolchain file, `SDKROOT`, or `-DCMAKE_OSX_SYSROOT` still win. **The Windows ARM job's real failure, found on the third look:** both ARM runs had died one second into the build step, and the first diagnosis -- the shell reporting its own architecture -- was half right. The runner's MSYS2 is an x64 build running under emulation, and for an emulated *64-bit* process Windows sets no `PROCESSOR_ARCHITEW6432` at all (it exists for 32-bit processes only), so the "processor variables" fix read AMD64, the script took the host for windows-x64, and `--no-defer` did its job: "cannot build windows-arm64 natively on this host". On Windows the script now takes the answer from `MSYSTEM`, which names the toolchain the shell was set up for -- CLANGARM64 is ARM64, UCRT64/MINGW64/CLANG64 are x64 -- and only falls back to the variables without one; an x64 shell then builds x64, which is what its toolchain does anyway. Two things make the next such failure readable without admin rights: `die()` emits a workflow error command on a runner, and every build and test step tees its output and, when it fails, hands the log to the new `lib/scripts/ci-annotate.sh`, which publishes the ctest summary and each failed test's own output as error annotations -- annotations are public on a public repository, the raw log is not. (The first version put the last forty raw lines in one annotation; ctest's dot-padded lines overflowed GitHub's 4 KB limit before the summary, which is how the script learned to compact and split.) **With every build green on the next run, the test stages spoke for the first time:** macOS passed, and the two Windows rows failed the same thirty-five tests -- eight of them the byte-exact pins of embedded assets to their shipped files. The repository had no `.gitattributes`, Git for Windows checks text files out with CRLF, and a text-mode read of a CRLF file on Windows gives back LF while the embedded copy keeps the CRLF. `* text=auto eol=lf` now, on every platform: the bytes a pin compares are the same bytes everywhere. The rest of the Windows list is the portability gap the release workflow's header has recorded since 2026-09-01 -- POSIX modes, path semantics, program resolution -- now measured rather than presumed, and every one of the thirty-five was run to a named cause and fixed the same day, four of them in the source: `find_on_path` split PATH on ':' (which cuts every Windows drive letter in half) and never tried PATHEXT, so nothing on Windows was ever found -- it splits on the platform's separator now and tries `.exe` and its siblings for a bare name; `models delete` refused an absolute path with `is_absolute()`, which on Windows does not cover "/etc/hosts" (root-relative) or "C:x" (a drive, no root), so both would have walked out of the models directory -- any root is refused now; the model acquirer removed a failed transfer's partial file while the stream writing it was still open, which POSIX allows and Windows refuses with a sharing violation, so the partial survived there -- the removal follows the stream's close now; and three places wrote paths into contracts a model or a remote client reads back (`search_files` output, the ingest source name, the agent API's `prompt_path`), each in the platform's spelling, so a Windows machine recorded `nested\deep\file.md` for what every other host calls `nested/deep/file.md` -- all three use the generic form now. The tests: a case whose name began with `/branch` never ran on Windows, because Catch2 reads a leading slash as an option prefix there; the config editor's YAML quoting rule (a Windows `model_path` carries a drive colon and is written quoted) is public as `yaml_scalar` now, and the byte-exact train pins go through it; the doctor tests expect the recorded `Skipped` where the platform has no POSIX modes and put the fake venv interpreter where the platform's layout puts it; a ledger fixture is built as JSON rather than by concatenating paths into a string; the "unwritable path" is a path under a regular file, unwritable everywhere; and the tests that need a spawned child or a POSIX `/bin/sh` say so. The three scripts: the no-listen symbol scan accepts `.obj` members and Winsock's `__imp_` decoration, the install-only check expects `apogee.exe`, and the config lifecycle compares paths in CMake's spelling. All of it verified as far as a Mac allows -- the suite here and in the Linux container, and the MinGW cross-compile of every Windows-only branch. **And then the test stage was removed** (user decision, 2026-09-20: "remove the tests from the CI/CD; if we are going to run tests, we should just run the tests for the source code"). CI is the clone and the five builds; the release pipeline builds and packages without the suite and still proves the staged binary runs; `--test-only` went with the stage. The suite is the developer's gate -- `make test`, `cicd.sh --test` -- run against the source before a push, on every platform the developer can reach. The Windows fixes stay: they are correct, and the suite is still run on Windows, by a person. **And a suite bug the developer's terminal found:** two command tests -- knowledge capture reading a piped conversation, and `datasets prepare` refusing on a pipe until the Python environment exists -- passed under ctest on every runner and in every container and failed in Taylor's terminal. The fixtures feed "piped" input by swapping `std::cin`'s buffer, while `stdin_is_piped()` asked the operating system about descriptor 0; the two agree only when nothing runs the suite from a terminal, which is to say everywhere except a developer's own shell. `stdin_is_piped()` now also reports true when `std::cin`'s buffer is not the one it started with, which is the truth of the matter: the input is read from there. The Linux runners also gain `lsof`, which the Ubuntu images lack and without which the no-listen scripts skip their socket poll and pass having checked nothing. The release workflow follows the MinGW change, and its verify step now runs on every target and outside the MSYS2 shell, which is where a static link is proven. **The Linux runs' failure is the one thing this round did not close.** Both Linux jobs built and then left `ctest` with exit code 8 -- failures in the suite -- and the job logs need admin rights the session did not have. The same command, as a non-root user with the runner's environment, in the Ubuntu 24.04 container passed all 1445 (the earlier "reproduction" had only rerun the parallel flakes serially); `stable` is the initial commit, so the PR's merge ref is the branch head; and the one prerequisite the image lacks, `lsof`, makes the no-listen scripts skip rather than fail. Whatever it is, it is now a required check: the test stage's log names the cases, and that is where the next round starts.

### 2026-09-25 — Releases from the pull request's own build

**Goal.** Two user decisions, given together: the release workflow must not run when `stable` is pushed, and the release a merge publishes must be the artifacts the pull request's own CI run produced, tagged with the executable's version. Until now a merge into `stable` started `release.yml`, which built all five targets a second time from the merge commit. Those were binaries nobody had tested as such, and they took about eighty minutes of runners to produce.

**What was built**

- [x] **`.github/actions/package/`**, a composite action and the one packaging step. It holds the staging, the "binary runs" check and the upload that lived in `release.yml`, now used by both workflows, so a release from a merge and one from a tag cannot be packaged differently. It adds `apogee-<target>.source` to each artifact: the commit and the source **tree** the archive was built from.
- [x] **CI packages every build.** Each `build <target>` job ends with the action, so every run keeps the five archives a release would ship. The check that the binary runs, which only the release path had before, now runs on every pull request.
- [x] **`tag and release`**, a CI job that runs on the `closed` event of a merged pull request into `stable` and on nothing else; every other job skips that event. It runs **`lib/scripts/release-from-pr.sh --publish`**, and each of these steps is a hard stop:
  - the pull request is merged into `stable`;
  - the version is read from `CMakeLists.txt` at the merge commit and its tag looked up. A tag at another commit means the version is out, and nothing is published (successfully, the docs-or-hotfix merge). A tag at the merge commit means an earlier attempt made it, and this one finishes the release;
  - the newest successful run of the pull request's head that holds an archive for every `build <target>` job it ran is chosen;
  - every archive's `.source` must name the merge commit's exact tree;
  - the runner's own binary is run, and `apogee version` must report that version;
  - the tag is created at the merge commit together with the release, in one `gh release create`.
- [x] **A rehearsal.** `workflow_dispatch` takes `release_rehearsal_pr`, and the same script then runs without `--publish`: every check against GitHub's test merge, a report of what it would publish, and no build and no write. The script runs the same way from a terminal with `gh` signed in.
- [x] **`release.yml` became the manual path.** It no longer runs on a push to `stable`; a pushed `v*` tag (`make release`) and the dry-run dispatch remain. Its publish step attaches only the archives, through a null-globbed list, so a Windows build it allows to fail no longer leaves a literal pattern for `gh` to choke on.
- [x] **Least privilege in CI.** The workflow's token is `contents: read`. Only `tag and release` asks for `contents: write` and `actions: read`, and it has a concurrency group of its own that is never cancelled.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| Release trigger | The `closed` event of a merged PR, not the push to `stable` | The event carries the pull request, which is what finds its run. It also means a direct push to `stable` never publishes. |
| Identity of "what was tested" | The source **tree**, not the commit | CI builds GitHub's test merge (`refs/pull/N/merge`). The merge GitHub then makes is a different commit, but with the same tree, as long as `stable` has not moved since the run. The tree check turns "the base moved after CI" from a silent mismatch into a refusal. |
| Where the tag comes from | `apogee version` of the downloaded binary, required to equal `CMakeLists.txt` | The user's words: the version of the executable. The equality check makes a disagreement a stop rather than a choice. |
| Which platforms | Every successful `build <target>` job of the chosen run | The released set follows the matrix with no list kept in the script. |
| Logic location | A script in `lib/scripts/`, with the workflow a thin caller | It can be tested with no runner, and it can be rehearsed from a terminal. That is the same reason `cicd.sh` and `ci-annotate.sh` exist. |
| One tag+release call | Kept from 2026-09-22 | No tag without its release. A tag made with `GITHUB_TOKEN` triggers no workflow, so the merge's tag does not also start `release.yml`. |

**Recorded consequence.** The released binaries were built from the test merge, so `apogee version` names that commit: the same tree as the merge on `stable`, a different hash, and one no branch points at.

**Verified.** `release-from-pr.sh` was driven through 13 scenarios (41 checks) against a stand-in `gh` that served canned API responses and real archives. The macOS archive held the real binary, so the version check ran the executable:
- publish;
- a rehearsal on an open PR;
- publishing an open PR refused;
- a version already released;
- a tag at the merge commit, both lightweight and annotated;
- a tree mismatch;
- a version mismatch;
- a missing archive;
- a wrong base branch;
- an archive with no binary;
- bad arguments;
- the Actions error and summary output.

The guards were then removed one at a time (tree, merged, version, other-commit tag, base branch), and every removal failed a scenario. The API shapes the script depends on (runs filtered by head and conclusion, job names, tag refs and their 404, raw file contents) were checked against the live repository. The package action's two shell steps ran locally for `macos-arm64`: the archive layout was unchanged, `.source` matched `HEAD`'s tree, and the binary passed `check`.

**Not verified.** Nothing here has run on GitHub's runners yet. The first real exercise is the rehearsal on this release's own pull request; the first publish is its merge.

**After the merge, the same day: it worked, and the rebuild on `stable` is gone.** The v0.1.2 merge (PR #3, `b343620`) published v0.1.2 through this path:
- the `closed` run (36200564148) ran `tag and release` alone, in 43 seconds, with every other job skipped;
- the release carries the five archives from the pull request's own run (36196338575), tagged at the merge commit.

The one piece of the old shape still in place was CI's `push: branches: [stable]` trigger, which had rebuilt and retested every merge commit from scratch (run 35813923174 after v0.1.1). The user's call: a merge must not start a fresh run from `stable`. The trigger is removed, and with it the `what changed` job's handling of push events, which nothing could reach any more.

Nothing depended on that run. The merged tree is the one the pull request's run tested, and `tag and release` refuses to publish if it is not. A direct push to `stable` now runs nothing, and the branch protection rule is what keeps changes arriving by pull request.

The removal governs from the next merge on: a push runs the workflow file of the commit pushed, and the next merge commit carries the change.

### 2026-09-25 — A pull request's CI run, rehearsed locally (`make pr-ci`)

Asked for directly (Taylor, 2026-09-25): "a make file command that can spoof the workflow when a PR is made." `act` was ruled out: it runs Linux containers, so it cannot run the macOS job that matters most here, or the Windows ones. The workflow is instead replayed natively for the host's own target, through the scripts the workflow calls.

**What was built**

- [x] **`lib/scripts/pr-ci.sh`**, behind **`make pr-ci`** (and `make pr-ci-clean`).
  - It fetches `stable` and makes **GitHub's test merge** of the branch into it, since that, not the branch, is what CI builds. A branch that already contains `stable` is its own test merge; a conflicting one stops the run, as GitHub runs no CI on it.
  - It runs `ci.yml`'s pull-request jobs on that merge, in the workflow's order: `version bump`, `clone llama.cpp`, `unit tests <host>` with llama.cpp off, then `build <host>` with its packaging. The build runs only when the clone and the unit tests passed (the workflow's `needs`); `version bump` gates only the verdict.
  - It prints a checks-style summary, names the four targets this host cannot build as CI's own, keeps a log per job, and exits non-zero whenever CI would fail.
  - The jobs run in two git worktrees under `~/.cache/apogee/pr-ci` (`APOGEE_PR_CI_DIR`), one per job as on the runners: the unit build has llama.cpp off and the build has it on, so one shared build directory would rebuild half the tree at every switch. They are reset to the new test merge each run and keep their ignored build directories, so every run after the first is incremental. The developer's own checkout and build directories are never touched.
  - Uncommitted changes are left out, because a pull request carries commits; a note says so. **`UNCOMMITTED=1`** (`--uncommitted`) rehearses them anyway, as a commit object built through a scratch index that no branch, stash or reflog points at. `BASE=<ref>` merges into another base.
- [x] **Two steps out of the YAML, so they run anywhere.**
  - **`lib/scripts/package.sh`** is the staging, the `.source` record and the proof that the binary runs, moved out of `.github/actions/package`. The action now calls it and adds only the upload; `pr-ci.sh` calls it too. That keeps "one packaging definition" true: CI, the manual release and the rehearsal cannot package differently.
  - **`lib/scripts/version-check.sh`** is `ci.yml`'s inline `version bump` step. It uses `gh release view` whenever a token is in the environment or `gh` is signed in; without `gh` it asks `origin` for the tag, which agrees because a release and its tag are only ever created in one call.
- [x] **`cicd.sh --host`** prints the native target, so the host is detected in one place.

**Verified.**
- `version-check.sh` passes on this branch (v0.1.2 unreleased) and refuses a checkout of `stable` (v0.1.1 is released).
- A synthetic base editing the same `VERSION` line stops the rehearsal, naming the conflicting file and the fix.
- A synthetic base that only adds a file produces a two-parent test merge carrying both sides, with both worktrees on that one commit.
- `--clean` removes the worktrees and unregisters them.
- The whole run is exercised under bash 3.2, macOS's own `/bin/bash`, as well as bash 5.
- A full `make pr-ci UNCOMMITTED=1` on the reference Mac passed every job this host can run: the version bump, the clone, the unit tests (27,767 assertions in 1,587 test cases) and the build, packaged and run.

**What the first real pull request run found** (v0.1.2's own, run 21). macOS passed, and the other four platforms failed, which is exactly the gap a Mac-only rehearsal leaves.
- **Linux and Windows x64 did not compile the tests.** `tests/models/store_test.cpp` called `std::ranges::sort` without `<algorithm>`. Apple's libc++ reaches the header through others, and GCC's libstdc++ does not. The include is added.
  - To look for more of the same, every first-party file was compiled syntax-only by the MinGW GCC (`x86_64-w64-mingw32-g++ -fsyntax-only`, the real build's flags and include paths). That is the standard library the Linux and Windows x64 jobs use, and it takes the Windows branches of the code.
  - With the include removed, it reproduces the runner's error exactly; with it, all 362 files are clean.
- **Four test cases failed on Windows arm64**, and the job's public annotations could not say which. `ci-annotate.sh` still expected ctest's summary, but since 2026-09-22 CI runs the Catch2 binary directly. So a failing unit-test job published its last 25 lines: the totals, and no test's name. It now reads Catch2's own report: a summary annotation with the totals and every failed test case's name, then one annotation per failure (up to nine) with its location and each failed assertion and its expansion. Skipped test cases are left out, and CRLF logs are handled. Tested against a synthetic report in Catch2's format.

**Then, the same day: documentation-only pull requests, and a GCC compile in the rehearsal** (the user's calls).
- [x] **A documentation-only pull request builds nothing.**
  - CI's new first job, `what changed`, pipes the PR's diff (`base...head`, everything it changes) through **`lib/scripts/code-changed.sh`**, the one definition of documentation: `lib/documentation/`, `.claude/`, and Markdown at the repository root. Nothing CI runs reads those paths; the two ctest checks that pin reference docs to code are `make test`'s.
  - **It could not simply skip the jobs.** The previous run showed why: a matrix job skipped by its `if:` reports once, under the literal `build ${{ matrix.name }}`, so the required `build macos-arm64` would wait forever and the PR could never merge. So the matrix jobs and `clone llama.cpp`, which the builds need, always run and gate every **step** on the answer. Each finishes in seconds under its real name. `version bump`, not a matrix job, is skipped whole, and a skipped job counts as passing.
  - **`tag and release` does not ask.** On the `closed` event the payload's base can already be the merge, and a diff from there reads as "nothing changed", which could suppress a real release. A docs-only merge already publishes nothing, because its version is out. So `what changed` does not run on that event either.
  - It fails closed: if the job fails, the matrix jobs never report their names, and the PR cannot merge.
  - The PR is judged by everything it changes, so a docs commit pushed onto a code PR still runs everything. `[skip ci]` in a commit message is the way to push docs mid-review, recorded with its catch: the last commit must run.
- [x] **`gcc compile`, in `make pr-ci`.** **`lib/scripts/gcc-check.py`** compiles every first-party translation unit syntax-only with GCC's standard library, taking include paths and defines from a build's `compile_commands.json`, and borrowing only libcurl's headers from the host.
  - The compiler is the MinGW-w64 GCC when present (the Windows x64 job's own compiler, taking the code's Windows branches), else a real GCC, else "skipped" with the install line. It is never a failure for lacking one.
  - `pr-ci.sh` runs it after the build, on the build's compile commands (llama.cpp on, as CI's builds) or the unit tests' when the build never configured.
  - `pr-ci.sh` also asks `code-changed.sh` about the test merge, as CI does, and a docs-only rehearsal runs nothing and passes.
  - Its verdict now fails only on a job that ran and failed: a skip with its reason is not a failure.
- **Verified.**
  - `gcc-check.py` compiles all 362 files clean in about 27 seconds, and fails on `store_test.cpp` with its `<algorithm>` removed, with the runner's exact message. With no GCC on the `PATH` it exits 3 with the install line.
  - `code-changed.sh` answers "documentation only" for docs, skills and root Markdown; code for a Markdown test fixture; and code for this branch (299 of 327 files).
  - A rehearsal of a synthetic docs-only commit on top of the current code reported every job passing with no work.
  - The workflow parses. Its behaviour on GitHub is proven by the next docs-only PR.

**And the Windows failures, named at last** (run 22, the first with the Catch2 annotations). Linux passed on both architectures once the include was in. Windows x64 failed 7 test cases and Windows arm64 failed 4, all 4 among x64's 7, and none was a product bug except one message. Each was a POSIX assumption in a test:
- **The converter tests** made their fake interpreter at `venv/bin/python`. A Windows virtual environment keeps it at `Scripts\python.exe`, so the code found no environment at all. They now ask `PythonEnv::interpreter()`, as `check_test` already did.
  - They also wrote the "earlier Apogee's" converter in text mode, which on Windows stores `\r\n`, so its digest was never the retired one; they write binary now.
  - The product fix: the "converter is incomplete" message appended a bundled name written with `/`, so a Windows user read `C:\…\convert\gguf-py/gguf/__init__.py`. It is `make_preferred` now.
- **"A path outside the store is refused"** passed `/etc/hosts`, which exists on POSIX only. On Windows the refusal came from the name lookup, with another message. The test makes its own file outside the store now, and still checks that `/etc/hosts` is no model.
- **"A staging directory is claimed by its process"** kept the owner marker open in an `ifstream` while the commit had to delete it. Windows refuses to delete an open file, so the marker stayed. It is closed before the commit now.
- **Three tests aged a directory** with `std::filesystem::last_write_time`, which MinGW's libstdc++ cannot do: it goes through `_wutime`, which cannot open a directory ("cannot set file time: Permission denied"). libc++ on the ARM runner can, which is why arm64 passed them. `tests/support/file_time` (`set_modified_time`) falls back to the Win32 API there, with the FILETIME borrowed from a scratch file the standard call can stamp, so no clock is converted by hand.
- **Verified here as far as a Mac allows.** The affected tests pass on macOS (1,305 assertions), and `gcc-check.py` compiles all 363 files, the helper's Windows branch included, with the MinGW GCC. Nothing on this Mac can run a Windows binary (`wine` is an Intel build and Rosetta is not installed), so the run that proves them is the next pull request run.

### 2026-09-25 — The CLI pipeline: built only when the CLI changes, copied from the latest release when not

**Goal.** The user's call, made with the GUI applications in view: what CI and the release do today is the **CLI pipeline**, one pipeline per deliverable. It must run only when what the CLI is built from changes. When it has not changed, the CLI deliverables are copied from the latest release, so a future pull request that changes only a GUI application runs that application's pipeline and takes the latest CLI as it is. A release that leaves the CLI alone needs a name the CLI does not give it, so the release got its own version (the user chose it from two options):
- `lib/release/VERSION` names the release (first at the top of the repository; moved to `lib/release/` the same day, the user's call);
- the CLI's `project(... VERSION)` changes only when the CLI does, and then equals the release it ships in.

**What was built**

- [x] **`lib/release/VERSION`**, the release. It is one line, and deliberately not an input of the CLI's build, so bumping it alone runs no CLI pipeline.
- [x] **`lib/scripts/changed.sh`: what each deliverable is built from, declared once.** `changed.sh cli <from> <to>` answers `cli=true|false`. The CLI's inputs are:
  - `lib/src/cli/` and `lib/scripts/`, which the user named;
  - the CLI pipeline's own `ci.yml`, `release.yml` and package action, because a change to how the CLI is built must be exercised by building it;
  - `.gitattributes`, which sets the bytes the CLI's pinned assets are checked out with.

  A GUI application adds its own entry there. An empty `<from>`, or a commit the repository does not have, answers true: when in doubt, build. It replaces `code-changed.sh` and that morning's documentation-only rule, which it subsumes: documentation is simply not a CLI input.
- [x] **Against the latest release, not the pull request's base.** CI's `what changed` job diffs the latest release's commit (**`lib/scripts/latest-release.sh`**: published releases only, the commit as `origin` has it, never a local tag) against the test merge. A copy of the release is only true while the CLI that would merge is the CLI that was released, so that is the comparison. In the everyday case it agrees with the pull request's own diff.
- [x] **An unchanged CLI in CI.** The clone, the unit tests and the builds still run under their required names, with the build steps skipped (the reason the morning's rule found: a matrix job skipped whole never reports its name). Each `build <target>` then copies that platform's archive from the latest release (**`lib/scripts/cli-from-release.sh`**, through the package action's new `from-release` input) and runs the copied binary on its own platform. The copy goes into the same `apogee-<target>` artifact a build makes, so every run carries the CLI, built or copied, where a later job will look for it.
- [x] **`version bump` on every pull request.** When the CLI changed, `lib/release/VERSION` must be unreleased and the CLI's version must equal it. Otherwise the check passes and says what the merge will publish: a release with the CLI copied, or nothing.
- [x] **`release-from-pr.sh` publishes `v<VERSION>`.** The name comes from `lib/release/VERSION` at the merge. With the CLI changed since the latest release (the release being made excepted, for a retried attempt), it uses the pull request's own archives under the same checks as before, and the executable must now report that version. With the CLI unchanged, it downloads the latest release's archives, runs the host's binary to prove the copy starts, and publishes them again as they are.
- [x] **The manual path reads `lib/release/VERSION`.** `release.yml`'s gate and `make release`'s preflight compare the tag with it. The manual path still rebuilds the CLI even when it is unchanged; that is deliberate for an escape hatch, and recorded in the workflow's header.
- [x] **`pr-ci.sh` asks the same question.** With the CLI unchanged, it builds nothing and copies the host's archive from the latest release. It passes CI's answers to `version bump`.

**Verified.**
- The three scripts were driven through 56 checks in a throwaway repository, with a bare `origin`, real commits and tags, and a stand-in `gh` serving canned API responses and archives, under both bash 5 and macOS's bash 3.2:
  - `changed.sh` against CLI, documentation-plus-`lib/release/VERSION`, workflow and `.gitattributes` changes, and against no release, an unknown commit and an unknown deliverable;
  - `version-check.sh` through every rule;
  - `release-from-pr.sh` through a built release, a copied release, a documentation merge, a binary reporting the wrong version, a run whose archive was itself a copy, a rehearsal, an open pull request refused, finishing an earlier attempt, a latest release with no archives, and no release at all.
- Seven rules were removed one at a time (always copy, the empty-copy check, the release-being-made exclusion, the CLI-equals-`VERSION` rule, the released-`VERSION` rule, and two CLI inputs), and every removal failed a check.
- Against the real repository:
  - `latest-release.sh` names v0.1.2 at `b343620` without `gh`;
  - `changed.sh` calls this branch's version-bump commit a CLI change, and the skill-only commit `68d653c` not one;
  - `cli-from-release.sh` copied v0.1.2's macOS archive and ran it (it reports `apogee 0.1.2 (5827772, …)`, the test merge's hash, as recorded above);
  - `release.yml`'s gate script, run locally, passes a matching tag, refuses a mismatched one, and names the release on a dispatch.

**Not verified.** None of it has run on a runner. The copying build jobs on the Windows runners (`curl` and `7z` in Git Bash) are exercised first by the first pull request that leaves the CLI alone. `make pr-ci` was not run end to end, because this branch changes the CLI, so a rehearsal would be a full build.

### 2026-10-03 — Required checks, read from the pipeline

**Goal.** The user asked for merges into `stable` to need the pipeline green, whatever the approvals, and for a script that keeps the required checks current as the pipeline changes. The repository's one ruleset, "Stable", had a required-status-checks rule listing **no** checks. Its 1-approval rule cannot be met on one's own pull request, so every merge went through the admin bypass, and the bypass is all-or-nothing per ruleset: it covered the checks as well.

**What was built**

- [x] **`lib/scripts/required-checks.py`.** It reads the required checks from GitHub's own record of a pull request's CI. That is every job that ran and passed in its runs, across every workflow, with each matrix row expanded, named and pinned to the app exactly as GitHub reports the check. So nobody types them, and a renamed job, a new platform or a GUI workflow follows from one passing run.
  - The pull request is the newest open one into the default branch, else the last merged; `--pr N` picks one.
  - Its head's newest run of each workflow must have passed, and must not be still running.
  - A merged pull request's `closed` run is created at or after the merge, so it is ignored. `tag and release` is skipped on an open pull request, so it is never required.
- [x] **The "Stable: CI must pass" ruleset**, created or updated by `--apply`. It requires those checks with **no bypass list**, and the branch up to date with `stable`. An update replaces only the check list and keeps the ruleset's other settings. Without `--apply` the script prints the difference and changes nothing. The "Stable" ruleset keeps its review rules and admin bypass.
- [x] **The documented list now includes `what changed`.** It runs and passes on every pull request, and the rule "every job that ran and passed" no longer carves it out.
- [x] **Every place a pipeline change is made or reviewed says what moves with it** (the user's call: a change to the pipeline must not leave the required checks behind).
  - [DEVELOPER.md → Changing the pipeline](DEVELOPER.md#changing-the-pipeline) has the full list: the required checks re-applied before the merge, `pr-ci.sh` mirrored, the two matrices kept as one list, the job and artifact names `release-from-pr.sh` reads, no job-level `if:` on a required matrix job, `changed.sh` for new CLI inputs, and when `required-checks.py` itself must change.
  - The short version is at the top of `ci.yml`.
  - A checklist item is in CLAUDE.md → Implementing a Feature.
  - The `apogee-cli-backlog-execute-item` (then `apogee-backlog-item`), `apogee-cli-maintenance-update-documents` (then `apogee-document-update`) and `apogee-cli-maintenance-summarize-pull-request` (then `apogee-pull-request`) skills each carry it. The docs pass runs the dry run; the PR description gains a **Before merging** section.
  - The script's header lists its own assumptions.
  - `--apply` is always the user's to run: it changes repository settings.

**Verified.**
- Against the real repository, read-only: the script picks PR #3, takes its passing run (36196338575), and derives 13 checks. `tag and release` is left out both ways: skipped in that run, and run only in the post-merge run, which the script ignores.
- Against a stand-in `gh`, 26 checks pass:
  - create and update, with the ruleset's other settings kept;
  - already up to date;
  - the bypass and enforcement warnings;
  - a failed run and a running pipeline refused;
  - an open pull request preferred;
  - two workflows combined;
  - a pull request into another branch refused.

  Removing the post-merge filter or the "passed" filter fails them.

**Not done.** The ruleset has not been created. Running `--apply` changes the repository's settings, and that is the user's to run.

### 2026-09-01 — Layout, doctor, installers, completions, release pipeline

**What was built**

- [x] **The on-disk contract** (`source/harness/layout.h/.cpp`) — one declaration of what `~/.apogee/` contains, with each row carrying its purpose, whether it may hold secrets, and whether it holds the user's own work. `logs/`, `sessions/`, `models/`, `embeddings/`, `cache/`, and `config/`. The scattered `logs_dir()` / `sessions_dir()` definitions in `logger/` now forward to it.
- [x] **`apogee check`** (`source/commands/check.h/.cpp`) — the doctor. Version and macOS quarantine, config parse, per-backend validation (a dangling `model_path`, an unloadable GGUF, a missing API key), role pointers, the directory contract, file modes, and installed models. `--fix` repairs the local install and **never touches config**.
- [x] **`apogee uninstall`** (`source/commands/uninstall.h/.cpp`) — plans first, names the user data it would destroy, prompts, and refuses rather than guessing when there is no terminal and no `--yes`. `--keep-data` reinstalls without losing conversations.
- [x] **Dynamic shell completion** — a hidden `apogee __complete` verb plus four small stubs (bash, zsh, fish, PowerShell) that do nothing but call back into the binary. `apogee complete -m <TAB>` offers the backends this user actually has.
- [x] **Two installers** (`lib/scripts/install.sh`, `install.ps1`) — download the archive for the host, install the binary and completions, then ask the binary to create and verify its own data directory.
- [x] **The release pipeline** (`.github/workflows/release.yml`) — a pushed tag builds all six targets on native runners *through `lib/scripts/cicd.sh`*, tests before packaging, verifies the staged binary **runs**, and publishes archives to a GitHub Release.
- [x] **`executable_path()`** on the platform seam — three genuinely different mechanisms (`GetModuleFileNameW`, `_NSGetExecutablePath`, `/proc/self/exe`), which is why it belongs behind the seam rather than in a caller.
- [x] **14 new tests** (462 total), plus `cli.install_parity` and `cli.test_names`.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| Distribution | **GitHub Releases + Actions** *(user call)* | Confirms the strong default; the repo already lives there. The release matrix invokes `cicd.sh`, so a release binary is built the same way a local one is. |
| Self-update | **Deferred to its own item** *(user call)* | It would be the third parity path, and widening the parity surface before this item has pinned it once is backwards. It also needs a release pipeline that has been proven before it can trust what it downloads. |
| macOS signing | **Unsigned** *(user call)* | A curl-driven install sets no quarantine attribute. For the browser-download case, `check` detects the attribute and prints the exact `xattr -d` command. Notarization joins when there is a Developer ID. |
| Windows | **Ships from the first tag, with `install.ps1`** *(user call, overriding the recommendation)* | See the caveat below — this is the one decision taken against advice, and the risk is recorded rather than hidden. |

**Notes — the guardrail failed its first mutation test, and that was the useful part.**

The parity gate is the whole point of this milestone, so it was mutation-tested by deleting one directory from the seeding loop. **It passed.** The reason: there were *two* implementations that could build the tree — `seed_data_directory()` and a copy inside the doctor's `--fix` — and the mutation hit the one the installers never reached. The copy they did reach was still correct, so nothing broke.

That is precisely the bug this milestone exists to prevent, reproduced inside the work meant to prevent it. `apply_fixes` now delegates to the single implementation, and the same mutation turns the gate red. The lesson is recorded in `CLAUDE.md` as an invariant: **two implementations of the layout is the bug, even when both are correct.**

A smaller version of the same thing happened earlier: `config/` was seeded as a special case outside the row list, so `check --fix` (which enumerated rows) produced a tree one directory smaller than seeding did. It is a row now.

**Two test-infrastructure bugs, both found by the full run rather than in isolation.** ctest executes cases as parallel *processes*, so a per-process counter generated the same temp directory name in several at once and they deleted each other's fixtures — the directories are claimed by atomic `create_directory` now. And a test whose **name** begins with `--` is handed to Catch2 by ctest as an option, failing with "Unrecognised token" while passing when run alone. That cost time twice, so `cli.test_names` now refuses it mechanically; it was verified against a deliberately dashed name.

**The recorded Windows caveat.** All six targets ship binaries from the first tag, which was chosen over the recommendation that Windows join a later release. The reason for the recommendation stands and is not resolved by this item: Apogee's Windows portability work is incomplete — the PTY tests, the `tcflush` typeahead flush, the `lsof` no-listen poll, and `0600` file modes have no Windows equivalent, and only `macos-arm64` is merge-blocking. `apogee check` reports the mode rows as **skipped** on Windows rather than passing them, and the Windows parity job is non-blocking, so a Windows install is verified more weakly than a POSIX one. That gap is visible in the tool's own output instead of being implied.

### 2026-09-06 — Three install and completion bugs, found in use

Reported from a real machine: tab completion did nothing. Pulling that thread found one cosmetic gap and two genuine defects, the worst of which had nothing to do with completion.

- [x] **`make install` broke every subsequent build of the project.** `FetchContent_MakeAvailable` adds each dependency's own `install()` rules to this project, so `cmake --install` also wrote replxx's headers, static library, and **CMake package config** into `~/.local`. Our `FIND_PACKAGE_ARGS` then *preferred* that installed copy on the next configure — and its config declares `Threads::Threads` with no `find_package(Threads)` behind it, so the generate step failed. Running the install once poisoned the source tree, with an error naming replxx rather than the install that caused it. Fixed with `COMPONENT apogee` on the rule and `--component apogee` on the invocation.
- [x] **The completion protocol never completed flags.** It handled subcommands, backend names, and `config` verbs, and returned nothing for `--`. Flags now come from the **live `CLI::App` tree** (`specs_from_app`), so a flag added to any command completes without this file being told.
- [x] **The zsh completion was installed where zsh does not look.** `~/.local/share/zsh/site-functions` is on nobody's default `fpath`, so the file was written and never loaded — `whence -w _apogee` returned `none`. The installer now cross-references zsh's real `fpath` against directories that are conventionally ours.
- [x] **`cli.install_is_ours_only`** — a guard asserting the install puts exactly `bin/apogee` in the prefix, and that the Makefile passes `--component`.

**Two lessons, both about checks that do not check.**

The install guard's first version called `cmake --install --component apogee` *itself*, and so passed while the Makefile was installing without the component and leaking replxx. **A guard that supplies the argument it is verifying tests nothing.** It now reads the Makefile's actual command, and was mutation-tested by removing the flag.

And my own verification loop had the same shape of hole: I checked builds with `grep -E "error:"`, which does not match CMake's *"Generate step failed"*. So I ran a stale binary through several rounds of "fixed?" — the configure had been broken the whole time.

**One near-miss worth recording.** The first fix for the zsh path took the first writable `fpath` entry under `$HOME`, which on an oh-my-zsh machine is `~/.oh-my-zsh/plugins/vscode` — a directory a framework owns and rewrites. It was caught before shipping, and the rule is now: a completion file belongs in a user completion directory (`~/.zfunc`, `~/.oh-my-zsh/completions`, `~/.local/share/zsh/site-functions`) or nowhere.

**Not verified:** no release has been cut. The workflow is syntactically valid and every step it runs is exercised locally, but the tag → build → publish path itself has never executed, and neither installer has downloaded a real archive — there is nothing published to download yet. The first tag is also the first test of that pipeline.

### 2026-09-23 / 2026-09-24 — Completion at every depth, and for what exists

Asked for directly (Taylor), in three reports: "Not all subcommands have tab autocomplete registration"; "the completions aren't working past the first command"; and, once they did, "I need more thorough tab auto complete coverage across the application."

**2026-09-23 -- the tree, the kinds, the stubs.** The protocol read only the top level and kept `config`'s verbs by hand, so `models <TAB>` offered nothing and the `config` list was four verbs behind. `specs_from_app` now reads the whole live parser, every visible verb at every depth with its aliases, flags and what each argument takes. An argument's **kind** is declared where it is added and shown in `--help`: `kBackendValue`, `kPathValue`, a `CLI::IsMember` set, or free text, which answers with a hint (`--model TEXT: Model name`) instead of the working directory's files. A directive line (`:values`, `:files`, `:hint`) reaches only a stub that asks. The zsh stub dropped the empty word under the cursor and, autoloaded from `fpath`, completed nothing on a shell's first `<TAB>`; `cli.shell_completion` now runs the stubs in the real shells. The "not working past the first command" report was a stale installed binary.

**2026-09-24 -- names, and the words commands already check.** 134 arguments answered `<TAB>` with a hint and nothing else. 95 of them name something that exists or take a known word, and now complete to it; the other 39 are free text on purpose. So:

- [x] **Name kinds** (`kNameValues` in `command.h`): `COLLECTION` (and `COLLECTION,...` for a comma list), `GRAPH`, `NAMED_GRAPH`, `AGENT`, `CHAT`, `SERVER`, `DATASET`, `KIT`, `SUITE`, `RUN`, `PIPELINE_RUN`, `PIPELINE`, `REGIME`, `MODEL`, `SNAPSHOT`, `GGUF`, `SNAPSHOT_ID`, `GGUF_ID`, `MODEL_REF`, `RECORD`, `GIT_REF`, `REMOTE`, `KEY`, `TOOL`. Each is listed by `complete_sources.cpp` from the config, the data directory, the model store, the local Ollama store or `git`, read-only (a missing knowledge collection is not opened, since opening creates it). A kind that also takes a path offers its names while any match and files once none do, so `./` still completes. `models convert <model> --from <TAB>` offers that model's ids, and a record id is looked up in the collection `--db` names -- the protocol hands a source the line's positionals and flag values.
- [x] **Word lists from the validators.** `--retriever`, `--status`, `--discipline`, `--trainer`, `--method`, `--with`, `--format`, `--from`, `--tools`, `--output-format`, `auth add <provider>`, `models quantize --type`, `train promote --quantize`: each offers the list the command validates against, made public beside its validator where it was a private constant or an inline comparison (`retriever_names`, `slot_names`, `trainer_names`, `lora_methods`, `format_names`, `create_source_names`, `quant_type_names`, the agent policy and format names), and `select_trainer`, `train run --method` and the config's stage `method:` now check against those same lists. `datasets prepare --format` is the Python driver's to decide; a test holds the C++ list to the driver's own argparse choices, and `--method` to both trainer drivers'.
- [x] **`config_keys()`** beside `config get`'s lookup, for `config get <TAB>`; a test asks `config get` for every key it lists.
- [x] **The stubs.** bash split `llama3.2:3b` into three words at `:` (a `COMP_WORDBREAKS` character) and miscounted the positionals; it now reads `COMP_LINE`, and trims a candidate to what follows the last `:` typed, since bash replaces only that. fish asks for the directives, so a path falls back to fish's own file completion. PowerShell keeps the bare protocol, where an empty answer already means its own path completion.

**The guardrail.** `lifecycle_test`'s coverage test walks the live tree: every TEXT argument must be tagged or on `kFreeText`, the reviewed list of arguments that are free text on purpose (prompts, names being created, dates, a vendor model id, a Hugging Face repository); an entry that is no longer free text is stale; and every name kind is declared by some argument. Untagging `chats info`'s argument fails it by name.

**Verification.** All 1544 ctest cases pass, run serially. `cli.shell_completion` in real bash and zsh covers names from a built home (a collection, a stand-in Ollama store's `llama3.2:3b`, a config key, the bundled kits), fixed word lists, the name-or-path fallback, and the word-break: run against the old bash stub it offers flags after `models pull llama3.2:`. On Taylor's machine, `models convert <TAB>` offers `Qwen--Qwen3.8-27B` and its SafeTensors handle, `chat --branch <TAB>` the repository's branches and tags, `chats info <TAB>` the saved conversations. fish and PowerShell are not on this host; their stubs are unverified here.

### 2026-10-04 — `reset-keep-models` (maintenance item M9): start over, the models kept

**Why.** The user's ask (2026-10-04), building from source: "pretty much a fresh build but replace everything except the models directory in the application directory." There was no middle path. `apogee uninstall` removes `~/.apogee` whole -- the models with it, tens of gigabytes to pull again -- or, with `--keep-data`, spares it whole; `make install` ends in `check --fix`, which creates what is missing and resets nothing; and deleting the other rows by hand means knowing the layout by heart, the drift the one declaration exists to close.

**What was built**

- [x] **`apogee reset [--keep <row>]... [--yes]`** (`cli/reset.h/.cpp`): the data directory back to a verified first-run state, selectively. Uninstall's contract is "Apogee is gone"; reset's is "Apogee starts over".
  - **The plan is the layout.** `plan_reset` walks `layout.h`'s rows at runtime, in their order, each marked `keep`, `remove` or `absent`. `--keep` is repeatable and accepts exactly those rows -- a `CLI::IsMember` set built from the declaration at bind time -- so a word that is not a row is refused, with the rows listed, before anything is removed. Nothing in the command, the Makefile or a test lists the rows.
  - **Outside the layout.** Whatever sits at the top of the directory that no row declares is removed too, and named (`chat_history   (not in the layout)`): a first-run directory holds none of it. The chat line editor's history is the one such file Apogee itself writes there; since nothing says what an unknown entry is, it is warned as the user's own.
  - **The user's data, by name.** The removed rows holding anything but Apogee's own unedited files are warned exactly as uninstall warns them, and the secrets store by its file: `config/   the secrets store (credentials.json): your stored API keys`. Where the store lives is asked of `secrets::credentials_path`, not assumed; a kept row is not warned.
  - **Kept means untouched.** A kept row is not opened, recreated or rewritten: `models/` after a reset is byte-identical, records included, so every stored model is still listed and a backend registered again on one resolves.
  - **Then the doctor.** The `check --fix` pass runs in process: `run_check_pass` (new in `check.h`, and now the body of `apogee check` itself) recreates the skeleton through the one seeding path and prints the report, so what remains is verified, not hollowed. The ~140 files a recreated layout gets are said as one count (`fold_created`); every other repair is said in full.
  - **Partial failure, reported.** A path that cannot be removed is named with the reason; the removals after it still run, the check still runs -- its report is the directory as it now is -- and the exit code is non-zero.
  - **Nothing outside the data directory.** The binary, the completions and shell files stay uninstall's business.
- [x] **Uninstall's plan machinery, generalized** (`cli/uninstall.h/.cpp`), so the two destructive verbs share one idea of what is the user's: `plan_rows` (the row walk and the user-data test), `describe_user_data` (the warning), `confirm_removal` (the discipline: `--yes` skips it, a terminal types `yes`, anything else is refused with the remediation) and `remove_planned` (the errors collection). Uninstall prints byte for byte what it did. Both now ask `stdin_is_piped` rather than the terminal alone -- the same answer in use, and a test that feeds `std::cin` is a pipe whatever terminal runs the suite.
- [x] **Completion.** `apogee reset --keep <TAB>` offers exactly the layout's rows, in its order, read from the validator the command holds the word to: what TAB offers and what the verb accepts are one list, and it is the layout's ([ADR 0007](../adrs/cli/tab-completion.md)).
- [x] **`make reinstall [KEEP="<row> …"]`** (`lib/src/cli/Makefile`): a fresh build (`cmake --build --clean-first`), `make install`, then `apogee reset` on the installed binary with each word of `KEEP` forwarded as `--keep <word>`. Only the named rows are kept; bare, nothing is, which the plan shows and the prompt confirms. The binary is run only as `$(APOGEE_BIN)`, defined beside `PREFIX` (`$(PREFIX)/bin/apogee`), so the install channels (M10) re-point the reset with the install. The Makefile knows no row and never passes `--yes`. `make help` names `KEEP`, its vocabulary and that bare keeps nothing.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| A new verb | `reset`, not a widened `uninstall` (2026-10-04) | Uninstall's contract is "Apogee is gone", reset's "Apogee starts over"; one verb with both blurs the one prompt that must never be misread. |
| The keep set | Generic `--keep <row>`, repeatable and completable, models the motivating case *(recorded default, 2026-10-04)* | Chats or training are the same ask a week later; a generic flag costs nothing over `--keep-models`. |
| Bare `apogee reset` | Every row reset *(recorded default, 2026-10-04)* | A carve-out nobody typed is state surviving a "fresh start" by surprise. |
| The make target's name | `reinstall` *(default taken)* | It reads as what it does from the source tree; `fresh-install` was the alternative. |
| The make keep set | Explicit at the invocation, `KEEP="…"`, only the named rows kept -- **the user's call** (2026-10-04, superseding a hard-wired `--keep models`) | `KEEP=` is make's spelling of `--keep` (M10's `MODE=` precedent); the rows complete on `apogee reset --keep`, where a completion Apogee ships can answer. |
| `--yes` from make | Never (2026-10-04) | A destructive confirmation belongs to the person, even mid-target. |
| The chats' row | `sessions`, as `layout.h` names it; no `chats` alias | The spec's example `KEEP="models chats"` is spelled `KEEP="models sessions"`. An alias accepted but not offered breaks ADR 0007's both-ways rule, and one offered breaks "exactly the layout's rows"; the refusal lists the rows before anything is removed. |
| Entries outside the layout | Removed, named, and warned as the user's | "A bare reset leaves exactly the first-run skeleton" is false while `chat_history` survives it, and nothing says an unknown entry is Apogee's to dismiss. |
| "A fresh build" | `cmake --build --clean-first`, not `make clean` | Every object recompiled; `make clean` deletes the fetched dependencies too (llama.cpp among them), so the target would need the network. |
| The fix pass's output | The files it creates folded to one count; every other repair verbatim | A recreated layout is ~140 created files, and a line each scrolled the plan and the removals away. |
| Modes reached | The command line only: no admin-plane route, no machine-mode verb -- skipped and said ([ADR 0002](../adrs/cli/mode-parity.md)), and classified a carve-out beside `uninstall` in `parity_test`'s table | As with `uninstall`, a served reset would delete the server's own state under it. |

**Guardrails, each mutation-tested** (six mutants, all caught by `[reset]`): kept rows removed too; no `check --fix` after; a pipe proceeding; stopping at the first failure; entries outside the layout surviving; the secrets store unnamed.

**Tests.** `tests/presentation/cli/reset_test.cpp`, 13 cases over sandboxed temp `APOGEE_HOME`s with `HOME` guarded:
- the plan word for word, over fixture rows handed in place of the layout -- a golden that pins the wording without restating the real rows;
- every declared row planned, in order; every removed user-data row warned, the secrets store named by its file and unnamed when its row is kept; a fresh install's own files never warned;
- **the no-second-list pin**: a row appended to the layout -- through the very parameter the command fills from `layout.h` -- is planned, warned, keepable and kept with nothing taught, and against the real layout the same directory is named as outside it;
- on the real command, in process: `--keep models` leaves `models/` byte-identical and every other row exactly what a fresh seed makes, with `check` passing and a backend registered again on the kept GGUF listed; a bare reset leaves exactly the first-run skeleton; a piped reset without `--yes` -- even with `yes` on the pipe -- shows the plan, refuses with the remediation and removes nothing; a keep the layout does not declare is refused with nothing removed; the confirmation word for word;
- a row that cannot be removed (POSIX: a locked directory inside `sessions/`) named, the removals after it run, the kept row untouched, the check run after it;
- the completion golden: `reset --keep <TAB>` offers exactly the layout's rows, enumerated rather than restated, and every row offered is one the verb keeps.

**Verified.**
- The full suite, `make test`: 100% of 2181 ctest cases pass (the pdftotext skip is the host's, as before), the 13 new ones, `harness.layering`, `cli.install_parity` and `cli.shell_completion` among them. The CLI↔HTTP parity table failed the first run, as it exists to -- a new verb unclassified -- and `reset` is now a carve-out in it.
- On the real binary, in a sandbox -- a temp `HOME`, `PREFIX` and `APOGEE_HOME`, never the real install: `reset --help` lists the rows; a piped `reset --keep models` shows the plan and refuses with nothing touched; `--keep chats` is refused with the rows listed (exit 105) before anything is removed; `reset --keep models --yes` removes the rest, recreates the skeleton, and keeps the stored model byte-identical; `__complete reset --keep` offers the fifteen rows, and `mo` gives `models`.
- **`make reinstall KEEP="models sessions"`** under a pseudo-terminal, answered at the prompt: a fresh build, the install, the plan with both rows kept and `config/` warned as the secrets store, then `yes` -- thirteen rows and `chat_history` removed, `fixed: created 139 directories and files`, `check` with no failures. `models/` byte-identical (hashed before and after), the chat kept, the keys and the history gone; after `config init`, `models list` lists the stored model, and `config add-backend m` fills itself from it and reads `ok`. About two and a half minutes on this Mac, the clean rebuild mostly ccache hits.
- **Bare `make reinstall` on a pipe**: the keep-nothing plan -- every row `remove`, the models, the chats, the keys and the history warned -- then the refusal; nothing removed, and the target fails.

**A sharp edge, recorded.** `reinstall` stops where `install` stops: `make install` fails when its own `check --fix` finds a failure (an unparseable config, an unreadable model), and the target then ends before the reset. Found by the sandbox run itself, whose first fixture model was not a GGUF. `apogee reset` needs nothing from the config and is the way through such an install; `install`'s gate is left as it is (it is install's, and M10 reshapes that target).

**Not verified.** The partial-failure case is POSIX-only: Windows has no directory mode to lock one with, so it is a recorded skip there; the reset itself is portable `std::filesystem`. A run on the user's own install is theirs.

### 2026-10-04 — `install-channels` (maintenance item M10): dev, test and release roots

**Why.** The user asked for this (2026-10-04) as the thorough fix behind the question that became M9 (the selective reset). A build from source shared `~/.apogee` with the install the user relies on. A dev build's experiments landed among the real models, chats and keys, and the only clean slate was an uninstall that took everything. Most of the pieces existed already: `APOGEE_HOME` relocated the whole tree, a global `--config` existed, and `home_for_config()` derived a root from a config file's position. What was missing:
- a root the binary knows by itself;
- shorthand flags for the common roots;
- one chain that re-roots the entire layout, not just the config read.

**What was built**

- [x] **The channel is a build fact.** `APOGEE_CHANNEL` is `release` by default, or `dev` or `test`; any other value fails the configure.
  - `apogee_channel_executable` (`source/CMakeLists.txt`) stamps it into `main.cpp` as a compile definition, and names a non-release build `apogee-dev` or `apogee-test`.
  - `main()` holds the stamp to the three names with a `static_assert`, then bakes it into the library (`harness::set_baked_channel`) before anything runs.
  - CI, `cicd.sh`, both installers and every release archive build the release channel, unchanged.
- [x] **One resolution chain**, in `contracts/paths.h`. `resolve_root` is a pure function over four inputs: the root flag, `APOGEE_HOME`, the baked channel and the home directory. It answers with the root, the config file, the rung that chose it, or a refusal.
  - `apogee_home()` is its answer for the process, so every layout row follows a flag.
  - A flag and an `APOGEE_HOME` naming different roots are refused, both named. Naming the same directory, compared absolute and through symlinks, is no conflict, and the flag is credited.
  - A channel's root with no home directory is refused rather than guessed.
  - `APOGEE_HOME` with no flag resolves exactly as before.
- [x] **The root flags** on the root command: `--release`, `--dev`, `--test` and `--custom <config file>`.
  - They exclude one another, and they complete like any flag: they are read from the live parser, and `--custom` offers paths.
  - A `RootFlagScope` holds the flag in force for the run. It is written nowhere.
  - `--custom` checks that its file sits at `<root>/config/<file>`, then derives the root through `home_for_config()`. A file anywhere else is refused, naming that shape and where the file would have to sit. A file at the top of the filesystem is refused too.
  - A refusal comes once parsing completes, before any command runs.
- [x] **`--config` beside them**, which the item asked to have stated. `--config` names a file only.
  - Beside a channel flag or `APOGEE_HOME`, it reads that file over the channel's data directory. The flag claims a root and no file, so the two do not disagree.
  - `--custom` names a file too, so a `--config` or `APOGEE_CONFIG` naming a different file is refused with both named. The same file is accepted.
  - Help says so on both flags.
- [x] **Which Apogee this is, said.** `apogee version` and `--version` keep the version line first and alone, since the release scripts read line one. Then come `channel:` and `root: <path> (<rung>)`, the rung being one of `the dev channel's own root, baked into this build`, `set by --test`, `set by APOGEE_HOME` or `set by --custom <file>`. `check`'s Version section gains the same two rows. `--version` became a plain flag, answered after the root flags: CLI11's version flag is answered while the flags are still being read.
- [x] **Uninstall per channel.**
  - `UninstallCommand` resolves its root through `install_home()`. That is `APOGEE_HOME` when set, so every sandboxed uninstall stays hermetic, and otherwise the baked channel's root. Never a flag's.
  - A dev or test build plans no completion stubs. The stubs call `apogee` by name and a dev install never writes any, so the release install's must survive a dev uninstall.
- [x] **Completion follows the line's root.** `typed_root_flag` reads a root flag typed before the verb. So `apogee --dev complete -m <TAB>` offers the dev root's backends, which are what the command will accept ([ADR 0007](../adrs/cli/tab-completion.md)).
- [x] **The Makefile.** `MODE ?= release`, and any other value is an error naming the three channels.
  - `APOGEE_BIN` names the channel's installed binary: `$(PREFIX)/bin/apogee`, `apogee-dev` or `apogee-test`.
  - `build` and `no-llama` pass `-DAPOGEE_CHANNEL=$(MODE)` on every configure, release included, because CMake caches the value. `install` builds through `build`.
  - `install` skips the completion stubs for a non-release channel, and ends with the channel binary's own `check --fix`.
  - A new `uninstall` target runs the channel binary's `uninstall`. Its plan and prompt are intact, and it never passes `--yes`.
  - M9's `reinstall`, built alongside, follows `MODE` too: it configures with the channel and resets through `APOGEE_BIN`, so `make reinstall MODE=dev KEEP=models` rebuilds and reinstalls `apogee-dev` and resets `~/.apogee-dev`, the release install untouched.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| Non-release binaries | Suffixed (`apogee-dev`, `apogee-test`), with no completion stubs *(default taken)* | Three channels overwriting one binary would make the baked root a lie. Suffixed stubs would be litter until someone asks for them. |
| How make takes the channel | `MODE=<channel>` (2026-10-04) | `make` has no long options. The targets keep their names, and the mode is the argument after them, the ask's own framing. |
| Where the channel lives | A compile definition (2026-10-04), stamped into the executable's `main.cpp` and baked into the library at startup | The binary must know its root before it reads a file, and a marker file inside a root begs the question of which root to read it from. The stamp is on the executable, not the contracts library, so `apogee_core` is the same for every channel. Switching channels recompiles one file, a test can be any channel's build in process, and the parity check builds a dev executable over the very same `apogee_core`. |
| Precedence | Flag, then `APOGEE_HOME`, then the baked channel (2026-10-04) | Explicit beats ambient beats built-in. The environment rung keeps every sandboxed test and probe working untouched. |
| `--custom` | Takes a config file, and derives the root from its position in the layout; anything off `<root>/config/<file>` is refused (2026-10-04) | The layout already fixes where a config sits, and `home_for_config()` is the one derivation. |
| `--config` beside a root flag | Allowed beside a channel flag, as beside `APOGEE_HOME`; beside `--custom`, refused when it names a different file | Disagreement is refused, never guessed, and only `--custom` and `--config` answer the same question. |
| Uninstall | Ignores the root flags and removes the baked channel's root, with `APOGEE_HOME` honored | A release uninstall must never take `.apogee-dev` because a flag was on the line. |

**Tests**
- `tests/data/contracts/paths_test.cpp`, the precedence table:
  - every rung alone, and each channel flag against each baked channel;
  - the environment against each baked channel;
  - a flag and `APOGEE_HOME` agreeing, with or without a trailing separator, and disagreeing, refused with both named;
  - what needs a home directory and what does not;
  - the `--custom` derivation and every refusal;
  - the channel names, checked at compile time.
- `paths_test.cpp`, the pins:
  - **the re-root:** with `--custom` in force, every row of `layout.h` is seeded under the derived root and nothing under the baked one;
  - a scope puts back the flag it replaced;
  - uninstall's root ignores every flag on every channel's build.
- `tests/presentation/cli/root_test.cpp`, the flags as the command line meets them:
  - help names them and how they relate to `--config`;
  - they exclude one another;
  - each roots its run, is gone afterwards, and writes nothing, and a second parse of the same root reads what that parse was given, never a value the first left bound;
  - the refusals: a disagreeing `APOGEE_HOME`, `--custom` against `--config` and against `APOGEE_CONFIG`, an off-layout path;
  - a channel flag beside `--config`;
  - the `version` and `--version` wording for each channel and each rung.
- `check_test`: the Version section's `channel` and `root` rows for each channel and rung.
- `lifecycle_test`:
  - a dev build's uninstall plans `.apogee-dev` and no stubs under every flag, and a release build plans `.apogee` and its stub;
  - the root flags complete, and `--custom` completes a path;
  - `typed_root_flag`'s rules;
  - `__complete` offers the dev root's backends under `--dev`.
- **`cli.install_parity`** takes a dev-channel executable, `apogee_channel_probe`. It is `main.cpp` stamped dev by the same `apogee_channel_executable`, built on demand by the new `cli.channel_probe_build` fixture and never by a plain build, so CI builds nothing new. With `APOGEE_HOME` unset and `HOME` in its own work directory, it requires:
  - the suffixed name;
  - the same tree and modes under `.apogee-dev`, and nothing under `.apogee`;
  - a passing `check`;
  - `version` naming the channel and the baked root;
  - the dev build's uninstall, run from a copy with `--release` on the line, removing its own root and binary and leaving the release root and its stub;
  - the release build's `--dev` seeding the same tree at the dev root.
- Each new half was checked against a planted fault. A `main()` that never baked its channel failed it, and so did an uninstall back on `apogee_home()`.
- `cli.install_is_ours_only` takes the executable's file name rather than assuming `apogee`.

**Verified end to end**, with a temporary `HOME`, a temporary `PREFIX` and no `APOGEE_HOME`, through the real Makefile:
- `make install MODE=dev`, `MODE=test` and a plain `make install` left `apogee`, `apogee-dev` and `apogee-test` side by side, each with its own root seeded. Only the release install wrote completions.
- Each binary's `version` named its channel and its own root. `apogee-dev check` named the dev channel and why.
- `MODE=bogus` and `MODE="dev test"` stopped make, naming the three channels.
- `make uninstall MODE=test`, answered `no` on a pseudo-terminal, removed nothing.
- `make uninstall MODE=dev`, answered `yes`, removed `apogee-dev` and `.apogee-dev`. The release binary, its root and its completions were untouched.
- Run again, it said there is no dev-channel install. From a pipe, the channel's own `uninstall` refused, naming `--yes`.
- The full suite: `make -C lib/src/cli test` ends "100% tests passed, 0 tests failed out of 2202" (the pdftotext case skips, as before).

**Found on the way.** The parity script's mode listing was written `a || b && c`. The shell binds `&&` after `||`, so it ran its GNU `stat -c` branch on macOS too, and printed a usage error for every directory. Both sides printed the same errors, so the diff still compared the BSD listing alone. It now asks which `stat` it has first.

**Not done.**
- No real `~/.apogee-dev` or `~/.apogee-test` was created, and nothing ran against the user's own install. Every check above used a temporary home.
- `make lint` was not run over the whole tree, which takes about 25 minutes. clang-tidy over the touched sources and tests raised none of the error-class checks, and the two new style warnings it did raise were fixed. The touched files are formatted, and the build has no new compiler warnings.

### 2026-10-04 — `ci-single-pipeline` (maintenance item M8): one pipeline

**Why.** The user's ask (2026-10-04): one pipeline instead of two, the tag and release happening only when the branch is merged, and the second pipeline's Actions title gone. The Actions sidebar listed **CI** — the pull-request gates, the five builds, and `tag and release` after a merge — beside **Release**, the manual escape hatch kept since 2026-09-25: a hand-pushed `v*` tag rebuilt every target and published, with a dry-run dispatch beside it. The merge path already published only on a merge. Retiring the second workflow and its tag trigger makes that true by construction rather than by convention. Its one capability nothing else had, the **re-cut** — rebuild all five targets from source and publish, for a merge whose archives cannot be released or a deleted release cut again — moved into CI, so the escape hatch survives inside the one workflow.

**What was built**

- [x] **`.github/workflows/release.yml` deleted**, and with it the "Release" title in Actions, the `v*` tag trigger, and the second copy of the five-row matrix ("the same list on purpose", because GitHub cannot share one). A hand-pushed tag now starts no workflow: a tag is what a release makes, never how one is asked for. `ci.yml` is the only workflow.
- [x] **The re-cut, as a `publish` input on CI's dispatch** (`ci.yml`, default off). A dispatch already ran every job and kept all five archives as artifacts, which was the old dry run in all but name. `publish` adds the one leg:
  - `tag and release` now `needs: build` and runs past a skipped need (`!cancelled()`). After a merge and in a rehearsal it routes as before. On a dispatch with `publish` it runs only once **all five** builds pass, downloads that run's own `apogee-<target>` artifacts, and calls the script's new re-cut mode.
  - The run is titled `<ref> Release Re-cut` and has a concurrency group of its own that is never cancelled. A release half made is a state nobody asked for, so a second dispatch queues; the old workflow kept that rule and it carries over.
  - `publish` beside `release_rehearsal_pr` is refused: a rehearsal builds nothing, so they are two different runs.
- [x] **`release-from-pr.sh --recut <dir> [--run <id>] [--publish]`**: the re-cut shares every step of the merge path but the first and the fourth, with no second copy:
  - **The commit is the one checked out**, and it must be on `stable`: a release is merged code, whichever way it is cut.
  - **The platforms are the run's successful `build <target>` jobs**, found by the same function the merge path now calls (`built_targets`). Every one's archive must be in `<dir>`, its `.source` naming that commit's tree.
  - **The binary is run**, and must report the version when the CLI changed since the latest release.
  - **A version already released is refused, by name**, where the merge path declines silently: a tag at another commit, or a release at this commit carrying every archive. A re-cut is asked for by name, so publishing nothing is a failure to report. A release at this commit that is missing an archive is an earlier attempt's, and the re-run finishes it.
- [x] **`make release` rewired**: the same preflight (on `stable`, clean, in sync with the remote, `VERSION` matching `lib/release/VERSION`, the tag free locally and on the remote, no branch named like it), one confirmation, then `gh workflow run CI --ref stable -f publish=true`. No local tag is made or pushed. A retired trigger with a live target still pointing at it would have been a silent no-op.
- [x] **The sweep**: `ci.yml`'s header, `changed.sh` (the deleted file is no longer a CLI input), the package action's header, `version-check.sh`, the Makefile's help and comments, the update-documents skill's pipeline step, CLAUDE.md (Stack, the Codebase Map's `ci.yml` and scripts rows, Release and Install Infrastructure) and DEVELOPER.md (the tree, **Changing the pipeline** — the matrix-coupling item is gone — and **Cutting a release**: the re-cut, the version guard, orphaned tags, the dry run, re-running).

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| The re-cut | Kept, as a CI dispatch input *(recorded default, 2026-10-04)* | The ask was about the workflow count and the Actions sidebar; the escape hatch exists for real needs (artifacts expire, the tree check can refuse). |
| The `publish` guard | Write access to dispatch, plus the already-released refusal; no extra confirmation input *(default taken)* | The Actions UI's run-workflow prompt, and `make release`'s own, are the confirmation. |
| Where a re-cut may publish | Only a commit on `stable` | "Only when the branch is merged", the user's own words, holds for both paths. Tagging an off-tip commit, which the old manual path allowed, is gone with it. |
| Gating | All five builds or nothing (2026-10-04) | The old workflow let both Windows targets fail and published three archives. One pipeline has one policy, and it is the stricter one the merge path already had. |
| A released version | Refused in a re-cut, declined on a merge | A merge that publishes nothing is how a docs or hotfix merge declines to cut a release. A re-cut that publishes nothing failed at what it was asked to do. |
| A tag at the dispatched commit | Finished while its release lacks an archive; refused once complete | A re-run must be able to finish a half-made release, but a re-cut never replaces published binaries. |

**Verified.**
- `release-from-pr.sh`, merge path unchanged: rehearsing #4 (v0.1.3) through the shared target lookup found its CI run, matched all five real archives to the merged tree, ran the real macOS binary (`apogee 0.1.3`) and answered "would finish v0.1.3's release" — its answer before the change, since that tag sits at the merge commit.
- `release-from-pr.sh --recut`, on stand-in archives with a real run's job list (run 37240820951's five `build` jobs):
  - the rehearsal on a release branch: "would tag v0.1.4 … with 5 archives";
  - refused: a commit not on `stable`; an archive from another tree; an archive the run built but `<dir>` lacks; a binary reporting the wrong version for a changed CLI;
  - in throwaway worktrees of `stable`: a re-cut at its tip, where v0.1.3 is released with every archive, and at an older `stable` commit, v0.1.3 released elsewhere, were both refused with the remediation;
  - the argument errors: no run, a pull request beside `--recut`, `--run` alone.
- `ci.yml` parses. Its `tag and release` routing was evaluated for every event: an open pull request, a merge, a closed unmerged pull request, a rehearsal, a plain dispatch, a re-cut whose builds pass, fail or are cancelled, and a re-cut with a rehearsal. The merge and the rehearsal route as before, a plain dispatch publishes nothing, and only a re-cut whose five builds pass publishes. No job was renamed, added or removed, so the required checks cannot have changed.
- `make release-check` on the version branch refuses ("a release is cut from 'stable'"), and `make -n release` shows the dispatch.
- The sweep: no reference to the deleted workflow, its tag trigger or its title remains outside dated history.

**Not verified here.**
- **On GitHub.** `actionlint` is not installed, so the workflow's syntax is first checked by the pull request's own run.
- **The required checks.** `required-checks.py --pr <N>` needs that pull request, and should report no drift.
- **The dispatch itself.** GitHub reads a dispatch's inputs from the default branch, so `publish` exists once this is merged. The first merge after it should publish exactly as before. A real re-cut is the user's to run, since it publishes.
- **`make pr-ci`.** It was not run; the pull-request jobs it rehearses are unchanged.

## Milestone L — The vendor-CLI family

**Goal.** A fifth backend and, more importantly, the machinery the rest of the vendor-CLI family will be built on: Claude driven through the official `claude` CLI as a **long-lived child process** — the subscription-auth path beside the API-key path from Milestone D. It reuses that backend's IR mapping and typed sinks and none of its HTTP: this one speaks JSONL over pipes.

### 2026-09-02 — `claude-cli`: persistent child, JSONL framing, typed events

**What was built**

- [x] **The process seam** (`source/platform/child_process.h/.cpp`) — `posix_spawn` with three pipes, `poll()` + `read()` on **raw file descriptors**, and stdout/stderr kept strictly separate. Both of those are load-bearing: a buffered `FILE*` holds up to 4 KiB before handing anything over, reintroducing the exact chunking token-level streaming exists to remove; and merging stderr puts the child's diagnostics *inside* the JSONL stream. Windows returns a clear "no implementation yet" rather than a silent gap.
- [x] **The line framer** (`source/backends/jsonl_framer.h/.cpp`) — pure, no I/O, carry buffer across reads. Separated from JSON parsing so a malformed object cannot desynchronise framing.
- [x] **Typed events** (`source/backends/cli_event.h`, `claude_cli_events.h/.cpp`) — the wire→event mapping from the item's `[verified 2.1.233]` table. Unknown types and malformed lines are **dropped, never fatal**: most observed types are framing, and a `switch` that assumes it has seen every case turns a CLI upgrade into an outage.
- [x] **The provider** (`source/backends/claude_cli.h/.cpp`) — one child per session, sending only what is new each turn; `--resume` on a dead child with a fall back to replaying Apogee's own transcript; side requests on their own short-lived child; `complete_structured()` as a distinct entry point.
- [x] **`claude-cli` as an ordinary config type**, with `binary` and `mode` (`subscription` | `bare`), documented in the starter config including the `ANTHROPIC_API_KEY` precedence trap.
- [x] **22 new tests** (516 total), plus `cli.no_vendor_credentials`.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| Release placement | **Gated ring**, per the item's own 2026-08-24 decision | The open call asked whether to re-earmark into v0.1.0. Left at its recorded default: v0.1.0 is complete and sealed at four backends, and the more valuable next act is cutting the first tag. One line to revisit if wanted. |
| Event union's home | `backends/`, not `harness/` | The design notes put it in the harness because *their* harness had only an untyped token channel. Apogee's already has typed `TokenSink`/`ThinkingSink`/`StatusSink`, so promoting a CLI-shaped union would add a second vocabulary — and put vendor knowledge in the one layer forbidden to have it. |
| JSON parsing | nlohmann, line at a time (the stated default) | Throughput is irrelevant next to model latency. |
| `complete_structured` | Synchronous (the stated default) | The value arrives whole on the terminal event; a streaming sink would have nothing to stream. |
| Windows | Recorded skip, refused with a message | `posix_spawn`/`poll` have no direct equivalent; a `CreateProcess` path with overlapped I/O is its own piece of work. |

**Notes — a real crash bug, found by a test that took thirty seconds to write.**

The first run of the child-process suite failed on *writing to a dead child*: the write raises **SIGPIPE**, whose default disposition terminates the process. Apogee would have died trying to talk to a crashed `claude` — **in the exact code path that exists to recover from a crashed `claude`**. Fixed per-descriptor with `F_SETNOSIGPIPE` on BSD/macOS and, where that flag does not exist, by ignoring the signal once process-wide (called out in the source, because it is a global change). The write then returns `EPIPE`, which is an ordinary `false`.

**A design flaw the tests forced out.** The provider originally applied every event a read produced to the current turn. A persistent child's output is one continuous stream, so a read carrying a turn's terminal event *plus following bytes* would fold the next turn's text into this one's answer — and dropping the remainder would lose it instead. Parsing and consumption are now separate, with unconsumed events queued for the next turn. It surfaced because the fake was made to serve two turns from one child, which is what a persistent child actually does.

**Guardrails, each mutation-tested.** Breaking the carry buffer (the classic wrong framer: scan the incoming bytes, forget the seam) empties every fixture at small chunk sizes. Spawning per turn, or running a side request on the session child, each turn their tests red. And `cli.no_vendor_credentials` greps every source for the shortcut the SPEC principle forbids — reading the vendor's own session file to recover instead of replaying our transcript — verified by injecting exactly that read.

**What is verified, and what is not.** Every flag this backend depends on was confirmed to exist in the installed CLI (2.1.233 — the version the design notes were verified against), and the full flag combination is accepted. **The wire fixtures are transcribed from the item's verified table, not recorded from a live session**, because recording spends the user's subscription quota and they declined. So the replay suite proves the parser matches the *documented* schema; it would not catch the CLI's schema drifting away from that document. Re-recording against a live `claude` is a small, well-defined follow-up, and the fixture header says so. The two "verified live" acceptance criteria — multi-turn streaming from one child, and kill-then-resume — are covered against a scripted child only.


---

## Appendix — vendor-CLI design notes (carried forward from the claude-cli-backend item)

These are the design notes the `claude-cli` backend was built from, verified against **CLI 2.1.233**. They are kept here, rather than deleted with the backlog document, because every later member of the family was built against them. **The family is now complete** — `codex-cli` and `ollama-cli` shipped 2026-09-06, `gemini-cli` the same day — so read all four milestone entries above before trusting these notes too far. Between them they show how wide the spread turned out to be: Claude streams tokens with inline schemas, codex emits typed events but no deltas and needs a pinned sandbox, gemini streams real deltas and needs *two* flags to pin its sandbox, and ollama emits no typed events at all.

Two parts have already moved on and are **not** authoritative here: the thinking *renderer* (notes §8) belongs to the terminal UX layer (`source/commands/thinking_view.h`, Milestone G), and the two credential/binary policy rules were promoted to [SPEC.md](SPEC.md) → Principles, where they bind every vendor-CLI backend rather than just this one. Where these notes and the shipped code disagree, the code and Milestone L's write-up are current — see in particular the event union's home (`backends/`, not `harness/`) and the event-queue change that a persistent child turned out to require.

## 1. Scope and constraints

Two inference mechanisms, structurally different:

| Backend | Mechanism | Streaming source |
|---|---|---|
| Local models | llama.cpp linked in-process | token callback on a worker thread |
| Claude | official `claude` CLI as a child process | newline-delimited JSON on a pipe |

One is a function pointer, the other a byte stream from another process, and **the single most important design decision is to not let that difference reach the rest of the harness.** Both adapters write into one sink (§6); everything above the adapter layer sees an ordered stream of `Event`.

The two hard policy constraints — never read the CLI's credentials, never modify or bundle its binary — now live in [SPEC.md](../assistant/SPEC.md) → Principles, because they bind every vendor-CLI backend and not just this one. They are also restated as Core constraints above.

Non-goal: reimplementing the agent loop. The Agent SDK is Python and TypeScript only; for other languages, invoking the CLI as a subprocess is the documented path. We are a caller, not a reimplementation.

## 2. Why naive output is choppy

Two independent causes, both fixable:

- **Per-invocation spawn.** Launching `claude -p` per turn pays process startup, config discovery, and MCP server initialisation every time. Fix: one long-lived child (§4).
- **Message-granularity chunking.** Plain `--output-format stream-json` emits one JSON object per *message*, so assistant text arrives in whole blocks rather than tokens. Fix: `--include-partial-messages` (§3).

If you only change one thing, change the second.

A third cause is not the CLI's fault: even with perfect token streaming, dumping everything the model emits — including extended thinking — into the terminal produces output that *feels* worse than choppy. It feels like noise. That is the renderer's problem, and it lives in the terminal UX layer (shipped 2026-08-26; `lib/src/cli/source/commands/thinking_view.h`).

## 3. Invocation

```
claude \
  -p \
  --input-format stream-json \
  --output-format stream-json \
  --verbose \
  --include-partial-messages \
  [--json-schema <inline-json-or-path>] \
  [--bare] \
  [--resume <session_id>]
```

- `--output-format stream-json` **requires** `--verbose`. Without it the CLI rejects the combination.
- `--include-partial-messages` is what produces token deltas. This is the flag people miss.
- `--input-format stream-json` makes stdin accept JSONL user messages, which is what allows one process to serve many turns.
- `--json-schema` constrains the turn's result to a JSON Schema — see §9, which has the non-obvious part.
- `--bare` skips discovery of hooks, skills, plugins, MCP servers, and `CLAUDE.md`. It makes runs reproducible and is the right mode for scripted invocation. **It also disables subscription auth** — see §5.

**[verified 2.1.233]** All the above flags exist in this build. `--session-id <uuid>` also exists, so a caller may pre-name a session rather than capturing the CLI's minted id — useful only if your own session ids are UUIDs; do not build a mapping layer just to use it.

## 4. Process model

One child process per session, not per turn.

```
                   ┌──────────────────────────────┐
   turn request →  │ stdin  (JSONL user messages) │
                   │                              │
                   │        claude -p             │
                   │                              │
   reader thread ← │ stdout (JSONL events)        │
   stderr thread ← │ stderr (diagnostics)         │
                   └──────────────────────────────┘
```

**Spawn.** `posix_spawn` with three pipes, or `fork`/`exec` if you need finer control of the child environment. Set `CLOEXEC` on the parent ends. Do not use `popen` — it gives you one direction and a `FILE*` you do not want (§7).

**Environment.** Inherit the user's environment wholesale. Do not inject `ANTHROPIC_API_KEY` from harness config unless the user explicitly set it there, and if you do, document loudly that it overrides subscription auth — a stale key silently redirecting a Max plan to per-token billing is the single most common support complaint in this area.

**Turn submission.** One JSON object per line to the child's stdin, flushed after each. Because stdin stays open you can also inject additional user messages while a response is in flight — useful for a "wait, actually…" affordance. Guard the write end with a mutex; only one thread writes.

**Keep one-shot work one-shot.** Not every model call is a turn of the session. Background summarisation (auto-titling), classification, and schema-constrained extraction share no prefix with the conversation and may run *concurrently* with a real turn. Route those to their own short-lived `claude -p` invocation rather than the session child. The same rule applies to a local backend's prompt cache, for the same reason: a side request that writes into session state corrupts it.

**Flag changes mid-session.** System prompt, permission mode, effort, allowed tools, and MCP config are all *process-start* flags. If the UI lets the user change one mid-conversation, the child must be restarted with `--resume <session_id>` — transparent, and far better than silently ignoring the change until the next session.

**Shutdown.** Close stdin, drain stdout until EOF, then wait. The CLI drains queued output before exiting rather than truncating, scaling its wait with the backlog up to roughly 30 seconds. Budget for that — a 5-second kill will cut off tail output under load.

**Crash recovery.** Capture `session_id` from every `result` event. On unexpected child exit, respawn with `--resume <session_id>` rather than replaying history yourself. This is why you never need to read session files off disk.

**Keep your own transcript authoritative.** The CLI's session is an optimisation — it saves re-sending history and preserves server-side prompt caching. It is not the source of truth. When a resume fails, fall back to replaying Apogee's own transcript into a fresh child. A conversation that dies because a session id went stale is a worse failure than one slow turn.

## 5. Authentication modes

Configuration, not a hardcoded choice — the two modes are mutually exclusive and different users need different ones.

```yaml
claude:
  binary: claude          # resolved from PATH if unset
  mode: subscription      # subscription | bare
```

| | `mode: subscription` | `mode: bare` |
|---|---|---|
| Flags | no `--bare` | `--bare` |
| Credentials | whatever the user's CLI is logged into | `ANTHROPIC_API_KEY` or `apiKeyHelper` only |
| Keychain / OAuth | used | never read |
| `CLAUDE_CODE_OAUTH_TOKEN` | honoured | **not** read |
| Hooks / MCP / `CLAUDE.md` | discovered | skipped |
| Best for | an individual on their own machine | CI, servers, reproducible runs |

Precedence gotcha worth surfacing in error messages: in subscription mode, if `ANTHROPIC_API_KEY` is present in the environment and approved, the API key wins over the subscription login. If a user reports unexpected charges, that variable is the first thing to check.

Cloud provider auth (Bedrock, Google Cloud, Microsoft Foundry) reads its own provider credentials and works under either mode.

## 6. Event model

A tagged union at the adapter boundary. Do not pass JSON objects around the harness — the point of the adapter is that the rest of the code never learns what a `stream_event` is.

```cpp
namespace harness {

struct TextDelta      { std::string text; };
struct ThinkingDelta  { std::string text; };
struct ThinkingTokens { int estimated; };            // text-redacted progress
struct ToolUseStart   { std::string id, name; };
struct ToolResult     { std::string id; bool is_error; std::string content; };
struct TurnComplete {
    std::string session_id;
    std::string final_text;
    std::string structured_output;   // raw JSON, empty when no schema was set (§9)
    double      cost_usd   = 0.0;
    int         num_turns  = 0;
    bool        is_error   = false;
    std::string error_subtype;       // empty on success
};
struct Notice        { std::string kind, detail; };  // init, api_retry, …

using Event = std::variant<
    TextDelta, ThinkingDelta, ThinkingTokens,
    ToolUseStart, ToolResult, TurnComplete, Notice>;

class EventSink {
public:
    virtual ~EventSink() = default;
    virtual void on_event(Event&&) = 0;
};

} // namespace harness
```

**Keep `ThinkingDelta` a distinct variant — do not merge it into `TextDelta`.** If the provider interface is a plain token channel rather than a typed sink, you will be tempted to wrap thinking in in-band markers like `<thinking>…</thinking>` and demux downstream. That works — it is what a channel-typed harness is forced into — but it costs a filter that must tolerate markers split across reads, and it creates a permanent hazard: a *literal* `<thinking>` in the answer text is now ambiguous. Apogee's typed harness gets this for free.

### Wire events to map

**[verified 2.1.233]** — observed in a real `--include-partial-messages` session (text turn with a schema, so the tool-call turn appears too):

| Wire `type` | `subtype` / `event.type` | Meaning | Maps to |
|---|---|---|---|
| `system` | `init` | session start, model, tools, session id | `Notice` |
| `system` | `status` | periodic transport status | ignore |
| `system` | `thinking_tokens` | running estimate, field `estimated_tokens` | `ThinkingTokens` |
| `system` | `post_turn_summary` | end-of-turn accounting | ignore |
| `system` | `api_retry` | transient API failure being retried | `Notice` |
| `rate_limit_event` | — | rate-limit state change | `Notice` (or ignore) |
| `stream_event` | `message_start` / `content_block_start` / `content_block_stop` / `message_delta` / `message_stop` | block framing | ignore |
| `stream_event` | `content_block_delta`, `delta.type == "text_delta"` | answer tokens, field `delta.text` | `TextDelta` |
| `stream_event` | `content_block_delta`, `delta.type == "thinking_delta"` | reasoning tokens, field `delta.thinking` | `ThinkingDelta` |
| `stream_event` | `content_block_delta`, `delta.type == "signature_delta"` | thinking signature, no payload | ignore |
| `stream_event` | `content_block_delta`, `delta.type == "input_json_delta"` | tool-call arguments streaming in | ignore, or feed a tool-args view |
| `assistant` | — | a complete assistant message | usually ignore (already streamed) |
| `user` | — | tool results being fed back | `ToolResult` |
| `result` | `success` / error subtypes | terminal event: `session_id`, `result`, `structured_output`, `total_cost_usd`, `num_turns`, `usage` | `TurnComplete` |

Ignore unknown `type` values rather than erroring. New event types get added; an unknown-type crash turns a CLI upgrade into an outage. Note how many observed types are *framing* — a strict allowlist that logs-and-drops the rest is the right shape, not a switch that assumes it has seen everything.

## 7. Reading and parsing

### Read the fd, not a `FILE*`

The child's stdout is a pipe. Wrap the read end in a fully-buffered `FILE*` and glibc will hold up to 4 KiB before handing you anything — reintroducing the exact chunking you set out to fix. Use `poll()` + `read()` on the raw fd.

```cpp
void ReaderThread::run() {
    std::string carry;               // partial line across reads
    char buf[16 * 1024];

    for (;;) {
        struct pollfd p{fd_, POLLIN, 0};
        int rc = ::poll(&p, 1, 250);          // timeout lets us observe cancel_
        if (rc < 0 && errno == EINTR) continue;
        if (cancel_.load(std::memory_order_relaxed)) break;
        if (rc == 0) continue;

        ssize_t n = ::read(fd_, buf, sizeof buf);
        if (n == 0) break;                     // EOF: child closed stdout
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            break;
        }

        carry.append(buf, static_cast<size_t>(n));

        size_t start = 0, nl;
        while ((nl = carry.find('\n', start)) != std::string::npos) {
            std::string_view line{carry.data() + start, nl - start};
            if (!line.empty()) dispatch_line(line);
            start = nl + 1;
        }
        carry.erase(0, start);
    }

    if (!carry.empty()) dispatch_line(carry);  // unterminated final line
}
```

The `carry` buffer is the part that gets skipped and then causes intermittent parse failures under load. A single `read()` will routinely land mid-object.

**Size the line buffer generously.** The `init` event alone can exceed 64 KiB once a user has MCP servers configured, and a schema echo or large tool result goes further. A fixed 64 KiB cap will work on your machine and fail on a user's.

**Tolerate non-JSON lines on stdout.** Login notices and similar have been observed interleaved with the JSONL. Skip any line whose first non-space character is not `{` rather than treating it as a parse failure.

**Note on the platform seam:** `poll`/`read`/`posix_spawn` are POSIX. Per the six-target matrix this goes behind `lib/src/cli/source/platform/` with a Windows equivalent (overlapped I/O or a reader thread on the pipe handle), or a recorded skip — not an inline `#ifdef`.

### JSON

**simdjson** is a natural fit — `iterate_many` is built for newline-delimited JSON and fast enough that parsing never shows up in a profile. **nlohmann/json** is fine parsing one line at a time, and it is Apogee's standardized pick; the throughput difference is irrelevant next to model latency. Treat every field as optional either way: a missing `cost_usd` should produce a zero, not a thrown exception mid-stream.

## 8. Rendering thinking

Moved to the terminal UX layer (shipped 2026-08-26; `lib/src/cli/source/commands/thinking_view.h`) — the terminal UX layer owns it. This backend only emits typed events. Two facts from that section matter here, because they are wire behavior rather than rendering:

**[verified 2.1.233]** Thinking arrives in two distinct forms, and the adapter must handle both:

1. **Thinking text streams.** `thinking_delta` events carry real reasoning in `delta.thinking`.
2. **Thinking text is redacted.** The newest models default to `display: "omitted"` at the API level. You still get `thinking_delta` events — but with an **empty** `thinking` field, followed by a `signature_delta`. The only live signal is `system` / `thinking_tokens` carrying `estimated_tokens`.

Case 2 is why the empty-payload guard is load-bearing: a blank `thinking_delta` that opens the thinking UI renders an empty reasoning block on every redacted turn.

**Display only, always.** Thinking must never leak into persisted conversation history, the text returned to a programmatic caller, or a served API response. Strip it at the boundary: it bloats every subsequent prompt if it re-enters history, and an OpenAI-format client does not expect reasoning in its content field.

## 9. Structured output (`--json-schema`)

For calls wanting a typed result rather than prose — classification, extraction, routing, any "clerk" that must return a record:

```
claude -p --output-format json --json-schema '{"type":"object", …}' "…"
```

**[verified 2.1.233]**, and the details are not what the flag's name suggests:

- **Inline JSON is accepted.** No temp file; pass the schema bytes directly.
- **The conforming value arrives in `structured_output`** on the `result` object, as a real JSON value. The `result` string field carries the same value serialised, so it remains a usable fallback.
- **It composes with `--output-format stream-json`.** An earlier draft advised skipping streaming for schema calls; that turned out to be unnecessary.
- **But it is implemented as a forced tool call *after* the answer.** The model first produces a normal prose answer, then the CLI makes it call an internal `StructuredOutput` tool with the conforming value. On the streaming path: the `text_delta` events are a **prose pre-answer**, the schema value appears only on the terminal `result` event, and thinking streams for *both* turns.
- **Budget one extra generation.** A trivial schema call reported `num_turns: 3`. Calling a schema-constrained clerk once per document chunk roughly doubles the cost of the job. Worth it where a malformed parse would waste the whole generation anyway; measure before applying it to a hot loop.

Consequences for the adapter:

```
schema mode:  suppress TextDelta  →  hold the prose as fallback
              forward ThinkingDelta / ThinkingTokens normally
              on `result`: emit structured_output if present,
                           else emit the held prose
```

Suppressing the prose matters: without it the user watches a full paragraph get streamed only to be replaced by a JSON object. Holding it as fallback matters too — if a future CLI stops populating `structured_output`, the clerk degrades to the prose-parsing path instead of returning nothing.

Expose this as a distinct entry point — `complete_structured(prompt, schema)` — rather than a flag threaded through the conversational path. The two have different failure modes and different cost profiles.

## 10. Threading

```
  llama.cpp token callback ─┐
                            ├──→ [ SPSC / MPSC queue ] ──→ consumer (UI, HTTP, log)
  CLI reader thread ────────┘
```

- One reader thread per child process, owning the fd and the `carry` buffer.
- One writer path, mutex-guarded, for stdin.
- A separate stderr reader. **Do not merge stderr into stdout** — diagnostics interleaved into the JSONL will break the parser at the worst possible time. Log it separately and surface it on non-zero exit; retain a bounded tail (8 KiB is plenty) so a spawn failure can be reported with what the child actually said.
- Push into a **bounded** queue with the same `Event` type from both backends. Bounded matters: if the consumer stalls you want back-pressure in your own process, not unbounded memory growth.

Cancellation: set an atomic flag, close stdin, let the `poll()` timeout notice. Avoid killing the child mid-turn — a clean stdin close lets it emit its `result` event, which is where the cost accounting lives.

**Terminal writes are shared state too.** If a spinner thread and the reader thread both draw, they will interleave and tear the line. Serialise every terminal write under one mutex. Not theoretical — a spinner repainting at 200 ms against a token stream is exactly the collision case.

## 11. Testing

Record real sessions to fixture files and replay them into the parser:

```
tests/fixtures/
  simple_text.jsonl
  thinking_text.jsonl          # thinking_delta with real payloads
  thinking_redacted.jsonl      # empty thinking_delta + thinking_tokens events
  structured_output.jsonl      # schema run: prose turn, then the tool-call turn
  tool_use.jsonl
  api_retry.jsonl
  error_max_turns.jsonl
  truncated_mid_object.jsonl   # hand-truncated, for the carry-buffer path
```

The replay harness feeds bytes in adversarial chunk sizes — 1 byte, 3 bytes, 4096 bytes, whole file — and asserts the emitted `Event` sequence is identical in every case. This catches the entire class of framing bugs with no network call and no API charge, and it is the suite that will actually save you when the CLI's schema shifts.

The renderer half of this section moved to the terminal UX layer (shipped 2026-08-26; `lib/src/cli/source/commands/thinking_view.h`) with §8.

## 12. Open items

Resolved since the first draft: `--json-schema` is characterised (§9 — a post-answer forced tool call, composes with streaming, costs an extra generation), and the thinking-render question is settled (rolling window plus collapse, made safe by pre-wrapping — now in the terminal UX layer (shipped 2026-08-26; `lib/src/cli/source/commands/thinking_view.h`)).

Still open — each carried into **Open calls** above with a default:

- Whether `complete_structured()` should also accept a streaming sink, or stay synchronous.
- Prompt-cache interaction and whether `TurnComplete.cost_usd` is meaningful for local models or always zero.
- Session persistence across harness restarts.
- Whether the thinking window's height should be user-configurable — carried to the terminal UX layer (shipped 2026-08-26; `lib/src/cli/source/commands/thinking_view.h`), which owns the renderer.

## References

- Headless mode and stream formats — https://code.claude.com/docs/en/headless
- Authentication — https://code.claude.com/docs/en/authentication
- Legal and compliance — https://code.claude.com/docs/en/legal-and-compliance

### 2026-09-06 — `ollama-cli`: the vendor-CLI family's awkward case

The fourth cloud vendor's subscription path, and the one that does not fit the family template. Built as a spawned CLI backend by user decision, over a recommendation to make it a direct HTTP backend — the characterization that recommendation rested on is kept here, because it is also what the design had to work around.

**Characterization — `ollama` 0.33.2, signed-in cloud session (`gpt-oss:20b-cloud`)**

Everything below was observed, not inferred:

| Question | Finding |
|---|---|
| Self-contained? | **No.** The CLI is a client of a local HTTP server (`OLLAMA_HOST`, default `127.0.0.1:11434`), which also answers an OpenAI-compatible API (`/v1/models` → 200). |
| Event stream? | **None.** No `--output-format stream-json`, no stdin-fed turns. So **no persistent child**: each turn is its own invocation and the whole conversation is re-sent every time. |
| Control bytes when piped? | **Yes** — the default run writes `ESC[nD ESC[K` *into stdout* to re-wrap words, even when stdout is a pipe. `--nowordwrap` removes them entirely (measured: zero ESC bytes), and is therefore **mandatory**. |
| Thinking? | In-band on stdout, between the literal markers `Thinking...` and `...done thinking.` |
| No server? | **`ollama run` tries to START one**: `Error: timed out waiting for server to start`. |

**What was built**

- [x] **`source/backends/ollama_cli_output.h/.cpp`** — the thinking demultiplexer. In-band markers are exactly what the design notes warn against, and Apogee avoids them everywhere else; here there was no alternative, so the cost is paid in one small pure state machine. Two bounds keep the unavoidable ambiguity contained: a marker counts **only on its own line**, and the opener **only before any answer text**, so a model writing "Thinking..." in its reply is never mistaken for framing.
- [x] **`source/backends/ollama_cli.h/.cpp`** — the provider, over the existing `platform::child_process` seam.
- [x] **`ollama-cli` config type** with a `host` field; **recorded** fixtures from the real session.
- [x] **13 new tests** (535 total).

**The last characterization row changed the design, and it is the important part.**

The item's original rule was "never run `ollama serve`". That turned out to be insufficient: the CLI starts a server *itself* whenever it cannot reach one. Spawning `ollama run` on a machine with no server would therefore have made Apogee the cause of a listening socket — one process removed, but ours, and a breach of a `## ⚠` invariant that every other backend satisfies by construction.

So the provider **pre-flights**: it asks whether a server is already reachable and refuses the turn if not, *before* spawning anything. Connecting outward to check is invariant-safe — the rule forbids listening, not connecting. The refusal names the fix and says why Apogee will not apply it. Both halves are mutation-tested: removing the pre-flight, or the mandatory flag, turns the relevant test red.

A second, smaller correction came out of timing that refusal end to end: it took **3.5 seconds**, because the shared `HttpClient` retries with backoff. That policy is right for a real request over a flaky network and wrong for a localhost probe, where a refused connection is an immediate and certain answer — retrying only makes the refusal take four times as long. The probe now makes a single attempt: **3.5s → 0.23s**.

**Verified live**, against the user's own signed-in CLI: `apogee complete -m oll "Say exactly: hello"` prints exactly `hello` — thinking demuxed away, piped output clean — and the dead-host backend refuses in 0.23s without spawning anything.

**Recorded honestly, per the family rule that a template is a shape to aim at rather than a promise to fake:** this backend is the weakest in Apogee. There is no persistent child, so every turn pays process startup; there is no turn protocol, so the entire conversation is re-sent as one flattened prompt each time and the CLI remembers nothing; streaming granularity is whatever falls out of reading stdout; and the CLI reports no token accounting, so the loop's estimator fills the gap. The direct-HTTP alternative remains recorded in case the trade is ever revisited.

### 2026-09-06 — `codex-cli`: OpenAI's subscription path, and a safety pin

The second vendor CLI, and the one that sits between the other two in what the vendor gives you. Characterized against `codex-cli` 0.153.4 under a real ChatGPT login before any adapter code, per the family rule.

**Characterization**

| Question | Finding |
|---|---|
| Event stream? | **Yes** — `codex exec --json` emits typed JSONL (`thread.started`, `turn.started`, `item.completed`, `turn.completed`), so the shared `jsonl_framer` applies directly. |
| Streaming granularity? | **Message-level.** A twelve-line answer arrived in a *single* `item.completed`; there are no delta events in this mode at all. Less than Claude's token stream, and recorded rather than implied. |
| Usage? | **Real**, on `turn.completed` — unlike Ollama's, which reports none. |
| Session / resume? | **Yes, verified end to end.** `thread.started` carries a `thread_id`; `codex exec resume <id>` continued a thread and correctly recalled the earlier turn's subject. |
| Thinking? | **Counted, never surfaced** — `reasoning_output_tokens` appears in usage, but no reasoning item is emitted. Nothing to feed the thinking view. |
| Structured output? | `--output-schema <FILE>` — a **file path**, the opposite of Claude's inline `--json-schema`; the conforming JSON arrives as the `agent_message.text` itself. |
| Auth modes? | None. No `--bare` analogue, so the family's `mode` field is **not** invented here for symmetry — a config carrying one is refused with an explanation. |

**What was built**

- [x] **`source/backends/codex_cli_events.h/.cpp`** — wire→typed-event mapping over the shared framer.
- [x] **`source/backends/codex_cli.h/.cpp`** — the provider. No persistent child (the CLI has no stdin turn stream), but no re-sending of history either: turn one spawns `exec`, every later turn spawns `exec resume <thread_id>`, and only the new user message goes out.
- [x] **`codex-cli` config type**, and **recorded** fixtures from the real session.
- [x] **13 new tests** (548 total).

**Two findings from the characterization shaped the code, and one of them is a safety decision.**

**`codex exec` is an agent, not a chat endpoint.** Its `-s/--sandbox` flag selects a policy for *model-generated shell commands it will execute on the user's machine*. Every other backend Apogee drives only produces text. So the sandbox is pinned to `read-only` as a literal in the argv builder and is deliberately **not** configurable: a config key able to widen it would turn an ordinary chat turn into arbitrary local execution, which is not a trade a chat backend gets to offer. The test asserts the pin and that none of `workspace-write`, `danger-full-access`, `--dangerously-bypass-approvals-and-sandbox`, or `--yolo` can appear.

**`codex exec resume` does not accept `codex exec`'s flags.** Passing `--color` to the subcommand fails with "unexpected argument" — found by trying it. A single shared argv builder would therefore have worked on turn one and broken on turn two, which is the worst place to discover it, so `build_arguments` takes the invocation form explicitly. Both rules are mutation-tested: widening the sandbox or sharing the builder turns the relevant test red.

**Verified live** against the user's own ChatGPT login: `apogee complete -m cdx "Say exactly: hello"` prints exactly `hello`. A first attempt with `model: gpt-5-codex` failed, and usefully — the backend surfaced the CLI's own message verbatim ("not supported when using Codex with a ChatGPT account"), which is exactly what the error path is for. With no model pinned, the CLI's default is used and the turn succeeds.

**Where this backend stands in the family:** better than `ollama-cli` (typed events, real usage, real session continuity) and worse than `claude-cli` (no token-level streaming, no thinking to display). Recorded plainly rather than averaged into a claim of parity.

---

### 2026-09-06 — `gemini-cli`: Google's subscription path, and a test that never ran

The fourth and last vendor CLI, completing SPEC's dual-path claim: every cloud vendor now works under both a subscription plan and an API billing plan, chosen per backend entry. Characterized against `gemini` 0.46.0 under a real Google login before any adapter code, per the family rule — and the recording **corrected three things** the item document had written down when no login was available.

**Characterization**

| Question | Finding |
|---|---|
| Event stream? | **Yes** — `-o stream-json` emits typed JSONL (`init`, `message`, `tool_use`, `tool_result`, `result`), so the shared `jsonl_framer` applies. |
| Streaming granularity? | **Token-level, and genuinely so.** A twenty-line answer arrived in three assistant deltas, and one boundary fell *inside* the number 13 (`…12\n1` then `3\n14…`). Best in the family after Claude. |
| Usage? | **Real**, on `result.stats` — and broken down per model. |
| Session / resume? | **Yes, and better than the others.** `--session-id <uuid>` lets Apogee *choose* the id, so there is nothing to capture from the stream and no session file to read. Verified end to end. |
| Which model answers? | **`auto`, and more than one per turn.** `init` reports `"model":"auto"`; `result.stats.models` named two models in every recording. The adapter reports the one with the most output tokens. |
| stdout hygiene? | **Clean.** The 256-color warning and `[STARTUP]` lines go to stderr unprompted; nothing had to be filtered. |
| Auth modes? | None. No `--bare` analogue, so `mode` is **not** invented for symmetry — a config carrying one is refused with an explanation pointing at the `google` backend. |

**Three corrections to the pre-login characterization**

| The item document said | The recording showed |
|---|---|
| `--resume` takes `latest` or an **index number** "(not a uuid)" | It takes the **UUID**. This is what makes the clean continuity story possible. |
| `--skip-trust` suppresses the approval-mode override | It does — *and* without it a headless run in an untrusted directory **refuses to start at all**. It is mandatory, not an optimisation. |
| The read-only `plan` pin may be silently overridden | It is — but **not** with `--skip-trust`. Verified against a canary: asked to write a file, the child refused and wrote nothing. |

**What was built**

- [x] **`source/backends/gemini_cli_events.h/.cpp`** — wire→typed-event mapping over the shared framer.
- [x] **`source/backends/gemini_cli.h/.cpp`** — the provider. No persistent child (no stdin turn stream), but Apogee generates the session UUID up front: turn one passes `--session-id`, every later turn passes `--resume <same uuid>`, and only the new user message goes out.
- [x] **`TurnComplete::model`** on the shared event union — "which model answered" is turn accounting, and this is the first CLI where it is not simply what was asked for.
- [x] **`gemini-cli` config type**, documented in the starter config including the `GEMINI_API_KEY` precedence trap and the two pinned flags.
- [x] **`cli.no_vendor_credentials` widened to the whole family** — it had only ever covered Claude's paths, so three of four backends were unguarded. Now covers `.gemini/`, `google_accounts.json`, `oauth_creds.json`, and `.codex/`. `~/.ollama/` is deliberately excluded and the reason is written down: it is a *model* store, and reading it is a supported source (shipped — Milestone N).
- [x] **16 new tests** (588 total), with **recorded** fixtures from the real session.

**The safety pin needed two flags, not one.** `--approval-mode plan` alone pins nothing: this CLI was observed replacing it with `default` in an untrusted folder, and Apogee spawns wherever the user's shell happens to be. So `plan` and `--skip-trust` are both literals in the argv builder, asserted together, and mutation-tested together — removing either turns the safety test red. `--raw-output` (which disables output sanitisation and which the CLI itself calls a security risk) can never appear.

**A test that passed without ever running.**

The test asserting the answering model is reported was named `the model that answered is reported, not "auto"`. `catch_discover_tests` registers each case by passing its **name** to the binary as a filter — and the embedded quotes broke the generated command's shell quoting, splitting one filter into two, neither of which matched anything. Catch2 printed "No tests ran" and **exited 0**. The ctest entry passed. It had never executed.

It surfaced only because the guardrail was mutation-tested: hard-coding `response.model = "auto"` — precisely what the test forbids — came back 100% green. Running the case by tag instead showed it failing correctly all along. The test was right; its *name* made it unrunnable.

`tests/test_names.cmake` already existed for the sibling bug (a leading dash is read as an option, which cost real time twice), so the quote case was added there rather than in a second checker. Its first draft did not fire either, for a second-order reason worth keeping: **`if(... MATCHES ...)` clobbers `CMAKE_MATCH_1`**, so testing the capture twice in a row silently tests an empty string the second time. The capture is now saved before either check. Both checks are mutation-tested.

The general lesson is the one this repo already applies elsewhere and had not applied to test *registration*: a check that can pass vacuously will eventually pass vacuously, and "no tests ran" is the most expensive kind of green there is.

**Verified live** against the user's own Google login: `apogee complete -m gem-sub` answers, an API-billing `google` entry and a subscription `gemini-cli` entry coexist in one config and are selectable per entry, and through machine mode the concatenated `answer_delta`s equal `result.text` exactly.

**Two things recorded rather than fixed.**

*The answering model does not reach the user.* The provider reports it (unit-tested at the seam), but `complete` builds a fresh `ChatResponse` from the agent loop's `RunResult` and sets `response.model` to the backend selector. That is pre-existing and affects every backend — the loop's result type carries no model — so surfacing it is its own item, not a change smuggled in here.

*This backend is not in the cross-provider conformance table.* Neither are its three siblings, and for a structural reason: that table scripts a **tool-calling** turn, and no vendor-CLI backend surfaces a `harness::ToolCall` — each runs tools inside the CLI and emits only text. The item asked for a conformance row; the honest answer is that the table's contract does not fit this family, and the family's own shared guardrail — fixture replay at adversarial chunk sizes — is what covers it instead.

**Where this backend stands in the family:** the best of the four on session handling (Apogee owns the id), second on streaming (real deltas, behind Claude's), and the only one whose safety pin needs two flags to hold.

---

## Milestone M — The front-end contract

**Goal.** The protocol a GUI drives Apogee over: `--output-format stream-json` and `--input-format stream-json`, emitting the typed event stream the Reporter seam already carries as JSONL on stdout, and accepting user turns and answers as JSONL on stdin. The GUI sibling project gates on this.

### 2026-09-06 — `stdio-machine-mode`: Apogee on the other side of the protocol

**The framing that made this cheap.** Apogee already consumes exactly this kind of protocol from four vendor CLIs. Building the emitter was mostly a matter of *owing its own consumers every discipline it demands of them* — framing-safe lines, diagnostics off stdout, tolerance of unknown event types, no listening socket. Where a design question came up, the answer was usually "what did we wish that CLI had done", and the vendor-CLI work had already produced the answer.

**What was built**

- [x] **`source/commands/json_reporter.h/.cpp`** — the `Reporter`→JSONL adapter, sibling of `CliReporter` over the same seam. Ten event types, each one a `Reporter` method: `session`, `thinking`, `thinking_delta`, `tool_status`, `answer_start`, `answer_delta`, `answer_end`, `result`, `question`, `error`. Nothing invented, aggregated, or renamed — a second vocabulary would be a second thing to keep in sync, and the first time it drifted a driver would see an event the terminal never shows.
- [x] **`--output-format` on `complete` and `chat`**, and **`--input-format` on `chat`** — the latter is what makes one child serve a whole conversation.
- [x] **A shared `run_chat_turn()`** extracted from `chat.cpp` so the terminal REPL and the driven loop are literally the same turn, not two implementations that agree today.
- [x] **`ask_user` over the protocol** — a `question` event answered by an `answer` line, so a driving GUI renders a native dialog instead of the tool being silently unavailable on the one surface built for a real UI.
- [x] **[`documentation/reference/machine-mode.md`](../reference/machine-mode.md)** — the protocol reference, and the repo's first document written for *external consumers* rather than contributors.
- [x] **24 new tests** (572 total).

**Three decisions worth recording.**

**`on_clear_status()` deliberately emits nothing.** Erasing a transient indicator is a terminal concern; there is nothing to erase in a stream of records. An event for it would put a rendering detail into the protocol, which is how a second vocabulary starts growing out of the first.

**Absent usage is not zero usage.** A turn where the provider reported no token counts omits the `usage` field rather than sending zeros. A driver displaying `0` would be stating a measurement nobody made.

**Contradictory format flags are refused, not half-honoured.** `--input-format stream-json --output-format text` is an error. A JSONL-emitting REPL has no coherent meaning — slash commands print through the terminal reporter and have no protocol event — and a driven session rendering prose gives its driver nothing to parse. The alternative was accepting the flag and ignoring it, which is how a driver ends up debugging output it never asked for. **This was a real bug found by running all four combinations by hand:** both mixed cases silently dropped a flag the user had passed, and the comment in the source claimed they were supported.

**The guardrails, all mutation-tested.**

| Guardrail | The mutation that proves it bites |
|---|---|
| `cli.machine_mode` — every stdout line opens with `{` | One `std::cout << "apogee: warming up\n"` in the machine branch → caught |
| `cli.machine_mode` — one child, many turns | `while` → one-shot in the driven loop → "expected 2 result events, got 1" |
| Thinking distinctly typed | `thinking_delta` → `answer_delta` → two tests red |
| `cli.reference_driver` — the deltas rebuild the result | `result.text` + `" [truncated]"` → caught on both turns |
| `cli.machine_schema_conformance` | Removing an event from the doc, and adding one the code cannot emit → caught in both directions |

**The reference driver is documentation and test at once.** [`tests/reference_driver.py`](../../src/cli/tests/scripts/py/reference_driver.py) is the worked example a GUI author reads, and it checks that three independent paths agree: the concatenated `answer_delta` chunks, the `result` event's text, and what `apogee complete` printed in text mode. If they ever disagree, one surface has grown a behaviour the other lacks — the failure the shared Reporter seam exists to prevent.

**The schema document is pinned to the code.** A protocol document that drifts is worse than none: a GUI author trusts it, builds against it, and debugs Apogee for a fault that is in the prose. `cli.machine_schema_conformance` checks the vocabulary in both directions, so an event added without documentation, or documented without an implementation, fails the build.

**What this deliberately does not do.** No push channel (a driving GUI performs its own mutations by shelling out to `apogee config …`, so it already knows when to re-read); no socket, ever (`lsof`, sampled continuously while the child lives); no protocol representation of slash commands, which are terminal-REPL affordances a driver replaces with its own UI.

### 2026-09-25 — The integration spike: a naive host embeds the binary (backlog item 28, for v0.1.5)

Asked for by the user (2026-09-25): the CLI pluggable into **other people's** harnesses and applications, with the native machine mode as the floor and a common protocol integrators extend from. The spike's instrument is [`tests/naive_host_driver.py`](../../src/cli/tests/scripts/py/naive_host_driver.py) — a third-party-style host, kept as evidence and re-runnable (`naive_host_driver.py <binary> <work-dir>`), that knows **only what machine-mode.md says**: it may not learn from Apogee's source, and where the documented contract leaves it blind it records a wall instead of peeking. It ran against the installed `v0.1.2` binary (the shipped contract an integrator meets today) in a throwaway `APOGEE_HOME`, with a scripted `mock` backend as the model actor.

**What worked, exactly as documented.** The host completed a tool-using conversation end to end: `ask_user` flowed out as a `question` event and the answer back in; the permission gate's `question` (`kind:"permission"`, `tool`, `target`) arrived, was answered `yes`, and the tool ran; a failed tool came back **as a tool result the model read**, and the turn continued to a clean `result` — the denial-semantics contract holding under a real failure. One child served both turns; stdout carried nothing but JSONL; an unknown *inbound* line was ignored exactly as the doc promises. And the headline measurement: **host-supplied tools already work today** — the host ran a 40-line MCP stdio server, registered it with `apogee mcp create`, and its tool round-tripped through the loop (`mcp__host__host_lookup` → `host-answer:…` in the final text) with **zero prompts**, the `readOnlyHint` honoured.

**The seven walls** (full evidence in the probe's `findings/walls.md`):

| # | Wall |
|---|---|
| W1 | No handshake or discovery: the host learns `protocol_version` only from the `session` event after spawning, and cannot declare itself or ask what the binary supports (`session` carries only `model` + `protocol_version`) |
| W2 | No machine-readable schema: the host hand-transcribes the event vocabulary from prose; nothing ships to validate a stream against |
| W3 | No turn or correlation ids: events belong to "the current turn" by position only, so a host cannot pipeline or attribute after a race |
| W4 | No cancel: the only exits from an in-flight turn are killing the child or failing the turn by closing stdin |
| W6 | Host tools need a config mutation: `mcp create` edits the install's config — global state a host must mutate and clean up to wire tools for one child; no per-run flag |
| W7 | Reads are prose: "everything else is a CLI command", but the read commands emit human text, so a host UI screen-scrapes `apogee models` or re-reads config files |
| W8 | The child's tool sandbox is scoped to the *user's* config, not the host's workspace: `write_file` into the host's own project was refused ("outside the allowed root `/Users/taylor`") because `tools.fs_root` defaults to the user's home — no per-run scoping exists |

(W5 — undocumented outbound events — did not fire: the v0.1.2 stream is exactly its documented vocabulary.)

**The recommendation: grow the JSONL contract; do not reframe it.** JSON-RPC/LSP framing would break every `protocol_version: 1` driver to buy request/response multiplexing the walls do not demand — turns serialize by design, and the one axis that wants a peer protocol (host tools) is **already answered by MCP as a sidecar**, proven above, wanting only per-run wiring. The decisive finding is that the existing tolerance rules make the contract **retrofittable in both directions**: an unknown inbound line is ignored (verified live), so a new host can send a `hello` to an old binary harmlessly, and rule 1 means an old host survives every additive event. The gaps close as additions: a handshake and a written stability promise (W1), a schema artifact pinned like the prose doc (W2), turn ids and an inbound cancel (W3, W4), per-run wiring for host MCP servers and the file-tool root (W6, W8 — W8's *default* also changes under item 25a's launch-folder rule, which shipped later the same day, [Milestone V](#milestone-v--the-native-toolsets); the spike's evidence is the v0.1.2 binary, and the per-run declaration remains the integration half), and `--output-format json` on the read commands a host UI needs (W7).

**Split (2026-09-25), all five specced into the v0.1.5 table:** 28d the handshake and the stability promise → 28e per-run integration wiring → 28f turn ids and cancel → 28g the schema artifact → 28h machine-readable reads. **Parked with evidence, the user's call:** a push channel (v1's "events arrive in response to turns, never unprompted" held comfortably for an embedding host — the case for push is config/model change notification for long-lived embeds, and 28d's capability field is where it would negotiate if ever wanted). A SPEC revision naming third-party embedding as a product surface is proposed alongside the split rather than made unilaterally.

---

## Milestone N — Model operations

**Goal.** Model management, end to end: one shared resolver for the `models:` role pointers, the `apogee models` suite, a real GGUF header reader that `check` uses to tell a working model from a broken one, and — from 2026-09-07 — acquiring, quantizing, and repairing models from Hugging Face and the user's Ollama store without ever leaving a half-downloaded one on disk. From 2026-09-28 (26b), helper models beside the chat model: `vision`, `transcription` and `utility` in the same resolver.

### 2026-09-07 — `model-operations`: one resolver, and a check that stops lying

**What was built**

- [x] **`source/harness/roles.h/.cpp`** — the one resolution chain: `override > per-feature pin > role pointer > models.default`, with whitespace trimmed at every rung. `resolve_backend()` also reports **which rung answered**, so `models status` can say "(via models.default)" without re-walking the chain at the call site.
- [x] **`source/models/gguf_inspect.h/.cpp`** — a self-contained GGUF header reader: architecture, container version, tensor counts, and a text-vs-vision split, with every self-declared length bounds-checked against the real file size.
- [x] **`source/commands/models.h/.cpp`** — `list` (aligned table, or JSONL under `--output-format stream-json`), `info <backend>`, and `status`.
- [x] **Three call sites migrated** to the resolver — `harness.cpp`, `chat.cpp`, `complete.cpp` — plus `cli.one_role_resolver`, a mechanical check that no fourth one appears.
- [x] **34 new tests** (622 total).

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| GGUF reader | **Ours, not llama.cpp's** — reversing the item's recorded decision | The item chose llama.cpp's `gguf.h` so that "the header parses" and "llama.cpp can read it" would be one claim. But llama.cpp is **off by default**, and `macos-arm64` — the only merge-blocking CI target — builds without it. That reader would leave `check` and `models info` reporting nothing in the default build, and would mean this item's own guardrail never ran in the job that gates merges. A check that cannot run where it matters is not a check. |
| Fixtures | Built in the test, not committed | The cases that matter are the *malformed* ones — a truncated download, a length running past the end, an LFS pointer. Those cannot be downloaded; they have to be constructed. Building them also keeps the bytes readable in a diff. |
| `models list` columns | `ARCH` and `PROFILE` kept **separate** | An architecture is what the file says it is; a profile is what Apogee knows about how that family behaves. Until model-profiles lands, every row reads `unprofiled` — true and useful. Showing the architecture in that column would claim knowledge Apogee does not have. |
| 14b gating | No dependency on model-profiles | Recorded at grooming so this item could be built while model downloads were still in flight — which is exactly how it was taken. |

**`apogee check` was reporting "model loads" on the strength of four bytes.**

The Config section validated a `model_path` with a magic-bytes check and then printed `llamacpp -- model loads`. A half-finished download starts with a perfectly good `GGUF` magic, so the row said "model loads" for precisely the file that cannot be loaded — a claim four bytes cannot support, worded exactly like a pass. Both call sites now do a full header parse; the row says what it actually verified.

The existing test for this passed a fixture of `"GGUF"` plus 64 nul bytes, which the new parser also accepts (it is a valid empty header), so the suite stayed green while asserting nothing about the upgrade. It now uses a real minimal GGUF and there is a new case for the truncated file — the one that separates a header read from a magic glance. Reverting to magic-only turns it red.

**Two duplicates found by writing the guard.**

Adding `check_models` as a new function collided with an existing `check_models` that `run_checks` already called — an overload that would never have run. And the configured-path validation it was meant to add already existed in the Config section. Both were caught before they landed; what survives is one upgraded check rather than a second one beside it. `has_gguf_magic` is gone, its recorded lesson moved to the reader that now does the work.

**Guardrails, each mutation-tested (thirteen mutations, all caught).**

| Guardrail | Mutation that proved it bites |
|---|---|
| The chain's rung order | Role pointer promoted above the override → 2 tests red |
| Whitespace trimming | Trim removed → the typo test red |
| Full header parse | Accept on magic alone → 5 reader tests red |
| No partial results on failure | Keep fields after a failed parse → truncation test red |
| Self-declared length caps | Cap raised to 2^64 → the huge-string test red |
| `ARCH` ≠ `PROFILE` | Architecture copied into profile → separation test red |
| Roles column uses the resolver | Column computed from raw config → resolver test red |
| `info` states the reason | Reason dropped from the failure line → info test red |
| `check` rejects a bad GGUF | Forced `parsed = true` → check test red |
| One resolution chain | Second chain reintroduced in `complete.cpp` → `cli.one_role_resolver` red |
| …and its own vacuous-pass guard | Allowlist swallowing every file → refuses to run |
| Completion is registry-derived | `models` unregistered → completion test red |

Two of those mutations initially came back **green**, and both were test bugs worth recording. The `ARCH`/`PROFILE` separation was asserted only over rows whose files were unreadable, so the branch that sets the architecture never ran — the test was vacuous for the property it named. And `info`'s "states the reason" assertion searched the whole body for the filename, which also appears on the `model_path:` line, so deleting the reason from the failure line left it passing. Both now assert what they claim.

**A dangling reference, found by the tests it broke.** Three tests bound `const ModelRow&` to a row inside a temporary vector returned by `build_model_rows(...)`; the vector died at the end of the statement and the fields read as empty strings. The rvalue overload of the helper is now `= delete`, so the mistake is a compile error rather than a test reading freed memory.

**Verified live** against the user's own models: `models list` reports `llama` and `qwen35` from real headers, `models status` names the rung each role resolved through, `check` passes the good files and fails the dangling one, and the JSONL listing is one object per line. A header read on a **71 GB** model takes **0.6 s**, which is what makes running it on every `check` affordable rather than theoretical — and it correctly reported `qwen35moe`, a MoE variant none of the fixtures cover.

**What is deliberately not here.** *Verification state* (a sidecar's `verified` flag) has no source yet — it arrives with model-acquisition, and inventing a column for it now would be a placeholder claiming a fact. *Loadability* is not asserted either: a full load costs gigabytes of I/O, so the reader claims only that the header is well formed, and says so rather than implying more.

### 2026-09-07 — `model-acquisition`: the copy → verify → commit ladder

**What was built**

- [x] **`source/models/acquire.h/.cpp`** — the ladder: stream to `<name>.partial`, check size (when declared), check sha256 (when published), parse the GGUF header, and only then rename. A failure at any rung removes the partial and lands **nothing**.
- [x] **`source/models/sidecar.h/.cpp`** — the provenance/integrity record, with what the source *claimed* and what is *on disk* as separate fields from the first version.
- [x] **`source/models/sha256.h/.cpp`** — streaming SHA-256, because nothing in the build hashed and adding OpenSSL across six targets for one function is the worse trade.
- [x] **`source/models/source_ollama.h/.cpp`** — the store reader: manifest → model layer → blob, `$OLLAMA_MODELS` honoured, **read-only by construction**.
- [x] **`source/models/source_hf.h/.cpp`** — Hugging Face, entirely new: ref grammar, repository listing, and a download over the existing HTTP transport.
- [x] **`source/commands/models_pull.h/.cpp`** — `pull`, `delete`, `repair`, in their own translation unit so "what can this command destroy?" has a short answer.
- [x] **`models list` widened** to show models on disk and a `VERIFIED` column.
- [x] **68 new tests** (687 total).

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| The end-of-install offer | **There is none** *(user call)* | Installing installs; nothing is downloaded and nothing is asked. This removed a whole limb of the item — no offer module, no decline-marker file, no install-path wiring — and is the reading most faithful to SPEC's *installers download nothing unasked*. |
| A repo with several GGUFs | **Refuse and list them** | A quantised upload holds a dozen precisions differing by gigabytes and by quality. Picking one spends the user's bandwidth on a file they did not choose. Verified live: a real repo returned 12 files and all 12 were named. |
| Ollama store mutation | **Never** | Its blobs are shared between models, so removing one corrupts every sibling. `ollama rm` is the only supported removal; the module has no delete path to misuse. |
| SHA-256 | Ours, ~120 lines | Nothing in the build hashed; libcurl exposes no portable digest API and OpenSSL is not a dependency. |

**The verification report is a set of checks, not a boolean — and that is the whole design.**

A harness that *chooses* its models can always compare against a pinned digest. Apogee has no allowlist by policy, so most sources publish no digest at all. Three states have to stay distinguishable: a digest matched, a digest did **not** match, and no digest was ever published. Collapsing the third into the second would train a user to ignore the word. So nothing here ever prints the bare word "verified" — the live pull reports `no digest published, header ok`, which is exactly what happened.

**A live 460 MB pull** from `TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF` landed in 43 s, wrote its sidecar, and `repair` then reported `size ok, digest ok, header ok` — because the sidecar records the *on-disk* digest even when the source published none. That is the provenance/integrity split earning its place: at pull time there was nothing to compare against, and afterwards there is.

**Three things running it found that the tests had not.**

*`models list` could not see a pulled model.* Immediately after that 460 MB download the listing said "no backends configured" — it was built (in Milestone N's first half) from `backends:` alone, and a freshly acquired model is not in the config. It now lists both halves.

*A cloud model was reported as never pulled.* `gpt-oss:20b-cloud` **is** in the Ollama store; its manifest simply carries an empty `layers` array because the weights are on Ollama's servers. Telling that user to run `ollama pull` sends them to re-fetch what they already have. `store_has_manifest` now separates the two cases.

*Hugging Face answers 401 for a repository that does not exist,* not just for a gated one — it will not leak which. The message asserted "gated or private", sending anyone with a typo hunting for a licence to accept. It now names the likelier cause first.

**Guardrails, each mutation-tested (nine mutations, all caught).** Leaving the `.partial` behind, committing before verifying, allowing a `..` in a model name, printing a bare "verified", guessing a quantisation instead of refusing, treating a cloud manifest as local weights, corrupting one SHA-256 round constant, dropping the on-disk half of the listing, and claiming a model with no record is verified — each turns its own tests red.

Two of those mutations needed a second attempt, and both times the *mutation* was at fault rather than the test: the first "commit early" mutation also moved the cleanup, so it preserved the very property it was meant to break.

**A flaky test, caught by mutation testing rather than by a run.** Two listing tests shared one temp path, and the fixture's destructor removed the `.gguf` but not the `.json` beside it — so one test's sidecar leaked into the other and the result depended on order. Each fixture now gets its own directory, and the suite was re-run under randomised orders to confirm it.

**What is NOT done, and why.** Three of this item's acceptance criteria are unmet and the backlog document stays open for exactly them: **in-process `quantize`** (needs llama.cpp linked, which is off by default — the same constraint that shaped the GGUF reader), the **vision transform** for Ollama's combined text+vision blobs (an open question then, answered the same day in Milestone O: mtmd needs a separate projector, so the operation required is an *extract* rather than a strip), and **SafeTensors/dataset** downloads, which SPEC lists in scope and which only the GGUF path covers today.

---

### 2026-09-23 — the model store: one directory per model, per format, per set of weights; `models convert`; and a pull that had been destroying its own snapshots

Asked for directly (Taylor, 2026-09-23), in three steps that each exposed the next: a way to turn a pulled SafeTensors snapshot into a GGUF without hand-running llama.cpp's converter; then a layout where every model's files live together, `safetensors/` and `gguf/` inside its directory, **each download or training output in its own directory named by the weights' own hash**, so nothing a later pull or fine-tune produces can overwrite an earlier one; then training's output in the same structure (the user's call, in this change rather than later), and old models moved automatically.

**The bug first, because it decided the rest.** `acquire_tree` wrote each file's download record beside it under a name made by swapping the extension for `.json`. In a repository that name is usually taken: `config.json`'s record *is* `config.json`, and `tokenizer.model`'s is `tokenizer.json`. Every `models pull --safetensors` since Milestone Z replaced the model's configuration and tokenizer with download records -- and reported "verified". Nothing read those per-file records back (the snapshot has one `apogee-snapshot.json` listing every file's real size and sha256), so the fix is to return them and never write them. Found only because the first real conversion needed `config.json`; the user's own 52 GB pull was damaged. The record that was always right is also what repairs a damaged snapshot in place: `models repair` fetches only what is missing, the wrong size, not what its digest says, or a download record, each copy checked against the record before it replaces anything -- seconds, not 52 GB.

**What was built**

- [x] **`models/store.h/.cpp` -- the store, declared once.** `<models>/<model>/gguf/<id>/` and `<root>/<model>/safetensors/<id>/` (`paths.hf_dir` as the SafeTensors root when set; both roots searched). An id is 12 hex of a sha256 -- the GGUF's, or a digest over every shard's -- so identical weights find the directory they already occupy; a random id of the same shape where nothing can be hashed. Stage, then commit by rename; an existing id wins. Every writer, finder, lister and remover asks it; `legacy_refusal` names the migration for anything only the flat layout has.
- [x] **`apogee models convert <model> [--from <id>] [--type f16]`** over the vendored converter in Apogee's Python environment (`training/convert.h`: one `script_converter`, shared with `train promote`). Newest SafeTensors set by default; into the same model's `gguf/`. Refusals come before anything is announced; progress is the growing `.partial` against an estimate read from the shard headers (the converter logs to a stderr Apogee captures); Ctrl-C terminates the child and removes the partial (`InterruptScope`, moved out of `train.cpp`); the GGUF is header-verified before it is committed. Verified on the real binary against a real Hugging Face model and a real cancel.
- [x] **pull, quantize, delete, repair, list, check** on the store. `quantize` starts from the newest *unquantized* GGUF -- never re-quantizes by default. `delete` removes whole `<id>` directories and warns when a backend points into one. `check` names each stored model by the handle the other verbs take.
- [x] **`apogee models migrate [--yes]`** -- a command, not a `check --fix` step, because moving a model repoints the `model_path` naming it and `check` never edits the config. Moves the flat files (snapshots first, so a GGUF from the same repository cannot land inside a snapshot that is about to move), then repoints `model_path` and the new `set_backend_mmproj_path` through the one config editor, rebinds any vector collection that recorded an old path as its embedding model (a llamacpp backend without `model:` is identified by its path, and a stale binding silently demotes a collection to lexical), moves promoted versions out of `training/versions/` beside the model they were trained from, rewrites run and pipeline manifests, and repairs damaged snapshots.
- [x] **The converter's own `gguf` package, vendored beside it** (`third_party/llama.cpp-convert/gguf-py/`). The first real `models convert` of Qwen3.8-27B failed with `Can not map tensor 'model.layers.64.eh_proj.weight'`: the pinned `conversion/qwen.py` maps Qwen3.5's multi-token-prediction layer, but the tensor names it maps to live in `gguf/constants.py`, and PyPI's `gguf` 0.19.0 predates them -- while the pin's own `gguf-py`, also 0.19.0, has them. Upstream changes the two together and does not bump the version with each change, so no PyPI floor could have caught it. This reverses the 2026-09-19 `training-run` decision that `gguf` "still comes from PyPI": the script's own `sys.path` tweak imports a `gguf-py/` beside it ahead of site-packages, so the seeded copy is the one used (the `convert` set keeps PyPI's `gguf` for its dependencies). `converter_unavailable` now refuses an incomplete seeded tree rather than letting the script fall back silently, `check`'s converter row counts missing files (it read a partial tree as "matches"), and `explain_converter_failure` adds one plain line to a `Can not map tensor` failure as it already did for an unknown architecture.
- [x] **Training in the store.** `train promote` builds in a staging directory under the base model and commits the verified GGUF (`<backend>-v<N>.gguf`, with a record) under its hash before the config or ledger is touched -- "a failing candidate never reaches inference" is unchanged. `--keep-fused` puts the fine-tuned weights into the base model's `safetensors/`; ledger entries gain `fused_path`. Retention prunes through a remover closure (the store's business, not `training/`'s), and never removes a file another kept version records -- the same run promoted twice is the same weights, one stored file. `train run` names a student the way `convert` does. The mock trainer's GGUF now carries its run in `general.name`, so two runs' outputs differ as real fine-tunes do.

**Trade-offs.** Content addressing means identical weights are one directory: promoting the same run twice records two versions over one file, and pruning follows references rather than version numbers. A Hugging Face single GGUF has no digest until it lands, so an identical re-pull downloads before it is recognised (Ollama and SafeTensors pulls are recognised before a byte moves). `--keep-fused` stays opt-in: full weights are gigabytes per fine-tune.

**Verification.** 1474 unit cases and the 35 executable cases, including `cli.train_lifecycle` updated to read every promoted path back through `config get` rather than a hard-coded layout, the snapshot fix mutation-checked (re-enabling the per-file write fails the new byte-for-byte tree test), and `models migrate` end to end in process: a flat GGUF with projector, a flat snapshot, a promoted version, a manifest and a collection binding, all repointed, the config byte-exact but for the paths. The `gguf` fix against the real 27B snapshot: the converter's dry run maps all 866 tensors (the MTP layer as `blk.64.nextn.*`) with the vendored package and reproduces the user's exact error with `NO_LOCAL_GGUF=1`; then the installed binary's `apogee models convert Qwen/Qwen3.8-27B` wrote and header-verified the 50 GiB F16 GGUF (qwen35, 866 tensors) into `Qwen--Qwen3.8-27B/gguf/47226ca8859c/` at about 1 GiB/s.

### 2026-09-23 — `models convert` makes the projector; a hybrid model's second turn; the chat display

Asked for directly (Taylor, 2026-09-23): "Add the image projector to convert" -- Qwen3.8-27B reads images, and the first conversion made only its text model. While that was being built, the same model in `apogee chat` failed on every second turn and printed its answers badly ("the formatting here is HORRIBLE"), and a Markdown renderer was asked to be planned.

**What was built**

- [x] **The projector, beside its model.** When a snapshot's `config.json` declares a vision or audio encoder (`vision_config`, `audio_config`, `whisper_config`; not when it says `language_model_only`), `models convert` runs the converter a second time with `--mmproj` into the same staging directory, so the projector (`<stem>-mmproj.gguf`, with its record) is committed with the model it belongs to. The size estimate is split between the two by tensor name. A projector the converter cannot make is a warning -- the model is kept, and it says the model cannot read images without one. The closing hint is one command: `config add-backend … --model-path … --mmproj-path …`, a flag `add-backend` gains here (`pull` prints the same command).
- [x] **A conversion is recognised before it runs.** The same SafeTensors set at the same precision is found by its record (`find_conversion`): a second `convert` makes only a projector the first lacked -- which is exactly the case of every model converted before today -- and otherwise says it is already converted and how to redo it. `commit_weights` lets a projector join a stored model directory that has none (the id is the model file's hash alone), never replacing one; an Ollama projector that failed to copy the first time is picked up the same way.
- [x] **`quantize` brings the projector.** The quantized copy's directory gets its source's projector as a hard link (a copy across filesystems) with its record, so the smaller model still reads images. **Correction (2026-09-24): claimed here, never wired** -- `quantize` never called `share_projector`, and no test ran the command. See the 2026-09-24 entry below.
- [x] **A hybrid model's second turn.** Qwen3.5's linear-attention layers keep a running state that llama.cpp can rewind only a few tokens, and a thinking model's template re-renders the last answer without its reasoning -- so turn two's prompt leaves the cache 14 tokens in, the trim was refused, the refusal ignored, and decoding on from there failed: "for M-RoPE, it is required that the position satisfies: X < Y". `trim_to` now reports where the cache really ends, clearing it when the trim is refused, and the prompt decodes from there. Such a model re-reads its whole conversation each turn; correct, and slower on a long one.
- [x] **llama.cpp's own log lines no longer reach the terminal.** They went to stderr at WARN and above and landed on top of the live spinner (`✻ Thinking…init: the tokens of sequence 0…`). They are kept instead and appended to the error of a failed load, context, decode or image evaluation. This revises the decision (Milestone J) that WARN and ERROR pass through: the reason still reaches the user, on the error it explains.
- [x] **The chat display.** Replayed through a small terminal model, the recorded bytes of a real session showed the thinking view's rows filling exactly the terminal width: in a terminal that wraps as soon as the last column is written, the erase landed a row short and every repaint left a line behind -- and the width was measured once per session, so a resize did the same. Rows now stop one column short, the width is measured at every repaint, and `display_width` counts wide characters as two cells and combining marks as none. The answer's leading blank lines (the newlines after a model's reasoning -- a three-line gap under "Thought for") and trailing ones are dropped, the first line's indentation kept, and interactive chat leaves one blank line between an answer and the next prompt.
- [x] **The Markdown renderer, planned** as backlog item 23 (shipped 2026-09-25 -- see Milestone G, "Terminal Markdown rendering"): rendered as it streams on a terminal, one open line redrawn in place, pipes and history untouched. Two calls are the user's: a hand-written renderer or md4c, and code highlighting.

**Not done, on purpose.** The duplicated question in the same report is the terminal echoing what was typed while the answer printed. Hiding that echo means switching it off during a turn, and chat has no Ctrl-C handler during a turn: the default signal kills the process and would leave the user's shell with echo off -- the risk `platform::discard_pending_input` already documents. It rides with the renderer's painter work.

**Verification.** All 1526 ctest cases pass, run serially. New: the command end to end with the converter played by a shell script (`commands/models_convert_test.cpp` -- the projector made and recorded, a refused projector leaving the model, a second run making only the projector, a third running nothing, a text-only model converted once), the non-rewindable context, the reporter's whitespace across chunk boundaries, and the thinking view's width rules. The projector adoption and the prefix fallback were each checked by reintroducing the old behaviour and watching the new tests fail. On the real machine: a three-turn chat with Qwen3.8-27B Q4_K_M, which failed on turn two before, completed every turn with no llama.cpp text on screen, and its recorded bytes replay cleanly through the terminal model in both wrap modes; `apogee models convert Qwen/Qwen3.8-27B` recognised the existing F16 conversion and made only its 884 MiB projector (8 s) into the same directory; and the Q4_K_M model with that projector, shown a red-and-blue test image, answered "red on the left and blue on the right".


### 2026-09-23 — llama.cpp moves to `b11151`: Gemma 4's unified checkpoints, and existing installs that update themselves

Asked for directly (Taylor, 2026-09-23): `apogee models convert google--gemma-4-12B/…` failed with "Model Gemma4UnifiedForConditionalGeneration is not supported". The pinned converter (`549b9d84`, May 2026) knew `Gemma4ForConditionalGeneration`; the "unified" checkpoints -- a transformer-less vision and audio front end, `attention_k_eq_v`, a 48-pixel patch -- arrived upstream later, in the converter, the `gguf` package and mtmd (projector types `gemma4uv` and `gemma4ua`) together. Nothing short of moving the pin could fix it: patching the vendored converter is ruled out, and the runtime could not have loaded its projector anyway.

**What was built**

- [x] **The pin, to release `b11151`** (`bd4f514d`, upstream `master` that day), both halves: the in-process runtime through FetchContent and the vendored converter, re-vendored whole -- entry script, `conversion/`, the `gguf` package, and now seven templates (DeepSeek-V4, Kimi-K3 and MiniMax-M1 are read by path too). The `convert` set's floors follow the new requirements file (`torch>=2.11`, `transformers>=4.57`, `numpy>=2.2`), per the Milestone Z decision that they are the file's own.
- [x] **`scripts/vendor_llama_convert.py`**, the pin bump as one step: record every file vendored now in `retired-digests.txt`, replace the tree from the new revision (templates found by scanning `conversion/` for the names it opens), regenerate.
- [x] **Existing installs update themselves.** Seeding is skip-if-present, so the bump alone would never have reached `~/.apogee`: the seeded converter would have stayed the old one, and refused Gemma from a binary whose runtime runs it. A seeded file byte-identical to a retired version is an earlier Apogee's copy, not an edit -- `seed_bundled_assets` now replaces it (or removes it when no longer shipped, tidying emptied directories) before its skip-if-present pass, and leaves every other difference alone. `check` counts stale files and names `check --fix`; `models convert` and `train promote` refuse a stale tree the same way; `uninstall` counts an earlier Apogee's unedited copy as Apogee's. The installers and `make install` run `check --fix`, so most users never see it: on Taylor's machine it updated 86 files and created 19.
- [x] **Two runtime changes that compiled silently or not at all.** `mtmd_helper_bitmap_init_from_buf` now returns a `{bitmap, video_ctx}` wrapper and takes options (a compile error, fixed; a video is refused). And `mtmd_input_text` gained `text_len`, read instead of the terminator: value-initialised to zero, it made mtmd see an empty prompt with no image markers, so every image was refused with "marker/image mismatch" -- found only by running Qwen3.8's projector again after the build was green. A tokenize failure now says which of mtmd's two failures it was, with mtmd's own words.
- [x] **Converting a base model says so.** Gemma 4 12B as pulled is the base release -- no chat template anywhere -- and chatting with it produced a ramble in ChatML. `convert` now notes a snapshot with no chat template and points at the instruction-tuned release; it converts all the same, since a base model is what fine-tuning starts from.

**Found, not changed.** With no `context_size`, the llamacpp backend opens the model's trained context -- 262,144 tokens for Gemma 4 -- and Gemma 4 12B F16 then fails its first decode with Metal out of memory on a 128 GiB machine; at 32,768 it runs. Qwen3.8-27B, trained to the same length, runs at its default, its linear-attention layers keeping no per-token cache. Whether the default should stop being "whatever the model was trained for" (a Milestone J decision) is the user's call.

**Verification.** All 1530 ctest cases pass, run serially, and the build is warning-free against the new pin. New: the refresh (a stale file updated, a retired one removed with its directory, an edit and `__pycache__` untouched), the compiled-in list (sorted, well-formed, never naming a shipped file), a stale tree refused by `converter_unavailable`, and the base-model note. On the real machine: the vendored converter's dry run maps Gemma 4 12B (667 tensors) and its projector (11), and Qwen3.8-27B unchanged (866 and 334); `make install` brought Taylor's seeded converter up to date in place; Taylor's own command, `apogee models convert google--gemma-4-12B/safetensors/c05cdbad36b8/ --type f16`, wrote the 22 GiB model and its 116 MiB image-and-audio projector in 2 min 20 s; Gemma 4 with that projector, at a 32K context, named the red and blue halves of a test image; and Qwen3.8-27B Q4_K_M with its projector answered the same test at its default context once `text_len` was set.

### 2026-09-24 — A convert that looked hung: the hash, Ctrl-C, and the staging it left

Asked for directly (Taylor): "I ran this, and it just hung in the terminal and didn't update" -- `models convert Qwen--Qwen3.8-27B --type f16`, stopped after "and its projector, so it can read images". It had not hung. After the converter finished, the store names the model's folder by the SHA-256 of the file, and 54.6 GB at the portable implementation's 324 MB/s is three silent minutes. Interrupting that left two `.incoming-*` folders holding 104 GB, which were removed on request. Then: "Finish the fix."

- [x] **A faster hash.** `sha256.cpp` uses the ARMv8 SHA-2 instructions when the build targets them (`__ARM_FEATURE_SHA2`, always on Apple Silicon), with state kept in registers across blocks, and the portable code everywhere else. `file_sha256` reads unbuffered 1 MiB chunks. The real 54.7 GB F16 hashes in 32.6 s (1677 MB/s), to the digest its record already held.
- [x] **The hash says so.** "hashing it (50.9 GiB) -- its SHA-256 names its folder in the store", with the download progress line under it, for `convert` and `quantize` alike (`commit_with_progress`).
- [x] **Ctrl-C during the hash.** The hash stops between chunks (`HashProgress` returns false, `write_record` and `commit_gguf` return `kStopped`), the staging folder is removed, and the command says "cancelled -- nothing was written".
- [x] **Staging that something did leave behind is found.** Each staging folder gets an owner marker beside it (`<staging>.owner`, the pid). `find_abandoned_staging` reports a folder whose owner is not running (`platform::process_running`), or one with no marker and nothing changed in an hour. `check` shows them as a Warn row, "leftovers", with their size. `check --fix` removes them, and never a folder a live process owns.
- [x] **`quantize` brings the projector -- this time for real.** The 2026-09-23 entry said it did; the code never called `share_projector`, and the Q4_K_M made that day had no projector. Now it does. Taylor's Q4_K_M got its projector by running `quantize` again: identical bytes, so "already here", and the projector joined the existing folder as a hard link.

**Verification.** All 1551 ctest cases pass, run serially. New: the accelerated compression against the portable one over 97 random blocks, `file_sha256`'s progress and stop, `process_running`, the owner marker's life (written, removed on commit and on `remove_weights`), abandoned-staging detection (dead owner and stale unmarked are found, live and fresh are not), `check --fix` removing only the dead one, and convert asserting the hashing line and no staging left behind. On the real machine: the 54.7 GB hash above, and `apogee models quantize Qwen--Qwen3.8-27B --type Q4_K_M` re-run in a recorded PTY showed the hashing progress and ended "(its projector, added now)" (link count 2, no staging left). `check` then reported everything clean.

### 2026-09-25 — The wait after every chat answer

Asked for directly (Taylor, with a four-question transcript on Qwen3.8-27B Q4_K_M): "When the response is completed by the model, I have to wait sometimes up to 15 seconds to be able to type my next prompt. Also, the prompt is taking forever to print to terminal."

**The wait was the title.** Auto-titling ran in line after the turn, and `sanitize_title` kept the first line of the answer. A reasoning model's answer starts with the blank lines its closed think block leaves, so the title was always "", and it was asked for again after **every** turn. Each attempt re-read the whole transcript on a fresh context sized to the model's full trained window (256K positions), and Qwen reasoned before its six words. Recorded in a PTY, the gap between the end of the answer and the next `You:` was 13.9, 23.4, 33.4, then 50.2 s, and the session never got a title.

- [x] **`sanitize_title`** takes the first line with anything on it, and strips Markdown emphasis and heading marks. Its truncation no longer keeps the lead byte of a codepoint it cut.
- [x] **`title_request`** (`chat_history.cpp`) carries only what the user asked, each message clipped and the total bounded, not the answers. It has a 32-token cap and `skip_reasoning`.
- [x] **`ChatRequest::Transient::skip_reasoning`**, honoured by the llama.cpp backend through a new profile field. `ModelProfile::skip_reasoning` is what the family's own template writes when thinking is off; for `qwen3` that is the closed `<think>` block, appended after the generation prompt. An uncharacterised family gets nothing, since a wrong guess would reach the model as text.
- [x] **A side request's context holds the request** (`side_context_size`): its prompt, its cap and some slack, floored at 4096 and never past the backend's window. The session keeps its whole window.
- [x] **`BackgroundTitle`** (`chat.cpp`) asks once per process, in a background thread, after the first completed exchange. A local provider is not safe to drive from two threads, so everything that may reach the model calls `settle()` first: the next line in the REPL (slash commands included), the next driver message in machine mode, and the exit. `settle()` waits for the title and records it. An interrupt cancels it instead.

**The slow text was not Apogee.** Measured from the same recording: the reply streams at 13-15 tokens/s on the first turns, close to what a dense 27B model at Q4 can do on the M3 Max, since every token reads all 16.8 GB of weights. It fell to about 10/s by turn four. The GPU is shared: with no model running it was 55-83% busy, with Hyper at 29% and WindowServer at 28% of GPU time (per-process `accumulatedGPUTime` from the driver's user clients). `macmon` was redrawing in a Hyper tab at the time. llama.cpp's own `llama-bench` on the same file measured 6.1-8.2 tokens/s under that load. Nothing in the token loop was changed.

**Not done, noted.** A hybrid model (qwen35) still re-reads its whole conversation each turn (see 2026-09-23): 1-4 s before the first token on this chat. A snapshot of the recurrent state at the end of each prompt would let the next turn resume from there. And the reasoning itself is most of each answer's time (26 s of 57 on turn four); a way to switch it off for a quick question is a separate decision.

**Verification.** All 1558 ctest cases pass, run serially. New: the sanitizer's blank-line, emphasis and codepoint cases; the title request carrying questions and not answers, bounded; the side context's size (small, large, capped); the closed think block appended for a Qwen and never guessed for an unprofiled model; and `apogee chat` end to end on a scripted mock. There, the title lands between turns and takes exactly its one scripted reply, and an empty title is not asked for again (were it, it would take the third answer as the conversation's title). On the real machine the same four questions, recorded in a PTY: the gap after each answer was 0.1 s on every turn, and the session was titled "Casual Greeting Exchange".

### 2026-09-25 — `models list`: colour by what a row is, and one note fewer

Asked for directly (Taylor): "I don't think the 'full weights, trainable -- ...' are really necessary. Fully configured backends should also be highlighted with the cyan coloring for the text, the non configured models should be a different color."

- [x] **The note under every SafeTensors set is gone.** Its state column already reads `safetensors`. A damaged set keeps its note, because that one names the repair.
- [x] **Colour by what a row is.** A configured backend is in Apogee's cyan, the `[apogee]` tag's colour. What no backend points at is dimmed. Anything needing attention is the warning yellow, configured or not: a missing or unreadable file, no API key, a projector configured as a model, a damaged snapshot, the old layout. Without that third colour a broken backend would read as a working one. A note is its row's colour, and the header stays plain. The flags are facts on `ModelRow` (`configured`, `attention`), set where `build_model_rows` learns them.
- [x] **Colour never moves a column.** Each line is laid out plain and coloured whole. `--no-color`, `NO_COLOR` and a pipe get exactly the old table, and `stream-json` is unchanged.

**Verification.** New: the flags across a working cloud backend, three broken ones and an unconfigured GGUF; the colour of each row and of a note; and the coloured table equal to the plain one with the escapes removed. On Taylor's store, in a PTY: seven backends in cyan, four SafeTensors sets dimmed with no line under them, and no escape bytes through a pipe or with `--no-color`.

### 2026-09-25 — Room around the banner and each question

Asked for directly (Taylor): "I want there to be an extra line of white space between the user's prompt and the thinking block", and one "after the [apogee] Qwen.... line and before the first user prompt."

- [x] **Two blank lines, on a terminal only.** One after the banner, and one after each question before whatever answers it -- the thinking block, or the answer on a model that does not reason. Both go through the status writer, as the existing blank line after each answer does, so they are ordered with the spinner and the thinking view, and the view's repaints (which erase only rows they painted) never reach them. A pipe and machine mode get neither.

**Verification.** `cli.chat_typeahead_and_crash_safety` gains a `spacing` check on a PTY with the mock backend: run against the installed binary from before the change, both of its assertions fail. On the real machine, Taylor's two questions to Qwen3.8-27B Q4_K_M, recorded in a PTY and replayed through a terminal model, give exactly the layout asked for. All 1560 ctest cases pass, run serially.

### 2026-09-28 — `helper-model-roles` (backlog item 26b): a small model for the chores, and a check on what a helper can read

**Why.** A harness that gets the most from small local models uses several: the large one answers, and smaller ones do the chores and read what the large one cannot. Until now every chore went to the chat model. Titling a chat, compacting it, judging a rerank: each took the model that was also answering, and a large tool result was read whole by the slowest reader in the process. Asked for by the user on 2026-09-25, with "a helper is used automatically" as their call.

**What was built**

- [x] **Three roles in the one resolver** (`harness/roles.h`): `Vision`, `Transcription` and `Utility` join `Chat`, `Embedding` and `Extraction`, with pointers `models.default_vision`, `default_transcription` and `default_utility`. A helper has one more rung than the others, after its pointer and before `models.default`: **the conversation's own backend** (`RoleRequest::conversation`, reported as `ResolvedFrom::Conversation`). So an unset helper runs on whatever the chat is on, including a backend chosen with `-m`, and a user who sets nothing sees nothing change. `cli.one_role_resolver` still holds: every surface asks this chain.
- [x] **One mutation path, and its admin twin.** `config set-default-vision`, `set-default-transcription` and `set-default-utility` (one `bind_set_role` call each, through `set_models_role`), `config get` of all three, and `POST /v1/admin/backends/default-vision`, `-transcription` and `-utility`, byte-identical to the CLI on the same file. `GET /v1/admin/backends` lists all six roles with the rung each resolved on. The template documents the helpers.
- [x] **The utility model's chores**, each going to the chat's own backend when no utility model is set, and each saying under `--verbose` which model did it:
  - **Titles** (`BackgroundTitle`, `title_request(session, backend)`).
  - **Compaction**, automatic and `/compact`, in `chat` and `serve`.
  - **A follow-up's search query** (`agentloop/query_rewrite`): before a retrieval turn in a conversation with an earlier user turn, the last six messages and the question become one standalone query ("which region does it deploy to?" becomes "Which region does Project Heron deploy to?"). The search changes and the chat model's question does not. It never fails a turn: an error, a blank reply or an answer instead of a query searches with the question as asked.
  - **`rerank: on`**, a new value meaning the utility model, for the flag, a collection's pin, `check`, the admin routes and `/rerank`, all through one validator (`valid_rerank`).
  - **A tool result over 8 KiB** (`agentloop/tool_summary`), summarised before a local chat model has to read it, but only by a **named** utility model: asking the chat model to summarise for itself costs the reading it saves. The chat model reads the summary under a header naming the size, the summariser, and how to see part of the rest -- a line range for `read_file`, the offset for `fetch_url`, a narrower call otherwise. A failed summary leaves the result as it was.
  - **`/capture`'s clerk** in chat, only when a utility model is named; otherwise the model already loaded, as Milestone Y decided.
- [x] **Side requests stay side requests.** The title, the query rewrite, the summary, the rerank judge and compaction all set `transient.side_request`, so a local helper runs on a context of its own and the chat's cache is never cleared. The judge and compaction were plain requests before.
- [x] **Audio as a capability** (`harness/provider.h`): `AudioCapable::accepts_audio`, discovered by `Harness::accepts_audio` like the other probes. The local backend answers yes when llama.cpp is linked, an `mmproj_path` is set, and the projector's header declares an audio encoder. The header reader now reads a projector's `clip.has_vision_encoder` and `clip.has_audio_encoder`.
- [x] **What a helper costs, and whether it can do its job.** `models status` lists six roles; an unset helper reads `(unset -- the chat's own backend)`, and a local backend is followed by `[local: N MiB of weights, M MiB of cache]`, since a helper is a second model resident beside the chat's. `check` warns when `default_vision` points at a local backend with no `mmproj_path` or a projector with no vision encoder, or `default_transcription` at one with no audio encoder or at a cloud backend, each naming why -- judged from the config and the projector's header, without loading anything. `idle_unload_seconds` was already per backend, so a resident helper can be given back on its own.

**On real weights** (Qwen3.8-27B Q4_K_M as the chat, Qwen3-VL-8B Q4_K_M with its projector as the utility, Gemma 4 12B-it with its projector for transcription; greedy; `--verbose` lines):

- **`models status`**: `chat: q27 [local: 16032 MiB of weights, 1088 MiB of cache]`, `utility: q8 [local: 4795 MiB of weights, 2448 MiB of cache]`, and the rest.
- **`check`**, with `default_vision` at the 27B (no projector configured) and `default_transcription` at the 8B: `q27 -- it has no mmproj_path, so it cannot read an image` and `q8 -- its projector has no audio encoder`, both warnings. Pointed at the 8B and Gemma 4 12B, whose projector declares both encoders, both passed. A pointer at a backend that does not exist is refused by `config` before it is written.
- **A three-question chat over a notes collection, with a `/compact`**: `titled by q8: Project Heron Overview`, `search query by q8: Which region does Project Heron deploy to?`, `history compacted by q8`, and all three answers right. Searched as asked, that follow-up ranked Project Heron's note last of three.
- **The 27B's cache**: without retrieval, turns two and three read 57 and 98 tokens from the 27B's cache while the 8B titled the chat in the background -- the same as with no utility model set.
- **A tool result**: asked for the one error in a 35 KB log, the 27B read the file; `read_file's 35 KB result summarised by q8`; the 27B's next step read **413 new tokens, 3,988 from its cache**, where the file itself is about ten thousand tokens. It then ran a narrower `grep_files` to confirm, and named the line, invoice and cause correctly.

**Found on the way, and fixed.** The first real run titled a chat "Project Heron is a system for managing and scaling distribut…". Shown "In one sentence, what is Project Heron?" under "give this conversation a title", the 8B answered the question -- inventing the answer -- where the 27B had titled it. The prompt now says the questions are shown for their topic and are not to be answered, and the 8B gives "Project Heron Overview" and "What is a Ledger"; the 27B's titles are unchanged.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| Where an unset helper runs | The conversation's own backend, a rung of its own after the pointer *(default taken)* | "The chat backend" has to mean the one this chat is on; `models.default` would send a `-m` chat's chores to another model. |
| Query rewriting | Only with an earlier user turn and retrieval on *(default taken)*; on the chat's backend when no utility model is set | A first question already stands alone. A follow-up searched as asked finds the wrong thing, whichever model restates it. |
| Tool-result summaries | Over 8 KiB, and only by a named utility model *(default taken)* | The chat model summarising for itself would read the whole result anyway. |
| The rerank judge's default | A new value, `on`: the utility model, else the chat's backend | A backend name still works; `on` says "whatever my helper is" without naming it twice. |
| The capture clerk | The utility model only when one is named (2026-09-25) | Milestone Y's "the loaded model is the clerk, no second load" stands unless a helper was set on purpose. |
| What `check` judges from | The config and the projector's header | A doctor that loads a model to find out would cost what a helper is meant to save. |
| Audio on a cloud backend | Not a transcription backend here | Nothing in Apogee sends a cloud backend audio yet; `check` says so rather than pass a pointer that fails later. |

**Guardrails, each mutation-tested (67 mutants), run in separate git worktrees against the whole unit suite.** 65 were caught there, ten of them only after a test was added for what they exposed. One more is caught by `cli.knowledge_lifecycle`, and one cannot be reached.
- **The resolver:** the conversation's rung for every role, gone, or above the pointer; transcription not a helper; a helper's pointer unread; a role's key misnamed.
- **The pointers:** not parsed, not compared, not an editable field; `config get` of one reading another; a verb or an admin route setting the wrong pointer, on its own and through the mux.
- **The chores:**
  - the title asked of the chat's backend, or its request ignoring the backend it was given;
  - `/compact` and automatic compaction on the chat's backend, and compaction not a side request;
  - the query rewrite on the chat's backend or not used at all, tried on a first question, shown the question twice, shown system prompts, the whole history or unclipped messages, not a side request, reasoning not skipped, no budget, its label or quotes kept, a blank or overlong reply used, a failure failing the turn, and never marked as a rewrite;
  - the summary always taken, taken far past the threshold, never used, never said, shown the whole output, not a side request, blank accepted, missing its way back, a failure failing the call, and never asked by chat;
  - `named_utility` taking any rung, and `helper_backend` ignoring the conversation;
  - `rerank: on` refused, resolving the chat role, ignoring the conversation, or silent when nothing can judge; the judge not a side request.
- **What is said:**
  - `check` not checking the helpers, never judging a medium, passing a vision backend with no projector, ignoring the encoder flag, reading audio from the vision flag, or passing cloud audio;
  - `models status` naming `models.default` for an unset helper, dropping the utility row, or its cost;
  - the header's flags unread, swapped, or kept after a failed read;
  - an undeclared audio capability counted as yes.
- **Caught outside the unit suite:** `/capture`'s clerk never the utility model. `knowledge_e2e.sh` now points the utility model at a mock that only writes prose, and checks that the capture fails. The mutant passes the unit suite and fails that script; this was checked by hand.
- **Cannot be reached:** the admin listing's "conversation" rung. The listing resolves without a conversation, so an unset helper reports `models.default`, as `http-api.md` says.

**Not verified, and found on the way.**
- **No audio is transcribed yet.** The role, its pointer, `accepts_audio` and `check` are here; [26e](#milestone-h--apogee-chat), shipped 2026-09-30, is what sends audio, and `Harness::accepts_audio` had no caller until then. The header probe was checked against real projector files: Gemma 4 12B-it's declares both encoders, and Qwen3-VL-8B's and Qwen3.8-27B's declare vision only.
- **A retrieval turn re-reads a local model's whole prompt**, helper or not. The retrieved block sits at the conversation's start, as 25c's notes say. Every turn of the notes chat above read from 0 on the 27B, the same with no utility model set. That is its own change.
- **Qwen3.8-27B answered one turn with nothing.** In a plain three-question chat, the third answer was saved empty, with and without a utility model set. It is older than this item and left for its own change.
- **`--verbose` prints a tool call as `[tool] [tool] read_file`.** The tool's status line carries its own tag and the terminal adds another. It is cosmetic, and older than this item.

### 2026-10-03 — `gguf-header-cache` (maintenance item M2): the header reader stops seeking, and no cache is needed

**Why.** `apogee models list` took 14 seconds on this machine's store of 31 models, and `check`, which reads most of them twice, about 30. M2 was specced as a cache of what the headers say: a file under `cache/`, keyed by each model file's path, size and modification time, so that a second listing would read no headers at all. Shipping M1 the same day, a sample of `models list` showed that the time was not in the number of headers but in how each was read.
- Nearly all of it was inside `inspect_gguf`, stepping over the tokenizer's vocabulary, and two-thirds of it was system time.
- Each of a vocabulary's strings (150,000 or more in a current model) was skipped with its own `seekg`. A seek throws the stream's buffer away, so the next read went back to the operating system: some 300,000 system calls per file.

**What was built**

- [x] **Small skips read through the buffer** (`models/gguf_inspect.cpp`). A skip of up to 64 KiB (`kBufferedSkip`) is `ignore`d; only a larger one, such as an array of scores hundreds of kilobytes long, seeks. A skip that comes up short (a file cut after its size was taken) is a parse error with its reason, like every other short read.
- [x] **`inspect_gguf(std::istream&, size)`**, the core the path overload now calls once the file is open. A test can hand it a stream that counts what a read costs. This reader's regression was in cost, not correctness, and only a count can hold that.
- [x] **The test builder** (`tests/support/gguf_builder.h`) makes vocabularies and score arrays.

**Measured** (this machine's store, read only, comparing the build from before the change with this one, output compared byte for byte):

| Command | Before | After | Output |
|---|---|---|---|
| `models list` | 13.95 s | 0.34 s | identical |
| `models status` | 0.86 s | 0.03 s | identical |
| `check` | 27.6 s | 0.68 s | identical |

Every other header read goes through the same function, so each is faster the same way with the same answer: the llamacpp backend's profile and window, `convert`, `quantize`, acquisition's verify and training's promotion.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| No cache | The reader fix only — **the user's call**, on the numbers above | A cache would save about another third of a second. It would cost a file under `cache/`, a window in which a listing could be stale, and guardrails of its own (the test keeping it off the loading paths). With the read this cheap, nothing is stored and no listing can be out of date. The document's four open calls were all about the cache, and lapsed with it. |
| The threshold | 64 KiB | Every vocabulary string and every small value stays in the buffer. An array of scores, hundreds of kilobytes, is one seek rather than a read of bytes nothing looks at. |

**Guardrails, each mutation-tested (5 mutants, all caught)**, against the whole unit suite. The mutants: every skip a seek, no skip a seek, a short skip unchecked, the size not passed to the stream overload, and the file size not recorded.
- `gguf_inspect_test` reads through a stream that counts its seeks:
  - a 50,000-string vocabulary is stepped over with no seeks, and its scores with one;
  - the architecture after both is read in step;
  - the stream and the file overloads read alike;
  - a stream shorter than it claims fails inside a skip, the last field included.

**Not verified, and found on the way.**
- **Every measurement ran with the files in the operating system's cache**, read moments before. A first read after a restart also pays for the disk, for the same header bytes as before.
- **Windows** uses the same standard library calls, but was not measured there.
- **M1's busy line now shows only briefly.** A 31-model `models list` finishes in a third of a second, so its line appears for a frame or two after the 150 ms gate.

### 2026-10-03 — `pull-register-chain` (maintenance item M3): one command from a pull to a runnable backend, and a quantize that stops talking over itself

**Why.** Getting a full-weight model from Hugging Face to a chattable backend took four commands, each typed after watching the last finish (the user's transcript, 2026-10-03): `models pull … --safetensors`, `models convert`, `models quantize --type Q4_K_M`, then `config add-backend`. Every stage was already a shipped core. The same transcript showed `models quantize` printing llama.cpp's own metadata dump straight onto the terminal, where every other `models` verb reports in its own words: 14 lines for a one-tensor model, hundreds for a real one.

**What was built**

- [x] **`--register` and `--register-with <levels>`**, on `models pull <ref> --safetensors` and on `models convert <model>`.
  - The chain pulls (when it starts from a pull), converts to F16 with its projector, quantizes to each listed level, and registers a backend for every artifact: `<model>-F16` and `<model>-<level>`, named since 2026-10-03 as the user names them (below).
  - Levels are comma-separated and spelled any way the quantize table accepts; `q4_k_m` is `Q4_K_M`, and F16 is always made.
  - `--register` without `--safetensors` is refused, naming why: a GGUF pull is runnable as it lands.
- [x] **The verbs became functions the chain calls** (`commands/models_pull.cpp`): `pull_snapshot`, `convert_model` and `quantize_model`.
  - Standalone, each prints exactly what it printed before.
  - Chained, each hands its warnings (a base model, a projector that could not be made, an unrunnable architecture) to the chain, which says them once in its summary. Each verb's own "add a backend" and "make it smaller" hints give way to the chain's.
  - Every stage keeps its guarantees because it is the same code: the staging directory, the hash-named store home, the projector carried to every quantization, Ctrl-C cleaning up.
- [x] **The orchestration** (`commands/model_chain.h/.cpp`): stages in order under `[i/n]` lines.
  - A stage that fails has said why; the chain then says where it stopped and the one command that resumes it, and passes the failure on with the stage's own exit code (a cancel stays a cancel).
  - Once the pull is done, the chain says at once that from here on `apogee models convert <model>/safetensors/<id> --register-with …` resumes it. That command runs offline, and a run killed outright has already shown it.
- [x] **Resuming makes nothing twice.** `convert` already found an existing conversion by its record; `quantize` now finds an existing quantization the same way (the source it was made from, and the level). Standalone `models quantize` benefits too: a level already made is reported, not made again.
- [x] **Registration is the hand-typed edit.** Each backend goes through `harness::append_backend` in the one config editor, the edit `config add-backend <name> --type llamacpp --model-path … [--mmproj-path …]` makes. A name an earlier run of the same chain registered for the same files is left as it is.
  - Before the first stage, the chain refuses what would refuse at its last: no config to register into, a name another model's backend holds, or a name differing only in case.
- [x] **llama.cpp's log stays off the terminal** (`models/quantize.cpp`). For the run, its log callback is Apogee's `QuantizeLog`:
  - the metadata dump is dropped;
  - the per-tensor lines become a tensor count on M1's busy line;
  - warnings and errors are kept and attached to a failure as `llama.cpp said: …`;
  - the callback in place before (the backend's own, when a model is loaded) is put back afterwards.
  - On a small hand-built model, the build from before printed 14 lines of llama.cpp's own. This one prints none, and on a failure says llama.cpp's reason in one line beneath its own.
- [x] **The new flags complete after another flag** (`commands/complete_protocol.cpp`; the user's report, 2026-10-03). `models pull <repo> --safetensors --<TAB>` showed `--safetensors`' own description, and `--register` never appeared.
  - The cause predates this item: since flag completion shipped, the protocol took every boolean flag for one that takes a value, so the word after any of them (`chat --raw`, `models delete --yes`) was read as that flag's value. It read CLI11's type size, which is one for every option, flags included; the parser reads the items an option expects, which is none for a flag. Completion now asks the parser's question.
  - A fixed set the parser splits at commas completes its last word, as a collection list already did: `--register-with Q4_K_M,<TAB>` offers the other levels, not the one already listed.
- [x] **Backend names the user's way, and `--base-name`** (`commands/models_pull.cpp`; asked for after the user's first live chain, 2026-10-03). The chain registered `gemma-4-E2B-F16` and `gemma-4-E2B-Q4_K_M` beside the user's own `Gemma4-E4B-F16` and `Gemma4-E4B-Q4KM`.
  - A backend's level is written without its underscores: `-F16`, `-Q4KM`, `-Q5KM`, `-Q6K`, `-Q80`. The file keeps the table's spelling (`gemma-4-E2B-Q4_K_M.gguf`), and so does the record.
  - `--base-name <name>`, after the levels on `pull` and on `convert`, is what every name begins with: `--base-name Gemma4-E2B` registers `Gemma4-E2B-F16` and `Gemma4-E2B-Q4KM`. Without it, the model's own name, as before.
  - The resume command carries it, so a resumed chain registers the names the first run would have.
  - A name the config cannot hold is refused before anything runs, by the very edit that will add it, run on the config's text and kept nowhere -- one rule, not a copy. `--base-name` without `--register`, or empty, is refused the same way.

**Not run, by the user's call.** The guardrail's live check, one full-weight pull chained to a registered quant and chatted with, was not run: the user runs it (2026-10-03). Nothing in this item's tests downloads, converts or quantizes a real model.
- The converter is the existing test's shell script.
- The chain's quantizer is a stand-in that writes small GGUFs.
- llama.cpp's real quantizer runs, on a llama build, only on a 2 KiB model built in the test (`quantizable_gguf`).
- The pull stage itself, which needs the network, is exercised only through the orchestrator's tests.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| What registers | Every artifact: `<model>-F16` and `<model>-<level>` *(default taken)* | The user picks at chat time and deletes what they do not want. |
| Backend names | `<base>-F16`, `<base>-Q4KM`: the level without underscores, the base `--base-name` or the model's name — **the user's call**, 2026-10-03 | The names the user gives backends by hand; the files keep the table's spelling. |
| The F16 | Always made and kept *(default taken)* | It is the quantization source and the training input; retention is the user's, through `models delete`. |
| Flag composition | `--register` without `--safetensors` refused *(default taken)* | The chain is for full-weight pulls; a GGUF pull is runnable as it lands. |
| Discoverability | A plain `pull --safetensors` now suggests `--register-with Q4_K_M` *(default taken)* | The next pull can be one command. |
| The resume command | `models convert <model>/safetensors/<id> --register…`, said as soon as the pull is done | Offline, and it skips what is done; `convert` gained the same flags for this. |
| No real pull | Built and verified without downloading or converting a real model — **the user's call** | The live check is the user's; the tests need none of it. |

**Guardrails, each mutation-tested (24 mutants, all caught on the first pass)**, against the whole unit suite; the quantize log's five on a llama build.
- **The orchestration:** a failure unsaid or swallowed; the offline resume unsaid; a warning said twice.
- **The chain:** registration not idempotent; another model's name, a name differing in case, or a missing config not refused up front; the names not checked first; the projector or the quantizations not registered; an existing quantization not recognised; levels kept as typed, or F16 kept as a level; `--type` allowed beside `--register`; a base model unmarked; a chained verb printing its own hints or its warnings inline; the resume command without its levels.
- **The quantize log** (on a llama build): the log not routed or not put back, the reason dropped, the tensor count unsaid, the warnings not kept.
- Not mutation-tested: the `--register needs --safetensors` refusal. Its mutant turns the test's command into a real pull, which reaches the network.
- **The names** (7 more mutants, all caught): the level keeping its underscores; the base name ignored; the resume command without it; a name the config cannot hold not refused up front; `--base-name` taken with nothing to name, or empty; `convert` passing none. `pull`'s passing it on is not mutation-tested, for the reason above: its test would be a real pull.
- **The completion fix** (4 more mutants, all caught): a flag read as taking a value, a set split at commas not completed as a list, the parser's delimiter not read, a level already listed offered again.

**Tests.**
- `model_chain_test`: the orchestration as a table over stand-in stages.
- `models_convert_test`, all in process:
  - the chain with a stand-in quantizer, each config byte-identical to `config add-backend` typed by hand;
  - a stop at a quantize, resumed by the printed command, making nothing twice;
  - a stop at the conversion, resumed by running the printed command itself;
  - the up-front refusals;
  - a base model's note said once;
  - on a llama build, the whole chain through the command line with the real quantizer.
- `quantize_test`, on a llama build: a real quantization writes nothing of llama.cpp's to the terminal (captured at the file descriptor) and counts its one tensor; a refusal carries llama.cpp's reason; the backend still hears llama.cpp afterwards.
- `lifecycle_test`, for the completion fix: the reported line and its siblings (`--safetensors --`, `chat --raw --`, `uninstall -y --`, a positional after a flag); every spelling in the real tree held to the parser's own count of what it takes; `--register-with`'s comma list, and a single-word set that is not one.

### 2026-10-03 — `models-list-lineage` (maintenance item M4): every model says where it came from, and a snapshot whose job is done leaves the listing

**Why.** The user's `models list` (2026-10-03) opened with eleven `(not configured)` SafeTensors rows, and most of them had already done their job: each had been converted into the GGUFs registered below it. A consumed input shown as an unconfigured model reads as work to do, and it hides the one snapshot that really is waiting. The user's rule, taken as written: **a snapshot used to make a GGUF no longer shows as an unregistered model; one never converted still does — and what was built from a snapshot says so.**

**What was found first.** The item was specced on the premise that lineage was recorded nowhere, so that `convert` and `quantize` would first have to start writing it. They already did. Since `models convert` first shipped (2026-09-23), the record beside every conversion has said `source: convert` with `ref` naming its snapshot as `<model>/safetensors/<id>`, and every quantization `source: quantize` with `ref` naming its GGUF. Every one of the 22 GGUF records on the user's store carries it. So the "record" half became a **reading** of those two fields, with no new field written: a `derived_from` beside `ref` would have been a second copy of one fact.

**What was built**

- [x] **The lineage core** (`models/lineage.h/.cpp`, new):
  - `origin_from_record` reads a record: converted, quantized, pulled from Hugging Face or Ollama, a fine-tune, or unknown.
  - `Lineage` is built from what one sweep of the store holds:
    - `chain()` walks back from a model, one link per step: quantized from its F16, converted from its snapshot, pulled from its upstream.
    - `consumed()` names the GGUFs whose chain reaches a snapshot.
  - Recorded lineage answers first, and inference only where no record does. Every answer built on inference carries `inferred`.
  - **Inference is narrow.** A GGUF with no record, under a model holding exactly one snapshot, is taken to be its conversion. So is a quantization whose F16 has been deleted, record and all; the F16 is the big file, and once quantized it is the one most often removed. With two snapshots nothing is guessed. Outside the store nothing is inferred at all.
  - A fine-tune's record names its run, so it never consumes its base snapshot: the base model is no more runnable because a fine-tune of it exists.
- [x] **One handle parser in the layout's file** (`models/store.h`): `weights_handle` and `parse_weights_handle` read `<model>/<format>/<id>` without asking the disk. A record's `ref` is read through them, never with path arithmetic in the display.
- [x] **`models list` folds consumed snapshots** (`commands/models.cpp`).
  - A snapshot the lineage says is consumed is not shown. It is still shown when it needs attention (a damaged one) or a backend points at it.
  - One dim line under the table counts the fold: `12 snapshots consumed by conversions are folded -- --all lists them`.
  - `--all` lists everything; nothing about the store changes, so `check`, `train` and `models delete` see what they saw before.
  - Deleting a snapshot's GGUFs brings it back, because the listing follows the store.
  - A GGUF whose chain passes through a conversion reads `converted` in SOURCE, backend or not; everything else reads what it read before.
  - `--output-format stream-json` keeps every row: the fold is the table's, and lineage in machine output is 28h's.
- [x] **The sweep reads nothing twice** ([M2](#milestone-n--model-operations)'s cost discipline).
  - Each file's record is read inside its own counted step of the busy line, as before, and the lineage is applied in one pass over the finished rows.
  - The first version read every record up front. That would have left the busy line on its uncounted label while the records loaded. It would also have hung `cli.busy_line`, which holds the sweep on a named-pipe sidecar until it sees a counted frame. It was caught reading that test, before it ran.
- [x] **`models info` takes stored weights as well as a backend**: `<model>/<format>/<id>`, or a bare id.
  - Completion offers both through a new name kind, `BACKEND_OR_WEIGHTS`.
  - A local model's info prints its chain:
    ```
    lineage:      quantized to Q4_K_M from google--gemma-4-E4B/gguf/c52b3d2c5304 (recorded)
                  converted from google--gemma-4-E4B/safetensors/af4523bb6580 (recorded)
                  pulled from google/gemma-4-E4B (Hugging Face)
    ```
  - A broken link says what is gone: `-- source snapshot no longer on disk`, or `-- no longer on disk` for a quantization's F16.
  - A snapshot's info says what was made from it, conversion first, and whether the listing folds it.
  - A whole model name is refused with the handles to pick from.

**On the user's store** (read-only, through a scratch `APOGEE_HOME` holding a copy of the config, with the models directory linked in).
- `models list` went from 12 `(not configured)` snapshot rows to none, with the one tail line, and its 22 local backends now read `converted`. Every snapshot there had been converted, `google/gemma-4-E2B` among them, through M3's chain earlier the same day.
- `--all` listed exactly the old table, line for line, apart from the SOURCE column.
- Everything there is recorded, so nothing was inferred: the inferred path runs only on the tests' fixtures.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| Where lineage lives | A reading of the record's `source` and `ref`; no `derived_from` field | Convert and quantize have written the parent there since the store's first day; a second field would be a second copy. |
| When to infer | Only where it names exactly one snapshot: a record-less GGUF, or a quantization whose F16 is gone | With two, either could be the parent, and a guess would fold the wrong one. |
| A fine-tune | Does not consume its base | The base model stays unrunnable until it is converted. |
| Discoverability of the fold | `models list --all`, plus one tail line counting what is folded *(default taken)* | The fold is visible, never silent. |
| SOURCE | `converted` for a GGUF whose chain reaches a conversion, recorded or inferred; unchanged otherwise *(default taken)* | The full chain lives in `models info`, not in new columns. |
| A snapshot a backend points at | Never folded *(default taken)* | Possible once MLX (27a) runs one directly; a backend's own model is never hidden. |
| Machine output | `stream-json` keeps every row | A machine reader is not misled by `(not configured)`, and lineage there is 28h's. |
| `models info` | Takes `<model>/<format>/<id>` or an id as well as a backend | The acceptance needs it: a fresh conversion is not a backend yet, and a folded snapshot must stay reachable. |
| M3's chain | Stamps the same records through the same functions, nothing extra *(default taken)* | Proven through the command line: chain, then `info` and `list`. |

**Guardrails, each mutation-tested (30 mutants, all caught on the first pass)**, against the whole unit suite. The three on `consumed()` ran again on its final form, after the conversion was ordered first.
- **The reading:** a conversion, a pull or a fine-tune read as unknown.
- **Inference:** none at all; not marked as inference; a guess between two snapshots; none across a deleted F16; not carried down a chain; an inference beating a record; the conversion not listed first.
- **The chain:** a gone F16 or a gone snapshot not marked; a path outside the store not checked; the upstream link dropped; the walk unbounded.
- **The handle parser:** any id accepted.
- **The listing:** `converted` never said; a snapshot needing attention or a backend's own folded; nothing folded; `--all` ignored; the fold unsaid.
- **Info:** inferred said as recorded; a gone snapshot or F16 unsaid; no stored handle taken; no lineage on a backend; the command line passing no store; a record not kept for the lineage.
- **Completion:** stored handles not offered for `models info`.

**Tests.**
- `lineage_test`, over a store built in a temp directory:
  - the record table;
  - `consumed()` exhaustively: recorded; never converted; inferred; two snapshots with one converted (only the recorded one); two snapshots and no record (nothing guessed); a record beating an inference; a quantization reaching it through its F16; the F16 deleted, with one snapshot or two; a pulled GGUF, a fine-tune and another model's GGUF consuming nothing;
  - the conversion ordered before its quantizations;
  - a snapshot that stops being consumed once its GGUF is gone;
  - the full chain to the upstream; the broken chains; the inferred chains; a parent outside the store, present and gone;
  - a record naming itself; a store split across `paths.hf_dir`.
- `store_test`: the handle parser against what is not a handle.
- `models_test`:
  - the listing's fold and its tail line, and `--all` as a superset of every line;
  - `converted` on backends and stored rows, and a pulled GGUF unchanged;
  - deleting the GGUFs brings the snapshot back; deleting only the F16 keeps it folded, said as inferred; deleting the snapshot names the break;
  - a damaged snapshot and a backend's own never folded;
  - the sweep's reads counted exactly as before;
  - `info` goldens for a backend, a stored GGUF, a consumed snapshot, a waiting one, a bare id, an inferred conversion, an unknown origin and a whole model name.
- `models_convert_test`: M3's chain run, then `models info` and `models list` through the command line on what it made.
- `complete_sources_test`: `models info` completes backends and stored handles, and no bare model.
- No inference was run, by the user's call: nothing in this item loads a model.

### 2026-10-03 — `shell-completion` (maintenance item M7): a backend's name is enough to delete its model or register a stored one

**Why.** The user's delete transcript (2026-10-03) showed the gap twice in three commands.
- `apogee models delete gemma-4-E2B-F16` — the backend's name, as `models list` prints it — was refused: "no model 'gemma-4-E2B-F16'".
- The store path that works, `google--gemma-4-E2B/gguf/513ee1b91245`, had to be hunted by hand. The accepted command's own warning then proved the mapping existed the other way: "backend 'gemma-4-E2B-F16' points into this".
- Registration is the same friction inverted: `config add-backend` hand-types a name, `--type llamacpp` and a `--model-path` the store already knows.

**What was found first.** The item was written as if Apogee had no shell completion. It has had it since v0.1.2:
- the hidden `__complete` verb, reading candidates from the live config and store;
- the zsh, bash, fish and PowerShell scripts that `make install` and `install.sh` put in place.

So, **by the user's call**, the item builds on what is installed: no `apogee completion <shell>` command, and no second way to deliver the scripts. What was missing was candidates — `models delete <TAB>` offered store models but no backends, and `config add-backend <TAB>` nothing — plus the two behaviours behind them.

**What was built**

- [x] **`models delete` takes a backend's name** (`commands/models_pull.cpp`, `plan_delete(roots, config, name)`).
  - A name the store knows means what it always did.
  - Failing that, a backend whose `model_path` is a stored GGUF means that GGUF's weights, planned exactly as its handle would be. The will-remove text, the warning that the backend stops working, and `--yes` are the handle's, byte for byte.
  - A store name that a backend also has keeps its store meaning, and the collision is said before the plan: `'org--repo' is also a backend -- its model, other/gguf/…, is not what this removes`. Nothing is said when the backend's model is among what goes, because the existing warning names it.
  - A backend outside the store, or with no model file (a cloud one), is refused with the reason.
- [x] **`config add-backend <name>` fills itself from the store** (`commands/config_cmd.cpp`, `fill_from_store`).
  - A name that is a stored GGUF's file stem gives the type its format runs as, its `model_path` and its projector.
  - It says them as the arguments they stand for (`filled from the store: <handle>` then `--type llamacpp --model-path … --mmproj-path …`), then makes the same `append_backend` edit a hand-typed command makes.
  - A flag given always wins: a `--model-path` given, or another `--type`, fills nothing, and a `--mmproj-path` given is kept.
  - Two stored GGUFs of one name are refused, both listed.
  - `--type` is no longer required by the parser. When nothing fills it, the callback throws the parser's own `RequiredError`, so a name the store does not know still reads `--type is required` and exits 106.
- [x] **The format → type map** (`models/store.h`, `backend_type_for_format`): `gguf` → `llamacpp`, one row per format beside the formats themselves, so MLX's `mlx/` (27b) is one more row.
  - `stored_gguf_at` (the stored GGUF a backend points at) and `stored_ggufs_named` (the stored GGUFs of a name) are the lookups delete, add-backend and completion share.
- [x] **Completion, through the resolver that exists** (`commands/complete_sources.cpp`), with two name kinds:
  - `MODEL_OR_BACKEND`, for `models delete`: store models, handles, and the backends whose model is stored, never a cloud one.
  - `NEW_BACKEND`, for `config add-backend`'s name: free text that offers the file names of the stored GGUFs no backend points at and no backend is named.
  - Directories and the config only, never a header.
  - The free riders were already wired: `models info` completes backends and stored weights (M4), and `chat -m` completes backends.

**On the user's store** (read-only, the M4 scratch home): `models delete gemma-4<TAB>` offers `gemma-4-E2B-F16` and `gemma-4-E2B-Q4_K_M`. `config add-backend <TAB>` offers nothing but its hint: every stored GGUF there already has a backend.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| Delivery | The installed scripts and the `__complete` that exists; no `apogee completion` — **the user's call** | Completion shipped in v0.1.2; a second delivery path would be a second copy of the scripts. |
| Shells | zsh and bash checked, fish when present *(default taken)* | Already shipped; `cli.shell_completion` drives the real shells. |
| A new backend's candidate name | The stored GGUF's file stem *(default taken)* | The name `models list` notes under the row. |
| A name both the store and a backend have | The store's meaning, the backend said | Today's meaning stands; nothing is removed with fewer words. |
| No match and no `--type` | The parser's own `RequiredError` | A non-store name behaves exactly as before. |
| Free riders | Already wired; nothing new *(default taken)* | `models info` (M4) and `chat -m` complete backends. |

**Guardrails, each mutation-tested (17 mutants, all caught on the first pass)**, against the whole unit suite.
- **Delete:** a backend's name not tried, or tried before the store's; the collision unplanned, unsaid, or said for what goes; an outside backend called modelless; the command reading no config.
- **Completion:** delete offering no backends; a registered GGUF, or a name already taken, offered as new.
- **Add-backend:** no fill; a given model path, another type or a given projector filled over; two GGUFs of one name taken as one; the refusal in other words than the parser's; the `gguf` row naming another type.

**Tests.**
- `config_cmd_test` (new, over `support/cli_home`, a throwaway install the real command tree runs against in process):
  - the fill written byte for byte as the hand-typed command writes it in a twin install, comments kept;
  - each flag winning;
  - the unknown name's exact refusal, with the config untouched;
  - the ambiguous name.
- `models_pull_test`: delete by a backend's name planned as its handle; the store's meaning kept and the collision said through the command line; refusals saying why; the confirmation and `--yes` byte-identical to the handle's through the command line.
- `complete_sources_test`: both kinds over a store of files that are not GGUFs at all.
- `lifecycle_test`: add-backend's name hint.
- `cli.shell_completion`: the real zsh, autoloaded and sourced, and the real bash complete a backend for `models delete`, a handle prefix, and an unregistered GGUF's name for `config add-backend`. The three fail against the installed binary from before this item.

## Milestone O — Local multimodal

**Goal.** Make `VisionCapable` tell the truth on the local backend: wire llama.cpp's `mtmd`, add `mmproj_path`, and close the cross-surface guard gap that let one surface accept a picture the other refused.

### 2026-09-07 — `multimodal-vision`: a real answer, and the guard that was written once

**Verified live.** A 128×128 red circle through `apogee complete -m vision --image circle.png` against SmolVLM-500M answered `Circle.` — a local model, in-process, reading an actual image.

**What was built**

- [x] **The seam extended** (`backends/llama_runtime.h`): `LlamaContext::decode_multimodal`, `LlamaModel::supports_vision`/`image_marker`, and an `mmproj_path` parameter on `load`. Every addition has a default that refuses, so a runtime without vision says so rather than ignoring the pictures it was handed.
- [x] **`backends/llama_real.cpp`** — mtmd wired: projector loading, image decoding, `mtmd_tokenize`, and `mtmd_helper_eval_chunks`, with every raw handle in a `unique_ptr` at the boundary per the Code Style rule for C APIs.
- [x] **`third_party/CMakeLists.txt`** — mtmd enabled *narrowly*. Upstream ships it under `tools/`, whose only documented switch also builds llama-bench, perplexity, quantize and more; adding the one subdirectory gets the library without the rest.
- [x] **`mmproj_path`** as a backend field, documented in the starter config.
- [x] **`commands/helpers.cpp` → `attachment_refusal`** — the shared guard, plus the missing call in `chat.cpp`.
- [x] **The sampling loop extracted** into `LlamaCppProvider::generate`, shared by the text and image paths.
- [x] **`GgufInfo::is_projector()`** — a projector is no longer misreported as a combined blob.
- [x] **12 new tests** (699 total), green in **both** the default build and the llama-enabled one.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| Enabling mtmd | Add the one subdirectory, not `LLAMA_BUILD_TOOLS=ON` | The documented switch builds half a dozen binaries Apogee has no use for. `mtmd` is self-contained (`PUBLIC ggml llama`), so the narrow version works and stays honest about what it costs. |
| `accepts_images()` | Answers from **configured state** | It is asked before a turn starts; loading 16 GB of weights to answer a yes/no question would make every `--image` check pay for a model load. |
| The image path | A fresh context, no KV reuse | An image occupies embedding positions no token comparison can match. Pretending otherwise would corrupt the cache rather than save work, so the cost is paid only on turns that carry a picture. |
| A projector that fails to load | An **error**, not a downgrade to text | The user configured vision. Quietly answering without looking at their picture is worse than saying why. |

**Three bugs that only running it found.**

*mtmd logs to stdout.* The first successful image turn printed `encoding image slice...` and `image decoded (batch 1/1) in 55 ms` **interleaved with the answer**. `llama_log_set` does not reach mtmd — it has its own two log channels. On a terminal that is noise; in machine mode it is non-JSON in the middle of the event stream, which breaks a driver's parser at the worst possible moment. Both channels now route WARN-and-above to stderr and drop the rest, and machine mode was re-checked: zero non-JSON lines with an image attached.

*A Homebrew llama.cpp hijacked the build.* `apogee_core` linked `llama` but not `mtmd`, so `#include <mtmd.h>` fell through to `/opt/homebrew/include` — a **different** llama.cpp — and the build failed with a wall of `ggml` redefinitions naming neither the real cause nor the file. Linking the target carries its include directory and fixes it.

*A projector was reported as a combined blob.* A real mmproj has 198 tensors and every one is a vision tensor, so `tensors > text_tensors` was true and `models list` announced "combined text+vision blob" about a file that is exactly what `mmproj_path` wants. `is_projector()` now separates the two, and `check` fails a projector configured as a `model_path` with the exact fix.

**The parity gap, closed.** `complete.cpp` guarded attachments from the day `--image` landed; `chat.cpp` never got a copy, so `apogee chat --image` loaded the file, built the content part, and handed it to a provider that had just answered that it cannot read images — silently. Both now call one helper, and the test is written over a **list** of surfaces so a sixth that grows an `--image` flag fails by existing. That is the only shape of the assertion that keeps working after everyone has forgotten it: nobody writes the per-surface test for the surface they forgot.

**Guardrails, each mutation-tested (six mutations, all caught).** Removing the guard from `chat`, reaching for a private capability probe, dropping the refusal's guidance, ignoring the build flag in `accepts_images`, dropping `mmproj_path` on the way to the loader, and reporting a projector as a combined blob.

One of those came back green at first and the *test* was at fault, in the same shape as two earlier ones this branch: the message assertion was guarded behind a refusal that, in a build without llama.cpp, never arrives — so it asserted nothing. The message is now exposed as `image_refusal_message()` and checked directly.

**The question multimodal-vision owed model-acquisition, answered.** **mtmd requires a separate projector file.** Passing a text model as its own `mmproj_path` fails at `mtmd_init_from_file` ("Failed to load CLIP model"). So a combined text+vision blob cannot be used for vision as-is — but the text model loads fine untouched, so nothing calls for *stripping* the vision tensors to make it load. What a combined blob would need is the projector **extracted**, not the vision tensors discarded. That question was closed the same day: the registry showed Ollama already ships the projector as its own layer, so no transform was ever needed — see Milestone N's completion entry.

### 2026-09-07 — `model-acquisition` completed: quantize, and a plan the manifests refuted

The residue this item was deliberately reduced to on 2026-09-07 — three pieces that were not buildable in the same pass — is now closed. Two of them landed; the third turned out to be the wrong work.

**What was built**

- [x] **`source/models/quantize.h/.cpp`** — `apogee models quantize in.gguf out.gguf --type Q4_K_M` via `llama_model_quantize`, behind `APOGEE_ENABLE_LLAMA` with a refusal that names the flag. **Verified live: 5 GiB F16 → 1 GiB Q4_K_M in 11 seconds**, and the result loads and generates.
- [x] **`GgufInfo::file_type` / `is_quantized()`** — the header already knew.
- [x] **The Ollama projector layer**, read as its own file, with `models pull` fetching it alongside the model and printing the `mmproj_path` line to paste.
- [x] **A SafeTensors repository now names the conversion path** instead of a bare "no .gguf".
- [x] **11 new tests** (713 total), green in both builds.

**The plan was wrong, and one HTTP request said so.**

This item was written on the assumption that Ollama ships vision models as a **single combined blob**, and that Apogee would need a transform to deal with it. Milestone O had already narrowed the question — mtmd needs a *separate* projector, so the operation would be an *extract* rather than a *strip*. Before building either, the registry was simply asked:

```
llava:      model 4108.9 MB   projector 624.4 MB   license   template   params
moondream:  model  828.7 MB   projector 909.8 MB   license   template   params
```

**Ollama already ships the projector as its own layer** — `application/vnd.ollama.image.projector`. There is nothing combined, nothing to extract, and nothing to strip. The right work was not a transform at all: it was for the store reader to *notice a layer it had been ignoring*, and for `pull` to copy it. Two carefully-reasoned designs, the second derived from the first, both retired by a `curl` that cost nothing.

The lesson is the cheap one: **a plan written before anyone looked is a hypothesis about the world, and the world is queryable.** The finding was true when it was made; it stopped being true, and nothing in the document said so because documents cannot notice.

**Quantize's error message, improved by running it.** The first live attempt was against an already-quantized model, and llama.cpp does refuse that — after a couple of hundred per-tensor log lines, by which point `requantizing from type q8_0 is disabled` has scrolled away and the user sees a bare failure. The header already carries `general.file_type`, so it is now caught up front in one sentence that names the way forward. Two false-positive tests guard it: an F16 input and a header with no `file_type` must both be allowed through, since refusing either would block the only path that works.

**Two UX faults, also from running it.** `quantize` announced "this reads and rewrites the whole model" *before* discovering it could not, which reads as a crash rather than a refusal; and `quantize types` was unreachable because CLI11 demanded the two positionals before the listing callback could run. It is `--types` now.

**Guardrails, each mutation-tested (five mutations, all caught).** Refusing without naming the flag, silently overwriting an existing output, ignoring the projector layer, dropping the SafeTensors conversion path, and failing to detect an already-quantized input.

**An observation worth recording for model profiles** (then queued; shipped later the same day — see [Milestone P](#milestone-p--model-profiles))**.** The freshly quantized Llama 3.2 loads and generates — and answers with ChatML markers and prompt echo, because this GGUF ships no embedded chat template and the narrow name-matched registry falls back to ChatML. The **pre-existing** Q4 of the same model, which Apogee never touched, produces worse output still. So this is not a quantization defect: it is the quirk layer's absence, observed directly. Local models on this machine are not usably conversational until model-profiles lands, which is the most concrete argument for that item anyone has made so far.

**What SafeTensors does and does not do.** A SafeTensors repository is now refused with the `convert_hf_to_gguf.py` invocation and a note that the script needs Python with torch and transformers. Apogee does **not** run it: a C++ harness cannot assume that environment exists and should not install it on someone's behalf. Dataset downloads remain unimplemented and are not refused with a special message — they simply are not GGUF, and land in the same branch.

---

## Milestone P — Model profiles

**Goal.** The local-model quirk layer: a per-family profile registry with an explicit resolution ladder, and a streaming filter that keeps a model's private reasoning out of its answer.

### 2026-09-07 — `model-profiles`: characterized, and the premise was wrong

**The characterization came first, and it contradicted the item.** Three families were pulled and run before any code was written:

| Family | Embedded chat template | Observed |
|---|---|---|
| `gemma3` (1b-it Q8_0) | **yes** | Clean. Answered "Paris". Nothing to strip. |
| `qwen3` (3.6-28e Q4_K_M) | **yes** | **Emitted `<think>\n\n</think>\n\n4` — all of it reaching the user.** |
| `llama3` (3.2-3b, local files) | **no** | Degenerate on every prompt. |

This item was written around a Gemma 4 build that shipped **no** chat template and had to be reverse-engineered — that was the case the bespoke-override slot existed for. **Gemma 3 ships a good template and needs no help at all.** The family that needed help was Llama, and its files here carry a content hash where a name should be and degenerate like base models, so nothing about it could be verified.

Had the profiles been written from the item's premise rather than characterized, Apogee would now carry a hand-written Gemma template that overrode a working one.

**What was built**

- [x] **`source/backends/think_filter.h/.cpp`** — the streaming reasoning filter, with a hold-back buffer sized to the longest marker so a tag split across two tokens is still recognised. **Fixes an observed bug**: Qwen's answer is now `4`, not `<think></think>4`.
- [x] **`source/backends/model_profile.h/.cpp`** — the registry and its ladder: explicit setting > the GGUF's own `general.architecture` > a name hint > nothing.
- [x] **`LlamaCppProvider` implements `ModelBehaviorReporting`**, and filters its own stream at the source so display, returned text, and persisted history cannot disagree.
- [x] **`models list` and `models info` report the resolved profile**, its verified state, and the evidence line behind it.
- [x] **44 new tests** (738 total), green in both builds.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| Ladder rung 2 | The **file's architecture** above a name guess | An architecture is a fact recorded in the GGUF; a name is a string somebody chose. This is "specific before family" in the form that bites. |
| `verified` | True only for what was **run here** | Under the open-model policy no allowlist makes any family a promise, so characterization state is the only signal a user gets. Two of five profiles are verified; the other three say why not, and a test asserts that list is exactly `{gemma3, qwen3}`. |
| A known profile's empty tag list | Honoured as "emits none" | Gemma 3 was *observed* emitting no wrapper. Substituting the defaults there would strip text it never wrapped — which is why `known()` exists at all. |
| Reasoning filtering | At the **source**, in the provider | Filtering at one surface and not another is how a `<think>` block ends up in a saved transcript after being hidden on screen. |

**Guardrails, each mutation-tested (five mutations, all caught).** Removing the hold-back, eating a partial marker at end of stream, routing an unterminated block's residue into the answer, substituting defaults for a known profile's empty list, and swapping ladder rungs 2 and 3.

The third of those **survived its first test**, and the reason is worth keeping. Inside a reasoning block the filter streams text to the thinking sink *as it goes*, holding back only what could still become the close marker — so by end of stream almost nothing remains to leak, and the assertion had nothing to fail on. A second case that ends the stream mid-`</think>` leaves six held-back bytes, and that one catches it. This is the third time on this branch that a green mutation exposed a test which could not observe the property it named.

**What was NOT done at first, and why the item stayed open.** Three acceptance criteria were unmet, and all three needed model output nothing here produced: a **channel-header markup filter**, **control-token tool-call parsing**, and the display/parser opener-parity assertion that only matters once a parser exists. The evidence for those came from Gemma 4's control tokens; Gemma 3 emits none, and this item's own core constraint is that a profile is characterized from real weights rather than a published format. Building them blind is precisely the guesswork the constraint forbids, so they stayed queued against a model that actually emits one.

### 2026-09-07 — the gate opened: gpt-oss emits both, and the earlier transcription was right

**The model this was waiting for.** `gpt-oss-20b` (MXFP4, 11.3 GiB, pulled from Hugging Face through Apogee's own acquisition ladder) emits **both** mechanisms. The same framing had been transcribed earlier from an Ollama manifest and marked unverified against generation, *because the GGUF would not load in the llama.cpp then at hand at all*. Apogee's pin (`549b9d84`) carries `LLM_ARCH_OPENAI_MOE`, so what could not be checked then is checkable here. **It was right** — which is worth recording precisely because the last two characterization runs on this branch contradicted the plan.

**The bug, verbatim.** "What is 2+2? Answer briefly." reached the caller as:

```
<|channel|>analysis<|message|>The user asks: "What is 2+2? Answer briefly." The answer is 4.<|end|><|start|>assistant<|channel|>final<|message|>4
```

Every character of it, shown as the answer. And with one tool declared:

```
<|channel|>commentary to=functions.read_file <|constrain|>json<|message|>{"path":"/tmp/notes.txt"}
```

printed as prose while nothing dispatched — the two-symptom failure this item was written against, reproduced exactly.

**Why it is visible at all, and the design that came out of it.** llama.cpp's load log settles it: `<|channel|>`, `<|message|>`, `<|start|>` and `<|constrain|>` are set to **USER_DEFINED**, not CONTROL, so they detokenize into the stream even with `special = false`. `<|return|>` and `<|call|>` *are* control tokens and are end-of-generation, so they never reach the text and generation already stopped correctly. The visibility half could be solved with a per-template `show_special` switch; **Apogee needs none** — there is nothing to turn on, only framing to remove.

The characterization also refuted the obvious design. Stripping headers alone would have dropped the model's *reasoning* into its answer, because `<|channel|>analysis<|message|>` … `<|end|>` is not framing to delete — it is a reasoning block whose opener happens to be a header. So the profile splits it: the analysis channel is a **`TagPair` for the existing `ThinkFilter`**, and only what is left over is the markup filter's. Three filters compose in the provider, and **the order is load-bearing**:

| Order | Filter | Why here |
|---|---|---|
| 1 | `ThinkFilter` | The reasoning block's opener *is* a header. Strip headers first and the block loses its boundary, putting the model's working into the answer — Milestone P's Qwen bug, reintroduced by a different route. |
| 2 | `ToolCallGate` | Keys on `<\|channel\|>commentary to=`, and the markup filter would have eaten the `<\|channel\|>` half of it. |
| 3 | `MarkupFilter` | What is left is pure framing between the channels. |

**What was built**

- [x] **`source/backends/markup_filter.h/.cpp`** — the header filter. Grammar: OPEN, an identifier run, optional whitespace, an **optional** CLOSE. Both gpt-oss shapes are handled — `<|channel|>NAME<|message|>` closes, `<|start|>NAME` does not — and binding to the identifier rather than the close is what keeps an unterminated header costing one word instead of a paragraph.
- [x] **`source/backends/native_tool_calls.h/.cpp`** — `tool_call_openers()`, the parser, and `ToolCallGate`. The gate withholds a call from the display and hands **exactly the bytes it withheld** to the parser.
- [x] **The gpt-oss profile**, verified, with the evidence line naming the model and what it emitted.
- [x] **`LlamaCppProvider` declares `InTextToolCalling`**, answering from the resolved profile rather than from the backend type. It had deliberately not declared it while no parser existed; that comment is now the implementation.
- [x] **`FakeLlamaRuntime::script_text`** — generation scripted as the exact pieces a model emits. Needed because a real tokenizer splits framing where no word-splitting fake would: gpt-oss emits `commentary` as `comment` + `ary`, straight through the middle of a marker.
- [x] **32 new tests** (810 total), green in both builds.

**Verified live**, not only against fixtures: `apogee complete` answers `4`; `apogee complete --tools` dispatches `fetch_url` and answers from its result; the saved session file contains `Paris` and no framing at all.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| The analysis channel | A **reasoning pair**, not a header to delete | Its content is the model's working. Deleting it would hide reasoning the user asked to see; leaving it would put it in the answer. |
| Header defaults for an unprofiled model | **None**, unlike reasoning pairs | The asymmetry is the point: a header is deleted outright once matched, so guessing one for an uncharacterised family risks deleting its answer. Recognising too few reasoning wrappers only shows some working. |
| `tool_call_openers()` | One entry, for the one family observed | A list is not a wish list. A second entry belongs to whichever family is run next. |
| Suppress-then-parse | The gate holds the bytes and the parser reads *those* | Two lists that must agree are two lists that will not. Here there is one buffer, so they cannot disagree. |
| GBNF grammar-constrained sampling *(open call)* | Recorded default taken: **not now** | The model emits a well-formed call unaided. Constraining it into a grammar it was not trained on trades one failure mode for a worse one. |
| An Ollama layer's chat template disagreeing with the profile *(open call)* | Recorded default taken: **a hint, never auto-applied** | The sidecar already treats it as advisory, and the profile is the characterized statement. |

**Guardrails, each mutation-tested (20 mutations, all caught).** Emptying the shared opener list; the **display** and then the **parser** each leaving that shared list; removing the next-opener bound; accepting an unbalanced argument object; removing the safety net; dropping the tool-name plausibility check; not stripping the `functions.` namespace; binding a header to its close alone; removing either filter's hold-back; eating whitespace after an unclosed header; giving an unprofiled model default headers; demoting the reasoning pair; not publishing the openers to the harness; dropping a header from the profile; and, in the provider, each of the three filter orderings, not flushing, dropping parsed calls, and claiming `InTextToolCalling` unconditionally.

**Three of those survived their first run, and every one of the three was a test that could not see the property it named.**

- *The next-opener bound.* The test fed a malformed call followed by a good one — but this grammar rejects a malformed prefix long before it could swallow anything, so the bound changed no outcome. The input that distinguishes it is narrower: a call whose argument object is unterminated, followed by one supplying a closing brace before opening its own. Unbounded, the brace scan spans both and **dispatches a real tool with another call's text as its arguments**. That test now exists.
- *The tool-name check.* The test used prose containing a brace, which never reaches an opener at all. It now feeds a call whose arguments parse and whose *name* is empty or a path.
- *Opener parity.* The first attempt mutated the list's contents, which does not break parity — both sides read the same list, so both change together. The mutation that matters is one side ceasing to read it, and there are now two: one for the display, one for the parser.

**And the harness itself was wrong twice**, which is the more useful lesson. Restoring a mutated file with `mv` gave it an *older* mtime than the object built from it, so the next build was skipped and a stale binary reported a clean tree — one supposed pass was really the previous mutation still compiled in. Separately, a patch whose search text did not match changed nothing and was duly reported as "survived": **a no-op mutation always survives, and a harness that cannot tell that is reporting on itself.** Both now fail loudly. The pattern is the same one recorded in Milestone E about the install guard that supplied its own argument.

**Found in passing, and left alone.** Running a tool live for the first time on this branch showed `[tool] [tool] fetch_url` — `agent/tool.cpp` prefixes the tag that `CliReporter` adds again, and machine mode emits a `text` field the protocol reference says should carry no prefix. It is pre-existing, pinned by three tests, and not this item's work; it is filed as its own task.

---

## Milestone Q — The retrieval floor

**Goal.** RAG that works with no model, no API key, and no network: a SQLite chunk store with an FTS5/BM25 index, and `--rag` splicing retrieved context into the outgoing request without ever touching persisted history.

### 2026-09-07 — `embedstore-lexical-rag`: lexical first, and a question that returned nothing

**Verified live** against Apogee's own documentation, with zero backends configured:

```
$ apogee embed ingest apogee-docs ./lib/documentation/assistant
2 file(s), 92 chunk(s)
skipped 1:
  image.png: looks binary

$ apogee embed query apogee-docs "listening socket"
0.869  [lexical]  CLAUDE.md#27
    turns never open a listening socket  **Rule.** Only `apogee serve` may own a port…
```

**What was built**

- [x] **SQLite with FTS5**, fetched and compiled with the flag on (`third_party/`).
- [x] **`source/embedstore/store.h/.cpp`** — per-collection database, single-transaction migrations, an **external-content** FTS index kept current by triggers, and `integrity-check 1` for validation.
- [x] **`source/embedstore/fts.h/.cpp`** — the query builder, and BM25 normalisation to `s/(1+s)`.
- [x] **`source/embedstore/chunk.h/.cpp`** — overlapping chunking by **codepoint**.
- [x] **`source/embedstore/ingest.h/.cpp`** — directory walk, binary sniffing, PDF via `pdftotext`.
- [x] **`source/commands/embed.cpp`** — `ingest` / `query` / `list` / `info` / `delete`.
- [x] **`source/agentloop/rag.h/.cpp`** and **`--rag` on `complete` and `chat`**.
- [x] **48 new tests** (778 total).

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| SQLite | **Fetched, not the system library** — the recorded default | The libcurl precedent points the other way, but sqlite fails it twice: **Windows ships no system sqlite3** (two of six targets), and **FTS5 is a compile-time flag** a packager may have left off. A floor that depends on someone else's build options is not a floor. My instinct was to use the system library; checking the targets showed the recorded default was right. |
| UTF-8 handling | **Hand-rolled, deviating from the recorded utf8proc default** | utf8proc exists for normalisation and character properties; chunking needs "advance N codepoints", which is `(b & 0xC0) != 0x80`. Same trade as the GGUF reader and SHA-256 earlier in this project. |
| Query terms | **OR-joined, not FTS5's implicit AND** | See below — this one was a bug, not a preference. |

**The first real query returned nothing, and the reason was a design flaw.**

`"what is the capital of France"` against a corpus containing *"the capital of France is Paris"* matched **zero** documents. FTS5 joins space-separated terms with an implicit **AND**, so the document had to contain the word *what* — and a question always carries words its answer does not.

Confirmed directly before changing anything:

```
AND-form: 0
OR-form:  1
```

OR is also what makes BM25 worth having: it ranks by how many terms matched and how rare they were, so a chunk sharing *capital* and *France* outranks one sharing only *the*. AND throws that ranking away by refusing everything imperfect.

**The query builder is a security boundary and a usability boundary, and they are the same boundary.** Every term is quoted as an FTS5 literal, so a query can express nothing but "documents containing these words". The **injection corpus** — 28 strings a person might plausibly type, including `AND`, `NEAR`, unbalanced quotes, `text:`, `*`, `(`, `'; DROP TABLE chunks; --`, and non-ASCII — is a permanent regression suite asserting one sentence: **a search never raises a syntax error.**

**Injected context never reaches persisted history**, asserted as a grep for a distinctive sentence that appears in the request and not in the saved transcript — and verified against a real session file on disk after a live `chat --rag`. Mutating the loop's splice to leak it turns that test red *and* the loop's own pre-existing one, which is the right pair to fail.

**Guardrails, each mutation-tested (five mutations, all caught).** Passing the raw query to MATCH, chunking by byte instead of codepoint, leaking transient context into history, forgetting BM25's negative sign (which silently inverts the ranking), and dropping the delete in `replace_source`.

One of those needed a second attempt: the first "leak into history" mutation changed a message's role rather than where it was written, so it did not touch the property at all. The property lives in the loop's splice, and mutating *that* is what proved the test.

**PDF support verified both ways.** A generated PDF's text is ingested and retrievable by a phrase that exists only inside it; a file `pdftotext` cannot read is skipped **by name, with a reason**, and the rest of the directory still ingests. That is the rule for Apogee's one optional external binary — its absence must never be the reason a directory of Markdown failed.

**What was NOT done at first.** One acceptance criterion was unmet and the item stayed open for it: **auto-registration of `embeddings:` config entries** through the comment-preserving edit helpers, and the **`auto_rag` key** for always-on injection without the flag. Both needed an `embeddings:` config section that did not exist yet, plus an edit helper and its byte-diff test — a coherent slice of config-layer work rather than a loose end. It shipped five days later; see below.

### 2026-09-12 — the config layer: collections the config knows about, and retrieval nobody typed a flag for

**What was built**

- [x] **An `embeddings:` section** (`harness/config.h/.cpp`) — a map of collection name → `EmbeddingConfig{chunk_size, chunk_overlap, description}`, compared case-insensitively and rejecting fold-collisions by name, exactly as `backends:` does. Plus **`auto_rag`**, a top-level scalar naming one collection to retrieve from on every turn.
- [x] **`append_embedding` / `delete_embedding`** (`harness/config_edit.h/.cpp`). The existing `append_backend`/`delete_backend` bodies became two section-generic internals, `append_entry` and `delete_entry`, and all four public helpers are now thin callers — one copy of the placement rule, the separator rule, the collision rule, and the comment-ownership rule, rather than a second.
- [x] **Registration on ingest** (`commands/embed.cpp`). The first `apogee embed ingest <name>` writes the entry through `edit_config_file`, recording the chunking it used; a later ingest reads that chunking back as its default, and a flag still wins for one run without rewriting the file. A config that will not load, or will not take the edit, costs the user the registration and **says so on stderr — never the ingest**, which is already on disk.
- [x] **One shared decision for both surfaces** (`commands/helpers.h/.cpp`): `choose_rag_collection(flag_given, flag_value, auto_rag)` and `describe_retrieval(...)`. `complete` and `chat` both call them, so the precedence cannot differ between surfaces and the status line is the same sentence on both.
- [x] **`config get`** learned `auto_rag`, `embeddings`, `embeddings.<name>`, and `embeddings.<name>.<field>`.
- [x] **The starter config documents both**, at the end of the file so a section created on first ingest lands beneath its own documentation.
- [x] **46 new test cases** (824 total): loader, golden-file, helper table, the transient guard on the config path, and two exec-style checks.

**Verified against the real binary and the real shipped template**, in `cli.config_lifecycle`: after `config init` and one ingest, the file is byte-for-byte `PRISTINE + "\nembeddings:\n  notes:\n    chunk_size: 512\n    chunk_overlap: 64\n"` — a literal equality over the whole comment-dense template, not a "still contains". Then with `auto_rag: notes` and no flag anywhere, `complete` reports `1 chunk(s) from 'notes', top … [lexical] (auto_rag)`; `--rag other` reports `other.db` and never touches `notes`; a piped `chat` turn announces the injection and the saved session carries the question and none of the injected text.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| Section shape | A **map** keyed by name, not a list of `- name:` items | The edit helpers, the loader's collision check, and `config get`'s dotted keys are all map-shaped. A list would have needed a second copy of every one of them for one section. |
| Chunk sizes *(open call, default taken)* | **On the collection entry**, written at first ingest | A corpus of ADRs wants 768 where prose wants 512, and re-typing that on every ingest is how corpora end up chunked inconsistently. Proven in the e2e: an ingest with `--chunk-size 100` and a re-ingest with no flags produce the same chunk count. |
| `auto_rag` arity *(open call, default taken)* | **One collection**, a top-level key beside `status_mode` and `color` | Several would mean merging incomparable score scales, which is `vector-hybrid-rerank`'s problem. |
| `--rag ""` | **The off switch for one run**, distinct from no flag | A key that cannot be overridden per invocation is a key people stop using. `flag_given` is therefore a separate input from `flag_value.empty()` — an absent flag falls through to the config, a present-but-empty one does not. |
| When `auto_rag` is read | **At turn build, from the file**, on every chat turn | So an edit mid-session takes effect on the next question like every other config value. `RagSettings` holds a *path*, not a value. An unreadable config costs that turn its `auto_rag` and says so; it never ends the turn. |
| Registration failure | **Reported, never fatal** | The corpus is already on disk. Telling a user their ingest failed because their config could not be edited would be a lie about what happened. |
| A setter for `auto_rag` | **None**; it is hand-edited like `status_mode` and `color` | The template documents it commented-out, which is the documented flow for every other top-level key. A subcommand can come when something needs to set it programmatically. |
| Registered-vs-exists | **Registration is a convenience, not a requirement** | A collection works the moment its file exists, and `auto_rag` may name one that was never registered. Retrieval never depended on config and does not start to. |

**Guardrails, each mutation-tested (17 mutations, all caught).** Eleven judged by the unit suite: an empty flag falling through to the config; the config beating the flag; injection from config not announced; the retriever not named; a new entry landing below trailing comments; no blank separator; the entry's fields in another order; delete leaving the separator behind; colliding names merged silently; `auto_rag` parsed but dropped; `find_embedding` never finding. Six judged by the two exec-style tests: a known collection re-registered on every ingest; the entry's chunk size ignored on re-ingest; `complete` and then `chat` unable to tell `--rag ""` from no flag; `chat` reading `auto_rag` once at startup instead of every turn; `complete` injecting silently.

**Two of the e2e assertions were strengthened before the mutations ran**, because reading them against the mutation list showed they would let one through. "Not registered twice" had checked stdout for the word *registered*; a re-registration that *failed* prints to stderr and exits 0, so it would have passed. It now requires a silent stderr. And only `complete` had its `--rag ""` plumbing exercised; the shared decision is one function, but whether the flag was *present* is plumbed per surface, so `chat --rag ""` is now checked too.

**What the exec-style tests cannot see, recorded rather than papered over.** With the mock backend nothing echoes the request, so an omission — a surface that announces retrieval and then never splices the prefix into the outgoing request — is invisible to `cli.config_lifecycle`. The unit-level guard (`rag_test.cpp`, `[transient][config]`) closes it where it can: it asserts the secret **does** reach the provider *and* never reaches history, so it cannot pass by not injecting. The surfaces' own wiring of `transient_prefix` was verified live against a real model in Milestone Q and is unchanged here.

**Two platform facts surfaced by the tests, both kept.** CMake's `execute_process` drops an empty list element, so there is no way to pass `--rag ""` from a `cmake -P` script at all — and `--rag=` reaches CLI11 as a flag still awaiting its value, which then swallows the prompt and blocks on stdin (the first run of the e2e hung there). The off switch and the mid-session re-read therefore live in a POSIX shell check, `cli.auto_rag`, the same recorded-skip convention as `no_listen_check.sh`; the precedence itself is table-tested everywhere. And a one-document corpus scores its only hit as `0.000` — BM25's IDF for a term in every document is zero — which is a property of the lexical floor and not a regression; the e2e corpus was made six sentences long so chunk size has something to change.

**Retrieval quality is the lexical floor's, honestly.** Targeted terms rank well; a question padded with common words dilutes and ranks the right chunk lower. That is inherent to BM25 over an OR of every word, and it is precisely what `embedding-clients` and `vector-hybrid-rerank` exist to improve — the floor is meant to be a floor.

---

## Milestone R — Embedding clients

**Goal.** The vector supply side: turn text into vectors through every backend that can, discovered as a capability of the entry rather than looked up in a list of types — OpenAI and Gemini over their embeddings endpoints, llama.cpp in-process — so `vector-hybrid-rerank` has embedders to consume and every v0.1.0 backend shipped without dead code.

### 2026-09-12 — `embedding-clients`: the capability the type list could not have learned

**What was built**

- [x] **`source/backends/openai_embed.h/.cpp`** and **`google_embed.h/.cpp`** — pure wire translators for `POST /v1/embeddings` and `:batchEmbedContents`, with the vendor default model, the documented native widths, and a parser that refuses a body whose row count does not match the request. OpenAI rows are placed **by their `index`**, never by position — the API does not promise request order, and a parser that trusts it hands input 0 the vector for input 1 silently.
- [x] **`source/backends/embedding_batch.h/.cpp`** — the one shared piece: `batch_ranges`, splitting a list at a provider's maximum. Batch-first is a Core constraint (per-chunk calls are the cost trap); the recorded default took the **documented maxima** as the batch sizes, 2048 inputs for OpenAI and 100 rows for Gemini.
- [x] **`OpenAIProvider` and `GoogleProvider` implement `EmbeddingCapable`.** Same key, same retry policy, same `fail()` that never echoes the key; a bounded 120 s timeout, unlike the streaming chat path, because an embeddings call returns one body and a long silence *is* a hung connection.
- [x] **`BackendConfig::embedding_model`** — the model an entry uses when it embeds, beside the `model` it chats with. One OpenAI key serves both, so one entry does both. Parsed, written by `append_backend`, settable by `config add-backend --embedding-model`, readable by `config get`. Empty means the vendor's default, decided by the **provider**, not the loader — so the default differs per vendor without the loader knowing any vendor.
- [x] **`LlamaModel::embed_batch` / `embedding_dimensions`** on the runtime seam, **`source/backends/llamacpp_embed.h/.cpp`** for the provider side (64 texts per runtime call so Ctrl-C between slices is honoured promptly), and **`LlamaCppProvider` implements `EmbeddingCapable`**: mean-pooled, L2-normalised vectors from a dedicated embedding context, cleared between batches, so the conversation's warm KV state is never touched.
- [x] **`tests/support/embedding_fixtures.h`** — the dimension fixtures `vector-hybrid-rerank`'s per-store binding will consume: both vendors' native widths, and response bodies of deliberately different, deliberately small widths.
- [x] **47 new test cases** (858 total), green in both builds.

**Verified live, in-process, against a real GGUF** (gpt-oss-20b, the model already on disk from Milestone P): four texts embedded in 1.07 s including the model load, width 2880, every norm 1.0000, the two sentences about Paris closer to each other (0.58) than either to a sentence about SQLite (0.53, 0.33), and a warm second call at 43 ms. Then 64 chunk-length texts: packed into one decode, 2.15 s; one decode per text, 7.65 s.

**Two bugs the scripted runtime could not have found, both found by that run.**

*The first* was an abort on the very first real call. The embedding context was created with llama.cpp's default of **one** sequence, and a batch using sequence id 1 against it is `decode failed (-1)` — which Apogee turned into a clear `ProviderError`, as designed, but the call still failed. `n_seq_max` is now the packing bound.

*The second* was quieter and worse. The same text embedded alone and embedded in a batch came back at cosine **0.986** of each other — deterministic per shape, so not float noise. A controlled run showed the rule: a text is perturbed exactly when a **shorter** text shares its batch, worse the closer the lengths (a six-token companion to an eight-token text: cosine **0.565**), and never when every batch-mate is at least as long. That is llama.cpp's non-unified KV mode giving each sequence its own stream and splitting a mixed-length batch into **equal-length micro-batches** — with pooling done per micro-batch, so the longer sequence is pooled over a fragment of itself. `kv_unified = true` makes the batch one micro-batch with the sequences distinguished by id, and every case went to 1.000000: single tokens, empty inputs, six-token companions, and 64 packed texts against 64 single ones at 0.99984 — the ordinary Metal kernel-shape noise, not a 44% distortion. The one-decode-per-text alternative would also have been correct, at 3.5× the cost; the fix keeps the packing.

Neither is reachable from the merge-blocking target, whose runtime is a fake with no allocator and no micro-batches. They are recorded here, in the code beside the two parameters, and in the llama-enabled build's live check — and they are the argument, once more, for running the real thing before believing the green.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| Which entries embed | **The provider type inherits `EmbeddingCapable`**; Anthropic, the vendor CLIs and the plain mock do not | The harness discovers it by asking the object. A test over *built* providers asserts Anthropic answers no and OpenAI and Google yes — the Core constraint, against the objects the factory actually produced. |
| The embedding model | A **per-entry `embedding_model`**, defaulting to the vendor's | A cloud vendor serves chat and embeddings from different models behind one key. Making the user add a second entry to embed would have meant a second key reference for the same key. |
| Dimensions | The documented width for a known model, else **0 until the first vector**, then observed | The interface's "known after the first call", made true rather than guessed at. |
| Fixtures | **Shaped from the documented format, not recorded** | Apogee holds no API key and never reads a user's, so there is nothing to record with; the chat translators were fixtured the same way. Said so in every fixture file. |
| Local pooling | **Mean, L2-normalised**, one embedding context per model | What llama.cpp's own embedding tool does by default; cosine downstream becomes a dot product. A separate context because generation and pooling want different settings, and because a document decoded into the chat KV would corrupt the warm state. |
| An over-long local input | **Truncated to the batch** | The chunker bounds real inputs far below it; this is the defence against a pathological one, and the choice llama.cpp's tool makes. |
| An empty local input | Embedded as a single space | A zero-length sequence has nothing to pool. The chunker never emits one; the defence costs nothing. |
| Idle policy | **An embed is a use**, and runs the idle check on the way in | A long ingest is nothing but embedding calls; an embed-only workload that never counted as use would unload the weights under itself — or, the gap the test found, never unload at all. |

**Guardrails, each mutation-tested (15 mutations, all caught).** The last batch overrunning the input; OpenAI rows placed by position; a row-count mismatch accepted; OpenAI never splitting; the OpenAI key leaking into the URL; the entry's model ignored; an unknown width never learned; Gemini rows losing their `models/` prefix; Gemini embedding through the chat model's URL; Gemini batching at OpenAI's size; llama handed a whole corpus in one call; llama never reporting its width; `embedding_model` not parsed; not written; and `embed` skipping the idle check. **The two live findings are not on that list and cannot be**: no scripted runtime models `n_seq_max` or micro-batch splitting, so they are held by the code comments and by re-running the live check when the llama.cpp pin moves.

**The idle check was missing when the test for it was written.** `expire_if_idle()` ran at the start of a chat turn and nowhere else, so an embed-only process with `idle_unload_seconds` set would have kept its model forever. The test asserting "an embedding is a use" failed on its last assertion — the model never unloaded — which is the right way round for a test to be wrong.

---

## Milestone S — The retrieval matrix

**Goal.** The rest of retrieval on top of the lexical floor and the embedders: vectors in the store bound to the model that built them, cosine and hybrid search, **one** per-turn resolver every surface shares, and a rerank judge that can never cost context — with the honesty rule that a turn runs exactly one retriever and reports the one that actually ran.

### 2026-09-13 — `vector-hybrid-rerank`: one decision, every surface, and a judge that never fails

**The user's call first.** The item carried one `[user]` open call — whether auto may spend money on a paid embedder — and it was put to the user before any code, in plain terms: *"Corpus needs a yes; questions don't."* A whole collection is never vectorised through a metered embedder on Apogee's initiative (that takes `--retriever vector` or a `retriever: vector` pin); a question against a collection already built with that model is one small call and is allowed. Whether an embedder is metered is a fact the provider states about itself (`EmbeddingCapable::embedding_is_metered`, defaulting to *true* so an unknown embedder is assumed to cost), never a type list.

**What was built**

- [x] **`source/embedstore/vector.h/.cpp`** — float32 blobs, cosine, and `fuse_rrf`: Reciprocal Rank Fusion with k = 60, each half fetched 50 deep, **reading only ranks** because BM25, cosine and RRF are three incomparable scales. Ties break by chunk id.
- [x] **Store schema v2** — `chunks.embedding` and `chunks.dim` beside the text (`dim = 0` is lexical-only), `search_vector`, `search_hybrid`, `stats()` (width, lexical-only count, distinct widths), and the **per-store binding**: `embed_model` / `embed_dim` in `store_meta`, recorded at vector ingest and compared at query time. A v1 store gains the columns on open, in the one migration transaction — tested against a real v1 file the previous build wrote, checked in as `tests/fixtures/embedstore/schema-v1.db`.
- [x] **`source/agentloop/embed_func.h/.cpp`** — the one embedding seam: a function from texts to vectors plus which model, how wide, and whether metered, resolved through the embedding role chain and `Harness::embedder_for`.
- [x] **`source/agentloop/retriever.h/.cpp`** — **the one per-turn resolver**, a pure function over facts, now a `## ⚠` invariant in CLAUDE.md. Flag > pin > auto. An impossible explicit `vector` is a hard error naming lexical; an unqualified `vector` pin excludes the collection with a note; hybrid is explicit-only and a hybrid without a vector half is *reported* lexical; a mismatched model, partial coverage or mixed widths demote the store **wholesale** with the re-ingest hint. `resolve_ingest_retriever` carries the spend rule. `valid_retriever` is the single validator behind the flag, `apogee check`, and `/retriever`.
- [x] **`source/agentloop/rerank.h/.cpp`** — one judge call over the widened candidates (10× the limit, capped at 50, 400-codepoint snippets) under the **(ranked, applied) contract**: every failure path returns raw order with `applied = false`; an empty array is a verdict, not a failure. `reranked` on the status line is set from the same place as the ordering, so a surface cannot claim what did not happen.
- [x] **`agentloop::retrieve_for_turn`**, and `commands::retrieve_for_collection` over it, so `complete` and `chat` gather the same facts and make the same decision. `--retriever` and `--rerank` on both, and on `embed query`; `embed ingest --retriever`; `embed info` shows the binding and coverage.
- [x] **Per-collection `backend:`, `retriever:`, `rerank:` pins** in config, written by the same helper that registers a collection; **`check` rejects a `retriever:` typo** and a `rerank:`/`backend:` that names nothing configured.
- [x] **Session persistence**: `retriever` and `rerank` are saved as the session's *settings* (never a per-turn resolution), restored on resume with flag > saved > default, changed mid-session by `/retriever` and `/rerank`, and a judge deleted since resumes without reranking with a warning.
- [x] **A config-built mock embedder** (`type: mock` with an `embedding_model`), so the whole vector path is driven end to end with nothing installed — and two of them with different names are two vector spaces, which is what the mismatch e2e needs.
- [x] **58 new test cases** (902 in all), green in both builds.

**Verified live, in-process, with gpt-oss as embedder AND judge.** Apogee's own contributor docs — 5 files, 689 chunks — vectorised in 1 min 20 s (2880-d, every chunk, binding recorded). `embed query` on auto reported `[vector]` with the listening-socket rule as its top hit; `--retriever hybrid` reported `[hybrid]` at RRF's small numbers; `--retriever lexical` reported `[lexical]`. A `complete` turn with `--retriever hybrid --rerank gptoss` injected four chunks and answered from them, its status line reading `[hybrid, reranked]`.

**The live run found the judge's budget wrong.** At 256 tokens — a constant sized for a Haiku judge — every rerank through gpt-oss degraded to raw order with *"returned something that was not a ranking."* Correct under the contract, and useless: a **reasoning** model spends its budget thinking before the array appears, and never reached its final channel. At 1024 the judge applied on every surface and visibly reordered and dropped. Recorded beside the constant.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| Spend *(user)* | Corpus needs a yes; questions don't | Above. |
| Vector search *(open call, default taken)* | Brute-force cosine over blobs | sqlite-vec waits for a collection that outgrows a linear scan. No query-vector cache: a cache that made the per-question call silent would hide the decision just made. |
| One collection per turn | Kept from Milestone Q | So the resolver is single-store; per-store hybrid participation modes collapse to "this collection, or nothing, with a note." |
| Structured output for the judge | **Deferred**, behind the never-fail contract | The IR carries no structured-output request; the judge asks for a bare JSON array and the parser tolerates prose and fences. A misbehaving judge costs nothing but the improvement. |
| Judge timeout | The transport's, not a separate deadline | `CancellationToken` has no deadline; Ctrl-C cancels. A hung judge hangs the turn until then — recorded as a limitation rather than solved with a thread that abandons an HTTP call. |
| A transient embedding failure mid-turn | Degrade to lexical and *say so* | Honest about the scale the scores are on; never a failed turn for a flaky endpoint. |
| A source the embedder fails on during ingest | Stored lexical-only, **named** | The store then reports partial coverage and the resolver demotes it wholesale — the hole is visible until re-ingested, never hidden. |

**Guardrails, each mutation-tested (22 mutations, all caught).** In the resolver: auto resolving to hybrid; partial coverage ignored; a model mismatch ignored; an unqualified pin searched lexical silently; an explicit flag substituting instead of erroring; a degraded hybrid reported hybrid; the spend rule dropped. In fusion and the store: RRF reading scores; ranks off by one; lexical-only miscounted. In the judge: garbage flagged applied; an empty verdict treated as failure; the cap removed. On the turn: the explicit-vector error swallowed; an embedding failure failing the turn. A vanished judge resuming silently; `check` accepting a typo; metering not propagated. And four through the real binary: a vector ingest not recording its binding; `embed query` ignoring the hard error; `complete` dropping `--retriever`; `chat` not storing it on the session.

**Two harness lessons repeated, and one honest gap.** The format pass reflowed string literals and a call site after the tests were written, so two mutation patches and one message rewrite silently matched nothing — the no-op detection built in Milestone P is what said so, twice. And the `one_role_resolver` guard fired on two *user-facing messages* that named `models.default_embedding` in its dotted form; they were reworded rather than allowlisted, so the guard stays strict. The gap: with the mock backend nothing echoes the request, so a surface that announces a retriever and then splices nothing is invisible end to end — the unit guard asserts the secret *does* reach the provider on the vector path, and the live run shows the injected chunks answering.

---

### 2026-10-04 — `retrieval-reporting-floor` (backlog item 26s): how strong a match, and a floor below which nothing is injected

**Why.** The attachment-representation spike (2026-10-03, a sandboxed probe) measured two faults.
- **The line misled.** A hybrid turn's score is Reciprocal Rank Fusion, whose best possible value (first in both lists) is 2/61 ≈ 0.033. The stress test's "top 0.023" and "top 0.031" were mid-to-high rank agreement, and the user and the assistant alike read them as junk.
- **The injection had no floor.** It checked only for emptiness. A one-file attach handed the model a 0.000-score excerpt, and an unrelated question injected excerpts at 0.950 lexical, the same score an on-topic one got, because normalised BM25 saturates. Misleading context is worse than none: the stress-test model built its spiral on excerpts that did not answer.

**What was built**

- [x] **Strength, per retriever** (`agentloop/retriever`: `match_strength`, `MatchEvidence`, `MatchStrength`), read off the search's best hit on its own scale. The three scales stay apart.
  - **Hybrid:** the score over RRF's ceiling (`rrf_ceiling()`, 2/61).
  - **Vector:** the cosine as it is.
  - **Lexical:** how many of the question's content words the hit holds, word for word as the index matches, stop words left out (`word_coverage`). BM25's saturation is why.
  - Bands are `strong` / `fair` / `weak`, with each edge a named constant.
- [x] **The floor** (`agentloop/rag`, in `retrieve_for_turn`, so attachments, `--rag`/`auto_rag`, `complete`, `serve`, `analyze` and recall all get it).
  - The confirmed floors: a hybrid match under a quarter of RRF's ceiling, a cosine under 0.25, a lexical match holding none of the question's words.
  - Under the floor the turn injects nothing, chunks or graph section, and says so. The turn itself runs.
  - A hybrid search keeps its vector half (`fuse_rrf` over its two searches, exactly `Store::search_hybrid`), so the best hit's cosine is evidence.
- [x] **The one line**, from the one renderer (`operations/retrieval`), for chat, `complete` and machine mode (whose diagnostics ride stderr). The strength comes first, then the raw score and retriever, then what it was read by:
  - `4 excerpts from the attachments, strong match (0.032 [hybrid], 100% of RRF's ceiling)`
  - `… fair match (0.930 [lexical], 2 of 3 question words)`
  - floored: `nothing relevant in the attachments -- the best match is under the floor (0.660 [lexical], none of the question's words)`, and the same for a collection, its origin kept.
- [x] **Tests**:
  - **Tables:** every band edge and floor per retriever, and `word_coverage`.
  - **The probe's fixtures through `retrieve_for_turn`:** the one-line file, the saturated off-topic question beside an on-topic one, a cosine of 0, a hybrid one-chunk store, a cosine alone as evidence, and first in both lists.
  - **The line's goldens.**
  - **End to end:** a one-line attachment with an unrelated question, where nothing is injected, the turn still answers, and the identical line appears on `complete` and in machine mode.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| Per-retriever floors | **By measurement** *(recorded 2026-10-03)* | No shared threshold can exist across the three scales. |
| How strength reads | A word band per retriever, raw score and retriever in parentheses *(default, confirmed)* | The word for a person, the facts for anyone checking. |
| The floors | RRF under 25% of its ceiling, cosine under 0.25, lexical under one content word *(default, confirmed)* | Named constants, as confirmed. |
| Where the floor applies | Attachment and `auto_rag` turns alike, an explicit `--retriever` too *(default, confirmed)* | One function, so every surface. |
| Hybrid's floor | **Also floored when neither half has evidence: a cosine under 0.25 and none of the question's words** *(group run, flagged for veto)* | At the fetch depth a hybrid search uses (50 a half), a hit's RRF never falls under a quarter of the ceiling, so the confirmed floor alone could never fire. A single chunk is first in the vector half by construction. The two other floors already confirmed are the evidence. |
| Word coverage | **Word for word**, stop words out *(group run)* | Measured: a five-letter shared stem let "painted" count for "painter". The lexical index itself matches whole words. |

**Verified on the real binary.**

- **Lexical** (no embedding model, `lib/documentation/assistant` attached, Qwen3-VL-8B answering):
  - "What is photosynthesis?" went from `4 excerpts …, top 0.660 [lexical]` to `nothing relevant in the attachments -- the best match is under the floor (0.660 [lexical], none of the question's words)`. It matched only "what" and "is".
  - "How is a release cut and published?" reads `fair match (0.930 [lexical], 2 of 3 question words)`.
  - "Who painted the Mona Lisa?" and the sourdough question read `weak match (…, 1 of 3)` and `(…, 1 of 4)`. The docs do hold "painted" and "home", and the model answered from its own knowledge.
- **Hybrid** (Embedding-Gemma): an on-topic question reads `strong match (0.032 [hybrid], 98% of RRF's ceiling)` where it read `top 0.032`.

**Found, and left for the user (flagged).** Measured on Embedding-Gemma over the same docs:
- **Off-topic questions still score cosine 0.56–0.59** ("bake sourdough bread", "the Mona Lisa"), against 0.71–0.76 for on-topic ones. The confirmed cosine floor of 0.25 therefore never fires on this model, and neither does hybrid's evidence rule.
- **Rank agreement is not relevance.** The sourdough question's hybrid line reads `strong match (0.027 [hybrid], 85% of RRF's ceiling)`, because both halves ranked the same chunk first.

A floor near 0.65 would separate this model's cases. Cosine baselines differ by embedding model, though, so a fixed higher floor would drop real matches on another; a per-model floor is the user's call.

One more thing worth knowing: the probe's "unrelated" question, "What is the capital of France?", is in these docs verbatim, in the record of the search's OR-joining, so it matches for real.

**Guardrails, each mutation-tested (17 mutants, all caught).**
- **Lexical:** no floor; stop words counted; and against the first draft's stem rule, endings not shared. On the word-for-word code: a word counted when absent; case kept.
- **Vector:** no floor; the strong edge moved.
- **Hybrid:** no evidence rule; no RRF floor; the cosine ignored; the wrong ceiling.
- **The turn:** the floor not applied; no cosine looked up.
- **The line:** floored unsaid, for attachments and for a collection; no band.

**Not verified.**
- **Cloud embedders** (OpenAI, Google) were not run; their cosine baselines may differ from Embedding-Gemma's either way.
- **The rerank judge** reorders after the floor; a turn the judge would have rescued is not.

## Milestone T — The public inference plane

**Goal.** The third surface: `apogee serve`, an OpenAI-compatible HTTP server for **server deployments** — the executable on a server, remote mobile or desktop clients making REST calls to it — over the same agent loop every other surface runs, with server-owned sessions that are ordinary chat sessions, and the listening-socket invariant given its one reviewed exception.

### 2026-09-13 — `serve-public-plane`: a third adapter, one loop, and a port with a name on it

**What made this cheap is what Milestone F bought.** The loop was extracted behind a Reporter before the surfaces multiplied, so the whole of HTTP rendering is one more adapter over that seam: `SseReporter` turns the same events `CliReporter` draws and `JsonReporter` prints into OpenAI streaming frames. There is no second loop for HTTP. A tool, a retriever, a reasoning filter or a compaction rule reaches a remote client the day it reaches the terminal, and the conformance suite asserts that with the same `MockProvider` scripts the loop tests use.

**What was built**

- [x] **`source/httpserver/`** graduates from a reserved header to a package: `http_types` (the transport-neutral `HttpRequest`/`HttpResponse` and the OpenAI error envelope every refusal takes), `mux` (the route table, `{id}` capture, 404/405 in the error shape), `handler` (one method per route; every chat request is one `agentloop::run` behind a `NullReporter` or an `SseReporter`), `sse_writer` (framing, `[DONE]`, a writer that **cancels the turn's token when the client leaves**), `sse_reporter` (the third Reporter adapter), `session` (the store), and `serve` (the bind policy and the listener).
- [x] **The routes**: `POST /v1/chat/completions` (streamed or not, plus the extensions `system`, `tool_mode`, `apogee_events`, `session_id`, and `?retriever=`/`?rerank=` through the one shared validator and resolver), `POST /v1/completions`, `GET /v1/models` (served backends, the default flagged), `GET /v1/model/status`, `GET /health`, and the session plane `GET /v1/sessions`, `GET`/`DELETE /v1/sessions/{id}`.
- [x] **Meta-frames**: with `apogee_events`, status rides the stream as chunks whose `delta.content` is the empty string carrying a `meta` object — `context_warning`, `rag_search`/`rag_result`, `model_loading`/`model_ready`, `thinking`, `tool_call`, `token_count` — the `harness::StatusEvent` vocabulary the terminal status line already renders. A stock client appends nothing; an event-aware one drives an indicator.
- [x] **Server-owned sessions as chat sessions.** The store holds `logger::Session` objects and persists each through `logger::save` after every completed turn — the same file, under the same id, that `apogee chat --resume` reads. The history is compacted before it overflows the window (the same `compact_history`, the same 80/90 thresholds, now in `agentloop/content` where every surface reaches them), idle sessions leave memory on a TTL swept by a background thread, and the transcript on disk stays.
- [x] **`commands/serve_cmd.h/.cpp`** — `-m`/`--all-backends`, `--bind`/`--port`/`--allow-remote`, `--rag`/`--retriever`/`--rerank`, `--tools`/`--search`, `--preload`, `--ignore-timeout`, `--session-ttl`. `--preload` goes through a new `StatusReporting::preload` capability and `Harness::preload_model`, so only a backend that reports a load state is asked, and a paid provider is never sent a warm-up request in the name of preloading.
- [x] **cpp-httplib `v0.56.0`**, the recorded default, fetched rather than found with every optional integration off — so `serve` adds a header and no TLS stack.
- [x] **Two helpers the surfaces were already duplicating** — `names_a_configured_backend` (and `configured_backend_key`) and `make_built_in_tools` — moved to `commands/helpers` and used by `complete`, `chat`, and `serve`.
- [x] **[`documentation/reference/http-api.md`](../reference/http-api.md)** — the contract for remote-client authors, pinned to the route table by `cli.http_api_conformance` in both directions.
- [x] **49 new test cases** (951 in all), green in both builds.

**The invariant's exception, made structural.** The symbol check used to scan `apogee_core` and the linked binary for `listen`/`accept` and fail on any reference. With a server in the binary that is no longer a statement anything can make — so the check now scans **every library the executable links that the build produces, static or shared**, collected by walking the link graph in CMake so a new in-process dependency is covered without anyone remembering, attributes each reference to the object file (or shared library) that made it (`nm -A -u`), and allow-lists exactly one object by name: `serve.cpp.o`. Every other file under `httpserver/` is as socket-free as the rest of the library, which is also what lets the handlers be tested with no port. Two vacuity guards: an empty archive list fails, and the allow-listed object **must be seen** referencing the symbols — a changed `nm` format or a moved listener fails loudly instead of passing quietly. Verified by planting a `listen()` in `http_types.cpp`: the check failed naming the file; and by handing it a throwaway shared library that calls `listen()`: it failed naming the library. **The walk found a gap the old check had been papering over.** In the llama preset it listed three archives where llama.cpp should have been — because llama.cpp builds as *shared* libraries there, and a shared library's own imports never appear in the executable's undefined-symbol table. The binary scan added in Milestone J to cover in-process llama.cpp therefore never saw inside it; the walk now scans every shared library the build produces, by file, and the llama preset reports ten. The runtime half gained its positive twin: `cli.serve_lifecycle` asserts with `lsof` that `serve` *does* hold a port while `cli.complete_opens_no_listening_socket` asserts `complete` does not.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| HTTP library *(default taken)* | cpp-httplib, **fetched not found** | A system copy is a compiled library built with whatever TLS and compression options its packager chose; the server must not gain a TLS stack on one machine and not another. Plain HTTP, TLS terminated in front. |
| Sessions | **Asked for, never implied** | Never minted for a request lacking an id: a stock OpenAI client re-sends its whole history each call and would leave one transcript file on the server per request. `"session_id": "new"` mints, a known id continues, absent stays stateless — OpenAI semantics exactly. An unknown id is a typed 404, so an evicted session is a signal rather than a silent fresh context. |
| Concurrency | **One turn at a time** | Inference requests serialise behind one mutex; `/health`, `/v1/models` and the session routes answer meanwhile. A local model has one context and the cloud clients were never audited for concurrent use. Per-backend concurrency is a later item; a second request waiting beats a first one corrupted. |
| Vendor-CLI backends | **Refused by config type**, even when built | The Core constraint made visible: a request naming a subscription backend gets a 400 that says why, rather than a silent gap in `/v1/models`. The serve command skips them from the served set and announces it. |
| Client-supplied `tools` | **400**, not ignored | The server runs its own loop and returns only the final answer. A client waiting for `tool_calls` that never come is a worse failure than a clear refusal; an empty `tools` list (what some clients send by default) means nothing and is accepted. |
| `effort` → thinking budgets | **Not accepted** | The IR carries no per-request thinking budget to map it onto. Accepting a field the server cannot honour would be a promise without a product; it waits for the seam. |
| Thinking on a served stream | **Dropped**, recorded as the seam | Reasoning is display metadata and never part of a served response. A client wanting the model's working live would get it as its own meta-frame type; the status vocabulary exists to drive an indicator, not to carry a transcript. |
| `/v1/training/*` stubs, harness bare mode *(defaults taken)* | With the training item; a later flag | A route that returns nothing but 501 is a promise without a product. |
| Errors | The OpenAI envelope everywhere | A stock client library turns `{"error":{"message","type"}}` into its usual typed exception; a plain-text error body does not give it that. |
| `finish_reason` | The provider's, mapped to OpenAI's three words | `length` is what a client acts on when an answer was cut short. Found live — above. |

**Verified live, with a stock client.** `build/llama` serving gpt-oss-20b on loopback, preloaded; the `openai` Python package (3.13.0, from a scratch venv) completed a non-streamed chat (`'OK'`, with real usage: 13 prompt, 30 completion), a streamed chat reassembled from deltas (`'1 2 3 4 5'`), and a two-turn session through `X-Apogee-Session-Id` in which the server remembered a name given one turn earlier (`'noted'`, then `'Ada'`) — unmodified, through `extra_body` for the one extension field. `/v1/models` listed the model and `/v1/model/status` reported it ready. The session was then continued from the terminal with `apogee chat --resume <id>`, which answered `Bye Ada soon`, and SIGTERM stopped the server with exit 0. The mock-backed e2e runs the same shape in CI.

**The live run found a missing word.** The first pass gave the session turns a 32-token budget, and both came back empty — gpt-oss spends its budget in its analysis channel first, the same lesson the rerank judge taught in Milestone S — yet the server reported `finish_reason: "stop"` for both. An empty answer called `stop` tells a client nothing went wrong. The loop had been discarding the provider's finish reason; `RunResult` now carries the final call's, and the server maps it to the OpenAI vocabulary — `length` when the budget ran out, `content_filter`, else `stop` — on every shape (the completion, the final chunk, `/v1/completions`). Two more mutations, both caught.

**Guardrails, each mutation-tested (33 mutations: 32 caught outright, and the one survivor caught once its test was strengthened -- below).** In the stream: a meta-frame on a non-empty delta; meta-frames without the opt-in; `[DONE]` never sent; the wrong finish reason; a provider failure mid-stream not ending the stream; a dead client not cancelling the turn; `model_ready` repeated; an empty chunk ending the stream at the Reporter. In the handler: a session's history not dispatched; a turn never committed; retrieval persisted into the transcript; an unknown session minted instead of refused; the vendor-CLI check dropped; compaction skipped; usage sent as zeros; `tool_mode: none` ignored; `?retriever=` unvalidated; the model list unfiltered. In the rest: eviction reversed; the loop's usage split not accumulated; the finish reason forced to `stop` and dropped by the loop; `preload_model` and the llama `preload` doing nothing; the bind policy permissive and `0.0.0.0` counted as loopback; the serve command serving a vendor CLI; SIGTERM unhandled. And the checks themselves: a `listen()` outside the listener; the symbol check with a wrong allow-list name and with no archives; the API reference with a route missing and a route invented.

**One equivalent mutant, recorded.** "An empty chunk ends the stream" planted in `SseReporter::on_answer_token` survives the handler-level test, because the loop's own `on_token` drops empty pieces before any Reporter sees them — the server path is safe by construction one layer down. The reporter-level test now asserts that no `[DONE]` appears in its sequence, which catches the same mutation at the layer it lives in.

---

## Milestone U — The control plane

**Goal.** The mutating half of `apogee serve`: `/v1/admin/*` behind a bearer, on the listener that already exists — the substrate every later mutating route rides on, built before that surface grows. Auth, a lifecycle bus with one SSE stream, an async-job registry, the first config CRUD slices through the one config-mutation path, and a CLI↔HTTP parity test that is complete rather than maintained.

### 2026-09-13 — `admin-plane`: a gate before routing, bytes that match, and a table that cannot go stale

**The shape it inherited.** Milestone T left a listener-free mux, a transport-neutral `HttpRequest`/`HttpResponse`, and a bind policy. The control plane is a second handler on the same table, and the whole of its security posture is three small things done in the right order: the gate runs *before* routing, the token has no serializable type, and the one route that can carry a secret asks the socket who is calling.

**What was built**

- [x] **`source/events/bus.h/.cpp`** — a **leaf** publish/subscribe bus for lifecycle events: bounded per-subscriber queues (256), non-blocking publish, drop-on-slow, an RAII subscription, and the event catalog (`session.*`, `model.load.*`, `agent.run.*`, `admin.job.*`). Includes nothing from the project, so the session store, the local backend and the job registry all publish without an include cycle, and `harness.layering` now holds `events/` to that.
- [x] **`httpserver/admin_auth`** — the per-install token: 64 hex characters, a sibling of `config.yaml`, generated at first serve, **written `0600` on the temporary file before the rename** (a new `private_mode` on `harness::write_file_atomically`, so a secret is never on disk under the umask's mode, not even between two syscalls), read with `apogee serve --print-admin-token`; `Authorization: Bearer` only, compared in constant time; a query-string token refused even when correct.
- [x] **The gate in the mux, before routing.** The `/v1/admin/` prefix answers `401` unauthenticated whether or not the path exists, so a probe cannot enumerate routes; authenticated, an unknown path is an honest `404` — which is what the parity test reads. The plane is mounted only when the mux is given an `AdminHandler` and a token; a public-only server does not pretend to have one.
- [x] **`httpserver/admin_config`** — the config slice as free functions over a config path, so the byte-identity test calls exactly what the route calls: `POST`/`GET`/`DELETE /v1/admin/backends[/{id}]`, the three role pointers, and `POST /v1/admin/config/format`, each the twin of a `config` subcommand and each **the CLI's own transform** (`append_backend`, `delete_backend`, `set_models_role`, `format_config` under `edit_config_file`). `backend_view` is a type with no `api_key` field. Every write reads the file fresh and reports `restart_required`: true when the file now differs from what the server started with, in membership or role pointers.
- [x] **`httpserver/jobs`** — the async-job registry: `start`/`progress`/`finish`/`fail`/`cancel`, each an `admin.job.*` event, `GET /v1/admin/jobs[/{id}]`, `DELETE` to cancel through the job's `harness::CancellationToken`. **Cancel wins**: `finish` and `fail` are no-ops once a job has left `running`. Ships proven by a scripted job; the first real kinds arrive with the backfills that own them.
- [x] **`httpserver/admin_events`** — `GET /v1/admin/events`: named SSE frames from the bus, a heartbeat every 25 s, and a poll that notices shutdown, so Ctrl-C ends an idle stream within a second rather than hanging the thread pool's join (`httpserver/shutdown` is the atomic a signal handler can set).
- [x] **`apogee check`** gained a `Secrets` section — the token, when present, must be `0600`; `--fix` tightens it — and `serve` a `--print-admin-token`.
- [x] **The reference** ([http-api.md](../reference/http-api.md)) gained the admin plane, pinned by `cli.http_api_conformance`.
- [x] **26 new test cases** (977 in all), green in both builds.

**The parity table is complete, or the build fails — and it fired on the first run.** Not a list a human extends: Apogee's `tests/httpserver/parity_test.cpp` walks the real registry's subcommand tree and requires every leaf to be classified: a twin with its route, a backfill with its owning area, a carve-out with its reason, or read-only. The first run failed with *"unclassified subcommand: config format"* — a mutating command the grooming had not listed — and it got its twin (`POST /v1/admin/config/format`) before the test went green. A row naming a subcommand that no longer exists fails too. Every twin's route must be in the table, flagged as gated, and answer `401` unauthenticated.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| Bind flag | **One**: Milestone T's `--allow-remote` | A non-loopback bind exposes the inference plane already; the operator confirms that once, and the bearer sits on top. A second flag is one more thing to forget with nothing to protect. |
| Cancellation | `harness::CancellationToken`, not `std::stop_token` | One cancellation type threads through every provider call already; a second would need a bridge at each, and the first forgotten bridge is a job that cannot be stopped. |
| The gate | Before routing; `401` for unknown paths too *(default taken)* | An unauthenticated probe must not enumerate routes. |
| Reload | None; `restart_required` on every write *(default taken)* | Rebuilding providers under a live turn is its own problem. Saying so is honest; the definition — "the file differs from what this server started with" — is stateless and also reports drift a CLI edit made while the server ran. |
| `PUT /v1/admin/backends/{id}` | **Not built** | The CLI has no `config edit-backend`; an HTTP-only upsert is the reverse parity gap, HTTP doing what the CLI cannot. `POST` with `force: true` is the `--force` twin. |
| A literal `api_key` in a request | Loopback peers only (`403` otherwise) | The guard document's secret-accepting-route rule, applied to the one route in this item that can carry a secret; `${ENV}` references are not secrets and pass from anywhere. The credential store inherits it. |
| Jobs substrate | Proven by a scripted job | The first real job kinds belong to the embedstore and models backfills; pulling one forward would build half of someone else's route. |
| SSE auth | Bearer-only *(default taken)* | A fetch-style reader sets headers; a ticket parameter is an addition for a later item. |
| Mounting | Always *(default taken)* | Apogee always resolves a config path, so a "no config path → plane disabled" mode has nothing to carry. |
| Delete responses | `200` with a body, not `204` | `restart_required` has to ride somewhere. |

**Verified on the real binary.** `cli.serve_lifecycle` now drives the control plane: the token file is `0600`, an unauthenticated request, a wrong bearer and the right token in a query string are all `401` in the error envelope, an add-backend over HTTP is read back by `config get` and leaves **the same bytes** the CLI's add-backend leaves on the same starting file, and a session minted on the public plane arrives as `session.created` on the event stream — which never carries the token.

**Guardrails, each mutation-tested (31 mutations: 28 caught outright, 2 caught after their tests were strengthened, 1 equivalent -- below).** The gate accepting any bearer, a query-string token, an empty token, or running after routing; the token written under the umask's mode; the HTTP edit formatting its own entry; the view leaking the key; a literal key accepted from any peer; a `${ENV}` reference treated as literal; a collision not a `409`; a dangling role pointer accepted; `restart_required` blind to role drift or to membership; `fail` overwriting `cancelled`; `finish` overwriting `failed`; cancel not firing the token; cancel not idempotent; an unbounded queue; unsubscribe not closing; unnamed event frames; no heartbeat; a stream deaf to shutdown or to a departed client; a twin route removed; an admin row not flagged; the doctor passing a world-readable token; `--fix` leaving it; `session.created` never published; the events package including the server; and the reference missing a route.

**Two survivors, and what they taught.** "Cancel is not idempotent" survived because the test re-cancelled an already-cancelled job, which a re-cancelling mutant cannot be told apart from; it now cancels a *succeeded* job and requires it to stay succeeded with nothing emitted. "The client leaving does not end the stream" survived because the heartbeat path ends the stream too, twenty-five seconds later; a test now hangs up *on* an event write and requires the next event never to be sent. Both caught after.

**One equivalent mutant, recorded.** "The compare stops at the first differing byte" survives: constant-time-ness is a property of *how long* a comparison takes, and no result-based test can see it. The function is six lines beside a comment saying why it is written that way; the review is the guard.

**A harness lesson, the third of its kind.** The "no heartbeat" mutant did not fail the events test -- it *hung* it, because the test's fake client kept reading until a heartbeat arrived, and the harness sat on ctest's 25-minute default timeout looking finished. A test that waits for the thing under test to happen is a test that hangs when it does not; every streaming client in these tests now carries a deadline, a missing heartbeat fails in a second, and the harness runs ctest with a per-test timeout. The first run's harness also died decoding a test's output as UTF-8 after a byte-diff assertion; it now decodes leniently.

### 2026-09-13 — `provider-credential-store`: one key, one chain, and a type that cannot carry it

**The shape it inherited.** Apogee calls three vendors' APIs directly, so it owns the keys rather than handing auth to a vendor's CLI. Until today a key lived in `config.yaml` (`api_key: "${OPENAI_API_KEY}"`, expanded at load) or nowhere, and each cloud provider read `config.api_key` for itself: three readers of one field, and the day one of them grew a fallback the other two would not have it. The admin plane had already put the pieces on the table — the `0600` private-mode write, the loopback-peer rule for a literal key, the doctor's `Secrets` section, the parity table — so this item is mostly the discipline of using them once.

**What was built**

- [x] **`source/secrets/store.h/.cpp`** — `CredentialStore` over a `0600` `credentials.json` beside the config (following `--config`, exactly where the admin token lives), one slot per API-billing provider *type* — `anthropic`, `openai`, `google` — holding the key and when it was stored. The type that holds a key is private to the `.cpp`; the header hands out `CredentialMetadata`, which structurally has no key field, and **exactly one function returns a key** (`key_for`), called by the resolver and by nothing that renders. A store that cannot be read degrades to empty with a `warning()` naming the file — and `put`/`clear` refuse to overwrite it, because whatever is in a corrupt store may be someone's only copy.
- [x] **`source/secrets/resolve.h/.cpp`** — **the one key resolver**, the sibling of `harness/roles.h` for the same reason: the entry's `api_key` (already `${ENV}`-expanded) > the stored slot for its type > the conventional variable (`ANTHROPIC_API_KEY`; `OPENAI_API_KEY`; `GEMINI_API_KEY` then `GOOGLE_API_KEY`), from an `EnvSnapshot` **captured once**, so a token one path injects into the environment cannot change what another path resolves mid-run. `KeyResolution` reports which rung answered and which variable, so every surface can say *where* without ever saying *what*. Vendor-CLI types take no key at all: `slot_type("claude-cli")` is nullopt, and asking is refused with the principle.
- [x] **The factory resolves; the providers receive.** `anthropic`/`openai`/`google::from_config` now take the key as a parameter and read no config field; `backends/factory.cpp` resolves through the chain (a store beside `BuildOptions::config_path`, the injected or process-wide snapshot) and hands it in. The three wire fixtures that already asserted the auth header prove the retrofit without learning anything new; `complete`, `chat`, `serve` and `embed` pass the config path so the store is consulted everywhere a backend is built.
- [x] **`apogee auth add|list|clear <provider>`** (`commands/auth_cmd`): `add` takes the key through a hidden prompt — a new `platform::read_hidden_line`, echo off via termios / `SetConsoleMode`, restored on every path, plain `getline` on a pipe — or `--stdin`, or `--from-env` (copying the conventional variable into the store); **never on the command line**, which lands in shell history and process listings, and the parser refuses a positional. `list` is metadata plus, per configured backend, which rung answers.
- [x] **`apogee check`** reports each cloud backend's key *source* — "from config", "from the credential store", "from `GOOGLE_API_KEY`" — or warns "no API key found" with `apogee auth add <type>` first among the remedies; a `Credential store` row (absent is fine; present must be `0600`, `skipped` on Windows; corrupt is a warning naming the file); `--fix` tightens it as it does the token. **`apogee models list`** carries the same answer in a cloud row's state column — `key: store`, `key: OPENAI_API_KEY`, `no key` — from the same resolver.
- [x] **`httpserver/admin_auth_routes`** — `GET /v1/admin/auth` (metadata plus the backends' sources), `PUT /v1/admin/auth/{id}` (the `auth add` twin: key **in the body only**, served to **loopback peers only** — judged from `remote_address` **before the body is read**, so a remote caller learns nothing about its shape — `403` otherwise, whatever the bind), `DELETE /v1/admin/auth/{id}` (the `auth clear` twin, bearer-gated only: it accepts no secret and mints none). Three mux rows, three headings in [http-api.md](../reference/http-api.md), three parity rows (`auth add`/`clear` twins, `auth list` read-only).
- [x] **`cli.one_key_resolver`** (`tests/one_key_resolver.cmake`) — fails any source outside the resolver and an allow-list of readers-with-reasons that reads an entry's `api_key` or names a conventional variable; **`harness.layering`** now holds `secrets/` to including only `harness/` and itself. `cli.no_vendor_credentials` gained its one exemption, with the reason in the file: Apogee's own store is named `credentials.json`, and the single line that names it — the constant in `secrets/store.h` — is skipped for that one pattern, every other file still held.
- [x] **The e2e suites scrub the four variables** before they start: the third rung means a developer's real `ANTHROPIC_API_KEY` would have turned a "no key" expectation into a live cloud backend. The factory and doctor unit tests inject an empty snapshot for the same reason.
- [x] **16 new test cases** (993 in all), green in both builds.

**The leak test is the acceptance criterion as one assertion.** `tests/secrets/leak_test.cpp` puts one distinctive key into *every* rung at once — a config entry, a stored slot, the snapshot — then renders every surface that reports on keys and searches each for it: `auth list`, every doctor row and the rendered report, every admin response and the backend view, every build reason, everything the bus published, everything the operational log wrote. The surfaces are the complete set; a new one is added there. `cli.serve_lifecycle` does the same on the real binary: a key stored with `auth add --stdin` is attributed by `check`, listed as metadata over HTTP, and then grepped for in every file the run wrote and every response it received — found only in the `0600` store.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| Backing store | **A private `0600` file beside `config.yaml`** *(user)* | Identical on all six targets, the same place as the admin token, behind one `CredentialStore` interface an OS keychain could sit behind later. Nothing queued for a keychain until asked. |
| `api_key:` in config | **Stays supported, and stays first** *(user)* | A `${ENV_VAR}` reference there remains the documented, portable way, and lets two entries for one vendor carry different keys; the store is for keys kept out of config and environment entirely. `check` does not nudge. |
| Git-forge slots | **Not here** *(user)* | Nothing in Apogee talks to a forge yet; they ride the forge-target decision made when the agents item is groomed. |
| Slots keyed by | Provider **type**, not backend entry | Two `openai` entries with different keys is what a per-entry `api_key` is for; the store answers "the key for this vendor", the case a user actually has. |
| Where resolution runs | The factory, once | The providers take a key and stay unaware of stores, snapshots and precedence; the wire fixtures prove the retrofit. |
| Key fragment in metadata | **None** *(default taken)* | A fragment is a partial secret; `stored_at` plus the source in effect is enough. |
| Google's variable | `GEMINI_API_KEY` then `GOOGLE_API_KEY` *(default taken)* | The Gemini SDKs read `GEMINI_API_KEY` first; both honoured, in that order. |
| `auth clear` | Bearer-only *(default taken)* | It accepts no secret and mints none; a remote operator may revoke. |
| The store's file name | `credentials.json`, with one narrow guard exemption | The name the item pinned; `cli.no_vendor_credentials` forbids the literal because vendors use it, so the one constant that names Apogee's own file is exempted for that one pattern, and the rest of the tree — `store.cpp` included — is still held to it. |

**Verified on the real binary.** `cli.serve_lifecycle` now also: stores a key with `auth add --stdin` and finds the file `0600`; `auth list` shows the slot; a keyless `openai` entry is attributed to the store by `check`; `GET /v1/admin/auth` returns metadata (`401` without the bearer); a `PUT` from loopback is accepted, a vendor-CLI slot is `400`, `DELETE` clears and then `404`s; `auth add claude-cli` is refused naming the principle and `auth add openai <key>` is refused outright; and the secret appears in no output, response, event or log.

**Guardrails, each mutation-tested (31 mutations, all 31 caught outright).** The store written under the umask's mode; `put` clobbering a corrupt store; `key_for` answering for any provider; metadata carrying the key; a config key losing to the store; the store rung skipped; `GOOGLE_API_KEY` consulted before `GEMINI_API_KEY`; a vendor CLI taking a key; the snapshot reading the live environment; the no-key message naming the wrong variable; the factory ignoring the store, or reading the process environment despite an injected snapshot; `PUT` served to any peer, or judging the peer from a forwarded header, or echoing the key, or accepting an empty one; the listing carrying the key; a vendor-CLI slot accepted by the route; `DELETE` answering `200` on an empty slot; the `PUT` row ungated; the `DELETE` row removed; the models column carrying the key; the doctor printing the key, passing a world-readable store, `--fix` skipping it, or reporting the store rung as config; `auth add` refusing a vendor CLI without the principle; a second chain naming a variable in a command; `secrets/` including a surface; a vendor credentials file named outside `store.h`; and the reference losing a credential heading. No survivors and nothing equivalent this time — the surfaces are small enough that every mutant changes an output some test reads.

**A lesson, from the tests this time.** Two new doctor tests read a row through a pointer into the *temporary* report `run_checks()` returned — the report died at the end of the statement and the assertions read freed memory, failing in ways that looked like the feature was wrong. `tests/commands/models_test.cpp` had already met this bug and deletes its helper's rvalue overload so the call cannot be written; the doctor's helper now does the same.

## Milestone V — The native toolsets

**Goal.** The model's hands: five in-process C++ toolsets in the one registry every surface builds, and the permission gate's first real consumer — the `permissions:` schema, the prompt on every surface that has one, and the rule that a surface with nobody to ask denies.

### 2026-09-13 — `native-toolsets`: five toolsets, one gate, and a shell that asks first

**The shape it inherited.** Milestone F left a tool registry, a dispatch chain and a permission gate, with exactly one tool in them (`fetch_url`) and nothing declaring `writes`. The gate already resolved "ask with nobody to ask" to deny; what it lacked was anything to ask about, a config to read levels from, and a prompt on any surface.

**What was built**

- [x] **`source/tools/`** — a guarded package (never `backends/` or `commands/`; `harness.layering` holds it): **fs** (`read_file` with a 64 KiB cap and its note, `write_file`, `delete_file`, `list_directory`, `search_files` skipping hidden directories) inside a root that is resolved through symlinks and required to be an ancestor **by path components** — a `startswith` sandbox would let `/home/user` admit `/home/userX`; **shell** (`run_command` through `/bin/sh -c` with the directory and command as positional parameters that are cleared before the command runs, `[stderr]`/`[exit N]` trailers, a timeout that is a result); **git** (`git_status`, `git_log`, `git_diff`, `git_show` as `git` children with list arguments and a ref allow-list that starts with a letter or digit, so nothing reaches git as an option; `git_diff`'s review form `base...head` with fetch-on-demand and `ReviewDefaults` for the flags the agents item will set); **notes** (a scratchpad under a new `notes/` layout row, keys that can never be paths); **rag** (`search_documents`, `list_collections`, `collection_info` over the store, the retriever decided by the same `resolve_turn_retriever` the turn uses and named on every hit). `register_native_toolsets` is the one entry point; `destructive_tool_names` is the list the template, the doctor and the admin view all read.
- [x] **The `permissions:` schema** — one level per **tool name**, `ask`/`allow`/`deny`, unlisted means `ask`, a bad level refused at load; a `tools:` section (`fs_root`, `disabled`); the shipped template listing the five destructive tools at `ask`; `set_permission` as the config editor's transform (the shared body of `set_models_role`, now `set_section_scalar`), and `config set-permission` / `PUT /v1/admin/permissions/{id}` as its two thin callers, byte-identical.
- [x] **The gate on every surface** (`commands/permissions`): `make_permission_checker` — the config's level, then the session's `session` answers, then `Ask`; `terminal_confirm_fn` — `[y]es / [n]o / [a]lways / [s]ession` through the status line, `always` written through the editor, **null where there is no terminal** so a pipe denies; `make_driver_confirm_fn` — machine mode's `question` with `"kind":"permission"`, answered by one `answer` line; `serve` carries the checker only, so `allow` in the config is the sole way a destructive tool runs there. `chat`, `complete` and `serve` wire it through `ToolGate`.
- [x] **`apogee check`** gained a `Tools` section: every destructive tool's level (a misspelt key warned, an `mcp__` key accepted for the client to come), the effective `fs_root`, the toolset switches, and `git` on `PATH` — warnings only.
- [x] **The references**: [machine-mode.md](../reference/machine-mode.md) (the permission question) and [http-api.md](../reference/http-api.md) (the two routes), each pinned by its conformance check.
- [x] **32 new test cases** (1025 in all), green in both builds; `cli.complete_lifecycle` reads the doctor's section and round-trips `set-permission`.

**The sandbox is a table.** `tests/tools/fs_test.cpp` tries every spelling of an escape — `..`, an absolute path outside, a symlink pointing out, and the `rootX` prefix trick — through the resolver and through each tool, and requires the refusal to name the root and the outside file to be untouched. The gate is proven where it runs: `tests/tools/gate_test.cpp` drives the shared loop with a scripted model against the real registry, and `tests/httpserver/handler_test.cpp` does the same through a served request.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| Subprocess or in-process | **In-process** | No Python runtime, no JSON-RPC hop for a file read, the embedder reached directly; the MCP client is for third-party servers. |
| The shell | **Gated**, default `ask` | A docstring warning is not a gate; a tool that can `rm -rf` is what the gate is for. `allow` is one config line. |
| Git | Shell out *(default taken)* | libgit2 is a dependency on six targets and a second ref resolver, for nothing the user's `git` lacks. |
| Permissions keyed by | **Tool name** | A two-field struct would need a new field per gated tool; a namespaced MCP tool fits the same key. |
| `fs_root` default | The home directory *(default taken)* | A chat started from `/` would otherwise sandbox nothing. **Reversed 2026-09-25** (the user's call): the folder Apogee was started in — see *Tool safety defaults* below. |
| The machine-mode prompt | The existing `question` event *(default taken)* | A `kind` field, no new channel; the "advertised iff someone can answer" rule already covers the no-driver case. |
| `always` | Per tool, not per target *(default taken)* | A per-path allow-list is a larger schema for a case `session` covers. |
| `git_diff` with a fetch | Read-only for the gate *(default taken)* | It updates remote-tracking refs, never the tree or history; prompting on every review diff trains the reflex the gate avoids. |
| The shipped `permissions:` block | **Active**, five tools at `ask` | `ask` is the default either way; an active block means `always` changes exactly one line rather than appending a section. |
| Windows shell | `cmd /C` *(default taken)* | A recorded per-platform difference, not a skip. |
| The shell's positionals | Cleared before `eval` | Found by the test: with the script's `$1`/`$2` still set, a command reading `$1` saw the working directory. `set --` first. |

**Verified on the real binary.** The doctor's `Tools` section on a fresh config, `config set-permission` round-tripping through `config get` and refused for a level outside the three (a parser exit, before the file is touched), and `complete --tools` on a pipe with the whole native set registered and nothing prompting.

**Guardrails, each mutation-tested (36 mutations: 35 caught outright, 1 caught after its test was strengthened).** The sandbox as a string prefix, or gone; `write_file`, `delete_file` and the shell ungated; hidden directories searched; the read cap silent; the shell's positionals visible to the command; a timeout reported as an ordinary exit; a ref allowed to start with a dash; the file argument reaching git unchecked; `never` fetching anyway; `auto` never fetching; the review defaults ignored; a note key holding a slash; the resolver's error ignored; hits not naming their retriever; a disabled toolset registered anyway; `run_command` dropped from the destructive list; a config `deny` reading as allow; session answers forgotten; `always` not written; an unknown answer allowing; the terminal prompt existing on a pipe; a closed driver denying instead of failing the turn; `tools.disabled` ignored by the registry; the served checker dropped; ask with nobody to ask allowing; the loader accepting `yes` as a level; `set_permission` accepting any level, or a path as a tool name; the doctor blind to a misspelt key; `restart_required` always false; the `PUT` permissions row ungated; `notes/` not a layout row; and `tools/` including a backend.

**The one survivor, and what it taught.** "`never` fetches anyway" survived because the test's `never` case asked for a ref that existed *nowhere*, so the real code and the mutant refused it with the same message. The case now names a branch only the remote has: the real code refuses, a mutant that quietly fetched would succeed. A refusal test has to use something that *could* have been found.

### 2026-09-25 — `tool-safety-defaults` (backlog item 25a): ask before a new website, work in the launch folder

**Why it came first.** The local-tools spike (2026-09-25) found an exposure that already existed. `read_file` and `fetch_url` were both read-only to the gate, so neither ever asked, and the file tools reached the whole home directory. A model with `--tools` could read a file and send its contents out inside a URL with no prompt at any point, and a web page carrying hidden instructions was enough to set that off. Every cloud backend run with `--tools` had it. [Local tool calling](#milestone-j--local-inference) would hand it to local models, and [web search](#2026-09-28--web-search-searxng-backlog-item-25e-search-through-the-users-own-searxng) would multiply the untrusted pages a model reads. So this went first in the local-agent-tools track.

**What was built**

- [x] **Outbound, as its own kind of gated tool** (`agent/tool.h`). `Tool::outbound` sits beside `writes`. The gate is asked a `GateRequest`: the tool, the target it decides on, a `detail` it shows but never decides on, and whether the tool is outbound. An outbound tool must name a target (`ToolRegistry::add` refuses one that cannot), and a call it names none for is refused unrun. `run_gated` hands a running tool a `TargetGate`, so each further target goes through the same checker and the same prompt.
- [x] **`fetch_url`, outbound** (`agent/fetch_url`). The target is the URL's host and the detail is the whole URL. `parse_http_url` is the one URL parser: userinfo, a backslash or a percent escape in the authority, a host that is not a bare host name, and a port outside 1–65535 are refused, and path bytes that are not printable ASCII are percent-encoded. **The URL fetched is rebuilt from those parts**, so the host asked about is the host the transport reaches. Redirects are followed one hop at a time (`resolve_redirect`, at most ten). A hop to a new host goes through the gate before anything is fetched from it, a hop on the same host does not ask again, and a redirect to any other scheme is refused. A redirected page's text starts with `[X redirected to Y]`.
- [x] **The fetcher** (`commands/helpers`, `make_http_fetcher`): one GET with `follow_redirects = false` and `max_body_bytes` at 5 MB, both new on `HttpRequest`. `HttpResponse` gained `location` and `body_limit_exceeded`. Stopping at the cap is reported, not failed, so it is never retried. `CurlTransport`'s header callback now resets at each status line, so only the last response's headers count.
- [x] **One definition of a host** (`harness/host.h`). `canonical_host` lowercases, drops one trailing dot and IPv6 brackets, and refuses anything else. `host_listed` compares in that form, **exactly**. The config, the checker, the URL parser, `check` and the editor all ask it.
- [x] **Answers per website** (`commands/permissions`). For an outbound call the checker asks, in order: is the host in `tools.allowed_hosts`, has this session allowed it, else ask. `permissions.fetch_url` is not a key: `check` warns that it does nothing and names the list. `SessionApprovals` keeps tools and hosts apart, so `session` allows that one website. `always` adds the host through the editor. The terminal prompt shows the whole URL under the host (`Allow reaching this website?`), and machine mode's permission question gains `outbound` and `detail`.
- [x] **`tools.allowed_hosts`**. The template's `tools:` section is active now, holding `allowed_hosts: []` with the rule in its comment, so `always` changes exactly one line. `add_allowed_host`/`remove_allowed_host` are exact inverses over flow and block lists, a missing key or section, and CRLF. `config add-allowed-host` and `delete-allowed-host` (the latter completing to the listed hosts, a new `ALLOWED_HOST` kind) have admin twins: `GET`, `PUT` and `DELETE /v1/admin/allowed-hosts[/{id}]`.
- [x] **The launch folder** (`tools/toolsets`, `effective_fs_root`). `tools.fs_root` unset now means the folder Apogee was started in, not the home directory. A refusal names the root and both ways to widen it: start in a folder that contains the file, or set `tools.fs_root`.
- [x] **`check`** reports the allowed hosts (an entry that is not a host is a warning, never a load failure), and the file root with where it came from.
- [x] **The references**: [http-api.md](../reference/http-api.md) (the three routes, and the `serve --tools` behaviour change) and [machine-mode.md](../reference/machine-mode.md) (the per-website question). Each is pinned by its conformance check.
- [x] **21 new test cases** (1608 in all), and `cli.config_lifecycle` drives the real binary.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| Ask when | **Per new website** (the user's call) | Over asking only once a file has been read, an allow-list with no prompt, or no guard. A known site stays fast; a new one is always a visible choice. |
| The file root | **The launch folder** (the user's call) | A chat started in a project works in that project; `tools.fs_root` still widens it. |
| How `fetch_url` is gated | A new `Tool::outbound` flag *(default taken)* | Outbound is a different risk from mutation. A `read-only` agent (Milestone X's policy, which drops `writes` tools) keeps `fetch_url`, gated per website, so with nobody to ask it reaches only the listed hosts and still never blocks. |
| `always` and `session` | The host into `tools.allowed_hosts`; the host for this process *(default taken)* | What the two answers already mean for a tool, applied to a host. |
| Nobody to ask | Only `tools.allowed_hosts` *(default taken)* | `serve --tools` fetched anything before; the change is recorded in http-api.md. |
| The search provider's host | Trusted by configuration *(default taken; built in 25e, below: the checker counts the instance's host as listed)* | The user named it. The pages a search returns are ordinary fetches and ask. |
| `chat --resume` | The folder it is resumed in *(default taken)* | The root is a property of the process, not the transcript. It falls out: nothing in Apogee changes directory after startup. |
| `permissions.fetch_url` | Not a key | A tool-level `allow` would reopen every website at once, which is what per-website asking exists to prevent. |
| The URL handed to the transport | Rebuilt from the parsed parts | Two URL parsers that disagree about a host are the classic way round a host check; here the second parser has nothing ambiguous to read. |
| A redirect on the same host | Not asked again | It was just allowed; asking again teaches the reflex the gate avoids. |
| Redirect limit | Ten | It bounds a loop, not the guard: every new host is asked about anyway. |
| Download cap | 5 MB, refused naming the size | 25a's seam named a cap and 25f had recorded the size, so it was taken here and marked consumed there ([25f](#2026-09-28--fetch-url-reader-backlog-item-25f-fetch_url-reads-a-page-as-a-model-should) shipped the reader). |
| Where the host rule lives | `harness/host.h` | `agent/`, `commands/`, `httpserver/` and the editor all need it, and `harness/` is the one layer every one of them may include. |
| A pasted URL in the list | Loads, and `check` warns | Refusing the whole config over one entry would take every other command down with it. |

**Verified on the real binary.** `cli.config_lifecycle` covers the full sequence. On a pipe the fetch is refused, and the model reads the refusal as a tool result. A driver's `always` in machine mode names the host in the question and adds exactly `[127.0.0.1]` to the config. The same pipe run then reaches the host, and `check` lists both rows. `delete-allowed-host` restores the file's bytes. By hand, against a local server: a redirect to a different host was refused, and the server never saw the request; a same-host redirect was followed; a 6 MB page was refused, naming the limit, and requested once. Under a pseudo-terminal, the prompt showed the host and the whole URL, and `a` wrote the host to the config.

**Guardrails, each mutation-tested (34 mutants: 31 caught outright, 2 caught once their tests were strengthened, 1 equivalent).**
- **The host rule:** a suffix match in place of equality; a trailing dot kept; an empty label allowed.
- **`fetch_url`:** a redirect hop not gated; the model's own URL fetched rather than the rebuilt one; the redirect bound off by one; control bytes passed raw; other schemes followed.
- **The gate:** outbound not gated; an outbound call with no target allowed, at the gate and at dispatch; the hop gate always answering yes; an outbound tool without a target reader registered.
- **The checker and answers:** the allow-list ignored; session hosts keyed by tool; outbound decided by the tool's level; a non-host asked about rather than denied; `session` remembering the tool; `always` writing a tool level.
- **The fetcher:** curl following redirects itself; the body uncapped; an over-cap body read as a page; `Location` dropped.
- **The editor:** a duplicate host added; the host written as typed; a missing key placed inside a nested list; a URL accepted as a host.
- **The rest:** the unset root not the launch folder; machine mode's `outbound` dropped; the admin `PUT` never needing a restart; an unlisted `DELETE` not a 404.

**The survivors, and what they taught.**
- **The same host asked again.** A same-host hop that asked a second time was invisible, because the test's first host was on the allow-list, so the checker never reached the prompt. The case now allows the host by an answer.
- **The first-item removal.** Removing the first of two hosts while leaving its separator went unseen, because the only first-item removal was followed by a second one, which tidied the stray comma. The case now checks the single removal's bytes.

Both show the same gap: a check that another step can quietly repair proves nothing about the step before it.

**The equivalent mutant.** Dropping `@` from the authority check changes nothing observable, because `canonical_host` refuses `@` as well. It stays as a second layer, stated in the code rather than relied on alone.

**One incident, recorded.** The branch's commit of this work (`72c2d75`) was taken while the mutation run had the "allow-list ignored" mutant applied, so that commit's checker never allowed a listed host (it failed closed). The working tree was restored by the run, and the next commit carries the correct line.

**Not verified.** The transport's new options (`follow_redirects`, `max_body_bytes`, the `Location` header) were exercised only on macOS's curl; the Linux and Windows builds set the same curl options. `apogee check`'s rows are asserted by the end-to-end test, not by a `check_test` case.

### 2026-09-28 — `local-tool-ergonomics` (backlog item 25d): tools that return less and ask for less

**Why.** The native tools were built for cloud models, with large windows and fast prompt reading. The local-tools spike (2026-09-25) measured the two costs that dominate on a local model instead: every byte a tool returns is read at roughly 100 tokens/s on a 27B model before the model can act on it, and a model that has to rewrite a whole file to change one line spends generation, at 10–15 tokens/s, on text it is not changing. So the tools return less, and ask for less. Every backend gets the change, since the tools are shared.

**What was built**

- [x] **`run_command`'s output, capped** (`tools/shell`, `tools/process`). The runner keeps each stream's first and last 8 KiB (`CapturedOutput`, bounded by an `OutputLimit`), so a command that prints gigabytes costs the kept bytes and a count. `git` keeps its last 256 KiB, as before. `render_command_output` cuts the rendered result — stdout, `[stderr]`, `[exit N]` — to its first and last 8 KiB, joined by a line naming exactly how many bytes were left out and how to see them: redirect to a file and read it in ranges, or search it. Each seam falls at a line break when one is in the kept half, and otherwise between characters, never through one. A 5.7 MB command returns under 17 KiB.
- [x] **`read_file` in ranges** (`tools/fs`). `offset` and `limit`, in lines. Each line comes back prefixed with its number and a tab, `cat -n`'s shape, and a footer says `[lines A-B of T. Read on with offset B+1]`. A range is exact at both edges of the file. An offset past the end is an error naming the file's length. A range stops on a whole line at the 64 KiB cap and says so. The file is streamed, never held. A read without a range is unnumbered, byte for byte.
- [x] **A plain read past the cap returns the file's size, not its first 64 KiB.** It names the size and line count and points at ranges and `grep_files`. This was decided on real weights; see below.
- [x] **`edit_file`** replaces an exact string. It refuses when the string is absent, or when it occurs more than once without `replace_all`, saying how many times it occurs. Either refusal leaves the file untouched. The result names the lines it changed. The file is written beside itself and renamed into place (`write_file_atomically`), with its mode carried over, so an edited script stays executable. When the file has Windows line endings and the given text has none, the text still matches, and the replacement is written with the file's line endings. It declares `writes`, joins `destructive_tool_names()`, and the shipped template lists it at `ask`, so `check`, the admin view and machine mode's permission question all know it.
- [x] **`grep_files`** searches file contents with an ECMAScript regular expression under the file root. Each match comes back as `path:line: text`, the path relative to the root, the files in sorted order. It takes a folder or one file, a `glob` and `ignore_case`. It skips hidden folders and binary files (a NUL byte in the first 8 KiB), and says how many binary files it skipped. A linked file or folder is judged by where it points, like any path. It returns at most 100 matches and 16 KiB, stops at the first match that does not fit (so what comes back has no gaps), and says how many more there were.
- [x] **The environment note** (`tools/environment`, `agent::ToolRegistry`, `agentloop/loop`). It gives the date and weekday, the time zone's name and UTC offset (`platform::local_date`), the system, the working folder and the shell, and the file root. `register_native_toolsets` sets it on the registry it fills, even with every toolset switched off. `apply_tool_policy` carries it onto an agent's filtered registry. The loop renders it once per turn and puts it first in the request's transient block, ahead of any retrieval. It is never in history. It never gives the time of day.
- [x] **The system messages opening a conversation reach a local template as one** (`backends/llamacpp`, `prompt_messages`). Qwen3.5 and 3.8's templates raise "System message must be at the beginning" on a second system message, and a failed render dropped the model to the fallback template, and its tools with it. The note ahead of a chat's own system prompt is the ordinary case. A retrieval block or review note ahead of one was already a case, so this fixes a latent bug too. They are joined a blank line apart, as the Anthropic and Google wires already join theirs.
- [x] **Descriptions state the limits**: `read_file`'s cap and ranges, `run_command`'s kept ends, `grep_files`' caps and what it skips, `edit_file`'s rule of one exact match.
- [x] **The references**: [machine-mode.md](../reference/machine-mode.md) and [http-api.md](../reference/http-api.md) name `edit_file` among the gated tools.
- [x] **15 new test cases**: 1,681 pass under `make test`, and 1,646 in the unit suite built without llama.cpp, which is what CI gates on.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| What `run_command` keeps | The first and last 8 KiB *(default taken)* | Enough for a compiler's first error and a test runner's summary; stated in the description. |
| What the cap measures | The whole rendered result, not each stream | A total bound. Capping each stream alone would let two long ones cost 32 KiB. The tail always ends in the exit status. |
| `grep_files`' caps | 100 matches and 16 KiB *(default taken)* | Past either, it says how many more there were and to narrow the search. |
| Line numbers | Only when a range is asked for *(default taken)* | A plain read stays byte for byte, for a model about to quote or edit it. |
| The environment note | On whenever tools are *(default taken)* | About ninety tokens a turn, and a date a model cannot otherwise know. |
| The note's time | **The date, never the time of day** | The note heads every request. A line that changed each turn would make every local turn re-read the whole conversation, since the KV cache and 25c's checkpoints both match from the first token. A model that needs the time can run `date`. |
| A plain read past 64 KiB | **None of it, with its size and line count** *(decided on real weights, 2026-09-28)* | The item kept the cap, and it stays. What changed is what comes back past it. The first 64 KiB of an 80 KB file was 38,502 tokens, more than a 32K window, so the turn ended before the model could act. Refused, Qwen3.8-27B searched and read a range instead. Claude Code's Read tool refuses the same way. A cloud model pays one more step on a big file. |
| Where the note is set | On the registry, by `register_native_toolsets` | Built once, where the tools are. Every surface builds its registry through `make_built_in_tools`, so no surface can have tools without the note. |
| Joining system messages | On the local path only | The cloud wires already lift and join them, and OpenAI's API accepts several. The restriction is the local template's. |
| `grep_files`' paths | Relative to the root | What `read_file` and `edit_file` resolve against. `search_files` gives paths relative to the folder it searched; that difference is recorded, not changed. |
| The regular-expression engine | `std::regex`, ECMAScript | No new dependency. A line is searched in its first 4 KiB, because the engine recurses per character and a minified megabyte line would overflow the stack. |
| `edit_file` on a missing file | Refused, naming `write_file` | One tool creates files and one changes them. |

**Verified on real weights.** Two models, each with a 32K context: Qwen3.8-27B, a hybrid, and Qwen3-VL-8B. Every run used `complete --tools` at temperature 0 on the real binary, with `edit_file` allowed, in a folder holding a 2,000-line `settings.py` and a small Python package:

| Task | Qwen3.8-27B | Qwen3-VL-8B |
|---|---|---|
| "What is today's date?" | "Monday, September 28, 2026 (MDT, UTC-06:00)", 29 s | "Monday, 2026-09-28", 7 s |
| Change `TIMEOUT_SECONDS` from 30 to 45 | read refused → `grep_files` → a ranged read → `edit_file` → `grep_files` to check; 58 s | read refused → `grep_files` → `edit_file`; 12 s |
| …the file afterwards | Line 1234 changed; the other 1,999 byte-identical | The same |
| Where is `compute_checksum` defined? | One `grep_files` call, line 46; 37 s | One `grep_files` call, line 46; 9 s |

Also checked:
- On Qwen3.8, a system prompt and the note together rendered with the tools; nothing fell back.
- A six-turn hybrid chat carrying the note read 39–171 new tokens per later turn, so the note leaves the cache intact.
- `check` on a fresh config lists `permissions.edit_file ask`.

**What real weights caught that the suite had not.**
- **An invalid schema.** `grep_files`' parameter schema carried `int\s+main` in an example, and `\s` is not a JSON escape. The whole tool list then failed to render, and Qwen3.8 answered without tools (with the right date, from the note). A test now requires every built-in tool's parameters to parse as an object schema whose required properties are declared.
- **The plain-read overflow** above.
- **A phrasing trap, not a tool fault.** Asked where "the function `compute_checksum`" is defined, the 8B looked for a tool of that name. Qwen's template calls tools "functions". Asked where in the project's code it is defined, it found it in one call.

**Guardrails, each mutation-tested (40 mutants).** 36 were caught outright, 2 once their test was strengthened, and 2 after mistakes in the run itself were fixed. The mutants, by area:
- **The cap:** the capture's head never filling, or its tail never dropping bytes; the shell keeping no head; the output never cut; a gap left out of the count; the head not ended at a line; either cut going through a character.
- **The ranges:** starting a line late; one line too many; "read on" offered at the end; a range past the end not refused; the byte cap ignored; a plain read past the cap returning its first 64 KiB again.
- **`edit_file`:** an ambiguous match replaced; `replace_all` ignored; an absent match not refused; whitespace trimmed from the match; the tool ungated; the file's mode lost; the CRLF fallback gone.
- **`grep_files`:** hidden folders walked; binary files read; a link out of the root followed; either cap removed; gaps in the shown matches after the cap; the schema's original `\s`.
- **The rest:** `edit_file` dropped from the gated list or the template; the note never set, missing from the request, written into history, or lost by an agent's policy; the offset's sign flipped; the shell line shown with the shell off; the month counted from zero; the offset dropped; the system messages never joined, or an empty one kept.

**The survivors, and what they taught.**
- **The two character cuts.** The test shifted its text with a leading prefix. The head's cut lands at the stream's own head, where the runner stops. The tail is measured from the end, so its alignment against the characters never moved with that shift. The test now shifts both ends, with and without a gap, so each cut meets every alignment. A test that sweeps an input has to sweep the thing the cut is actually measured against.
- **The two mistakes in the run.** The run filtered tests by tag, and the read-only-policy test is tagged `[permissions]` where the filter said `[permission]`, so it never ran. The schema mutant had a doubled backslash, which is valid JSON. Rerun against the whole suite with the original spelling, both were caught.

**Not verified.**
- **Windows.** Its paths (`cmd`, `localtime_s` and `_mkgmtime`, the zone's long name) are built by CI only.
- **The 5 MB acceptance** is proven by a test that runs a real `awk` through the real shell. No model was asked to run one, since `run_command` asks first.
- **Two tools stay uncapped.** `search_files` and `list_directory` were outside the item.
- **A file under 64 KiB can still fill a small window.** Sizing what is sent to the real window became [26c](#milestone-f--the-shared-agent-loop)'s work, shipped 2026-09-28: a finished turn's results are sent as stubs.

### 2026-09-28 — `web-search-searxng` (backlog item 25e): search through the user's own SearXNG

**Why.** Local models have no provider-side search. With tools they could read a URL they were given, but they could not find one. On 2026-08-26 search had been left to the providers' own server-side tools, because a DuckDuckGo search that scrapes a results page breaks silently and returns nothing rather than failing. **This revises that decision** (the user's call, 2026-09-25: SearXNG, over Brave's or Tavily's keyed APIs and over MCP only). SearXNG is a metasearch engine the user runs. It answers over a JSON API, keeps its own engines working against upstream changes, and fails out loud: an HTTP status, or the engines it names in `unresponsive_engines`. So the rule the tool is built on is **never an empty success**.

**What was built**

- [x] **`web_search`** (`agent/web_search`). It takes a `query` and an optional `time_range` (day, week, month, year). It returns up to `results` results, each with its title, URL, date when the engine gave one, and a snippet. Any direct answer and fact box an engine gave comes too, and the engines that failed are named. It is `outbound`, and the instance's host is the target of every call.
- [x] **The seam.** `SearchProvider` is a closure, so a keyed API would be a second implementation of it, never a reshaping of the tool. `make_searxng_provider` asks one GET per search through a `UrlFetcher`, so `agent/` includes no transport and everything is tested with no network. The search path is the instance's own path plus `search`, the query form-encoded, `format=json` always, and the instance's own `safesearch` and language.
- [x] **`parse_searxng_response`**, pure. It reads results, answers in both the string and object forms SearXNG has used, infoboxes, and failing engines as `name (reason)`. A body that is not that JSON is an error that shows how it began.
- [x] **Every failure says what to change.**
  - Nothing matched: a result that says so and names the query.
  - Every engine asked failed: an error naming them.
  - HTTP 403, SearXNG's default with JSON off: an error naming `search.formats` in its `settings.yml`.
  - HTTP 429: an error naming `server.limiter`.
  - A refused connection: an error naming the configured URL.
  - A redirect: an error naming where it went.
- [x] **A tool that cannot work is withdrawn for the rest of the turn** (`ToolOutcome::unavailable`, `agentloop/loop`). An instance failure sets it. The loop then leaves the tool out of the turn's later requests, and answers a call that still names it without running it. The next turn offers it again. This was found on real weights; see below.
- [x] **`tools.search`** (`harness/config`): `provider`, `url` (with `${ENV}`) and `results` (1 to 20, default 5). It is kept as written. `search_instance` is the one reading of it, used three places:
  - registration: `make_built_in_tools` adds the tool beside `fetch_url`, over the same HTTP client, only for a usable instance, so a model is never offered a tool that can only fail;
  - the checker: `make_permission_checker` counts the instance's host as listed (25a's recorded default, built here);
  - `check`: the `Tools` section's `search` row says how to add search, where it points, or what is wrong, and makes no request.
- [x] **The shipped config** carries the section commented out under `tools:`, with the JSON setting SearXNG needs.
- [x] **[tools.md](../reference/tools.md)**, a new reference: running SearXNG in a container on a local port, its JSON output and limiter, the `tools.search` lines, `check`, what the model gets, and where a query goes. [http-api.md](../reference/http-api.md)'s `--tools` row names it.
- [x] **Fixtures recorded from a real instance** (`tests/fixtures/searxng/`, the version pinned in their README): ordinary results with a failing engine, dated results, an answer, an infobox, nothing found, nothing because the one engine failed, and the 403 page.
- [x] **12 new test cases**: 1,693 pass under `make test`, and 1,658 in the unit suite built without llama.cpp, which is what CI gates on.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| Results per search | 5, each with title, URL, snippet and date *(default taken)* | A local model reads every result it is given; five is enough to choose one to open. `results` changes it, 1 to 20. |
| `time_range` | Optional: day, week, month, year *(default taken)* | "What changed recently" is the commonest search a model makes. |
| `check` | Configuration only, no request *(default taken)* | `check` never waits on the network. A failing instance is reported by the tool when used. |
| `safesearch` and language | The instance's own *(default taken)* | Its owner already chose them. |
| Backends | Every backend with tools on *(default taken)* | A cloud backend with `--search` has its provider's search as well; the model may use either. |
| The instance's host | Counted as listed by the checker | "Trusted by configuration" (25a) made concrete: a search never asks, on any surface. A section that cannot be used trusts nothing. |
| An absent `provider` | Means `searxng` | The one provider there is; `url` alone is enough. |
| An unknown provider or unusable URL | Loads, registers nothing, and `check` warns | As with a pasted URL in `allowed_hosts`: refusing the whole config over one section would take every other command down with it. `results` out of range is a load error, as other numbers are. |
| **An instance failure** | **The tool withdrawn for the rest of the turn** *(decided on real weights, 2026-09-28)* | Told in words to stop, Qwen3-VL-8B rephrased the query 12 times until the step limit (149 s). The repeated-call guard only catches identical calls. Withdrawn, the model searched once and answered in 77 s. Every engine failing does not withdraw the tool, because engines come back. |
| Fixtures | Recorded, then cut to six results | As the vendor-CLI fixtures are. A change in the API's shape is detected, not silently absorbed. |

**Verified on the real binary.** Two SearXNG containers (`searxng/searxng`, version `2026.9.25-12f8b6515`) ran on loopback: one with JSON on, one as shipped. The model was Qwen3-VL-8B, 32K context, temperature 0.
- **Search, open, cite.** Asked, under a pseudo-terminal, what changed in llama.cpp release b11151, the model called `web_search`. It got five dated results, and opened a write-up of b11151 with `fetch_url`, which asked about the website first, showing its host and whole URL. The answer summarized the release and cited that URL. An earlier run on b6000 went through the same steps: a search, then two fetches, each asked about.
- **JSON off.** Against the instance as shipped, the tool result named `search.formats` in `settings.yml`, and the model searched once more and answered without it.
- **`check`.** With no `tools.search`, `check` shows the lines to add. With it, the row shows where search points and that its host is reached without asking.

**Guardrails, each mutation-tested (34 mutants, every one run against the whole unit suite).** 33 were caught outright, and 1 once its test was strengthened. The mutants, by area:
- **The parser:** a body without `results` accepted; a result with no URL kept; any `publishedDate` taken as a date; failing engines not read; answers in their object form ignored; an infobox's page not read from `urls`.
- **The provider:** a 403, a 429, a refused connection or a redirect not explained; another status parsed as a success; more results than the count.
- **The search URL:** no `format=json`; the query not encoded; `time_range` dropped; a `+` kept raw.
- **The tool:** an instance failure not `unavailable`; every engine failing reported as nothing found; any `time_range` accepted; the tool not outbound; any provider accepted; a query in the instance's URL kept; a snippet not shortened, or cut through a character; nothing found without naming the query.
- **The rest:** a withdrawn tool still offered, or still run; `unavailable` ignored; the instance's host not trusted; the tool never registered; a broken section reported ok, or no way to add search named; `results` unbounded; the section not read.

**The survivor, and what it taught.** The snippet's cut through a character went unseen. The test's text of two- and three-byte characters put the 300-byte cut on a character boundary every time. It now shifts the text by up to four bytes, so the cut meets every position within a character. This is 25d's lesson again: a test that cuts text at a fixed length has to sweep the alignment.

**Not verified.**
- **Cloud backends.** They get the tool; no cloud model was run with it.
- **The redirect, 429 and refused-connection messages** are proven against a scripted instance, not a real one.
- **Found along the way, not fixed here.** On a turn that reaches the step limit, a local model can write its call as raw markup on the forced final step, where tools are withdrawn, and that markup reaches the answer. It predates this item (25b), and is recorded for its own fix.

### 2026-09-28 — `fetch-url-reader` (backlog item 25f): fetch_url reads a page as a model should

**Why.** `fetch_url` had not changed since Milestone F. It stripped every tag, kept the first 8,000 bytes, and marked the rest `[truncated]`. On a documentation or news page those bytes were mostly menu text, the links were gone, and whatever lay past the cut could not be reached. A local model also pays twice for the noise, once more to read it. The spike found two smaller faults as well: the cut could split a UTF-8 character, and no content type was checked, so a PDF was "stripped" into noise.

**What was built**

- [x] **The reader** (`agent/readable`), hand-written with no parser library (the recorded default).
  - **Parsing.** A forgiving tokenizer builds a flat tree of the page: indices rather than pointers, HTML's implied end tags, scripts and styles skipped (still parting the words either side), and nesting capped at 256.
  - **The content landmark.** `<main>` or `role="main"`; within it, an article holding at least half of all the articles' text (the story, not a list of teasers); else the body.
  - **What is dropped.** Page furniture:
    - by element: `nav`, `aside` (but never a footnote), and a form that doesn't hold the content;
    - by role;
    - `hidden`, `aria-hidden` and `display:none`;
    - a short list of class and id names (consent banners, ad slots, share bars, sidebars, MediaWiki's menus and edit links). A name match never drops what holds the page's heading or half its text.
  - **What is written.** Light Markdown: `#` headings, `-` and `1.` list items, fenced code with its whitespace, `|` table rows (a layout table as paragraphs), blockquotes, and links as `[text](absolute URL)`. Dot segments are resolved and `<base href>` honoured. In-page links keep their words, and a lone pilcrow or back-arrow goes.
  - **Characters.** Every numeric and Latin-1 named entity is decoded, and any declared charset is converted to valid UTF-8 (`as_utf8`, `charset_of`, `meta_charset`); a stray byte becomes U+FFFD, since a tool result is serialized as JSON downstream.
- [x] **Pages** (`page_of`). The first call returns at most 6 KiB of the page, and each call reading on at most 12 KiB. The cut falls at a paragraph break in the page's second half, else a line break, else a space, and never inside a character. The tool takes `offset`, says `Page N of M`, and ends a continuing page with the exact offset to read on from. Walking the offsets covers the text with no gap and no overlap.
- [x] **A header line**: the title, and the final URL, with where a redirect came from. It replaces 25a's `[X redirected to Y]` line.
- [x] **Content types** (`classify_body`). The response's `Content-Type` now travels through `HttpResponse` and `FetchResult`.
  - HTML is extracted; `text/*`, JSON, XML and YAML pass through.
  - An unlabelled or `octet-stream` body is read for what it is.
  - A PDF (by type, or by its `%PDF-` bytes whatever it is labelled), an image, audio, a video, an archive or binary data is **refused by name**, never returned as bytes.
- [x] **The description** says what it reads, the page size and how to read on.
- [x] **The corpus** (`tests/fixtures/web/`), its sources and licences in its README:
  - two pages recorded (a Python documentation page, PSF licence; a Wikipedia article, CC BY-SA), with the user's approval;
  - three hand-written in real sites' structure with invented text, so no newspaper's copyright enters the repository: a news story, a GitHub release, a page built by scripts.
- [x] **13 new test cases**: 1,706 pass under `make test`, and 1,671 in the unit suite built without llama.cpp, which CI gates on.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| The extractor | Hand-written, no library *(default taken)* | The corpus passes without one. lexbor or gumbo stay the fallback, and each would be a dependency on five targets. |
| One call's page | **6 KiB for the start of a page, then 12 KiB a call** *(the user's call, 2026-09-28: "whatever is fastest"; it replaced the recorded 12 KiB)* | A lookup reads only the start of a page, and every byte is read before the model can act: 12 KiB was about 3,000 tokens, ~40 s on a hot M3 Max running Qwen3.8-27B. Halving only the first page halves a lookup's reading (1,484 tokens for the start of the Python `json` page, where 12 KiB was about 3,000), while a model reading on gets 12 KiB at a time, so a long document costs no more calls, and no more reasoning steps, than before. The `json` page is four calls now: 6 + 12 + 12 KiB, and its end. |
| PDFs | Refused with a note *(default taken)* | Reading PDFs is its own dependency and its own item. |
| Links | Inline Markdown *(default taken)* | A numbered list at the end costs a lookup per link. |
| The download cap | 5 MB *(consumed: 25a)* | Unchanged. |
| The code layout | A new `agent/readable` pair beside `fetch_url` | The reader is most of the item. `fetch_url` stays the tool: gate, redirects, classification and paging. |
| What `offset` counts | Bytes of the extracted text, from the start | A model copies the number the last page gave; the pages are counted from the start, so it is always "N of M". |
| Refusing a body | After it is downloaded | The fetcher returns the whole body. Refusing on the headers alone would save the bandwidth, but it is not needed for correctness. |
| Footnotes | Kept, even in `<aside>` | Sphinx wraps them so, and they are the text's. Found on the live page, where "Footnotes" stood alone. |
| The corpus | Two recorded pages, three hand-written | The user's call on licensing: recorded where the licence allows, and never a newspaper's article. |

**Verified on real pages.** Every run used Qwen3-VL-8B on the real binary, with `docs.python.org` and `arxiv.org` allowed.
- **Paging.** Asked for the last command-line option on Python's `json` page (29 KB of text), the model read page 1 of 3, then offsets 12208 and 24127, and quoted from the last section. It named `--indent … --compact` rather than `-h, --help`, which follows it on the same page. Qwen3.8-27B read the same three pages and quoted the last option, `-h, --help`, "Show the help message.", exactly (196 s).
- **Following a link.** The model read the `json` page, followed its (now absolute) link to `pickle`, and quoted pickle's first sentence exactly.
- **A PDF.** `arxiv.org/pdf/1706.03762` was refused by name. The model searched, found the paper's HTML abstract page, read it, and summarized the paper correctly.

**What makes a local answer slow on this machine** (investigated 2026-09-28, the user's question: a price lookup on Qwen3.8-27B took 2½ minutes).
- **Heat, first.** Measured with `macmon`, the 14-inch M3 Max's GPU reaches 95–97 °C within about 25 s of steady generation, and its clock falls from 1,372 MHz to about 610 MHz after two minutes. Generation fell from 17.5 to 12.3 tokens/s over those two minutes and was still falling. After an hour of builds and model runs, it measured 5.2 tokens/s generating and 73 reading, against 13.6 and 152 when cool.
- **Ruled out.** CPU load from builds (12 busy cores cost 7%). Graphics from the terminal and this desktop app, which kept the GPU "96% busy" at rest yet cost the model little. Apogee itself: llama.cpp's own benchmark, at the same pin, matches it.
- **This machine supports High Power mode** (`pmset -g cap`), which runs the fans harder. It is the user's setting to change, not Apogee's.
- **The rest is tokens.** Qwen3.8's reasoning before every step, and every byte a tool returns. That is what the smaller first page, and [thinking control](#milestone-j--local-inference) (26i, shipped 2026-10-03), address.

**Guardrails, each mutation-tested (51 mutants, every one run against the whole unit suite).** 48 were caught outright, and 3 once their tests were strengthened. The mutants, by area:
- **Landmarks:** `<main>` ignored; no dominant article, or any largest article taken.
- **Furniture:** a page's header kept, or a section's dropped; `hidden`, roles or `display:none` ignored; class names ignored, or their content guard removed; every form, or no form, dropped; footnotes dropped.
- **Links:** left relative; dot segments kept; `<base>` ignored; in-page links kept as links; a lone symbol kept; image alt text ignored.
- **Markdown:** code blocks collapsed; no backticks; no table header rule; a layout table as rows; ordered lists as bullets; a marker's line broken; no quote prefix.
- **Characters:** numeric entities, Latin-1 names, or C1 references not decoded; stray bytes kept; overlong sequences accepted; Latin-1 not converted; the `meta` charset ignored.
- **The parser:** `li` or a block not closing what HTML closes; a script parting no words; no depth limit.
- **Paging:** no seam; a cut through a character; overlapping pages; numbering from the offset.
- **The tool:** a PDF by its bytes missed; an image unnamed; binary read as text; an unlabelled page not sniffed; the charset ignored; no offset to read on from; the redirect's source unnamed; the offset ignored; a negative offset accepted; the fetcher dropping the type.

**The survivors, and what they taught.**
- **"Any largest article."** The listing test's teasers were all under the 200-character floor, so none could ever be chosen whatever the dominance rule said. The listing now has entries long enough to be chosen.
- **A list marker's line broken inside `block`.** No test put a heading directly in a list item, the one way to reach it.
- **A paragraph not closed by a block.** It changes the tree, not the text, since every block writes its own breaks. Where it does show is `<p hidden>gone<div>shown</div>`: a browser closes the paragraph, so the `div` is seen.

**Two mistakes in the run itself.** The run crashed on its 30th mutant when the tests printed a byte its script could not decode; its cleanup restored the file, and the rest was rerun. It was paused, not killed, while the speed was investigated.

**Not verified, and known limits.**
- **A page built by JavaScript still has no text.** The item keeps it out of scope; the tool now says it may need JavaScript.
- **Pages go stale between calls.** Each page is a fresh fetch, so a page that changes between calls can shift the offsets.
- **Answered `yes`, the next page asks again.** `session` is the answer for reading a whole page.
- **An offset past the end** is an error that says to start again.
- **What a real news site or GitHub looks like today** was not recorded; those fixtures copy their structure by hand.

### 2026-10-04 — `session-permission-presets` (backlog item 26o): the `session` answer, given early

**Why.** The prompt's `[s]ession` answer already granted a tool for the rest of a run, but only once the first prompt had asked, so a user who already knew what they wanted still had to wait to be asked. The user named the claude CLI's launch-time flags as the reference (2026-09-30). The gate itself needed no change. `SessionApprovals` was already the set the checker and the prompt share, so a preset only has to fill it before the first turn.

**What was built**

- [x] **Launch flags on `chat` and `complete`, the same on both** (`--allow`, `--deny` and `--allow-host`, each repeatable).
  - `--allow write_file` names a gated tool, and `--allow-host docs.python.org` names a website.
  - `--deny` takes either kind.
  - They need `--tools`, because without tools nothing is ever asked.
  - A machine-mode child takes them on its argv, so a front-end presets its session the same way. The `question` event is unchanged.
- [x] **`complete --allow` is the one new reach, and it is deliberate.** `complete` has nobody to ask, so `ask` resolves to deny; with `--allow write_file`, a one-shot script uses a destructive tool on purpose. Without the flag the denial is the same tool result as before, byte for byte.
- [x] **Slash verbs mid-chat** (`/allow`, `/deny`, `/revoke`, `/permissions`), four rows in the one command table:
  - `/allow ` and `/deny ` complete the gated tools (the registry's `writes` tools, MCP tools without a read-only hint included).
  - `/revoke ` completes the session's own answers.
  - Bare `/allow` lists, as `/permissions` does.
  - `/permissions` says each gated tool's answer and where it comes from, then the websites: `write_file   allow  (this session)`, `delete_file  allow  (config)`, `website example.org: deny (this session)`.
  - `/revoke` reaches only the session's answers. For a config answer, it names `apogee config set-permission <tool> ask`, because chat never mutates config permissions.
- [x] **The session's no** (`SessionApprovals::denied_tools`, `denied_hosts`).
  - For a tool, the checker reads, in order: the config's `deny`, the session's no, the config's `allow`, the session's yes, then ask.
  - For a website, the session's no comes before everything else.
  - A no therefore wins over a config `allow`, and a yes never reaches past a config `deny`.
  - `/allow` on a tool the config denies says the tool stays denied.
  - `seed_approvals` applies denials last, so `--allow X --deny X` is a no.
- [x] **A typo grants nothing** (`name_gated`). A name that is neither a gated tool nor a website is refused, naming the gated set. Websites are canonicalised by the one host rule (`harness::canonical_host`) and match exactly, the way the `s` answer remembers one.
- [x] **Grants die with the process.** Nothing a preset or a verb does writes config, and a resumed chat starts with no session answers, asserted.
- [x] **Tests**:
  - **The equivalence table** (`[presets]`): every config level × preset × prompt answer, a flag-seeded session against an `s`-answered one, with a config `deny` never loosened in any of them.
  - **Cases**: a no tightening a config allow, `--allow-host` answering one website and no other, typos, revoke, and the `/permissions` golden.
  - **`chat_test`**: the `complete` pair; `chat --allow` writing without a prompt while a fresh chat asks; the slash verbs end to end with a clean resume.
  - **Completion goldens.**
  - **The PTY check's new presets case** (its ninth): `--allow write_file` writes with no prompt on a real terminal, `run_command` still asks, and `/permissions` says both.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| The model | **A preset is the `session` answer, given early** *(recorded 2026-09-30)* | It reuses the shipped structure, so there is no second permission path, and the ladder stays config-first. |
| Bare `/allow` | Lists *(default, confirmed by the user)* | Listing beats an error. |
| Hosts on `--deny`/`/deny` | Yes *(default, confirmed)* | One vocabulary for both kinds of ask. |
| `/revoke` of a config answer | Points at `config set-permission` *(default, confirmed)* | Chat never mutates config permissions. |
| An unknown name | Refused, naming the set *(default, confirmed)* | A typo must not grant nothing silently. |
| `--allow` and websites | **Tools only; websites take `--allow-host`** *(group run, flagged for veto)* | A typo'd tool name that parses as a host would otherwise become a website grant. A `--deny` that lands on a host by mistake only tightens. |
| How a session host matches | **Exactly, by canonical host** *(group run)* | That is what the `s` answer already does. `tools.allowed_hosts` is unchanged. |
| Allow and deny together | **Deny wins** *(group run)* | Tightening always wins. |
| What is gated | **The registry's `writes` tools** *(group run)* | Outbound read-only tools are asked per website, so they are named by host. |
| Presets without `--tools` | **Refused** *(group run)* | Without tools nothing is asked, so a preset there is a mistake, not a no-op. |

**Guardrails, each mutation-tested (16 mutants, all caught).**
- **The checker:** the session's no ignored, for a tool and for a website; the config's `allow` read before the session's no; a session yes read before a config `deny`.
- **Seeding and names:** `--allow` winning over `--deny`; `--allow` taking a website; a typo accepted.
- **The verbs:** an allow keeping a denial; a deny keeping an allowance; revoke keeping a website's answer; `/deny` not applied; `/revoke` of a config answer not naming `config set-permission`; `/permissions` calling a session answer the config's.
- **The surfaces:** `chat`'s flags not seeded; `complete`'s not seeded; `complete` accepting presets without `--tools`.

**Not verified.**
- **Machine mode's argv path** is the same code as `complete`'s pipe path, not separately driven by a front-end.
- **`--allow-host` on a real website** is covered by unit tests, not by a live fetch.

### 2026-10-04 — `tool-use-policy` (backlog item 26p): reaching for the tool instead of refusing

**Why.** The user's transcript (2026-10-03): Qwen3-VL-8B with `--tools` and search configured answered "What is the current temperature in Lehi Utah?" with *"I can't provide real-time weather information"*. Told to search, it searched and answered correctly. Nothing in the prompt pushed back on the trained refusal. The environment note (25d) said the date, the system and the folder, but nothing about when to use a tool. `web_search`'s description said what it *returns*, not when to reach for it.

**What was built**

- [x] **A policy paragraph in the environment note** (`tools/environment`: `tool_reach`, `render_tool_use_policy`).
  - It follows the note after a blank line: for anything current, recent or beyond what the model can know (the weather, news, prices, scores, schedules, what a page says now), search the web and read what it finds before answering. It must never say it lacks access to information one of its tools can get, and when it already knows the answer, it just answers.
  - **Composed from what the registry holds**, by capability:
    - with search and a reader, the whole paragraph;
    - with search alone, no promise of reading pages;
    - with a reader alone, reading a page the user links or whose address the model knows, and no search named;
    - with neither, no paragraph at all.
- [x] **The note is rendered from the registry that asks for it** (`ToolRegistry::EnvironmentRender`, a renderer handed the registry). `fetch_url` and `web_search` are registered beside the native toolsets that set the note, and a read-only agent's filtered copy keeps them, so each registry's note says what that registry holds. `apply_tool_policy` copies the renderer unchanged.
- [x] **Descriptions that lead with when to call the tool.**
  - `web_search` now starts: "Search the web for anything current, recent or that you cannot know from memory: the weather, news, prices, scores, schedules, releases, or anything after your training data", and only then what it returns.
  - `fetch_url` now starts: "Read a web page for what it says now: a URL the user gives you, a link a search result or another page names, or a page whose address you know."
  - The descriptions also feed 26g's ranking, so the weather words now rank `web_search` for a weather question.
- [x] **Tests**:
  - **Goldens**: one per composition.
  - **Size**: the paragraph stays under 512 bytes, well inside a note the budget never trims.
  - **The note reads the registry when asked**, including a tool added after the note was set.
  - **A read-only agent's note** carries the reader-only paragraph.
  - **Byte-identical across turns**: a two-turn chat through the loop (three requests) sends the same note bytes on every request, and none of them reach history. This pins the prefix-cache property.
  - **Trigger phrases**: both descriptions' trigger phrases come before what they return.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| Policy, not heuristics | **Prompt-side** *(recorded 2026-10-03)* | Refusal detection or re-prompting treats the symptom per conversation at inference cost. |
| The wording | The drafted policy, refined: the paragraph opens "How to use these tools:", and gains "When you already know the answer, just answer" *(default, confirmed; refined during the build)* | The draft told a model what to reach for, but not when to stop. Small talk must not search. |
| Which descriptions | `web_search` and `fetch_url` only *(default, confirmed)* | The live runs showed no missed reach for files, the shell or git. |
| What the paragraph names | Capabilities, not tools *(default, confirmed)* | An MCP tool that fits benefits without an edit. |
| How capability is read | **By the two built-in tools' names** *(group run, flagged for veto)* | The doc's own rule is "no search claim without `web_search`, no page-reading claim without `fetch_url`". A generic flag on `Tool` would be speculative plumbing for tools that do not exist yet. |
| Where the composition lives | **The renderer is handed the registry asking** *(group run)* | Capturing the tools when the note was set would miss `web_search` on some surfaces and a filtered copy's removals on others. Rendering per request from the asking registry is exact and still byte-stable, since the registry does not change during a session. |
| A family that ignores it | **Not built** *(recorded 2026-10-03; the need now shown, below)* | The per-family line stays deferred, as the item says. Llama 3.1 8B's small-talk search is the evidence for it. |

**Verified on real weights.** Each family ran in its own `serve --tools` process, model loaded once, against the user's SearXNG. Three independent conversations each asked "Hello, how are you?" and then the weather question, on the binary before this item and after it. Weather questions that searched on the first ask:

| Family | Before | After |
|---|---|---|
| Qwen3-VL-8B | 1 of 3; twice "I can't provide real-time weather information", the transcript reproduced | **3 of 3**, each answering a temperature |
| Llama 3.1 8B | 0 of 3; each reached for `run_command` and `curl` instead | **3 of 3**, each answering a temperature |
| Gemma 4 12B | 3 of 3 | 3 of 3 |
| gpt-oss 20B | 3 of 3 searched, but each then claimed it had no access to live data | 3 of 3 searched; one answered with the temperature, one said it needed to read a page, one still claimed it had no way. Its page fetches went to hosts not in `tools.allowed_hosts`, which a served request refuses (nobody can answer the prompt), so the claim followed real refusals. |

- **Small talk** called no tool on Qwen, Gemma and gpt-oss, before and after.
- **Llama 3.1 8B searches on "Hello" before and after.** The paragraph's last sentence did not change that. It is the demonstrated need the deferred per-family line waits for.
- **The acceptance case, on `chat`:** `apogee chat --tools` on Qwen3-VL-8B answered "Hello" with no tool and the weather question with one `web_search`, giving a temperature.
- **With search left unconfigured**, the same question got "I cannot directly provide the current temperature": the honest answer when no tool can find it.

**Guardrails, each mutation-tested (12 mutants, all caught).**
- **The paragraph:** a search claimed for a reader alone; a paragraph with nothing to reach; page reading promised without a reader; the never-claim sentence dropped.
- **Reach:** search read off the reader's name.
- **The note:** the policy from every tool rather than the registry's; a different note each time it is asked; the policy run into the last line; the note rendered from an empty registry; a read-only agent's copy without one.
- **The descriptions:** each old lead restored.

**Not verified.**
- **Cloud backends** get the same note and descriptions through the same request assembly; no cloud model was run.
- **MCP tools** reach the paragraph's wording but cannot switch it on: a session without `fetch_url` or `web_search` has no paragraph, whatever its MCP servers offer.

## Milestone W — The MCP client

**Goal.** The model picking up anyone else's tools: a from-scratch client for stdio MCP servers whose tools join the shared loop as first-class registry entries, with the subprocess discipline a misbehaving server demands, `apogee mcp` and its admin twins over one scaffold core, and Apogee hosting a server of its own.

### 2026-09-13 — `mcp-stdio-client`: a client whose dead servers fail now, and a server with nothing to install

**The shape it inherited.** Milestone V had just put the native toolsets into the one registry and given every destructive tool a gate. The child-process seam had a bounded stderr tail from the vendor CLIs, the JSONL framer had already absorbed the chunk-boundary bug class, the events bus and the admin plane existed, and the scaffold pattern — a TTY-free core both the CLI and a route call — had been named but never built. Two failure modes set the bar: a proxy that narrates its OAuth handshake straight onto the terminal, and a server that dies mid-handshake and holds the frozen status line for twenty seconds.

**What was built**

- [x] **`source/mcp/`** — `types` (JSON-RPC hand-rolled: ids the client allocates, a notification told from a response by the absence of one; `mcp__<server>__<tool>` split on the **first** `__`; `readOnlyHint` read from `annotations`, absent meaning destructive), `transport` (`Transport` as an interface so the test fleet is scripted transports; `StdioTransport` over `platform::ChildProcess` with the JSONL framer and a **mandatory** stderr sink — no inheriting constructor, not even "for compatibility"; this API has no such door — plus the eight-line `StderrTail` that tees outside its lock), `client` (the reader started *before* `initialize`; calls demultiplexed by id; `mark_done` from the read loop's error path as well as `close()`, once, by compare-and-swap; notifications logged and dropped), `registry` (every enabled server dialled in order, `[mcp] connecting:` *before* the dial, a bound covering connect **and** handshake, a bad server logged and skipped, tools registered as `writes` unless read-only, `mcp.server.connected/disconnected` on the bus), and `serve_stdio` (the other side of the four methods; only read-only tools served, because the gate lives in the loop and a server has nobody to ask).
- [x] **`source/scaffold/mcp_server`** — `create_mcp_server`: a runnable Python server, its test and README from templates compiled into the binary, or an existing executable registered with `--command`; the entry appended through the config editor. The one function `apogee mcp create` and `POST /v1/admin/mcp-servers` both call.
- [x] **`mcp_servers:` in the config** — a map like `backends:` (`command`, `args`, `env`, `enabled`), `${ENV}` **and** `~` expanded, and three transforms: `append_mcp_server` (fields alphabetical after the name, `enabled` always written), `delete_mcp_server`, `set_mcp_server_enabled` (one line replaced in place, its comment kept). An `mcp/` layout row for scaffolds.
- [x] **`apogee mcp create|list|test|enable|disable`** and `config delete-mcp-server`; the five admin routes (`GET/POST /v1/admin/mcp-servers`, `GET/DELETE/PUT …/{id}`) in [http-api.md](../reference/http-api.md), views that show `env_set` and never the values; the doctor's `MCP` section (`PATH` lookup, a missing file, a lost execute bit `--fix` restores, never editing config).
- [x] **The loop**: `make_built_in_tools` connects every enabled server on a caller-owned registry and registers the tools last, so a namespaced name can never shadow a native one; progress repaints the status line and a warning stays; `--verbose` routes a server's raw stderr to the terminal and `serve` keeps it as its daemon log.
- [x] **`apogee __mcp-tools`** — hidden; serves the read-only native toolsets over this process's own stdio, built from the toolsets directly and never the MCP registry (a config naming this very command would otherwise spawn itself without end). Registered as `command: apogee, args: ["__mcp-tools"]`, it is a server for any MCP client with nothing to install — and the real-subprocess fixture the e2e suite drives.
- [x] **31 new test cases** (1056 in all), green in both builds; two new checks on the real binary: `cli.mcp_lifecycle` and the PTY check `cli.mcp_server_stderr_stays_off_the_terminal`.
- [x] A new `## ⚠` section in CLAUDE.md, **A child's stderr is captured, never inherited**, now that the enforcing code exists.

**The fleet is the test.** `tests/support/fake_mcp_server` scripts seven personalities as transports — well, chatty, slow, dying, malformed, erroring, slow-calls — and every client and registry test runs against them: a dying server fails in under two seconds of a twenty-second bound with its stderr tail on one line; a slow one is closed at the bound while the next still connects; junk frames, unexpected ids and notifications are logged and skipped; built-ins dispatch identically with zero, one failed, or one connected server. On the real binary, a `/bin/sh` server that narrates on stderr and exits is registered and a `complete --tools` run reports it with its last line folded in **exactly once**, under a pseudo-terminal that shows none of its lines as themselves.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| Transports | **Stdio only** | Remote transports need an outbound HTTP long-poll inside startup and an OAuth story; both ride the deferred call on the roadmap. |
| The in-binary server's first user | **Apogee's own read-only toolsets** | Making Apogee itself the first means the pattern ships proven and the tests get a real subprocess with no interpreter dependency. Writing tools withheld: the gate lives in the loop. |
| `protocolVersion` | `2025-03-26`, accepting what the server answers *(default taken)* | The newer date carries `annotations`; every server in the wild accepts either. Recorded in `mcp list`. |
| Bounds | 20 s connect (covering handshake), 60 s call, 8-line tail, 1 MiB frame *(default taken)* | Each with its reason in the file. |
| `~` in `command`/`args`/`env` | Expanded, with `${ENV}` *(default taken)* | The starter config's own example writes a `~` path in `command`; the documentation and the loader have to agree. |
| A tool with no `readOnlyHint` | **Gated** *(default taken)* | A third-party tool with no annotation is treated as destructive; `permissions.mcp__<server>__<tool>: allow` opts one out. |
| Scaffold languages | Python only, plus register-only *(default taken)* | A Go template would assume a toolchain nothing checks for. |
| `tools/list_changed` | Logged and ignored *(default taken)* | Tools are fetched once at connect; live refresh is new behaviour for a later item. |
| Where MCP tools land | The same `ToolRegistry`, registered last | One registry, not a fall-through pair; a namespaced tool whose server is down is an error result the model reads, never a fall-through. |
| The framer | `backends/jsonl_framer.h` allowed into `mcp/` by name | A second framer would be a second copy of the chunk-boundary bug class; the layering guard exempts that one header for `mcp/` only, with the reason in the file. |
| A failed connect on the terminal | A warning line that **stays**; progress repaints | A warning is never repainted away: a failure the user cannot read back once the prompt is up is a failure the user will report as "tools are missing". |

**Verified on the real binary.** `cli.mcp_lifecycle`: a scaffolded server whose own `test_server.py` passes, `mcp test` through the production client with `connecting:` before the dial, `mcp list` with the negotiated protocol, `__mcp-tools` registered as one more server answering a read-only tool and refusing a writing one, a `complete --tools` run connecting at startup and reporting a dying server once with its tail, and `disable`/`enable`/`delete-mcp-server` round-tripping through `config get`. The PTY check on top of that.

**Guardrails, each mutation-tested (33 mutations: 31 caught outright, 1 caught after its test was strengthened, 1 that exposed a redundant branch, since removed).** `done` not closed from the read loop's error path; the reader started after the handshake; tools not cached; cancellation and the call deadline ignored; notifications treated as responses; the initialized notification not sent; disabled servers dialled; no progress before the dial; the failure reason dropped; tools not namespaced; MCP tools never gated; the connected event not published; the stderr tail unbounded; the sink not teed; the last line lost at EOF; an unknown method silently ignored; writing tools served, or dispatchable over stdio; namespacing split on the last delimiter; a leading tilde not expanded; `enabled` accepting any word; `enabled` inserted instead of replaced; a double underscore allowed in a server name; `server.py` written without the execute bit; a missing command passing the doctor; env values listed by the admin view; a collision not a `409`; MCP tools not registered into the loop; the `PUT` row ungated; `mcp/` not a layout row; and `mcp/` including a provider.

**The survivor, and what it taught.** "`done` not closed from the read loop's error path" survived its first run because the dying fake *refused the write*, so the send path released the waiter and the read loop never had to. A real child that dies mid-handshake takes the `initialize` frame into its pipe buffer and only the read hits EOF — that is the path the fix exists for. The fake now takes the frame and hangs up, and the mutant costs the full twenty-second bound, which the test refuses. A fixture that fails earlier than the real thing tests the wrong path. The other survivor, an extra quoting check on flow-list items, was dead code: `yaml_scalar` already quotes every character a flow list could misread. The check was removed rather than tested.

## Milestone X — Agents as data

**Goal.** A named workflow the user runs with `apogee analyze --agent <name>`: a persona from prompt files, an output schema the answer must satisfy, a tool policy that is the agent's permission model, executed by the same loop every other surface runs — so an agent behaves identically on every backend the loop drives — with structured output validated on every provider, an in-process renderer, deterministic branch review from flags, `apogee agents` and its admin twins over one scaffold core, and three review agents re-authored for the owned loop.

### 2026-09-13 — `analyze-agents`: per-agent tool policy, structured output everywhere, and the bundled reviewers

**The shape it inherited.** Milestones V and W had put the native toolsets and the MCP client into one registry with every destructive tool declared into the gate, so a read-only policy could be a *filter over registered tools* rather than a plea to the model. The loop's Reporter seam, the profile filters that strip reasoning from every backend's answer, `auto_rag` and the one retriever resolver, the config editor, the layout declaration, the scaffold pattern and the admin plane all existed. So an agent's tool policy and its servers are applied in Apogee's own loop for every backend, never forwarded to the `claude` CLI as `permission_mode` and `--mcp-config`.

**What was built**

- [x] **`agents:` in the config** — a map like every other section: `description`, `model`, `prompts[]`, `schemas[]`, `output_format ∈ auto|json|markdown`, `tools ∈ read-only|all|none`, `mcp[]` (the servers this agent connects; empty means none — an agent names what it needs), `questions`, `collection` (the `auto_rag` mechanism, read from the agent instead of the top-level key), `save_dir`, `save_filename`, `save_subdir`; every enum validated at load, `${ENV}` and `~` expanded, a **relative path resolved against the data directory the config lives in** (`prompts/x.txt`), which is what keeps an entry portable and a `--config` temp tree hermetic. `append_agent` (fields alphabetical after the name; a string or list only when set, a boolean only when true, `tools` always) and `delete_agent`.
- [x] **The bundled agents, compiled in and seeded skip-if-present** (`harness/assets`) — `security-review`, `release-notes`, `merge-request`, their prompts and schemas byte-identical to the shipped files under `assets/prompts` and `assets/schemas` (a test fails the build on drift, like the config template's), materialised by the ONE seeding path (`seed_data_directory`, hence `check --fix`, hence both installers), never overwritten once present. A config entry of the same name overrides the bundled definition; a bundled agent whose files are not seeded runs from the compiled-in text. Three new layout rows: `prompts/`, `schemas/`, `analyses/`.
- [x] **The tool policy, structural** — `make_built_in_tools` takes a policy and applies it *over the registry* last, native, `fetch_url` and MCP alike: `read-only` keeps only tools that declare no `writes` (an MCP tool is read-only exactly when its server said so), `none` builds nothing and dials no server, `all` is the gate as `chat` has it. What the loop advertises IS the filtered set. An agent's `mcp[]` selects which servers connect; an unknown name is reported and skipped.
- [x] **Structured output on every provider** (`agentloop/structured`) — the schema rides `ChatRequest::Transient::response_schema` on every request; each wire translates it where the API has a native mode: OpenAI's Responses `text.format` (`json_schema`, `strict: false` because strict rejects a schema with optional fields), Gemini's `responseMimeType` + `responseSchema` reduced to the OpenAPI subset the API accepts **and only on a request with no tools** (the API refuses the combination), Anthropic's `output_format` with its beta header where the model id says the field exists, else **one forced tool** whose `input_schema` is the answer's schema — `tool_choice` by name with no other tools, `any` with them, none under extended thinking — and whose arguments the provider folds back into answer text so the loop sees an ordinary answer; llama.cpp states the schema in the system block, once, never twice. **Validated client-side always** with `pboettch/json-schema-validator` (draft-07, on nlohmann/json), one corrective turn carrying the validator's every complaint, and a second miss delivered RAW with `conforms: false` — never dropped, never a silent pass.
- [x] **The renderer, in-process** (`render/json_report`) — top-level keys alphabetised, the reserved `human_summary` always last, `## Title` per key, arrays of objects as bullets keyed by their longest string, unparseable input returned unchanged. No second model call, ever.
- [x] **Deterministic branch review** (`agentloop/review_context`, `tools/git`) — `--branch`/`--base`/`--remote`/`--fetch|--no-fetch` become the git tools' defaults as plain parameters (no environment side channel) plus a one-line system note; `git_log` gained the review form beside `git_diff`, so "read the commit messages" means the reviewed branch's. `chat` has the same flags and `/branch`, re-pointing the tools through a live shared value without rebuilding the registry (and without re-dialling every MCP server in it); the note rides the transient prefix, never the transcript, so a resumed session gets what its own flags say.
- [x] **`apogee analyze`** — `--agent <name> [text] [--input] [--interactive] [--list] [--prompt…] [--schema…] [--json|--markdown|--text] [--show] [--save] [--save-name] [-m] [--rag] [--retriever] [--rerank] [--branch…] [--no-questions] [--output-format stream-json]`; the persona is the agent's prompt alone (no chat persona, no backend `system_prompt`); input precedence positional > `--input` > piped stdin; the quiet-save rule `show || json || !tty || !saved`; a named agent saves to `analyses/<save_subdir>/` under `<base>-YYYYMMDD-HHMMSS.<ext>` and says `Saved:` on stderr; `--interactive` runs turns under the persona with `/rag`; machine mode is the same loop over the JSON reporter. **Vendor-CLI backends refused by type** *(the user's call)* before anything is spawned, with the reason.
- [x] **`apogee agents create|list|edit|delete`** through `scaffold/agent` — the one function `POST /v1/admin/agents` also calls, so an agent made over HTTP is byte-identical to one made on the command line (files and entry both); `create` prompts for the three core fields on a terminal and takes the defaults on a pipe; `edit` opens the files in `$EDITOR` through `platform::run_foreground`, the one documented exception to the captured-stderr rule; `delete` asks about the files on a terminal, `--purge`/`--keep-files` decide on a pipe. Five admin routes (`GET/POST /v1/admin/agents`, `GET/PUT/DELETE …/{id}`; `GET` inlines the bodies, `PUT` is `force`, `DELETE ?purge=true` removes the files) in [http-api.md](../reference/http-api.md). The doctor's `Agents` section: bundled files present or a warning with `--fix` as the remedy, every entry's files, schema (validated as draft-07), model, collection and servers checked.
- [x] **The mock learned a script** — `type: mock` with a `model_path` answers from a JSON file of turns, tool calls included, with `{{last_tool_result}}` / `{{system}}` (and `:json` forms) expanded against the request — which is how the real binary is driven through a tool-calling review with nothing installed.
- [x] **The three reviewers, re-authored for the owned loop** — read-only policies so an unattended run never blocks; every prompt says a report finding nothing is a complete, valid result; every schema closed at the top and ending in a required `human_summary`; `merge-request` in branch-review form (the diff plus ADRs from a collection through `search_documents`; acceptance criteria from the input when given, never invented), its forge-backed mode deferred with the forge target.
- [x] **56 new test cases** (1112 in all), green in both builds; a new check on the real binary, `cli.analyze_lifecycle`.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| Vendor-CLI backends under `analyze` | **Refused by type, with the reason** *(user decision)* | Those CLIs run their own tools outside Apogee's gate, so a `read-only` policy cannot hold there and the loop sees no tool call; allowing them under `tools: none` would be the forwarding this item retires. Same shape as `serve`. |
| Schema validation | `pboettch/json-schema-validator`, fetched and pinned *(default taken)* | It sits on nlohmann/json, already the project's JSON library; a hand-rolled validator would be a second draft-07. |
| Local models | Prompt-level JSON, not a grammar *(default taken)* | The pinned llama.cpp subtree carries no schema-to-grammar converter; the validator and the retry cover it, and a grammar mode is a later upgrade behind the same request field. *Superseded 2026-10-03 by 26f, below: a grammar wherever the model's template can hold the schema, and the prompt only where it cannot.* |
| Anthropic | The structured-outputs field where the model id says so, else one forced tool *(default taken)* | The forced-tool pattern is universal on the Messages API; the version parse reads both `claude-<family>-<major>-<minor>` and the older `claude-<major>-<minor>-<family>`, and an id it cannot read lands on the universal path rather than on a 400. |
| Saved filenames | `<base>-YYYYMMDD-HHMMSS.<ext>` *(default taken)* | No colon in any filename; Windows is a target. |
| `analyses/` | A layout row *(default taken)* | Declared, seeded and doctor-checked like every other; a lazily created directory would sit outside the parity check. |
| The bundled agents | **Compiled in**, files seeded skip-if-present, an entry of the same name overriding | A fresh `config init` runs `--agent security-review` before any `check --fix`; a user's edits survive an update; a pinned model is one entry, not a copy of the prompt. |
| An agent's MCP servers | `mcp[]` names them; empty means **none** | An agent is a curated workflow; dialling every configured server for a report generator would be noise and child processes for nothing. |
| Gemini JSON mode with tools | Sent only on a tools-less request | The API refuses `responseMimeType: application/json` with function calling; the prompt carries the schema on every request and the validator is the check. |
| The review note in `chat` | The transient prefix, never the transcript | `/branch` changes it between turns, and a resumed session must get what its own flags say. |
| `agents edit` | The user's `$EDITOR` in the foreground, stdio inherited | The one deliberate exception to the captured-stderr rule, recorded in that section: the editor is the user's own program, opened at their request, while no turn is running. |
| The uninstall plan | Unedited bundled files are Apogee's, not the user's | A fresh install's seeded prompts must not read as user data; an edited one must. |

**Verified on the real binary.** `cli.analyze_lifecycle`: `--list` with no backend; `--branch feature` against a fixture repository reviewing `main...feature` from the flags while the input text names another branch, the branch left un-checked-out, the report saved under `analyses/security-review/` with no colon in its name; the rendered report with `## Human Summary` as its last heading, printed when piped; an ad-hoc `--prompt` run printed and not saved; the validator's retry delivering the corrected answer, and two misses delivered raw with `"conforms": false` and a stderr warning; a `claude-cli` backend refused naming the type; `agents create` runnable at once and visible to the doctor, `delete --purge` clean; and `chat --tools --branch` reviewing the same diff, the note visible to an echoing mock on the turn and gone on a resume without the flag.

**Guardrails, each mutation-tested (41 mutations: 39 caught outright, 2 caught after their tests were strengthened).** `human_summary` not moved last; unparseable input dropped rather than returned; `conforms` reported true regardless; no corrective retry, or a retry with no correction message; prose around the JSON not tolerated; a read-only policy keeping writing tools; `none` still dialling servers; the policy not applied over the registry; an unknown server name not reported; `/branch clear` and `base..head` misparsed; `git_log` ignoring the review defaults; the live review not read at call time; `strict: true` on OpenAI; Gemini's JSON mode sent beside tools, or its schema sent unsanitised; Anthropic's native field on every model, `tool_choice` forced under thinking, the by-name choice never used, `Stop` not set after the fold, an ordinary turn's calls emptied by the fold, the beta header dropped; a re-seed overwriting an edit; a config entry not overriding a bundled agent, or matched case-sensitively; `save_subdir` not defaulted; a duplicate agent not refused; `questions` written when false; `tools` not written; a bad `tools` value silently accepted; the quiet-save rule printing a saved report; a colon in the filename; a vendor CLI accepted; the review note dropped from chat; the `:json` placeholder not expanded; a missing file a warning rather than a failure; the uninstall plan counting seeded files as user data; `?purge` ignored; `bundled` not reported.

**The two survivors, and what each taught.** Removing the fold's presence check survived because the existing tool-call test asserted the calls and not the words: without the check an ordinary turn kept its calls and lost its text. The test now pins a turn with prose *and* a real call. Disabling the "prompt file already exists" refusal survived because the duplicate test collided on the config entry too, which refuses on its own; the check exists for a file with no entry — a seeded or edited bundled prompt — and the test now proves `agents create security-review` will not clobber one without `--force`. A mutant that survives is usually a property the test never stated.

**A lesson recorded.** The first version of `fold_structured_output` moved every tool call out of the response *before* checking whether a structured call was present, so an ordinary turn — a real `search` call — came back with empty names and ids. The existing "tool calls arrive as structured IR tool calls" test caught it on the first full run, and the fix is a presence check before anything moves. A function that touches its input before deciding whether it applies is a function that breaks the callers it was never for.

### 2026-10-03 — `local-structured-output` (backlog item 26f): a grammar holds a local model's answer to its schema

**Why.** Structured output has worked on every provider since the section above. A local model, though, could only be *told* the schema: the pinned llama.cpp had no converter Apogee could reach. So the schema went into the prompt, and the validator and its one retry did the rest. Small models miss that way, and a miss can survive the retry. On the corpus below, Llama 3.2 3B, the extraction role in the user's own config, gave three of twenty capture records a `discipline` of `infrastructure`. That is not one of the five values the schema allows. When corrected, it gave the same answer again, so all three captures failed. In graph extraction, 8 of its 24 calls failed even after their retry. Since then, 25b had linked llama.cpp's `common` chat layer, which turns a schema into a grammar for the model's own format.

**What was built**

- [x] **The model's own template holds the schema.** A request with a `response_schema` and no tools is first rendered through llama.cpp's chat layer with the schema: `json_schema`, which is llama-server's `response_format`. The grammar this produces applies from the first token (it is not lazy). It allows the format's reasoning block first, then holds the whole answer to the schema token by token: every field, type, `enum` and `pattern`.
- [x] **Advanced past the reply's opening.** The grammar for a format starts at its assistant header, which the prompt already contains. So the grammar is fed the generation prompt before the first sample (`SamplingGrammar::prefill`), as llama-server does. Without this, the grammar made the model write the header a second time, and the reply no longer matched its own format.
- [x] **Compiled while the prompt can still change.** The grammar is compiled and advanced past the opening while the prompt is being rendered, so one that fails is caught while the schema can still be stated instead.
- [x] **Stated once.** While a grammar holds the answer, the backend adds nothing to the prompt. Every structured caller already ends its system prompt with an OUTPUT FORMAT block of its own (`schema_instruction`). That block is the caller's prompt and is left alone: it is the one place the fields' meanings reach the model.
- [x] **The fallback, said once.** Some cases fall back to stating the schema in the prompt, as before:
  - the model has no template;
  - its format has no place for a schema (Kimi, Functionary, GigaChat, Ling, Muse);
  - the converter cannot express the schema, such as an unresolved `$ref` or an invalid `pattern`;
  - the grammar does not compile.

  Each reason is said once: as a notice where the surface shows notices, and as a line in the operational log, since the clerks and the extractor show none.
- [x] **Tools first.** A grammar over the whole answer leaves no room for a tool call. So a turn with tools keeps its tool-call grammar and states the schema, the same rule Gemini follows in the section above. The clerks, the extractor and `refine` have no tools, so they are held from their first request. An `analyze` agent with tools is held only on the loop's final pass, which has no tools.
- [x] **A thinking model still thinks.** The template's thinking switch is left as the request set it, and the reasoning reaches the thinking sink as on any turn.
- [x] **The author's order.** `run_structured` gained an overload that takes the schema's own text and sends it on the request as written. The clerk, the extractor and `analyze` use it. A grammar writes the properties in the order the text lists them, but Apogee's JSON type sorts keys alphabetically when parsed. Without the overload, the clerk would have written its `decision` before the `intent` its schema puts first.
- [x] **The mock echoes it.** In a scripted mock's answer, `{{response_schema:json}}` becomes the schema the request carried. That is how `cli.analyze_lifecycle` checks that `analyze` sends the schema file as written.

**On real weights** (Q4_K_M weights). The test was the production clerk and extractor, run through a probe that counts attempts, over a corpus of twenty short meeting transcripts that each end in a decision, ingested as one collection of twenty chunks. "Before" is the last commit's build:

| Model | Capture, valid on the first try | Graph extraction |
|---|---|---|
| Llama 3.2 3B, before | 17/20; the other 3 failed even after their retry | 24 calls: 11 retried, 8 failed |
| Llama 3.2 3B, **after** | **20/20** | **20 calls, none retried, none failed** |
| Qwen3-VL-8B, before | 20/20 | 20 calls, none retried |
| Qwen3-VL-8B, **after** | **20/20** | **20 calls, none retried** |

- The three records that failed before (Jenkins to Actions, cron to Airflow, DKIM) were valid on the first try: the grammar admits only the five disciplines.
- **The grammar alone writes the schema.** The schema was given to the model nowhere but the grammar: two invented field names, one with a `^[A-Z]{3}-[0-9]{4}$` pattern. Qwen3-VL-8B, Gemma 4 12B, gpt-oss-20b and Llama 3.1 8B each answered with exactly those fields, in that form.
- **A thinking model still thinks.** Gemma 4 12B and gpt-oss-20b reasoned first (667 and 525 bytes, to the thinking sink) and then wrote the JSON. Qwen3.8-27B did too, asked a word problem under a schema: it reasoned, then answered 205 minutes on the first try.
- **Timings are not comparable.** The runs shared the GPU in different combinations before and after.

**Found on the way, and settled.**
- **The prefill.** llama-server feeds a format's grammar the reply's opening before it samples. A grammar applied without that step demanded the header again: `<|im_start|>assistant` on Qwen, `<|start_header_id|>assistant<|end_header_id|>` on Llama. Found by reading `common/sampling.cpp`, then confirmed on real weights with the step removed.
- **Alphabetical keys.** Apogee's JSON type sorts object keys, so a schema re-serialized before it reached the grammar would have reordered every answer. Fixed for the local path with the text overload. The cloud wires re-parse the schema the same way and still send it sorted; that is left as it is, though OpenAI's structured outputs also write keys in schema order.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| When the grammar applies | Whenever a local request has a `response_schema` and no tools *(default taken)* | There is no reason to leave a local structured request unconstrained. The tools exception is the loop's rule for every provider whose JSON mode cannot share a turn with tools. |
| A schema no grammar can hold | Falls back to the prompt statement with one note per reason, never an error *(default taken)* | The validator still guards the answer. |
| The prompt statement | Dropped while a grammar holds the answer; a caller's own OUTPUT FORMAT block untouched | The spec's "stated once": the grammar is the second statement. A caller's block is its prompt, and it carries what the fields mean. With the schema stated nowhere, Llama 3.1 8B filled in two invented fields that were right in form and wrong in substance. |
| Where the schema's text comes from | The author's own text, through a `run_structured` overload | A grammar fixes the order the properties are written in, and the author chose that order. |
| When the grammar is checked | While the prompt is being rendered: compiled and advanced past the opening | A grammar that failed only at the first sample would leave the answer held by nothing: the prompt was already sent without the schema. |
| Prefill | The template's generation prompt, and only for a grammar that is not lazy | llama-server's rule: a lazy tool-call grammar starts at its trigger, not at the header. |
| Qwen3.8-27B in the acceptance runs | **Left out** *(user decision, 2026-10-03)* | It thinks before every answer, and a twenty-item run took hours. It is left out of real-weights tests until thinking control (26i) can set its level. It had passed 8 of 8 captures on the first try when stopped. The acceptance models are Qwen3-VL-8B, plus Llama 3.2 3B, the user's own extraction model. |

**Guardrails, each mutation-tested (17 mutants, all caught on the first pass; 16 against the whole unit suite in a git worktree, one by `cli.analyze_lifecycle` on the real binary).** What they covered:
- **The grammar:** dropped altogether; held on a turn with tools; the thinking switch turned off under a schema.
- **Stated once:** the schema stated beside a grammar; the "already stated" guard removed; the fallback left unstated; the checkpoint prefix rendered with the statement, which loses the last-user checkpoint.
- **The note:** said on every call, never said, without its reason, or without the model's name.
- **The author's order:**
  - the text overload re-serializing the schema, or validating against nothing;
  - the clerk, the extractor or `analyze` sending the parsed object;
  - the mock not echoing the schema.

The prefill, which lives only in the llama build, was checked on real weights instead: removing it put the assistant header into every reply.

**Not verified.**
- Linux and Windows.
- The families whose formats have no place for a schema were not run on real weights. Their fallback is tested over the scripted runtime.
- DeepSeek's template renders the schema into the prompt itself, so a caller's OUTPUT FORMAT block states it a second time there. No DeepSeek model was run.
- An `analyze` agent with tools is held only on its final pass, and most answer before it.
- Qwen3.8-27B over the corpus (see the decision above).

## Milestone Y — The knowledge layer

**Goal.** Capture the *why* behind a decision at the moment the idea is formed — before it evaporates into a ticket, a design file, or a commit with the rationale stripped out — as one canonical record in an ordinary collection: findable with no model at all, archived rich and indexed thin, produced identically by the command line, a live chat, a saved session and the control plane, and edited without ever re-embedding.

### 2026-09-13 — `knowledge-records`: the canonical record, the capture clerk as one structured-output call, and `/capture` from chat

**The shape it inherited.** The retrieval matrix (Milestone S) had put a chunk store with a lexical floor, vectors with a per-collection binding, and the one ingest resolver under the user's spend rule in place; Milestone X had made structured output a property of every provider — the schema on the request, validation client-side always, one corrective retry — and the config editor, the layout declaration and the admin plane all existed. This slice took the day. Here the clerk is `agentloop::run_structured` — one provider-native call, validated on every backend, a second miss a failed capture rather than a stored guess.

**What was built**

- [x] **The canonical record** (`knowledge/record`) — the full schema: `intent` (the load-bearing field, in the participants' words), `decision`, `status ∈ shipped|rejected|superseded` (the branch marker — a brainstorm is mostly roads not taken), `discipline`, `downstream_link` (the write-time link), `provenance{source, attribution}` (two fields, so names can be stripped without severing the chain), `raw_ref`, `timestamp`, `supersedes`; `normalize` (trim, synonyms onto the canonical statuses, `manual` as the default source), `validate` (an intent required, a canonical status), `index_text` (intent, then `Decision: …` — never attribution, never the mutable link or status, so an edit can never make a stored vector stale), `anonymize` (names off, chain on), ids `kr-<UTC second>-<6 hex>` that sort by time, and `record_from_metadata`, the ONE decoder of a chunk's record: strict, keyed by its own id, an intent present.
- [x] **Schema v3 on the chunk store** — a nullable `chunks.metadata TEXT` column added on the one-transaction migration path; `replace_source` with per-chunk metadata, `update_metadata` (the metadata alone: the text, the vector and the FTS index stay), `chunks_with_metadata`, `chunk_by_id`, `chunk_vector` (so a test can hold an edit to "the bytes did not move"), and every search hit carrying its chunk's metadata. Never indexed: the FTS triggers do not know the column exists.
- [x] **The capture clerk** (`knowledge/clerk`) — its prompt and schema **compiled in** (byte-identical to `assets/clerks/`, a test enforcing it; not seeded — a fixed system concern, not a user-editable agent, and so no install-parity surface), the OUTPUT FORMAT block worded by the same `schema_instruction` every agent's persona ends with; `run_capture(ClerkFn, raw, overrides)` — the clerk arrives as a function, so every test of the capture logic runs model-free — applying each override to its field and only its field, then normalising and validating; `make_structured_clerk` binding a harness and a model to `run_structured` at temperature 0.2, a 2048-token budget, no tools, a throwaway history, and **a side request** — `agentloop::Options::side_request`, new, so a local backend runs the clerk on its own context and a live chat's cache is untouched.
- [x] **The store** (`knowledge/store`) — one record is one chunk keyed by the record id, its text the thin index and its metadata the whole record; the raw conversation archived first under `knowledge/raw/<id>.md` (a new **layout row**, private, user data — declared, seeded, doctor-checked, never exported) with `0600` on the file and `0700` on the directory, and `raw_ref` set; `put` replace-by-source so re-capture is idempotent; `list` newest first skipping chunks that are not records; `set_link`/`set_status`/`supersede` as metadata rewrites that leave the vector's bytes untouched; `remove` taking the archive with it.
- [x] **The capture core** (`commands/knowledge_core`) — `decide_store` (the collection's pin, the embedder through the capability probe, `resolve_ingest_retriever` under the spend rule), `draft_capture` (the clerk, then an id-less draft), `store_record` (mint what is missing and keep what is given, embed per the decision, archive, write, flip a superseded record, register the collection under `embeddings:` through `append_embedding` — the same write `embed ingest` makes, reported and never fatal), and `capture_and_store`, which refuses a resolver's refusal **before** the clerk runs so an impossible explicit ask never costs a model call. Every surface calls it.
- [x] **`apogee knowledge capture`** (alias `kn`) — `[TEXT] [--input <file>] [--from-chat <id>] [--status] [--discipline] [--source] [--link] [--supersedes] [--retriever] [-m] [--db] [--dry-run] [--json]`; input precedence positional > `--input` > `--from-chat` > piped stdin; the clerk's backend through the **extraction role** (`-m` > `models.default_extraction` > `models.default`), a vendor CLI refused by type before anything is spawned; `--dry-run` runs the clerk and every override exactly as a real run and prints the record and `Would store in "<db>" (retriever: <r>)` — zero footprint, proven by a helper that reads the collection, the archive and the config's bytes before and after; `--json` as `{draft, record, db, retriever[, warning][, note][, notes], registered}`; a clerk that never conforms exits 2 with the validator's words and stores nothing.
- [x] **Chat** — `/capture [status|link]` distils the live conversation through the same core with the **loaded model as the clerk** (no second backend, no second load), the argument a status when it is one and a link otherwise, `source: chat`; `knowledge.auto_capture: true` distils the session on a clean exit — `/exit`, `/quit`, the end of the input — and never on an interrupt: `LineReader::interrupted()` is new, reading replxx's `EAGAIN` for Ctrl-C. `logger::transcript_text` renders the participants' turns and nothing else: no system prompt, no tool result, no empty assistant turn. `capture --from-chat` renders a saved session the same way.
- [x] **The `knowledge:` section** — `auto_capture` (off by default: a generation call per session, and most chats carry nothing worth keeping) and `db` (the collection; a plain name, refused at load otherwise), documented in the template, hand-edited like `auto_rag`.
- [x] **The control plane** — `POST /v1/admin/knowledge/capture`, the twin, on the inference plane's own harness with the clerk's backend resolved as a chat request's `model` is (served backends only; `501` when the server serves none; `502` when the clerk failed twice; `400` for everything the CLI refuses), and `POST /v1/admin/knowledge` storing a finished record without the clerk — the store step a review UI needs, minting an id and a timestamp when absent and keeping them when given. `HttpError` became public so the admin plane can catch what `resolve_served` throws; `Handler::harness()` is the accessor. Both in [http-api.md](../reference/http-api.md).
- [x] **The doctor's `Knowledge` section** — the collection counted with its text index verified (or "no records yet"), the raw archive private (`--fix` tightens it) or "none archived yet"; `knowledge/` is guarded by `harness.layering` with its own allow-list (`embedstore/`, `agentloop/`, `agent/`, `harness/`, `platform/`, itself — never a surface); `knowledge capture` is a twin in the parity table.
- [x] **46 new test cases** (1158 in all), green in both builds; a new check on the real binary, `cli.knowledge_lifecycle`.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| The clerk | **One `run_structured` call, validated on every backend** *(consumed decision)* | Every backend, not one; a record is not a report, so a second miss is a failed capture rather than a stored `conforms: false`. |
| Where the full record lives | **A nullable `metadata` column, schema v3** *(default taken)* | Archive rich / surface thin needs one place the whole record sits beside its thin index; a sidecar file would be a second store to reconcile. |
| The raw archive | **A layout row** (`knowledge/`, private, user data) *(default taken)* | Declared, seeded, doctor-checked like every other; a lazily created directory would sit outside the parity check. Never seeded with content, never exported. |
| The clerk's backend on the CLI | **The extraction role** (`-m` > `models.default_extraction` > `models.default`) | A capture is structured extraction, so a cheaper extractor configured for that role runs it; chat's `/capture` uses the model already loaded. |
| The clerk's prompt and schema | **Compiled in, not seeded** | A fixed system concern, not a user-editable agent, and so no install-parity surface; byte-matched to the shipped files by a test. |
| Vendor-CLI backends as clerks | **Refused by type**, before anything is spawned | The same rule `analyze` and `serve` apply; the claude CLI's `--json-schema` path is a later wiring behind the same request field. |
| A resolver refusal | **Before the clerk**, in a real run; a warning in a dry run | An explicit vector ask with no embedder is knowable up front; spending a model call first would be waste, and the dry run exists to preview. |
| The default collection | `knowledge`, `knowledge.db` overriding, `--db` per run *(default taken)* | Separate collections per team or discipline stay one flag away. |
| Clerk temperature and budget | 0.2 and 2048 tokens *(default taken)* | Extraction, not creativity; an untuned local model that misses its stop token would otherwise decode toward its context limit. |
| `--from-chat` and `/capture` | User and assistant turns only *(default taken)* | Tool results and thinking are neither the participants' words nor the reasoning. |
| A clerk that fails twice | Nothing stored, exit 2 with the validator's message *(default taken)* | A record with no intent is worse than no record. |
| `auto_capture` and Ctrl-C | Fires on `/exit`, `/quit` and end of input; not on an interrupt | A user who hit Ctrl-C did not ask for a model call; replxx sets `EAGAIN` on an aborted line, which is what `interrupted()` reads. |
| A missing `--supersedes` target | The record is stored; the miss is a note | The lineage the user stated is worth keeping even when the old id was mistyped; losing the capture over it would be the wrong direction to fail. |

**Verified on the real binary.** `cli.knowledge_lifecycle`: `check --fix` seeding the `knowledge/` row; `--dry-run` printing the draft and the store decision with the collection absent, the archive absent and the config byte-identical afterwards, and `kn` as the alias; a capture stored with `Captured kr-…`, registered under `embeddings:`, its raw conversation archived under a `0700` directory, findable by `embed query knowledge` by its reasoning and **not** by the attribution; every override winning over the clerk and `--supersedes` recorded, a missing target a note; the doctor counting three records with the index ok and three private conversations; a `claude-cli` backend refused naming the type; a clerk that never conforms refused with nothing stored; `/capture rejected` in a piped chat storing a fourth record; and `capture --from-chat` over that session marked `chat` with the archived transcript opening `User: …`.

**Guardrails, each mutation-tested (52 mutations: 50 caught outright, 2 caught after their tests were strengthened; one more was equivalent and removed by simplifying the code).** The status not normalised; the default source dropped; an empty intent accepted; attribution in the index text; `anonymize` keeping the name, or the JSON always writing it; the decoder ignoring an id mismatch; each override ignored; a non-conforming clerk answer accepted; a clerk-given id kept; the clerk's temperature dropped, or the call not a side request, or the OUTPUT FORMAT block missing; the archive never written, or not private; the listing oldest first; `remove` keeping the file; an edit re-putting the chunk instead of rewriting the metadata; `supersede` setting the wrong status; metadata never stored, or an update touching the vector, or the listing returning every chunk, or the migration skipped, or a hit dropping it; a dry run storing; a vendor CLI accepted; `--from-chat` not marked `chat`; a status override not normalised; a backend failure exiting 1; the registration skipped; the supersede not applied; the vector never embedded, or its model not recorded; the default collection misnamed; the refusal after the clerk; the raw not archived by the core; a given id overwritten; the `501` skipped; `raw` not required; a clerk failure a `400`; the finished-record route skipping validation; the transcript including the system prompt; `knowledge.db` and `auto_capture` not parsed; auto-capture never firing; `/capture`'s argument never a status; chat's capture not marked `chat`; the doctor not counting, or ignoring a world-readable archive; the side-request flag not on the request; the layout row missing.

**The two survivors, and what each taught.** Accepting a non-conforming clerk answer survived because every non-conforming fixture also failed `validate` — prose, or JSON with no intent — so the schema check and the record check agreed on every case the suite had; the new case is valid JSON with an intent, a canonical status, and one key the closed schema forbids, which only the schema refuses. Keeping a clerk-volunteered id survived because a conforming answer can never carry one (the schema is closed), so the clearing is defensive; `draft_record` is now tested directly with an id, a timestamp and a `raw_ref` that must not survive it. A third mutant — the command normalising `--status` before handing it to the core — was **equivalent**: `draft_record` normalises every override anyway, so the second normalisation was a second path, and the fix was to delete it rather than to test it.

### 2026-09-19 — `knowledge-query-lifecycle`: records back out, kept true, shared, and the stateless review flow

**The shape it inherited.** The first slice had put the record, the clerk and the store in place, with every capture path going through one core. What was missing was the other direction — getting records back out through the same retriever rules every other surface obeys — and the two things a record needs over its life: edits that never touch its vector, and a way to share it with the names off and the chain on. And the flow a GUI actually needs between the clerk and the store: a stateless three-step contract (its own route, 2000 characters, the store step the existing route). Built here as specified, with the clerk's revision pass on the same `run_structured` seam as capture.

**What was built**

- [x] **`knowledge/query`** — `query(store, harness, config, collection, options)`: the collection's pins, the embedder through the capability probe, and **the one resolver** (`resolve_turn_retriever`) deciding lexical, vector or hybrid from the same facts `embed query` and every chat turn read — an explicit vector ask with nothing to run it a hard error naming lexical, hybrid without a vector half run and *reported* lexical, a vector pin that cannot run an exclusion with a note, a model mismatch a demotion with the re-ingest hint. **Rank everything, then filter, then cut**: the whole collection is ranked (the lexical search learned that a limit of 0 means every match), `filter_hits` drops off-branch and off-discipline records and chunks that are not records at all, and only then is the list cut — so six rejected records that match hardest never starve the one shipped record on the default branch. **Defaults to shipped.** The judge sees the same immutable index text the retrievers matched, and `reranked` is set from the same place as the ordering. `ScoredRecord` carries the chunk id as a storage handle off the wire, the seed the graph walk uses next.
- [x] **`knowledge/refine`** — the refine prompt compiled in (byte-matched to `assets/clerks/refine_prompt.txt`), the OUTPUT FORMAT block and the capture schema shared through the one `with_output_format`, so both passes ask for one shape and a refined draft is storable through exactly the path a captured one is; `refine_user_message` rendering `CURRENT DRAFT` (the six clerk-owned fields and nothing else — `id`, `raw_ref`, `timestamp` and `supersedes` never reach the model), `REVIEWER INSTRUCTION`, and `RAW CONVERSATION` or an explicit "not supplied — do not invent" line; `validate_refine_instruction` (non-empty, at most 2000 **codepoints**) and `run_refine`, whose guards fire before any clerk call, which carries `supersedes` through untouched and keeps a `provenance.source` the clerk dropped rather than letting `normalize` default it, and whose non-conforming revision is a failure, never a partial.
- [x] **`knowledge/export`** — `export_records(records, strip_names)`: the attribution stripped AND the machine-local `raw_ref` cleared (it leaks the local username and is useless to a recipient) while `source`, `downstream_link` and `supersedes` stay; `render_markdown`: a report grouped shipped, rejected, superseded, then other, a heading per record and a bullet per set field, the attribution line simply omitted when empty. Neither ever carries a raw conversation.
- [x] **The store's lifecycle** — `Store::reindex(embed, ids)`: every record or exactly the named ones, an unknown id refused before any embedding is spent, an embedder failure stopping the run naming the record; a put with an empty raw rewrites the text, the vector and the metadata and leaves the archive and `raw_ref` alone. `vectorless_count` for the mixed-collection disclosure.
- [x] **`apogee knowledge query|list|info|link|status|delete|export|reindex`** — `query [--status shipped|rejected|superseded|""] [--discipline] [-n] [--retriever] [--rerank] [--json]` printing `Top N result(s) … [lexical]` with every score on its retriever's scale, and telling a user who found nothing on the default branch how to search every branch; `list` newest first; `info --raw` printing the archive; `link` and `status` (synonyms folded) as metadata edits whose vector bytes a test reads back unchanged; `delete` taking the archive; `export [--format json|markdown] [--anonymize] [--status] [--discipline] [--out]`; `reindex [ID] [-m]` honest about a collection with no records or no vectors before any backend is built, warning on a mixed one, and recording the space the vectors now live in. A read never creates a collection out of a typo, and a config that will not load costs a read nothing but a warning.
- [x] **The control plane** — `GET /v1/admin/knowledge` (a missing collection an empty list, never an error and never a created file; `?q=` through the one resolver with `{object, data: [{record, score}], retriever, reranked[, note]}`, `501` for `?retriever=vector` with no embedding backend naming `?retriever=lexical`; `?anonymize=true` on both modes; no default branch over HTTP — a listing route shows what a client asks for), `GET`/`PATCH`/`DELETE /v1/admin/knowledge/{id}` (`PATCH` exactly one of `link` or `status`), `POST /v1/admin/knowledge/reindex` (`200 {reindexed: 0, note}` for a lexical or empty collection, `501` for vectors with no embedder), **`draft: true` on capture** (`200 {draft, record, db, retriever[, warning]}`, an id-less, timestamp-less record with no `raw_ref`, zero footprint), and **`POST /v1/admin/knowledge/refine`** (`{record, instruction, raw?, model?}` → `200 {draft: true, record}`, `400` for a missing record or a bad instruction *before* the clerk, `501`, `502`). The literal knowledge paths are matched before the `{id}` rows, so `capture`, `refine` and `reindex` are never read as record ids.
- [x] **The round trip, proven** — draft → refine (a no-op instruction) → the ordinary finished-record `POST` lands a record field-equivalent to a one-shot capture of the same conversation (chunk text and archive included), with the clerk run exactly twice and the footprint — the collection, the archive, the config's bytes — unchanged after the draft, unchanged after the refine, and changed once after the store.
- [x] **Parity**: `knowledge link`/`status`/`delete`/`reindex` are twins in the table; `query`/`list`/`info`/`export` read-only; refine has no CLI form by design. Every route in [http-api.md](../reference/http-api.md), pinned by `cli.http_api_conformance`.
- [x] **23 new test cases** (1181 in all), green in both builds; `cli.knowledge_lifecycle` extended on the real binary.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| The refine loop | **Stateless, HTTP-only** | The draft is the client's; the store step is the existing finished-record route, so the round-trip equivalence is a test rather than a hope; a CLI user iterates by re-running `--dry-run`. |
| `query`'s default branch | **`--status shipped`** | The branch marker is the single most important guard in a brainstorm-based corpus. Over HTTP a listing route shows what a client asks for, and says so. |
| Filtering | **Rank everything, then filter, then cut** | A filter after the cut starves the result the moment the best matches sit on another branch; the lexical search gained an unbounded form (limit 0) to make that possible. |
| A metadata edit | **Never a re-embed** — the vector's bytes asserted unchanged across `link`, `status` and `PATCH` | The index is built only from the immutable reasoning, and only `reindex` rewrites vectors, only when asked. |
| `--top-k` | 5 on the CLI; 20 over HTTP, `?limit=` to change it *(default taken)* | A terminal-sized default; the HTTP number is a GUI page size and the judge's widened pool. |
| Markdown export | Grouped shipped, rejected, superseded, then other *(default taken)* | Shipped first, the branch `query` defaults to. |
| `PATCH` | Exactly one of `link` or `status` per call *(default taken)* | One edit, one intent, one audit line. |
| The instruction cap | 2000 **codepoints**, checked before any clerk call | A reviewer's note in any script gets the same allowance; the source material belongs in `raw`. |
| A dropped `source` on refine | The draft's value, not the default | `normalize` would otherwise rewrite a field the instruction never mentioned. |
| A read on a missing collection | The CLI refuses naming `capture`; HTTP lists nothing | Neither creates an empty database out of a typo; a GUI polling an empty layer must not see an error. |

**Verified on the real binary.** `cli.knowledge_lifecycle`, extended: `query` returning the shipped record on the default branch and not the rejected one, the rejected one on `--status rejected`, every branch under `--status ""` with the retriever named in the JSON envelope, an explicit vector ask refused naming lexical; `list` counting five; `info --raw` printing the archived session; `link` and `status` landing in `info --json`; an anonymized export with no name and no archive path but the chain kept, and a Markdown report with its status groups; `reindex` honest about a lexical collection; `delete` taking the archive with it and the doctor counting one fewer.

**Guardrails, each mutation-tested.** 45 mutations: 41 caught outright, 3 caught after their tests were strengthened (a schema miss whose JSON still reads as a record must fail the refine; the reindex binding is the reindex's own, proven on a store that had lost it; the refine guards answer `400` even on a server with no backend to run the clerk); one more was equivalent — the query's post-judge cut duplicated the filter's own, since without a judge the fetch limit IS `top_k`, and the dead branch is gone. The mutations: The status or discipline filter ignored; non-record chunks kept; the search cut before the filter; a resolver refusal swallowed; an excluded collection searched anyway; `reranked` claimed regardless, or the judge's order dropped; the retriever always reported lexical; the top-k cut skipped; the instruction guard after the clerk; `supersedes` not carried, or reaching the model; a dropped source defaulted; the cap counting bytes; the absent-raw line dropped; a non-conforming revision accepted; the refine prompt not the refine prompt; anonymize keeping the archive path or the name; Markdown groups unordered, or empty fields written; reindex re-archiving, embedding before refusing an unknown id, or miscounting the vectorless; a lexical limit of 0 returning nothing; the CLI's default branch every branch; a status edit not normalised; export never anonymizing; reindex skipping the binding, running on a lexical collection, or skipping the mixed note; delete keeping the record; a read creating the collection; `info --raw` never printing; the list route creating a collection; the vector `501` skipped; `PATCH` accepting both fields; a draft storing; the refine guards after resolution; a `502` as a `400`; the reindex `501` skipped; anonymize ignored on the list; `?db` ignored on a get; the `PATCH` route missing.

### 2026-09-19 — `knowledge-graph-build`: the graph over a collection, and what a retrieval turn cannot reach by resemblance

**The shape it inherited.** Two slices had put records in and got them back out through the one resolver. What retrieval still could not do is surface **connected context** — what a question is *about*, walked by exact SQL over extracted edges, so a document that shares no vocabulary with the query still reaches the request when the graph says it is related. Three lessons set its shape: a staleness fingerprint needs a second half, or a same-count re-ingest silently loses every decision node; expansion has to be seeded *before* the judge, so a verdict that keeps nothing cannot drop the graph; and generation needs a budget, because an untuned local model that misses its stop token on one chunk keeps decoding. All three are load-bearing here.

**What was built**

- [x] **The `kg_*` layer in `embedstore/` (schema v4)** — `graph.h/.cpp`: the tables and the entity FTS index created on open inside the one migration transaction, a dropped trigger self-healing; node identity by (normalised name, type), the first casing kept, descriptions merged first-non-empty, **a description change clearing the stored vector** so a stale embedding never outlives its text; `upsert_edge` corroborating (weight +1 per re-statement) against `ensure_edge`, the fact that stays at weight 1; mention dedup with the salience counter; the per-source fingerprint `(chunk_count, max_chunk_id, model)`; `reconcile_graph` pruning dead mentions and vanished sources, recomputing every count, dropping orphans and their edges in one transaction; `delete_graph`; `graph_meta`. `graph_search.h/.cpp`: stats, exact and full-text lookup through the same match-query guard as the chunks, neighbourhoods, supporting chunks, and **`graph_expand`** — seeds from the retrieved chunks' mentions ∪ hop-0 entity hits, hops clamped to 1..2 as plain per-hop queries, neighbours ranked by connecting weight × mention count, capped, one support chunk each, every relation among the traversed neighbourhood; chunk seeds never re-listed (their text is already injected), hop-0 seeds listed (nothing else carries their descriptions). The three translation units share `store_impl.h`, package-private, so the raw handle still never leaves the package. `kg_mentions.collection` and `kg_state.collection` carry the provenance column the named-graph item keys on; a collection's own graph writes `''`.
- [x] **Chunk ids are never reused.** Running the record lifecycle against the build found it: the chunk table's `INTEGER PRIMARY KEY` handed a deleted maximum straight back to the next insert, so a same-count re-ingest of a record's chunk produced *the same id* — and the fingerprint's second half, there for exactly that case, would have read the source as up to date. The table is `AUTOINCREMENT` since v4, an older store rebuilt in place with rowids preserved (so the external-content FTS index stays valid) and the sequence picking up past the highest id ever stored; the hand-built v2 fixture migrates through it.
- [x] **`graph/extract`** — its prompt and schema compiled in (byte-matched, not seeded); the closed set of eight types with **`decision` never among them**; `normalize` enforcing in host code what the schema can only request — types, the 12/16 caps, empty names, duplicates merged, self-loops, endpoints resolving only to surviving entities, duplicate relations — and `make_structured_extractor`: **one `run_structured` call** at temperature 0.2 under the 2048-token cap, no tools, a throwaway history, marked a side request, the one OUTPUT FORMAT wording every structured caller ends with; a non-conforming answer after the one correction a failed outcome, never a partial.
- [x] **`graph/build`** — reconcile → **materialise records** → plan by fingerprint → extract stale sources chunk by chunk with one retry → a state row after a complete file only → embed mutated nodes last. A limit or a cancellation stops between chunks and leaves the file unstamped; a failed chunk is counted, its file left for the next build; a failing embedder stops the phase and keeps the build, recording no model. Every knowledge record in the collection — recognised per chunk through the one decoder — becomes a **`decision` node**: name = record id, description = decision — intent clipped to 400 codepoints, status and discipline as metadata **replaced on every run** (so an in-place status edit, which churns no chunk, still shows), mentioned by the record's own chunk so the ordinary reconcile retires it with the record; **`concerns`** edges from the decision to every entity the extractor pulls from its own text — the link that lets a documents chunk reach the *decision* in one hop — and **`supersedes`** mirroring the lineage when the target is in the graph, a missing target counted and never a placeholder. A decision node embeds by description alone: the id carries no meaning.
- [x] **Retrieval-time expansion on every surface** — `agentloop/graph_context`: `[Knowledge graph: <name>]`, entity lines (`kr-… (decision, shipped): …` for a record, so a superseded rationale is never mistaken for the live one), then `A —[relation]→ B` triples, under a **1,500-codepoint budget with whole-line truncation** that never leaves a triple without its entity. `retrieve_for_turn` seeds it from the retrieval-ordered top-k **before the judge** and, on a lexical or hybrid turn, from the query's own terms through the entity index — so an entity-name hit expands with no embedder at all — then appends it after the chunk list in the same transient message; a judge that drops every chunk leaves the section framed on its own; a failing walk is a note, never the reason a turn loses its chunks. Every surface injects whenever there is a prefix, and reports `+N graph entities` — the status line, `analyze`'s note, and the `rag_result` meta-frame's `graph_entities`.
- [x] **`apogee graph build|stats|show|delete`** — the extractor `-m` > the collection's `graph.extract_backend` > the extraction role > the default, through the one role resolver; **a metered default refused** naming the three ways, a vendor CLI refused by type, an unconfigured name refused; entity vectors under the embedding spend rule (the collection's embedder when unmetered or pinned to vector, else full-text only and said so); `--dry-run` printing every extraction with zero footprint; failed chunks printed as they happen; the first success registering an unregistered collection and writing **`graph.enabled: true`** through the config editor, the same bytes the admin twin writes; a summary with `Records as nodes: N decision node(s), M supersedes edge(s)`. `show` resolves exact then closest, groups relations by verb with `x<weight>` corroboration, and points a decision node at `knowledge info`.
- [x] **The control plane** — `POST /v1/admin/graph/{id}/build` as the plane's **first async job** (`202 {job_id}`, progress as `admin.job.*`, the counts on the record, cancel between chunks) on new `JobWorkers` whose destruction cancels every token and then joins, so no worker outlives the plane; the CLI's chain and refusals with the served set on top (`400` for a metered default, a vendor CLI, an unserved name; `404` no data; `501` no generation backend); `GET …/stats` zeros for an unbuilt graph, never an error; `GET …/entity?name=` exact then fuzzy with `also_matched`; `DELETE …`; `PUT /v1/admin/embeddings/{id}/graph` as the twin of the auto-enable write; and **`?graph=true`** on `GET /v1/admin/knowledge`, the twin of **`knowledge query --graph`**, both through one `graph_for_records` core — the note only when no graph covers the collection, so "no graph" and "nothing related" stay distinguishable.
- [x] **The fact behind the cost policy** — `LLMProvider::generation_is_metered()`, default **true** (unknown is metered), the API-billing providers saying yes explicitly, the mock and llama.cpp no; `Harness::generation_is_metered(model)` through the probe, true for an unroutable name. The config's `graph:` block (`enabled`, `extract_backend`, `hops` validated 1..2 at load, `max_entities`) on `EmbeddingConfig`, a commented example in the template, `set_embedding_graph_enabled` scoped to `embeddings:` so a same-named backend is never touched, and the doctor's `Graph` section. `graph/` joins the layering guard with its own allow-list.
- [x] **61 new test cases** (1243 in all), green in both builds; `cli.graph_lifecycle` on the real binary.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| The extractor's temperature and cap | **0.2 and 2048 tokens** *(default taken)* | The cap bounds the worst case: an untuned local model that misses its stop token would decode toward its context limit on one chunk. |
| Entity vectors | **The collection's embedder chain under the embedding spend rule** *(default taken)* | Unmetered, or the collection pins `retriever: vector`; otherwise the graph is full-text searchable and complete. The same rule as ingest, decided once. |
| `graph show --chunks` | **3 by default** *(default taken)* | `--chunks 0` prints every one. |
| The build's scope | **A collection's own graph, single-store** | The `Member` generalisation and the named graph belong to the global item; the provenance columns are already in the schema so it needs no migration. |
| Chunk ids | **AUTOINCREMENT, an old table rebuilt in place** | The fingerprint's second half is only meaningful if an id can never come back; found by running the record lifecycle, not by reading. |
| Where the section rides | **After the chunk list, in the same transient message** | It augments the chunks and never crowds them out; with no chunks left it is framed on its own so a judge's empty verdict cannot drop it. |
| The HTTP build | **An async job on the plane's harness, with worker threads owned by the admin handler** | One generation call per chunk is minutes, not a request; the knowledge routes' precedent of running a clerk on the plane's harness is kept, and shutdown cancels before it joins. |
| Metered-ness | **A provider fact, default true** *(the item's recorded decision, kept)* | Never a type list; a new backend is assumed to cost until it says otherwise. |
| The `decision` type | **Storage-only, never in the schema's enum, dropped by `normalize`** | Prose that says "we decided X" can never forge a decision node. |

**Verified on the real binary.** `cli.graph_lifecycle`: a metered default refused before any provider call; `build --dry-run` printing every extraction with the config byte-identical afterwards and nothing stored; the build stamping both sources and writing `graph.enabled` with every byte above the entry untouched; `stats` and `show`; a second build a no-op; a `complete --rag` turn over two documents that share no vocabulary — the status line counting the graph entities while the answer carries the *other* document's entity and not its text; an entity-name query expanding with no lexical match; a captured record materialised as a `decision` node with its `concerns` edge, shown with its markers; `knowledge query --graph` and its JSON; a docs turn reaching the decision as `kr-… (decision, shipped): …`; `delete` clearing the graph and keeping the chunks.

**Guardrails, each mutation-tested.** 76 mutations: 63 caught outright, 11 caught after their tests were strengthened (a decision node's description change must clear its vector; a schema miss that still decodes must fail the extractor; a forced rebuild must keep the deterministic edges at weight 1; a walk the store cannot run must be a note with the chunks kept; a vector turn must seed from its chunks alone, on the RAG turn and on `query --graph` alike; the graph knobs must travel from the config into the turn; a metered backend named explicitly must be allowed, on the CLI and over HTTP, which took a scripted mock that can claim to be metered; entity vectors must obey the spend rule, which pulled that decision into one `resolve_entity_embedder` both surfaces call; the doctor must warn on an enabled graph that is not built; the `PUT` route must exist), 2 equivalent — the entity index's update trigger firing on every column changes what the index rewrites, never what it answers; and the `AUTOINCREMENT` on a fresh chunk table is masked by the self-healing migration, which a new mutant of its own holds — and one more removed as dead code: the expansion's hop-0 exclusion could never fire, because a seed is never a candidate. The mutations: a description overwritten, or its vector kept on a text change; corroboration missing, or a fact corroborating; a mention counted twice; reconcile keeping orphans or skipping the recount; a decision's metadata never refreshed; the id migration skipped; the expansion ignoring the cap, walking past two hops, or scoring without mentions; stats blind to the max id; the whitelist, the caps, self-loops, unresolved endpoints, duplicate relations, unmerged duplicates; the extractor not a side request, or without the format block; no reconcile; staleness blind to the max id or the model; a failed file stamped; no retry; a dry run storing; the limit or a cancellation ignored; an embed failure failing the build, or a partial embed claiming the model; a decision embedded by name; a `supersedes` placeholder; records not materialised; the clip by bytes; the budget ignored, no lexical seed, the decision marker dropped, triples after a cut; the graph skipped whenever a judge runs, or ignoring `enabled`, or dropped when the judge drops every chunk, or not reported; `describe_retrieval` silent; a metered default allowed on the CLI or over HTTP, a vendor CLI allowed, `enabled` never written, a dry run writing the config; the `query --graph` note swallowed; the build route skipping the served set, stats a `404`, the enabled write never landing, the `501` skipped, the entity route never fuzzy, `PUT` accepting a non-boolean, the graph flag ignored on the list route; hops not validated; the enabled edit writing the wrong section, or rewriting the line; the doctor ignoring an unconfigured extractor; an unroutable name unmetered, the mock metered.

### 2026-09-19 — `knowledge-graph-global`: the global layer, and a graph that spans collections

**The shape it inherited.** The third slice answered *local* questions: what a retrieved chunk is about, walked by exact SQL. Two things it could not do are the reason for this slice. "What are the main themes in this corpus?" needs *global* structure -- clusters, not neighbours -- and a per-collection graph cannot see across collection boundaries, so the same person, system or project mentioned in `docs`, `meetings` and `tickets` was three disconnected twins. The recorded scope answers were kept whole: precedence only, rebuild never absorb, CRUD in the config family, identity by exact member set, threshold 0.92. So was the core insight: every edge comes from one chunk's extraction, so cross-collection connectivity flows entirely through shared node identity, which is exactly what one build-time store makes real. And the provenance columns the third slice put in the schema for this item meant the storage generalised with no migration at all.

**What was built**

- [x] **Communities** -- `graph/communities`: **deterministic weighted label propagation** over the extracted relations (labels from node ids, ascending-id asynchronous updates, weighted-majority adoption with the smallest label winning ties, at most 20 rounds, edgeless nodes never joining, clusters of fewer than 3 dropped), no model in the detection; each new or changed cluster summarised by **one plain generation call** under a summariser prompt compiled in (byte-matched to `assets/clerks/community_prompt.txt`, no schema -- the output is prose); the summary stored as an **ordinary retrievable chunk** under `graph://community/<id>`, so a corpus-level answer surfaces through `embed query`, every RAG turn and every retriever with zero new query paths. **Identity is the exact member set**, stored verbatim as the sorted ids: an unchanged cluster costs nothing, a changed one is pruned and regenerated, `--force` regenerates all; a summariser failure is soft and leaves the old summary in place. Pseudo-chunks are graph output and never corpus input: excluded from extraction planning, staleness and coverage, and taken along by delete. The embed phase runs last over **every summary still without a vector**, not only this run's, so an earlier embed failure heals on the next run rather than needing a forced regeneration -- a failed summary never stays lexical-only for good.
- [x] **Dedupe** -- `embedstore/graph_dedupe`: union-find per type over pairwise cosine at a threshold (0.92); the earliest-extracted node survives, edges repoint to it (weights summed on a collision, would-be self-loops dropped), mentions union and recount, the description merges first-non-empty with the survivor's vector cleared on a text change, the merged nodes' community memberships removed (derived; the next communities run recomputes). Nodes without a vector are never considered, **decision nodes are never merged** -- two records with near-identical text are still two decisions -- it never runs on its own, `--dry-run` previews, and everything commits in one transaction.
- [x] **Named graphs** -- a `graphs:` entry (`collections`, `extract_backend`, `hops`, `max_entities`; **no `enabled` -- building is the enablement**), keyed by name like every other section and kept in file order because the precedence rule reads it first to last; `config add-graph`/`delete-graph` through `append_graph`/`delete_graph`, section-scoped end to end so a graph sharing an agent's name can never touch the agent. Its database is **derived data** at `<embeddings_dir>/graphs/<name>.db`, created by the first build and never by an installer -- not a layout row, unlike `knowledge/raw`, because it is rebuildable from its members with one command. `graph/build_multi` runs the one loop over `Member`s into the target: every mention and state row labelled with the member it came from, per-member state so a resume works across members, records in any member materialised, the graph's own name and as-built member set stamped into `graph_meta`; `reconcile_graph_multi` converges membership -- an ex-member's rows die, dead mentions are pruned per member through a cross-database `chunk_ids_existing`, a member whose database is gone reads as empty. **Rebuild, never absorb**: a member's own graph is neither consulted nor migrated, and never written to. A named graph embeds entities through the default chain and records its own model; members may use different chunk embedders freely.
- [x] **Retrieval precedence, decided once** -- `agentloop::resolve_turn_graph`: a **built** named graph covers its members (the first entry listing a collection wins a double-listing; an unbuilt one covers nothing), else the collection's own enabled block, else nothing; the member's block is left untouched and resumes the moment the collection leaves. Every surface -- the conversational turns through `retrieve_for_collection`, `knowledge query --graph`, the HTTP twins -- makes the same call. The turn opens the named graph's database for the walk alone, seeds it with the member's label and renders under the graph's name. Apogee retrieves one collection per turn, so a turn renders one section under the one budget, and there is no several-sections case.
- [x] **`apogee graph` resolves `<name>` graphs-first** (`check` keeps the two name spaces apart): `build` over a named graph's members with missing ones contributing nothing and a dry run planning against an in-memory store so its footprint is zero -- not even the file; `stats` per member with an as-built membership-drift note; `show` naming each supporting chunk's collection; `delete` removing the file. `communities [-m] [--force] [--min-size] [--list]` and `dedupe [--threshold] [--dry-run]`, against a collection or a named graph alike. The summariser resolves exactly as the extractor does, through the one chain with the metered fall-through refused. **`embed ingest --graph`** chains the covering graph's build after a successful ingest only -- the first `graphs:` entry listing the collection by config membership alone, since the first chained build is what creates the database, else the collection's own -- from a fresh config so it sees the entry the registration just wrote.
- [x] **The control plane** -- every `/v1/admin/graph/{id}/*` route named-aware; `POST …/communities` as the plane's second job kind (`graph-communities`), `GET …/communities`, `POST …/dedupe` synchronous; and the `graphs:` CRUD slice at `/v1/admin/graphs` with `built` on every response, the CLI's rules through one `validate_named_graph`, `409` on a POST collision -- an entry made over HTTP byte-identical to one made by the CLI. Forty-eight admin rows.
- [x] **The doctor** validates every `graphs:` entry: the collision ban and a missing member as failures, an unconfigured extractor, built or not with the build as the remedy. Schema v5 adds the two community tables on the same open-time path.
- [x] **52 new test cases** (1295 in all), green in both builds; `cli.graph_lifecycle` extended on the real binary.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| `--min-size`, the relation cap, the round bound | **3, 20 lines, 20 rounds** *(default taken)* | Label propagation converged in a handful of rounds on real graphs. |
| A named graph's community summaries | **Listable in its own database; not surfaced through member retrieval** *(default taken)* | Surfacing them would need a chunk-retrieval path over a graph database -- a new query path. Recorded, not built. |
| Louvain | **Not built** *(default taken)* | An upgrade behind `detect_communities` when label propagation proves insufficient. |
| The community identity | **The sorted member ids verbatim, not a hash** | `graph/` cannot reach the SHA-256 in `models/` under its layering allow-list, and a second hash implementation would be a second copy of something that exists once; the verbatim key is exact where a hash is merely very likely so. |
| The embed phase's scope | **Every summary still without a vector** | A failed embed on an unchanged community would otherwise stay lexical-only until a forced regeneration; healing on the next run costs one query. |
| A named dry run | **Plans against an in-memory store when the graph is unbuilt** | A dry run's footprint is zero, and "zero" includes the database file the first real build creates. |
| Sections per turn | **One** | Apogee retrieves one collection per turn; the precedence rule picks one graph for it, and the budget is shared by construction. |
| The HTTP ingest chain | **Deferred to the ingest route** | There is no ingest route yet (`embed ingest` is a recorded backfill of the embeddings data plane); `"graph": true` rides with it, through the same `run_graph_build` body. |
| The community tables | **Schema v5** | A version number names the shape; two new tables are a new shape, on the same idempotent open-time path. |

**Verified on the real binary.** `cli.graph_lifecycle`, extended: `communities` on the scripted summariser over the three-node graph (two entities and the decision) producing one summary that is the **top lexical hit** for "main themes", an immediate re-run reporting it unchanged, the summary chunk counted by `stats` as a community and never as a node; a graph named after a collection refused with the rule; a named graph over `notes` and `knowledge` reported by `check`, not created by `add-graph`, built into its own database with **both** records materialised and progress naming the member; `stats` with the per-member breakdown and one Atlas node with four mentions across two collections; `show` naming each chunk's collection; a notes-only `complete --rag` turn expanding under `[Knowledge graph: work]` and carrying the decision captured into the *other* collection, one hop from the shared entity; `dedupe --dry-run` finding nothing on the distinct-entity fixture; `config delete-graph` leaving the database with the collection's own graph resuming on the next turn, `graph delete` removing the file, and the collection's `delete` still keeping its chunks.

**Guardrails, each mutation-tested.** 80 mutations: 74 caught outright, 5 caught after their tests were strengthened (a node without a vector must stay out of dedupe even at a threshold no surface would pass; a tie in support must go to the smallest label, which needed a torn node whose cluster the tie decides; the summariser must be a side request with no schema; a failed ingest must not even attempt the chained build; under a hand-made name collision every subcommand and every route must resolve the `graphs:` entry first, which is what makes the doctor right to fail it), and 1 removed as dead code -- the explicit deletes of a merged node's mentions and memberships, which the schema's `ON DELETE CASCADE` already performs. One mutant was re-formed after its first shape proved a no-op (clearing a named graph's rows *and* removing its file is the file removal); its real form was caught. The mutations: a community replaced by key keeping the old row, or written without its pseudo-chunk; the prune sweep keeping everything, or taking the row and leaving the chunk; pseudo-chunks planned for extraction, counted as corpus, or left behind by delete; the heal listing every community; a dry run writing; the latest node surviving; weights not summed on a fold, self-loops kept, mentions not unioned, types ignored, the threshold a floor at zero, decision nodes considered, the survivor's vector kept on a text change; ex-member rows kept, a null member fully present, dead chunks never pruned per member, mentions or state rows unlabelled, member stats never summed, members recorded unsorted, expansion seeds ignoring the label; detection ignoring weight, the min size ignored, unchanged communities re-summarised, `force` ignored, a summariser failure stored as an empty summary, the relation cap ignored, the embed phase covering only this run, an embed failure failing the run, the key unsorted, prune skipped, edgeless nodes joining; members unlabelled in the build, state stamped under the wrong member, identity never stamped, a null member planned, records or reconcile from the first member only; an unbuilt named graph covering, the own graph winning, the named graph's knobs dropped, seeds unlabelled, the turn walking the own store, the header always the collection, the helpers dropping the named path, a case-sensitive member match; the collision ban skipped or blind to an on-disk collection, the chain targeting the collection, a named build enabling something on the CLI or over HTTP, a named dry run creating the database, the metered refusal skipped for the summariser, `graph delete` clearing rows instead of the file on the CLI or over HTTP, the doctor never failing the collision or ignoring a missing member, `delete-graph` writing through another section, the entry omitting its members, hops not validated at load; the communities route allowing a metered default or skipping the `501`, dedupe ignoring `dry_run` or accepting any threshold, the entity route dropping the collection, `POST /graphs` replacing instead of `409`, `PUT` ignoring a mismatched body name, the graphs routes skipping validation, the communities route missing.

## Milestone Z — The training track

**Goal.** Fine-tune local models on their full-weight SafeTensors files, end to end, on one orchestration shape -- a C++ orchestrator over Python trainer subprocesses -- with the execution that does not port kept behind one owned boundary. Three items, in build order: the floor (the Python boundary, datasets, kits), the run (train / eval / promote / rollback), and the orchestration layer (pipelines, regimes, the continuous cycle). Scheduled for v0.1.0 on 2026-09-19, the user's call; the same day the two tool kits were deferred to the in-text tool protocol as its own tools-track item.

### 2026-09-19 — `training-datasets`: the Python boundary, datasets, and kits

**What was built**

- [x] **The `training` layout row** (private, user data) -- `datasets/`, `datasets/raw/`, `kits/`, `scripts/`, `venv/` beneath it, created by seeding or first use, never restated as rows -- with accessors in `harness/layout`; the doctor, `check --fix`, both installers and the uninstall plan picked it up from the one declaration.
- [x] **Compiled-in training assets**, like the bundled agents: four kits (`instruction-following`, `structured-output`, `summarization`, `reasoning`) and `prepare_dataset.py`, in `harness/assets_training.cpp`, byte-matched to `assets/training/` by a test and **seeded skip-if-present** under `training/kits/` and `training/scripts/` by the one seeding path. `bundled_files()` is now the single list the seeder, the unmodified check and the doctor's drift row read; `is_unmodified_bundled_asset` answers for a directory whose every file is Apogee's, so a fresh install's `training/` does not read as user data at uninstall.
- [x] **The Python environment Apogee owns** (`training/python_env`): `training/venv/`, seeded from `training.python` or `python3` on PATH with `-m venv`, requirement sets (`prepare`, `mlx`, `peft`, `convert`) installed with its own pip and recorded in `apogee.json`, versions as floors. `apogee train setup [--trainer auto|mlx|peft] [--with SET]` creates it explicitly; a command that finds it missing asks on a terminal and on a pipe refuses naming the command. **Seeding never creates it** -- a fresh install downloads nothing unasked. The doctor's `Training` section: the environment and its sets, each seeded script against the shipped copy (an edit kept and shown, a missing one repaired by `--fix`), every installed kit, `paths.hf_dir`.
- [x] **The script runner** (`training/script_runner`): a driver under the environment's interpreter, stdout framed by the vendor-CLI framer (a named layering allowance, as `mcp/` has), every line classified -- `{"message"}`, `{"error"}` as its own event, a terminal record, and a non-JSON line kept as a message -- the exit code carried, stderr a bounded tail never inherited, cancellation terminating the child. Three silent gaps closed by construction: a reader that drops a line that does not parse, a progress struct with no `error` field so an error decodes as a blank tick, and a discarded exit status so a crashed trainer reads as success.
- [x] **`prepare_dataset.py`**, its presets (`alpaca`, `sharegpt`, `chatml`, `oasst`, `prompt-completion`) with auto-detection, `--map`, `--split`, `--as-eval`, `--flat` and the no-match refusal listing the columns -- **JSON, JSONL and CSV through the standard library**, so the common path needs nothing installed and its test runs on bare `python3`; the `datasets` library imported for Parquet only; the environment guards before the first ML import, grep-tested.
- [x] **Kits** (`training/kit`): the YAML shape -- `synth{system, seeds, count 200, per_seed 8, temperature 0.9}`, `train{iters, batch_size, num_layers}`, `eval[{prompt, expected}]` -- validated (a non-blank prompt, at least one eval item), listed sorted with a broken file named rather than hidden, found by name or path, the inline suite materialised one object per line.
- [x] **The synth core** (`training/synth`), model-free: a contract prompt appended to the kit's, batches of `per_seed` with the seeds cycled, the array extracted through prose and fences, the field aliases, case-insensitive prompt de-duplication, the call bound `(target/per_call + 1) * 3 + seeds`; **and the two upgrades the placeholder promised** -- batches in flight in parallel, and a failed batch retried with exponential backoff (five times, capped at a minute, on top of the transport's own `Retry-After` handling) before it is skipped. A cancelled run hands back nothing it produced past the stop.
- [x] **`apogee datasets prepare|create|synth|kits|list|info|delete|pull`** and `apogee train setup`. `create --from sessions` mines the persisted chats -- one line per completed exchange with both sides non-empty, an assistant turn that only called tools waiting for the answer that follows, `--backend`/`--since`/`--until` filters -- and needs no consent key (the unattended source of the cycle item does). `synth` names its teacher (`--teacher`, required; a vendor CLI refused; naming it is what satisfies the metered rule), runs an API teacher with `--parallel` batches (default 4) and a local one serially. `pull` downloads a Hugging Face **dataset** into `training/datasets/raw/` through the ladder.
- [x] **The dataset store** (`training/datasets`): `<name>.jsonl`, one writer through a temp file and a rename, an existing dataset refused without force, `role` before `content` on every line so the CLI, the admin twin and the Python script agree byte for byte; listing with shapes (`chat`, `flat`, `eval`, `mixed`, `empty`).
- [x] **SafeTensors snapshots and dataset downloads** -- what the models item left unbuilt. `models pull <owner>/<repo> --safetensors` takes the whole full-weight repository (shards, configuration, tokenizer, custom code) into `paths.hf_dir` or `models/<owner>--<repo>/` through **`acquire_tree`**: every file into a `.staging` tree via `acquire_file` (the ladder with the GGUF rung skipped and the sidecar saying so), the directory renamed into place last, a failure anywhere removing the staging tree. The `/tree` endpoint lists files with the sha256 Hugging Face publishes for LFS files, so every shard is digest-checked -- the first source in Apogee where a digest is the rule rather than the exception. `models list` shows a snapshot as `safetensors` (trainable, not runnable) with its architecture from `config.json`; `models delete` removes one whole; the bare-`owner/repo` refusal now names `--safetensors` beside the converter.
- [x] **The control plane** -- six routes: `GET/POST /v1/admin/datasets`, `GET/DELETE /v1/admin/datasets/{id}`, `GET /v1/admin/datasets/kits`, and `POST /v1/admin/datasets/synth` as the plane's third job kind (`datasets-synth`), the teacher served by the plane and never a vendor CLI. A dataset created over HTTP is byte-identical to the CLI's; the literal paths are matched before `{id}`. Fifty-four admin rows. The parity table: `datasets create|synth|delete` twins, `prepare|pull` backfills, `kits|list|info` read-only, `train setup` a carve-out.
- [x] **`lib/documentation/reference/training.md`**, the user-facing reference for the track, and the `training/` layering rule: `harness/`, `platform/`, `agentloop/`, `agent/`, the framer and `models/sha256.h` by name, itself, never a surface.
- [x] **71 new test cases** (1366 in all); `cli.datasets_lifecycle` on the real binary.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| The release | **v0.1.0, built now** *(user decision)* | Like the rest of the ring. |
| The tool kits | **Four kits now; `tool-use` and `fetch-url` with the in-text tool protocol as its own tools-track item** *(user decision)* | Today a local model is shown no tool definition and only one family's native tokens are parsed; a kit teaching a prose tool-call line would tune a student into lines nobody dispatches. |
| The environment | **`training/venv/`, created by `train setup` or after a terminal prompt, never by seeding, never on a pipe** *(the placeholder's default, narrowed)* | A fresh install downloads nothing unasked, and a `pip install` is a download. |
| Requirement versions | **Floors, not exact pins** | An exact pin ages into an uninstallable one the day it is yanked; the drivers are written against the libraries' stable surfaces. |
| `prepare_dataset.py`'s loaders | **The standard library for JSON, JSONL and CSV; `datasets` for Parquet only** *(the placeholder's default, refined)* | The common path needs no install, and its test runs on bare `python3`. |
| Kits and scripts | **Compiled-in assets seeded skip-if-present** | Install parity by construction; a user's edit survives an update; the doctor shows the drift. |
| Teachers | **Direct API or local calls, named explicitly, parallel for an API backend, serial for a local one** *(the placeholder's stated upgrade)* | A local provider runs one context; an API one is limited by its rate, which the retries respect. |
| Explicit session mining | **No consent key** *(default taken)* | An asymmetry, on purpose: the gate guards the loop nobody is watching. |
| The `training` row | **Private** | A dataset mined from sessions holds the user's own words. |
| Snapshots | **Under `paths.hf_dir` when set, else `models/<owner>--<repo>/`** *(default taken)* | The template already reserved `hf_dir` for SafeTensors directories. |
| `datasets pull` | **Every data file at the revision unless `:file` names one** *(default taken)* | A dataset is usually several shards. |
| The synth rate-limit numbers | **`--parallel 4`; five retries, backoff capped at 60 s** *(default taken)* | On top of the transport's own `Retry-After` handling. |
| A missing seeded script in `check` | **A warning with `--fix` as the remedy** | A fresh, empty install passes; the doctor's own repair is the fix. |
| `train setup` in the parity table | **A carve-out** | It creates a Python environment on the host; training control is CLI-only by the track's constraint. |

**Verified on the real binary.** `cli.datasets_lifecycle`: the four seeded kits listed and the deferred tool kit absent; a template dataset created; a dataset distilled from the scripted mock as a named teacher and a vendor-CLI teacher refused with nothing written; `datasets prepare` refusing on a pipe before the environment exists and naming `apogee train setup`; `train setup` creating the environment from the host's python3 (installing nothing) and `check` reporting it; `prepare` converting a JSONL file through the **seeded** script under the environment's interpreter; list, info, delete. Skipped by name (ctest 77) on a host with no python3. Running the llama build's suite in parallel found the acquire tests' scratch directory named by a per-process counter alone -- two ctest processes deleted each other's -- the flake class the doctor's tests had already fixed; the name now carries a random suffix, and the tests pass in parallel in both builds.

**Guardrails, each mutation-tested.** 68 mutations, every one caught by the suite as first written; four were re-formed after `make format` reflowed the lines their first shape named (a no-op is a pattern that matched nothing, not a survivor), and caught in their real form. Two tests were strengthened while the plan was drawn, before the run: a cancelled synth must hand back nothing produced past the stop, and parallel batches must really overlap (a teacher that waits for a second call in flight, which serial workers can never satisfy). The mutations: duplicates kept, the call bound doubled, retries never waiting, a final failure retried, the contract not appended, seeds never cycled, the aliases dropped, a batch merged after cancellation, parallel batches run serially, progress never reported, nothing usable not an error, blank halves kept, the temperature not the kit's; a non-JSON line dropped, the exit code not carried, an error line ignored, stderr never kept, the last line without a newline lost, cancellation not terminating the child, the arguments not passed; `venv` called without its directory or its failure reported as success, the installed set not recorded, create re-running on an existing environment, install before create, a missing configured interpreter accepted; a kit with no eval or a blank prompt validating, the `per_seed` default off by one, kits listed unsorted, the stem never naming a kit, the eval suite's keys reordered; role after content, force ignored by the store, the name never validated, the temp file never renamed, a blank question mined, a tool-only turn ending the exchange, the `since` filter inverted, an eval suite read as flat; a vendor CLI accepted as teacher on the CLI and over HTTP, a local teacher run in parallel, synth's `--force` ignored, the kit not validated before the teacher runs, `prepare` running without the environment, the session filter written before validation, the pipe refusal skipped; a missing environment failing the install, an edited script reported ok, a broken kit reported ok; a duplicate dataset overwritten over HTTP, synth without a served backend, the job kind misnamed, explicit lines ignored, create's conflict a `400`, the `{id}` rows before the literal paths, a failed synth writing an empty dataset; the staging tree left behind on failure, a partial tree committed, an escaping path downloaded, the header rung run on every file, the git oid taken as a digest, the README in the snapshot, datasets downloading from the models prefix, any directory a snapshot, `hf_dir` snapshots not listed; kits never seeded, an empty directory Apogee's.

### 2026-09-19 — `training-run`: train, eval, promote, rollback

**What was built**

- [x] **The trainer contract and the JSONL progress protocol** (`training/trainer`): `Trainer` (`train`, `fuse`, `candidate_runner`, `capabilities`) and `CandidateRunner` (adapter-only inference; an empty adapter is the untuned base), `TrainRequest`, and `parse_progress_line` over the script runner's classification -- an `{"error"}` line its own event, a non-JSON line a message, the exit code carried -- so **a crashed driver is a failed run, never a silent success**. The contract is tested on both sides: the parser against a golden fixture event by event, and the shipped drivers under stub `mlx_lm` / `transformers` / `peft` / `torch` modules on the host's bare python3, proving each mode emits the fixture's shapes with no ML stack installed.
- [x] **The drivers**, one argv shape for both (`--mode train|fuse|infer` with the same flags): `train_mlx.py` over `mlx_lm.lora`/`mlx_lm.fuse` and the `mlx_lm` API for inference; `train_peft.py` over transformers' `Trainer` + peft (+ bitsandbytes for QLoRA), the Apple-Silicon hard-exit before any other import and the environment guards before the first ML import, both grep-tested. Checking the drivers against today's libraries found three things a straightforward driver gets wrong: `mlx_lm.lora --data` wants a *directory* of `{train,valid}.jsonl`, not a file; the MLX driver has to accept the `--mask-prompt` its orchestrator passes; and trl removed `DataCollatorForCompletionOnlyLM`, so the PEFT driver cannot import it. Apogee's lay out the data directory (every example trains; validation is a copy of the first tenth, so a small dataset loses nothing), forward the flag, and mask the prompt **exactly** -- the token count of the chat template with the generation prompt appended -- with no trl at all.
- [x] **`ScriptTrainer`** (`training/script_trainer`) drives either script through the script runner with `PYTHONDONTWRITEBYTECODE` set, so a run leaves no `__pycache__` in the seeded tree and the doctor's drift row stays honest; `mlx_trainer` and `peft_trainer` are the two specs. **The mock trainer** (`training/mock_trainer`) ships in the binary as `--trainer mock` -- scripted iterations with a falling loss, a token adapter, a copying fuse, an echoing candidate with the base distinguishable, and at promote a minimal GGUF the header reader parses -- so the whole chain runs on the real binary with no Python: the lifecycle test does exactly that. It is scripted by the dataset's first line (`{"mock": {"error", "fuse_error", "iters"}}`), the way the mock backend is scripted by its file, which is what lets a failed run and a failed promote be driven through the real command line; the mutation run found both paths untested until it was.
- [x] **`apogee train run <student> --dataset <name|path> [--method] [--iters] [--batch-size] [--num-layers] [--grad-checkpoint] [--mask-prompt] [--trainer]`**: the student a snapshot directory, or a name under `paths.hf_dir` or `models/`; a GGUF or a backend entry refused naming `apogee models pull <owner>/<repo> --safetensors` -- **only full-precision weights are trainable**. The trainer: the flag > the config > `auto` (`mlx` on Apple Silicon, `peft` with `nvidia-smi`, else a refusal naming both). The progress protocol becomes the status line (`iter n/N · loss · lr · it/s`; a line every tenth on a pipe); Ctrl-C terminates the child through a handler that only flips the token and records `cancelled`. **The manifest** (`training/manifest`: `training/runs/<YYYYMMDD-HHMMSS>/manifest.json`, suffixed on collision) is written `running` at the start and rewritten at the end -- `complete`, `failed` with the error, or `cancelled` -- with the trainer, the student, the dataset and its sha256, the hyperparameters, the final loss and the timestamps.
- [x] **`apogee train eval <run> [--suite <path|name>] [--judge] [--force]`** (`training/eval`): a suite is `{prompt, expected?}` JSONL -- a path, `training/suites/<name>.jsonl`, a prepared `<name>.eval.jsonl`, or a kit's inline items by kit name. An item with `expected` is a substring check; one without is a **pairwise judge** comparison of the candidate against **its own untuned base** through the same runner (not against `models.default`, an unrelated model), the verdict's first word `A`/`B`/`TIE` with anything else a tie (a first-byte read would count "Both are fine" as B), a judge or baseline failure a tie under the **never-fail contract**; without a judge such items skip and auto-pass, loudly, with the count. The judge is named (`--judge` or `training.judge_backend`), never a vendor CLI, and gets a 1024-token verdict budget, not 10: a reasoning judge spends its budget thinking, and under never-fail a verdict that never arrives is a tie that passes. **The gate is 100%**; the results land in the manifest; re-run only with `--force`.
- [x] **`apogee train promote <run> --as <backend> [--force] [--quantize TYPE] [--keep-fused]`** (`training/promote`), the only path from a run to inference, with every refusal before the expensive part: the eval gate (hard by default; `gate_mode: soft` a warning; `--force` skips), a non-llamacpp `--as`, an unknown or -- in a build without llama.cpp -- unsupported `--quantize`. Then fuse → **convert** → verify → (quantize → verify) → register → ledger. The converter is **llama.cpp's own `convert_hf_to_gguf.py`, vendored verbatim at the pinned revision** under `third_party/llama.cpp-convert/` -- which at that revision is a `conversion/` package of seventy-nine modules plus the three chat templates it reads by path, not one file -- compiled in as chunked literals under MSVC's limit and seeded under `training/scripts/convert/`, run under the environment's interpreter with the `convert` set (floors from its own requirements file). The GGUF is written and its header verified **before the config is touched**: a new `llamacpp` entry appended, or an existing one's `model_path` replaced in place through the new section-scoped `set_backend_model_path`, byte-exact on two copies of the shipped template. Then the **version ledger** (`training/versions/<backend>.json`) with `max + 1` numbering -- numbers derived from `len(versions)` would regress after the first prune, and a later promote would overwrite a live file -- and `retain_versions` (3) pruning the oldest inactive GGUFs, never the active one, marking entries rather than erasing them so a rollback can name what is gone. The fused checkpoint goes after a successful conversion unless `--keep-fused`. **A failure at any step leaves the config and the ledger unchanged**, and removes what the failed step left.
- [x] **`apogee train rollback <backend>`**, repointing at the highest version below the active one (not `active - 1`: numbers have gaps) and **deleting nothing**, a pruned or missing target refused by name; **`train versions [<backend>]`** and **`train status`** reading the manifests and ledgers (`training/store`) -- the filesystem the source of truth, nothing cached.
- [x] **The control plane, reads only**: `GET /v1/admin/training/status|runs|runs/{id}|versions` under the bearer, off the same store; `active_pipeline` and `cycle_active` reserved in the status shape for the pipelines item. **Every control action is a parity carve-out with no route**, and a test sends every mutating method to every training path and requires never a 200. Fifty-eight admin rows.
- [x] **`TrainingConfig`** grows `trainer`, `judge_backend`, `eval_suite_path`, `retain_versions`, `gate_mode`, validated at load and documented in the template; the doctor's `Training` section grows the converter tree as one row, the trainer this host would use and its set, the `convert` set, and every ledger's consistency with the config -- warnings, never a failed install. `apogee train setup --with convert` installs the converter's stack. [training.md](../reference/training.md) extended with the run; the `training/` package description and the converter in the codebase maps.
- [x] **49 new test cases** (1416 in all); `cli.train_lifecycle` on the real binary.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| The pairwise baseline | **The untuned base through the same candidate runner** *(default taken)* | The adapter-versus-base question is the one promotion asks; `models.default` is an unrelated model. |
| The GGUF | **F16; `--quantize TYPE` only when the binary links llama.cpp, else refused naming `apogee models quantize`** *(default taken)* | The in-process quantizer exists behind that flag; a refusal that names the way out beats a silent F16. |
| Retention | **`retain_versions: 3`; 0 keeps all; pruned entries stay in the ledger as history** *(default taken, extended)* | Multi-gigabyte files argue for a bound; keeping the entry is what lets `rollback` say "v1 was pruned" rather than "only one version". |
| The fused checkpoint | **Removed after a successful conversion unless `--keep-fused`; also removed when a later step fails** *(default taken, extended)* | A fused tree is model-sized and the next attempt fuses again anyway. |
| Where promoted GGUFs live | **`training/versions/<backend>/v<N>.gguf`** *(default taken)* | Beside the ledger, inside the private row. |
| The read routes | **`/v1/admin/training/*`, bearer-gated** *(default taken)* | Local training state belongs to the control plane. |
| The converter | **Vendored whole -- the entry script, the `conversion/` package and the three templates it reads -- and compiled in as chunked literals** | At the pinned revision the converter is a package, not a file; a converter fetched at setup would be a second downloader with its own failure modes, and one that exists only when llama.cpp is linked would make promotion a build-flag feature. `gguf` still comes from PyPI. |
| `train_peft.py`'s trainer | **transformers' `Trainer` with an exact prompt mask; no trl** | trl removed the collator a trl-based driver imported, so such a driver cannot start on a fresh install; the mask needs no library, and the template heuristic it replaced was a guess. The `peft` set drops `trl`. |
| `train_mlx.py`'s data | **The `{train,valid}.jsonl` directory laid out from the dataset, validation a copy of the first tenth** | `mlx_lm.lora --data` refuses a file; holding examples back from a small synthetic dataset would cost more than an in-sample validation number, and the eval gate is the real check. |
| The judge's budget | **1024 tokens, not 10** | The rerank judge's finding, live: a reasoning judge never reaches its verdict at a small budget, and under never-fail that silently ungates every judged item. |
| The verdict parse | **The first word, exactly `A`/`B`/`TIE`** | A first-byte read counts "Both are fine" as a vote for B. |
| The manifest's timing | **Written `running` at the start, rewritten at the end** | A run in flight is visible for what it is, and a crashed driver leaves a failed run with its error rather than a directory nobody can explain. |
| The Apple-Silicon guard's test seam | **`APOGEE_TRAINING_STUB_MODULES=1` lifts the hard-exit; the guard still precedes every import** | Without it the PEFT emitter could never be proven on the one merge-blocking CI target, which is Apple Silicon; the variable is set only by the protocol test, under stubs that never import bitsandbytes. |
| The `convert` set's floors | **`torch>=2.6 transformers>=5.5 gguf>=0.19 numpy>=1.26 sentencepiece>=0.2 protobuf>=4.21`** | What the vendored converter's own requirements file states at the pinned revision. |
| The HTTP list shape | **`{"object": "list", "data": [...]}`, `data` never null** | Every other list on the plane; the item's "`[]` not null" kept as the rule for `data`. |

**Verified on the real binary.** `cli.train_lifecycle` with `--trainer mock` and no Python: a template dataset; `train run` writing a manifest with the status line seen; a GGUF and a backend name refused naming `--safetensors`; a promote before any eval refused with nothing written; `train eval --suite` with substring items passing against the mock's echo, the judge-less item skipping loudly, a kit's inline suite by name; `promote --as` registering a new `llamacpp` entry byte-exactly (the diff is the entry's lines and nothing removed) and writing `v1.gguf` that `models info` inspects; a second promote repointing the same entry with only the path changed; `rollback` to v1 as the exact inverse of the repoint, deleting nothing, and a second rollback refused; `retain_versions: 1` pruning v1 and v2 while the next version is v3, never the count, the pruned entry shown as history and a rollback into it refused; `status`; `check` green with the ledger and converter rows. The merge-blocking build passes every test (1416); the llama build passes all but two in parallel -- two embedstore ingest cases of the known temp-directory flake class, unrelated to this item, passing serially on the rerun. The lint gate is clean. The generated units now carry `// clang-format off`/`on`, so a regeneration stays format-check clean; running the drivers' protocol tests found the host's python3 has torch and mlx_lm installed, so a "missing package" case must shadow them with a raising stub on `PYTHONPATH` rather than rely on absence, and `tests/training/scripts/stub_missing/` does.

**Guardrails, each mutation-tested.** 81 mutants, every one caught in its final form. Five were re-formed after the first run -- three because `make format` had reflowed the lines their first shape named, two because dropping an `if` with an init-statement does not compile -- and one was re-sharpened (a `set_backend_model_path` that fell back to the embeddings section was unobservable while the backends entry was found first; searching embeddings first is the mutant that bites). **Three survived on first contact, and each was a real gap:** a path-shaped run id read through the store survived because the test's `../` targets did not exist -- the test now plants a manifest outside `runs/` and requires it unreachable; and two command-layer mutants -- the manifest never marked `failed`, and the config edited despite a failed build -- survived because no test could make the mock trainer fail from the command line. The mock is now scripted by the dataset's first line, as the mock backend is by its file, and both paths are driven through the real command tree and the lifecycle check. The mutations: an iteration read as a message, an error line read as a message, `auto` never picking mlx, an unknown trainer accepted, a GGUF or a shardless directory accepted as a student; `--mask-prompt` never forwarded, the final loss not carried, the exit code not carried, infer without a text record ok, the bytecode guard dropped; the mock emitting one event too many, its loss never falling, cancellation ignored, the base indistinguishable, its GGUF without an architecture, its scripted error ignored, its fuse failure not carried; a substring check that always passes, a baseline win counted as a pass, a judge error failing the item, a baseline error aborting, skips not counted, the gate below 100%, a candidate error not aborting, the verdict read from the first byte, the candidate as Response B, a bad suite line ignored, a promptless item kept; the manifest's status not written, its eval not read back, the collision suffix skipped, path-shaped ids valid, the digest not a prefix, a missing `run_id` accepted; runs oldest first, a path-shaped id read, pruned entries not written, `kept` counting pruned, ledgers unsorted; the gate never refusing, an incomplete run promotable, soft read as hard, the version from the count, a convert failure leaving the GGUF, verify skipped, the fused tree kept, the F16 kept after quantizing, an existing GGUF overwritten, the active version pruned, retain 0 pruning everything, a pruned entry erased not marked, rollback to `active - 1`, rollback into a pruned target, rollback to a missing file, the eval fields not recorded; `model_path` edited in the wrong section, its trailing comment dropped, a bad `gate_mode` or a negative `retain_versions` accepted; a backend name accepted as a student, the manifest never marked failed, eval re-running without `--force`, a vendor-CLI judge accepted, the eval not recorded, the promote gate skipped, a non-llamacpp `--as` accepted, the config edited despite a failed build, retention ignored, the ledger not saved, rollback deleting the current version, rollback's ledger not updated, a kit suite unlabelled; a drifted `model_path`, an edited converter and a missing set each reported ok; an unknown run and a missing ledger answering 200, a bad `kind` accepted, running ids not reported; the converter never seeded.

### 2026-09-19 — `training-pipelines`: pipelines, regimes, and the cycle -- the track complete

**What was built**

- [x] **Pipelines** (`training/pipeline`, `apogee train pipeline run|resume|status`): a spec -- a YAML file, or a `training.pipelines:` entry, **one parser for both** -- of ordered stages, each a fresh LoRA on the previous stage's **fused** weights: stage 0 from the snapshot, every intermediate stage fused into a concrete SafeTensors checkpoint under its run (`runs/<id>-s<N>/fused/`) the next stage trains from, never a stack of raw adapters, and the final stage left unfused for `promote`. **The cumulative gate is the contract**: stage N is evaluated on the union of suites 0..N at 100%, so a stage that improves its own task but regresses an earlier one fails; under the hard gate the run stops `aborted` with the stage `failed` and the later ones `pending`, and `resume` continues from the first stage that has not passed, the passed ones untouched -- refusing a complete run and a spec whose stage count drifted. `--continue-on-fail` and `gate_mode: soft` go on with the failed stage still fused for the next. Every stage is an ordinary run carrying `parent_run` and `pipeline_run_id`, so `train eval` and `train promote` take one directly; the pipeline's manifest is rewritten on every transition; `rehearsal_fraction` mixes a deterministic sample of each prior dataset into a stage (seeds from the sizes alone, the mix written beside the run as what trained). The core resolves no name -- the command hands it resolved stages -- and composes the trainer, `run_eval` and the fuse, nothing more.
- [x] **Regimes** (`training/regime`, `apogee train regime run [<name>] [--teacher] [--student] [--kit …] [--all-kits] [--as] [--count] [--iters] [--temperature] [--max-tokens] [--judge] [--no-promote] [--trainer] [--regime <file>]`): for each kit the teacher synthesises a dataset **through the datasets item's synth core** into `training/regime/<id>/<kit>.jsonl` with the kit's inline eval materialised beside it; the kits become one gated pipeline `<id>-pipe` (a stage per kit, the kit's `train:` block its defaults, `--iters` overriding every kit's); the last passing stage is promoted as `--as` unless `--no-promote`. Ad hoc from flags, a `training.regimes:` entry by name, or a spec file, **flags winning field by field**; `--all-kits` alphabetical with an explicit `--kit` list winning so the order can be curated. Every refusal -- the teacher (named, never a vendor CLI), the student, the kits, the judge, the trainer, and the converter when promoting -- comes before the first teacher call.
- [x] **The cycle** (`training/cycle`, `apogee train cycle run [--source] | status | halt | resume`): unattended, scheduler-invoked training as **one gated pass per invocation** -- no daemon, no `--watch`, launchd and cron documented -- under a PID lock created exclusively and the **circuit breaker** (`halted`, or `consecutive_fails ≥ circuit_breaker_k`, refused naming `cycle resume`). The sources: a `directory` queue of `*.jsonl` (`--source <dir>` for one run), and `sessions`, the user's own persisted chats mined **through the one miner** `datasets create --from sessions` uses, **only with `log_consent: true`** (a source without it fails the config load naming the two risks) and **only sessions newer than the watermark** the history keeps, so a conversation is never trained on twice. No data records `skipped` and counts no failure; otherwise the sources merge, the named pipeline runs with **every stage's dataset replaced by the merged file and its own suite kept**, and the **anchor-baseline dual gate** applies: the final stage's cumulative score must not regress beyond `regression_threshold` against the last passing cycle **and** against the pinned anchor (the first passing cycle's version, or `anchor_version`). A pass promotes into `training.cycle.backend` **through the same promote body the command runs** (its gate runs again), consumes the queue files, advances the watermark and resets the count; a fail discards the candidate -- **nothing reaches inference** -- counts, and at `k` halts with the reason in the record. `history.json` is written atomically at every outcome; `halt` and `resume` are its two edits, so nobody edits it by hand; a failed or refused pass exits non-zero so a scheduler's log shows it.
- [x] **One promote body** (`commands/train.cpp`): the gate, the name, the quantize type, the build, the config edit only after the GGUF parses, the ledger only after the config -- one function the `promote` command calls and hands to the regime and the cycle as a closure, so "a failing candidate never reaches inference" is one rule in one place. The mock trainer is now **scripted with an `answer`** the trained candidate replies with, carried into a fused checkpoint and inherited by a stage trained from it -- which is how a stage that *regresses* an earlier suite is driven through the real command line and the lifecycle check. `train status` rolls up the cycle and the pipelines; the doctor's `Training` section validates every named pipeline, the cycle block (an unknown pipeline or a non-`llamacpp` backend a failure) and reports a halted history with `cycle resume` as the remedy.
- [x] **The reads**: `GET /v1/admin/training/cycle` (the history plus `active` from the lock; `404` until the first run), the pipeline kind under `/runs` and `/runs/{id}`, `status` reporting `pipelines`, `active_pipeline`, `cycle_active` and the history's headline; **every control action a carve-out** -- `pipeline run|resume`, `regime run`, `cycle run|halt|resume` -- with no `POST .../cycle/halt`, deliberately, held by the test that sends every mutating method to every training path (the cycle's control paths included). Fifty-nine admin rows. [training.md](../reference/training.md) grows the three sections with the launchd and cron examples; [http-api.md](../reference/http-api.md) the cycle route.
- [x] **`TrainingConfig`** grows `pipelines`, `regimes` and `cycle`, validated at load (a stage without a name, dataset or suite; a bad method; a number out of range; an unknown source type; a `sessions` source without consent; a breaker below zero; a threshold outside `[0, 1]`), the template documenting all three; `training/pipelines`, `regime/` and `cycle/` beneath the one `training` row, created by first use, no new row. `platform::current_process_id` for the lock's PID.
- [x] **29 new test cases (1445 in all)**; `cli.train_lifecycle` extended on the real binary.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| The breaker and the threshold | **`circuit_breaker_k: 3`, `regression_threshold: 0.0`** *(default taken)* | Strict no-regression, and a bound on unattended failure, by default -- safety for unattended operation is a default, not a footnote. |
| Rehearsal | **Off unless a stage sets `rehearsal_fraction`** *(default taken)* | A stage that wants rehearsal says so. |
| A regime's per-kit count and `--all-kits` | **The kit's `synth.count` unless `--count`; `--all-kits` alphabetical, an explicit `--kit` list winning** *(default taken)* | The explicit list is how the stage order is curated. |
| The sessions source's filters | **`backend` and `since`, through the one miner `datasets create` uses** *(default taken)* | Both applied, through the miner that already exists rather than a second one. |
| The rehearsal mix | **Written beside the stage's run as `rehearsal.jsonl` and recorded as the stage's dataset, not a temp file removed after** | The manifest must name what trained, and its digest must be of a file that exists. |
| The cycle's exit code | **A failed or refused pass exits non-zero** | A zero exit on a failed gate is, under a scheduler, a silent night. |
| Sorting the runs list | **Both kinds under `/runs`, newest first by start time, tagged** | One list a client pages, rather than a second endpoint for pipelines. |
| `GateMode`'s home | **Moved to `eval.h`** | Read by `promote` and the pipeline alike; `store.h` now includes the pipeline manifest, so `pipeline.h` could not include `promote.h` back. |
| The pinned anchor's score | **The history's record for that version, else no anchor half** | A pinned version needs a score to gate against, and the history has it. |
| The lock's exclusive create | **`platform::create_exclusive_file` -- `O_CREAT\|O_EXCL` on POSIX, `CREATE_NEW` on Windows -- plus `platform::current_process_id`** | The first cut used C11 `fopen("wx")` and tripped the owning-memory lint on its deleter; an OS mechanism belongs in the one place `#ifdef`s live anyway, and the seam version needs no handle wrapper at all. |
| The cycle's score | **The final stage's cumulative score, whatever its status** | The last *passed* stage's is 1.0 by the 100% gate's own definition -- an anchor gate reading it could never fail, and the mutation run found exactly that dead gate here on the first pass (the "failed gate still promotes" mutant survived because no test could reach a regression). Under the hard gate the two readings agree; under `gate_mode: soft` the dual gate is the one that holds. |

**Verified on the real binary.** `cli.train_lifecycle` continues with the mock and no Python: a two-stage pipeline whose second stage regresses the first's suite aborting under the cumulative gate (stage 0 fused, the last stage not, the lineage in the stage manifests), `pipeline status` with the resume hint, `resume` after the data is fixed completing and the last stage promoted like any run, a complete run refused; a regime over two kits with the mock backend as the teacher and `--no-promote` (a dataset and a suite per kit, one pipeline, no new ledger); the cycle from a queue directory -- skipped with nothing queued, a pass promoting into `training.cycle.backend` with the anchor set and the file consumed and not a config line removed, a regression failing, discarding and tripping the breaker at `k=1`, the halted loop refused naming `cycle resume` and reported by `check`, resumed and running again with the lock released; `train status` rolling both up; `check` green. The merge-blocking build passes every test (1445) through `cicd.sh`; a rebuild after the lock's lint fix passed all but two in a parallel `ctest` -- two models-package cases of the known temp-directory flake class, untouched by this item, passing serially and on three repeats; the llama build passes all but the two embedstore ingest cases of the same class, passing serially. `make lint` ran clean over the tree once the lock moved into the platform seam (the full run's one error was that deleter; the three files it changed re-linted clean in the error classes). The format check is clean.

**Guardrails, each mutation-tested.** 82 mutants, every one caught in its final form. Four were re-formed after the first run because `make format` had reflowed the lines their first shape named, and one was re-formed because its first shape was equivalent (dropping the "no prior data" short-circuit on the previous-cycle half changes nothing while scores are non-negative; the re-form makes no data *fail*, which the gate must not). **Two survived on first contact, and one of them was the design finding above:** "a failed gate still promotes" survived because no test could reach a regression -- the cycle read the last *passed* stage's score, which is 100% by definition, so the dual gate was dead code; the cycle now reads the final stage and a soft-gate case drives a real regression through it. The other was a config test that refused a pipeline with no `stages` key but never one with `stages: []`. The mutations: stage 1's base not the fused checkpoint, the last stage fused too, the cumulative suite only the stage's own, a hard-gate failure ignored, the gate below 100%, a transition not written, `parent_run` and `pipeline_run_id` not set, resume always from stage 0, a complete run and a drifted spec resumable, cancellation not aborting, rehearsal never mixed and the fraction ignored, a failed stage not fused under continue, `completed_at` never set, `last_passed` counting every stage, the stage run never marked complete, its eval not recorded, the judge's baseline the candidate itself, a stage-count mismatch and a path-shaped id accepted; the teacher flag not winning, `--all-kits` unsorted and overriding `--kit`, the regime's `iters` never overriding, no kits accepted, the eval suite not materialised, the count not passed, `promote_run_id` not set, an empty teacher output not an error; `k = 0` not disabling the breaker, a halt ignored, resume not resetting the count, non-jsonl files collected, consumed files copied not moved, consent not required, the watermark ignored, the newest session not tracked, blank lines merged, the threshold ignored, no prior data failing, the anchor half ignored, the first passing score read instead of the last, a pass not resetting the count, a skip counted as a failure, the breaker tripping one late, no data still training, a failed gate still promoting, the queue not consumed, the anchor never set and overwritten on every pass, the lock not required and not exclusive and never released, the breaker not checked, stage datasets not replaced by the merge, the history not saved on a failure, the watermark not advanced, a failed promotion counted as a pass, the pinned anchor ignored, `consecutive_fails` not persisted, `history_exists` always true; a sessions source without consent loading, an unknown source type loading, the threshold unbounded, zero stages accepted, `eval_suite` not required, the breaker's default not 3; the active pipeline a complete one, pipelines oldest first; `?kind=pipeline` keeping the runs, the cycle route answering with no history, `active_pipeline` never reported; the mock's answer ignored and not carried into the fused checkpoint; a halted cycle not warned and a non-llamacpp cycle backend ok by the doctor; a failed cycle exiting 0, `--no-promote` ignored, the config edited despite a failed build, a resume refusal not honoured at the command.

---

## Milestone AA — The four layers

**Goal.** Make the CLI's source say what it is: four layers -- **Presentation → Business → Data → Infrastructure** -- each package in one, each including only its own layer and those below, the law enforced by the build itself. Asked for by the user on 2026-10-03 and specced from a spike that measured the real include graph into the standing **Architecture** queue (A1–A4, A5 joining the same day). In C++ nothing makes this free the way Go's ban on import cycles does, which is why the layering here has always been a guarded convention, and this milestone turns it into structure.

### 2026-10-03 — `arch-contracts-carve` (architecture item A1): the contracts carved to the Data floor

**Why.** The spike measured 22 packages and 81 include edges, and exactly **six edge types fought the four-layer model**: `backends → harness ×44`, `backends → models ×5`, `logger → harness ×6`, `secrets → harness ×2`, `mcp → agent ×2`, `mcp → harness ×1`, with a true cycle between `backends` and `models`. One root cause: the contracts every implementor reads -- the provider interface, the IR, errors, the config engine -- lived in `harness/`, a Business package, so the Data layer reached up for them. Re-measured at build: the same 81 edges, the same six types.

**What was built**

- [x] **`contracts/`, the Data floor** -- the provider interface, the message IR (`types.h`), `errors.h`, `cancellation.h`, `ModelBehavior` (`behavior.h`), the config engine (`config.h/.cpp`, its template, and the comment-preserving editor `config_edit.h/.cpp`), the layout contract (`layout.h`) and `paths.h`.
  - With them, the moved files' own dependencies: the host rule (`host.h`), the bundled assets (`assets.h/.cpp` and the two generated units, whose generator now writes here), and `sha256.h`.
  - It includes `platform/` and itself only, held there by `harness.layering`.
- [x] **`modelstore/`, model files as data** -- `store`, `sidecar`, `snapshot`, `gguf_inspect`, `kv_cache`.
  - Also `hf_ref.h/.cpp`: `HfRef`, `parse_hf_ref` and `repo_directory_name`, moved verbatim out of `models/source_hf` because the store names its directories with them.
  - `snapshot.h`'s include of `source_hf.h`, which it never used, is gone.
  - The backends read the header reader and the cache arithmetic from here, so the `backends ↔ models` cycle is gone both ways.
- [x] **`transport/`, the wire primitives** -- the HTTP client, the SSE parser, the JSONL framer. `mcp/` and `training/` read the framer from here, so their named allowances into `backends/` are gone.
- [x] **The factory fills the Harness without including it.** No carve could remove `backends/factory.h → harness/harness.h`: the Harness stays Business.
  - `contracts/provider.h` gains `ProviderRegistry`: the three things the factory asks -- the config, `register_provider`, `use_default_router`.
  - The Harness implements it, and `build_providers` takes it. Every call site still passes a Harness, unchanged.
  - The capability-interface pattern CLAUDE.md prescribes, pointed the other way.
- [x] **The four-layer law in the layering test** (`tests/layering.cmake`).
  - The layer map holds every package, and any include that reaches up a layer fails, naming the file and both layers.
  - A package with no row fails, and so does a row naming no package, so a new package declares its layer the day it exists.
  - `contracts/`, `modelstore/` and `transport/` are held to their floors, and `secrets/`, `knowledge/`, `graph/` and `training/` to allow-lists naming `contracts/`.
  - Each rule was verified against a planted violation in a scratch copy of the tree: a Data→Business include, a Business→Presentation include, `contracts/` reaching sideways, `modelstore/` and `transport/` past their floors, `secrets/` past the contracts, an unmapped package, a map row naming a missing package, and `mcp/` including a backend -- nine planted, nine caught by name.
- [x] **The checks that name paths moved with them.** `cli.one_key_resolver`'s and `cli.one_role_resolver`'s allow-lists now name `contracts/`. Each now refuses an allow-list entry that names no file, so a file that moves without its entry fails loudly instead of being scanned under a stale allowance. Both were verified against a removed file.
- [x] **Every includer updated** -- 549 include lines in 314 files, a scripted rewrite, then clang-format re-sorting the include blocks. **Namespaces did not move**: `apogee::harness::Config` lives in `contracts/config.h`. The include path is what the layers govern, and renaming namespaces would have touched thousands of lines for no layering gain.
- [x] **Tests mirrored**: `tests/contracts/` (config, config edit, host, paths, types, assets, sha256), `tests/modelstore/` (store, sidecar, header reader, cache arithmetic), `tests/transport/` (HTTP client, SSE parser, framer).

**The measurement after:** 91 edges, **zero upward**. `backends/`, `logger/` and `secrets/` include nothing from `harness/` or `agent/`; `contracts/` reaches only `platform/`; `modelstore/` and `transport/` only the contracts and the platform.

**Decisions** -- the spike's file lists were right in spirit and short in four places, each settled the conservative way: move a dependency with what needs it, never change what code does.

| Decision | Choice | Why |
|---|---|---|
| The config engine | Moves whole, loading with the types *(default taken)* | Splitting load from types would be a seam nobody asked for. |
| What else `contracts/` takes | `host`, the bundled assets and the config template, `sha256` | The moved files' own dependencies; without them `contracts/` would include upward or sideways. |
| `modelstore/` | Takes `snapshot` and the HF ref helpers (`hf_ref`, moved verbatim) | The store's own dependencies. |
| `transport/`'s layer | **Data**, not Infrastructure | `http_client` speaks the contracts' cancellation token and errors; A2's table updated. |
| `mcp/`'s layer | **Business**, and `agent/tool.h` stays put | The spike read `mcp → agent` as one struct; the use is the whole `ToolRegistry` and dispatch. Moving those to the floor would drag Business logic down; only `commands/` includes `mcp/`. |
| The factory's reach into the Harness | `ProviderRegistry` in `contracts/`, implemented by the Harness | Dependency inversion; no call site changes. |
| Namespaces | Unchanged | The include path is what the layers govern. |
| `events/` | Stays put *(default taken)* | Already a leaf; Infrastructure by assignment. |
| `transport/` before A2 | Named now, flat beside the others *(default taken)* | A2 makes the directories say the layers. |

**Verified.** `make test` green on the llama build -- all 2,009, the usual one skip -- with no test logic edited: the config editor's byte-golden suite, `cli.one_key_resolver`, `cli.one_role_resolver`, `cli.install_parity` and `cli.no_listen_symbols` among them. The asset generator reproduces the moved units with only their path lines changed. Lint shows 0 errors.

### 2026-10-03 — `arch-layer-move` (architecture item A2): the directories say the layers

**Why.** With the contracts carved (A1), the include graph obeyed the four-layer model, but the tree did not show it: 25 packages sat side by side under `source/`. This item makes the directories carry the layers, and does nothing else, so it can be reviewed as what it is: renames.

**What was built**

- [x] **Every package `git mv`'d into its layer** -- `source/presentation/` (`commands`, `httpserver`, `markdown`, `render`), `source/business/` (`harness`, `agentloop`, `agent`, `tools`, `knowledge`, `graph`, `training`, `scaffold`, `models`, `mcp`), `source/data/` (`contracts`, `backends`, `embedstore`, `logger`, `secrets`, `modelstore`, `transport`), `source/infrastructure/` (`platform`, `ansi`, `events`, `version`).
  - The tests are mirrored as `tests/<layer>/<package>/`. The cross-cutting checks, `support/`, `fixtures/` and the root-level suites stay at `tests/`'s root.
- [x] **Short include paths, byte-stable.** The four layer directories are `apogee_core`'s include roots, root-first, and the flat `source/` directory is no longer one, so a stale path cannot resolve two ways. **No include line changed**: `#include "agentloop/loop.h"` names a package wherever its layer puts it, and moving a package between layers would change no include. `rg '#include "(business|data|presentation|infrastructure)/'` finds nothing.
- [x] **The checks that name paths moved with them:**
  - **`tests/layering.cmake`** reads the layer from the directory and requires it to agree with its map, so a package moved without its row, a row changed without the move, or a package outside any layer directory each fails by name.
  - **The two resolver checks and `cli.no_vendor_credentials`** name layered paths.
  - **The conformance checks** read `presentation/httpserver/mux.cpp` and `presentation/commands/json_reporter.cpp`.
  - **The asset generator** writes `data/contracts/`.
  - **Two tests that build paths into the tree** follow it: `attachment_guard_test` reads the command sources, and `progress_contract_test` its fixture and stub modules.
  - **The no-listen check** attributes by object file name (`serve.cpp.o`), so it needed nothing.
  - **The Makefile's `format`/`lint` scope** and `gcc-check.py` find sources recursively or from the compile database, so nothing there needed changing either.
- [x] **The documentation sweep, in-change** -- a committed script, `lib/src/cli/scripts/sweep_layer_paths.py`, rewrote every `source/<package>` and `tests/<package>` path to its layered form across CLAUDE.md, DEVELOPER.md, the ROADMAP, every pending backlog document, the skills and the scripts' comments. Hand-written prose says what the tree now is: CLAUDE.md's "Where new source code goes" (a new package goes in its layer's directory with a row in the map) and the Codebase Map, DEVELOPER.md's tree and a layer table.

**The diff, checked.** `git diff -M` over the move: 433 source and 194 test renames.
- **Below 100% similarity:** seven files. Six carry one comment line each naming a test by its old flat path. The seventh is `attachment_guard_test`'s path line (`progress_contract_test`'s two path lines were fixed after the first run, below).
- **Every other changed line** is a CMake path list, a check's path, or documentation. No line of code changed meaning.

**Verified.**
- **The suite:** `make test` on the llama build passes all 2,009, the usual one skip.
  - The first run failed four training tests. `progress_contract_test` built its fixture's path from the tests root (`/training/fixtures/`), a path that had moved.
  - Its two path lines were fixed, and the rerun was green.
- **A fresh build:** a new worktree holding exactly the moved tree, with a brand-new build directory, configured, built and passed the whole unit suite and the path checks. Nothing cached locally can be what made it build.
  - `cicd.sh --fresh` clones from GitHub, and these commits are not pushed, so the fresh worktree stood in for it.
- **The planted violations, after the move:**
  - a `listen()` planted in `data/transport/http_client.cpp` failed `cli.no_listen_symbols`, naming `http_client.cpp.o`;
  - a Data→Business include failed the layering test; so did `logger` moved into `business/` without its row, a package directly under `source/`, and `contracts/` reaching sideways;
  - a second key chain failed `cli.one_key_resolver`;
  - an allow-listed file removed failed `cli.one_role_resolver`;
  - a binary whose `__complete` answers nothing failed `cli.shell_completion` 65 ways.

  Each passed again once the plant was removed. `make format-check` passes over the whole tree.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| The window | Now, mid-`v0.1.3`, with the user's other session told to pause -- **the user's call** | It was still writing backlog documents when A1 committed; the user chose to commit and continue. |
| Include resolution | Four layer roots, root-first; the flat `source/` root removed *(default taken)* | A stale short path cannot resolve two ways. |
| The tests root | Cross-cutting checks, `support/`, `fixtures/` stay; per-package directories move *(default taken)* | They belong to no one layer. |
| Sweep mechanics | A committed script, its diff read as text *(default taken)* | The 2026-08-24 pattern: two dozen documents cannot drift one by one. |
| MILESTONES.md | Not swept | It records what shipped; its paths were true when written. |
| Stale paths in source comments | Swept with the documents | They are path references like any other. The diff shows them as comment-only lines, so the renames stay reviewable. |
| Where a layer is read from | The directory, checked against the map | Two declarations that must agree: a move without its row fails, and so does a row without its move. |

### 2026-10-03 — `arch-commands-modules` (architecture item A3): `commands/` in three

**Why.** `commands/` was the biggest package in the tree -- 85 files -- and it mixed three presentation concerns that change for different reasons: the commands themselves, what paints a terminal, and the machine-mode adapter. The code already kept them apart informally -- views never parse argv, the adapter never paints, commands compose both. This item makes those boundaries modules, kept separate from A2's renames because which unit is a view is a judgment, not a rename.

**What was built**

- [x] **Three modules in `source/presentation/`:**
  - `cli/` -- every command, the root and registry, `helpers`, `permissions`, the one command table and the slash ecosystem whole (`chat_completer`), and the completion protocol. The composition root, unguarded as `commands/` was.
  - `views/` -- `status_line`, `thinking_view`, `answer_view`, `line_reader`, `download_progress`, `ask_prompt`, `terminal`, `input_gate`, and the terminal adapter `cli_reporter`.
  - `machine/` -- `json_reporter`, the machine-mode adapter.

  Tests mirrored (`tests/presentation/{cli,views,machine}/`). No `presentation/common/` was needed; `httpserver/`, `markdown/` and `render/` are untouched.
- [x] **Allow-lists for the two guarded modules** in `tests/layering.cmake`, the layer map listing the three in Presentation.
  - `views/` includes itself, `ansi/`, `markdown/`, `platform/`, `contracts/` and `agentloop/` (the Reporter seam and `ask_user` its adapters implement), and **never `cli/`, `machine/` or CLI11**. A view that parses argv fails by name.
  - `machine/` includes itself, `agentloop/`, `agent/` and `contracts/`, and **never a painter** (`views/`, `ansi/`, `markdown/`).
  - Five planted violations, five caught by name: a view reaching the commands, a view including CLI11, the adapter including a view, the adapter including `ansi/`, a view reaching the adapter.
- [x] **The include spellings updated in this item, the one place they legitimately change**: `#include "commands/status_line.h"` is now `"views/status_line.h"`. The module directories sit in the presentation include root A2 made, so no CMake include path changed. The checks that name paths followed: `cli.machine_schema_conformance` reads `presentation/machine/json_reporter.cpp`, the two resolver checks name `presentation/cli/`, and the attachment guard reads the command sources in `cli/`.
- [x] **The documents name the modules**: a sweep mapped each `commands/<unit>` to its module across CLAUDE.md, DEVELOPER.md and 30 pending backlog documents. MILESTONES is left as history. The prose says what the three are.

**Where each ambiguous unit landed**

| Unit | Module | Why |
|---|---|---|
| `cli_reporter` | `views/` *(default taken)* | The terminal adapter: it owns paint order, composing the status line, the thinking view and the answer view behind the Reporter seam. |
| `json_reporter` | `machine/` *(default taken)* | The machine-mode adapter; it writes events, never a painted byte. |
| `interrupt` | `cli/` (the spec sketched `views/`) | The SIGINT scope a command runs under, exiting with `helpers`' `kCancelled`. It paints nothing, only command files use it, and in `views/` it would include `cli/`. |
| `ask_prompt` | `views/` | It paints the question on the status line and reads the answer; `ask_user` is the seam it implements. |
| `input_gate` · `line_reader` | `views/` | Terminal input as a view: typeahead and line editing, nothing about argv. |
| `permissions` | `cli/` | It builds each surface's permission checker from config and composes the status line and the machine adapter -- composition. |
| `chat_completer` | `cli/` | The slash ecosystem moves whole with the one command table; splitting dispatch from completion would reopen the drift item 24 closed. |
| `tool_vectors` · `model_chain` · `helpers` | `cli/` | Composition: caches, orchestration and shared command helpers, used by commands only. |

"Machine-input parsing" has no file of its own: machine mode's input is read in `chat.cpp`, a command, in `cli/`.

**Verified.**
- `make test` on the llama build passes all 2,009, the usual one skip: the PTY checks, machine-mode e2e and conformance, the piped byte-identity checks and the one-table completion test all pass unchanged.
- `--help` output for 25 commands is byte-identical to a binary built before the split (the installed one, from A1's commit).
- The diff is 116 renames: 11 exact, 105 differing only in include lines and comments.
- `make format-check` passes over the whole tree.

### 2026-10-03 — `arch-build-enforcement` (architecture item A4): the build holds the layers

**Why.** After A1–A3 every package sat in its layer, but the law was a test's: `harness.layering` grepped the includes after the fact, and `apogee_core` -- one static library of everything -- let any file include any other and still link. In Go the reverse edge is an import cycle and the build fails; this item buys that back with the linker. The user's calls: **dual enforcement** (the link graph for the coarse law, the scan for the rules finer than a layer), and **the build in the ADRs' layer order**, expecting it to build faster -- an expectation measured below, not assumed.

**What was built**

- [x] **The module map, `cmake/modules.cmake`** -- every module's layer and the modules it links, 28 modules and 109 links, read by the build and the layering test alike. Plain `set()` data, so `cmake -P` reads it exactly as the configure does.
- [x] **One static library per module**, `apogee_<layer>_<module>`, made by `apogee_add_module` (`source/CMakeLists.txt`) from its layer's `CMakeLists.txt`, which names only its sources and third-party code. Its links are the map's row, never the caller's.
  - A module's PUBLIC include root is its own layer directory, so it sees its layer and what its links bring up from below. **An include that reaches up a layer does not compile.**
  - Third-party code sits on the module that uses it: CLI11 on `cli/`, curl on `transport/`, httplib on `httpserver/`, replxx on `views/`, the schema validator on `agentloop/`, yaml-cpp on `contracts/` and `training/`, SQLite on `embedstore/`, llama.cpp on `backends/` and `models/` (with `apogee_llama_chat`, linked PRIVATE into `backends/`).
  - **The version stamp sits on `version/` alone**, and `APOGEE_ENABLE_LLAMA` on the two modules that test it. Both used to sit on all of `apogee_core`.
- [x] **The build in layer order.** `source/CMakeLists.txt` adds `infrastructure/`, `data/`, `business/`, `presentation/` in that order, ADR 0001's. Each layer is an INTERFACE target, `apogee_<layer>`, buildable alone: its one source, the layer's `CMakeLists.txt`, is never compiled; it is what makes CMake build an INTERFACE target. `apogee_core` is an INTERFACE over the four, the one name the executable and the cross-cutting suites link.
- [x] **The link policy walks the graph** (`cmake/ApogeeLinkPolicy.cmake`), failing the configure step by name on:
  - a module linking anything its row does not name, or missing a link it names (a `target_link_libraries` added by hand fails);
  - a link up a layer;
  - a cycle, even inside a layer (CMake itself allows cycles between static libraries);
  - a module compiling a source outside its own directory;
  - a layer's test library linking above its layer.
  - "Nothing links `apogee`" stands as before.
- [x] **`harness.layering` rescoped** (`tests/layering.cmake`), reading the map from `cmake/modules.cmake`:
  - The directory ↔ map checks stand.
  - **The coarse upward scan retired** in favor of **the map's mutation check**: the includes held to the map in both directions. An include of another module the row does not link fails, and so does a declared link nothing includes. The build sees a layer, not a module -- one include root per layer -- so an undeclared edge *inside* a layer, or downward, compiles; this is where it is caught.
  - **The named rules stand verbatim**: the guarded packages never include `backends/`; `events/` a leaf; the floors of `contracts/`, `modelstore/`, `transport/` and `secrets/`; `markdown/` only `ansi/`; the `knowledge/`, `graph/` and `training/` allow-lists, the framer by name; `views/` and `machine/`. Each still fails with the map loosened to allow its violation.
- [x] **`operations/`, a new Presentation module** -- the refactor the graph needed, on the user's word mid-build. The measured graph had a cycle: `cli/serve_cmd` includes `httpserver/`, and `httpserver/` included `cli/` for seven helpers both surfaces run. Moved there verbatim:
  - `run_settings` -- the temperature, token cap and system prompt a run resolves;
  - `retrieval` -- the one retrieval choice, its status-line sentences, `retrieve_for_collection`;
  - `backend_names` -- the helper and utility backends, and an explicit model mapped to its entry;
  - `collections` -- a collection's path and the names on disk;
  - `knowledge_core` -- the capture core, `git mv`'d;
  - `graph_members` -- a named graph's members and their validation;
  - `dataset_core` -- session loading, `create_dataset`, the teacher.

  `cli/`'s headers include them, so no command changed a line. The graph is acyclic: 28 modules, 109 edges.
- [x] **Tests compile per layer, run as one** (ADR 0004).
  - Four OBJECT libraries, `apogee_tests_<layer>`, each link their layer's aggregate and those below, so **a test that includes a header from above its layer does not compile**. All four link into the one `apogee_tests`, so test names, `catch_discover_tests` and `cicd.sh --unit-tests` are untouched.
  - The three root suites (smoke, packages, bundled agents) compile in the executable against `apogee_core`. A support file sits with the lowest layer whose headers it includes.
  - Eight test files reached up a layer and were re-homed to the highest layer they touch:
    - the Harness-driven provider cases into `tests/business/harness/` (`provider_capability_test`, `factory_harness_test`, `llamacpp_harness_test`, 14 cases, moved verbatim);
    - the `auto_rag` choice case into `tests/presentation/operations/retrieval_test.cpp`;
    - `leak_test` into `tests/presentation/cli/`;
    - the `ffmpeg` fakes split out of `support/media_fakes.h` into `support/fake_ffmpeg.h`, for the platform suite.

    Every test name kept; the 2,009 cases all still run.
- [x] **Two test hazards, fixed in passing.**
  - **Shared scratch directories.** Seven suites (`embedstore/` ×4, `business/models/` ×2, `presentation/cli/models_test`) named their scratch directories by a per-process counter, so parallel ctest processes shared them. The re-homed tests reshuffled the schedule enough to show it: 27 failures under `-j8`, every one green alone. Each now draws a random name beside the counter, the fix `rag_test` already carried.
  - **A test that deleted the developer's completions.** `plan_uninstall` finds the shell completions under the real home directory, and `lifecycle_test`'s uninstall cases planned against it. One of them ran `execute_uninstall`, deleting whatever `make install` had put there, and "an already-removed install plans nothing" failed whenever they were present. Each uninstall case now plans against a home of its own (`HOME`/`USERPROFILE` guarded), and a new case pins the lookup. The suite is 2,010.

**The timings** -- measured on the dev host (Apple silicon, Ninja, `-j8`, RelWithDebInfo), the pre-A4 tree (`e11efcc`) against this one, each in a fresh worktree and build directory, with no other build running. Llama-on with ccache off: llama.cpp turns ccache on for the whole build, ours included, so a `touch` there is a cache hit -- those runs used real content edits instead, cold, and llama.cpp itself was built first, untimed.

| Change | Llama off: before → after | Llama on: before → after |
|---|---|---|
| Clean build | 118.0 s → 113.3 s (553 → 563 units) | 113.9 s → 118.4 s (Apogee's own code) |
| One `.cpp`, Infrastructure (`ansi/text_width.cpp`) | 1.9 s → 1.3 s | 2.0 s → 1.5 s |
| One `.cpp`, Data (`backends/anthropic.cpp`) | 4.0 s → 3.5 s | 4.1 s → 3.6 s |
| One `.cpp`, Business (`agentloop/loop.cpp`) | 3.8 s → 3.2 s | 4.0 s → 3.4 s |
| One `.cpp`, Presentation (`views/status_line.cpp`) | 2.2 s → 1.6 s | 2.3 s → 1.8 s |
| A Data floor header (`contracts/types.h`) | 62 s, 183 units → 62 s, 190 units | 62.5 s → 65 s |
| A new commit (the configure-time stamp changes) | **55.3 s, 211 units → 1.5 s, 1 unit** | **57.6 s, 214 units → 2.8 s, 4 units** |

What the numbers say, plainly:
- **The user's expectation holds for the edit loop.** Every one-file edit, in every layer, is 0.5-0.6 s faster: the step that shrank is the archive -- one module's, not all of `apogee_core`'s 200-odd objects.
- **Every `make` after a commit is the largest win.** It reconfigures, and the version stamp used to sit on all of `apogee_core`, so a new commit recompiled the whole core. It now recompiles `version.cpp`.
- **A view edit recompiles one file and relinks** (1 unit, 3 links), with `backends/` untouched. That was already true: what recompiles follows the includes, and no library layout changes it -- which is also why a `contracts/types.h` edit costs the same, the Data floor being included nearly everywhere.
- **Clean builds are a wash**, within the run-to-run noise: -4% with llama off, +4% with it on, for 27 more archives.
- The layer order shows in the build's structure -- each layer buildable alone (below) -- more than in a parallel build's clock, as the item predicted.

**Proving it bites** -- twenty planted violations in a scratch worktree, twenty caught, each naming the target or file and the rule; each restored, and the unplanted tree passing.
- **Compile time:** an include up a layer at each boundary fails with the header not found -- Data→Business (`logger/session.cpp` → `harness/`), Business→Presentation (`harness/harness.cpp` → `cli/`), Infrastructure→Data (`ansi/ansi.cpp` → `contracts/`) -- and so does a Data test including the Harness.
- **Configure time:** the link policy names each of these:
  - a link up a layer in the map (`contracts → harness`);
  - a cycle (`agentloop ↔ knowledge`, named as exactly those two);
  - a `target_link_libraries` added by hand;
  - a source compiled into the wrong module;
  - a Data test library linking Business;
  - a mapped module that no layer builds.
- **The scan:**
  - a declared link deleted inside a layer (`knowledge` without `harness`, which still compiles);
  - a link nothing includes;
  - a package with no row;
  - a row disagreeing with its directory;
  - **each named rule with the map loosened to permit its violation**: the harness including a backend, `markdown/` including a view, `events/` including the platform, a view including `cli/`.
- **The symbol scan:** a `listen()` planted in `transport/` fails `cli.no_listen_symbols`, attributed to `libapogee_data_transport.a(http_client.cpp.o)`. The scan's walk now covers all 28 module archives.
- **Each layer builds alone, from nothing:**
  - `--target apogee_infrastructure` builds the four Infrastructure modules;
  - `apogee_data` builds the seven Data modules plus `platform` and `events` -- not `ansi`, not `version`;
  - `apogee_business` builds the ten Business modules plus the five Data modules they link -- not `backends`, not `secrets` -- and `platform`, `events`.

**Verified.**
- **The suite:** `cicd.sh --test` on the llama build (`macos-arm64`) passes all 2,010, the usual one skip; the no-llama build passes too, twice in a row under `-j8`.
- **Test names:** listed from both binaries, every pre-A4 test name is unchanged; the one difference is the added uninstall case.
- **A fresh build:** a new worktree holding exactly this tree, with a brand-new build directory, configured (`module graph OK (28 modules, 109 declared links, layered, acyclic)`), built and passed the suite. Its first run surfaced the two test hazards above, both fixed before the rest; the planted violations ran in it.
- **Checks:** `make format-check` passes, and clang-tidy over every new and changed source shows 0 errors (its warnings are the moved code's, as before the move).


**Decisions**

| Decision | Choice | Why |
|---|---|---|
| Library kind | STATIC *(default taken)* | Ordinary link semantics; the symbol scan attributes by archive member as before. |
| Where the map lives | One table, `cmake/modules.cmake`; the per-layer `CMakeLists.txt` name sources only *(default taken)* | Every link greppable in one place, read by both enforcers. |
| Proving it bites | Planted violations, one per boundary and rule, then removed *(default taken)* | The house pattern. |
| The frozen graph | Refactored: `operations/` and the re-homed tests -- **the user's call**, mid-build | A module cycle and upward test includes cannot be a link graph. |
| Include grain | The layer, by construction; the edge inside a layer is the scan's | Per-module include roots would need symlinked shim directories, moving every header out of `source/`, out from under clang-tidy's header filter and the IDE, on Windows too. |
| Layer aggregates | INTERFACE, with the layer's `CMakeLists.txt` as a never-compiled source | CMake builds an INTERFACE target only when it has sources. |
| Tests | Per-layer OBJECT libraries in one executable | Compile-time layering for tests, with every name and entry point unchanged. |
| Compile definitions | Scoped to the module that reads them | The version stamp on all of `apogee_core` recompiled the whole core at the first configure after every commit. |

### 2026-10-03 — `arch-adrs-layer-context` (architecture item A5): the law next to the code

**Why.** The layers were built and enforced (A1–A4), and their reasons were written down as ADRs the same day (`lib/documentation/adrs/cli/`, at the user's direction). But a model or a person opening a file in `source/business/` still had nothing in reach saying what the layer may include, which rules bind a change there, or where the law is enforced. The user asked that "each file … must know the correct context for its layer". This item puts a card at every layer root and gives the ADRs teeth.

**What was built**

- [x] **Eight layer cards**, a `CLAUDE.md` at each layer root -- `lib/src/cli/source/<layer>/` and `lib/src/cli/tests/<layer>/` -- which the context system reads for any file opened beneath them.
  - **A source card** gives the layer's position, its **Modules:** line, what it may include, how the law is enforced, the rules finer than a layer that bind it, and what a change there owes. Its ADR links: ADR 0001 always, 0002 where behavior and modes meet, 0003/0004 for a new module, 0005/0006/0007 where they bind.
  - **A test card** gives the mirror, the per-layer test library and what it may link, where a behavior spanning layers is tested, the support files at its floor, and the hermetic conventions.
  - **Summaries that point**, 11 to 17 lines each: the rationale stays in the ADRs and DEVELOPER.md.
- [x] **ADR 0008, the module map** -- the gap a card surfaced. ADR 0001's prose package list predated the settled module set: `transport` in Infrastructure, `commands` named, `agent`, `mcp` and `scaffold` missing. 0001 is append-only, so a new record states the map as a table, each layer's modules and what it may depend on, and 0001's Status names the supersession of its list alone. The rule itself, "its own layer or any layer below", stands in 0001.
- [x] **`harness.layer_context`** (`tests/layer_context.cmake`), a `cmake -P` ctest case beside the layering test, reading the same map:
  - **the cards:** all eight present, each at most 30 lines and 3,000 bytes, each linking ADR 0001, every relative link resolving, a source card's modules exactly its layer's;
  - **the mirror** (ADR 0004's structural check): every test directory a module of its layer, every module with its test directory, `version` excepted by name (the root smoke test covers it);
  - **the index:** every listed ADR present, numbered in order, titled and dated, every ADR file listed;
  - **the law, golden:** ADR 0001's numbered layers in the map's order and its rule sentence verbatim, ADR 0008's table equal to the map -- membership per layer and the may-depend-on column. The prose law and the mechanical law are provably one.
- [x] **The docs flow carries it:**
  - CLAUDE.md's Documentation and Status step 4 adds the card and ADR 0008 for a module added or moved;
  - the docs skill's mechanical validation names the cards' links and the check;
  - DEVELOPER.md's tree and checks section describe both.

**Proving it bites** -- sixteen planted violations in a scratch copy, sixteen caught by name, the unplanted and restored trees passing:
- **The cards:** a card deleted; a card padded past its budget; a card's module list short one; a card's link broken; a card no longer linking ADR 0001.
- **The mirror:** a test directory in the wrong layer; a module losing its test directory; `version`'s named cover gone.
- **The index:** an indexed ADR missing; an ADR file the index does not list; an ADR titled with another number.
- **The law:** ADR 0001's layers reordered; its rule sentence changed; ADR 0008 short a module; ADR 0008 letting Business depend up; the map moving a module with the documents unchanged.

**Verified.** `cicd.sh --test` on the llama build -- all 2,011 pass, the usual one skip; `harness.layer_context` and `harness.layering` pass on the real tree; every relative link in the documentation and the cards resolves.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| The law's form | "Its own layer and **any** below" *(default taken)* | The shipped graph: Presentation reads `contracts/` directly. The user's phrasing "the layer below it" is recorded; tightening would be a new ADR. |
| Card placement | Four source, four tests; no ninth at `tests/`'s root *(default taken)* | The test cards mirror the source ones, as the trees do. |
| Enforcement vehicle | A ctest case beside the layering test *(default taken)* | It rides the existing suite. |
| ADR 0001's stale list | **A new record, ADR 0008**, superseding the list alone | ADRs are append-only; the item's seam says a gap becomes a new numbered record. |
| Card budget | 30 lines, 3,000 bytes | Room to grow; no room to restate DEVELOPER.md. |
| `version` without a test directory | Excepted by name, its cover checked | The root smoke test is its suite. |
| The pipeline | Unchanged -- `changed.sh` does not list the ADRs | `cli.http_api_conformance`'s precedent with the HTTP reference; widening the CLI's inputs is a pipeline change of its own (ADR 0005), flagged for the user. |

## Milestone AB — MLX inference

**Goal.** A second local runtime beside in-process llama.cpp: Apple's MLX on Apple silicon, for day-one support of architectures the GGUF ecosystem lags on and a shorter train → try loop. MLX's core is C++, but what makes it an LLM runtime -- the model zoo, tokenizers, each family's chat template and call format -- lives in Python's `mlx-lm`, so the shape is not a second in-process link but a **persistent Python child over pipes**, built from parts already shipped: the claude-cli backend's persistent child (Milestone L) and the training track's driver discipline (Milestone Z). Opened 2026-10-03 as track 27's MLX items (27a–27c) under the dated SPEC amendment: llama.cpp stays the zero-dependency default on every platform, and an explicitly opt-in, interpreter-backed type is permitted beside it, refusing loudly where its runtime is absent.

### 2026-10-04 — `mlx-backend-core` (backlog item 27a): a persistent Python child over pipes

**What was built**

- [x] **The `mlx` backend type** (`BackendType::Mlx`, `type: mlx`): `model_path` names a Hugging Face model directory (config.json, the tokenizer files, SafeTensors weights), the sampling fields, `max_tokens`, `context_size`, `thinking` and `idle_unload_seconds` as on a llamacpp entry. Not a vendor CLI and keyless, so the factory, the key resolver and every type switch learned it; `config add-backend --type mlx` and the admin twin take it from the one type table. A commented example joins the starter config.
- [x] **The driver** (`assets/mlx/mlx_generate.py`): one process holding one model and its attention cache, speaking protocol 1 -- `generate`/`cancel` in; `ready`, `text`, `reasoning`, `tool_call`, `done`, `error` out -- and exiting when stdin closes. It renders through the tokenizer's own chat template (`apply_chat_template` with the tools and the thinking switch), reuses the session cache by the token prefix the next prompt shares (trimmed where the cache allows, restored from a checkpoint where it cannot be cut back, rebuilt otherwise), answers a side request on a cache of its own, and reads the reply in the model's own format: reasoning by the tokenizer's markers, calls by `mlx-lm`'s own parser for the format (Qwen's `json_tools`, Gemma 4's), a reply that **is** a JSON call to an offered tool as Llama 3's format, and a span that opened like a call and parses as nothing given back as text. It never reads config, never touches the store, ignores SIGINT (a cancel arrives over the protocol), sets the Hugging Face offline switches before its first import, and points descriptor 1 at stderr so a library's print cannot reach the protocol channel.
- [x] **Compiled in like the trainers**: `scripts/generate_training_assets.py` writes a third unit, `contracts/assets_mlx.cpp` (`bundled_mlx_scripts()`), and `bundled_files()` seeds it skip-if-present at `training/scripts/mlx_generate.py` -- beside the trainers, under the same environment. The backend runs the seeded copy; an edit is kept and shown.
- [x] **The provider** (`backends/mlx_local.h/.cpp`) over `platform/child_process` and the one framer: one child per provider, started on first use (or `preload`), the load said as `ModelLoading`/`ModelReady`; each turn one `generate` line built from `local_prompt`'s messages and the 26h ladder's resolved values (`generation_config.json` the model-file rung); the driver's text through the family's profile filters exactly as llama.cpp's own text is, its typed reasoning and calls straight through; a `PromptCache` line for `--verbose`. Stderr drained continuously into an 8 KiB tail whose last line is folded into a failure. A dead driver fails the turn at once and the next one respawns; a load failure is the driver's own sentence; a request error keeps the child; a cancel is sent and its `done` awaited, the child ended only when none comes; a sink that throws cancels the generation it was hearing. `VisionCapable` answers false (27c's), the context window is the entry's or 0 (27b's).
- [x] **The protocol** (`backends/mlx_protocol.h/.cpp`), pure: the request in Hugging Face template shapes, the events parsed with a stray line or a newer driver's type dropped.
- [x] **The refusal ladder** (`probe_mlx_runtime`, `probe_mlx_backend`), file facts only: not Apple silicon; no interpreter under `training/venv`; no `mlx_lm` package there; no `model_path`; a path that is not there; a file, or a directory without `config.json`; the driver not seeded -- eight rungs, eight sentences, each with its fix (`use a llamacpp backend…`, `apogee train setup --with mlx`, `apogee config add-backend <name> --type mlx --model-path <model directory> --force`, `apogee check --fix`). Construction, the factory's skip reason, `check` and `models list/info` all ask the one function.
- [x] **`check`'s MLX section**: the runtime -- skipped off Apple silicon, skipped while nothing needs it, a warning with the fix once an `mlx` entry does, and a pass that says it saw `mlx-lm`'s files and imported nothing -- and the seeded driver against the shipped copy; an `mlx` entry's row in Config from the ladder (a dangling directory fails, as a dangling GGUF does). **`models list/info`** show an `mlx` entry's directory, `model_type`, profile, sampling and the ladder's verdict, never "remote".
- [x] **`backends/local_prompt.h/.cpp`**: the schema statement (26f) and the system join, moved out of `llamacpp.cpp` so both local runtimes shape a prompt one way.
- [x] **Tests**: `mlx_protocol_test` (golden lines both directions, every chunk size), `mlx_local_test` (the provider over a scripted driver that answers requests, and the ladder), `mlx_driver_test` (the shipped driver on bare python3 under stub `mlx`/`mlx_lm` packages), the assets, config and `check` cases; on the real binary `cli.mlx_lifecycle` (`mlx_e2e.sh`) and the MLX phase of `cli.complete_opens_no_listening_socket`, both over the real seeded driver under the stubs through a fake environment (`mlx_fake_runtime.sh`).

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| Where the driver lives | `assets/mlx/` compiled by the **same generator** into a third unit; seeded at `training/scripts/mlx_generate.py` *(2026-10-04)* | One Python environment, one owner (the confirmed default); a layout row of its own would be a second home for the same environment's drivers. |
| How calls come back | **Typed `tool_call` events from the driver** where `mlx-lm` parses the model's format or the reply is Llama 3's JSON form; the profile filters for what streams as text (gpt-oss) *(2026-10-04)* | The spec's "calls come back as text the existing profiles parse" would have printed Qwen's, Gemma 4's and Llama 3's calls as answers: the profiles parse gpt-oss's native form and nothing else. The 25b lesson -- the runtime that rendered the template reads its calls -- carried over; nothing in `agentloop/` learns the word MLX. |
| Llama 3's JSON reply | **One call**, the first object, naming an offered tool with an arguments object *(2026-10-04)* | Llama 3.2 3B emitted two `;`-separated calls and Llama's own template then refused the history ("only supports single tool-calls at once"); llama.cpp's reader takes one too. |
| Reasoning | Split in the driver by the tokenizer's markers; gpt-oss's channels by the C++ profile *(2026-10-04)* | Gemma 4's `<\|channel>thought` markers are `mlx-lm`'s knowledge, not the profile table's. |
| Sampling | The 26h ladder resolved in C++, `generation_config.json` the model-file rung, `do_sample: false` read as greedy | The confirmed default: llamacpp's fields and semantics; the driver guesses nothing. |
| A cache that cannot be cut back | **A deep-copied checkpoint at the last prompt's history boundary** -- the rendering without the generation prompt, when it is a token prefix *(2026-10-04)* | Measured on Gemma 4: every prompt read 0 from the cache (its sliding window, past its size, cannot be trimmed); a checkpoint one token short of the prompt's end saved one step of three, because Gemma 4's generation prompt after a tool result opens a thinking channel the next render drops; at the history boundary every step inside a turn reused. `mlx-lm`'s own server deep-copies prompt caches. Taken only when the cache is untrimmable, so a full-attention model pays nothing. |
| Side requests | The same child, a cache of their own | A second copy of the model for a title is what the persistent child exists to avoid. |
| Cancellation | A `cancel` line, polled between tokens, answered `done cancelled`; the child ended after 10 s of silence; SIGINT ignored by the driver *(2026-10-04)* | Ctrl-C reaches the whole process group; a driver killed by it costs a reload. |
| Closed stdin | Ends the driver: in flight cancelled, nothing queued started *(2026-10-04)* | "Close stdin and it exits", promptly. |
| A sink that throws | The generation is cancelled and its `done` read *(2026-10-04)* | Otherwise the next turn reads the last one's tail; found when mutation showed the stale-id filter untested. |
| The window | The entry's `context_size`, else 0 | 27b reads the real one from `config.json`. |
| `serve` | Not refused by type, requests serialised by a mutex, unexercised *(2026-10-04)* | 27c's decided policy and its conformance case; a credential exclusion should not grow a runtime. |
| Detecting `mlx-lm` | The package's files (`__init__.py`, `_version.py`), never an import *(2026-10-04)* | Construction runs for every entry at every startup, and importing MLX takes seconds; the driver's `missing_dependency` error is the second layer. |
| The fix named | `apogee train setup --with mlx` *(2026-10-04)* | Installs exactly the `mlx` set. |
| `check` | Off-platform skipped; not set up skipped until an entry needs it, then a warning; a pass names what it saw *(2026-10-04)* | "Never lies a pass", and a fresh install still passes. |
| `local_prompt` | Moved, not copied *(2026-10-04)* | Two copies of the schema instruction would drift; llamacpp's suite green unchanged. |
| Base models | `base_model` when the directory ships no chat template; a plain transcript stopped at the next user turn | 26r's rule, a file fact. |

**Verified on real weights** (one family at a time, each model loaded once, inference only over the SafeTensors snapshots on disk, a scratch `APOGEE_HOME` whose `training/venv` is a link to the owner's environment, `mlx-lm` 0.32.0; nothing else running beside it). Each family: one `chat --tools --verbose` of three questions -- the capital of France, `read_file` on a notes file and the secret word in it, the secret word again -- then `/exit`, stdout timestamped to show the answer arriving in pieces.
- **Meta** -- Llama-3.1-8B-Instruct: **pass.** Answers streamed (19 stdout chunks), `read_file` parsed from Llama 3's JSON reply and dispatched, "pineapple" carried to the third turn; the cache held across the loop (3055 of 3119, 3151 of 3200 … prompt tokens reused). The 8B model also tried `fetch_url` six times on the first question, each refused by the host gate. Earlier, Llama-3.2-3B: `complete` byte-exact `4\n` on a pipe, and the two-call reply above.
- **Qwen** -- Qwen3-VL-8B-Instruct: **skipped.** This `mlx-lm` cannot build the snapshot: `TypeError: ModelArgs.__init__() missing 1 required positional argument: 'tie_word_embeddings'`, said by the backend as the turn's failure, no driver left behind. Qwen3.8-27B was not substituted (excluded but where nothing else works; the brief named the VL snapshot). Qwen's `json_tools` path is covered by the stub suite.
- **Google** -- gemma-4-12B-it (`gemma4_unified`, built as `mlx-lm`'s `gemma4`): **pass.** Streamed; `list_directory` then `read_file` parsed by `mlx-lm`'s `gemma4` parser and dispatched; with the history-boundary checkpoint the loop's steps reused 1945 of 2054 and 2051 of 2091 tokens (before it: 0 at every step).
- **OpenAI** -- gpt-oss-20b: **pass.** Its harmony stream read by the existing `gpt-oss` profile -- the analysis channel to the thinking sink, `Paris.` alone in history -- its native `read_file` call parsed by the gate and dispatched, 1563 of 1606 tokens reused inside the loop.
- Every run: no driver left running after `/exit`. A turn after a change of question re-reads from near the start: the offered tool set changed (26g), as on llama.cpp.
- **No socket, on the real stack**: a `complete` on Llama-3.2-1B-Instruct with apogee's whole process tree sampled by `lsof -i` from start to exit -- 55 samples, the driver seen, **no internet socket of any kind** (listening or not; the driver runs offline), stderr empty, the answer streamed, no driver left.

**Guardrails, mutation-tested.** 31 mutants across the ladder's eight rungs, its remedy, construction, the side request's cache, the thinking switch, the cancel, the stderr tail, the stale-id filter, the base-model flag, the persistent child, the schema statement, the four `check` severities and nine driver behaviours (an unoffered tool, the safety net, cancel, closed stdin, the checkpoint's prefix test, the history boundary, the stdout redirect, the side cache, one JSON call). 28 caught as first written; three survived -- the stale-id filter, the checkpoint's prefix test and the one-call rule for a JSON list -- each now pinned by a test and caught. The no-listen phase was verified against a stub that listens on load: caught, naming the port.

**Not verified.** Performance (no tokens per second measured); the off-platform refusal on a real Linux or Windows host (unit-tested with every target); `serve` over an `mlx` entry; the chat under a pseudo-terminal (the `/exit` path ran piped).

### 2026-10-04 — `mlx-model-operations` (backlog item 27b): the store's `mlx/` row, pull, `convert --mlx`, and honest windows

**Why.** 27a ran an MLX model it was pointed at; nothing in the model store knew the format. A user could not pull an `mlx-community` build (the HF path refused anything without a GGUF), could not make one from a snapshot already on disk, and an `mlx` entry's window was its `context_size` or 0 -- so with no `context_size`, a chat never warned and never compacted (the table knows no local model). This item makes an MLX model a first-class citizen of the store, under the rules every other format already obeys.

**What was built**

- [x] **The `mlx/` row, declared once** (`modelstore/store.h/.cpp`): `kMlxFormat`, `<models>/<model>/mlx/<id>/`, rooted under the models directory -- runnable like a GGUF, never under `paths.hf_dir` -- its id the SafeTensors rule's (a digest over every shard's sha256). In `is_format`, `list_store_models`, `resolve_store_target` (model, `owner/repo`, handle, bare id, path), `find_abandoned_staging` and `remove_weights` (whole, its emptied format and model directories tidied); `StoredMlx`/`list_store_mlx` (every directory under `mlx/`, whole or not, so one that cannot load is listed and said), `stored_mlx_at`, `stored_mlx_name`/`stored_mlx_named`, `backend_type_for_format("mlx") == "mlx"`, and `commit_mlx` -- every file of a staged directory recorded in `apogee-snapshot.json` (a digest a download verified kept, the rest hashed under a stoppable `HashProgress`), then renamed to its id. The record gained `transform` (how a verb made the files, written only when set).
- [x] **The reader** (`modelstore/mlx_info.h/.cpp`): `read_mlx_info` reads a directory whole from its files -- `complete` only when the configuration parses and is the model's (not a download record), a tokenizer is there, every shard `model.safetensors.index.json` names is present and each shard's header fits its file with every tensor's data inside it; otherwise `problem`, "cannot load: ..." naming the file, with nothing guessed in its place. It reads the trained window (`max_position_embeddings` and its spellings, at the top level, then a composite's `text_config`/`language_config`/`llm_config` or an omni model's `thinker_config.text_config`, and the key that said so), `mlx-lm`'s quantization (bits, group, mode, a mixed recipe's high bits) or the publisher's (`quant_method`), the dtype, the shards and the bytes on disk; `mlx_format` from `config.json`'s `quantization` or a shard saved with `format: mlx`. `read_mlx_config` is `config.json` alone; `mlx_window` the window rule below.
- [x] **The window** (`backends/mlx_local`): `context_window()` is `mlx_window` over what `read_mlx_config` read at construction -- `context_size`, else 26a's `default_local_window` over the trained window (32,768, or the trained window when smaller), 0 only when the configuration cannot be read. The Harness's chain (`context_window_for_model`) was already right: configured, then the provider's own answer, then the table -- so the 80% warning and the 90% compaction now fire for an `mlx` chat where they fire for a GGUF chat of the same model.
- [x] **Pull** (`cli/models_pull`, `models/source_hf`): `HfListing` reads the repository's `library_name` and `tags`; `models pull <owner>/<repo>` with no file named, on a repository whose card says MLX (`mlx()`: `library_name: mlx` or the `mlx` tag, over SafeTensors) and that holds no GGUF, runs `pull_mlx` -- the snapshot file filter, a refusal before a byte moves when there is no `config.json` or no shard, "already here" when the published shard digests name a stored set, the tree ladder (`acquire_tree`) into a staging path the process claims, then the MLX rung (`read_mlx_info` whole), then `commit_mlx`; the output says which checks ran ("6 size(s) checked, 2 against a published sha256 (4 had none published); config.json, a tokenizer and 2 shard header(s) read whole"), the one line every surface says an MLX model in (`mlx_summary`), and the `config add-backend` that uses it. Ctrl-C through `InterruptScope`. `choose_file` is `resolve_file`'s decision over the listing in hand, so the GGUF path costs no second request.
- [x] **Convert** (`models convert <model> --mlx [--type 4bit|3bit|6bit|8bit|mxfp4|bf16|f16] [--from <id>]`): the precision, the backend's own runtime ladder (`probe_mlx_runtime`: Apple silicon, the environment, `mlx-lm`'s files) and the seeded driver asked before anything is announced; the conversion recognised by its record (`source: convert`, the source handle, `transform: mlx_lm.convert <precision>`); `models::convert_snapshot_to_mlx` -- refusals (not a snapshot, damaged, already quantized by `mlx-lm`, something at the staging path), the converter writing the staging directory itself, what lands read whole and checked for the bits asked, anything else removed -- watched growing against `estimated_mlx_bytes`; then the hash, said and stoppable; then `commit_mlx`. Ctrl-C at the conversion or the hash removes the staging and its claim: the store is left exactly as it was.
- [x] **The driver** (`assets/mlx/mlx_convert.py`, compiled into `assets_mlx.cpp` by the same generator and seeded beside `mlx_generate.py` at `training/scripts/mlx_convert.py`): `mlx_lm.convert`'s own `convert()` under the training drivers' protocol (`{"message"}`, `{"error"}` then a non-zero exit, a terminal record), with the 27a driver's guards before the first import -- the Hugging Face offline switches, SIGINT ignored (Apogee ends it), descriptor 1 pointed at stderr. `training/mlx_convert.h/.cpp`: the precision table, one argv shape and `script_mlx_converter` over the one script runner.
- [x] **Every verb asks the row**: `models list` -- a stored set is a row (`mlx`, or `cannot load` with why; quant and window in its note), an `mlx` backend's row reads its directory whole, `ModelRow` carries `format`, `quant` and `window` (JSONL fields), and a snapshot an MLX conversion recorded consuming folds like one a GGUF consumed; `models info` on an MLX handle or an `mlx` backend (`files:`, `quantization:`, `window:`, `size:`, its lineage from the record) and on a snapshot ("made from it" names MLX sets); `models delete` by handle, format, model or a backend's name (M7's rule); `models repair` against the set's record, then read whole; `check` -- Models rows read whole (a cut shard a failure naming `models repair <handle>`), an `mlx` entry's row failing on a directory that cannot load and stating its quantization and window, the conversion driver's row, interrupted staging a leftover `--fix` removes; `config add-backend <name>` filled from a stored set's name (`--type mlx --model-path <dir>`); completion offering MLX handles, backends over them and unregistered names; `models migrate` sending a flat directory carrying `mlx-lm`'s marks to `mlx/` and repointing the `mlx` backend that named it.
- [x] **Two fixes the new paths exposed.** `http_source` turned a `CancelledError` into an exception past the ladder, skipping its cleanup -- harmless until now, since no pull had passed a live token; it is now a failure (`"cancelled"`) the ladder removes its partial and staging after. And a tree download's `<name>.staging` had no owner marker of its own, so an interrupted one was a leftover only after an hour's silence; it is now claimed by `<name>`'s marker and found as soon as its owner is gone (and `remove_weights` takes that marker with it).
- [x] **Tests**: `modelstore/mlx_info_test` (goldens over built directories, the dishonesty guards), `store_test` (the row, `commit_mlx`, the tree's claim), `migrate_test`, `models/convert_test` (the MLX ladder), `source_hf_test` (the card, `choose_file`, the cancelled download), `training/mlx_convert_test` (the argv, then the shipped driver on bare python3 under a stub `mlx_lm.convert`), `backends/mlx_local_test` (the window), `cli/models_mlx_test` (the pull against a local fixture "source" -- the Hugging Face API served from a directory through the real HTTP client -- convert played in process, delete, add-backend, and the command line over the real driver on Apple silicon), `check_test`, `models_migrate_test`, `assets_test`; `support/mlx_model.h` builds model directories with real SafeTensors headers. On the real binary `cli.mlx_models_lifecycle` (`mlx_models_e2e.sh`); `cli.mlx_lifecycle`'s fixture model gained its one shard (the doctor now reads an entry whole) and the check of its window. 38 new unit cases and one script; the suite is 2299.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| Where the reader lives | **`modelstore/mlx_info`**, not the spec's `models/mlx_info` *(2026-10-04)* | The backend reads the window from it, and `backends/` (Data) cannot include `models/` (Business); A1 put model files as data -- the GGUF header reader, the cache arithmetic -- in `modelstore/` for exactly this reason. Its tests mirror it (`tests/data/modelstore/`). |
| Where an MLX model lives | Under the models directory, never `paths.hf_dir` *(2026-10-04)* | It is what a backend runs, like a GGUF; `hf_dir` exists for full-weight sets tens of gigabytes large. |
| Its id | The SafeTensors rule: a digest over every shard's sha256 | One rule for a directory of shards; Hugging Face publishes every shard's sha256, so a pulled build is recognised before a byte moves. |
| What makes a pull an MLX pull | **The repository's own model card** -- `library_name: mlx` or the `mlx` tag -- over SafeTensors, with no GGUF in it *(2026-10-04)* | A fact the repository states (`mlx-lm` writes both for every build), never a name; a repository with GGUFs still pulls a GGUF, the zero-dependency default; `--safetensors` still pulls a snapshot when asked. |
| The MLX rung of the ladder | **The directory read whole** -- configuration, tokenizer, every indexed shard present, each within its header -- before commit; said as "read whole", never "loads" *(2026-10-04)* | The GGUF rung's analogue; whether this `mlx-lm` builds an architecture is a load's to say (Qwen3-VL's `TypeError`, 27a). |
| The unset window | **26a's default over the trained window**, not the trained window raw *(2026-10-04)* | "The same warned-at-80%, compacted-at-90% behaviour a GGUF chat gets": a GGUF chat of the same model gets 32,768; MLX grows its cache with the conversation instead of allocating it, so the default is what bounds it. A model that declares no window gets the default, as a GGUF with no trained length does; `context_size` wins. |
| The converter | **A seeded driver over the script runner** (`mlx_convert.py`), not `python -m mlx_lm convert` *(2026-10-04)* | The protocol's framing, the offline switches, SIGINT ignored and descriptor 1 on stderr -- the 27a driver's discipline -- need a driver; one more file in the unit the generator already writes. |
| Precisions | `mlx_lm.convert`'s own: `4bit` (default), `3bit`, `6bit`, `8bit` at its affine group 64, `mxfp4`, unquantized `bf16`/`f16`; one `--type`, checked per engine *(2026-10-04)* | The spec's "nothing beyond what `mlx_lm.convert` exposes"; named as community builds end; `f16`/`bf16` mean a GGUF precision without `--mlx` and an unquantized MLX model with it, and each engine's own names are refused under the other. |
| A quantized source | Refused when `mlx-lm` quantized it; a publisher's `quant_method` (gpt-oss's mxfp4) converts *(2026-10-04)* | An MLX model runs as it is; gpt-oss's full release is what `mlx-lm` converts. |
| Off Apple silicon | `convert --mlx` refused by the backend's own ladder; `pull` lands it with a note *(2026-10-04)* | A conversion needs `mlx-lm`; a pull is files, and the note says where they run. |
| `--register` with `--mlx` | Refused, naming the printed `add-backend` *(2026-10-04)* | The chain registers GGUF backends; an MLX registration is one line. |
| A stored set's name | The repository part, its precision appended unless the name already ends with it *(2026-10-04)* | M7's "a stored model's name fills add-backend"; `mlx-community` builds already end in theirs, a conversion's would otherwise collide with its sibling precisions. |
| The pull's end-to-end case | **In process over a fixture transport**, no `HF_ENDPOINT` override added *(2026-10-04)* | Hermetic with no socket; an endpoint override would be a user-facing feature this item did not ask for. The command callback's dispatch (the card's verdict, then `pull_mlx`) is the two lines between the tested halves. |
| A cancelled HTTP source | A failure, not an exception *(2026-10-04)* | The ladder's contract is that any failure leaves nothing; an exception skipped it. |
| A tree's staging claim | `<name>.staging` owned by `<name>`'s marker *(2026-10-04)* | An interrupted pull's leftover found by its owner's death, as a convert's is. |
| The doctor's row | "conversion driver" *(2026-10-04)* | Training's section already has a `converter` row. |
| Real pulls and conversions | **Not run** -- the owner's rule for this item: no downloads, no conversions *(2026-10-04)* | Proven over hermetic fixtures instead; what real files could be read without writing was read. |

**Verified on real files** (read-only, a scratch `APOGEE_HOME` whose `training/venv` links to the owner's environment, `mlx-lm` 0.32.0; one `mlx` entry per family pointing at the SafeTensors snapshot on disk):
- **Meta** -- Llama-3.1-8B-Instruct and Llama-3.2-1B-Instruct: read whole (4 and 1 shards), bf16, **trained for 131072** (`config.json`'s own), window 32768. **The window machinery driven on Llama-3.2-1B** (a piped chat, model loaded once, `max_tokens` 24): turn 2 at **83% (estimated) warned**, turn 3 at **91% compacted** (summarised on the same driver as a side request), the next prompt 2,115 tokens, the follow-up answered from the summary, no driver left. The estimate (characters / 4) ran about a fifth above the driver's own count (21,276 tokens at the 83% turn) -- 26c's estimator, unchanged.
- **Google** -- gemma-4-12B-it (`gemma4_unified`): read whole, **trained for 262144** (from `text_config`), window 32768. Not chatted.
- **OpenAI** -- gpt-oss-20b: read whole (3 shards), "mxfp4 (as published)", **trained for 131072**, window 32768. Not chatted.
- **Qwen** -- Qwen3-VL-8B-Instruct: read whole (4 shards), **trained for 262144** (from `text_config`), window 32768. Not chatted: this `mlx-lm` cannot build it (27a).
- The reads: `models info` 0.01 s each, `models list` over all five 0.01 s -- every shard header read, no cache needed (M2's lesson, measured first).
- **Pull and convert on real weights: skipped by the owner's rule** (no `models pull` of an `mlx-community` ref, no `convert --mlx` on a real snapshot). Both are proven over fixtures: the pull against the local fixture source, the conversion through the real seeded driver under a stub `mlx_lm.convert`, and on the real binary a real SIGINT mid-conversion leaving the store byte-identical.

**Guardrails, mutation-tested.** 39 mutants, every one caught as first written (38 against the unit suite, one against the real binary). The row: `is_format` forgetting `mlx`, the type map, leftovers looked for in two formats, a tree's staging claimed by its own marker only, a verified digest hashed again, a random id, the trailing-slash rule. The reader's guards: a missing `config.json` read past, a shard's data never held to its size, the index never asked, no tokenizer accepted, the trained window used raw, a mixed recipe read as uniform, shard metadata ignored, a text model's window not looked for. The provider's window as the entry's alone. The pull: the MLX rung skipped, a failed tree's claim left behind, a cancel escaping the byte source, the card not read from `library_name`, digests not counted. Convert: the bits never checked, a failed converter's output kept, a conversion never recognised, an MLX-quantized source converted again, a cancel's claim left behind -- caught in process, and again by `cli.mlx_models_lifecycle`'s real SIGINT, its before/after listing naming the leftover marker and `mlx/`. `check`: stored sets unchecked, an entry's files unread. Delete: MLX sets unplanned, a backend's set not found. Migrate: every flat directory a snapshot, an `mlx` backend not repointed. List: stored sets unlisted, an MLX conversion consuming nothing. `add-backend`: MLX names not filled. The driver: the library's print on the protocol channel, the group never passed, an existing output written over.

**Not verified.** A real `mlx-community` pull and a real `mlx_lm.convert` (the owner's rule); the pull command's two-line dispatch on the real binary (no endpoint to point it at); a repair re-fetching an MLX set's file from Hugging Face (the snapshot repair it reuses is tested); off-platform refusal of `convert --mlx` on a real Linux host (the ladder is the backend's, unit-tested with every target); a chat over a converted or pulled set on real weights.

### 2026-10-04 — `mlx-depth` (backlog item 27c): vision, serve, and the training shortcut

**Why.** 27a ran an MLX model and 27b made it a citizen of the store; three things still made the track feel bolted on. An MLX vision model answered "no" to every picture (`accepts_images` was false by construction); `serve` routed an `mlx` entry -- it was never on the vendor-CLI predicate -- but nothing exercised it, so the policy was an accident rather than a decision; and the train → try loop on Apple silicon still went fuse → convert to GGUF → quantize before a tuned model could be chatted with, although the fused SafeTensors are exactly what the `mlx` backend runs. This item closes all three on shapes already shipped: the one media pipeline (26d/26e), the serve plane's refusal semantics (Milestone T), the closure-driven promote plan (Milestone Z) and 27b's store row.

**What was built**

- [x] **Vision through `mlx-vlm`, as a capability the files state.** `inspect_mlx_model` reads a vision model from its directory -- `config.json` declares a `vision_config` object *and* an image processor's configuration ships beside it (`preprocessor_config.json`, or Gemma 4's `processor_config.json`), what `mlx-vlm` needs to read a picture -- and `probe_mlx_vision` adds the environment: `mlx-vlm`'s package under `training/venv` (`find_mlx_vlm`: its files, its version from the `mlx_vlm-<v>.dist-info` record, else `version.py`), never an import. `MlxLocalProvider::accepts_images` answers from that, before any turn and at no load's cost, so every surface's `--image`, `--attach` and `@path` route to it through `Harness::can_read` exactly as to an mtmd-equipped llamacpp entry -- nothing in `agentloop/` learned the word MLX. A non-vision MLX entry (or a vision one without `mlx-vlm`) answers no, and its image goes through 26e's helper path: described by the `vision` role, said as such, or refused naming the role and -- new in the one shared message -- "an mlx backend over a vision model with mlx-vlm installed".
- [x] **The driver's vision mode** (`assets/mlx/mlx_generate.py --vision`): the model loaded through `mlx_vlm.load`, its format -- the template, the reasoning markers, the call parser -- from `mlx_lm.utils.load_tokenizer` over the same directory, so a vision reply is read by the same `Reader` a text reply is (Qwen's calls by `mlx-lm`'s parser, stop strings, the cancel between tokens). A message's content may be parts: each `{"type": "image", "image": <data: URI>}` decoded into a private temporary directory the turn removes, handed to the processor in prompt order where the template places them, through `mlx_vlm.stream_generate`; a remote URL is refused ("never fetched"), as is an empty or non-base64 URI, and the model stays loaded. Sampling is `mlx-lm`'s sampler and processors where this `mlx-vlm`'s step takes a `sampler` (asked of its signature), its own `temperature`/`top_p`/`repetition_penalty` knobs where it does not. `ready` gained `vision` and `mlx_vlm`; a missing `mlx-vlm` is the driver's `missing_dependency` naming `apogee train setup --with mlx-vlm`, a load it refuses is `load` "through mlx-vlm". No turn keeps a cache in this mode (`cached_tokens` 0, and `--verbose` says "read whole each turn").
- [x] **The protocol, extended rather than versioned** (`backends/mlx_protocol`): a message carrying a picture crosses as its parts in order (text messages stay strings, byte for byte the 27a golden line), and `ready`'s `vision`/`mlx_vlm` are parsed. A driver from before 27c ignores `--vision` and never says `vision`, so it is never sent a picture: the provider refuses the turn naming the stale copy and `apogee check --fix`.
- [x] **One driver, swapped once.** A turn carrying a picture is answered by a child started with `--vision`; a text child already running is ended the ordinary way (stdin closed) and the model loaded again through `mlx-vlm`, which then answers every later turn -- text included -- so a chat never loads back and forth. Text-only conversations keep 27a's path and its cache untouched. Before anything is spawned the provider refuses what the model cannot read: a picture to a model that reads none (with the reason and its fix), a non-`data:` image, any audio.
- [x] **No silent capability claims.** `check`'s MLX section gains a `vision` row -- `mlx-vlm <v> is present (its files, not imported by check)`, a warning naming the vision entries that cannot see and the fix when one needs it, skipped when none does, the whole section skipped off Apple silicon. An `mlx` entry's Config row says "a vision model, reading images through mlx-vlm" or that without it it does not; a `default_vision` pointed at a non-vision MLX entry warns exactly as one at a projector-less llamacpp entry does (and names `train setup --with mlx-vlm` when that is the gap), a `default_transcription` at one says an mlx backend hears no audio; the Attachments rows reach MLX through the same `medium_gap`. `models info` prints `vision:` for an `mlx` entry and `models status` marks a vision role on one `[mlx: reads images as they are]` or why not.
- [x] **`train setup --with mlx-vlm`**: a fifth requirement set (`mlx-vlm>=0.3`), installed on demand only -- the confirmed default -- and named by every refusal.
- [x] **Serve routes MLX, structurally.** Nothing in `httpserver/` changed: the conformance suite gains a case that puts the real `MlxLocalProvider` (over a scripted driver) behind the listener-free mux and has a stock-client streamed `/v1/chat/completions` answered from it, a second unstreamed turn on the same child, while the vendor-CLI entry beside it is refused by type; and the type-refusal case now asserts the predicate over every type -- the four vendor CLIs refused, `mlx`, `llamacpp`, the cloud APIs and the mock not.
- [x] **The training shortcut: `train promote --target gguf|mlx`.** One plan, one branch (`training/promote`): `PromoteTarget` named for the store's formats, never a backend; for `mlx` the fuse writes straight into the store's staging (`incoming_path(…, "mlx", model)`, claimed), the command's verifier reads it whole (`read_mlx_info`, the store's MLX rung), and **the converter and quantizer are never called**; then `commit_mlx` under the weights' own id with an `apogee-snapshot.json` record (`source: train`, `ref: <backend> v<N>`, `transform: promote run <id>`), then -- only then -- the config through the one editor: a new `type: mlx` entry appended, or an existing `mlx` entry's `model_path` replaced in place, byte-exact. The ledger entry carries `mlx_path` instead of `gguf_path` (a GGUF ledger reads and writes exactly as before); `max + 1`, retention (an MLX version's stored directory removed whole, never one a kept version shares, never the active one) and the rollback target are the same code over `VersionEntry::weights()`. The target defaults to what an existing `--as` entry runs, else `gguf`; an asked-for one that disagrees is refused, as is `--quantize` with `mlx`, and a backend's versions never mix (`target_conflict`; a rollback refuses to cross kinds). `--keep-fused` is a note (the fused weights are the version). An MLX promotion builds no converter, so it needs no `convert` set; registered off Apple silicon, it is said that the entry cannot run here yet. `train versions` lists `WEIGHTS` (`mlx/<id>` for an MLX version), `rollback` prints which, and `check`'s ledger rows read an MLX version's directory.
- [x] **A child inherits its three pipes and nothing else** (`platform/child_process`, found by this item's served sample on real weights): the driver, spawned by `serve` on the first request, held that client's connection -- cpp-httplib's `accept` leaves the socket inheritable on macOS, and `posix_spawn` handed it on -- so the connection would have stayed open for the driver's whole life. `start_child` now spawns with `POSIX_SPAWN_CLOEXEC_DEFAULT` on macOS and closes from descriptor 3 on glibc 2.34+ (`posix_spawn_file_actions_addclosefrom_np`); every child -- MCP servers, vendor CLIs, the trainers, `ffmpeg` -- gets the same. The served phase of the no-listen check now also fails on a driver holding a socket of any kind, and was verified against the leaking build.
- [x] **An earlier Apogee's drivers refresh on upgrade** (the converter tree's rule, one level up; `contracts/assets`): seeding is skip-if-present, so an install that ran 27a kept its `training/scripts/mlx_generate.py` -- the owner's real install holds exactly that copy (its digest read, nothing written) -- and a picture sent to it would be refused naming the stale driver. Now `assets/retired-scripts.txt` records every version of a seeded driver an earlier Apogee shipped (27a's `mlx_generate.py`; v0.1.0's `train_mlx.py`, `train_peft.py` and `prepare_dataset.py`, whose bytes changed in a 2026-10-04 docs commit; `mlx_convert.py` has had one version), compiled in as `bundled_scripts_retired()`; `seed_bundled_assets` runs `refresh_seeded_scripts` before its skip-if-present pass, so `apogee check --fix` -- and through it `make install` and both installers -- replaces a seeded driver matching a retired digest with this build's and says `updated`, and keeps any other difference as the user's edit. `check`'s driver and `script:` rows tell the three apart (`inspect_seeded_script`: matches; "an earlier Apogee's copy, unedited -- 'apogee check --fix' brings it up to this build's"; your edit), and `uninstall` counts a stale driver as Apogee's. `scripts/generate_training_assets.py` appends the committed copy's digest whenever it finds a driver changed, so the next edit cannot forget it.
- [x] **The mock trainer's fuse is a whole model directory** -- the base's configuration and tokenizer files (a minimal `tokenizer_config.json` where the base has none) beside a SafeTensors shard with a real header and the adapter in its metadata -- so an MLX promotion is exercised end to end with nothing installed; its GGUF path is unchanged.
- [x] **Tests.** `mlx_protocol_test` (the parts golden, `ready`'s new fields), `mlx_local_test` (the capability flipping with each marker and the package, the image round-trip in place, the text → vision swap and no swap back, a pre-27c driver refused, the three refusals before a spawn), `mlx_driver_test` (the shipped driver under a stub `mlx_vlm` beside the stub `mlx_lm`: bytes reaching the model in place, calls/stop/cancel in vision mode, refusals, the legacy knobs, the missing package, a refused load, `--vision` absent loading as before), `check_test` (the vision row's three states, the Config phrase, the role pointers), `models_mlx_test` (`info`/`status`), `chat_attachments_test` (a real MLX provider: native for a vision model, described by the role for a text one, refused naming the mlx way with neither), `handler_test` (the served stream, the predicate), `promote_test` (the targets, the MLX artifacts and every failure leaving nothing, numbering/retention/rollback/no-mixing), `store_test`, `train_test` (the command line end to end on the mock), `python_env_test`. The scripted driver moved to `tests/support/fake_mlx_driver.h` so three suites share it. On the real binary: `cli.mlx_lifecycle` gained the vision phase (without `mlx-vlm` the doctor warns and a picture is refused naming the way; with it `chat -m <vision entry> --image` is answered from the picture's own bytes on one load; a text entry's picture described by the vision role) and the shortcut (a mock run promoted with `--target mlx` and answered from the store's `mlx/` row; promoted again through the mlx trainer's own `train_mlx.py` fuse over a stub `mlx_lm.fuse` in an environment holding no `convert` set; rolled back); `cli.complete_opens_no_listening_socket` gained a served phase -- `serve` started over an `mlx` default, a streamed turn requested, the whole tree sampled: serve's pid must listen, no descendant may hold a socket of any kind, the driver must be seen. 21 new cases (with `platform/child_process_test`'s inherited-descriptor case). The upgrade refresh: `assets_test` (an earlier Apogee's `mlx_generate.py` and `train_mlx.py` replaced under a test's list while an edit and a missing driver are left; **27a's real driver**, pinned as `fixtures/mlx/mlx_generate-27a.py` from `ad5124c`, stale under the compiled-in list, Apogee's for `uninstall`, brought up to this build's by the one seeding path and an edit of it kept; the compiled-in list sorted, naming only shipped drivers, never their current bytes, holding every earlier version, and equal to `retired-scripts.txt` as the generator compiles it) and `check_test` (the stale row's words and remedy for the MLX driver and a trainer, the same bytes an edit under another list) -- 4 more cases; and on the real binary `cli.mlx_lifecycle` lays 27a's driver into an install: `check` calls it an earlier Apogee's copy, `check --fix` says `updated` and leaves this build's, and an edit of it is kept and reported as one. The suite is 2324.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| What marks a vision model | **`config.json`'s `vision_config` object and an image processor's config file** (`preprocessor_config.json` or `processor_config.json`) *(2026-10-04)* | File facts, never a name; the processor file is what `mlx-vlm`'s loader needs, so a tower without one cannot read a picture. Every vision snapshot on this machine (Qwen3-VL, Qwen3.8, the Gemma 4 family) carries both; Qwen3-Omni nests its tower and is not claimed. |
| When the driver loads through `mlx-vlm` | **Only for a turn that carries a picture**; a running text child is then swapped once, and the vision child answers every later turn *(2026-10-04)* | Keeps 27a's text path -- its session cache, verified on real weights -- untouched for every conversation that never attaches a picture, and the first turn of `chat --image` spawns the vision child directly (nothing preloads but `serve --preload`). Swapping back would reload the model on every image. |
| The format in vision mode | **`mlx_lm.utils.load_tokenizer` over the same directory** beside `mlx_vlm.load` *(2026-10-04)* | The template, reasoning markers and call parser are `mlx-lm`'s knowledge (27a's lesson); one `Reader` reads both modes. Both tokenizers this machine's VLM snapshots ship carry the image-aware template. |
| A cache in vision mode | **None: every turn read whole** *(2026-10-04)* | Image embeddings enter through `mlx-vlm`'s own generation; reusing a cache across picture turns is not a stable surface of its API. Honest in `--verbose`; a later optimisation if it is. |
| Protocol version | **Still 1; images and `vision` are additions** *(2026-10-04)* | A bump would refuse every installed, unedited 27a driver even for text. The `vision` field is the handshake: a driver that does not say it is never sent a picture, and is refused naming `check --fix` when one arrives. |
| Sampling into `mlx-vlm` | **`mlx-lm`'s sampler where the step's signature takes one, else its own knobs** (`temp`/`temperature`, `top_p`, `repetition_penalty`) *(2026-10-04)* | `mlx-vlm` is not installed here and its signature moved across releases; asking it is the only way not to pass a knob it silently ignores. |
| What reaches the driver | **`data:` URIs only, written to a private temporary directory per turn**; audio and remote images refused before a spawn *(2026-10-04)* | "A remote image is never fetched" (26e's rule); a picture the provider cannot read must fail loudly rather than be dropped by `plain_text()`. |
| `mlx-vlm` floor | `mlx-vlm>=0.3`, its own set (`--with mlx-vlm`) *(2026-10-04)* | The confirmed default (on demand); the driver asks the 0.3-era surfaces (`load`, `stream_generate`, the step's signature). |
| The promote target's default | **What an existing `--as` entry runs, else `gguf`**; a disagreeing `--target` refused *(2026-10-04)* | A second promote into an `mlx` entry should not need the flag; a version never changes a backend's type, since rollback repoints `model_path` alone. |
| One kind per backend | **`target_conflict` before anything is built; rollback refuses to cross** *(2026-10-04)* | A deleted entry's ledger could otherwise collect both kinds and a rollback point an `mlx` entry at a GGUF. |
| Where an MLX version is written | **Fused straight into the store's claimed staging** (`incoming_path`), committed by `commit_mlx` *(2026-10-04)* | No copy of a model-sized directory, an interrupted promote's staging found by its dead owner like any other, identical weights landing on the directory they already occupy. |
| Regime and cycle | **Unchanged: they still promote GGUFs** and refuse an `mlx` entry by name *(2026-10-04)* | "Distillation changes beyond the promote target" are out of scope; `train promote` is the shortcut's surface. |
| Off Apple silicon | **An MLX promotion registers and says the entry cannot run here yet** (the backend's own ladder), rather than refusing *(2026-10-04)* | 27b's pull precedent: files land where they are made, and the note says where they run. |
| `--keep-fused` with `mlx` | A note, not a refusal *(2026-10-04)* | The fused weights are the version; nothing more is kept or dropped. |
| The ledger field | **`mlx_path` beside `gguf_path`, one or the other written** *(2026-10-04)* | A GGUF ledger reads and writes byte-for-byte as before (the admin plane's shape too); `weights()` is the one accessor retention and rollback use. |
| A stale seeded driver | **The converter tree's retired-digest refresh, extended to every seeded driver** (`retired-scripts.txt`, `refresh_seeded_scripts`) rather than a protocol bump or a forced overwrite *(2026-10-04)* | The precedent already answers "Apogee's copy or the user's edit?" by digest; a forced overwrite would destroy an edit, and a bump would refuse a text-only 27a driver for nothing. The trainers had the same gap (their bytes changed in a comment-only commit after v0.1.0) and the same few lines cover them. The digests are recorded by the generator from the committed copy, so an uncommitted intermediate never counts as shipped. |
| A served connection held by the driver | **Fixed at the platform seam: a child inherits its three pipes and nothing else** *(2026-10-04)* | Not a listening socket, so the invariant held -- but a connection kept open for the driver's life is a hang for any client reading to end of file, and the cause (`posix_spawn` inheriting every descriptor without CLOEXEC) is the seam's, not serve's: `accept` cannot be made atomic-CLOEXEC on macOS, and the next inherited descriptor would not be a socket. The only spawn that wants more is `run_foreground` (`$EDITOR` on the terminal), which is untouched. |

**Verified on real weights** (one family at a time, each model loaded once, inference over the SafeTensors snapshots on disk referenced read-only, scratch `APOGEE_HOME`s whose `training/venv` links to the owner's environment with `PYTHONDONTWRITEBYTECODE` set, `mlx-lm` 0.32.0; afterwards nothing under `~/.apogee` or `~/.cache` was newer than the run's marker):
- **Serve, streamed to an OpenAI-style client** (`/v1/chat/completions`, `stream: true`, read as the SDK reads SSE; `apogee serve --port 0`, loopback), the server's whole process tree sampled with `lsof` for the request's life:
  - **Meta** -- Llama-3.2-1B-Instruct: **pass.** 82 content chunks, first after 2.5 s (the load), done in 3.1 s, `[DONE]` last, `finish_reason: stop`; a second question 0.04 s to its first chunk on the same driver (one driver under serve); serve listening in all 16 samples, the driver seen, **no socket of any kind on it** -- after the fix above. Before it, the same run found the driver holding the client's connection (`localhost:<port>->localhost:<client>`, its descriptor 4).
  - **Google** -- gemma-4-12B-it: **pass.** Streamed, "The capital of France is Paris.", 13.0 s to the first chunk (the load), 66 samples, no socket on the driver, none left after SIGTERM.
  - **OpenAI** -- gpt-oss-20b: **pass.** Streamed, the analysis channel kept out of `content` by the gpt-oss profile, 29 samples, no socket on the driver.
  - **Qwen** -- **skipped.** This `mlx-lm` cannot build the Qwen3-VL snapshot as text (27a's `TypeError`), and loading it as a vision model needs `mlx-vlm`, which is not installed; Qwen3.8-27B was not substituted (excluded but where nothing else works).
- **The training shortcut** (Meta, Llama-3.2-1B-Instruct, base referenced read-only by path): a LoRA (8 layers, 40 iterations, batch 1) on eight pirate-voiced exchanges trained in 10.4 s; `train eval` on a two-item suite passed 2/2 through the mlx trainer's candidate runner; `train promote --as pirate --target mlx` fused, read whole, committed under `models/<model>/mlx/a2c59eab9f92/` and registered `type: mlx` in **6.6 s, no GGUF written**; `complete -m pirate "Do you like music?"` answered "Arr, a good sea shanty never fails, matey!" (3.2 s, the load included) and a piped chat answered two turns in the tuned voice, one novel ("Arr, the winds o' fortune have brought me to the shores o' the seven seas, matey!"); `train versions` listed `mlx/a2c59eab9f92 <- active`, `check` "active v1 of 1, and the backend points at it"; no driver left. On the way, `mlx_lm.lora` refused `--batch-size 2` over eight examples: the trainer's validation split is a copy of the first tenth (one line), smaller than the batch -- a pre-existing edge of `train_mlx.py`, recorded for the owner.
- **Vision on real files, without `mlx-vlm`**: `check` read the Gemma 4 (12B) and Qwen3-VL snapshots as vision models ("a vision model, but mlx-vlm is not installed, so it does not read images as they are"), warned in the MLX section naming both and `train setup --with mlx-vlm`, warned on a `default_vision` pointed at Gemma 4; `models info` said why for each family (Llama and gpt-oss "not a vision model"); `complete -m gemma12 --image` refused the picture naming the mlx way. **The native image question itself was skipped**: `mlx-vlm` is not installed in the owner's environment and the run's rule forbids installing it -- the path is proven against the scripted driver, the stub-module suite and the real binary under a stub `mlx_vlm`.

**Guardrails, mutation-tested.** 63 mutants, run in a scratch copy of the tree (never the checkout), each rebuilt and run against its suite: 62 caught as first written; one survived -- the driver never removing a turn's pictures -- because the test's `TMPDIR` override was silently ignored (see below), and is now pinned by the stub recording each picture's path and the test requiring the directory gone, then caught. The provider (16): the capability never/always, each marker and its object test, the package's name and version, the remedy, `--vision` never passed, a text driver never swapped or a vision one swapped back, a stale driver sent pictures, a picture to a blind model, a remote image, audio, images never detected, the cache line. The protocol (3). The driver (9): `--vision` ignored, a remote image fetched, the pictures not passed, the sampler never or the legacy key wrong, pictures kept, the cap reported as a stop, an empty image accepted, `ready` saying no. `check` (7) and `models` (2): sight claimed (also off Apple silicon), the row always ok, the entries unnamed, the phrase inverted, the remedy lost, an MLX ledger directory read as a file, both `models` lines inverted. Serve (1): `mlx` on the vendor-CLI predicate. The plan and the ledger (13): fusing into the run, falling through to the converter, never verifying, keeping an unreadable directory, recording a GGUF, pruning a directory as a file, sharing by `gguf_path`, a rollback crossing kinds or reading a directory as a file, no conflict, every version a GGUF, the ledger field written or read wrong (2). The command line (11): no inference, `--quantize` and a disagreeing target allowed, the conflict unasked, a llamacpp entry registered, the GGUF header verifier on a directory, rollback over an edited type, pruning an MLX version as a GGUF, the listing's prefix, the mock fuse without a tokenizer -- and **the converter built for an MLX promote, caught only on the real binary** by `cli.mlx_lifecycle`'s promote through `train_mlx.py` in an environment with no `convert` set. The platform (1): children inheriting everything. The refresh (10 more, the same way, all caught as first written): seeding never refreshing, a stale driver read as an edit or an edit as stale, a refresh overwriting edits or recording nothing, a stale driver not Apogee's for uninstall, the MLX drivers left out of the refresh, `check` calling a stale copy current or an edit, and `check` ignoring the injected list. By hand, on the real binary: the served no-listen phase against a stub that listens on load (caught, naming the port) and against the build that leaked the client's connection (caught, naming the socket).

**Found on the way, not fixed (left for the owner).** `ChildCommand::extra_environment` cannot override a variable the parent already has: its entries are appended after `environ`, and both `getenv` and CPython's `os.environ` take the first -- so an MCP server's configured `env:` that names an inherited variable (a `PATH`, say) is silently not applied. And `train_mlx.py`'s validation split (a copy of the first tenth, at least one line) is smaller than `--batch-size 2` on an eight-example dataset, which `mlx_lm.lora` refuses.

**Not verified.** A real `mlx-vlm`: its `load`, `stream_generate` and step signature are coded to the 0.3-era surfaces and exercised only under a stub, so the first install may meet an API this item guessed differently -- the driver then fails the turn with mlx-vlm's own words and the model stays loaded, and `check` passes only on files. No real picture was read natively by any family. Performance of a vision turn (no cache in that mode, by decision). The glibc 2.34+ branch of the descriptor fix on a real Linux host (CI's Linux runners are its first compile; the macOS branch is what the tests ran here). An MLX promotion off Apple silicon on a real host (its note is the ladder's, unit-tested with every target). `serve --preload` swapping to a vision driver on a served picture (unit-tested on the provider, not driven through serve).

## Milestone AC — Model suites

**Goal.** A root chat model working with designated small helpers -- a research suite on the 12B with a 3B for the chores, a fast one all on the 3B -- switched as one word rather than by rewriting `models.default_*` by hand. The suites spike (2026-10-03) found the idea half-shipped: six roles in one resolver (`harness/roles.h`), `models status` printing the whole implicit bundle and which rung chose each backend, and co-residency already the architecture (a live provider per backend, each with its own idle-unload clock). What was missing was a **name** for a bundle, a path for the root to **delegate**, and any **validation** between members. Track 27's suites items (27d–27g) and the long-term vision built on them -- symphonies, the execute surface, the Orchestrator (27q–27t) -- open here.

### 2026-10-04 — `model-suites` (backlog item 27d): a named bundle of models

**What was built**

- [x] **The `suites:` config unit** (`contracts/config.h/.cpp`): `suites.<name>` with an optional `description` and `members:` -- one per role it speaks for, named as the roles are (`chat`, `embedding`, `extraction`, `vision`, `transcription`, `utility`; `suite_role_names()`), any subset. A member is a backend's name, or a mapping with `backend` and the two knobs that make a helper small: `context_size` (a positive window) and `toolset` (words of `suite_toolset_names()` -- the five native toolsets, `web` for fetch_url and web_search, `mcp` for every MCP server's tools; `[]` for none). The loader refuses, by key: a role that is none, a member naming no backend, a window under 1, a toolset word that names nothing, one backend pinned two ways by two members, a suite named `off` (any case), a case-folded collision. Suite-level keys besides `members:` and `description` are not declared until something consumes them -- 27f's `consultable:`, 27g's `validate:`, 27t's `orchestrate:` hang there, beside `members:`. A commented example joins the starter config, and its chain comment gains the rung.
- [x] **`models.default_suite`** -- the suite every role resolves under, "" for none, and a name with no `suites:` entry fails the load ("models.default_suite: no suite named 'reserch' under suites: (configured: research)"). In the file it is the default; in the config a running session resolves against, it is that session's **active** suite. `active_suite(config)` finds it (whitespace ignored, as on every pointer).
- [x] **One new rung in the one resolver** (`harness/roles.h/.cpp`): `override > entry backend > the active suite's member > role pointer > conversation (a helper) > models.default`, reported as `ResolvedFrom::Suite`; `suite_role(ModelRole)` names a role as `members:` does, and `is_named(from)` -- a pointer *or* a member -- replaces the four places that asked `from == RolePointer` to mean "the config names this role's backend" (`named_utility`, the media refusal, the attachments' embedder, `models status`), so a suite's helper never reads as unset. With no suite active the rung answers nothing, and the chain is the one from before.
- [x] **The pins hold where they bite**: `suite_pins(config, backend)` is the one reader of a member's knobs, matched by backend name. The provider factory builds every entry as `backend_as_run(config, name)` -- its `context_size` the active suite's pin -- so a llamacpp or mlx member's model is loaded at the member's window; `Harness::context_window_for_model` reads the same, so the budget, the 80% warning and compaction measure against it; `backends::rebuild_providers` builds named entries again over the ones registered, and `commands::activate_suite` (cli/helpers) is the one switch -- the session's config and the harness's (`Harness::set_active_suite`) set together, exactly the backends whose window the switch moves rebuilt, every other provider left with its model loaded. The toolset pin: `commands::pin_toolset`/`apply_toolset` narrow a registry to the pinned toolsets after `tools.disabled` and any agent policy (a pin only narrows), `tools::toolset_of` filing each registered tool under its word by reading the registration itself; applied by `chat` (re-offered when `/model` or `/suite` moves the pin, the 26g selection rebuilt over the narrowed set), `complete`, `analyze`, and `serve` (a narrowed registry and ranker per served backend the suite pins, chosen per request by its backend).
- [x] **`chat --suite <name>`** (completable as `MODEL_SUITE` -- `SUITE` is the training track's eval suite) and **`/suite [name|off]`** in the one command table, completing the suites and `off`. The chat's suite is the flag, else what the chat last had, else `models.default_suite`; the banner names it. `/suite <name>` re-pins and rebuilds through `activate_suite`, and the suite's chat member takes the conversation as `/model` would (said: "switched to fastroot"); `/suite off` restores the global pointers and leaves the conversation where it is (said); `/suite` alone names the active suite and its members. A suite named at launch moves a resumed chat to its chat member too; `-m` still wins. The session saves its `suite` -- the name, or "" for one turned off, which a resume keeps off even under a default -- and a chat that never had one writes no key at all; a saved suite since deleted resumes under the default with a `suite_missing` warning. Retrieval's per-turn config re-read carries the chat's suite, so the embedder and the judge resolve under it.
- [x] **Refused at use, never routed around**: `operations::validate_active_suite` -- a member naming a backend the config does not have -- is checked by `chat` (at launch and before a `/suite` switch), `complete`, `analyze` and `serve`, in the existing words with the fix: `no backend named 'gone' (configured: …) -- suite broken's utility member; fix it with: apogee config set-suite broken --utility <backend>`. Without it the router's default rung would answer from `models.default`, a model nobody chose.
- [x] **The config verbs** (`cli/config_suites.h/.cpp`), every write through the one editor: `config add-suite <name> --chat/--embedding/--extraction/--vision/--transcription/--utility <backend>` with `--context-size ROLE=N`, `--toolset ROLE=a,b`, `--description`, `--force`; `config set-suite <name>` changing members one at a time (`--<role>` keeps the member's pins, `--context-size`/`--toolset`, `--unpin ROLE`, `--remove ROLE`), each change spliced in place by `set_suite_member`, so every other line of the entry -- its comments included -- stays as it was; `config delete-suite` (refused while it is the default, naming the way out); `config set-default-suite <name|off>`; `config get` of `models.default_suite`, `suites`, `suites.<name>`, `.description` and `.<role>`. The shared rules -- a name not `off`, at least one member, configured backends -- are `operations/suites.h/.cpp`, one copy for the CLI and the admin plane.
- [x] **The editor** (`contracts/config_edit.h/.cpp`): `append_suite` (members in role order, the short form unless a member pins), `delete_suite` (its exact inverse), `set_suite_member` (replace a member's lines, insert one in role order above the comment leading the next, or remove one; a `members:` written as a one-line flow mapping refused rather than misread), `set_default_suite`.
- [x] **The admin twins** (`httpserver/admin_suites.h/.cpp`, seven routes): `GET/POST /v1/admin/suites`, `POST /v1/admin/suites/default`, `GET/PUT/DELETE /v1/admin/suites/{id}`, `PUT /v1/admin/suites/{id}/members` -- each the CLI's transform under the CLI's rules, each write answering `restart_required`; `config_drifted` counts the suites and the default suite, and a role's `from` in `GET /v1/admin/backends` can now be `suite`. `serve` resolves under the config's default suite, said once at startup, never per request.
- [x] **`models status --suite <name|off>`** and the rung where it answered: a `suite:` header line (with the description), `(via suite research)` on each role the suite spoke for, a local member's cache at its pinned window, and `[suite pins window 4096 · toolset fs,git]` on a pinned backend. **`check`** gains a row per suite: its members, a member at nothing failed with `apogee config set-suite … --<role>` as the fix, a vision or transcription member that cannot read its medium warned as the pointer would be, a suite with no members warned.
- [x] **`cli.one_role_resolver` extended**: a suite member looked up by role (`members.find(`, `.at(`, `[`) outside the resolver, the config engine, the doctor and the suite verbs, twins and rules fails the scan -- verified against a lookup planted in `chat.cpp`.
- [x] **Tests**: `roles_test` (the golden below; the rung's place in a table; the whole matrix -- six roles × override × pin × conversation -- with a suite active against the rule stated once; `is_named`, `suite_role`), `config_test` (both member forms, every refusal by its message, the default suite, the pins and `backend_as_run`), `config_edit_test` (round trip and inverse, member surgery against a hand-commented suite, byte for byte), `harness_test` and `llamacpp_harness_test` (the window the harness measures and the context the scripted runtime is asked to make -- 4096 with the suite, 32768 without), `factory_harness_test` and `helpers_test` (rebuild exactly the re-pinned; the toolset narrowed, and the provider's recorded request carrying exactly it), `toolsets_test`, `session_test`, `models_test` (whole-output goldens with and without a suite), `check_test`, `config_cmd_test`, `chat_completer_test`, `complete_sources_test`, `operations/suites_test`, `admin_suites_test` (every edit byte-identical CLI ↔ HTTP), the parity, route-count and free-text tables; on the real binary `cli.suites_lifecycle` (`suites_e2e.sh`).

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| Where the active suite lives | **In the config the resolution runs against** -- `models.default_suite`, set in a session's in-memory view by `--suite`/`/suite` -- rather than a `RoleRequest` field *(2026-10-04)* | The spec sketched `RoleRequest` gaining the suite's name. Some twenty call sites resolve through `harness.config()` or a config handed down (the media helpers, the rerank judge, the embedder, compaction, titles, recall); a per-request field would have to be threaded to each, and the one forgotten would silently ignore the suite -- the drift the one-resolver rule exists to prevent. Riding the config, every caller resolves under the session's suite by construction, `serve` and the other surfaces under the file's default with nothing per request. The rung is still one rung in the one function, reported as `Suite`. |
| A default suite naming nothing | **Fails the load**, naming the configured ones *(2026-10-04)* | Unlike a pointer at a missing backend (validated at use, since a backend may be built or skipped), a suite is config-internal; a typo would otherwise run every command on the global pointers without a word. It also makes the editor refuse deleting the default suite, which `delete-suite` and its twin pre-check in words that name the way out. |
| What a pin belongs to | **The member's backend, while the suite is active** -- whichever role it is answering for *(2026-10-04)* | One backend is one provider with one loaded model and one window; a pin cannot follow a role. Two members pinning one backend two ways are refused at load. |
| How a window pin takes effect | The factory builds from `backend_as_run`; a switch **rebuilds** exactly the re-pinned backends *(2026-10-04)* | llama.cpp's context is made at the window it is given -- a cheaper path would claim a window the model does not run at. Construction is lazy, so a rebuild costs the next use's load, and only for the backends whose window moved. |
| Toolset vocabulary | The native toolsets plus `web` and `mcp`, declared in `contracts/` beside the parser that refuses any other word; `tools::toolset_of` reads each set's names off its registration *(2026-10-04)* | The parser sits below `tools/`; declaring the words beside it (the `lora_methods()` precedent) lets a typo fail at load, and a test holds the list to the toolsets that exist. |
| A member naming a backend the config does not have | **Refused where the suite is put to use** -- chat, complete, analyze, serve -- in the existing "no backend named" words; `check` fails the row *(2026-10-04)* | The router falls back to `models.default` for a name it cannot route, so a chat member at nothing would have answered from a model nobody chose; stricter than a pointer at nothing (which today falls back silently) because the suite is new and nothing depends on the old silence. |
| `/suite` and the conversation | A suite's chat member takes the conversation as `/model` would; with none, or `off`, it stays where it is *(2026-10-04)* | Re-resolving the chat role with no member would land on `models.default` and yank the chat off the backend it is talking to. |
| The session's suite | Saved as the name, "" for off, absent for never *(2026-10-04)* | "" keeps an off chat off on resume under a later default; absent keeps a chat with no suite writing exactly the file it did before. |
| `set-suite` | **Member-level surgery** (`set_suite_member`), one member per change, rather than re-rendering the entry *(2026-10-04)* | The one-editor rule is about comments surviving; `add-suite --force` already replaces a whole entry for anyone who wants that. |
| Admin twins | Built -- seven routes, one per CLI verb and the reads *(2026-10-04)* | ADR 0002: every config verb has had a byte-identical twin since the control plane; a suites slice classified as a backfill would have been the first config verb without one. |
| `--suite` | On `chat` (and `models status`) only; every other surface follows `models.default_suite` *(2026-10-04)* | The spec's surface; `execute` (27s) adds its own, and a one-shot that wants a suite has the default. |
| Value type | `MODEL_SUITE` *(2026-10-04)* | `SUITE` is the training track's eval suite and completes eval files. |
| `off` | Reserved as a suite name, at load and in the editor *(2026-10-04)* | `/suite off` and `--suite off` mean none. |

**The no-suite collapse, held byte for byte.** Before the rung was added, the resolver as it stood answered a matrix of 1,728 questions -- `models.default` absent, set and whitespace; every role pointer unset, all set, or each alone; six roles × an override × a feature's pin (absent, set, whitespace) × a conversation -- and the table was written to `tests/fixtures/harness/roles_no_suite.golden`. After, the same matrix with no suite configured must reproduce it exactly, rung names and all; it does. `models status` with suites configured but none active is byte-identical to the pre-change binary's output on the same sandbox config (diffed on the real binary as well as pinned by a golden).

**Guardrails, mutation-tested.** 14 mutants, each rebuilt and run against its tests: the rung moved below the pointer and above the feature's pin, the member untrimmed, the pin ignored by the factory's entry and by the harness's window, the toolset pin ignored, the suite always saved, a member not counted as named, pin conflicts allowed, a dangling default suite let through, `activate_suite` rebuilding nothing, member insertion ignoring the comment it lands under, suite drift unreported over HTTP, `off` accepted as a name -- 14 caught.

**Verified** -- the full suite and `cli.suites_lifecycle` on the real binary in a scratch `APOGEE_HOME`: `config add-suite research --chat root --utility helper --embedding embedder` writes exactly the old bytes and the new block; `models status --suite research` says `utility: helper   (via suite research)` and plain `models status` is unchanged; `chat --suite research` answers from root, is **titled by helper** (the utility role -- the control chat with no suite is titled by root) and saves `"suite": "research"`; `/suite fast` switches to fastroot and the next answer comes from it; `/suite` survives `--resume`; `/suite off` keeps the conversation on fastroot, saves `""`, and stays off on resume after `set-default-suite research`; a new chat takes the default suite; a suite whose member's backend was deleted is failed by `check` and refused by `chat` in the existing words with no model answering; `__complete chat --suite` offers the suites.

**Real weights.** Not run: this item's guardrails pin the window and the toolset at the wire with scripted providers (the context llama.cpp is asked to make, through the scripted runtime; the tools a provider's request carries), and nothing here changes how a model is called.

**Not verified.** The window pin on a real llama.cpp or MLX model (the scripted runtime records the context it is asked for; a real load at a pinned window was not run); `/suite` under a pseudo-terminal (the e2e runs piped); the TUI's suites view (32d) is not built.
