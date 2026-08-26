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
#   Tests       Catch2 v3       -- wired below (only when APOGEE_BUILD_TESTS)
#   HTTP client libcurl         -- the standing pick, NOT wired yet. It arrives
#                                  with the anthropic-backend item, which owns
#                                  find_package(CURL) and the platform TLS
#                                  backends. Wiring it now would make the
#                                  skeleton unbuildable on hosts with no curl
#                                  development package, for no present gain.

include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

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

FetchContent_MakeAvailable(nlohmann_json CLI11)

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
