# third_party/

Vendored and pinned external code for the **CLI application**. **Nothing here is edited in-tree.**

That rule is a Core constraint (see [CLAUDE.md](../../../documentation/assistant/CLAUDE.md) → Invariants). A local edit to a pinned dependency is invisible to everyone reading the pin, survives no update, and turns every future version bump into an archaeology exercise. When a third-party library is wrong for us:

- **Fix it upstream** and move the pin forward, or
- **Wrap it** — put the adaptation in a first-party boundary class under `../source/`, where it is reviewed, tested, and owned.

## What lives here

| Dependency | Acquisition | Notes |
|---|---|---|
| llama.cpp | `FetchContent`, pinned to release `b11151` (`bd4f514d`, 2026-09-23; `549b9d84` before) | Local inference, linked **in-process** rather than spawned per turn. Off by default (`-DAPOGEE_ENABLE_LLAMA=ON` to build); compile-proofed by its own non-blocking CI job until the `llamacpp-backend` backlog item consumes its API. |
| SQLite | `FetchContent`, the `3530400` amalgamation, `URL_HASH` | The chunk store and its FTS5 index; fetched rather than found because FTS5 is a compile-time flag ([`CMakeLists.txt`](CMakeLists.txt) says why). |
| tree-sitter and its grammars | [`tree-sitter/CMakeLists.txt`](tree-sitter/CMakeLists.txt): each an archive of an **exact commit**, `URL_HASH`-checked, built as plain C11 static libraries — never upstream's CMake, never found on the system | The code graph's deterministic parser (27k), in every build — the no-llama developer build and the unit-test runners included, since the code graph is model-free by design and the suite pins its extraction. One pin per repository, below. |

### The tree-sitter roster (27k, pinned 2026-10-04)

Every pin is the commit its release tag names (checked against `git ls-remote` when pinned), and every one is **MIT**. The grammars' ABI versions (14 or 15, each `parser.c`'s `LANGUAGE_VERSION`) are all within what the runtime loads (13–15). The extractor version in `source/business/graph/code_languages.cpp` names each grammar's tag, so a moved pin re-parses that language's files on the next `graph update` instead of mixing two grammars' facts — and the golden fixture of every language under `tests/fixtures/code_graph/` moves with it, regenerated in the same change.

| Repository | Tag | Commit | Licence | Grammar(s) | Code + tables in the binary |
|---|---|---|---|---|---|
| `tree-sitter/tree-sitter` (the runtime, `lib/src/lib.c` amalgamated) | `v0.27.0` | `6070dbfefd326bd735e5683eb128cc1b57dad0c0` | MIT | — | 0.13 MB |
| `tree-sitter/tree-sitter-c` | `v0.24.2` | `b780e47fc780ddc8da13afa35a3f4ed5c157823d` | MIT | `c` | 0.60 MB |
| `tree-sitter/tree-sitter-cpp` | `v0.23.4` | `f41e1a044c8a84ea9fa8577fdd2eab92ec96de02` | MIT | `cpp` | 3.26 MB |
| `tree-sitter/tree-sitter-python` | `v0.25.0` | `293fdc02038ee2bf0e2e206711b69c90ac0d413f` | MIT | `python` | 0.43 MB |
| `tree-sitter/tree-sitter-javascript` | `v0.25.0` | `44c892e0be055ac465d5eeddae6d3e194424e7de` | MIT | `javascript` (JSX included) | 0.39 MB |
| `tree-sitter/tree-sitter-typescript` | `v0.23.2` | `f975a621f4e7f532fe322e13c4f79495e0a7b2e7` | MIT | `typescript`, `tsx` | 1.34 + 1.37 MB |
| `tree-sitter/tree-sitter-go` | `v0.25.0` | `1547678a9da59885853f5f5cc8a99cc203fa2e2c` | MIT | `go` | 0.21 MB |
| `tree-sitter/tree-sitter-rust` | `v0.24.2` | `77a3747266f4d621d0757825e6b11edcbf991ca5` | MIT | `rust` | 1.06 MB |
| `tree-sitter/tree-sitter-java` | `v0.23.5` | `94703d5a6bed02b98e438d7cad1136c01a60ba2c` | MIT | `java` | 0.39 MB |
| `tree-sitter/tree-sitter-c-sharp` | `v0.23.5` | `cac6d5fb595f5811a076336682d5d595ac1c9e85` | MIT | `csharp` | 5.08 MB |
| `tree-sitter/tree-sitter-ruby` | `v0.23.1` | `71bd32fb7607035768799732addba884a37a6210` | MIT | `ruby` | 2.00 MB |
| `tree-sitter/tree-sitter-bash` | `v0.25.1` | `a06c2e4415e9bc0346c6b86d401879ffb44058f7` | MIT | `bash` | 1.30 MB |

**What they cost** (measured 2026-10-04 on macOS arm64, Apple clang, `RelWithDebInfo`): about 17.6 MB of code and parse tables in the executable — the stripped binary went from 21.4 MB to 39.1 MB — but only **+1.4 MB compressed** (gzip -9: 8.50 → 9.87 MB), since the tables compress about thirteenfold; the build compiles them in about 8 s of CPU time one after another (under 1% of a no-llama build's compile work), a first configure downloads about 7 MB of archives, and MinGW-w64 GCC 16 builds the same sources warning-free in about 11 s serially. Out of the lint and format scope (only `source/` and `tests/` are), compiled with warnings off, headers `SYSTEM`.

Everything else the CLI depends on is fetched by [`../cmake/ApogeeDependencies.cmake`](../cmake/ApogeeDependencies.cmake) rather than living here — see that file for the dependency strategy and how to add one.

## Updating a pin

A pin bump is a deliberate, reviewable event, not a drift. Give it its own commit, and say in the message what changed upstream and why Apogee wants it.
