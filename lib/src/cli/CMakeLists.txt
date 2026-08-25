# The Apogee CLI application.
#
# This is the build root for `lib/src/cli` — one application, one self-contained
# build. Everything the CLI needs to compile lives beside this file: its
# presets, its cmake helpers, its pinned third-party code, and its style and
# lint configuration. The GUI applications land later as sibling directories
# (lib/src/darwin|linux|windows), each owning its build the same way.
#
# The repo-wide CI/CD entry point is lib/scripts/cicd.sh, which drives this
# build; nothing above this directory needs to know how the CLI compiles.

cmake_minimum_required(VERSION 3.25)

# Version is the single source of truth for the binary's --version output and
# is stamped into version.h via configure_file. Bump it here when cutting a
# release (see CLAUDE.md -> Versioning).
project(Apogee
    VERSION 0.1.0
    DESCRIPTION "A self-contained C++ AI harness for local and cloud LLMs"
    LANGUAGES CXX
)

# C++20 baseline, decided 2026-08-24: the only standard uniformly supported
# across all six release targets. Promotion to C++23 is a deliberate event,
# not a drift -- change it here and nowhere else.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(APOGEE_BUILD_TESTS  "Build the test suite" ON)
# llama.cpp is pinned and compile-proofed, but its build is heavy (Metal/CUDA
# kernels). It stays OFF on the per-push hot path and is exercised by a
# dedicated non-blocking CI job until llamacpp-backend lands. See third_party/.
option(APOGEE_ENABLE_LLAMA "Configure and build the pinned llama.cpp target" OFF)

include(ApogeeWarnings)
include(ApogeeDependencies)
include(ApogeeLinkPolicy)

add_subdirectory(third_party)
add_subdirectory(source)

if(APOGEE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Enforced last, once every target exists: tests link the library, never the
# CLI executable. See cmake/ApogeeLinkPolicy.cmake for the rule it checks.
apogee_assert_link_policy()
