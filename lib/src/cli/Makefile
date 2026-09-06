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

.PHONY: help build install install-completions install-zsh-completion test clean fresh format format-check lint llama all

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
	@echo "  llama         Build with the pinned llama.cpp target enabled (slow)"
	@echo "  clean         Remove this preset's build directory"
	@echo "  fresh         Clean-room clone-and-build of the current branch"
	@echo ""
	@echo "Presets are the six release targets plus 'default' (host native):"
	@echo "  linux-x64  linux-arm64  macos-x64  macos-arm64  windows-x64  windows-arm64"
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

llama:
	cmake --preset $(PRESET) -DAPOGEE_ENABLE_LLAMA=ON
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
