# Install contract, apogee check doctor, completions (v0.1.0 release closer)

**What / why.** Make v0.1.0 shippable: define the ~/.apogee/ on-disk contract (config/, logs/{chats,main}/, models/ user-supplied only, embeddings/, cache/), a `cmake --install`/make-install path and a curl-able install script that produce byte-identical layouts, `apogee version`, `apogee uninstall` (removes the binary, ~/.apogee/, and installed completions — prompting before deleting user data such as models and chat logs; --yes skips), shell completions via a hidden `apogee __complete` verb + per-shell bash/zsh/fish stubs (powershell deliberately dropped from Ommi's set unless requested) (replicating cobra's dynamic protocol so backend names complete live), and `apogee check` — the doctor validating config parseability, backend references and role pointers, GGUF loadability (header read, not just presence — Ommi's 'sha256-healthy but unloadable' lesson), API-key presence per configured cloud backend, directory layout, and file modes. Check's rows are defined per backend type and grow with the type enum: when the CLI-backend items land (claude-cli, then the vendor family), check gains per-configured-backend rows validating the vendor binary resolves on PATH (or the configured path) and meets its minimum version — the CLI-type analog of the API-key presence row. Establishes the parity-in-one-commit rule in contributor docs now, while the parity surface is small (in-process llama.cpp already deleted Ommi's tools/lib/rpath machinery, so parity shrinks to binary + config template + data dirs). Ommi's dominant historical bug class was silent install drift; the same-commit parity rule and the directory-diff gate are process adopted verbatim. Self-update and the release pipeline are scoped by open call (they add the third parity file when the distribution host is decided).

**Core constraint(s).**
- Any asset landing under ~/.apogee/ is added to every install path plus check in the same commit (the parity rule, adopted from day one and written into contributor docs)
- Fail loud on install/parity, degrade gracefully at runtime
- No models bundled, nothing downloads without an explicit yes — permanent policy inherited verbatim
- check never edits config; dangling entries are reported with the exact command to run
- Per-install secrets (future key store) are parity-exempt but presence+mode checked
- Distributed binaries are verified by RUNNING them post-install, not by file presence

**Seam + files.** CMake install target, scripts/install.sh, lib/src/cli/source/commands/check.cpp, lib/src/cli/source/commands/version.cpp, completions/ (bash/zsh/fish stubs + the hidden __complete verb in lib/src/cli/source/main.cpp), lib/documentation/assistant/CLAUDE.md (parity rule + ~/.apogee layout contract recorded), lib/src/cli/tests/commands/check_test.cpp + a scripted install-parity diff check in CI.

**Reference (Ommi).** lib/cli/Makefile ↔ install.sh ↔ update.go ↔ check.go four-way parity (the v0.1.5 foundation; every early install bug was silent drift), check --fix discipline, completion machinery, Milestone Q. Divergence: no cpp-tool sidecars or rpath staging to install (in-process llama.cpp), so the parity surface shrinks; C++ needs per-platform native builders instead of Go cross-compilation; self-update joins later as the third parity file.

**Decisions made** (dated):
- 2026-08-31 — The **developer half of `make install` landed early**, ahead of this item: `install(TARGETS apogee RUNTIME …)` in `lib/src/cli/source/CMakeLists.txt` plus an `install` target in `lib/src/cli/Makefile` (`PREFIX ?= $HOME/.local`, so no sudo). It installs the binary and nothing else — **no `~/.apogee/` asset is seeded**, precisely so the parity rule is not half-satisfied before `check` exists to verify it. This item still owns the whole contract: the data-directory layout, the config template, completions, `install.sh`, `uninstall`, and the directory-diff gate. When they land they *extend* that existing rule; they do not introduce the install path.
- 2026-08-24 — Planned from Ommi's documentation (parallel digest → two planning lenses → merge → adversarial verify). Position in the queue: The release closer: it assumes every other earmarked item is complete (its gate names only the longest chain). Ommi v0.1.5–v0.1.7 were exactly this foundation; shipping v0.1.0 without the parity discipline reproduces Ommi's entire early bug class.

**Open calls:**
- [user] Distribution host and release pipeline shape — the repo is confirmed at github.com/QuantumCompiler/Apogee (2026-08-24), making GitHub Releases + GitHub Actions the strong default; confirm, and decide whether the future self-update item lands here or immediately after. The Actions workflow must invoke `lib/scripts/cicd.sh` (one build path locally and in CI)
- [user] Self-update in v0.1.0 or deferred (plan default: defer; Ommi's parity contract eventually needs it as the third parity file)
- (decided 2026-08-24) Release targets: all six — linux/macos/windows × x64/arm64. Remaining [user] sub-calls: macOS codesigning/notarization + quarantine-xattr handling; the Windows install story (install.sh is bash — Windows needs its own path: PowerShell installer, zip-plus-instructions, or winget later — plus a completions answer there); and whether all six ship binaries from the first tagged release or Windows joins once its portability work lands
- [default: with self-update] Background update notifier timing (TTY-only, 24h cache, subcommand skip-list rules port wholesale)

**Guardrail(s).** Automated install-parity directory-diff in CI on every release candidate; check exercised against a matrix of deliberately broken installs; completion protocol smoke test (`apogee __complete complete --model ""`).

**Acceptance criteria:**
- [ ] Fresh `make install` and fresh script install produce directory listings whose sorted diff is empty (Ommi's pre-tag gate, automated in CI) — `make install` exists as of 2026-08-31 and installs the binary only; this criterion is about the layout both paths must produce once the data-directory assets are defined here
- [ ] `apogee check` reports zero failures on a fresh install with no models and no keys (degraded-but-valid), and correctly flags a dangling model_path, a missing key on a configured cloud backend, an unloadable GGUF, and wrong file modes
- [ ] check never modifies config — it prints the exact remediation command; --fix-class remediations act only on the local install
- [ ] Dynamic shell completion returns live backend names via the __complete protocol after rebuild
- [ ] Installer downloads no models and executes no remote code; `apogee version` works
- [ ] `apogee uninstall` removes binary + data dir + completions, prompts before user data, honors --yes; a fresh install after uninstall passes check

**Scope note.** earmarked for v0.1.0. The release closer: it assumes the complete v0.1.0 set, which after the 2026-08-26 grooming split means items 1–6 and 7a/7b/7c (all shipped), plus **8** ([openai-google-backends.md](openai-google-backends.md)) and **9** ([llamacpp-backend.md](llamacpp-backend.md)).
