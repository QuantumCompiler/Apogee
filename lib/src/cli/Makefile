# Thin convenience wrapper for the CLI application's build. The real build
# lives in CMake beside this file, and the repo-wide CI/CD entry point is
# lib/scripts/cicd.sh -- which CI invokes directly, so there is exactly one
# build path locally and on every runner. Nothing here may become load-bearing:
# if a step matters, it belongs in cicd.sh or CMake.
#
# Run from this directory (lib/src/cli), or from anywhere with:
#     make -C lib/src/cli <target>

SHELL := /bin/bash

PRESET ?= default
BUILD_DIR := build/$(PRESET)

# Where `make install` puts the binary. A user-level prefix by default: it needs
# no sudo, so the everyday developer install never runs a build as root. The
# system-wide install is `make install PREFIX=/usr/local`.
PREFIX ?= $(HOME)/.local
JOBS ?= $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)

# Repo root is three levels up from lib/src/cli.
CICD := ../../scripts/cicd.sh

# Homebrew keeps LLVM off the default PATH on macOS, and Apple's toolchain
# ships no clang-tidy at all, so look there before giving up.
CLANG_FORMAT ?= $(shell command -v clang-format 2>/dev/null || echo /opt/homebrew/opt/llvm/bin/clang-format)
CLANG_TIDY   ?= $(shell command -v clang-tidy   2>/dev/null || echo /opt/homebrew/opt/llvm/bin/clang-tidy)
# The compiler that produces the lint build's compile database. Taken from
# clang-tidy's OWN directory on purpose: the two must be the same toolchain, or
# clang-tidy cannot find the standard library the database compiles against.
LINT_CXX     ?= $(dir $(CLANG_TIDY))clang++

# First-party code only: source/ and tests/. third_party/ and build/ are never
# formatted or linted.
SOURCES := $(shell find source tests \( -name '*.cpp' -o -name '*.h' \) 2>/dev/null)

.PHONY: help build install install-completions install-zsh-completion test clean fresh format format-check lint no-llama all release release-check pr-ci pr-ci-clean

help:
	@echo "Apogee CLI — build wrapper (repo-wide entry point: lib/scripts/cicd.sh)"
	@echo ""
	@echo "Usage: make [target] [PRESET=<name>] [JOBS=N] [PREFIX=<dir>]"
	@echo "       make -C lib/src/cli [target]   (from the repo root)"
	@echo ""
	@echo "Targets:"
	@echo "  build         Configure + build (preset: $(PRESET))"
	@echo "  install       Build, then install the binary to $(PREFIX)/bin"
	@echo "  test          Build, then run the full ctest suite"
	@echo "  lint          clang-tidy over source/ and tests/"
	@echo "  format        Rewrite source/ and tests/ in the project style"
	@echo "  format-check  Fail if anything in source/ or tests/ is unformatted"
	@echo "  no-llama      The fast developer build: everything except llama.cpp (ON by default)"
	@echo "  clean         Remove this preset's build directory"
	@echo "  fresh         Clean-room clone-and-build of the current branch"
	@echo "  pr-ci         Rehearse the CI run a pull request into stable starts, on this host:"
	@echo "                  the test merge, version bump, clone, unit tests, build + package"
	@echo "                  (UNCOMMITTED=1 includes the working tree; BASE=<ref> to merge elsewhere)"
	@echo "  pr-ci-clean   Remove pr-ci's worktrees, builds and logs"
	@echo ""
	@echo "Cutting a release (DEVELOPER.md -> Cutting a release):"
	@echo "  release-check Preflight a release -- checks only, pushes nothing"
	@echo "  release       Preflight, then tag $(RELEASE_BRANCH) and push; the push cuts it"
	@echo "                  make release VERSION=0.1.1"
	@echo ""
	@echo "Presets are the five release targets plus 'default' (host native):"
	@echo "  linux-x64  linux-arm64  macos-arm64  windows-x64  windows-arm64"
	@echo ""
	@echo "One-command bootstrap on a clean checkout:  make test"

all: build

build:
	cmake --preset $(PRESET)
	cmake --build $(BUILD_DIR) -j $(JOBS)

# Builds first so `make install` on a clean checkout is one command, and so an
# install can never quietly ship a stale binary from an earlier edit. The copy
# itself is CMake's -- this recipe adds no steps of its own, only the PATH note,
# because an installed binary the shell cannot find is the one failure mode a
# user-level prefix introduces.
# Installs the binary, its completions, and then asks the binary to create its
# own data directory and verify it.
#
# That last step is the parity rule in one line: this target does NOT know what
# `~/.apogee/` contains. `apogee check --fix` creates it from the single
# declaration in source/harness/layout.h, and install.sh / install.ps1 end with
# the same call -- so no install path holds a list of directories that could go
# stale against the others. Ommi's dominant early bug class was exactly that
# drift; the CI parity gate diffs two installs to keep it closed.
install: build
	@cmake --install $(BUILD_DIR) --prefix "$(PREFIX)" --component apogee
	@$(MAKE) --no-print-directory install-completions PREFIX="$(PREFIX)"
	@echo ""
	@"$(PREFIX)/bin/apogee" check --fix
	@case ":$$PATH:" in \
		*":$(PREFIX)/bin:"*) ;; \
		*) echo ""; \
		   echo "note: $(PREFIX)/bin is not on your PATH -- add it to run 'apogee' by name:"; \
		   echo "      export PATH=\"$(PREFIX)/bin:\$$PATH\"" ;; \
	esac

# Completions go where each shell already looks. A shell the user does not run
# gets nothing -- creating its config directory would be litter that `apogee
# uninstall` then has to guess the ownership of.
install-completions:
	@mkdir -p "$(HOME)/.local/share/bash-completion/completions"
	@install -m 0644 completions/apogee.bash "$(HOME)/.local/share/bash-completion/completions/apogee"
	@mkdir -p "$(HOME)/.config/fish/completions"
	@install -m 0644 completions/apogee.fish "$(HOME)/.config/fish/completions/apogee.fish"
	@$(MAKE) --no-print-directory install-zsh-completion
	@echo "installed shell completions (bash, zsh, fish)"

# zsh only loads completions from a directory on its `fpath`, which is a
# per-user setting -- so ask zsh where it looks, and cross that against
# directories that are conventionally OURS to write into.
#
# Both halves are needed, and each was learned the hard way on the same machine
# (2026-09-06). Installing to a fixed ~/.local/share/zsh/site-functions wrote a
# file zsh never reads, so completion silently did nothing. Then taking the
# first writable fpath entry under $HOME put our file inside
# ~/.oh-my-zsh/plugins/vscode -- a directory a framework owns and rewrites.
# A completion file belongs in a user completion directory or nowhere.
install-zsh-completion:
	@fpath_dirs="$$(command -v zsh >/dev/null 2>&1 && zsh -i -c 'print -l $$fpath' 2>/dev/null)"; \
	target=""; \
	for dir in "$(HOME)/.zfunc" "$(HOME)/.oh-my-zsh/completions" "$(HOME)/.local/share/zsh/site-functions"; do \
	    if printf '%s\n' "$$fpath_dirs" | grep -qx "$$dir" && [ -d "$$dir" ] && [ -w "$$dir" ]; then \
	        target="$$dir"; break; \
	    fi; \
	done; \
	if [ -n "$$target" ]; then \
	    install -m 0644 completions/_apogee "$$target/_apogee"; \
	else \
	    target="$(HOME)/.local/share/zsh/site-functions"; \
	    mkdir -p "$$target"; \
	    install -m 0644 completions/_apogee "$$target/_apogee"; \
	    echo ""; \
	    echo "note: no writable directory on zsh's fpath was found, so the zsh"; \
	    echo "      completion went to $$target, which zsh will NOT read until"; \
	    echo "      you add it. In ~/.zshrc, before compinit:"; \
	    echo "        fpath+=$$target"; \
	fi

test:
	@$(CICD) --platform $$(uname -s | tr '[:upper:]' '[:lower:]' | sed 's/darwin/macos/')-$$(uname -m | sed 's/aarch64/arm64/;s/x86_64/x64/;s/amd64/x64/') --test --jobs $(JOBS)

# Rehearse the CI run a pull request into `stable` starts: GitHub's test merge
# of this branch into stable, then ci.yml's pull-request jobs on it -- version
# bump, clone llama.cpp, unit tests, build and package -- for this host's
# target, through the same scripts the workflow calls. Thin by the rule above:
# the rehearsal is lib/scripts/pr-ci.sh. BASE=<ref> merges into another base;
# UNCOMMITTED=1 includes the working tree's changes, which a PR would not.
#
#     make pr-ci                 # exits non-zero if CI would fail
#     make pr-ci UNCOMMITTED=1   # as if everything here were committed
#     make pr-ci-clean           # its worktrees are gigabytes once built
PR_CI := ../../scripts/pr-ci.sh

pr-ci:
	@$(PR_CI) --jobs $(JOBS) $(if $(BASE),--base $(BASE)) $(if $(UNCOMMITTED),--uncommitted)

pr-ci-clean:
	@$(PR_CI) --clean

# The fast developer build: everything except llama.cpp, which is ON by
# default and is most of the compile time. The llamacpp backend then answers
# "not built in"; every other package builds and tests as usual.
no-llama:
	cmake --preset $(PRESET) -DAPOGEE_ENABLE_LLAMA=OFF
	cmake --build $(BUILD_DIR) -j $(JOBS)

clean:
	rm -rf $(BUILD_DIR)

fresh:
	@$(CICD) --fresh --test

format:
	@$(CLANG_FORMAT) -i $(SOURCES)
	@echo "formatted $(words $(SOURCES)) files"

format-check:
	@$(CLANG_FORMAT) --dry-run --Werror $(SOURCES) && echo "format OK"

# clang-tidy resolves headers using the compile database's compiler, so lint
# gets its OWN build directory configured with the same clang the tidy binary
# was built from. On macOS that matters: the default build uses Apple clang,
# whose libc++ headers Homebrew's clang-tidy cannot find, and every file fails
# to parse with a misleading "'cstddef' file not found".
#
# Only source/ and tests/ files are passed in, and .clang-tidy's
# HeaderFilterRegex keeps third_party/ and build/_deps headers out of the
# diagnosis scope.
lint: LINT_DIR := build/lint
lint:
	@cmake -S . -B $(LINT_DIR) \
		-DCMAKE_CXX_COMPILER=$(LINT_CXX) \
		-DCMAKE_EXPORT_COMPILE_COMMANDS=ON >/dev/null
	@$(CLANG_TIDY) -p $(LINT_DIR) --quiet $(filter %.cpp,$(SOURCES))
	@echo "lint OK"

# ---------------------------------------------------------------------------
# Cutting a release
#
# Thin by the same rule as the rest of this file: the tag this pushes runs
# .github/workflows/release.yml, which builds and publishes on GitHub's
# runners, and nothing here builds, packages or uploads anything.
#
# Since 2026-09-22 this is the ESCAPE HATCH, not the everyday path: a merge
# into stable publishes the release VERSION names (the repository's top-level
# file since 2026-09-25; the CLI keeps its own version in CMakeLists.txt) --
# from the merged pull request's own CI archives, or the latest release's when
# the CLI is unchanged (ci.yml's `tag and release`) -- so the normal way to cut
# a release is to bump VERSION and merge the PR. This target is for a re-cut, for a merge whose own archives could not be
# released (the tree check refused them, or they expired), or for tagging a
# commit that is not the tip of a merge. It remains worth keeping because the
# preflight below is the same set of mistakes either path can make, and it
# catches them before the remote hears about them.
#
#     make release-check VERSION=0.1.1   # preflight only, pushes nothing
#     make release VERSION=0.1.1         # preflight, confirm, tag, push
#
# The checks exist because each one is a real way to ship a broken release:
# a tag that disagrees with VERSION publishes a release under a name the
# repository does not give it, and a BRANCH sharing a
# tag's name makes every later `git push origin vX.Y.Z` fail with
# "src refspec ... matches more than one".
#
# The suite is deliberately NOT run here. Since 2026-09-22 CI runs it per
# platform, so a tag cut from a merged branch has already been tested; making
# this target rebuild and re-run it would only delay the push. `make test`
# remains the gate for anything not yet through CI, and the confirmation
# prompt says so rather than pretending otherwise.
# ---------------------------------------------------------------------------

REMOTE         ?= origin
RELEASE_BRANCH ?= stable
# CONFIRM=yes skips the interactive prompt, for a scripted release.
CONFIRM        ?=

# Accept 0.1.1 and v0.1.1 alike: the tag always carries the v, the CMake
# project() call never does.
VERSION_NUM := $(patsubst v%,%,$(VERSION))
TAG         := v$(VERSION_NUM)

# `git branch --show-current` and not `git rev-parse --abbrev-ref HEAD`: the
# latter disambiguates to "heads/<name>" the moment a tag shares the branch's
# name, which is precisely the situation the tag check below exists to catch.
release-check:
	@set -euo pipefail; \
	if [ -z "$(VERSION_NUM)" ]; then \
	    echo "error: VERSION is required -- e.g. make release VERSION=0.1.1" >&2; exit 1; fi; \
	if ! printf '%s' "$(VERSION_NUM)" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$$'; then \
	    echo "error: VERSION must be x.y.z (got '$(VERSION_NUM)')" >&2; exit 1; fi; \
	branch="$$(git branch --show-current)"; \
	if [ -z "$$branch" ]; then \
	    echo "error: HEAD is detached; a release is cut from '$(RELEASE_BRANCH)'" >&2; exit 1; fi; \
	if [ "$$branch" != "$(RELEASE_BRANCH)" ]; then \
	    echo "error: on '$$branch'; a release is cut from '$(RELEASE_BRANCH)'" >&2; \
	    echo "       git checkout $(RELEASE_BRANCH) && git pull" >&2; exit 1; fi; \
	if ! git diff --quiet || ! git diff --cached --quiet; then \
	    echo "error: working tree is dirty; commit or stash before tagging" >&2; exit 1; fi; \
	git fetch --quiet $(REMOTE) $(RELEASE_BRANCH); \
	if [ "$$(git rev-parse HEAD)" != "$$(git rev-parse $(REMOTE)/$(RELEASE_BRANCH))" ]; then \
	    echo "error: $(RELEASE_BRANCH) is not in sync with $(REMOTE)/$(RELEASE_BRANCH)" >&2; exit 1; fi; \
	release_version="$$(tr -d ' \r\n' < "$$(git rev-parse --show-toplevel)/VERSION")"; \
	if [ "$$release_version" != "$(VERSION_NUM)" ]; then \
	    echo "error: VERSION says $$release_version, but the tag would be $(TAG)" >&2; \
	    echo "       bump VERSION to $(VERSION_NUM) and commit it first" >&2; exit 1; fi; \
	if git show-ref --verify --quiet "refs/heads/$(TAG)"; then \
	    echo "error: a BRANCH named $(TAG) exists; git cannot then resolve the tag by name" >&2; \
	    echo "       git branch -D $(TAG)" >&2; exit 1; fi; \
	if git rev-parse -q --verify "refs/tags/$(TAG)" >/dev/null; then \
	    echo "error: tag $(TAG) already exists locally (git tag -d $(TAG) to redo it)" >&2; exit 1; fi; \
	if git ls-remote --exit-code --tags $(REMOTE) "refs/tags/$(TAG)" >/dev/null 2>&1; then \
	    echo "error: tag $(TAG) is already on $(REMOTE) -- that release is cut" >&2; exit 1; fi; \
	echo "preflight OK"; \
	echo "  branch   $(RELEASE_BRANCH) @ $$(git rev-parse --short HEAD), in sync with $(REMOTE)"; \
	echo "  version  VERSION $$release_version == $(TAG)"; \
	echo "  tag      $(TAG) is free locally and on $(REMOTE)"

release: release-check
	@set -euo pipefail; \
	if [ "$(CONFIRM)" != "yes" ]; then \
	    echo ""; \
	    echo "Pushing $(TAG) to $(REMOTE) builds all five targets and publishes a PUBLIC"; \
	    echo "GitHub release. The test suite is your gate, not the pipeline's -- run"; \
	    echo "'make test' first if you have not."; \
	    printf "Push %s? [y/N] " "$(TAG)"; \
	    read -r reply </dev/tty; \
	    case "$$reply" in y|Y|yes|YES) ;; *) echo "aborted; nothing was tagged or pushed"; exit 1 ;; esac; \
	fi; \
	git tag -a "$(TAG)" -m "Apogee $(TAG)"; \
	git push "$(REMOTE)" "refs/tags/$(TAG)"; \
	echo ""; \
	echo "pushed $(TAG) -- the Release workflow is now running."; \
	if command -v gh >/dev/null 2>&1; then \
	    echo "  gh run watch \$$(gh run list --workflow=Release --limit 1 --json databaseId -q '.[0].databaseId')"; \
	else \
	    url="$$(git remote get-url $(REMOTE))"; \
	    case "$$url" in \
	        *github.com*) echo "  https://github.com/$$(printf '%s' "$$url" | sed -e 's#.*github.com[:/]##' -e 's#\.git$$##')/actions" ;; \
	        *)            echo "  watch it on the repository's Actions tab" ;; \
	    esac; \
	fi; \
	echo ""; \
	echo "Next: open the branch for the release after this one, from $(RELEASE_BRANCH)."
