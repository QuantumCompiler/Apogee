# C++ project skeleton: CMake toolchain, test harness, CI baseline

**What / why.** Bootstrap the Apogee repo as a buildable, testable C++ project before any feature code: a root CMake build producing a single `apogee` binary, a strict library/CLI target split (tests link the library without the CLI), a wired-up unit-test framework runnable via ctest, the CLI root-command + subcommand self-registration scaffold and the CLI framework decision (moved here from complete-cli so config-engine's commands have a scaffold to register into), a source layout that mirrors Ommi's package map so every later backlog item has an obvious home, a dependency-acquisition strategy decided once, llama.cpp pinned as a read-only third-party library target (compile-proof only, no feature use), formatting/lint config, a thin Makefile-style convenience wrapper, and a minimal CI job. Nothing about the toolchain is decided yet, so this item forces those decisions up front where they are cheapest — every later item builds inside this skeleton, and Ommi's disciplines ('tests green before done', 'third-party modules are read-only', injectable seams from the first interface) start here or never.

**Core constraint(s).**
- Library-first layout: CLI and (later) HTTP server are thin faces over library targets — Ommi's parity guarantees depend on this split existing from day one
- Third-party code under third_party/ is never edited in-tree as part of Apogee work
- Tests must be runnable hermetically (no network, no models) — the injectable-seam discipline starts with the first interface, and retrofitting injectability in C++ is far more painful than in Go
- Single self-contained binary is the distribution goal — no runtime interpreter dependency on core inference paths
- Six-target portability from day one (linux/macos/windows × x64/arm64, user decision 2026-08-24): OS-specific mechanisms (process spawning, PTY/terminal control, file modes, socket peer checks) go behind a small platform seam (lib/src/platform/) rather than inline #ifdefs; POSIX-only test mechanisms named in later items (forkpty, lsof, tcflush) get per-platform equivalents or recorded per-item skips. CMake ships one preset per target, named exactly after the six targets — cicd.sh selects presets by those names
- Code-style contract from day one (user decision 2026-08-24, recorded in CLAUDE.md → Code Style): every class/module is a `.h`/`.cpp` pair when applicable, and ownership goes through smart pointers — no raw `new`/`delete`, no owning raw pointers (`unique_ptr` default, `shared_ptr` only for true shared ownership; C-API handles wrapped in `unique_ptr` with custom deleters at the boundary). This item wires the enforcement so the rule is lint-checked, not aspirational

**Seam + files.** CMakeLists.txt (root), cmake/ (presets, toolchain, dependency helpers), lib/src/cli/main.cpp (root command, subcommand self-registration, --config persistent flag, chosen CLI framework), lib/src/harness/ lib/src/backends/ lib/src/agentloop/ lib/src/cli/ lib/src/embedstore/ lib/src/httpserver/ lib/src/mcp/ (empty library-target stubs), lib/test/CMakeLists.txt + lib/test/smoke_test.cpp (per-package test dirs mirroring lib/src/), third_party/llama.cpp (pinned submodule/FetchContent, never edited), .clang-format, .clang-tidy (ownership/smart-pointer checks: cppcoreguidelines-owning-memory, cppcoreguidelines-no-malloc, modernize-make-unique/make-shared — scoped to lib/src, third_party excluded), Makefile (thin wrapper: build/test/install), CI config stub, lib/documentation/assistant/DEVELOPER.md seeded with the target map and how-to-add-a-dependency notes.

**Reference (Ommi).** Ommi's repo discipline: Go module layout (lib/cli, src/* leaf packages, external test/ packages), Makefile orchestration, `make test` gate, pinned modules/llama.cpp submodule. Deliberate divergence: CMake is the primary build system (Ommi's Makefile orchestrated `go build` + cmake); llama.cpp is linked as a library rather than compiled to embedded sidecar tools; Go's free cross-compilation is lost, so per-platform CI builders are a known future cost.

**Decisions made** (dated):
- 2026-08-24 — Planned from Ommi's documentation (parallel digest → two planning lenses → merge → adversarial verify). Position in the queue: Nothing can be built or tested before this exists; every open toolchain call bites here and nowhere else. Ommi's equivalent predates all milestones.
- 2026-08-24 — Code-style contract (user decision): `.h`/`.cpp` pairs when applicable; smart pointers only, never traditional owning raw pointers. Recorded in CLAUDE.md → Code Style; enforced here via .clang-tidy + the CI gate.
- 2026-08-24 — `lib/scripts/cicd.sh` already exists (user request; repo confirmed at github.com/QuantumCompiler/Apogee): it builds the currently checked-out branch, with --fresh for a clean-room clone-and-build. This item makes it build for real (CMakeLists + presets) and the CI job must INVOKE that script rather than duplicating build steps — one build path locally and in CI.
- 2026-08-24 — Six-target platform matrix (user decision): Linux/macOS/Windows, ARM + x86. cicd.sh takes `--platform` (each target + `all`), natively builds what the host can (macOS hosts: both Mac arches), and defers the rest to the GitHub Actions matrix — one native runner per target, each invoking this script. This item's CMake presets must carry the six target names the script selects by.

**Open calls:**
- [default: C++23 if the toolchain proves stable, else C++20] C++ standard — C++23 gives #embed for binary-embedded prompts/templates/allowlists (Ommi leaned on go:embed heavily; codegen is the C++20 fallback)
- [default: FetchContent, revisit if dependency count grows] Dependency strategy: FetchContent vs vcpkg vs Conan vs vendored amalgamations
- [default: Catch2] Test framework: Catch2 vs GoogleTest
- [default: nlohmann/json, libcurl, CLI11] Core library picks standardized HERE, once, for the whole project — JSON, HTTP client, CLI parser (+ the dynamic-completion protocol shape); downstream items consume these decisions and must not reopen them
- (decided 2026-08-24) Platform matrix: all six targets — linux/macos/windows × x64/arm64 (user decision; `lib/scripts/cicd.sh --platform` already speaks these names). Remaining [user] sub-call: which targets are merge-blocking in v0.1.0 vs allowed-failing while Windows portability catches up (recommend: macos-arm64 blocking first, the rest promoted as they stabilize)
- [default: keep a thin Makefile wrapper] Makefile wrapper vs CMake presets only

**Guardrail(s).** CI clean-tree build + ctest gate on every merge; the scripted fresh-clone check so the skeleton never silently rots; a link-layer assertion that the CLI target never leaks into library targets.

**Acceptance criteria:**
- [ ] Fresh clone: `cmake --preset default && cmake --build` produces an `apogee` binary that prints help/version and exits 0 on macOS arm64
- [ ] `ctest` builds and runs at least one real passing test in the chosen framework
- [ ] Library vs binary split exists and is enforced: test targets link the library without the CLI target
- [ ] llama.cpp is pinned and compiles as a linked library target (no API use yet); pin updates are deliberate events; its compile runs as a separate non-blocking CI job until llamacpp-backend lands, keeping the heavy Metal build off the per-push hot path
- [ ] CI runs build + tests on every push and fails on breakage; a scripted fresh-clone build check exists
- [ ] Documented one-command developer bootstrap works on a clean macOS arm64 checkout
- [ ] Subcommands self-register into the root command; bare `apogee` prints help (the CLI framework decision exercised end to end)
- [ ] clang-tidy runs in CI with the ownership checks enabled and fails the build on a raw `new`/`delete` or owning raw pointer (verified with a deliberately violating fixture, then removed); third_party/ is excluded from the lint scope

**Scope note.** earmarked for v0.1.0.
