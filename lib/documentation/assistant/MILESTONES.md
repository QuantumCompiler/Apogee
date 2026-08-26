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
