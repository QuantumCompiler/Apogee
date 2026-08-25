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

- **Language:** C++ (primarily — decided 2026-08-24). The C++ standard, build system (CMake presumed), dependency strategy, and test framework are open calls on the `cpp-project-skeleton` backlog item; satellite scripts (training drivers, MCP servers) may stay Python, as in Ommi.
- **Key dependencies:** the Anthropic, OpenAI, and Google LLM APIs (cloud backends, called directly over HTTPS); llama.cpp (local inference, planned in-process — likely a pinned submodule as in Ommi). HTTP client/server, JSON, and YAML libraries are open calls on the foundation items.
- **Platforms:** decided 2026-08-24 — Linux, macOS, and Windows, both ARM and x86: six targets (`linux-x64/arm64`, `macos-x64/arm64`, `windows-x64/arm64`), a wider matrix than Ommi's (which shipped no Windows and no Intel-Mac binaries). Each target builds natively on its own CI runner via `lib/scripts/cicd.sh --platform <target>`. Windows membership means the POSIX mechanisms named in backlog docs (forkpty PTY tests, tcflush, lsof assertions, 0600 modes, getpeername) need Windows equivalents or recorded per-item skips — the portability seam is owned by the `cpp-project-skeleton` item. Primary dev host is macOS.
- **Repository:** https://github.com/QuantumCompiler/Apogee (GitHub — making GitHub Releases the presumed distribution host; see the install-check-lifecycle backlog item).

---

## Invariants

No test-locked invariants exist yet — there is no code. But Apogee starts with **adopted design-time constraints** from Ommi (each earned there by a real bug or reversal; see [SPEC.md](SPEC.md) → Principles and Non-goals): interactive turns never open a listening socket — and local front-ends are pipes too: the GUI powers the CLI over stdin/stdout, while `serve` exists solely for server deployments answering remote REST clients; all install paths produce an identical layout; every capability reaches every surface through one shared core; secrets are `0600`, never logged, never returned over HTTP. Each is pinned as a **Core constraint** on the backlog items that own it, and earns a full `## ⚠` section here — rule, rationale, sub-rules, enforcement test — as the enforcing code and tests land.

---

## Codebase Map

| File / package | Contents |
|------|----------|
| `CLAUDE.md` (repo root) | Pointer to this docs system — the first thing an agent lands on. |
| `lib/documentation/assistant/` | These contributor docs (CLAUDE, SPEC, ROADMAP, MILESTONES, DEVELOPER). |
| `lib/documentation/backlog/` | The work queue — one document per pending item; priority-ordered index in its README. |
| `lib/scripts/` | Repo scripts. `cicd.sh` — the CI/CD entry point: builds the currently checked-out branch for any of the six release targets (`--platform linux-x64 … windows-arm64 | all`; builds what the host can natively, defers the rest to the CI matrix), with `--fresh` for a CI-style clean-room clone-and-build, `--test`, `--clean`, `--jobs`; the GitHub Actions matrix must invoke this same script per native runner so local and CI builds share one path. `cicd-completion.bash` — tab completion for its flags (source it from your shell rc). |

_TODO:_ no source code exists yet — add each source file/package here as it lands, dense enough that a contributor knows where things live before editing. The planned layout (C++ sources under `lib/src/`, tests under `lib/test/`, one package per concern mirroring Ommi's `config`/`harness`/`backends`/`agentloop`/`cli`/`httpserver` split) is sketched per item in the [`backlog/`](../backlog/README.md) documents' **Seam + files** sections.

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

- [ ] Tests for the new behavior pass, and existing tests stay green. (_TODO:_ record the test command once a test harness exists.)
- [ ] The affected docs are updated in the same change (see **Documentation and Status**).

_TODO:_ project-specific checklist items accrete here as invariants and conventions are established.

## Code Style

*(The two rules below are user decisions, 2026-08-24 — they apply from the first line of code and are lint-enforced via the `cpp-project-skeleton` backlog item.)*

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
