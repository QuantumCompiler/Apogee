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
    harness/errors.h
    harness/errors.cpp
    harness/cancellation.h
    harness/cancellation.cpp
    harness/types.h
    harness/types.cpp
    harness/behavior.h
    harness/behavior.cpp
    harness/provider.h
    harness/provider.cpp
    harness/context_windows.h
    harness/context_windows.cpp
    harness/harness.h
    harness/harness.cpp

    backends/backends.h
    backends/mock.h
    backends/mock.cpp
    backends/http_client.h
    backends/http_client.cpp
    backends/sse_parser.h
    backends/sse_parser.cpp
    backends/anthropic_wire.h
    backends/anthropic_wire.cpp
    backends/anthropic.h
    backends/anthropic.cpp

    # Package headers reserved for the items that fill them. They carry no code
    # yet -- they exist so every later backlog item has one obvious home, and
    # so `#include "agentloop/agentloop.h"` compiles from the first day.
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

target_link_libraries(apogee_core PUBLIC CLI11::CLI11 nlohmann_json::nlohmann_json yaml-cpp::yaml-cpp
                      PRIVATE CURL::libcurl)

apogee_set_target_options(apogee_core)

add_executable(apogee main.cpp)
target_link_libraries(apogee PRIVATE apogee_core)
apogee_set_target_options(apogee)
