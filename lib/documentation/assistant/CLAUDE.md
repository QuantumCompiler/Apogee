# CLAUDE.md

Project context for coding agents. Read this before making changes. The root [`README.md`](../../../README.md) (not yet written) is the public-facing overview (what Apogee is + install/build); this file and the rest of `lib/documentation/assistant/` are the contributor-facing detail. This file tells you **how to work here**; the sibling documents tell you **what** and **why**.

## Reading order & the docs system

`lib/documentation/assistant/` is a docs-as-source-of-truth system — work is discussed in chat, written up here, then handed to a coding session to build. Read them in this order, then pick the one that matches your intent:

| Doc | Purpose |
|---|---|
| **CLAUDE.md** (this file) | The contributor entry point: how to work, the hard rules, the codebase map. |
| [**SPEC.md**](SPEC.md) | The product spec — what Apogee is, its scope, non-goals, and design principles. The "why" behind the shape of the project. |
| [**ROADMAP.md**](ROADMAP.md) | The high-level running board — each release at the feature/theme level, plus loose ideas and explicit non-goals. |
| [**backlog/**](../backlog/README.md) | **The work queue: one document per pending work item** (a sibling directory of `assistant/`). Its README carries the format, lifecycle, and a **priority-ordered index** — an agent takes an item and implements it straight from its document; on ship the work is recorded in MILESTONES.md and the document deleted. |
| [**MILESTONES.md**](MILESTONES.md) | The detailed record of finished work — what shipped and how it was built. History and reference. |
| [**DEVELOPER.md**](DEVELOPER.md) | The package-by-package architecture reference: every directory, file, interface, and build/test command. |

**How work flows through the docs:** a feature is discussed in chat → written up in ROADMAP.md (and SPEC.md if it changes product shape) → given its own document in `lib/documentation/backlog/` once specced → an agent takes the item and **builds it straight from that document** → folded into MILESTONES.md when done, and the backlog document deleted. There is no intermediate claim step or separate TODO file — the backlog document *is* the working spec. See **Working process** below.

---

## What this is

Apogee is an AI harness: one system for running LLM workloads against the Anthropic, OpenAI, and Google cloud APIs as well as local models served through llama.cpp. It is a from-scratch, primarily-C++ re-implementation of **Ommi** (`~/Data/Development/Projects/Ommi`, a mature Go harness) — same product shape and docs-first process, with deliberate divergences recorded in [SPEC.md](SPEC.md) → Background. The repo is brand-new — these docs are the source of truth from which the first code will be built. See [SPEC.md](SPEC.md) for scope and principles and [MILESTONES.md](MILESTONES.md) for the shipped-feature record.

## Stack & environment

- **Language:** **C++20** (decided 2026-08-25 — the only standard uniformly supported across all six targets; promotion to C++23 would be a deliberate one-line change in `lib/src/cli/CMakeLists.txt`, not a drift). Satellite scripts (training drivers, MCP servers) may stay Python, as in Ommi.
- **Build & tooling:** **CMake ≥ 3.25** with **FetchContent** for dependencies (pinned tags, `FIND_PACKAGE_ARGS`, `SYSTEM`), **Catch2 v3** for tests via ctest, clang-format + clang-tidy for style and the ownership gate. Each application under `lib/src/<app>/` is a self-contained CMake project owning its own build; `lib/scripts/cicd.sh` drives them all.
- **Key dependencies:** the Anthropic, OpenAI, and Google LLM APIs (cloud backends, called directly over HTTPS); llama.cpp (local inference, in-process, pinned at `549b9d84` under `lib/src/cli/third_party/`, including its `mtmd` multimodal library for local vision — added as a single subdirectory rather than via `LLAMA_BUILD_TOOLS`, which would build half a dozen binaries Apogee has no use for; **off by default** — `-DAPOGEE_ENABLE_LLAMA=ON` builds it, and without it the llamacpp backend refuses construction with a message saying how to turn it on). Standardized library picks, decided once for the whole project: **nlohmann/json** (JSON), **yaml-cpp** (YAML — **read path only**; config writes go through text surgery, never a marshal), **CLI11** (CLI parsing + completions), **libcurl** (HTTP client — wired 2026-08-26; **found on the system, never fetched**, so it uses the platform's own TLS stack and trust store), **replxx** (chat line editing; readline was ruled out on licence grounds), and **SQLite** (the chunk store and its FTS5 lexical index — the one dependency deliberately *fetched* rather than found, because FTS5 is a compile-time flag and a system copy without it fails only once a user has ingested a corpus). Downstream items consume these rather than reopening them.
- **Platforms:** decided 2026-08-24 — Linux, macOS, and Windows, both ARM and x86: six targets (`linux-x64/arm64`, `macos-x64/arm64`, `windows-x64/arm64`), a wider matrix than Ommi's (which shipped no Windows and no Intel-Mac binaries). Each target builds natively on its own CI runner via `lib/scripts/cicd.sh --platform <target>`. Windows membership means the POSIX mechanisms named in backlog docs (forkpty PTY tests, tcflush, lsof assertions, 0600 modes, getpeername) need Windows equivalents or recorded per-item skips. **The portability seam exists** at `lib/src/cli/source/platform/` (shipped 2026-08-25) — that is the one place platform `#ifdef`s belong; everything else asks it. Primary dev host is macOS. CI gates on `macos-arm64` only for v0.1.0; the other five targets run informationally until they stabilize.
- **Repository:** https://github.com/QuantumCompiler/Apogee. **GitHub Releases is the distribution host** and GitHub Actions the release pipeline *(confirmed 2026-09-01)*: a pushed `v*` tag runs `.github/workflows/release.yml`, which builds all six targets on native runners through `lib/scripts/cicd.sh` and publishes one archive per target. `lib/scripts/install.sh` (macOS, Linux) and `lib/scripts/install.ps1` (Windows) fetch from there.

---

## Invariants

Apogee starts with **adopted design-time constraints** from Ommi (each earned there by a real bug or reversal; see [SPEC.md](SPEC.md) → Principles and Non-goals): interactive turns never open a listening socket — and local front-ends are pipes too: the GUI powers the CLI over stdin/stdout, while `serve` exists solely for server deployments answering remote REST clients; all install paths produce an identical layout; every capability reaches every surface through one shared core; secrets are `0600`, never logged, never returned over HTTP. Each is pinned as a **Core constraint** on the backlog items that own it, and earns a full `## ⚠` section here — rule, rationale, sub-rules, enforcement — as the enforcing code and tests land.

The first of them is now enforced in code:

### ⚠ Nothing links the CLI executable

**Rule.** `apogee_core` holds every capability. The `apogee` executable is a thin face over it — argv in, exit code out — and **no target may link `apogee`**: not tests, not the HTTP server (which lives inside `apogee_core` like every other surface), not a helper library.

**Why.** "Parity is the product" only holds if it is structural. If a capability can live in the executable, then a second surface either cannot reach it or has to reimplement it — and that is exactly how parity bugs are born. Keeping the executable empty means every capability is, by construction, reachable from anywhere that links the library.

**Sub-rules.**
- Shared code goes in a package under `lib/src/cli/source/`, never in `main.cpp`.
- Tests link `apogee_core`. Running the built binary as a subprocess is fine — that is not linking, and the `cli.*` ctest cases do exactly that to cover the user-facing contract.

**Enforcement.** `apogee_assert_link_policy()` (`lib/src/cli/cmake/ApogeeLinkPolicy.cmake`), called at the end of `lib/src/cli/CMakeLists.txt`, walks every target in the project and **fails the configure step** naming any that links `apogee`.

### ⚠ One config mutation path

**Rule.** Every config change, from every surface, forever, goes through the helpers in `harness/config_edit.h`. Nothing else writes a config file. **Never** load the config into structs, modify them, and serialize back.

**Why.** Two reasons, and the second is the one that bites. First, a config file's comments are most of its documentation, and every YAML library — yaml-cpp included — drops them on parse and reorders keys on emit, so load-modify-save silently deletes the user's file contents. Second, "parity is the product": when the admin plane lands, an edit made over HTTP has to be byte-identical to the same edit made from the CLI. That is structural only while there is exactly one path.

**Sub-rules.**
- The transforms are pure — text in, text out. Only `edit_config_file()` touches the filesystem.
- Every edit re-parses its own output before it lands, and writes via temp-file-then-rename, so a bad transform cannot corrupt a config.
- Reading is `load_config` / `parse_config`; those are the *only* uses of yaml-cpp.

**Enforcement.** The golden-file suite in `tests/harness/config_edit_test.cpp` asserts byte-identical round trips over a comment-dense fixture, plus CRLF preservation and the atomic-failure paths. `apogee config` is a thin caller — it formats no YAML of its own. The one config write a user never types `config` for — `apogee embed ingest` registering a new collection — goes through the same helpers, and `cli.config_lifecycle` holds it to a literal equality over the whole shipped template: pristine bytes plus exactly the new entry.

### ⚠ The harness never includes backends

**Rule.** The dependency runs one way: `backends/` includes `harness/`, never the reverse. **The same applies to `agentloop/` and `agent/`** — the loop consumes the IR and the Harness, not backends. When a guarded layer needs something only a backend knows, it crosses as **plain data** (that is what `harness::ModelBehavior` is) or as a capability interface.

**Why.** In Go this is free: the reverse edge is an import cycle and the build fails, which is why Ommi has the same seam. C++ gives you nothing — a `#include "backends/anthropic.h"` in the harness compiles perfectly and the layering is silently gone. Once it is gone, the harness knows about vendors, and "backend-agnostic core" stops being true in the one place it has to be.

**Sub-rules.**
- A capability a backend has and the harness needs to ask about becomes a **capability interface** in `provider.h`, discovered by the Harness (see below) — not an include.
- Model-family knowledge crosses as `ModelBehavior`, plain strings and bools.
- `agentloop/` and `agent/` are guarded too, as of Milestone F. A loop that includes a backend starts special-casing one vendor's tool dialect, and "one shared loop for all surfaces" quietly becomes "one loop with an Anthropic branch".
- `commands/` is deliberately NOT guarded: it is the composition root, and assembling providers is its job.

**Enforcement.** The `harness.layering` ctest case (`tests/layering.cmake`) greps every source in `harness/`, `agentloop/`, and `agent/` for the forbidden include and fails naming the file. It refuses to run against an empty source list for any guarded package, so it cannot pass vacuously.

### ⚠ Capability probes never leak a cast

**Rule.** Optional provider capabilities (`EmbeddingCapable`, `StatusReporting`, `InTextToolCalling`, `ModelBehaviorReporting`) are **discovered by the Harness**. Callers ask a plain typed question — `harness.can_embed(model)`, `harness.model_behavior_for(model)` — and never write a `dynamic_cast` of their own.

**Why.** Ommi gated embedding behind a hardcoded allowlist of backend types on the reasoning that its only cloud vendor could not embed; that rationale did not survive OpenAI and Google, both of which embed over their APIs. A capability question answered by testing a type turns into a switch over vendors that has to be edited every time one is added. Asking the object is the version that keeps working.

**Sub-rules.**
- A probe on an unroutable model answers "no" rather than throwing — a caller asking about a capability should not have to handle a routing failure too.
- `ModelBehavior`'s zero value means *unknown*, and **unknown must be treated as permissive**: failing to recognise a tool call means nothing dispatches AND the raw markup is printed to the user as if it were the answer.

**Enforcement.** `tests/harness/harness_test.cpp` → the `[harness][capability]` and `[harness][behavior]` cases, and `tests/backends/factory_test.cpp` → `[factory][embed][capability]`, which asks the harness about providers the factory actually BUILT: Anthropic answers no, OpenAI and Google yes, and nothing in the test names a type.

### ⚠ Interactive turns never open a listening socket

**Rule.** Only `apogee serve` may own a port. No interactive command, and no backend serving one, opens a listening socket — cloud backends make outbound HTTPS calls and llama.cpp runs in-process.

**Why.** A backend that quietly starts a local server still answers correctly, so nothing looks wrong until someone notices an open port on a shared machine. It is also the structural half of "interactive = pipes, serving = server": once one interactive path listens, "local front-ends are pipes too" stops being true and the GUI's stdio contract loses its justification.

**Sub-rules.**
- A persistent child process is not a listening socket — the vendor-CLI backends stay compliant by construction.
- **`serve` has exactly one reviewed exception** (Milestone T): `httpserver/serve.cpp`, the listener, is the only object file in the whole link that may reference `listen`/`accept`. The allow-list names that object; every other file under `httpserver/` is as socket-free as the rest of the library, which is what lets the handlers be tested with no port at all. Moving the listener is a deliberate edit to the allow-list, never a drift.
- `serve` is for **server deployments** answering remote REST clients. Nothing in `httpserver/` may become load-bearing for a local `chat` or `complete`, and a non-loopback bind fails closed behind `--allow-remote`.

**Enforcement — two checks, and neither is sufficient alone.**
- `cli.no_listen_symbols` (`tests/no_listen_symbols.cmake`) — `nm -A -u` on `apogee_core` **and on every library the `apogee` executable links that this build produces, static or shared, transitively** (the list is collected by walking the link graph in `tests/CMakeLists.txt`, so a new in-process dependency is scanned without anyone remembering to add it): every reference to `listen`/`accept` is attributed to the object file — or, for a shared library, the file — that made it, and only `serve.cpp.o` may. Deterministic, no timing; catches our own code however brief the window, and a third-party library's — the gap that became real once llama.cpp started running **in-process** (Milestone J). Shared libraries are scanned by name because llama.cpp *is* one under the llama preset, and a shared library's own imports never appear in the executable's symbol table — so the earlier binary scan, which claimed to cover it, did not (found in Milestone T). Imported system libraries are the platform's and are left out; libcurl legitimately calls `accept` for FTP. Two vacuity guards: it refuses an empty list, and it **requires** the allow-listed object to be seen referencing the symbols — so a stale list or a changed `nm` format fails rather than passes. Blind to a child process.
- `cli.complete_opens_no_listening_socket` (`tests/no_listen_check.sh`) — polls `lsof` across a real turn. Catches a *spawned* server holding a port. Blind to a sub-millisecond window. POSIX only; Windows is a recorded skip. Its positive twin, `cli.serve_lifecycle` (`tests/serve_e2e.sh`), asserts that `serve` **does** hold one — together they are the boundary.

Both were verified against a build that deliberately calls `listen()`: the symbol check failed it, the runtime check did not. That asymmetry is exactly why the pair exists — see MILESTONES.md → Milestone E. The per-object attribution was verified the same way in Milestone T: a `listen()` planted in `http_types.cpp` failed the check naming the file, and a throwaway shared library calling `listen()` handed to the check failed it naming the library.

### ⚠ One layout declaration, and every install path reads it

**Rule.** What `~/.apogee/` contains is declared **once**, in `harness/layout.h`. Every path that creates the tree, and the one that validates it, enumerates that declaration. No installer, no Makefile target, and no test may carry its own list of directories.

**Why.** Ommi's dominant early bug class was *silent install drift*: `make install` seeded one tree, `install.sh` another, the updater a third, and `check` validated a fourth. Each list was correct when written and they diverged one commit at a time. Nothing ever failed loudly — a fresh install simply lacked a directory that some command needed months later, and the report came back as an unrelated bug in that command.

Making this a review rule ("remember to update all four") is what Ommi tried; it holds until the day someone is in a hurry. Making it structural means there is nothing to remember: a contributor adds a row, and the installer and the doctor both learn about it because neither knows anything the row does not say.

**Sub-rules.**
- `seed_data_directory()` is the **only** implementation that creates the tree. `apogee check --fix` calls it; both installers call `apogee check --fix`; `make install` ends with the same call. A shell script that made directories itself would be a second declaration.
- Any asset landing under `~/.apogee/` is added to the declaration in the **same commit** as the code that uses it — the parity rule, adopted from Ommi verbatim.
- `check` reports the contract but never edits config. `--fix` may repair the local install (a missing directory, a wrong mode); it may not decide what a dangling `model_path` meant.
- A check that cannot run on a platform reports **skipped**, never passing. Windows has no `0700`, and "modes OK" there would be a lie shaped exactly like a pass.

**Enforcement.** `cli.install_parity` (`tests/install_parity.sh`) installs twice into throwaway roots and requires the sorted directory listings *and modes* to be identical, refuses to pass on fewer than five directories (so it cannot pass vacuously), and requires a freshly seeded install to pass `apogee check` with zero failures. CI runs it on Linux and, in PowerShell, on Windows. Verified against a deliberate drift — removing one directory from the seeding loop — which it caught.

The first version of this did **not** catch that drift, and the reason is worth keeping: there were two seeding implementations, and the mutation hit the one the installers did not reach. Two implementations of the layout is the bug, even when both are correct.

### ⚠ One retriever per turn, resolved once, reported honestly

**Rule.** A turn searches a collection with exactly one of `lexical`, `vector`, `hybrid`, decided by **one function** — `agentloop::resolve_turn_retriever` — from the flag, the collection's pin, and facts about the embedder and the store. Every surface reports the retriever that **actually ran**, including when it degraded: nothing ever claims `vector` or `hybrid` for a search that ran lexical.

**Why.** Normalised BM25, cosine and RRF are three incomparable scales; a score shown without its scale invites a comparison that cannot be made. And the rules for *which* scale a turn is on are subtle — model match, full coverage, a single vector space, an explicit ask versus a pin. Ommi's deepest retrieval bugs were those rules drifting between surfaces. A rule that exists once cannot drift.

**Sub-rules.**
- An explicit `--retriever vector` that cannot run is a **hard error** naming lexical as the way out — the user asked for something impossible and must not get something else under that name. A `retriever: vector` **pin** that cannot run **excludes** the collection with a note; it is never silently searched the other way.
- `hybrid` is explicit only; `auto` never resolves to it. A hybrid ask without a vector half runs and is *reported* lexical.
- A store whose vectors came from another model, or that holds any chunk without a vector, or more than one vector width, is demoted **wholesale** to lexical with the re-ingest hint — never queried as a fraction.
- Rerank can never cost context: every judge failure returns raw order with `reranked: false`, and `reranked` is set from the same place as the ordering.
- **Spend** (user decision, 2026-09-13): a whole collection is never vectorised through a metered embedder on Apogee's initiative; a question against one already built is one small call and is allowed. Whether an embedder is metered is a fact the provider states, never a type list.

**Enforcement.** `tests/agentloop/retrieval_test.cpp` is the exhaustive rule table (flag/pin/auto × embedder × store), `tests/agentloop/rerank_test.cpp` has one case per failure path, and `cli.config_lifecycle` drives the real binary through a model mismatch, an explicit-vector refusal, hybrid, and a `check` that rejects a `retriever:` typo. All mutation-tested.

### ⚠ Smart pointers, never owning raw pointers

The Code Style rule below is lint-enforced, not aspirational: `make -C lib/src/cli lint` fails on a raw `new`/`delete`, an owning raw pointer, or `malloc` anywhere in the CLI's `source/` or `tests/` (`cppcoreguidelines-owning-memory`, `cppcoreguidelines-no-malloc`, `modernize-make-unique`/`make-shared`, promoted to errors in `.clang-tidy`). `third_party/` is out of scope by construction. CI runs it in the `lint` job, which — since the 2026-08-31 trigger change — starts on a pull request into `stable`, on the merge itself, or on an explicit `gh workflow run CI --ref <branch>`, but never on an ordinary push. So the gate still stands in front of every merge; what it no longer does is watch a topic branch with no PR open. Run `make -C lib/src/cli lint` there.

---

## Codebase Map

| File / package | Contents |
|------|----------|
| `lib/documentation/assistant/` | These contributor docs (CLAUDE, SPEC, ROADMAP, MILESTONES, DEVELOPER) — **this file is the entry point**; there is no repo-root pointer. |
| `lib/documentation/backlog/` | The work queue — one document per pending item; priority-ordered index in its README. |
| `lib/documentation/reference/` | **User-facing** reference docs, as opposed to the contributor docs above. `machine-mode.md` — the JSONL protocol a GUI drives the CLI over, written for front-end authors. `cli.machine_schema_conformance` pins it to `json_reporter.cpp`, so an undocumented event or a documented-but-unimplemented one fails the build. `http-api.md` — the HTTP contract a **remote** client builds against (`apogee serve`: routes, extensions, meta-frames, sessions, errors, the bind policy); `cli.http_api_conformance` pins its route headings to the table in `httpserver/mux.cpp` in both directions. |
| `.claude/skills/` | Repo-local agent skills. `apogee-backlog-item` — take the next (or a named) backlog item: load these assistant docs, pick by the index's gate rules, resolve `[user]`/`[default]` open calls, build, then run the Documentation and Status flow. `apogee-create-backlog-item` — spec a new item: read the format/SPEC/roadmap first, check the Ommi analog, write the document to the quality bar, place it in the index, update ROADMAP (and SPEC only on shape changes). `apogee-document-update` — the pre-MR docs pass: audit every document against the branch diff, enforce the Documentation and Status flow, validate links/index/tags mechanically, report what needs the user. `apogee-pull-request` — draft the MR description from the branch's evidence (deleted backlog docs + MILESTONES entries = shipped items; dated decisions; verification), run after the docs pass. Skills point at the docs rather than duplicating them — the docs stay the single source of truth. |
| `lib/scripts/` | Repo scripts. `install.sh` (macOS, Linux) and `install.ps1` (Windows) — the two installers; each downloads the release archive for the host, installs the binary and completions, and then runs `apogee check --fix` so the **binary** creates and verifies the data directory (neither script owns a copy of the layout). `cicd.sh` — the CI/CD entry point: builds every application for any of the six release targets (`--platform linux-x64 … windows-arm64 \| all`; builds what the host can natively, defers the rest to the CI matrix), with `--fresh` for a CI-style clean-room clone-and-build, `--test`, `--clean`, `--jobs`; the GitHub Actions matrix invokes this same script per native runner so local and CI builds share one path. `cicd-completion.bash` — tab completion for its flags (source it from your shell rc). |
| `.github/workflows/ci.yml` | CI: the six-target build matrix (only `macos-arm64` is merge-blocking in v0.1.0; the rest are informational until they stabilize), plus format+lint, the llama.cpp integration job, the install-parity gate (POSIX blocking, Windows non-blocking), and the clean-room build. `.github/workflows/release.yml` | Releases: a pushed `v*` tag builds all six targets on native runners through `cicd.sh`, tests before packaging, verifies the staged binary **runs**, and publishes one archive per target to a GitHub Release. A thin caller into `cicd.sh` — it lives outside `lib/src/cli/` only because GitHub requires workflows at `.github/workflows/`. |
| `lib/src/cli/` | **The CLI application — a self-contained CMake project.** Build root (`CMakeLists.txt`), presets, `Makefile`, `cmake/` helpers, `third_party/`, `assets/` (the starter `config.yaml`), `.clang-format`, `.clang-tidy`, and `build/` output all live here beside the code. Targets: `apogee_core` (static library — every capability) and `apogee` (a thin argv→exit-code face). `source/` holds `main.cpp` plus one package directory per concern; `tests/` mirrors them. See [DEVELOPER.md](DEVELOPER.md) for the package-by-package breakdown. |

**Where new source code goes (decided 2026-08-24; app-owned builds confirmed 2026-08-25):** `lib/src/<app>/` — one directory per application, each a **self-contained CMake project** owning its own build, presets, dependencies, third-party pins, and style config, with `source/` and `tests/` as its first level. Nothing above `lib/src/<app>/` needs to know how that application compiles.

- **`lib/src/cli/`** — the CLI application (all of v0.1.0). Packages under `source/`: `commands/`, `harness/` (config engine, the on-disk layout contract, the one role resolver every surface shares, plus the provider interface, IR, and router), `backends/` (provider implementations — `mock`, `anthropic`, `openai`, `google`, each with its own `*_wire` translator; `llamacpp` over an injectable `llama_runtime` seam; the **embedding clients** — `openai_embed`, `google_embed`, `llamacpp_embed`, and the shared `embedding_batch` — behind which those three providers implement `EmbeddingCapable`; and the vendor-CLI family — `claude_cli`, `codex_cli`, `gemini_cli`, and `ollama_cli`, one per cloud vendor — over the `platform/child_process` seam — plus the shared HTTP client, SSE parser, JSONL framer, typed CLI event union, the Ollama thinking demultiplexer, local chat templates, the per-family **model profile** registry with its three streaming filters — reasoning, control-token headers, and the tool-call gate that withholds a native call from the display and hands the parser the same bytes — and the config→provider factory), `agentloop/` (the shared model→tool→model loop and its Reporter — whose adapters are `commands/cli_reporter` for the terminal, `commands/json_reporter` for machine mode, and `httpserver/sse_reporter` for a served stream, siblings over one seam — plus context measuring and compaction, and the retrieval layer: the one embedding seam, the **one per-turn retriever resolver**, the rerank judge under its never-fail contract, and the RAG prefix builder that rides the transient path), `agent/` (the tool registry, dispatch, and `fetch_url`), `ansi/` (colour and the mode matrix), `logger/` (session persistence and the operational log), `platform/` (OS mechanisms behind one interface: terminal, home directory, executable path, and child processes), `models/` (model files as artifacts: the GGUF header reader — ours rather than llama.cpp's, so it still works in the merge-blocking build that has no llama.cpp — plus the acquisition ladder, the provenance/integrity sidecar, a streaming SHA-256, in-process quantization behind the llama flag, and the Hugging Face and Ollama sources), `version/` (built), `embedstore/` (the chunk store: SQLite with an external-content FTS5 index for BM25, float32 vectors beside the text with a per-collection model binding, cosine and RRF hybrid search, codepoint-safe chunking, and the ingest walk that names every file it skips), `httpserver/` (the `apogee serve` inference plane: transport-neutral request/response types, a listener-free route table and handler over the **same** agent loop, the third Reporter adapter — SSE chunks and opt-in meta-frames — a session store that persists ordinary chat sessions, and `serve.cpp`, the one file in the whole link allowed to open a port), and `mcp/` (a reserved header stub, mirroring Ommi's package map — it names the backlog item that fills it). `assets/` holds the starter `config.yaml`, which a test keeps byte-identical to the template compiled into the binary; `completions/` holds the four shell stubs, each of which only calls back into `apogee __complete`.
- **`lib/src/darwin/` · `lib/src/linux/` · `lib/src/windows/`** — the GUI applications, one per platform (future — down the road, not yet planned; they will drive the CLI over the stdio machine mode). Each joins the `APPS` list in `cicd.sh` when it lands.

The per-item file breakdown is in the [`backlog/`](../backlog/README.md) documents' **Seam + files** sections, which are written against this layout.

**Build and test:**

```bash
lib/scripts/cicd.sh --test        # every app, host-native target — what CI runs
make -C lib/src/cli test          # the CLI alone
make -C lib/src/cli lint          # the ownership gate (see Code Style)
```

---

## Working process

Apogee is built docs-first: features are discussed in chat, written up in the planning docs, then handed to a coding session to build. Respect the flow between documents.

### Where new work is written down

- **A new feature or theme** → first a line in [ROADMAP.md](ROADMAP.md) (and an update to [SPEC.md](SPEC.md) if it changes the product's scope or principles).
- **Specced** (design decided) → its own document in [`lib/documentation/backlog/`](../backlog/README.md), one file per work item, added to the **priority-ordered index** in that directory's README (top = next to build). The backlog is the work queue — there is no separate TODO file.
- **Being built** → the agent works **straight from the backlog document** (it is the working spec); mark its index row **in progress** so parallel sessions see it's taken.
- **Completed** → recorded in [MILESTONES.md](MILESTONES.md), and the backlog document is deleted along with its index row (see **Documentation and Status** below). The backlog holds pending work only — never finished work.

### Working a backlog item

When asked to work on the next item, read the [`backlog/`](../backlog/README.md) index and take the **topmost item whose gate is satisfied** (skip gated items whose prerequisite hasn't shipped), or the specific item the user names. Read that item's document carefully — it is the working spec. Before writing any code, confirm scope and requirements with the user (the document's **Open calls** are the natural questions) — do not begin implementation until the user has answered. Complete one item at a time. When an item is finished, stop and wait for the user to explicitly ask to continue. (A bare "Continue" means: re-read this file, then take the topmost claimable backlog item.)

When applicable, also update the root `README.md` if the change affects how someone builds or installs the project.

### Versioning

Feature releases are `v0.x.0`; patch releases are `v0.x.y`. Development happens on a branch named for the upcoming release (currently `v0.1.0`) and merges into `stable` when the release is done. ROADMAP.md tracks each release at the theme level; MILESTONES.md records what actually shipped in detail. _TODO:_ the tagging/release procedure itself is not yet defined.

---

## Implementing a Feature

Every change should satisfy this before merge:

- [ ] Tests for the new behavior pass, and existing tests stay green: `lib/scripts/cicd.sh --test`.
- [ ] Formatting and lint are clean: `make -C lib/src/cli format-check` and `make -C lib/src/cli lint`.
- [ ] New tests are **hermetic** — no network, no models, no writes outside the test's own temp directory. A test that needs a live provider is not a unit test; go through the injectable seam instead.
- [ ] New source files are `.h`/`.cpp` pairs, added to the target's source list in `lib/src/cli/source/CMakeLists.txt`, and the test tree mirrors the package.
- [ ] The affected docs are updated in the same change (see **Documentation and Status**) — run the `apogee-document-update` skill before opening the MR; it audits every document against the branch diff and validates the system mechanically.

_TODO:_ further project-specific checklist items accrete here as invariants and conventions are established.

## Code Style

*(The two rules below are user decisions, 2026-08-24. They applied from the first line of code and are lint-enforced as of 2026-08-25 — see [⚠ Smart pointers, never owning raw pointers](#-smart-pointers-never-owning-raw-pointers) above. Formatting is settled in `.clang-format` and is never a review topic: run `make -C lib/src/cli format`.)*

- **Header/implementation split.** Every class/module ships as a `.h`/`.cpp` pair when applicable — declarations in the header, definitions in the `.cpp`. Header-only is the recorded exception, reserved for templates and trivial data-only structs. The backlog documents' **Seam + files** sections already name files as `foo.h/.cpp` pairs; follow them.
- **Smart pointers, never traditional pointers.** No raw `new`/`delete` and no owning raw pointers anywhere. `std::unique_ptr` is the default ownership type; `std::shared_ptr` only where ownership is genuinely shared (`std::weak_ptr` to break cycles); construct via `std::make_unique`/`std::make_shared`. For non-owning access, prefer references (or `std::string_view`/`std::span`) over pointers. C APIs (llama.cpp, SQLite, libcurl) hand out raw handles — wrap each in a `std::unique_ptr` with a custom deleter at the boundary class, and never let the raw handle escape it.
- _TODO:_ further naming conventions, error-handling idioms, and shared-helper locations accrete here as the first modules land.
- Match the surrounding code's conventions; when in doubt, find the closest existing analogue and follow it.

## Documentation and Status

When a work item ships, in the same change:

1. **Extend [MILESTONES.md](MILESTONES.md)** — fold the work into the matching milestone (or start a new one for a genuinely new area): goal, what was built, trade-offs.
2. **Delete the backlog document** and its index row — the backlog holds pending work only.
3. **Update [ROADMAP.md](ROADMAP.md)** — check the box / move the line to shipped.
4. **Update the [Codebase Map](#codebase-map) and [DEVELOPER.md](DEVELOPER.md)** for any new/moved/deleted files.
5. **Update [SPEC.md](SPEC.md)** only if scope, non-goals, or principles actually changed.

---

## Release and Install Infrastructure

Development happens on a version-named branch (currently `v0.1.0`) and merges into `stable` when the release is complete. _TODO:_ build, packaging, tagging, and install steps — to be defined once the stack is chosen.
