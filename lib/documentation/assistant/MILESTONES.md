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
| GBNF grammar sampling | Deferred | The stated default. Cloud tool calls arrive structured, so nothing here depends on it; `model-profiles-and-management` decides. |
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
