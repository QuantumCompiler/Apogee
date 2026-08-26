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
- **Key dependencies:** the Anthropic, OpenAI, and Google LLM APIs (cloud backends, called directly over HTTPS); llama.cpp (local inference, in-process, pinned at `549b9d84` under `lib/src/cli/third_party/`, off by default until the backend item lands). Standardized library picks, decided once for the whole project: **nlohmann/json** (JSON), **yaml-cpp** (YAML — **read path only**; config writes go through text surgery, never a marshal), **CLI11** (CLI parsing + completions), **libcurl** (HTTP client — wired 2026-08-26; **found on the system, never fetched**, so it uses the platform's own TLS stack and trust store). Downstream items consume these rather than reopening them.
- **Platforms:** decided 2026-08-24 — Linux, macOS, and Windows, both ARM and x86: six targets (`linux-x64/arm64`, `macos-x64/arm64`, `windows-x64/arm64`), a wider matrix than Ommi's (which shipped no Windows and no Intel-Mac binaries). Each target builds natively on its own CI runner via `lib/scripts/cicd.sh --platform <target>`. Windows membership means the POSIX mechanisms named in backlog docs (forkpty PTY tests, tcflush, lsof assertions, 0600 modes, getpeername) need Windows equivalents or recorded per-item skips. **The portability seam exists** at `lib/src/cli/source/platform/` (shipped 2026-08-25) — that is the one place platform `#ifdef`s belong; everything else asks it. Primary dev host is macOS. CI gates on `macos-arm64` only for v0.1.0; the other five targets run informationally until they stabilize.
- **Repository:** https://github.com/QuantumCompiler/Apogee (GitHub — making GitHub Releases the presumed distribution host; see the install-check-lifecycle backlog item).

---

## Invariants

Apogee starts with **adopted design-time constraints** from Ommi (each earned there by a real bug or reversal; see [SPEC.md](SPEC.md) → Principles and Non-goals): interactive turns never open a listening socket — and local front-ends are pipes too: the GUI powers the CLI over stdin/stdout, while `serve` exists solely for server deployments answering remote REST clients; all install paths produce an identical layout; every capability reaches every surface through one shared core; secrets are `0600`, never logged, never returned over HTTP. Each is pinned as a **Core constraint** on the backlog items that own it, and earns a full `## ⚠` section here — rule, rationale, sub-rules, enforcement — as the enforcing code and tests land.

The first of them is now enforced in code:

### ⚠ Nothing links the CLI executable

**Rule.** `apogee_core` holds every capability. The `apogee` executable is a thin face over it — argv in, exit code out — and **no target may link `apogee`**: not tests, not the future HTTP server, not a helper library.

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

**Enforcement.** The golden-file suite in `tests/harness/config_edit_test.cpp` asserts byte-identical round trips over a comment-dense fixture, plus CRLF preservation and the atomic-failure paths. `apogee config` is a thin caller — it formats no YAML of its own.

### ⚠ The harness never includes backends

**Rule.** The dependency runs one way: `backends/` includes `harness/`, never the reverse. When the harness needs something only a backend knows, it crosses as **plain data** — that is what `harness::ModelBehavior` is.

**Why.** In Go this is free: the reverse edge is an import cycle and the build fails, which is why Ommi has the same seam. C++ gives you nothing — a `#include "backends/anthropic.h"` in the harness compiles perfectly and the layering is silently gone. Once it is gone, the harness knows about vendors, and "backend-agnostic core" stops being true in the one place it has to be.

**Sub-rules.**
- A capability a backend has and the harness needs to ask about becomes a **capability interface** in `provider.h`, discovered by the Harness (see below) — not an include.
- Model-family knowledge crosses as `ModelBehavior`, plain strings and bools.
- The same rule will apply to `agentloop/`: it consumes the IR and the Harness, not backends.

**Enforcement.** The `harness.layering` ctest case (`tests/layering.cmake`) greps every harness source for the forbidden include and fails naming the file. It refuses to run against an empty source list, so it cannot pass vacuously.

### ⚠ Capability probes never leak a cast

**Rule.** Optional provider capabilities (`EmbeddingCapable`, `StatusReporting`, `InTextToolCalling`, `ModelBehaviorReporting`) are **discovered by the Harness**. Callers ask a plain typed question — `harness.can_embed(model)`, `harness.model_behavior_for(model)` — and never write a `dynamic_cast` of their own.

**Why.** Ommi gated embedding behind a hardcoded allowlist of backend types on the reasoning that its only cloud vendor could not embed; that rationale did not survive OpenAI and Google, both of which embed over their APIs. A capability question answered by testing a type turns into a switch over vendors that has to be edited every time one is added. Asking the object is the version that keeps working.

**Sub-rules.**
- A probe on an unroutable model answers "no" rather than throwing — a caller asking about a capability should not have to handle a routing failure too.
- `ModelBehavior`'s zero value means *unknown*, and **unknown must be treated as permissive**: failing to recognise a tool call means nothing dispatches AND the raw markup is printed to the user as if it were the answer.

**Enforcement.** `tests/harness/harness_test.cpp` → the `[harness][capability]` and `[harness][behavior]` cases.

### ⚠ Smart pointers, never owning raw pointers

The Code Style rule below is lint-enforced, not aspirational: `make -C lib/src/cli lint` fails on a raw `new`/`delete`, an owning raw pointer, or `malloc` anywhere in the CLI's `source/` or `tests/` (`cppcoreguidelines-owning-memory`, `cppcoreguidelines-no-malloc`, `modernize-make-unique`/`make-shared`, promoted to errors in `.clang-tidy`). `third_party/` is out of scope by construction. CI runs it on every push.

---

## Codebase Map

| File / package | Contents |
|------|----------|
| `lib/documentation/assistant/` | These contributor docs (CLAUDE, SPEC, ROADMAP, MILESTONES, DEVELOPER) — **this file is the entry point**; there is no repo-root pointer. |
| `lib/documentation/backlog/` | The work queue — one document per pending item; priority-ordered index in its README. |
| `.claude/skills/` | Repo-local agent skills. `apogee-backlog-item` — take the next (or a named) backlog item: load these assistant docs, pick by the index's gate rules, resolve `[user]`/`[default]` open calls, build, then run the Documentation and Status flow. `apogee-create-backlog-item` — spec a new item: read the format/SPEC/roadmap first, check the Ommi analog, write the document to the quality bar, place it in the index, update ROADMAP (and SPEC only on shape changes). `apogee-document-update` — the pre-MR docs pass: audit every document against the branch diff, enforce the Documentation and Status flow, validate links/index/tags mechanically, report what needs the user. `apogee-pull-request` — draft the MR description from the branch's evidence (deleted backlog docs + MILESTONES entries = shipped items; dated decisions; verification), run after the docs pass. Skills point at the docs rather than duplicating them — the docs stay the single source of truth. |
| `lib/scripts/` | Repo scripts. `cicd.sh` — the CI/CD entry point: builds every application for any of the six release targets (`--platform linux-x64 … windows-arm64 \| all`; builds what the host can natively, defers the rest to the CI matrix), with `--fresh` for a CI-style clean-room clone-and-build, `--test`, `--clean`, `--jobs`; the GitHub Actions matrix invokes this same script per native runner so local and CI builds share one path. `cicd-completion.bash` — tab completion for its flags (source it from your shell rc). |
| `.github/workflows/ci.yml` | CI: the six-target build matrix (only `macos-arm64` is merge-blocking in v0.1.0; the rest are informational until they stabilize), plus format+lint, the non-blocking llama.cpp compile proof, and the clean-room build. A thin caller into `cicd.sh` — it lives outside `lib/src/cli/` only because GitHub requires workflows at `.github/workflows/`. |
| `lib/src/cli/` | **The CLI application — a self-contained CMake project.** Build root (`CMakeLists.txt`), presets, `Makefile`, `cmake/` helpers, `third_party/`, `assets/` (the starter `config.yaml`), `.clang-format`, `.clang-tidy`, and `build/` output all live here beside the code. Targets: `apogee_core` (static library — every capability) and `apogee` (a thin argv→exit-code face). `source/` holds `main.cpp` plus one package directory per concern; `tests/` mirrors them. See [DEVELOPER.md](DEVELOPER.md) for the package-by-package breakdown. |

**Where new source code goes (decided 2026-08-24; app-owned builds confirmed 2026-08-25):** `lib/src/<app>/` — one directory per application, each a **self-contained CMake project** owning its own build, presets, dependencies, third-party pins, and style config, with `source/` and `tests/` as its first level. Nothing above `lib/src/<app>/` needs to know how that application compiles.

- **`lib/src/cli/`** — the CLI application (all of v0.1.0). Packages under `source/`: `commands/`, `harness/` (config engine + the provider interface, IR, and router), `backends/` (provider implementations — `mock` and `anthropic`, plus the shared HTTP client and SSE parser), `platform/`, `version/` (built), and `agentloop/`, `embedstore/`, `httpserver/`, `mcp/` (reserved header stubs, mirroring Ommi's package map — each names the backlog item that fills it). `assets/` holds the starter `config.yaml`, which a test keeps byte-identical to the template compiled into the binary.
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
