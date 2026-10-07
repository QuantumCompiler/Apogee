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
# The channel's installed binary: `install` ends in its `check --fix`,
# `uninstall` runs its own `uninstall`, and `reinstall` resets with it.
APOGEE_BIN ?= $(PREFIX)/bin/$(APOGEE_NAME)

# The install channel (M10, DEVELOPER.md -> Install channels): which data
# directory the installed binary owns -- release ~/.apogee, dev ~/.apogee-dev,
# test ~/.apogee-test -- baked in when it is built, and the name it installs
# under beside the others: apogee, apogee-dev, apogee-test. `make` has no long
# options, so `MODE=dev` is how this file spells the binary's `--dev`.
MODE ?= release
CHANNELS := release dev test
ifneq ($(words $(MODE)),1)
$(error MODE must be one of: $(CHANNELS) (got '$(MODE)'))
endif
ifeq ($(filter $(CHANNELS),$(MODE)),)
$(error MODE must be one of: $(CHANNELS) (got '$(MODE)'))
endif
APOGEE_NAME := $(if $(filter release,$(MODE)),apogee,apogee-$(MODE))

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

.PHONY: help build install reinstall install-completions install-zsh-completion test clean fresh format format-check lint no-llama all release release-check pr-ci pr-ci-clean

help:
	@echo "Apogee CLI — build wrapper (repo-wide entry point: lib/scripts/cicd.sh)"
	@echo ""
	@echo "Usage: make [target] [PRESET=<name>] [JOBS=N] [PREFIX=<dir>] [MODE=<channel>]"
	@echo "       make -C lib/src/cli [target]   (from the repo root)"
	@echo ""
	@echo "Targets:"
	@echo "  build         Configure + build (preset: $(PRESET), channel: $(MODE))"
	@echo "  install       Build, then install the binary to $(PREFIX)/bin"
	@echo "  reinstall     Start over from source: a fresh build, install, then 'apogee reset'"
	@echo "                  on the installed binary -- it shows the plan and waits for 'yes'."
	@echo "                  KEEP=\"<row> ...\" keeps those rows of the data directory and ONLY"
	@echo "                  those. A row is one of the layout's top-level directories --"
	@echo "                  'apogee reset --help' lists them, 'apogee reset --keep <TAB>'"
	@echo "                  completes them. Bare 'make reinstall' keeps nothing."
	@echo "                  make reinstall KEEP=models"
	@echo "  uninstall     Run the installed binary's own 'uninstall' (it shows the plan and asks)"
	@echo ""
	@echo "Install channels (MODE=, default release) -- each its own binary and data directory:"
	@echo "  release       apogee        ~/.apogee        with shell completions"
	@echo "  dev           apogee-dev    ~/.apogee-dev"
	@echo "  test          apogee-test   ~/.apogee-test"
	@echo "                  make install MODE=dev   /   make uninstall MODE=dev"
	@echo "                  make reinstall MODE=dev KEEP=models"
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
	@echo "  release-check Preflight a re-cut -- checks only, dispatches nothing"
	@echo "  release       Preflight, then dispatch CI's re-cut: rebuild $(RELEASE_BRANCH) and publish"
	@echo "                  make release VERSION=0.1.1"
	@echo ""
	@echo "Presets are the five release targets plus 'default' (host native):"
	@echo "  linux-x64  linux-arm64  macos-arm64  windows-x64  windows-arm64"
	@echo ""
	@echo "One-command bootstrap on a clean checkout:  make test"

all: build

# The channel is passed on every configure, release included: CMake caches it,
# so a build that left it to the cache would quietly stay whatever channel
# the build directory last saw.
build:
	cmake --preset $(PRESET) -DAPOGEE_CHANNEL=$(MODE)
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
# declaration in source/data/contracts/layout.h, and install.sh / install.ps1 end with
# the same call -- so no install path holds a list of directories that could go
# stale against the others. That drift is the bug class this guards against;
# the CI parity gate diffs two installs to keep it closed.
#
# MODE=dev or MODE=test installs that channel's binary beside the release one
# and seeds that channel's root; the completion stubs stay release-only, since
# they call `apogee` by name.
install: build
	@cmake --install $(BUILD_DIR) --prefix "$(PREFIX)" --component apogee
ifeq ($(MODE),release)
	@$(MAKE) --no-print-directory install-completions PREFIX="$(PREFIX)"
else
	@echo "no shell completions for the $(MODE) channel: the stubs call 'apogee' by name"
endif
	@echo ""
	@"$(APOGEE_BIN)" check --fix
	@case ":$$PATH:" in \
		*":$(PREFIX)/bin:"*) ;; \
		*) echo ""; \
		   echo "note: $(PREFIX)/bin is not on your PATH -- add it to run '$(APOGEE_NAME)' by name:"; \
		   echo "      export PATH=\"$(PREFIX)/bin:\$$PATH\"" ;; \
	esac

# Starting over from source (M9): a fresh build -- every object recompiled,
# the fetched dependencies kept -- then `install`, then `apogee reset` on the
# binary just installed. Each word of KEEP is forwarded as `--keep <word>`, and
# only the rows named are kept: bare, it keeps nothing, which the plan shows
# and the prompt confirms before anything is removed.
#
#     make reinstall                        # a fresh install, nothing kept
#     make reinstall KEEP=models            # ...the models kept
#     make reinstall KEEP="models sessions" # ...and the chats
#
# This file does not know the rows. They are the layout's one declaration
# (source/data/contracts/layout.h): the binary checks every word, refuses one
# the layout does not declare before anything is removed, and completes them
# on `apogee reset --keep <TAB>`. The reset is the binary's, not a shell `rm`,
# for uninstall's reason -- a removal written twice drifts -- and it is never
# given --yes: the destructive confirmation belongs to the person, even in the
# middle of a make target. The installed binary is run only as $(APOGEE_BIN),
# so the binary `install` put there is the one that resets its own root --
# with MODE=dev, `apogee-dev` resetting ~/.apogee-dev.
#
# It stops where `install` stops: an install whose `check --fix` reports a
# failure (an unparseable config, an unreadable model) ends the target before
# the reset. `apogee reset` needs nothing from the config -- run it directly.
KEEP ?=

reinstall:
	cmake --preset $(PRESET) -DAPOGEE_CHANNEL=$(MODE)
	cmake --build $(BUILD_DIR) -j $(JOBS) --clean-first
	@$(MAKE) --no-print-directory install
	@echo ""
	@"$(APOGEE_BIN)" reset $(addprefix --keep ,$(KEEP))

# Removes the MODE channel's install by running that binary's own `uninstall`:
# its binary, its data directory -- the root it was built for, whatever flag
# or channel any other binary would name -- and, for release only, the
# completions. Its plan is shown and its prompt asked as they are anywhere:
# this target never answers a destructive prompt for the user. The removal
# lives in the binary rather than here for the reason the uninstall header
# gives -- a second implementation in shell drifts.
.PHONY: uninstall
uninstall:
	@if [ ! -x "$(APOGEE_BIN)" ]; then \
	    echo "error: no $(MODE)-channel install at $(APOGEE_BIN)" >&2; exit 1; fi
	@"$(APOGEE_BIN)" uninstall

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
	cmake --preset $(PRESET) -DAPOGEE_ENABLE_LLAMA=OFF -DAPOGEE_CHANNEL=$(MODE)
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
# Thin by the same rule as the rest of this file: this dispatches CI -- the
# one pipeline, .github/workflows/ci.yml -- with `publish` on, which builds
# and publishes on GitHub's runners, and nothing here builds, packages, tags
# or uploads anything.
#
# Since 2026-09-22 this is the ESCAPE HATCH, not the everyday path: a merge
# into stable publishes the release lib/release/VERSION names (since
# 2026-09-25; the CLI keeps its own version in CMakeLists.txt) --
# from the merged pull request's own CI archives, or the latest release's when
# the CLI is unchanged (ci.yml's `tag and release`) -- so the normal way to cut
# a release is to bump lib/release/VERSION and merge the PR. This target is the
# RE-CUT (since 2026-10-04, M8, which retired the separate release workflow and
# its tag trigger): for a merge whose own archives could not be released (the
# tree check refused them, or they expired), or a deleted release cut again.
# CI rebuilds all five targets from `stable` and, once every one passes,
# publishes them -- a version already released is refused. It remains worth
# keeping because the preflight below is the same set of mistakes either path
# can make, and it catches them before the remote hears about them.
#
#     make release-check VERSION=0.1.1   # preflight only, dispatches nothing
#     make release VERSION=0.1.1         # preflight, confirm, dispatch the re-cut
#
# Pushing a tag by hand does nothing any more: no workflow listens for one.
#
# The checks exist because each one is a real way to ship a broken release:
# a tag that disagrees with lib/release/VERSION publishes a release under a name the
# repository does not give it, and a BRANCH sharing a
# tag's name makes every later `git push origin vX.Y.Z` fail with
# "src refspec ... matches more than one".
#
# The suite is deliberately NOT run here: the dispatched run runs the source
# suite on every platform before it builds, as every CI run does, and a
# re-cut is cut from `stable`, which only merged -- already tested -- pull
# requests reach. `make test` remains the gate for anything not yet through
# CI.
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
	release_version="$$(tr -d ' \r\n' < "$$(git rev-parse --show-toplevel)/lib/release/VERSION")"; \
	if [ "$$release_version" != "$(VERSION_NUM)" ]; then \
	    echo "error: lib/release/VERSION says $$release_version, but the tag would be $(TAG)" >&2; \
	    echo "       bump lib/release/VERSION to $(VERSION_NUM) and commit it first" >&2; exit 1; fi; \
	if git show-ref --verify --quiet "refs/heads/$(TAG)"; then \
	    echo "error: a BRANCH named $(TAG) exists; git cannot then resolve the tag by name" >&2; \
	    echo "       git branch -D $(TAG)" >&2; exit 1; fi; \
	if git rev-parse -q --verify "refs/tags/$(TAG)" >/dev/null; then \
	    echo "error: tag $(TAG) already exists locally (git tag -d $(TAG) to redo it)" >&2; exit 1; fi; \
	if git ls-remote --exit-code --tags $(REMOTE) "refs/tags/$(TAG)" >/dev/null 2>&1; then \
	    echo "error: tag $(TAG) is already on $(REMOTE) -- that release is cut" >&2; exit 1; fi; \
	echo "preflight OK"; \
	echo "  branch   $(RELEASE_BRANCH) @ $$(git rev-parse --short HEAD), in sync with $(REMOTE)"; \
	echo "  version  lib/release/VERSION $$release_version == $(TAG)"; \
	echo "  tag      $(TAG) is free locally and on $(REMOTE)"

release: release-check
	@set -euo pipefail; \
	if ! command -v gh >/dev/null 2>&1; then \
	    echo "error: the re-cut is a dispatch of CI: install gh (https://cli.github.com) and sign in" >&2; exit 1; fi; \
	if [ "$(CONFIRM)" != "yes" ]; then \
	    echo ""; \
	    echo "Dispatching CI on $(RELEASE_BRANCH) with publish=true rebuilds all five targets from"; \
	    echo "$(RELEASE_BRANCH) @ $$(git rev-parse --short HEAD) and, once every build passes, publishes $(TAG)"; \
	    echo "as a PUBLIC GitHub release."; \
	    printf "Dispatch the re-cut of %s? [y/N] " "$(TAG)"; \
	    read -r reply </dev/tty; \
	    case "$$reply" in y|Y|yes|YES) ;; *) echo "aborted; nothing was dispatched"; exit 1 ;; esac; \
	fi; \
	gh workflow run CI --ref "$(RELEASE_BRANCH)" -f publish=true; \
	echo ""; \
	echo "dispatched -- CI is building $(TAG) from $(RELEASE_BRANCH); its run is titled '$(RELEASE_BRANCH) Release Re-cut'."; \
	echo "  gh run watch \$$(gh run list --workflow=CI --branch $(RELEASE_BRANCH) --event workflow_dispatch --limit 1 --json databaseId -q '.[0].databaseId')"; \
	echo ""; \
	echo "Next: open the branch for the release after this one, from $(RELEASE_BRANCH)."
