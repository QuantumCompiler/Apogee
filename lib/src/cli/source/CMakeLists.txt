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
    commands/check.h
    commands/complete_protocol.h
    commands/complete_protocol.cpp
    commands/uninstall.h
    commands/uninstall.cpp
    commands/check.cpp
    commands/complete.h
    commands/complete.cpp
    commands/helpers.h
    commands/helpers.cpp
    commands/ask_prompt.h
    commands/ask_prompt.cpp
    commands/terminal.h
    commands/terminal.cpp
    commands/status_line.h
    commands/status_line.cpp
    commands/thinking_view.h
    commands/thinking_view.cpp
    commands/cli_reporter.h
    commands/cli_reporter.cpp

    commands/chat.h
    commands/chat.cpp
    commands/chat_history.h
    commands/chat_history.cpp
    commands/input_gate.h
    commands/input_gate.cpp
    commands/line_reader.h
    commands/line_reader.cpp

    ansi/ansi.h
    ansi/ansi.cpp

    logger/session.h
    logger/session.cpp
    logger/operational.h
    logger/operational.cpp

    harness/layout.h
    harness/layout.cpp
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
    backends/openai_wire.h
    backends/openai_wire.cpp
    backends/openai.h
    backends/openai.cpp
    backends/google_wire.h
    backends/google_wire.cpp
    backends/google.h
    backends/google.cpp
    backends/llama_runtime.h
    backends/llama_real.cpp
    backends/chat_template.h
    backends/chat_template.cpp
    backends/llamacpp_tokens.h
    backends/llamacpp_tokens.cpp
    backends/llamacpp.h
    backends/llamacpp.cpp
    backends/factory.h
    backends/factory.cpp

    agent/tool.h
    agent/tool.cpp
    agent/fetch_url.h
    agent/fetch_url.cpp

    agentloop/agentloop.h
    agentloop/reporter.h
    agentloop/loop.h
    agentloop/loop.cpp
    agentloop/content.h
    agentloop/content.cpp
    agentloop/question.h
    agentloop/question.cpp

    # Package headers reserved for the items that fill them. They carry no code
    # yet -- they exist so every later backlog item has one obvious home, and
    # so `#include "embedstore/embedstore.h"` compiles from the first day.
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
                      PRIVATE CURL::libcurl replxx::replxx)

# Local inference is opt-in at build time. When it is off, backends/llama_real.cpp
# compiles to the "not built in" answer and never includes llama.h -- so the
# provider, its tokenizer arithmetic, and every test over them build and run on
# the merge-blocking target, which has no llama.cpp in it.
if(APOGEE_ENABLE_LLAMA)
    target_link_libraries(apogee_core PRIVATE llama)
    target_compile_definitions(apogee_core PRIVATE APOGEE_ENABLE_LLAMA=1)
endif()

apogee_set_target_options(apogee_core)

add_executable(apogee main.cpp)
target_link_libraries(apogee PRIVATE apogee_core)
apogee_set_target_options(apogee)

# The install path: the binary, and nothing else. `make install` is a thin
# caller into this, so there is exactly one install path -- the same shape as
# the build (one CMake definition, a Makefile wrapper that adds no steps).
#
# apogee_core is deliberately NOT installed: it is a static library and an
# implementation detail of the executable, and installing it would invite
# something outside this project to link the whole product.
#
# This rule seeds no ~/.apogee/ assets on purpose. The full install contract --
# the data-directory layout, the config template, shell completions, and the
# byte-identical parity between this path and scripts/install.sh validated by
# `apogee check` -- is owned by the install-check-lifecycle backlog item.
# SPEC.md -> "No silent install drift" requires any asset landing under
# ~/.apogee/ to join EVERY install path in the same commit, so this one starts
# empty rather than shipping half a contract that check cannot yet verify.
install(TARGETS apogee RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
