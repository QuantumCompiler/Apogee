# Layering check: neither the harness nor the agent loop may include backends.
#
# A Core constraint of harness-core, extended to agentloop when the loop landed
# (CLAUDE.md said it would). The dependency runs one way — backends include
# harness — and `harness::ModelBehavior` exists as plain data precisely so the
# loop can ask about a model family without reaching back. Ommi has the same
# seam for the same reason: in Go the compiler enforces it, because the reverse
# edge is an import cycle and the build simply fails.
#
# The loop matters as much as the harness here. A loop that includes a backend
# starts special-casing one vendor's tool dialect, and "one shared loop for all
# surfaces" quietly becomes "one loop with an Anthropic branch".
#
# `commands/` is deliberately NOT checked: it is the composition root, and
# assembling providers is its job.
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

set(GUARDED_PACKAGES harness agentloop agent)

set(ALL_SOURCES "")
foreach(package IN LISTS GUARDED_PACKAGES)
    file(GLOB_RECURSE package_sources "${APOGEE_SOURCE_DIR}/${package}/*.h"
                                      "${APOGEE_SOURCE_DIR}/${package}/*.cpp")
    if(package_sources STREQUAL "")
        message(FATAL_ERROR "no sources found under ${APOGEE_SOURCE_DIR}/${package} — "
                            "this check would pass vacuously")
    endif()
    list(APPEND ALL_SOURCES ${package_sources})
endforeach()

set(VIOLATIONS "")
foreach(source IN LISTS ALL_SOURCES)
    file(STRINGS "${source}" offending REGEX "^[ \t]*#[ \t]*include[ \t]*[\"<]backends/")
    if(NOT offending STREQUAL "")
        get_filename_component(name "${source}" NAME)
        list(APPEND VIOLATIONS "  ${name}: ${offending}")
    endif()
endforeach()

if(NOT VIOLATIONS STREQUAL "")
    string(REPLACE ";" "\n" pretty "${VIOLATIONS}")
    message(FATAL_ERROR
        "a guarded layer includes the backends layer:\n${pretty}\n"
        "The dependency runs one way. If the harness or the loop needs something "
        "a backend knows, express it as plain data (see harness/behavior.h) or as "
        "a capability interface (harness/provider.h) rather than including the "
        "backend.")
endif()

list(LENGTH ALL_SOURCES count)
message(STATUS "layering: ${count} sources across ${GUARDED_PACKAGES}, no backends includes - OK")
