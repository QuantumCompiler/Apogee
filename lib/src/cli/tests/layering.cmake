# Layering check: the harness must never include the backends layer.
#
# This is a Core constraint of harness-core, not a style rule. The dependency
# runs one way — backends include harness — and `harness::ModelBehavior` exists
# as plain data precisely so the agent loop can ask about a model family without
# the harness reaching back. Ommi has the same seam for the same reason: in Go
# the compiler enforces it, because the reverse edge is an import cycle and the
# build simply fails.
#
# C++ has no such enforcement — a `#include "backends/anthropic.h"` in the
# harness compiles perfectly and the layering is gone, silently. So the check is
# mechanical and runs in CI: grep the harness sources for the edge that must not
# exist.
#
# Driven with `cmake -P` so it runs on all six targets, Windows included.

if(NOT DEFINED APOGEE_SOURCE_DIR)
    message(FATAL_ERROR "APOGEE_SOURCE_DIR must be set")
endif()

file(GLOB_RECURSE HARNESS_SOURCES "${APOGEE_SOURCE_DIR}/harness/*.h"
                                  "${APOGEE_SOURCE_DIR}/harness/*.cpp")

if(HARNESS_SOURCES STREQUAL "")
    message(FATAL_ERROR "no harness sources found under ${APOGEE_SOURCE_DIR}/harness — "
                        "this check would pass vacuously")
endif()

set(VIOLATIONS "")
foreach(source IN LISTS HARNESS_SOURCES)
    file(STRINGS "${source}" offending REGEX "^[ \t]*#[ \t]*include[ \t]*[\"<]backends/")
    if(NOT offending STREQUAL "")
        get_filename_component(name "${source}" NAME)
        list(APPEND VIOLATIONS "  ${name}: ${offending}")
    endif()
endforeach()

if(NOT VIOLATIONS STREQUAL "")
    string(REPLACE ";" "\n" pretty "${VIOLATIONS}")
    message(FATAL_ERROR
        "the harness layer includes the backends layer:\n${pretty}\n"
        "The dependency runs one way. If the harness needs something a backend "
        "knows, express it as plain data here (see harness/behavior.h) rather "
        "than including the backend.")
endif()

list(LENGTH HARNESS_SOURCES count)
message(STATUS "layering: ${count} harness sources, no backends includes — OK")
