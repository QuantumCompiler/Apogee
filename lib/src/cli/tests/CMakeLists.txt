# The CLI application's tests. Directory layout mirrors source/'s packages.
#
# Note what this target links: apogee_core and Catch2 -- NOT the `apogee`
# executable. That is the library-first split doing its job, and
# apogee_assert_link_policy() in the top-level CMakeLists fails the configure
# step if it ever stops being true.
#
# Tests must stay hermetic: no network, no models, no writes outside the test's
# own temp directory. A test that needs a live provider is not a unit test.

include(Catch)

add_executable(apogee_tests
    smoke_test.cpp
    packages_test.cpp
    platform/platform_test.cpp
    commands/registry_test.cpp
    commands/root_test.cpp
    harness/config_test.cpp
    harness/config_edit_test.cpp
    harness/paths_test.cpp
    harness/types_test.cpp
    harness/harness_test.cpp
    backends/mock_test.cpp
    backends/sse_parser_test.cpp
    backends/http_client_test.cpp
    backends/anthropic_test.cpp
    backends/factory_test.cpp
    commands/helpers_test.cpp
    support/fake_command.h
    support/fake_command.cpp
    support/env_guard.h
    support/env_guard.cpp
    support/fake_transport.h
    support/fake_transport.cpp
)

# The template-drift test reads the checked-in sample and compares it byte for
# byte with the template compiled into the binary. It needs to find the asset
# without depending on the working directory ctest happens to use.
target_compile_definitions(apogee_tests PRIVATE
    APOGEE_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../assets"
)

# So tests can `#include "support/fake_command.h"` the same way source files
# include across packages.
target_include_directories(apogee_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(apogee_tests PRIVATE apogee_core Catch2::Catch2WithMain)
apogee_set_target_options(apogee_tests)

catch_discover_tests(apogee_tests)

# The binary's own contract, exercised the way a user meets it. These run the
# built executable as a subprocess; running a target is not linking it, so the
# link policy above still holds.
add_test(NAME cli.bare_prints_help COMMAND apogee)
set_tests_properties(cli.bare_prints_help PROPERTIES
    PASS_REGULAR_EXPRESSION "Usage: apogee")

add_test(NAME cli.version_flag COMMAND apogee --version)
set_tests_properties(cli.version_flag PROPERTIES
    PASS_REGULAR_EXPRESSION "apogee [0-9]+\\.[0-9]+\\.[0-9]+")

add_test(NAME cli.version_subcommand COMMAND apogee version)
set_tests_properties(cli.version_subcommand PROPERTIES
    PASS_REGULAR_EXPRESSION "apogee [0-9]+\\.[0-9]+\\.[0-9]+")

add_test(NAME cli.help_lists_subcommands COMMAND apogee --help)
set_tests_properties(cli.help_lists_subcommands PROPERTIES
    PASS_REGULAR_EXPRESSION "version")

add_test(NAME cli.unknown_subcommand_fails COMMAND apogee no-such-command)
set_tests_properties(cli.unknown_subcommand_fails PROPERTIES WILL_FAIL TRUE)

add_test(NAME cli.help_lists_config COMMAND apogee --help)
set_tests_properties(cli.help_lists_config PROPERTIES PASS_REGULAR_EXPRESSION "config")

# The layering constraint: harness must never include backends. C++ cannot
# enforce this the way Go's import cycles do, so it is checked mechanically.
add_test(NAME harness.layering
    COMMAND "${CMAKE_COMMAND}"
        -DAPOGEE_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../source
        -P "${CMAKE_CURRENT_SOURCE_DIR}/layering.cmake"
)

# `apogee complete` end to end -- the walking skeleton, fully offline against
# the mock backend. The pattern every later CLI test reuses.
add_test(NAME cli.complete_lifecycle
    COMMAND "${CMAKE_COMMAND}"
        -DAPOGEE_BIN=$<TARGET_FILE:apogee>
        -DAPOGEE_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/complete-e2e-home
        -P "${CMAKE_CURRENT_SOURCE_DIR}/complete_e2e.cmake"
)

# The interactive-never-listens invariant (SPEC.md -> Principles), test-locked
# the day the first interactive command exists. TWO checks, because neither is
# sufficient on its own -- see the header comments in each for why.
#
#   symbols: deterministic; catches our own code calling listen/accept.
#   runtime: catches a spawned child process holding a port.
add_test(NAME cli.no_listen_symbols
    COMMAND "${CMAKE_COMMAND}"
        -DAPOGEE_CORE_LIB=$<TARGET_FILE:apogee_core>
        -P "${CMAKE_CURRENT_SOURCE_DIR}/no_listen_symbols.cmake"
)

if(UNIX)
    add_test(NAME cli.complete_opens_no_listening_socket
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/no_listen_check.sh"
            $<TARGET_FILE:apogee>
            ${CMAKE_CURRENT_BINARY_DIR}/no-listen-home
    )
    set_tests_properties(cli.complete_opens_no_listening_socket PROPERTIES TIMEOUT 60)
endif()

# `apogee config` end to end against the real binary, under a throwaway
# APOGEE_HOME. Driven by cmake -P so it runs on every target, Windows included.
add_test(NAME cli.config_lifecycle
    COMMAND "${CMAKE_COMMAND}"
        -DAPOGEE_BIN=$<TARGET_FILE:apogee>
        -DAPOGEE_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/config-e2e-home
        -P "${CMAKE_CURRENT_SOURCE_DIR}/config_e2e.cmake"
)
