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

FetchContent_MakeAvailable(nlohmann_json CLI11)

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
