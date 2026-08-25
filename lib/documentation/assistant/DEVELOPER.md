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
        │   ├── Makefile         — Thin wrapper (build/test/lint/format/clean/fresh)
        │   ├── .clang-format    — Formatting rules (make format / make format-check)
        │   ├── .clang-tidy      — Static analysis, incl. the smart-pointer ownership gate
        │   ├── cmake/
        │   │   ├── ApogeeDependencies.cmake — FetchContent declarations + how to add a dependency
        │   │   ├── ApogeeLinkPolicy.cmake   — Configure-time assertion that nothing links the CLI executable
        │   │   └── ApogeeWarnings.cmake     — Shared warning flags for first-party targets
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
| `commands/` | The CLI scaffold: the `Command` interface, the registry, the root command, and the built-in commands. |
| `harness/` | *Reserved* — LLMProvider interface, message IR, router (`harness-core` item). |
| `backends/` | *Reserved* — one provider implementation per model source (`anthropic-backend` onward). |
| `agentloop/` | *Reserved* — the shared model→tool→model loop and its Reporter seam (`agentloop-core` item). |
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

**Adding a subcommand** touches exactly two places: the command's own `.h`/`.cpp` pair, and one `registry.add(...)` line in `default_registry()`. It then appears in `apogee --help` with no other edit — `main.cpp` never grows.

A command signals failure by throwing `CLI::RuntimeError(code)`; `RootCommand::run` turns that into the process exit code. Read `RootContext` inside your callback, never at bind time — parsing has not happened yet at bind time.

### The platform seam

`platform/` exposes the build's OS, architecture, and release-target name. The target name is one of the exact six strings shared by `cicd.sh --platform`, the CMake presets, the CI matrix, and the binary itself; `tests/platform/platform_test.cpp` is what keeps them one vocabulary.

Mechanisms that land here as later items need them: process spawning (vendor-CLI backends), PTY and terminal control (the chat UX layer), file-mode enforcement (the `0600` secrets rule), and socket peer checks (the admin plane). Each arrives as a declaration in `platform.h` with one definition per platform. **Where a mechanism has no Windows equivalent, the owning item records the skip** rather than leaving the gap silent.

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
| `support/fake_command.h/.cpp` | A `Command` defined in test code — the injectable seam, exercised. |

Beyond those, `tests/CMakeLists.txt` registers `cli.*` ctest cases that run the built `apogee` binary as a subprocess, covering the contract as a user meets it (bare invocation prints help and exits 0; `--version`; unknown subcommand fails). Running a target is not linking it, so the link policy still holds.

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
| `make format` / `make format-check` | Apply / verify formatting across `source/` and `tests/`. |
| `make lint` | clang-tidy over `source/` and `tests/`, including the ownership gate. |
| `make llama` | Build with the pinned llama.cpp target enabled (slow). |
| `make clean` / `make fresh` | Drop a build dir / clean-room clone-and-build. |

`make lint` configures its own `build/lint` directory using the compiler from clang-tidy's own directory. That is not incidental: on macOS the normal build uses Apple clang, whose libc++ headers Homebrew's clang-tidy cannot find, and every file fails to parse with a misleading `'cstddef' file not found`.

### Options

| Option | Default | Meaning |
|---|---|---|
| `APOGEE_BUILD_TESTS` | `ON` | Build the Catch2 suite. |
| `APOGEE_ENABLE_LLAMA` | `OFF` | Configure and build the pinned llama.cpp target. Heavy; a dedicated non-blocking CI job carries it until `llamacpp-backend` lands. |

---

## Dependencies

**Strategy: FetchContent**, every declaration carrying `FIND_PACKAGE_ARGS` so a system-installed copy wins and the network fetch is the fallback, and `SYSTEM` so third-party headers are not held to Apogee's warning bar. All declarations live in one file: [`lib/src/cli/cmake/ApogeeDependencies.cmake`](../../src/cli/cmake/ApogeeDependencies.cmake).

| Concern | Pick | Status |
|---|---|---|
| JSON | nlohmann/json `v3.11.3` | Wired |
| CLI parsing + completions | CLI11 `v2.4.2` | Wired |
| Tests | Catch2 `v3.7.1` | Wired (only when `APOGEE_BUILD_TESTS`) |
| HTTP client | **libcurl** | Standing pick, **not wired yet** — it arrives with `anthropic-backend`, which owns `find_package(CURL)` and the platform TLS backends. Wiring it now would break the build on hosts with no curl development package, for no present gain. |
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
