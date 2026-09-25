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

### 2026-09-25 — Chat input completion (backlog item 24)

Asked for directly (Taylor, 2026-09-25): Claude Code's `/` command list and `@` file mentions, in `apogee chat`. Specced and pulled to the top of v0.1.2 the same day, and built that day after the user's one call: until the attachments item ([26d](../backlog/attachments-documents.md)) lands, a sent `@file` mention stays plain text, with no stopgap that pastes the file's contents.

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

### 2026-09-19 — CI trimmed to the builds, and the first pull request's runner fixes

The first PR into `stable` was the first time CI ran the five non-macOS targets at all (the 2026-08-31 trigger change had made runs PR-only), and most of the checks failed or sat queued. Two decisions and four fixes came out of it, the user's call on the shape: **CI builds the executable for each platform, plus the llama.cpp job that ships with the tool, and nothing else** -- the format+clang-tidy job and both install-parity jobs are gone; formatting and lint are local gates (`make format-check`, `make lint`), and `cli.install_parity` was already a case in the suite every platform build runs. The fixes, each reproduced or confirmed against the runner images' own manifests: the Linux images ship no libcurl headers, so `find_package(CURL REQUIRED)` -- the first hard lookup -- killed the configure five seconds in (reproduced in an Ubuntu 24.04 container; `libcurl4-openssl-dev` fixes it); the Windows images ship no libcurl at all, so it comes from the image's vcpkg as a static library on the `*-windows-static-md` triplet, handed to the script through the new `APOGEE_CMAKE_ARGS` hook; `macos-13` was retired, so the x64 Mac job queued forever and now runs on `macos-15-intel`; and the Windows ARM job had "passed" in twenty seconds because Git for Windows' x64 bash reports the shell's architecture from `uname -m`, the script took the host for x64, deferred the ARM target "to CI" and exited 0 -- the script now reads the machine from the processor variables on Windows, and every runner passes the new `--no-defer`, which turns a deferral into a failure. The release workflow carries the same runner and prerequisite changes. **Running the suite on Linux for the first time found four things macOS had hidden**, each fixed here: GCC enforces C++20's designated-initializer order, which Clang only warns about (five initializers of `BuiltInToolOptions` in `analyze.cpp` and the permissions test were out of order); the Hugging Face byte source captured its cancellation token *by reference* in the closure it returned, so a caller's temporary token dangled -- glibc's allocator reuses the memory at once and every download read as "cancelled" (`source_hf.cpp` now captures by value; the token is a shared flag); the retrieval turn built its graph query as `cond ? "" : turn.question`, whose common type is `std::string` -- a temporary the `string_view` dangled on, so the entity search received garbage and the graph expansion was silently empty on Linux (`rag.cpp` now binds views on both arms); and two shell checks used BSD `stat -f` with a GNU fallback that also *prints* on GNU, so the mode comparison could never match (`mode_of` picks the flavour). The HTTP-API conformance script also needed `cmake_policy(SET CMP0057 NEW)` for `IN_LIST` under CMake 3.28 in script mode. **And the Intel Mac is gone** (the user's call, the same day, in any capacity): `macos-13` had been retired under it, and rather than move it to `macos-15-intel` the target was dropped from the matrix, the presets, the script's target list, the platform vocabulary and the installer, leaving five targets -- Linux and Windows on both architectures, macOS on Apple silicon.

**The second push, the same day, and the pipeline's real shape.** That first round of fixes went up and the merge-blocking job died in seven seconds, both Windows jobs in one, and both Linux jobs at the end of their suites; the user set the shape of CI in four calls while it was being taken apart. **macOS:** the runner's bash is 3.2 (the image manifest says so), and under `set -u` bash 3.2 treats an empty array's `"${a[@]}"` as an unbound variable -- the new `env_args` expansion was empty on every non-Windows runner and killed the script before configure. Linux (bash 5) and MSYS2 never see it; `cicd.sh` now expands its arrays as `${a[@]+"${a[@]}"}`. **Windows builds with MinGW-w64** (user decision): the Visual Studio generator and the vcpkg curl are gone; `msys2/setup-msys2` prepares the MSYS2 that both Windows images ship off PATH -- GCC in UCRT64 on x64, clang in CLANGARM64 on arm64, with cmake, ninja, pkgconf, MSYS2's git and the Schannel-built `curl-winssl` -- and the build and test steps run in that shell, which `cicd.sh` now recognises as a Windows host. The Windows presets are Ninja, linked `-static` against a static libcurl so the executable ships alone; FindCURL's imported target names only libcurl on MinGW, so `ApogeeDependencies.cmake` reads curl's static dependency closure from pkg-config and puts it on the target, resolving the alias curl's own CMake config makes of `CURL::libcurl` first. Cross-compiling the x64 target on macOS with Homebrew's mingw-w64 (GCC 16) against a from-source static Schannel curl found two dependency age marks before the runner could: yaml-cpp 0.8.0's `emitterutils.cpp` uses `uint16_t` without `<cstdint>` (the vendored target is compiled with `-include cstdint`), and cpp-httplib's non-blocking resolver calls `GetAddrInfoExCancel`, which the mingw-w64 headers do not declare -- that option is now off on every target, since httplib is only ever the server and Apogee's client is curl. Every target in the tree, tests included, compiles and links under that cross-compile; the ARM64 clang build and the suite's run on Windows are the runner's to show. **The clean-room job is gone** (user decision); `cicd.sh --fresh` remains a local command. **Three stages** (user decision): `clone llama.cpp` proves the pin resolves (`cicd.sh --clone-llama` reads it from `third_party/CMakeLists.txt`, the one place it lives, and fetches that commit at depth one), then `build <platform>` -- one row per target -- each packing its build tree into a tar artifact (tar because `upload-artifact` drops file modes; object files and clone histories stay behind), then `test <platform>`, the same rows running `cicd.sh --test-only` on that tree with nothing rebuilt. The two matrices are the same list on purpose; GitHub offers no way to declare one. **And every one of those jobs is required** (user decision, 2026-09-19, superseding 2026-08-24's "macos-arm64 only" gate): no `continue-on-error` anywhere in the file -- a platform that cannot pass is a platform to fix -- with the branch protection rule on `stable` listing the checks by job name. **llama.cpp is in every build** (user decision, the same evening, on asking why a separate `macos-arm64-llama` row existed at all): `APOGEE_ENABLE_LLAMA` had defaulted to OFF since the pin landed, with one CI job carrying it ON -- which meant the release workflow, building through the presets, would have shipped executables whose local inference answered "not built in". The default is ON now, every platform build and test run links it, the extra row is gone, and `make no-llama` is the developer's fast build. Turning it on everywhere exposed two more things a single opt-in job had hidden: llama.cpp's own `BUILD_SHARED_LIBS` default leaks ON into the cache for every subproject configured after it (the llama-enabled executable needed libllama, libggml, libmtmd, replxx and the schema validator as dylibs beside it), so the top-level build now forces it OFF before any dependency; and ggml builds `-march=native` by default, so a release built on a runner with AVX-512 would die on a user's older CPU -- `GGML_NATIVE` is off (AVX2/FMA/F16C on x86-64, armv8-a on arm64, armv8.2-a+fp16+dotprod on Apple silicon), OpenMP is off in favour of ggml's own pool, and the Metal library stays embedded. Cross-compiling it for Windows found one more: upstream's `mtmd-audio.cpp` uses `M_PI`, which MinGW's math header hides under strict `-std=c++20`, so the `mtmd` target gets `_USE_MATH_DEFINES` on Windows. With the option on by default: the macOS suite passes in full with llama.cpp linked (a 17 MB executable with no library beside it, the Metal library embedded), the Ubuntu container passes in full, and the MinGW x64 cross-build links an executable carrying the ggml and llama symbols and importing only Windows system libraries. A lesson from that last check, recorded in the presets: a preset's pinned compiler is a cache entry and beats a cross toolchain file's, so the cross-compile must name its compilers on the command line, or `gcc` on a Mac quietly becomes Apple clang and the "Windows" build is a macOS one. **And a local build fix found by the user's `make fresh`:** Apple clang, given no sysroot, falls back to the Command Line Tools' `MacOSX.sdk` symlink even when xcode-select names an Xcode, and a CLT newer than that Xcode (27.0 against Xcode 26.6) ships stubs its linker cannot read -- every link failed with "tapi error: malformed file". The CLI build root now sets `CMAKE_OSX_SYSROOT` to `macosx` before `project()` when nothing else names an SDK, so CMake asks xcrun and gets the selected toolchain's own SDK; a toolchain file, `SDKROOT`, or `-DCMAKE_OSX_SYSROOT` still win. **The Windows ARM job's real failure, found on the third look:** both ARM runs had died one second into the build step, and the first diagnosis -- the shell reporting its own architecture -- was half right. The runner's MSYS2 is an x64 build running under emulation, and for an emulated *64-bit* process Windows sets no `PROCESSOR_ARCHITEW6432` at all (it exists for 32-bit processes only), so the "processor variables" fix read AMD64, the script took the host for windows-x64, and `--no-defer` did its job: "cannot build windows-arm64 natively on this host". On Windows the script now takes the answer from `MSYSTEM`, which names the toolchain the shell was set up for -- CLANGARM64 is ARM64, UCRT64/MINGW64/CLANG64 are x64 -- and only falls back to the variables without one; an x64 shell then builds x64, which is what its toolchain does anyway. Two things make the next such failure readable without admin rights: `die()` emits a workflow error command on a runner, and every build and test step tees its output and, when it fails, hands the log to the new `lib/scripts/ci-annotate.sh`, which publishes the ctest summary and each failed test's own output as error annotations -- annotations are public on a public repository, the raw log is not. (The first version put the last forty raw lines in one annotation; ctest's dot-padded lines overflowed GitHub's 4 KB limit before the summary, which is how the script learned to compact and split.) **With every build green on the next run, the test stages spoke for the first time:** macOS passed, and the two Windows rows failed the same thirty-five tests -- eight of them the byte-exact pins of embedded assets to their shipped files. The repository had no `.gitattributes`, Git for Windows checks text files out with CRLF, and a text-mode read of a CRLF file on Windows gives back LF while the embedded copy keeps the CRLF. `* text=auto eol=lf` now, on every platform: the bytes a pin compares are the same bytes everywhere. The rest of the Windows list is the portability gap the release workflow's header has recorded since 2026-09-01 -- POSIX modes, path semantics, program resolution -- now measured rather than presumed, and every one of the thirty-five was run to a named cause and fixed the same day, four of them in the source: `find_on_path` split PATH on ':' (which cuts every Windows drive letter in half) and never tried PATHEXT, so nothing on Windows was ever found -- it splits on the platform's separator now and tries `.exe` and its siblings for a bare name; `models delete` refused an absolute path with `is_absolute()`, which on Windows does not cover "/etc/hosts" (root-relative) or "C:x" (a drive, no root), so both would have walked out of the models directory -- any root is refused now; the model acquirer removed a failed transfer's partial file while the stream writing it was still open, which POSIX allows and Windows refuses with a sharing violation, so the partial survived there -- the removal follows the stream's close now; and three places wrote paths into contracts a model or a remote client reads back (`search_files` output, the ingest source name, the agent API's `prompt_path`), each in the platform's spelling, so a Windows machine recorded `nested\deep\file.md` for what every other host calls `nested/deep/file.md` -- all three use the generic form now. The tests: a case whose name began with `/branch` never ran on Windows, because Catch2 reads a leading slash as an option prefix there; the config editor's YAML quoting rule (a Windows `model_path` carries a drive colon and is written quoted) is public as `yaml_scalar` now, and the byte-exact train pins go through it; the doctor tests expect the recorded `Skipped` where the platform has no POSIX modes and put the fake venv interpreter where the platform's layout puts it; a ledger fixture is built as JSON rather than by concatenating paths into a string; the "unwritable path" is a path under a regular file, unwritable everywhere; and the tests that need a spawned child or a POSIX `/bin/sh` say so. The three scripts: the no-listen symbol scan accepts `.obj` members and Winsock's `__imp_` decoration, the install-only check expects `apogee.exe`, and the config lifecycle compares paths in CMake's spelling. All of it verified as far as a Mac allows -- the suite here and in the Linux container, and the MinGW cross-compile of every Windows-only branch. **And then the test stage was removed** (user decision, 2026-09-20: "remove the tests from the CI/CD; if we are going to run tests, we should just run the tests for the source code"). CI is the clone and the five builds; the release pipeline builds and packages without the suite and still proves the staged binary runs; `--test-only` went with the stage. The suite is the developer's gate -- `make test`, `cicd.sh --test` -- run against the source before a push, on every platform the developer can reach. The Windows fixes stay: they are correct, and the suite is still run on Windows, by a person. **And a suite bug the developer's terminal found:** two command tests -- knowledge capture reading a piped conversation, and `datasets prepare` refusing on a pipe until the Python environment exists -- passed under ctest on every runner and in every container and failed in Taylor's terminal. The fixtures feed "piped" input by swapping `std::cin`'s buffer, while `stdin_is_piped()` asked the operating system about descriptor 0; the two agree only when nothing runs the suite from a terminal, which is to say everywhere except a developer's own shell. `stdin_is_piped()` now also reports true when `std::cin`'s buffer is not the one it started with, which is the truth of the matter: the input is read from there. The Linux runners also gain `lsof`, which the Ubuntu images lack and without which the no-listen scripts skip their socket poll and pass having checked nothing. The release workflow follows the MinGW change, and its verify step now runs on every target and outside the MSYS2 shell, which is where a static link is proven. **The Linux runs' failure is the one thing this round did not close.** Both Linux jobs built and then left `ctest` with exit code 8 -- failures in the suite -- and the job logs need admin rights the session did not have. The same command, as a non-root user with the runner's environment, in the Ubuntu 24.04 container passed all 1445 (the earlier "reproduction" had only rerun the parallel flakes serially); `stable` is the initial commit, so the PR's merge ref is the branch head; and the one prerequisite the image lacks, `lsof`, makes the no-listen scripts skip rather than fail. Whatever it is, it is now a required check: the test stage's log names the cases, and that is where the next round starts.

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

**What was NOT done at first, and why the item stayed open.** Three acceptance criteria were unmet, and all three needed model output nothing here produced: a **channel-header markup filter**, **control-token tool-call parsing**, and the display/parser opener-parity assertion that only matters once a parser exists. Ommi's evidence for those came from Gemma 4's control tokens; Gemma 3 emits none, and this item's own core constraint is that a profile is characterized from real weights rather than a published format. Building them blind is precisely the guesswork the constraint forbids, so they stayed queued against a model that actually emits one.

### 2026-09-07 — the gate opened: gpt-oss emits both, and Ommi's transcription was right

**The model this was waiting for.** `gpt-oss-20b` (MXFP4, 11.3 GiB, pulled from Hugging Face through Apogee's own acquisition ladder) emits **both** mechanisms. Ommi carries the same framing, transcribed from an Ollama manifest and marked, in its own words, unverified against generation *because the GGUF it had would not load in its bundled llama.cpp at all*. Apogee's pin (`549b9d84`) carries `LLM_ARCH_OPENAI_MOE`, so the thing Ommi could never check is checkable here. **It was right** — which is worth recording precisely because the last two characterization runs on this branch contradicted the plan.

**The bug, verbatim.** "What is 2+2? Answer briefly." reached the caller as:

```
<|channel|>analysis<|message|>The user asks: "What is 2+2? Answer briefly." The answer is 4.<|end|><|start|>assistant<|channel|>final<|message|>4
```

Every character of it, shown as the answer. And with one tool declared:

```
<|channel|>commentary to=functions.read_file <|constrain|>json<|message|>{"path":"/tmp/notes.txt"}
```

printed as prose while nothing dispatched — the two-symptom failure this item was written against, reproduced exactly.

**Why it is visible at all, and the design that came out of it.** llama.cpp's load log settles it: `<|channel|>`, `<|message|>`, `<|start|>` and `<|constrain|>` are set to **USER_DEFINED**, not CONTROL, so they detokenize into the stream even with `special = false`. `<|return|>` and `<|call|>` *are* control tokens and are end-of-generation, so they never reach the text and generation already stopped correctly. Ommi solved the visibility half with a per-template `show_special` switch; **Apogee needs none** — there is nothing to turn on, only framing to remove.

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
| Section shape | A **map** keyed by name, not Ommi's list of `- name:` items | The edit helpers, the loader's collision check, and `config get`'s dotted keys are all map-shaped. A list would have needed a second copy of every one of them for one section. |
| Chunk sizes *(open call, default taken)* | **On the collection entry**, written at first ingest | A corpus of ADRs wants 768 where prose wants 512, and re-typing that on every ingest is how corpora end up chunked inconsistently. Proven in the e2e: an ingest with `--chunk-size 100` and a re-ingest with no flags produce the same chunk count. |
| `auto_rag` arity *(open call, default taken)* | **One collection**, a top-level key beside `status_mode` and `color` | Several would mean merging incomparable score scales, which is `vector-hybrid-rerank`'s problem. |
| `--rag ""` | **The off switch for one run**, distinct from no flag | A key that cannot be overridden per invocation is a key people stop using. `flag_given` is therefore a separate input from `flag_value.empty()` — an absent flag falls through to the config, a present-but-empty one does not. |
| When `auto_rag` is read | **At turn build, from the file**, on every chat turn | So an edit mid-session takes effect on the next question like every other config value. `RagSettings` holds a *path*, not a value. An unreadable config costs that turn its `auto_rag` and says so; it never ends the turn. |
| Registration failure | **Reported, never fatal** | The corpus is already on disk. Telling a user their ingest failed because their config could not be edited would be a lie about what happened. Ommi reached the same rule. |
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
- [x] **`source/backends/embedding_batch.h/.cpp`** — the one shared piece: `batch_ranges`, splitting a list at a provider's maximum. Batch-first is a Core constraint (Ommi's graph item recorded per-chunk calls as its cost trap); the recorded default took the **documented maxima** as the batch sizes, 2048 inputs for OpenAI and 100 rows for Gemini.
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

**The live run found the judge's budget wrong.** At 256 tokens — Ommi's constant, sized for a Haiku judge — every rerank through gpt-oss degraded to raw order with *"returned something that was not a ranking."* Correct under the contract, and useless: a **reasoning** model spends its budget thinking before the array appears, and never reached its final channel. At 1024 the judge applied on every surface and visibly reordered and dropped. Recorded beside the constant.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| Spend *(user)* | Corpus needs a yes; questions don't | Above. |
| Vector search *(open call, default taken)* | Brute-force cosine over blobs | sqlite-vec waits for a collection that outgrows a linear scan. No query-vector cache: a cache that made the per-question call silent would hide the decision just made. |
| One collection per turn | Kept from Milestone Q | So the resolver is single-store; Ommi's per-store hybrid participation modes collapse to "this collection, or nothing, with a note." |
| Structured output for the judge | **Deferred**, behind the never-fail contract | The IR carries no structured-output request; the judge asks for a bare JSON array and the parser tolerates prose and fences. A misbehaving judge costs nothing but the improvement. |
| Judge timeout | The transport's, not a separate deadline | `CancellationToken` has no deadline; Ctrl-C cancels. A hung judge hangs the turn until then — recorded as a limitation rather than solved with a thread that abandons an HTTP call. |
| A transient embedding failure mid-turn | Degrade to lexical and *say so* | Honest about the scale the scores are on; never a failed turn for a flaky endpoint. |
| A source the embedder fails on during ingest | Stored lexical-only, **named** | The store then reports partial coverage and the resolver demotes it wholesale — the hole is visible until re-ingested, never hidden. |

**Guardrails, each mutation-tested (22 mutations, all caught).** In the resolver: auto resolving to hybrid; partial coverage ignored; a model mismatch ignored; an unqualified pin searched lexical silently; an explicit flag substituting instead of erroring; a degraded hybrid reported hybrid; the spend rule dropped. In fusion and the store: RRF reading scores; ranks off by one; lexical-only miscounted. In the judge: garbage flagged applied; an empty verdict treated as failure; the cap removed. On the turn: the explicit-vector error swallowed; an embedding failure failing the turn. A vanished judge resuming silently; `check` accepting a typo; metering not propagated. And four through the real binary: a vector ingest not recording its binding; `embed query` ignoring the hard error; `complete` dropping `--retriever`; `chat` not storing it on the session.

**Two harness lessons repeated, and one honest gap.** The format pass reflowed string literals and a call site after the tests were written, so two mutation patches and one message rewrite silently matched nothing — the no-op detection built in Milestone P is what said so, twice. And the `one_role_resolver` guard fired on two *user-facing messages* that named `models.default_embedding` in its dotted form; they were reworded rather than allowlisted, so the guard stays strict. The gap: with the mock backend nothing echoes the request, so a surface that announces a retriever and then splices nothing is invisible end to end — the unit guard asserts the secret *does* reach the provider on the vector path, and the live run shows the injected chunks answering.

---

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
| Sessions | **Asked for, never implied** | Ommi minted a session on every request lacking an id; a stock OpenAI client re-sends its whole history each call and would leave one transcript file on the server per request. `"session_id": "new"` mints, a known id continues, absent stays stateless — OpenAI semantics exactly. An unknown id is a typed 404, so an evicted session is a signal rather than a silent fresh context. |
| Concurrency | **One turn at a time** | Inference requests serialise behind one mutex; `/health`, `/v1/models` and the session routes answer meanwhile. A local model has one context and the cloud clients were never audited for concurrent use. Per-backend concurrency is a later item; a second request waiting beats a first one corrupted. |
| Vendor-CLI backends | **Refused by config type**, even when built | The Core constraint made visible: a request naming a subscription backend gets a 400 that says why, rather than a silent gap in `/v1/models`. The serve command skips them from the served set and announces it. |
| Client-supplied `tools` | **400**, not ignored | The server runs its own loop and returns only the final answer. A client waiting for `tool_calls` that never come is a worse failure than a clear refusal; an empty `tools` list (what some clients send by default) means nothing and is accepted. |
| `effort` → thinking budgets | **Not accepted** | The IR carries no per-request thinking budget to map it onto. Accepting a field the server cannot honour would be a promise without a product; it waits for the seam. |
| Thinking on a served stream | **Dropped**, recorded as the seam | Reasoning is display metadata and never part of a served response. A client wanting the model's working live would get it as its own meta-frame type; the status vocabulary exists to drive an indicator, not to carry a transcript. |
| `/v1/training/*` stubs, harness bare mode *(defaults taken)* | With the training item; a later flag | A route that returns nothing but 501 is a promise without a product. |
| Errors | The OpenAI envelope everywhere | A stock client library turns `{"error":{"message","type"}}` into its usual typed exception; Ommi's plain-text `http.Error` did not give it that. |
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

**The parity table is complete, or the build fails — and it fired on the first run.** Ommi's table was a list a human extended. Apogee's `tests/httpserver/parity_test.cpp` walks the real registry's subcommand tree and requires every leaf to be classified: a twin with its route, a backfill with its owning area, a carve-out with its reason, or read-only. The first run failed with *"unclassified subcommand: config format"* — a mutating command the grooming had not listed — and it got its twin (`POST /v1/admin/config/format`) before the test went green. A row naming a subcommand that no longer exists fails too. Every twin's route must be in the table, flagged as gated, and answer `401` unauthenticated.

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
| Mounting | Always *(default taken)* | Apogee always resolves a config path, so Ommi's "no config path → plane disabled" mode has nothing to carry. |
| Delete responses | `200` with a body, not `204` | `restart_required` has to ride somewhere. |

**Verified on the real binary.** `cli.serve_lifecycle` now drives the control plane: the token file is `0600`, an unauthenticated request, a wrong bearer and the right token in a query string are all `401` in the error envelope, an add-backend over HTTP is read back by `config get` and leaves **the same bytes** the CLI's add-backend leaves on the same starting file, and a session minted on the public plane arrives as `session.created` on the event stream — which never carries the token.

**Guardrails, each mutation-tested (31 mutations: 28 caught outright, 2 caught after their tests were strengthened, 1 equivalent -- below).** The gate accepting any bearer, a query-string token, an empty token, or running after routing; the token written under the umask's mode; the HTTP edit formatting its own entry; the view leaking the key; a literal key accepted from any peer; a `${ENV}` reference treated as literal; a collision not a `409`; a dangling role pointer accepted; `restart_required` blind to role drift or to membership; `fail` overwriting `cancelled`; `finish` overwriting `failed`; cancel not firing the token; cancel not idempotent; an unbounded queue; unsubscribe not closing; unnamed event frames; no heartbeat; a stream deaf to shutdown or to a departed client; a twin route removed; an admin row not flagged; the doctor passing a world-readable token; `--fix` leaving it; `session.created` never published; the events package including the server; and the reference missing a route.

**Two survivors, and what they taught.** "Cancel is not idempotent" survived because the test re-cancelled an already-cancelled job, which a re-cancelling mutant cannot be told apart from; it now cancels a *succeeded* job and requires it to stay succeeded with nothing emitted. "The client leaving does not end the stream" survived because the heartbeat path ends the stream too, twenty-five seconds later; a test now hangs up *on* an event write and requires the next event never to be sent. Both caught after.

**One equivalent mutant, recorded.** "The compare stops at the first differing byte" survives: constant-time-ness is a property of *how long* a comparison takes, and no result-based test can see it. The function is six lines beside a comment saying why it is written that way; the review is the guard.

**A harness lesson, the third of its kind.** The "no heartbeat" mutant did not fail the events test -- it *hung* it, because the test's fake client kept reading until a heartbeat arrived, and the harness sat on ctest's 25-minute default timeout looking finished. A test that waits for the thing under test to happen is a test that hangs when it does not; every streaming client in these tests now carries a deadline, a missing heartbeat fails in a second, and the harness runs ctest with a per-test timeout. The first run's harness also died decoding a test's output as UTF-8 after a byte-diff assertion; it now decodes leniently.

### 2026-09-13 — `provider-credential-store`: one key, one chain, and a type that cannot carry it

**The shape it inherited.** Apogee calls three vendors' APIs directly, so it owns the keys — where Ommi could hand Claude's auth to the `claude` CLI, Apogee cannot. Until today a key lived in `config.yaml` (`api_key: "${OPENAI_API_KEY}"`, expanded at load) or nowhere, and each cloud provider read `config.api_key` for itself: three readers of one field, and the day one of them grew a fallback the other two would not have it. The admin plane had already put the pieces on the table — the `0600` private-mode write, the loopback-peer rule for a literal key, the doctor's `Secrets` section, the parity table — so this item is mostly the discipline of using them once.

**What was built**

- [x] **`source/secrets/store.h/.cpp`** — `CredentialStore` over a `0600` `credentials.json` beside the config (following `--config`, exactly where the admin token lives), one slot per API-billing provider *type* — `anthropic`, `openai`, `google` — holding the key and when it was stored. The type that holds a key is private to the `.cpp`; the header hands out `CredentialMetadata`, which structurally has no key field, and **exactly one function returns a key** (`key_for`), called by the resolver and by nothing that renders. A store that cannot be read degrades to empty with a `warning()` naming the file — and `put`/`clear` refuse to overwrite it, because whatever is in a corrupt store may be someone's only copy.
- [x] **`source/secrets/resolve.h/.cpp`** — **the one key resolver**, the sibling of `harness/roles.h` for the same reason: the entry's `api_key` (already `${ENV}`-expanded) > the stored slot for its type > the conventional variable (`ANTHROPIC_API_KEY`; `OPENAI_API_KEY`; `GEMINI_API_KEY` then `GOOGLE_API_KEY`), from an `EnvSnapshot` **captured once** — Ommi's OMMI-9 lesson, where a token one path injected into the environment changed what another path resolved mid-run. `KeyResolution` reports which rung answered and which variable, so every surface can say *where* without ever saying *what*. Vendor-CLI types take no key at all: `slot_type("claude-cli")` is nullopt, and asking is refused with the principle.
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

**Goal.** The model's hands: Ommi's five bundled Python MCP servers as in-process C++ toolsets in the one registry every surface builds, and the permission gate's first real consumer — the `permissions:` schema, the prompt on every surface that has one, and the rule that a surface with nobody to ask denies.

### 2026-09-13 — `native-toolsets`: five toolsets, one gate, and a shell that asks first

**The shape it inherited.** Milestone F left a tool registry, a dispatch chain and a permission gate, with exactly one tool in them (`fetch_url`) and nothing declaring `writes`. The gate already resolved "ask with nobody to ask" to deny; what it lacked was anything to ask about, a config to read levels from, and a prompt on any surface. Ommi's tools were five Python servers spawned per session and reached over JSON-RPC, gated by a two-name suffix set (`write_file`, `delete_file`) and nothing else.

**What was built**

- [x] **`source/tools/`** — a guarded package (never `backends/` or `commands/`; `harness.layering` holds it): **fs** (`read_file` with a 64 KiB cap and its note, `write_file`, `delete_file`, `list_directory`, `search_files` skipping hidden directories) inside a root that is resolved through symlinks and required to be an ancestor **by path components** — Ommi's `startswith` sandbox let `/home/user` admit `/home/userX`; **shell** (`run_command` through `/bin/sh -c` with the directory and command as positional parameters that are cleared before the command runs, `[stderr]`/`[exit N]` trailers, a timeout that is a result); **git** (`git_status`, `git_log`, `git_diff`, `git_show` as `git` children with list arguments and a ref allow-list that starts with a letter or digit, so nothing reaches git as an option; `git_diff`'s review form `base...head` with fetch-on-demand and `ReviewDefaults` for the flags the agents item will set); **notes** (a scratchpad under a new `notes/` layout row, keys that can never be paths); **rag** (`search_documents`, `list_collections`, `collection_info` over the store, the retriever decided by the same `resolve_turn_retriever` the turn uses and named on every hit). `register_native_toolsets` is the one entry point; `destructive_tool_names` is the list the template, the doctor and the admin view all read.
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
| The shell | **Gated**, default `ask` | Ommi shipped it unrestricted with a docstring warning; a tool that can `rm -rf` is what the gate is for. `allow` is one config line. |
| Git | Shell out *(default taken)* | libgit2 is a dependency on six targets and a second ref resolver, for nothing the user's `git` lacks. |
| Permissions keyed by | **Tool name** | A two-field struct would need a new field per gated tool; a namespaced MCP tool fits the same key. |
| `fs_root` default | The home directory *(default taken)* | Ommi's default; a chat started from `/` would otherwise sandbox nothing. |
| The machine-mode prompt | The existing `question` event *(default taken)* | A `kind` field, no new channel; the "advertised iff someone can answer" rule already covers the no-driver case. |
| `always` | Per tool, not per target *(default taken)* | A per-path allow-list is a larger schema for a case `session` covers. |
| `git_diff` with a fetch | Read-only for the gate *(default taken)* | It updates remote-tracking refs, never the tree or history; prompting on every review diff trains the reflex the gate avoids. |
| The shipped `permissions:` block | **Active**, five tools at `ask` | `ask` is the default either way; an active block means `always` changes exactly one line rather than appending a section. |
| Windows shell | `cmd /C` *(default taken)* | A recorded per-platform difference, not a skip. |
| The shell's positionals | Cleared before `eval` | Found by the test: with the script's `$1`/`$2` still set, a command reading `$1` saw the working directory. `set --` first. |

**Verified on the real binary.** The doctor's `Tools` section on a fresh config, `config set-permission` round-tripping through `config get` and refused for a level outside the three (a parser exit, before the file is touched), and `complete --tools` on a pipe with the whole native set registered and nothing prompting.

**Guardrails, each mutation-tested (36 mutations: 35 caught outright, 1 caught after its test was strengthened).** The sandbox as a string prefix, or gone; `write_file`, `delete_file` and the shell ungated; hidden directories searched; the read cap silent; the shell's positionals visible to the command; a timeout reported as an ordinary exit; a ref allowed to start with a dash; the file argument reaching git unchecked; `never` fetching anyway; `auto` never fetching; the review defaults ignored; a note key holding a slash; the resolver's error ignored; hits not naming their retriever; a disabled toolset registered anyway; `run_command` dropped from the destructive list; a config `deny` reading as allow; session answers forgotten; `always` not written; an unknown answer allowing; the terminal prompt existing on a pipe; a closed driver denying instead of failing the turn; `tools.disabled` ignored by the registry; the served checker dropped; ask with nobody to ask allowing; the loader accepting `yes` as a level; `set_permission` accepting any level, or a path as a tool name; the doctor blind to a misspelt key; `restart_required` always false; the `PUT` permissions row ungated; `notes/` not a layout row; and `tools/` including a backend.

**The one survivor, and what it taught.** "`never` fetches anyway" survived because the test's `never` case asked for a ref that existed *nowhere*, so the real code and the mutant refused it with the same message. The case now names a branch only the remote has: the real code refuses, a mutant that quietly fetched would succeed. A refusal test has to use something that *could* have been found.

## Milestone W — The MCP client

**Goal.** The model picking up anyone else's tools: a from-scratch client for stdio MCP servers whose tools join the shared loop as first-class registry entries, with the subprocess discipline Ommi earned the hard way, `apogee mcp` and its admin twins over one scaffold core, and Apogee hosting a server of its own.

### 2026-09-13 — `mcp-stdio-client`: a client whose dead servers fail now, and a server with nothing to install

**The shape it inherited.** Milestone V had just put the native toolsets into the one registry and given every destructive tool a gate. The child-process seam had a bounded stderr tail from the vendor CLIs, the JSONL framer had already absorbed the chunk-boundary bug class, the events bus and the admin plane existed, and the scaffold pattern — a TTY-free core both the CLI and a route call — had been named but never built. Ommi's `src/mcp` was a complete, proven client with its own postmortem attached: a proxy that narrated its OAuth handshake straight onto the terminal, and a server that died mid-handshake and held the frozen status line for twenty seconds.

**What was built**

- [x] **`source/mcp/`** — `types` (JSON-RPC hand-rolled: ids the client allocates, a notification told from a response by the absence of one; `mcp__<server>__<tool>` split on the **first** `__`; `readOnlyHint` read from `annotations`, absent meaning destructive), `transport` (`Transport` as an interface so the test fleet is scripted transports; `StdioTransport` over `platform::ChildProcess` with the JSONL framer and a **mandatory** stderr sink — Ommi kept an inheriting constructor "for compatibility"; this API has no such door — plus the eight-line `StderrTail` that tees outside its lock), `client` (the reader started *before* `initialize`; calls demultiplexed by id; `mark_done` from the read loop's error path as well as `close()`, once, by compare-and-swap; notifications logged and dropped), `registry` (every enabled server dialled in order, `[mcp] connecting:` *before* the dial, a bound covering connect **and** handshake, a bad server logged and skipped, tools registered as `writes` unless read-only, `mcp.server.connected/disconnected` on the bus), and `serve_stdio` (the other side of the four methods; only read-only tools served, because the gate lives in the loop and a server has nobody to ask).
- [x] **`source/scaffold/mcp_server`** — `create_mcp_server`: a runnable Python server, its test and README from templates compiled into the binary, or an existing executable registered with `--command`; the entry appended through the config editor. The one function `apogee mcp create` and `POST /v1/admin/mcp-servers` both call.
- [x] **`mcp_servers:` in the config** — a map like `backends:` (`command`, `args`, `env`, `enabled`), `${ENV}` **and** `~` expanded (Ommi's docs promised the tilde and its loader did not), and three transforms: `append_mcp_server` (fields alphabetical after the name, `enabled` always written), `delete_mcp_server`, `set_mcp_server_enabled` (one line replaced in place, its comment kept). An `mcp/` layout row for scaffolds.
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
| The in-binary server's first user | **Apogee's own read-only toolsets** | Ommi's pattern had one user, its GitLab server; making Apogee itself the first means the pattern ships proven and the tests get a real subprocess with no interpreter dependency. Writing tools withheld: the gate lives in the loop. |
| `protocolVersion` | `2025-03-26`, accepting what the server answers *(default taken)* | The newer date carries `annotations`; every server in the wild accepts either. Recorded in `mcp list`. |
| Bounds | 20 s connect (covering handshake), 60 s call, 8-line tail, 1 MiB frame *(default taken)* | Ommi's numbers, each with its reason in the file. |
| `~` in `command`/`args`/`env` | Expanded, with `${ENV}` *(default taken)* | Ommi expanded only the latter while its documentation showed the former. |
| A tool with no `readOnlyHint` | **Gated** *(default taken)* | A third-party tool with no annotation is treated as destructive; `permissions.mcp__<server>__<tool>: allow` opts one out. |
| Scaffold languages | Python only, plus register-only *(default taken)* | The Go template assumed a toolchain nothing checked for. |
| `tools/list_changed` | Logged and ignored *(default taken)* | Tools are fetched once at connect; live refresh is new behaviour for a later item. |
| Where MCP tools land | The same `ToolRegistry`, registered last | One registry, not Ommi's fall-through pair; a namespaced tool whose server is down is an error result the model reads, never a fall-through. |
| The framer | `backends/jsonl_framer.h` allowed into `mcp/` by name | A second framer would be a second copy of the chunk-boundary bug class; the layering guard exempts that one header for `mcp/` only, with the reason in the file. |
| A failed connect on the terminal | A warning line that **stays**; progress repaints | Ommi repainted results too; a failure the user cannot read back once the prompt is up is a failure the user will report as "tools are missing". |

**Verified on the real binary.** `cli.mcp_lifecycle`: a scaffolded server whose own `test_server.py` passes, `mcp test` through the production client with `connecting:` before the dial, `mcp list` with the negotiated protocol, `__mcp-tools` registered as one more server answering a read-only tool and refusing a writing one, a `complete --tools` run connecting at startup and reporting a dying server once with its tail, and `disable`/`enable`/`delete-mcp-server` round-tripping through `config get`. The PTY check on top of that.

**Guardrails, each mutation-tested (33 mutations: 31 caught outright, 1 caught after its test was strengthened, 1 that exposed a redundant branch, since removed).** `done` not closed from the read loop's error path; the reader started after the handshake; tools not cached; cancellation and the call deadline ignored; notifications treated as responses; the initialized notification not sent; disabled servers dialled; no progress before the dial; the failure reason dropped; tools not namespaced; MCP tools never gated; the connected event not published; the stderr tail unbounded; the sink not teed; the last line lost at EOF; an unknown method silently ignored; writing tools served, or dispatchable over stdio; namespacing split on the last delimiter; a leading tilde not expanded; `enabled` accepting any word; `enabled` inserted instead of replaced; a double underscore allowed in a server name; `server.py` written without the execute bit; a missing command passing the doctor; env values listed by the admin view; a collision not a `409`; MCP tools not registered into the loop; the `PUT` row ungated; `mcp/` not a layout row; and `mcp/` including a provider.

**The survivor, and what it taught.** "`done` not closed from the read loop's error path" survived its first run because the dying fake *refused the write*, so the send path released the waiter and the read loop never had to. A real child that dies mid-handshake takes the `initialize` frame into its pipe buffer and only the read hits EOF — that is the path the fix exists for. The fake now takes the frame and hangs up, and the mutant costs the full twenty-second bound, which the test refuses. A fixture that fails earlier than the real thing tests the wrong path. The other survivor, an extra quoting check on flow-list items, was dead code: `yaml_scalar` already quotes every character a flow list could misread. The check was removed rather than tested.

## Milestone X — Agents as data

**Goal.** A named workflow the user runs with `apogee analyze --agent <name>`: a persona from prompt files, an output schema the answer must satisfy, a tool policy that is the agent's permission model, executed by the same loop every other surface runs — so an agent behaves identically on every backend the loop drives — with structured output validated on every provider, an in-process renderer, deterministic branch review from flags, `apogee agents` and its admin twins over one scaffold core, and three review agents re-authored for the owned loop.

### 2026-09-13 — `analyze-agents`: per-agent tool policy, structured output everywhere, and the bundled reviewers

**The shape it inherited.** Milestones V and W had put the native toolsets and the MCP client into one registry with every destructive tool declared into the gate, so a read-only policy could be a *filter over registered tools* rather than a plea to the model. The loop's Reporter seam, the profile filters that strip reasoning from every backend's answer, `auto_rag` and the one retriever resolver, the config editor, the layout declaration, the scaffold pattern and the admin plane all existed. What Ommi could only do by forwarding `permission_mode` and `--mcp-config` to the `claude` CLI, Apogee now does in its own loop for every backend.

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
| Local models | Prompt-level JSON, not a grammar *(default taken)* | The pinned llama.cpp subtree carries no schema-to-grammar converter; the validator and the retry cover it, and a grammar mode is a later upgrade behind the same request field. |
| Anthropic | The structured-outputs field where the model id says so, else one forced tool *(default taken)* | The forced-tool pattern is universal on the Messages API; the version parse reads both `claude-<family>-<major>-<minor>` and the older `claude-<major>-<minor>-<family>`, and an id it cannot read lands on the universal path rather than on a 400. |
| Saved filenames | `<base>-YYYYMMDD-HHMMSS.<ext>` *(default taken)* | Ommi's format put a colon in every filename; Windows is a target. |
| `analyses/` | A layout row *(default taken)* | Declared, seeded and doctor-checked like every other; Ommi's lazy `reviews/` was the one directory outside its parity check. |
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

## Milestone Y — The knowledge layer

**Goal.** Capture the *why* behind a decision at the moment the idea is formed — before it evaporates into a ticket, a design file, or a commit with the rationale stripped out — as one canonical record in an ordinary collection: findable with no model at all, archived rich and indexed thin, produced identically by the command line, a live chat, a saved session and the control plane, and edited without ever re-embedding.

### 2026-09-13 — `knowledge-records`: the canonical record, the capture clerk as one structured-output call, and `/capture` from chat

**The shape it inherited.** The retrieval matrix (Milestone S) had put a chunk store with a lexical floor, vectors with a per-collection binding, and the one ingest resolver under the user's spend rule in place; Milestone X had made structured output a property of every provider — the schema on the request, validation client-side always, one corrective retry — and the config editor, the layout declaration and the admin plane all existed. Ommi built its knowledge layer on the same seams in two days; Apogee's port took the day, and the clerk is the one piece that came out better than the original: Ommi validated its clerk's answer only on its `claude` backend, with a forced second pass, and parsed prose everywhere else. Here the clerk is `agentloop::run_structured` — one provider-native call, validated on every backend, a second miss a failed capture rather than a stored guess.

**What was built**

- [x] **The canonical record** (`knowledge/record`) — Ommi's full schema: `intent` (the load-bearing field, in the participants' words), `decision`, `status ∈ shipped|rejected|superseded` (the branch marker — a brainstorm is mostly roads not taken), `discipline`, `downstream_link` (the write-time link), `provenance{source, attribution}` (two fields, so names can be stripped without severing the chain), `raw_ref`, `timestamp`, `supersedes`; `normalize` (trim, synonyms onto the canonical statuses, `manual` as the default source), `validate` (an intent required, a canonical status), `index_text` (intent, then `Decision: …` — never attribution, never the mutable link or status, so an edit can never make a stored vector stale), `anonymize` (names off, chain on), ids `kr-<UTC second>-<6 hex>` that sort by time, and `record_from_metadata`, the ONE decoder of a chunk's record: strict, keyed by its own id, an intent present.
- [x] **Schema v3 on the chunk store** — a nullable `chunks.metadata TEXT` column added on the one-transaction migration path; `replace_source` with per-chunk metadata, `update_metadata` (the metadata alone: the text, the vector and the FTS index stay), `chunks_with_metadata`, `chunk_by_id`, `chunk_vector` (so a test can hold an edit to "the bytes did not move"), and every search hit carrying its chunk's metadata. Never indexed: the FTS triggers do not know the column exists.
- [x] **The capture clerk** (`knowledge/clerk`) — Ommi's prompt and schema ported verbatim and **compiled in** (byte-identical to `assets/clerks/`, a test enforcing it; not seeded — a fixed system concern, not a user-editable agent, and so no install-parity surface), the OUTPUT FORMAT block worded by the same `schema_instruction` every agent's persona ends with; `run_capture(ClerkFn, raw, overrides)` — the clerk arrives as a function, so every test of the capture logic runs model-free — applying each override to its field and only its field, then normalising and validating; `make_structured_clerk` binding a harness and a model to `run_structured` at temperature 0.2, a 2048-token budget, no tools, a throwaway history, and **a side request** — `agentloop::Options::side_request`, new, so a local backend runs the clerk on its own context and a live chat's cache is untouched (Ommi's SideRequest lesson, reached through the loop).
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
| The clerk | **One `run_structured` call, validated on every backend** *(consumed decision)* | Ommi validated only on `claude`; a record is not a report, so a second miss is a failed capture rather than a stored `conforms: false`. |
| Where the full record lives | **A nullable `metadata` column, schema v3** *(default taken)* | Archive rich / surface thin needs one place the whole record sits beside its thin index; a sidecar file would be a second store to reconcile. |
| The raw archive | **A layout row** (`knowledge/`, private, user data) *(default taken)* | Declared, seeded, doctor-checked like every other; Ommi's lazy `~/.ommi/knowledge/raw/` sat outside its parity check. Never seeded with content, never exported. |
| The clerk's backend on the CLI | **The extraction role** (`-m` > `models.default_extraction` > `models.default`) | A capture is structured extraction, so a cheaper extractor configured for that role runs it; chat's `/capture` uses the model already loaded. |
| The clerk's prompt and schema | **Compiled in, not seeded** | A fixed system concern, not a user-editable agent, and so no install-parity surface (Ommi's rule, kept); byte-matched to the shipped files by a test. |
| Vendor-CLI backends as clerks | **Refused by type**, before anything is spawned | The same rule `analyze` and `serve` apply; the claude CLI's `--json-schema` path is a later wiring behind the same request field. |
| A resolver refusal | **Before the clerk**, in a real run; a warning in a dry run | An explicit vector ask with no embedder is knowable up front; spending a model call first would be waste, and the dry run exists to preview. |
| The default collection | `knowledge`, `knowledge.db` overriding, `--db` per run *(default taken)* | Ommi's; separate collections per team or discipline stay one flag away. |
| Clerk temperature and budget | 0.2 and 2048 tokens *(default taken)* | Extraction, not creativity; an untuned local model that misses its stop token would otherwise decode toward its context limit. |
| `--from-chat` and `/capture` | User and assistant turns only *(default taken)* | Tool results and thinking are neither the participants' words nor the reasoning. |
| A clerk that fails twice | Nothing stored, exit 2 with the validator's message *(default taken)* | A record with no intent is worse than no record. |
| `auto_capture` and Ctrl-C | Fires on `/exit`, `/quit` and end of input; not on an interrupt | A user who hit Ctrl-C did not ask for a model call; replxx sets `EAGAIN` on an aborted line, which is what `interrupted()` reads. |
| A missing `--supersedes` target | The record is stored; the miss is a note | The lineage the user stated is worth keeping even when the old id was mistyped; losing the capture over it would be the wrong direction to fail. |

**Verified on the real binary.** `cli.knowledge_lifecycle`: `check --fix` seeding the `knowledge/` row; `--dry-run` printing the draft and the store decision with the collection absent, the archive absent and the config byte-identical afterwards, and `kn` as the alias; a capture stored with `Captured kr-…`, registered under `embeddings:`, its raw conversation archived under a `0700` directory, findable by `embed query knowledge` by its reasoning and **not** by the attribution; every override winning over the clerk and `--supersedes` recorded, a missing target a note; the doctor counting three records with the index ok and three private conversations; a `claude-cli` backend refused naming the type; a clerk that never conforms refused with nothing stored; `/capture rejected` in a piped chat storing a fourth record; and `capture --from-chat` over that session marked `chat` with the archived transcript opening `User: …`.

**Guardrails, each mutation-tested (52 mutations: 50 caught outright, 2 caught after their tests were strengthened; one more was equivalent and removed by simplifying the code).** The status not normalised; the default source dropped; an empty intent accepted; attribution in the index text; `anonymize` keeping the name, or the JSON always writing it; the decoder ignoring an id mismatch; each override ignored; a non-conforming clerk answer accepted; a clerk-given id kept; the clerk's temperature dropped, or the call not a side request, or the OUTPUT FORMAT block missing; the archive never written, or not private; the listing oldest first; `remove` keeping the file; an edit re-putting the chunk instead of rewriting the metadata; `supersede` setting the wrong status; metadata never stored, or an update touching the vector, or the listing returning every chunk, or the migration skipped, or a hit dropping it; a dry run storing; a vendor CLI accepted; `--from-chat` not marked `chat`; a status override not normalised; a backend failure exiting 1; the registration skipped; the supersede not applied; the vector never embedded, or its model not recorded; the default collection misnamed; the refusal after the clerk; the raw not archived by the core; a given id overwritten; the `501` skipped; `raw` not required; a clerk failure a `400`; the finished-record route skipping validation; the transcript including the system prompt; `knowledge.db` and `auto_capture` not parsed; auto-capture never firing; `/capture`'s argument never a status; chat's capture not marked `chat`; the doctor not counting, or ignoring a world-readable archive; the side-request flag not on the request; the layout row missing.

**The two survivors, and what each taught.** Accepting a non-conforming clerk answer survived because every non-conforming fixture also failed `validate` — prose, or JSON with no intent — so the schema check and the record check agreed on every case the suite had; the new case is valid JSON with an intent, a canonical status, and one key the closed schema forbids, which only the schema refuses. Keeping a clerk-volunteered id survived because a conforming answer can never carry one (the schema is closed), so the clearing is defensive; `draft_record` is now tested directly with an id, a timestamp and a `raw_ref` that must not survive it. A third mutant — the command normalising `--status` before handing it to the core — was **equivalent**: `draft_record` normalises every override anyway, so the second normalisation was a second path, and the fix was to delete it rather than to test it.

### 2026-09-19 — `knowledge-query-lifecycle`: records back out, kept true, shared, and the stateless review flow

**The shape it inherited.** The first slice had put the record, the clerk and the store in place, with every capture path going through one core. What was missing was the other direction — getting records back out through the same retriever rules every other surface obeys — and the two things a record needs over its life: edits that never touch its vector, and a way to share it with the names off and the chain on. And the flow a GUI actually needs between the clerk and the store, which Ommi had settled as a stateless three-step contract (its OMMI-10 record: its own route, 2000 characters, the store step the existing route). Ported here as specified, with the clerk's revision pass on the same `run_structured` seam as capture.

**What was built**

- [x] **`knowledge/query`** — `query(store, harness, config, collection, options)`: the collection's pins, the embedder through the capability probe, and **the one resolver** (`resolve_turn_retriever`) deciding lexical, vector or hybrid from the same facts `embed query` and every chat turn read — an explicit vector ask with nothing to run it a hard error naming lexical, hybrid without a vector half run and *reported* lexical, a vector pin that cannot run an exclusion with a note, a model mismatch a demotion with the re-ingest hint. **Rank everything, then filter, then cut**: the whole collection is ranked (the lexical search learned that a limit of 0 means every match), `filter_hits` drops off-branch and off-discipline records and chunks that are not records at all, and only then is the list cut — so six rejected records that match hardest never starve the one shipped record on the default branch. **Defaults to shipped.** The judge sees the same immutable index text the retrievers matched, and `reranked` is set from the same place as the ordering. `ScoredRecord` carries the chunk id as a storage handle off the wire, the seed the graph walk uses next.
- [x] **`knowledge/refine`** — Ommi's refine prompt ported verbatim and compiled in (byte-matched to `assets/clerks/refine_prompt.txt`), the OUTPUT FORMAT block and the capture schema shared through the one `with_output_format`, so both passes ask for one shape and a refined draft is storable through exactly the path a captured one is; `refine_user_message` rendering `CURRENT DRAFT` (the six clerk-owned fields and nothing else — `id`, `raw_ref`, `timestamp` and `supersedes` never reach the model), `REVIEWER INSTRUCTION`, and `RAW CONVERSATION` or an explicit "not supplied — do not invent" line; `validate_refine_instruction` (non-empty, at most 2000 **codepoints**) and `run_refine`, whose guards fire before any clerk call, which carries `supersedes` through untouched and keeps a `provenance.source` the clerk dropped rather than letting `normalize` default it, and whose non-conforming revision is a failure, never a partial.
- [x] **`knowledge/export`** — `export_records(records, strip_names)`: the attribution stripped AND the machine-local `raw_ref` cleared (it leaks the local username and is useless to a recipient) while `source`, `downstream_link` and `supersedes` stay; `render_markdown`: a report grouped shipped, rejected, superseded, then other, a heading per record and a bullet per set field, the attribution line simply omitted when empty. Neither ever carries a raw conversation.
- [x] **The store's lifecycle** — `Store::reindex(embed, ids)`: every record or exactly the named ones, an unknown id refused before any embedding is spent, an embedder failure stopping the run naming the record; a put with an empty raw rewrites the text, the vector and the metadata and leaves the archive and `raw_ref` alone. `vectorless_count` for the mixed-collection disclosure.
- [x] **`apogee knowledge query|list|info|link|status|delete|export|reindex`** — `query [--status shipped|rejected|superseded|""] [--discipline] [-n] [--retriever] [--rerank] [--json]` printing `Top N result(s) … [lexical]` with every score on its retriever's scale, and telling a user who found nothing on the default branch how to search every branch; `list` newest first; `info --raw` printing the archive; `link` and `status` (synonyms folded) as metadata edits whose vector bytes a test reads back unchanged; `delete` taking the archive; `export [--format json|markdown] [--anonymize] [--status] [--discipline] [--out]`; `reindex [ID] [-m]` honest about a collection with no records or no vectors before any backend is built, warning on a mixed one, and recording the space the vectors now live in. A read never creates a collection out of a typo, and a config that will not load costs a read nothing but a warning.
- [x] **The control plane** — `GET /v1/admin/knowledge` (a missing collection an empty list, never an error and never a created file; `?q=` through the one resolver with `{object, data: [{record, score}], retriever, reranked[, note]}`, `501` for `?retriever=vector` with no embedding backend naming `?retriever=lexical`; `?anonymize=true` on both modes; no default branch over HTTP — a listing route shows what a client asks for), `GET`/`PATCH`/`DELETE /v1/admin/knowledge/{id}` (`PATCH` exactly one of `link` or `status`), `POST /v1/admin/knowledge/reindex` (`200 {reindexed: 0, note}` for a lexical or empty collection, `501` for vectors with no embedder), **`draft: true` on capture** (`200 {draft, record, db, retriever[, warning]}`, an id-less, timestamp-less record with no `raw_ref`, zero footprint), and **`POST /v1/admin/knowledge/refine`** (`{record, instruction, raw?, model?}` → `200 {draft: true, record}`, `400` for a missing record or a bad instruction *before* the clerk, `501`, `502`). The literal knowledge paths are matched before the `{id}` rows, so `capture`, `refine` and `reindex` are never read as record ids.
- [x] **The round trip, proven** — draft → refine (a no-op instruction) → the ordinary finished-record `POST` lands a record field-equivalent to a one-shot capture of the same conversation (chunk text and archive included), with the clerk run exactly twice and the footprint — the collection, the archive, the config's bytes — unchanged after the draft, unchanged after the refine, and changed once after the store.
- [x] **Parity**: `knowledge link`/`status`/`delete`/`reindex` are twins in the table; `query`/`list`/`info`/`export` read-only; refine has no CLI form by design (Ommi's documented skip, kept). Every route in [http-api.md](../reference/http-api.md), pinned by `cli.http_api_conformance`.
- [x] **23 new test cases** (1181 in all), green in both builds; `cli.knowledge_lifecycle` extended on the real binary.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| The refine loop | **Stateless, HTTP-only** *(Ommi's recorded decision, kept)* | The draft is the client's; the store step is the existing finished-record route, so the round-trip equivalence is a test rather than a hope; a CLI user iterates by re-running `--dry-run`. |
| `query`'s default branch | **`--status shipped`** *(Ommi's, kept)* | The branch marker is the single most important guard in a brainstorm-based corpus. Over HTTP a listing route shows what a client asks for, and says so. |
| Filtering | **Rank everything, then filter, then cut** | A filter after the cut starves the result the moment the best matches sit on another branch; the lexical search gained an unbounded form (limit 0) to make that possible. |
| A metadata edit | **Never a re-embed** — the vector's bytes asserted unchanged across `link`, `status` and `PATCH` | The index is built only from the immutable reasoning, and only `reindex` rewrites vectors, only when asked. |
| `--top-k` | 5 on the CLI; 20 over HTTP, `?limit=` to change it *(default taken)* | Ommi's terminal default; the HTTP number is a GUI page size and the judge's widened pool. |
| Markdown export | Grouped shipped, rejected, superseded, then other *(default taken)* | Ommi's `RenderMarkdown`. |
| `PATCH` | Exactly one of `link` or `status` per call *(default taken)* | One edit, one intent, one audit line. |
| The instruction cap | 2000 **codepoints**, checked before any clerk call | A reviewer's note in any script gets the same allowance; the source material belongs in `raw`. |
| A dropped `source` on refine | The draft's value, not the default | `normalize` would otherwise rewrite a field the instruction never mentioned. |
| A read on a missing collection | The CLI refuses naming `capture`; HTTP lists nothing | Neither creates an empty database out of a typo; a GUI polling an empty layer must not see an error. |

**Verified on the real binary.** `cli.knowledge_lifecycle`, extended: `query` returning the shipped record on the default branch and not the rejected one, the rejected one on `--status rejected`, every branch under `--status ""` with the retriever named in the JSON envelope, an explicit vector ask refused naming lexical; `list` counting five; `info --raw` printing the archived session; `link` and `status` landing in `info --json`; an anonymized export with no name and no archive path but the chain kept, and a Markdown report with its status groups; `reindex` honest about a lexical collection; `delete` taking the archive with it and the doctor counting one fewer.

**Guardrails, each mutation-tested.** 45 mutations: 41 caught outright, 3 caught after their tests were strengthened (a schema miss whose JSON still reads as a record must fail the refine; the reindex binding is the reindex's own, proven on a store that had lost it; the refine guards answer `400` even on a server with no backend to run the clerk); one more was equivalent — the query's post-judge cut duplicated the filter's own, since without a judge the fetch limit IS `top_k`, and the dead branch is gone. The mutations: The status or discipline filter ignored; non-record chunks kept; the search cut before the filter; a resolver refusal swallowed; an excluded collection searched anyway; `reranked` claimed regardless, or the judge's order dropped; the retriever always reported lexical; the top-k cut skipped; the instruction guard after the clerk; `supersedes` not carried, or reaching the model; a dropped source defaulted; the cap counting bytes; the absent-raw line dropped; a non-conforming revision accepted; the refine prompt not the refine prompt; anonymize keeping the archive path or the name; Markdown groups unordered, or empty fields written; reindex re-archiving, embedding before refusing an unknown id, or miscounting the vectorless; a lexical limit of 0 returning nothing; the CLI's default branch every branch; a status edit not normalised; export never anonymizing; reindex skipping the binding, running on a lexical collection, or skipping the mixed note; delete keeping the record; a read creating the collection; `info --raw` never printing; the list route creating a collection; the vector `501` skipped; `PATCH` accepting both fields; a draft storing; the refine guards after resolution; a `502` as a `400`; the reindex `501` skipped; anonymize ignored on the list; `?db` ignored on a get; the `PATCH` route missing.

### 2026-09-19 — `knowledge-graph-build`: the graph over a collection, and what a retrieval turn cannot reach by resemblance

**The shape it inherited.** Two slices had put records in and got them back out through the one resolver. What retrieval still could not do is the thing Ommi built its graph for: surface **connected context** — what a question is *about*, walked by exact SQL over extracted edges, so a document that shares no vocabulary with the query still reaches the request when the graph says it is related. Ommi's Milestone T carried three lessons worth more than its code: the staleness fingerprint that had to grow a second half after a same-count re-ingest silently lost every decision node; expansion seeded *before* the judge so a verdict that keeps nothing cannot drop the graph; and a generation budget found in the field when an untuned local model missed its stop token on one chunk. All three are load-bearing here.

**What was built**

- [x] **The `kg_*` layer in `embedstore/` (schema v4)** — `graph.h/.cpp`: the tables and the entity FTS index created on open inside the one migration transaction, a dropped trigger self-healing; node identity by (normalised name, type), the first casing kept, descriptions merged first-non-empty, **a description change clearing the stored vector** so a stale embedding never outlives its text; `upsert_edge` corroborating (weight +1 per re-statement) against `ensure_edge`, the fact that stays at weight 1; mention dedup with the salience counter; the per-source fingerprint `(chunk_count, max_chunk_id, model)`; `reconcile_graph` pruning dead mentions and vanished sources, recomputing every count, dropping orphans and their edges in one transaction; `delete_graph`; `graph_meta`. `graph_search.h/.cpp`: stats, exact and full-text lookup through the same match-query guard as the chunks, neighbourhoods, supporting chunks, and **`graph_expand`** — seeds from the retrieved chunks' mentions ∪ hop-0 entity hits, hops clamped to 1..2 as plain per-hop queries, neighbours ranked by connecting weight × mention count, capped, one support chunk each, every relation among the traversed neighbourhood; chunk seeds never re-listed (their text is already injected), hop-0 seeds listed (nothing else carries their descriptions). The three translation units share `store_impl.h`, package-private, so the raw handle still never leaves the package. `kg_mentions.collection` and `kg_state.collection` carry the provenance column the named-graph item keys on; a collection's own graph writes `''`.
- [x] **Chunk ids are never reused.** Running the record lifecycle against the build found it: the chunk table's `INTEGER PRIMARY KEY` handed a deleted maximum straight back to the next insert, so a same-count re-ingest of a record's chunk produced *the same id* — and the fingerprint's second half, the whole reason Ommi added it, would have read the source as up to date. The table is `AUTOINCREMENT` since v4, an older store rebuilt in place with rowids preserved (so the external-content FTS index stays valid) and the sequence picking up past the highest id ever stored; the hand-built v2 fixture migrates through it.
- [x] **`graph/extract`** — Ommi's prompt and schema ported verbatim and compiled in (byte-matched, not seeded); the closed set of eight types with **`decision` never among them**; `normalize` enforcing in host code what the schema can only request — types, the 12/16 caps, empty names, duplicates merged, self-loops, endpoints resolving only to surviving entities, duplicate relations — and `make_structured_extractor`: **one `run_structured` call** at temperature 0.2 under the 2048-token cap, no tools, a throwaway history, marked a side request, the one OUTPUT FORMAT wording every structured caller ends with; a non-conforming answer after the one correction a failed outcome, never a partial.
- [x] **`graph/build`** — reconcile → **materialise records** → plan by fingerprint → extract stale sources chunk by chunk with one retry → a state row after a complete file only → embed mutated nodes last. A limit or a cancellation stops between chunks and leaves the file unstamped; a failed chunk is counted, its file left for the next build; a failing embedder stops the phase and keeps the build, recording no model. Every knowledge record in the collection — recognised per chunk through the one decoder — becomes a **`decision` node**: name = record id, description = decision — intent clipped to 400 codepoints, status and discipline as metadata **replaced on every run** (so an in-place status edit, which churns no chunk, still shows), mentioned by the record's own chunk so the ordinary reconcile retires it with the record; **`concerns`** edges from the decision to every entity the extractor pulls from its own text — the link that lets a documents chunk reach the *decision* in one hop — and **`supersedes`** mirroring the lineage when the target is in the graph, a missing target counted and never a placeholder. A decision node embeds by description alone: the id carries no meaning.
- [x] **Retrieval-time expansion on every surface** — `agentloop/graph_context`: `[Knowledge graph: <name>]`, entity lines (`kr-… (decision, shipped): …` for a record, so a superseded rationale is never mistaken for the live one), then `A —[relation]→ B` triples, under a **1,500-codepoint budget with whole-line truncation** that never leaves a triple without its entity. `retrieve_for_turn` seeds it from the retrieval-ordered top-k **before the judge** and, on a lexical or hybrid turn, from the query's own terms through the entity index — so an entity-name hit expands with no embedder at all — then appends it after the chunk list in the same transient message; a judge that drops every chunk leaves the section framed on its own; a failing walk is a note, never the reason a turn loses its chunks. Every surface injects whenever there is a prefix, and reports `+N graph entities` — the status line, `analyze`'s note, and the `rag_result` meta-frame's `graph_entities`.
- [x] **`apogee graph build|stats|show|delete`** — the extractor `-m` > the collection's `graph.extract_backend` > the extraction role > the default, through the one role resolver; **a metered default refused** naming the three ways, a vendor CLI refused by type, an unconfigured name refused; entity vectors under the embedding spend rule (the collection's embedder when unmetered or pinned to vector, else full-text only and said so); `--dry-run` printing every extraction with zero footprint; failed chunks printed as they happen; the first success registering an unregistered collection and writing **`graph.enabled: true`** through the config editor, the same bytes the admin twin writes; a summary with `Records as nodes: N decision node(s), M supersedes edge(s)`. `show` resolves exact then closest, groups relations by verb with `x<weight>` corroboration, and points a decision node at `knowledge info`.
- [x] **The control plane** — `POST /v1/admin/graph/{id}/build` as the plane's **first async job** (`202 {job_id}`, progress as `admin.job.*`, the counts on the record, cancel between chunks) on new `JobWorkers` whose destruction cancels every token and then joins, so no worker outlives the plane; the CLI's chain and refusals with the served set on top (`400` for a metered default, a vendor CLI, an unserved name; `404` no data; `501` no generation backend); `GET …/stats` zeros for an unbuilt graph, never an error; `GET …/entity?name=` exact then fuzzy with `also_matched`; `DELETE …`; `PUT /v1/admin/embeddings/{id}/graph` as the twin of the auto-enable write; and **`?graph=true`** on `GET /v1/admin/knowledge`, the twin of **`knowledge query --graph`**, both through one `graph_for_records` core — the note only when no graph covers the collection, so "no graph" and "nothing related" stay distinguishable.
- [x] **The fact behind the cost policy** — `LLMProvider::generation_is_metered()`, default **true** (unknown is metered), the API-billing providers saying yes explicitly, the mock and llama.cpp no; `Harness::generation_is_metered(model)` through the probe, true for an unroutable name. The config's `graph:` block (`enabled`, `extract_backend`, `hops` validated 1..2 at load, `max_entities`) on `EmbeddingConfig`, a commented example in the template, `set_embedding_graph_enabled` scoped to `embeddings:` so a same-named backend is never touched, and the doctor's `Graph` section. `graph/` joins the layering guard with its own allow-list.
- [x] **61 new test cases** (1243 in all), green in both builds; `cli.graph_lifecycle` on the real binary.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| The extractor's temperature and cap | **0.2 and 2048 tokens** *(default taken)* | Ommi's numbers, the cap found live: an untuned local model that misses its stop token would decode toward its context limit on one chunk. |
| Entity vectors | **The collection's embedder chain under the embedding spend rule** *(default taken)* | Unmetered, or the collection pins `retriever: vector`; otherwise the graph is full-text searchable and complete. The same rule as ingest, decided once. |
| `graph show --chunks` | **3 by default** *(default taken)* | Ommi's. `--chunks 0` prints every one. |
| The build's scope | **A collection's own graph, single-store** | The `Member` generalisation and the named graph belong to the global item; the provenance columns are already in the schema so it needs no migration. |
| Chunk ids | **AUTOINCREMENT, an old table rebuilt in place** | The fingerprint's second half is only meaningful if an id can never come back; found by running the record lifecycle, not by reading. |
| Where the section rides | **After the chunk list, in the same transient message** | It augments the chunks and never crowds them out; with no chunks left it is framed on its own so a judge's empty verdict cannot drop it. |
| The HTTP build | **An async job on the plane's harness, with worker threads owned by the admin handler** | One generation call per chunk is minutes, not a request; the knowledge routes' precedent of running a clerk on the plane's harness is kept, and shutdown cancels before it joins. |
| Metered-ness | **A provider fact, default true** *(the item's recorded decision, kept)* | Never a type list; a new backend is assumed to cost until it says otherwise. |
| The `decision` type | **Storage-only, never in the schema's enum, dropped by `normalize`** *(Ommi's, kept)* | Prose that says "we decided X" can never forge a decision node. |

**Verified on the real binary.** `cli.graph_lifecycle`: a metered default refused before any provider call; `build --dry-run` printing every extraction with the config byte-identical afterwards and nothing stored; the build stamping both sources and writing `graph.enabled` with every byte above the entry untouched; `stats` and `show`; a second build a no-op; a `complete --rag` turn over two documents that share no vocabulary — the status line counting the graph entities while the answer carries the *other* document's entity and not its text; an entity-name query expanding with no lexical match; a captured record materialised as a `decision` node with its `concerns` edge, shown with its markers; `knowledge query --graph` and its JSON; a docs turn reaching the decision as `kr-… (decision, shipped): …`; `delete` clearing the graph and keeping the chunks.

**Guardrails, each mutation-tested.** 76 mutations: 63 caught outright, 11 caught after their tests were strengthened (a decision node's description change must clear its vector; a schema miss that still decodes must fail the extractor; a forced rebuild must keep the deterministic edges at weight 1; a walk the store cannot run must be a note with the chunks kept; a vector turn must seed from its chunks alone, on the RAG turn and on `query --graph` alike; the graph knobs must travel from the config into the turn; a metered backend named explicitly must be allowed, on the CLI and over HTTP, which took a scripted mock that can claim to be metered; entity vectors must obey the spend rule, which pulled that decision into one `resolve_entity_embedder` both surfaces call; the doctor must warn on an enabled graph that is not built; the `PUT` route must exist), 2 equivalent — the entity index's update trigger firing on every column changes what the index rewrites, never what it answers; and the `AUTOINCREMENT` on a fresh chunk table is masked by the self-healing migration, which a new mutant of its own holds — and one more removed as dead code: the expansion's hop-0 exclusion could never fire, because a seed is never a candidate. The mutations: a description overwritten, or its vector kept on a text change; corroboration missing, or a fact corroborating; a mention counted twice; reconcile keeping orphans or skipping the recount; a decision's metadata never refreshed; the id migration skipped; the expansion ignoring the cap, walking past two hops, or scoring without mentions; stats blind to the max id; the whitelist, the caps, self-loops, unresolved endpoints, duplicate relations, unmerged duplicates; the extractor not a side request, or without the format block; no reconcile; staleness blind to the max id or the model; a failed file stamped; no retry; a dry run storing; the limit or a cancellation ignored; an embed failure failing the build, or a partial embed claiming the model; a decision embedded by name; a `supersedes` placeholder; records not materialised; the clip by bytes; the budget ignored, no lexical seed, the decision marker dropped, triples after a cut; the graph skipped whenever a judge runs, or ignoring `enabled`, or dropped when the judge drops every chunk, or not reported; `describe_retrieval` silent; a metered default allowed on the CLI or over HTTP, a vendor CLI allowed, `enabled` never written, a dry run writing the config; the `query --graph` note swallowed; the build route skipping the served set, stats a `404`, the enabled write never landing, the `501` skipped, the entity route never fuzzy, `PUT` accepting a non-boolean, the graph flag ignored on the list route; hops not validated; the enabled edit writing the wrong section, or rewriting the line; the doctor ignoring an unconfigured extractor; an unroutable name unmetered, the mock metered.

### 2026-09-19 — `knowledge-graph-global`: the global layer, and a graph that spans collections

**The shape it inherited.** The third slice answered *local* questions: what a retrieved chunk is about, walked by exact SQL. Two things it could not do are the reason Ommi's Milestone T had a Phase C and a Phase D. "What are the main themes in this corpus?" needs *global* structure -- clusters, not neighbours -- and a per-collection graph cannot see across collection boundaries, so the same person, system or project mentioned in `docs`, `meetings` and `tickets` was three disconnected twins. Ommi's recorded scope answers were kept whole: precedence only, rebuild never absorb, CRUD in the config family, identity by exact member set, threshold 0.92. Its core insight carried too: every edge comes from one chunk's extraction, so cross-collection connectivity flows entirely through shared node identity, which is exactly what one build-time store makes real. And the provenance columns the third slice put in the schema for this item meant the storage generalised with no migration at all.

**What was built**

- [x] **Communities** -- `graph/communities`: **deterministic weighted label propagation** over the extracted relations (labels from node ids, ascending-id asynchronous updates, weighted-majority adoption with the smallest label winning ties, at most 20 rounds, edgeless nodes never joining, clusters of fewer than 3 dropped), no model in the detection; each new or changed cluster summarised by **one plain generation call** under Ommi's summariser prompt compiled in (byte-matched to `assets/clerks/community_prompt.txt`, no schema -- the output is prose); the summary stored as an **ordinary retrievable chunk** under `graph://community/<id>`, so a corpus-level answer surfaces through `embed query`, every RAG turn and every retriever with zero new query paths. **Identity is the exact member set**, stored verbatim as the sorted ids: an unchanged cluster costs nothing, a changed one is pruned and regenerated, `--force` regenerates all; a summariser failure is soft and leaves the old summary in place. Pseudo-chunks are graph output and never corpus input: excluded from extraction planning, staleness and coverage, and taken along by delete. The embed phase runs last over **every summary still without a vector**, not only this run's, so an earlier embed failure heals on the next run rather than needing a forced regeneration -- a small departure from Ommi, whose failed summaries stayed lexical-only for good.
- [x] **Dedupe** -- `embedstore/graph_dedupe`: union-find per type over pairwise cosine at a threshold (0.92); the earliest-extracted node survives, edges repoint to it (weights summed on a collision, would-be self-loops dropped), mentions union and recount, the description merges first-non-empty with the survivor's vector cleared on a text change, the merged nodes' community memberships removed (derived; the next communities run recomputes). Nodes without a vector are never considered, **decision nodes are never merged** -- two records with near-identical text are still two decisions -- it never runs on its own, `--dry-run` previews, and everything commits in one transaction.
- [x] **Named graphs** -- a `graphs:` entry (`collections`, `extract_backend`, `hops`, `max_entities`; **no `enabled` -- building is the enablement**), keyed by name like every other section and kept in file order because the precedence rule reads it first to last; `config add-graph`/`delete-graph` through `append_graph`/`delete_graph`, section-scoped end to end so a graph sharing an agent's name can never touch the agent. Its database is **derived data** at `<embeddings_dir>/graphs/<name>.db`, created by the first build and never by an installer -- not a layout row, unlike `knowledge/raw`, because it is rebuildable from its members with one command. `graph/build_multi` runs the one loop over `Member`s into the target: every mention and state row labelled with the member it came from, per-member state so a resume works across members, records in any member materialised, the graph's own name and as-built member set stamped into `graph_meta`; `reconcile_graph_multi` converges membership -- an ex-member's rows die, dead mentions are pruned per member through a cross-database `chunk_ids_existing`, a member whose database is gone reads as empty. **Rebuild, never absorb**: a member's own graph is neither consulted nor migrated, and never written to. A named graph embeds entities through the default chain and records its own model; members may use different chunk embedders freely.
- [x] **Retrieval precedence, decided once** -- `agentloop::resolve_turn_graph`: a **built** named graph covers its members (the first entry listing a collection wins a double-listing; an unbuilt one covers nothing), else the collection's own enabled block, else nothing; the member's block is left untouched and resumes the moment the collection leaves. Every surface -- the conversational turns through `retrieve_for_collection`, `knowledge query --graph`, the HTTP twins -- makes the same call. The turn opens the named graph's database for the walk alone, seeds it with the member's label and renders under the graph's name. Apogee retrieves one collection per turn, so a turn renders one section under the one budget; Ommi's several-sections case has no counterpart here.
- [x] **`apogee graph` resolves `<name>` graphs-first** (`check` keeps the two name spaces apart): `build` over a named graph's members with missing ones contributing nothing and a dry run planning against an in-memory store so its footprint is zero -- not even the file; `stats` per member with an as-built membership-drift note; `show` naming each supporting chunk's collection; `delete` removing the file. `communities [-m] [--force] [--min-size] [--list]` and `dedupe [--threshold] [--dry-run]`, against a collection or a named graph alike. The summariser resolves exactly as the extractor does, through the one chain with the metered fall-through refused. **`embed ingest --graph`** chains the covering graph's build after a successful ingest only -- the first `graphs:` entry listing the collection by config membership alone, since the first chained build is what creates the database, else the collection's own -- from a fresh config so it sees the entry the registration just wrote.
- [x] **The control plane** -- every `/v1/admin/graph/{id}/*` route named-aware; `POST …/communities` as the plane's second job kind (`graph-communities`), `GET …/communities`, `POST …/dedupe` synchronous; and the `graphs:` CRUD slice at `/v1/admin/graphs` with `built` on every response, the CLI's rules through one `validate_named_graph`, `409` on a POST collision -- an entry made over HTTP byte-identical to one made by the CLI. Forty-eight admin rows.
- [x] **The doctor** validates every `graphs:` entry: the collision ban and a missing member as failures, an unconfigured extractor, built or not with the build as the remedy. Schema v5 adds the two community tables on the same open-time path.
- [x] **52 new test cases** (1295 in all), green in both builds; `cli.graph_lifecycle` extended on the real binary.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| `--min-size`, the relation cap, the round bound | **3, 20 lines, 20 rounds** *(default taken)* | Ommi's numbers; label propagation converged in a handful of rounds on real graphs. |
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

**Goal.** Fine-tune local models on their full-weight SafeTensors files, end to end, on the orchestration shape Ommi proved -- a C++ orchestrator over Python trainer subprocesses -- with the execution that does not port kept behind one owned boundary. Three items, in build order: the floor (the Python boundary, datasets, kits), the run (train / eval / promote / rollback), and the orchestration layer (pipelines, regimes, the continuous cycle). Scheduled for v0.1.0 on 2026-09-19, the user's call; the same day the two tool kits were deferred to the in-text tool protocol as its own tools-track item.

### 2026-09-19 — `training-datasets`: the Python boundary, datasets, and kits

**What was built**

- [x] **The `training` layout row** (private, user data) -- `datasets/`, `datasets/raw/`, `kits/`, `scripts/`, `venv/` beneath it, created by seeding or first use, never restated as rows -- with accessors in `harness/layout`; the doctor, `check --fix`, both installers and the uninstall plan picked it up from the one declaration.
- [x] **Compiled-in training assets**, like the bundled agents: four kits (`instruction-following`, `structured-output`, `summarization`, `reasoning` -- Ommi's byte for byte) and `prepare_dataset.py`, in `harness/assets_training.cpp`, byte-matched to `assets/training/` by a test and **seeded skip-if-present** under `training/kits/` and `training/scripts/` by the one seeding path. `bundled_files()` is now the single list the seeder, the unmodified check and the doctor's drift row read; `is_unmodified_bundled_asset` answers for a directory whose every file is Apogee's, so a fresh install's `training/` does not read as user data at uninstall.
- [x] **The Python environment Apogee owns** (`training/python_env`): `training/venv/`, seeded from `training.python` or `python3` on PATH with `-m venv`, requirement sets (`prepare`, `mlx`, `peft`, `convert`) installed with its own pip and recorded in `apogee.json`, versions as floors. `apogee train setup [--trainer auto|mlx|peft] [--with SET]` creates it explicitly; a command that finds it missing asks on a terminal and on a pipe refuses naming the command. **Seeding never creates it** -- a fresh install downloads nothing unasked. The doctor's `Training` section: the environment and its sets, each seeded script against the shipped copy (an edit kept and shown, a missing one repaired by `--fix`), every installed kit, `paths.hf_dir`.
- [x] **The script runner** (`training/script_runner`): a driver under the environment's interpreter, stdout framed by the vendor-CLI framer (a named layering allowance, as `mcp/` has), every line classified -- `{"message"}`, `{"error"}` as its own event, a terminal record, and a non-JSON line kept as a message -- the exit code carried, stderr a bounded tail never inherited, cancellation terminating the child. Three of Ommi's silent gaps closed by construction: its reader dropped a line that did not parse, its progress struct had no `error` field so an error decoded as a blank tick, and it discarded the child's exit status so a crashed trainer read as success.
- [x] **`prepare_dataset.py`**, Ommi's presets (`alpaca`, `sharegpt`, `chatml`, `oasst`, `prompt-completion`) with auto-detection, `--map`, `--split`, `--as-eval`, `--flat` and the no-match refusal listing the columns -- **JSON, JSONL and CSV through the standard library**, so the common path needs nothing installed and its test runs on bare `python3`; the `datasets` library imported for Parquet only; the environment guards before the first ML import, grep-tested.
- [x] **Kits** (`training/kit`): Ommi's YAML shape -- `synth{system, seeds, count 200, per_seed 8, temperature 0.9}`, `train{iters, batch_size, num_layers}`, `eval[{prompt, expected}]` -- validated (a non-blank prompt, at least one eval item), listed sorted with a broken file named rather than hidden, found by name or path, the inline suite materialised one object per line.
- [x] **The synth core** (`training/synth`), model-free: Ommi's contract prompt appended to the kit's, batches of `per_seed` with the seeds cycled, the array extracted through prose and fences, the field aliases, case-insensitive prompt de-duplication, the call bound `(target/per_call + 1) * 3 + seeds`; **and the two upgrades the placeholder promised** -- batches in flight in parallel, and a failed batch retried with exponential backoff (five times, capped at a minute, on top of the transport's own `Retry-After` handling) before it is skipped. A cancelled run hands back nothing it produced past the stop.
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
| Explicit session mining | **No consent key** *(default taken)* | Ommi's asymmetry, kept on purpose: the gate guards the loop nobody is watching. |
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
- [x] **The drivers**, one argv shape for both (`--mode train|fuse|infer` with the same flags): `train_mlx.py` over `mlx_lm.lora`/`mlx_lm.fuse` and the `mlx_lm` API for inference; `train_peft.py` over transformers' `Trainer` + peft (+ bitsandbytes for QLoRA), the Apple-Silicon hard-exit before any other import and the environment guards before the first ML import, both grep-tested. Reading the reference's drivers against today's libraries found three things that could not have run: `mlx_lm.lora --data` wants a *directory* of `{train,valid}.jsonl` (the reference passed a file), the MLX driver's argparse rejected the `--mask-prompt` its own Go side passed, and trl removed `DataCollatorForCompletionOnlyLM`, which the PEFT driver imported. Apogee's lay out the data directory (every example trains; validation is a copy of the first tenth, so a small dataset loses nothing), forward the flag, and mask the prompt **exactly** -- the token count of the chat template with the generation prompt appended -- with no trl at all.
- [x] **`ScriptTrainer`** (`training/script_trainer`) drives either script through the script runner with `PYTHONDONTWRITEBYTECODE` set, so a run leaves no `__pycache__` in the seeded tree and the doctor's drift row stays honest; `mlx_trainer` and `peft_trainer` are the two specs. **The mock trainer** (`training/mock_trainer`) ships in the binary as `--trainer mock` -- scripted iterations with a falling loss, a token adapter, a copying fuse, an echoing candidate with the base distinguishable, and at promote a minimal GGUF the header reader parses -- so the whole chain runs on the real binary with no Python: the lifecycle test does exactly that. It is scripted by the dataset's first line (`{"mock": {"error", "fuse_error", "iters"}}`), the way the mock backend is scripted by its file, which is what lets a failed run and a failed promote be driven through the real command line; the mutation run found both paths untested until it was.
- [x] **`apogee train run <student> --dataset <name|path> [--method] [--iters] [--batch-size] [--num-layers] [--grad-checkpoint] [--mask-prompt] [--trainer]`**: the student a snapshot directory, or a name under `paths.hf_dir` or `models/`; a GGUF or a backend entry refused naming `apogee models pull <owner>/<repo> --safetensors` -- **only full-precision weights are trainable**. The trainer: the flag > the config > `auto` (`mlx` on Apple Silicon, `peft` with `nvidia-smi`, else a refusal naming both). The progress protocol becomes the status line (`iter n/N · loss · lr · it/s`; a line every tenth on a pipe); Ctrl-C terminates the child through a handler that only flips the token and records `cancelled`. **The manifest** (`training/manifest`: `training/runs/<YYYYMMDD-HHMMSS>/manifest.json`, suffixed on collision) is written `running` at the start and rewritten at the end -- `complete`, `failed` with the error, or `cancelled` -- with the trainer, the student, the dataset and its sha256, the hyperparameters, the final loss and the timestamps.
- [x] **`apogee train eval <run> [--suite <path|name>] [--judge] [--force]`** (`training/eval`): a suite is `{prompt, expected?}` JSONL -- a path, `training/suites/<name>.jsonl`, a prepared `<name>.eval.jsonl`, or a kit's inline items by kit name. An item with `expected` is a substring check; one without is a **pairwise judge** comparison of the candidate against **its own untuned base** through the same runner (the reference compared against `models.default`, an unrelated model), the verdict's first word `A`/`B`/`TIE` with anything else a tie (the reference read the first byte, so "Both are fine" counted as B), a judge or baseline failure a tie under the **never-fail contract**; without a judge such items skip and auto-pass, loudly, with the count. The judge is named (`--judge` or `training.judge_backend`), never a vendor CLI, and gets a 1024-token verdict budget, not the reference's 10: a reasoning judge spends its budget thinking, and under never-fail a verdict that never arrives is a tie that passes. **The gate is 100%**; the results land in the manifest; re-run only with `--force`.
- [x] **`apogee train promote <run> --as <backend> [--force] [--quantize TYPE] [--keep-fused]`** (`training/promote`), the only path from a run to inference, with every refusal before the expensive part: the eval gate (hard by default; `gate_mode: soft` a warning; `--force` skips), a non-llamacpp `--as`, an unknown or -- in a build without llama.cpp -- unsupported `--quantize`. Then fuse → **convert** → verify → (quantize → verify) → register → ledger. The converter is **llama.cpp's own `convert_hf_to_gguf.py`, vendored verbatim at the pinned revision** under `third_party/llama.cpp-convert/` -- which at that revision is a `conversion/` package of seventy-nine modules plus the three chat templates it reads by path, not one file -- compiled in as chunked literals under MSVC's limit and seeded under `training/scripts/convert/`, run under the environment's interpreter with the `convert` set (floors from its own requirements file). The GGUF is written and its header verified **before the config is touched**: a new `llamacpp` entry appended, or an existing one's `model_path` replaced in place through the new section-scoped `set_backend_model_path`, byte-exact on two copies of the shipped template. Then the **version ledger** (`training/versions/<backend>.json`) with `max + 1` numbering -- the reference derived numbers from `len(versions)`, so they regressed after the first prune and a later promote overwrote a live file -- and `retain_versions` (3) pruning the oldest inactive GGUFs, never the active one, marking entries rather than erasing them so a rollback can name what is gone. The fused checkpoint goes after a successful conversion unless `--keep-fused`. **A failure at any step leaves the config and the ledger unchanged**, and removes what the failed step left.
- [x] **`apogee train rollback <backend>`**, repointing at the highest version below the active one (not `active - 1`: numbers have gaps) and **deleting nothing**, a pruned or missing target refused by name; **`train versions [<backend>]`** and **`train status`** reading the manifests and ledgers (`training/store`) -- the filesystem the source of truth, nothing cached.
- [x] **The control plane, reads only**: `GET /v1/admin/training/status|runs|runs/{id}|versions` under the bearer, off the same store; `active_pipeline` and `cycle_active` reserved in the status shape for the pipelines item. **Every control action is a parity carve-out with no route**, and a test sends every mutating method to every training path and requires never a 200. Fifty-eight admin rows.
- [x] **`TrainingConfig`** grows `trainer`, `judge_backend`, `eval_suite_path`, `retain_versions`, `gate_mode`, validated at load and documented in the template; the doctor's `Training` section grows the converter tree as one row, the trainer this host would use and its set, the `convert` set, and every ledger's consistency with the config -- warnings, never a failed install. `apogee train setup --with convert` installs the converter's stack. [training.md](../reference/training.md) extended with the run; the `training/` package description and the converter in the codebase maps.
- [x] **49 new test cases** (1416 in all); `cli.train_lifecycle` on the real binary.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| The pairwise baseline | **The untuned base through the same candidate runner** *(default taken)* | The adapter-versus-base question is the one promotion asks; the reference's `models.default` was an unrelated model. |
| The GGUF | **F16; `--quantize TYPE` only when the binary links llama.cpp, else refused naming `apogee models quantize`** *(default taken)* | The in-process quantizer exists behind that flag; a refusal that names the way out beats a silent F16. |
| Retention | **`retain_versions: 3`; 0 keeps all; pruned entries stay in the ledger as history** *(default taken, extended)* | Multi-gigabyte files argue for a bound; keeping the entry is what lets `rollback` say "v1 was pruned" rather than "only one version". |
| The fused checkpoint | **Removed after a successful conversion unless `--keep-fused`; also removed when a later step fails** *(default taken, extended)* | A fused tree is model-sized and the next attempt fuses again anyway. |
| Where promoted GGUFs live | **`training/versions/<backend>/v<N>.gguf`** *(default taken)* | Beside the ledger, inside the private row. |
| The read routes | **`/v1/admin/training/*`, bearer-gated** *(default taken)* | Local training state belongs to the control plane. |
| The converter | **Vendored whole -- the entry script, the `conversion/` package and the three templates it reads -- and compiled in as chunked literals** | At the pinned revision the converter is a package, not a file; a converter fetched at setup would be a second downloader with its own failure modes, and one that exists only when llama.cpp is linked would make promotion a build-flag feature. `gguf` still comes from PyPI. |
| `train_peft.py`'s trainer | **transformers' `Trainer` with an exact prompt mask; no trl** | trl removed the collator the reference imported, so that driver cannot start on a fresh install; the mask needs no library, and the template heuristic it replaced was a guess. The `peft` set drops `trl`. |
| `train_mlx.py`'s data | **The `{train,valid}.jsonl` directory laid out from the dataset, validation a copy of the first tenth** | `mlx_lm.lora --data` refuses a file; holding examples back from a small synthetic dataset would cost more than an in-sample validation number, and the eval gate is the real check. |
| The judge's budget | **1024 tokens, not 10** | The rerank judge's finding, live: a reasoning judge never reaches its verdict at a small budget, and under never-fail that silently ungates every judged item. |
| The verdict parse | **The first word, exactly `A`/`B`/`TIE`** | The reference's first-byte read counted "Both are fine" as a vote for B. |
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
- [x] **The cycle** (`training/cycle`, `apogee train cycle run [--source] | status | halt | resume`): unattended, scheduler-invoked training as **one gated pass per invocation** -- no daemon, no `--watch`, launchd and cron documented -- under a PID lock created exclusively and the **circuit breaker** (`halted`, or `consecutive_fails ≥ circuit_breaker_k`, refused naming `cycle resume`). The sources: a `directory` queue of `*.jsonl` (`--source <dir>` for one run), and `sessions`, the user's own persisted chats mined **through the one miner** `datasets create --from sessions` uses, **only with `log_consent: true`** (a source without it fails the config load naming the two risks) and **only sessions newer than the watermark** the history keeps, so a conversation is never trained on twice. No data records `skipped` and counts no failure; otherwise the sources merge, the named pipeline runs with **every stage's dataset replaced by the merged file and its own suite kept**, and the **anchor-baseline dual gate** applies: the final stage's cumulative score must not regress beyond `regression_threshold` against the last passing cycle **and** against the pinned anchor (the first passing cycle's version, or `anchor_version`). A pass promotes into `training.cycle.backend` **through the same promote body the command runs** (its gate runs again), consumes the queue files, advances the watermark and resets the count; a fail discards the candidate -- **nothing reaches inference** -- counts, and at `k` halts with the reason in the record. `history.json` is written atomically at every outcome; `halt` and `resume` are its two edits, replacing the reference's hand edit; a failed or refused pass exits non-zero so a scheduler's log shows it.
- [x] **One promote body** (`commands/train.cpp`): the gate, the name, the quantize type, the build, the config edit only after the GGUF parses, the ledger only after the config -- one function the `promote` command calls and hands to the regime and the cycle as a closure, so "a failing candidate never reaches inference" is one rule in one place. The mock trainer is now **scripted with an `answer`** the trained candidate replies with, carried into a fused checkpoint and inherited by a stage trained from it -- which is how a stage that *regresses* an earlier suite is driven through the real command line and the lifecycle check. `train status` rolls up the cycle and the pipelines; the doctor's `Training` section validates every named pipeline, the cycle block (an unknown pipeline or a non-`llamacpp` backend a failure) and reports a halted history with `cycle resume` as the remedy.
- [x] **The reads**: `GET /v1/admin/training/cycle` (the history plus `active` from the lock; `404` until the first run), the pipeline kind under `/runs` and `/runs/{id}`, `status` reporting `pipelines`, `active_pipeline`, `cycle_active` and the history's headline; **every control action a carve-out** -- `pipeline run|resume`, `regime run`, `cycle run|halt|resume` -- with the reference's `POST .../cycle/halt` deliberately not ported, held by the test that sends every mutating method to every training path (the cycle's control paths included). Fifty-nine admin rows. [training.md](../reference/training.md) grows the three sections with the launchd and cron examples; [http-api.md](../reference/http-api.md) the cycle route.
- [x] **`TrainingConfig`** grows `pipelines`, `regimes` and `cycle`, validated at load (a stage without a name, dataset or suite; a bad method; a number out of range; an unknown source type; a `sessions` source without consent; a breaker below zero; a threshold outside `[0, 1]`), the template documenting all three; `training/pipelines`, `regime/` and `cycle/` beneath the one `training` row, created by first use, no new row. `platform::current_process_id` for the lock's PID.
- [x] **29 new test cases (1445 in all)**; `cli.train_lifecycle` extended on the real binary.

**Decisions**

| Decision | Choice | Why |
|---|---|---|
| The breaker and the threshold | **`circuit_breaker_k: 3`, `regression_threshold: 0.0`** *(default taken)* | Ommi's documented example values: strict no-regression, and a bound on unattended failure, by default -- "not recommended for unattended operation" is a default, not a footnote. |
| Rehearsal | **Off unless a stage sets `rehearsal_fraction`** *(default taken)* | Ommi's default; a stage that wants rehearsal says so. |
| A regime's per-kit count and `--all-kits` | **The kit's `synth.count` unless `--count`; `--all-kits` alphabetical, an explicit `--kit` list winning** *(default taken)* | Ommi's rules; the explicit list is how the stage order is curated. |
| The sessions source's filters | **`backend` and `since`, through the one miner `datasets create` uses** *(default taken)* | The one filter Ommi declared (`log_filter_backend`) and never applied, made real without a second miner. |
| The rehearsal mix | **Written beside the stage's run as `rehearsal.jsonl` and recorded as the stage's dataset, not a temp file removed after** | The manifest must name what trained, and its digest must be of a file that exists. |
| The cycle's exit code | **A failed or refused pass exits non-zero** | The reference returned 0 on a failed gate; under a scheduler that is a silent night. |
| Sorting the runs list | **Both kinds under `/runs`, newest first by start time, tagged** | One list a client pages, rather than a second endpoint for pipelines. |
| `GateMode`'s home | **Moved to `eval.h`** | Read by `promote` and the pipeline alike; `store.h` now includes the pipeline manifest, so `pipeline.h` could not include `promote.h` back. |
| The pinned anchor's score | **The history's record for that version, else no anchor half** | Ommi declared `anchor_version` and never read it; a pinned version needs a score to gate against, and the history has it. |
| The lock's exclusive create | **`platform::create_exclusive_file` -- `O_CREAT\|O_EXCL` on POSIX, `CREATE_NEW` on Windows -- plus `platform::current_process_id`** | The first cut used C11 `fopen("wx")` and tripped the owning-memory lint on its deleter; an OS mechanism belongs in the one place `#ifdef`s live anyway, and the seam version needs no handle wrapper at all. |
| The cycle's score | **The final stage's cumulative score, whatever its status** | The reference read the last *passed* stage's, which the 100% gate makes 1.0 by definition -- so its anchor gate could never fail, and the mutation run found the same dead gate here on the first pass (the "failed gate still promotes" mutant survived because no test could reach a regression). Under the hard gate the two readings agree; under `gate_mode: soft` the dual gate is the one that holds. |

**Verified on the real binary.** `cli.train_lifecycle` continues with the mock and no Python: a two-stage pipeline whose second stage regresses the first's suite aborting under the cumulative gate (stage 0 fused, the last stage not, the lineage in the stage manifests), `pipeline status` with the resume hint, `resume` after the data is fixed completing and the last stage promoted like any run, a complete run refused; a regime over two kits with the mock backend as the teacher and `--no-promote` (a dataset and a suite per kit, one pipeline, no new ledger); the cycle from a queue directory -- skipped with nothing queued, a pass promoting into `training.cycle.backend` with the anchor set and the file consumed and not a config line removed, a regression failing, discarding and tripping the breaker at `k=1`, the halted loop refused naming `cycle resume` and reported by `check`, resumed and running again with the lock released; `train status` rolling both up; `check` green. The merge-blocking build passes every test (1445) through `cicd.sh`; a rebuild after the lock's lint fix passed all but two in a parallel `ctest` -- two models-package cases of the known temp-directory flake class, untouched by this item, passing serially and on three repeats; the llama build passes all but the two embedstore ingest cases of the same class, passing serially. `make lint` ran clean over the tree once the lock moved into the platform seam (the full run's one error was that deleter; the three files it changed re-linted clean in the error classes). The format check is clean.

**Guardrails, each mutation-tested.** 82 mutants, every one caught in its final form. Four were re-formed after the first run because `make format` had reflowed the lines their first shape named, and one was re-formed because its first shape was equivalent (dropping the "no prior data" short-circuit on the previous-cycle half changes nothing while scores are non-negative; the re-form makes no data *fail*, which the gate must not). **Two survived on first contact, and one of them was the design finding above:** "a failed gate still promotes" survived because no test could reach a regression -- the cycle read the last *passed* stage's score, which is 100% by definition, so the dual gate was dead code, in the reference as much as here; the cycle now reads the final stage and a soft-gate case drives a real regression through it. The other was a config test that refused a pipeline with no `stages` key but never one with `stages: []`. The mutations: stage 1's base not the fused checkpoint, the last stage fused too, the cumulative suite only the stage's own, a hard-gate failure ignored, the gate below 100%, a transition not written, `parent_run` and `pipeline_run_id` not set, resume always from stage 0, a complete run and a drifted spec resumable, cancellation not aborting, rehearsal never mixed and the fraction ignored, a failed stage not fused under continue, `completed_at` never set, `last_passed` counting every stage, the stage run never marked complete, its eval not recorded, the judge's baseline the candidate itself, a stage-count mismatch and a path-shaped id accepted; the teacher flag not winning, `--all-kits` unsorted and overriding `--kit`, the regime's `iters` never overriding, no kits accepted, the eval suite not materialised, the count not passed, `promote_run_id` not set, an empty teacher output not an error; `k = 0` not disabling the breaker, a halt ignored, resume not resetting the count, non-jsonl files collected, consumed files copied not moved, consent not required, the watermark ignored, the newest session not tracked, blank lines merged, the threshold ignored, no prior data failing, the anchor half ignored, the first passing score read instead of the last, a pass not resetting the count, a skip counted as a failure, the breaker tripping one late, no data still training, a failed gate still promoting, the queue not consumed, the anchor never set and overwritten on every pass, the lock not required and not exclusive and never released, the breaker not checked, stage datasets not replaced by the merge, the history not saved on a failure, the watermark not advanced, a failed promotion counted as a pass, the pinned anchor ignored, `consecutive_fails` not persisted, `history_exists` always true; a sessions source without consent loading, an unknown source type loading, the threshold unbounded, zero stages accepted, `eval_suite` not required, the breaker's default not 3; the active pipeline a complete one, pipelines oldest first; `?kind=pipeline` keeping the runs, the cycle route answering with no history, `active_pipeline` never reported; the mock's answer ignored and not carried into the fused checkpoint; a halted cycle not warned and a non-llamacpp cycle backend ok by the doctor; a failed cycle exiting 0, `--no-promote` ignored, the config edited despite a failed build, a resume refusal not honoured at the command.
