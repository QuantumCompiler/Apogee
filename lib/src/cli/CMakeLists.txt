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

# On macOS, resolve the SDK through xcrun ("macosx") rather than leaving it to
# the compiler. Apple clang, given no -isysroot and no SDKROOT, falls back to
# the Command Line Tools' MacOSX.sdk symlink even when xcode-select names an
# Xcode -- and a CLT newer than that Xcode ships stubs its linker cannot read
# (2026-09-19: the 27.0 SDK's arm64e.x1 entries against Xcode 26.6's ld,
# "tapi error: malformed file ... unknown architecture", on every link).
# "macosx" makes CMake ask xcrun, which answers with the selected toolchain's
# own SDK -- the one its linker was built for. Before project(), because the
# compiler check inside it is what links. SDKROOT in the environment and
# -DCMAKE_OSX_SYSROOT on the command line both still win; a cross-compile is
# left alone -- tested on CMAKE_TOOLCHAIN_FILE, not CMAKE_SYSTEM_NAME, because
# the toolchain file has not been read yet at this point and the name it sets
# does not exist until project().
if(CMAKE_HOST_APPLE AND NOT CMAKE_TOOLCHAIN_FILE AND NOT CMAKE_SYSTEM_NAME AND NOT CMAKE_OSX_SYSROOT AND NOT DEFINED ENV{SDKROOT})
    set(CMAKE_OSX_SYSROOT "macosx" CACHE STRING "macOS SDK, resolved through xcrun")
endif()

# Version is the single source of truth for the binary's --version output and
# is stamped into version.h via configure_file. Bump it here when cutting a
# release (see CLAUDE.md -> Versioning).
project(Apogee
    VERSION 0.1.0
    DESCRIPTION "A self-contained C++ AI harness for local and cloud LLMs"
    # C is enabled for one reason: SQLite ships as a C amalgamation, and the
    # chunk store compiles it. No Apogee source is C -- "primarily C++" is
    # unaffected by the language a vendored dependency happens to be written in.
    LANGUAGES CXX C
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
# llama.cpp is part of the executable (user decision, 2026-09-19): local
# inference ships with the tool, so every platform build and every release
# build links it, and every CI test run exercises it. ON is the default for
# that reason; the build is heavy (GPU kernels), and a developer iterating on
# something else turns it off with -DAPOGEE_ENABLE_LLAMA=OFF (`make no-llama`)
# -- the "not built in" half of backends/llama_real.cpp keeps that build
# honest. Until 2026-09-19 the default was OFF and one CI job built it.
option(APOGEE_ENABLE_LLAMA "Configure and build the pinned llama.cpp target" ON)

# Everything links statically into the one executable: it ships alone, on
# every target (the release workflow copies the binary and nothing else; the
# Windows presets add -static for the runtime too). Set HERE, before any
# dependency is declared, and forced: a subproject's own
# `option(BUILD_SHARED_LIBS ... ON)` -- llama.cpp's, for one -- would otherwise
# put ON into the cache for every subproject configured after it, and for the
# whole project on the next configure. That is what the llama-enabled build
# had been doing: its executable needed libllama, libggml, libmtmd, replxx
# and the schema validator as dylibs beside it (found 2026-09-19).
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Apogee links everything statically; the executable ships alone" FORCE)

# Standard bin/lib/share destinations for the install rule in source/. This one
# ships with CMake; the three below it are ours.
include(GNUInstallDirs)

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
