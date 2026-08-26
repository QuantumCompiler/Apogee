# The CLI application.
#
# Two targets, and the split between them is load-bearing:
#
#   apogee_core -- EVERY capability. Tests link this. The future HTTP server
#                  links this. It is the whole product minus an entry point.
#   apogee      -- a thin face: argv in, exit code out, nothing else.
#
# Parity across surfaces is structural because of that split (SPEC.md ->
# Principles: "one source of truth per concern"). cmake/ApogeeLinkPolicy.cmake
# fails the configure step if anything ever links `apogee`.

# Stamped into the binary at configure time. GIT_COMMIT is best-effort: a
# tarball export with no .git still builds, it just reports "unknown".
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE APOGEE_GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT APOGEE_GIT_COMMIT)
    set(APOGEE_GIT_COMMIT "unknown")
endif()
string(TIMESTAMP APOGEE_BUILD_DATE "%Y-%m-%d" UTC)

add_library(apogee_core STATIC
    version/version.h
    version/version.cpp
    platform/platform.h
    platform/platform.cpp
    commands/command.h
    commands/registry.h
    commands/registry.cpp
    commands/root.h
    commands/root.cpp
    commands/version_command.h
    commands/version_command.cpp
    commands/config_cmd.h
    commands/config_cmd.cpp

    harness/paths.h
    harness/paths.cpp
    harness/config.h
    harness/config.cpp
    harness/config_template.cpp
    harness/config_edit.h
    harness/config_edit.cpp

    # Package headers reserved for the items that fill them. They carry no code
    # yet -- they exist so every later backlog item has one obvious home, and
    # so `#include "backends/backends.h"` compiles from the first day.
    harness/harness.h
    backends/backends.h
    agentloop/agentloop.h
    embedstore/embedstore.h
    httpserver/httpserver.h
    mcp/mcp.h
)

target_include_directories(apogee_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_compile_definitions(apogee_core PRIVATE
    APOGEE_VERSION="${PROJECT_VERSION}"
    APOGEE_GIT_COMMIT="${APOGEE_GIT_COMMIT}"
    APOGEE_BUILD_DATE="${APOGEE_BUILD_DATE}"
)

target_link_libraries(apogee_core PUBLIC CLI11::CLI11 nlohmann_json::nlohmann_json yaml-cpp::yaml-cpp)

apogee_set_target_options(apogee_core)

add_executable(apogee main.cpp)
target_link_libraries(apogee PRIVATE apogee_core)
apogee_set_target_options(apogee)
