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

.PHONY: help build install test clean fresh format format-check lint llama all

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
install: build
	@cmake --install $(BUILD_DIR) --prefix "$(PREFIX)"
	@case ":$$PATH:" in \
		*":$(PREFIX)/bin:"*) ;; \
		*) echo ""; \
		   echo "note: $(PREFIX)/bin is not on your PATH -- add it to run 'apogee' by name:"; \
		   echo "      export PATH=\"$(PREFIX)/bin:\$$PATH\"" ;; \
	esac

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
