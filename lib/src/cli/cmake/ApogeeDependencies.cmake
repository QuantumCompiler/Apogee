# Dependency acquisition for Apogee.
#
# Strategy, decided 2026-08-24: FetchContent, with FIND_PACKAGE_ARGS on every
# declaration so a system-installed copy is preferred and the network fetch is
# only the fallback. That keeps offline and packaged builds working without a
# second dependency mechanism.
#
# Adding a dependency:
#   1. Declare it here with a PINNED GIT_TAG (never a branch) + FIND_PACKAGE_ARGS.
#   2. FetchContent_MakeAvailable it here.
#   3. target_link_libraries against it in the consuming target's CMakeLists.
#   4. Record the pick in DEVELOPER.md -> Dependencies.
#
# The standardized picks (decided once, project-wide -- downstream work consumes
# these rather than reopening them):
#   JSON        nlohmann/json   -- wired below
#   YAML        yaml-cpp        -- wired below. READ PATH ONLY: the config
#                                  engine never serializes through it, because
#                                  marshaling a struct back to YAML strips
#                                  comments and reorders keys. Writes go
#                                  through the text-surgery helpers in
#                                  harness/config_edit.h. See config-engine.
#   CLI parsing CLI11           -- wired below
#   Line editing replxx         -- wired below. Used ONLY by the chat REPL's
#                                  interactive path; a non-TTY run never
#                                  constructs it. GNU readline was ruled out on
#                                  licence grounds (GPL).
#   JSON Schema json-schema-validator -- wired below (arrived with analyze-agents).
#                                  Draft-07 validation of structured agent
#                                  output; sits on nlohmann/json.
#   Tests       Catch2 v3       -- wired below (only when APOGEE_BUILD_TESTS)
#   HTTP client libcurl         -- wired below (arrived with anthropic-backend).
#                                  Found, never fetched: curl is a system
#                                  library everywhere Apogee ships, and it is
#                                  built against the platform's own TLS stack
#                                  (Secure Transport / OpenSSL / Schannel),
#                                  which is what makes the system trust store
#                                  work without a bundled CA list.
#   HTTP server cpp-httplib     -- wired below (arrived with serve-public-plane).
#                                  Header-only, FETCHED NEVER FOUND: a system
#                                  copy is a compiled library built with
#                                  whatever TLS and compression options its
#                                  packager chose, and `apogee serve` must not
#                                  gain a TLS stack on one machine and not
#                                  another. Every optional feature is switched
#                                  off explicitly, so the binary's dependency
#                                  set is the same on all five targets. Plain
#                                  HTTP only: a deployment terminates TLS in
#                                  front of it, which is where certificates
#                                  belong.

include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

# libcurl: found on the system, never fetched.
#
# Building curl from source would mean choosing and building a TLS stack too,
# and the whole point of the platform-trust-store decision (2026-08-25) is to
# use the one the OS already manages -- an administrator's cert policy applies,
# and revocations arrive without an Apogee release. Every target ships curl:
# macOS and Linux have it, and Windows has had it in-box since 1803 -- though
# the in-box copy is the tool alone, so a Windows BUILD takes MSYS2's
# `curl-winssl` package (curl on Schannel, the OS trust store) and links it
# statically, which is what lets the executable ship by itself.
find_package(CURL REQUIRED)

# The two adjustments below set properties on curl's imported target, and
# CURL::libcurl is not always that target: curl's own CURLConfig.cmake (a curl
# built with CMake, which some packagers ship) makes it an ALIAS of
# CURL::libcurl_static or CURL::libcurl_shared, and an alias cannot carry
# properties. Resolve it once, here.
set(_apogee_curl_target CURL::libcurl)
if(TARGET CURL::libcurl)
    get_target_property(_apogee_curl_aliased CURL::libcurl ALIASED_TARGET)
    if(_apogee_curl_aliased)
        set(_apogee_curl_target "${_apogee_curl_aliased}")
    endif()
endif()

# A static libcurl drags its own dependencies behind it (on MSYS2: zlib,
# brotli, zstd, nghttp2, libpsl, libidn2, libssh2, and the Win32 libraries
# Schannel and Winsock live in), and FindCURL's imported target names only the
# library itself -- it fills the transitive list from pkg-config's SHARED
# answer, and on the MinGW side adds nothing at all. The `--static` answer is
# the closure, so read that one and put it on the target.
if(MINGW AND CURL_USE_STATIC_LIBS AND TARGET CURL::libcurl)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(APOGEE_CURL_PC REQUIRED libcurl)
    set_target_properties(${_apogee_curl_target} PROPERTIES
        INTERFACE_LINK_LIBRARIES   "${APOGEE_CURL_PC_STATIC_LIBRARIES}"
        INTERFACE_LINK_DIRECTORIES "${APOGEE_CURL_PC_STATIC_LIBRARY_DIRS}")
    message(STATUS "Apogee: static libcurl on MinGW links ${APOGEE_CURL_PC_STATIC_LIBRARIES}")
endif()

# Drop a redundant system include directory from curl's imported target.
#
# On macOS, CURL_INCLUDE_DIRS is the SDK's own /usr/include -- already searched
# implicitly. Propagating it as an explicit -isystem is not merely redundant: it
# places the SDK's C headers AHEAD of the compiler's own, so a toolchain whose
# stddef.h lives elsewhere (Homebrew LLVM, which is what `make lint` runs) never
# defines size_t, and every system header that uses it fails to parse. The
# symptom is bizarre -- hundreds of "unknown type name 'size_t'" errors inside
# Apple's _stdio.h -- and it points nowhere near the actual cause.
#
# Compiling is unaffected because the build uses Apple clang, whose own headers
# are in that same SDK. Only the mixed-toolchain case breaks, which is exactly
# the lint job.
if(TARGET CURL::libcurl)
    get_target_property(_apogee_curl_includes ${_apogee_curl_target} INTERFACE_INCLUDE_DIRECTORIES)
    if(_apogee_curl_includes)
        set(_apogee_curl_kept "")
        foreach(dir IN LISTS _apogee_curl_includes)
            if(NOT dir MATCHES "/usr/include$")
                list(APPEND _apogee_curl_kept "${dir}")
            endif()
        endforeach()
        set_target_properties(${_apogee_curl_target} PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${_apogee_curl_kept}")
    endif()
endif()

# SYSTEM on every declaration: third-party headers are included by our
# translation units, and Apogee's warning bar (-Wconversion, -Wold-style-cast,
# ...) is ours to meet, not theirs.
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
    SYSTEM
    FIND_PACKAGE_ARGS NAMES nlohmann_json
)

FetchContent_Declare(CLI11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG        v2.4.2
    GIT_SHALLOW    TRUE
    SYSTEM
    FIND_PACKAGE_ARGS NAMES CLI11
)

FetchContent_Declare(yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG        0.8.0
    GIT_SHALLOW    TRUE
    SYSTEM
    FIND_PACKAGE_ARGS NAMES yaml-cpp
)
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_FORMAT_SOURCE OFF CACHE BOOL "" FORCE)

FetchContent_Declare(replxx
    GIT_REPOSITORY https://github.com/AmokHuginnsson/replxx.git
    GIT_TAG        release-0.0.4
    GIT_SHALLOW    TRUE
    SYSTEM
    FIND_PACKAGE_ARGS NAMES replxx
)
set(REPLXX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(REPLXX_BUILD_PACKAGE OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(nlohmann_json CLI11 replxx)

# JSON Schema validation (draft-07), for structured agent output: a report is
# validated client-side on EVERY provider, whatever native mode the wire also
# asked for. pboettch's validator sits on nlohmann/json -- the project's JSON
# library -- which is why it was picked over a second JSON stack; a hand-rolled
# validator would be a second implementation of draft-07 (decided 2026-09-13).
# Pinned and fetched, with FIND_PACKAGE_ARGS so a packaged copy is preferred.
FetchContent_Declare(json_schema_validator
    GIT_REPOSITORY https://github.com/pboettch/json-schema-validator.git
    GIT_TAG        2.3.0
    GIT_SHALLOW    TRUE
    SYSTEM
    FIND_PACKAGE_ARGS NAMES nlohmann_json_schema_validator
)
set(JSON_VALIDATOR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(JSON_VALIDATOR_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JSON_VALIDATOR_INSTALL OFF CACHE BOOL "" FORCE)
set(JSON_VALIDATOR_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(json_schema_validator)
if(NOT TARGET nlohmann_json_schema_validator::validator)
    add_library(nlohmann_json_schema_validator::validator ALIAS nlohmann_json_schema_validator)
endif()

# cpp-httplib: the `apogee serve` listener. Deliberately WITHOUT
# FIND_PACKAGE_ARGS -- see the note at the top of this file -- and with every
# optional integration off, so that including it changes nothing about what
# the binary links. The library's own CMake target would otherwise probe for
# OpenSSL, zlib, brotli and zstd and quietly link whichever it found.
FetchContent_Declare(httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.56.0
    GIT_SHALLOW    TRUE
    SYSTEM
)
set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZSTD_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_REQUIRE_OPENSSL OFF CACHE BOOL "" FORCE)
set(HTTPLIB_COMPILE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_INSTALL OFF CACHE BOOL "" FORCE)
set(HTTPLIB_TEST OFF CACHE BOOL "" FORCE)
# The non-blocking resolver is a per-platform code path (CFHost on macOS,
# GetAddrInfoEx with overlapped I/O on Windows), and on Windows it calls
# GetAddrInfoExCancel, which the MinGW-w64 headers do not declare -- the
# header does not compile there. It only matters to httplib's CLIENT, and
# Apogee's HTTP client is curl; `apogee serve` binds, it never resolves a
# name. Off everywhere, so the header compiles to the same thing on all five
# targets, which is the stance of everything else in this block.
set(HTTPLIB_USE_NON_BLOCKING_GETADDRINFO OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(httplib)

# yaml-cpp 0.8.0 (the newest release; tagged 2023) opens with
# cmake_minimum_required(VERSION 3.4), and CMake >= 4.0 refuses to configure a
# project asking for < 3.5 compatibility. This raises the floor for that
# subproject only, then restores the previous value so Apogee's own targets and
# every other dependency keep the project's real policy settings.
#
# Revisit when yaml-cpp cuts a release past 0.8.0 -- the fix is already on its
# master branch. Pinning a master SHA instead was rejected: a pinned release
# with a two-line shim is easier to reason about than an unreleased commit.
set(APOGEE_SAVED_POLICY_MINIMUM "${CMAKE_POLICY_VERSION_MINIMUM}")
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_MakeAvailable(yaml-cpp)
set(CMAKE_POLICY_VERSION_MINIMUM "${APOGEE_SAVED_POLICY_MINIMUM}")

# The same release's other age mark: emitterutils.cpp uses uint16_t without
# including <cstdint>, which libstdc++ 13+ no longer pulls in transitively --
# GCC 15/16, the MinGW-w64 compilers, stop on it (also fixed on yaml-cpp's
# master, same revisit). Only when this build compiled yaml-cpp itself: a
# found package is already built.
if(yaml-cpp_SOURCE_DIR AND TARGET yaml-cpp)
    target_compile_options(yaml-cpp PRIVATE
        $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-include$<SEMICOLON>cstdint>)
endif()

if(APOGEE_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.7.1
        GIT_SHALLOW    TRUE
        SYSTEM
        FIND_PACKAGE_ARGS NAMES Catch2 CONFIG
    )
    FetchContent_MakeAvailable(Catch2)

    # catch_discover_tests() ships in Catch2's extras/ directory, which is only
    # on the module path automatically when Catch2 came from find_package.
    # This file is include()d, so the append lands in the top-level scope and
    # reaches every subdirectory.
    if(catch2_SOURCE_DIR)
        list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
    endif()
endif()
