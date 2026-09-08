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
- [x] **llama.cpp pinned** to commit `549b9d84` (the revision Ommi's local-inference path builds against), off by default, verified to compile as a linked library target with Metal on macOS arm64.
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
- [x] **Case-insensitive backend names with collision rejection.** The `backends` map is keyed by a case-folding comparator, which turns Ommi's silent-merge bug into a detectable one: a second key differing only by case fails to insert and both names are reported. Rejected at load *and* at add.
- [x] **Comment-preserving text surgery** (`source/harness/config_edit.h/.cpp`) — `append_backend`, `delete_backend`, `set_models_role`, `format_config`, plus section scanning and fold-collision detection. Line-oriented splicing that leaves every untouched byte alone, including CRLF terminators and a missing final newline.
- [x] **Atomic, validated writes.** `edit_config_file` parses the file before the transform, re-parses its output, and only then writes — via a temp file in the same directory, renamed into place. A transform that produced invalid YAML cannot corrupt the config, and a failed edit leaves the file untouched.
- [x] **The starter config** (`assets/config.yaml`) and the template compiled into the binary, generated from that same file and held byte-identical by a test — Ommi's template-drift test, ported.
- [x] **`~/.apogee/` + `APOGEE_HOME`** (`source/harness/paths.h/.cpp`), over a new `platform::home_directory()` so Windows resolves it correctly. The override relocates the whole tree, which is what makes config tests hermetic.
- [x] **The `apogee config` command family** — `init`, `path`, `add-backend`, `delete-backend`, `set-default`, `set-default-embedding`, `set-default-extraction`, `get`, `format`. Nine subcommands, registered by one line in `default_registry()`, exactly as the skeleton's scaffold promised.
- [x] **51 new tests** (81 total) — the golden-file byte-diff suite, a garbage-input battery, and a `cmake -P` end-to-end test driving the real binary through init → add → roles → delete → byte-identical round trip under a throwaway `APOGEE_HOME`.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| Config format | **YAML** | The item's open call framed this as YAML-needs-text-surgery vs TOML-round-trips-natively. That premise is false: comment preservation in toml++ is an open, unimplemented request ([#28](https://github.com/marzer/tomlplusplus/issues/28)) and it reorders keys alphabetically on serialize ([#62](https://github.com/marzer/tomlplusplus/issues/62)). Text surgery is mandatory either way, which removes TOML's only claimed advantage — so YAML, where Ommi's proven helpers port across. |
| Parse library | yaml-cpp 0.8.0, **read path only** | Never used to write. Marshaling a struct back to YAML strips comments and reorders keys, which is the whole failure this module exists to prevent. |
| Data directory | `~/.apogee/` + `APOGEE_HOME` | User decision. One identical layout on all six targets; the override is what lets tests write nowhere near a real home directory. |
| Transform shape | **Pure text in, text out** | A divergence from Ommi, where each helper read, edited, and wrote in one function. Separating I/O is what makes the golden suite filesystem-free and what makes re-parse validation and atomic writes possible at all. |
| `config get` on an api_key | Redacted unless `--reveal` | `get` output lands in terminals, screenshots, and shell history. Keeping secrets out of logs and off HTTP, then printing one on a bare `get`, would be the same mistake by a shorter path. |
| `format` scope | Whitespace only | Ommi's `FormatConfig` also sorted fields inside each entry. Dropped: moving a field line moves it out from under the comment explaining it. |

**Notes.** Two bugs worth recording, both caught by the tests rather than by review. **Insertion point:** appending at the end of a section put the new backend *below* the file's trailing comments — a comment does not terminate a YAML block, so `backends:` ran to end-of-file in the shipped template. Entries now go after the section's last *content* line. **Comment ownership on delete:** scanning forward to the next sibling key swallows the blank line and comment header that document the *following* entry. Blank and comment lines are now tentative — they extend an entry only when a deeper-indented field follows. Ommi has the first-order version of this flaw; the fixture test `deleting an entry does not swallow the next entry's comment header` pins the fix.

The one documented exception to byte-exactness: appending to a file with no final newline gives its last line one, since nothing can follow an unterminated line. Delete cannot know to take it back. Both directions are test-pinned.

yaml-cpp 0.8.0 opens with `cmake_minimum_required(VERSION 3.4)`, which CMake ≥ 4.0 refuses. `CMAKE_POLICY_VERSION_MINIMUM` is raised around that one `FetchContent_MakeAvailable` and restored immediately, so no other target inherits it. Revisit when yaml-cpp cuts a release past 0.8.0 — the fix is already on master.

`--config` no longer carries CLI11's `ExistingFile` check: `apogee config init --config <new path>` must be able to name a file it is about to create. A command needing an existing config reports the miss itself, with a message that names the fix.

---

## Milestone C — The harness core

**Goal.** The interface layer everything else is written against: one provider interface, one canonical message IR, one router. After this, "add a backend" means implementing an interface, and no surface, loop, or session file learns that a fifth vendor exists. Ommi's recorded lesson is that streaming, cancellation, multimodal content, and tool structures have to be in the interface on day one — adding any of them later means touching every implementation and every caller at once.

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
| Streaming shape | **`std::function` token sinks** | The item's stated default. Ommi-equivalent, simple in every backend, and it does not force the whole call stack to become coroutines. |
| Capability discovery | Interfaces + `dynamic_cast` **inside the Harness** | "Discovered rather than required" as the item asks, but the cast lives in one file. Callers get `can_embed(model)`, so a capability check cannot decay into a type-switch over backends. |
| Router rungs | The three named rungs only | Ommi has a fourth — "if exactly one backend is registered, use it". Deliberately not ported: it papers over an unset `models.default` in a way that stops working the moment a second backend is added, which is exactly when the user has least idea why routing changed. A clear "set models.default" is the better failure. |
| Index collisions | **First registration wins** | Two entries may declare the same `model:`. Letting the later one win makes routing depend on map iteration order — a bug that reproduces on one machine and not another. |
| `Usage` zero | "Not reported", not "zero tokens" | Different facts. A surface that renders "0 tokens" for a silent provider is lying, so `reported()` distinguishes them and `to_json` omits the block entirely. |
| Provider `complete` | Defaults to `chat` | Most providers have no cheaper single-turn endpoint; duplicating the call in each would only let the two paths drift. |

**Notes.** The transient-region contract is the one worth understanding. If a per-turn RAG blob reaches persisted history it is re-sent on every later turn, growing the prompt without bound and feeding the model context it was told was for one question only — silent, and expensive. Excluding three fields in every serializer anyone ever writes is the kind of rule that holds until it doesn't, so the fields live in a nested struct with no JSON conversion at all: the mistake does not compile.

The layering check needed a real guardrail rather than a convention. In Go the reverse edge is an import cycle and the build fails; in C++ a `#include "backends/…"` in the harness compiles fine and the layering is quietly gone. `harness.layering` greps for that edge, and it refuses to run against an empty source list so it cannot pass vacuously. It was verified in all three directions — clean tree passes, a violating tree fails with the offending file named, an empty tree errors.

`MockProvider` holds itself to the same cancellation contract real providers must honour — checking between chunks, not just at entry — because otherwise tests of cancellation prove nothing about the interface they are supposed to pin.

---

## Milestone D — The first real backend

**Goal.** Prove the hardest new C++ ground — streaming HTTPS, SSE framing, provider dialect mapping — on exactly one provider before tripling the surface. Everything built here is infrastructure the OpenAI and Google backends reuse, and everything Ommi rented from the `claude` CLI (the thinking stream, native tool use, web search) is re-sourced from native API features.

### 2026-08-26 — Anthropic Messages API backend

**What was built**

- [x] **The shared HTTP client** (`source/backends/http_client.h/.cpp`) — split into `HttpTransport` (one request; `CurlTransport` is the real one) and `HttpClient` (retry/backoff over a transport). Exponential backoff on 429/5xx, `retry-after` honoured and capped, and an injected sleeper so retry tests run instantly. Every curl handle is a `unique_ptr` with a custom deleter; no raw handle escapes.
- [x] **The SSE parser** (`source/backends/sse_parser.h/.cpp`) — a byte-fed state machine, shared by every streaming cloud backend. Handles repeated `data:` fields, comment/keep-alive lines, CRLF, unknown fields, and an unterminated trailing event.
- [x] **IR ↔ Anthropic translation** (`source/backends/anthropic_wire.h/.cpp`) — system-prompt lifting, image source blocks (base64 and URL), `input_schema` naming, tool results as user-turn `tool_result` blocks, server-side `web_search`, and stop-reason mapping.
- [x] **The provider** (`source/backends/anthropic.h/.cpp`) — streaming and non-streaming chat, `list_models`, and `count_tokens` for exact context accounting. A `StreamAccumulator` tracks indexed content blocks so interleaved text, thinking, and tool-argument deltas each land in the right place.
- [x] **The typed thinking seam** — `harness::ThinkingSink` added to `StreamOptions`. Thinking reaches its own sink and never the token stream, the returned message, or persisted history.
- [x] **Extended-thinking replay** — an Anthropic requirement Ommi never had to meet. A turn that thought and then called a tool must have its thinking block, signature included, sent back on the next request. Since thinking must not enter the IR, the provider caches the raw blocks (`ThinkingCache`) and splices them back itself.
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

**Goal.** Extract the model→tool→model loop behind an observer **before** the surfaces multiply, not after. That ordering is Ommi's most load-bearing sequencing lesson: it is what kept its four front-ends consistent, and what made deleting an entire front-end a local change rather than a rewrite. `apogee complete --tools` is the first consumer; chat and serve become thin adapters over the same `run()`.

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
| Web search | **Provider server-side tools only** | User decision. Ommi's local search meant scraping DuckDuckGo's HTML results page with regexes; the markup changes and the tool returns *nothing* rather than erroring. `fetch_url` — the durable half — is ported; searching is the vendors' job. Local models get `fetch_url` but no search in v0.1.0, and the registry seam stays open for a pluggable one. |
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

The PTY check (`tests/pty_startup_check.py`) is the only test here that sees what the user sees. `apogee` asks whether stdout is a terminal before rendering anything, so a pipe-based test exercises the branch that deliberately emits nothing — it would pass on a build that rendered garbage interactively. The script replays the escape codes to reconstruct the final screen, then asserts the startup notice appears exactly once on one row and that **no spinner frame survived**. Verified against a build that also wrote the notice raw to stderr, in the way OMMI-14 forbade: the check failed it, naming both rows.

---

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
| Session format | A single JSON file rewritten per turn | The stated default, Ommi's proven crash-safety shape. Append-only JSONL survives a crash equally well but turns "read the session" and "rewrite after compaction" into a replay. |
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

**Goal.** Link llama.cpp into the process and let the KV cache simply *stay alive* between turns. This is the largest single simplification Apogee takes over Ommi: an entire on-disk prompt-cache apparatus — cache files, fingerprinting, an M-RoPE replay self-heal, rules about what may never be written into a cache — existed only to move KV state between processes that could not share memory. In-process, all of it collapses into arithmetic over a token prefix.

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

## Milestone K — The install contract

**Goal.** Make v0.1.0 shippable, and do it by closing Ommi's dominant early bug class rather than by documenting it. Ommi lost real time to *silent install drift*: `make install` seeded one tree, `install.sh` another, the updater a third, and `check` validated a fourth — each list correct when written, diverging one commit at a time, and never failing loudly. The fix adopted here is structural: one layout declaration, and every install path reads it.

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

**The reference driver is documentation and test at once.** [`tests/reference_driver.py`](../../src/cli/tests/reference_driver.py) is the worked example a GUI author reads, and it checks that three independent paths agree: the concatenated `answer_delta` chunks, the `result` event's text, and what `apogee complete` printed in text mode. If they ever disagree, one surface has grown a behaviour the other lacks — the failure the shared Reporter seam exists to prevent.

**The schema document is pinned to the code.** A protocol document that drifts is worse than none: a GUI author trusts it, builds against it, and debugs Apogee for a fault that is in the prose. `cli.machine_schema_conformance` checks the vocabulary in both directions, so an event added without documentation, or documented without an implementation, fails the build.

**What this deliberately does not do.** No push channel (a driving GUI performs its own mutations by shelling out to `apogee config …`, so it already knows when to re-read); no socket, ever (`lsof`, sampled continuously while the child lives); no protocol representation of slash commands, which are terminal-REPL affordances a driver replaces with its own UI.

---

## Milestone N — Model operations

**Goal.** Model management, end to end: one shared resolver for the `models:` role pointers, the `apogee models` suite, a real GGUF header reader that `check` uses to tell a working model from a broken one, and — from 2026-09-07 — acquiring, quantizing, and repairing models from Hugging Face and the user's Ollama store without ever leaving a half-downloaded one on disk.

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

Adding `check_models` as a new function collided with an existing `check_models` that `run_checks` already called — an overload that would never have run. And the configured-path validation it was meant to add already existed in the Config section. Both were caught before they landed; what survives is one upgraded check rather than a second one beside it. `has_gguf_magic` is gone, its recorded Ommi lesson moved to the reader that now does the work.

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
- [x] **`source/models/source_hf.h/.cpp`** — Hugging Face, entirely new (Ommi had no such path): ref grammar, repository listing, and a download over the existing HTTP transport.
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

Ommi could always compare against a pinned digest because it *chose* its models. Apogee has no allowlist by policy, so most sources publish no digest at all. Three states have to stay distinguishable: a digest matched, a digest did **not** match, and no digest was ever published. Collapsing the third into the second would train a user to ignore the word. So nothing here ever prints the bare word "verified" — the live pull reports `no digest published, header ok`, which is exactly what happened.

**A live 460 MB pull** from `TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF` landed in 43 s, wrote its sidecar, and `repair` then reported `size ok, digest ok, header ok` — because the sidecar records the *on-disk* digest even when the source published none. That is the provenance/integrity split earning its place: at pull time there was nothing to compare against, and afterwards there is.

**Three things running it found that the tests had not.**

*`models list` could not see a pulled model.* Immediately after that 460 MB download the listing said "no backends configured" — it was built (in Milestone N's first half) from `backends:` alone, and a freshly acquired model is not in the config. It now lists both halves.

*A cloud model was reported as never pulled.* `gpt-oss:20b-cloud` **is** in the Ollama store; its manifest simply carries an empty `layers` array because the weights are on Ollama's servers. Telling that user to run `ollama pull` sends them to re-fetch what they already have. `store_has_manifest` now separates the two cases.

*Hugging Face answers 401 for a repository that does not exist,* not just for a gated one — it will not leak which. The message asserted "gated or private", sending anyone with a typo hunting for a licence to accept. It now names the likelier cause first.

**Guardrails, each mutation-tested (nine mutations, all caught).** Leaving the `.partial` behind, committing before verifying, allowing a `..` in a model name, printing a bare "verified", guessing a quantisation instead of refusing, treating a cloud manifest as local weights, corrupting one SHA-256 round constant, dropping the on-disk half of the listing, and claiming a model with no record is verified — each turns its own tests red.

Two of those mutations needed a second attempt, and both times the *mutation* was at fault rather than the test: the first "commit early" mutation also moved the cleanup, so it preserved the very property it was meant to break.

**A flaky test, caught by mutation testing rather than by a run.** Two listing tests shared one temp path, and the fixture's destructor removed the `.gguf` but not the `.json` beside it — so one test's sidecar leaked into the other and the result depended on order. Each fixture now gets its own directory, and the suite was re-run under randomised orders to confirm it.

**What is NOT done, and why.** Three of this item's acceptance criteria are unmet and the backlog document stays open for exactly them: **in-process `quantize`** (needs llama.cpp linked, which is off by default — the same constraint that shaped the GGUF reader), the **vision transform** for Ollama's combined text+vision blobs (an open question then, answered the same day in Milestone O: mtmd needs a separate projector, so the operation required is an *extract* rather than Ommi's strip), and **SafeTensors/dataset** downloads, which SPEC lists in scope and which only the GGUF path covers today.

---

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

**The question multimodal-vision owed model-acquisition, answered.** **mtmd requires a separate projector file.** Passing a text model as its own `mmproj_path` fails at `mtmd_init_from_file` ("Failed to load CLIP model"). So a combined text+vision blob cannot be used for vision as-is — but note the asymmetry with Ommi's finding: Ommi *stripped* vision tensors so the text model would load, and here the text model loads fine untouched. What a combined blob would need is the projector **extracted**, not the vision tensors discarded. That question was closed the same day: the registry showed Ollama already ships the projector as its own layer, so no transform was ever needed — see Milestone N's completion entry.

### 2026-09-07 — `model-acquisition` completed: quantize, and a plan the manifests refuted

The residue this item was deliberately reduced to on 2026-09-07 — three pieces that were not buildable in the same pass — is now closed. Two of them landed; the third turned out to be the wrong work.

**What was built**

- [x] **`source/models/quantize.h/.cpp`** — `apogee models quantize in.gguf out.gguf --type Q4_K_M` via `llama_model_quantize`, behind `APOGEE_ENABLE_LLAMA` with a refusal that names the flag. **Verified live: 5 GiB F16 → 1 GiB Q4_K_M in 11 seconds**, and the result loads and generates.
- [x] **`GgufInfo::file_type` / `is_quantized()`** — the header already knew.
- [x] **The Ollama projector layer**, read as its own file, with `models pull` fetching it alongside the model and printing the `mmproj_path` line to paste.
- [x] **A SafeTensors repository now names the conversion path** instead of a bare "no .gguf".
- [x] **11 new tests** (713 total), green in both builds.

**The plan was wrong, and one HTTP request said so.**

This item inherited from Ommi the idea that Ollama ships vision models as a **single combined blob**, and that Apogee would need a transform to deal with it. Milestone O had already narrowed the question — mtmd needs a *separate* projector, so the operation would be an *extract* rather than Ommi's *strip*. Before building either, the registry was simply asked:

```
llava:      model 4108.9 MB   projector 624.4 MB   license   template   params
moondream:  model  828.7 MB   projector 909.8 MB   license   template   params
```

**Ollama already ships the projector as its own layer** — `application/vnd.ollama.image.projector`. There is nothing combined, nothing to extract, and nothing to strip. The right work was not a transform at all: it was for the store reader to *notice a layer it had been ignoring*, and for `pull` to copy it. Two carefully-reasoned designs, one inherited and one derived from it, both retired by a `curl` that cost nothing.

The lesson is the cheap one: **a plan inherited from the reference implementation is a hypothesis about the world, and the world is queryable.** Ommi's finding was true when Ommi found it; it stopped being true, and nothing in the document said so because documents cannot notice.

**Quantize's error message, improved by running it.** The first live attempt was against an already-quantized model, and llama.cpp does refuse that — after a couple of hundred per-tensor log lines, by which point `requantizing from type q8_0 is disabled` has scrolled away and the user sees a bare failure. The header already carries `general.file_type`, so it is now caught up front in one sentence that names the way forward. Two false-positive tests guard it: an F16 input and a header with no `file_type` must both be allowed through, since refusing either would block the only path that works.

**Two UX faults, also from running it.** `quantize` announced "this reads and rewrites the whole model" *before* discovering it could not, which reads as a crash rather than a refusal; and `quantize types` was unreachable because CLI11 demanded the two positionals before the listing callback could run. It is `--types` now.

**Guardrails, each mutation-tested (five mutations, all caught).** Refusing without naming the flag, silently overwriting an existing output, ignoring the projector layer, dropping the SafeTensors conversion path, and failing to detect an already-quantized input.

**An observation worth recording for [model-profiles.md](../backlog/model-profiles.md).** The freshly quantized Llama 3.2 loads and generates — and answers with ChatML markers and prompt echo, because this GGUF ships no embedded chat template and the narrow name-matched registry falls back to ChatML. The **pre-existing** Q4 of the same model, which Apogee never touched, produces worse output still. So this is not a quantization defect: it is the quirk layer's absence, observed directly. Local models on this machine are not usably conversational until model-profiles lands, which is the most concrete argument for that item anyone has made so far.

**What SafeTensors does and does not do.** A SafeTensors repository is now refused with the `convert_hf_to_gguf.py` invocation and a note that the script needs Python with torch and transformers. Apogee does **not** run it: a C++ harness cannot assume that environment exists and should not install it on someone's behalf. Dataset downloads remain unimplemented and are not refused with a special message — they simply are not GGUF, and land in the same branch.

---

## Milestone P — Model profiles

**Goal.** The local-model quirk layer: a per-family profile registry with an explicit resolution ladder, and a streaming filter that keeps a model's private reasoning out of its answer.

### 2026-09-07 — `model-profiles`: characterized, and the premise was wrong

**The characterization came first, and it contradicted the item.** Three families were pulled and run before any code was written:

| Family | Embedded chat template | Observed |
|---|---|---|
| `gemma3` (1b-it Q8_0) | **yes** | Clean. Answered "Paris". Nothing to strip. |
| `qwen3` (3.6-27b Q4_K_M) | **yes** | **Emitted `<think>\n\n</think>\n\n4` — all of it reaching the user.** |
| `llama3` (3.2-3b, local files) | **no** | Degenerate on every prompt. |

This item was written from Ommi's Gemma 4, which shipped **no** chat template and had to be reverse-engineered — that was the case the bespoke-override slot existed for. **Gemma 3 ships a good template and needs no help at all.** The family that needed help was Llama, and its files here carry a content hash where a name should be and degenerate like base models, so nothing about it could be verified.

Had the profiles been ported from the reference implementation rather than characterized, Apogee would now carry a hand-written Gemma template that overrode a working one.

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

**What is NOT done, and why the item stays open.** Three acceptance criteria are unmet, and all three need model output nothing here produces: a **channel-header markup filter**, **control-token tool-call parsing**, and the display/parser opener-parity assertion that only matters once a parser exists. Ommi's evidence for those came from Gemma 4's control tokens; Gemma 3 emits none, and this item's own core constraint is that a profile is characterized from real weights rather than a published format. Building them blind is precisely the guesswork the constraint forbids, so they stay queued against a model that actually emits one.
