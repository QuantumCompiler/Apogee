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

set(GUARDED_PACKAGES harness agentloop agent secrets tools mcp knowledge graph training)

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
    # The one allowance: `mcp/` reads newline-delimited JSON off a child's
    # pipe, which is exactly what `backends/jsonl_framer.h` was written to do
    # for the vendor CLIs -- a pure line splitter with no provider in it. A
    # second framer would be a second copy of the chunk-boundary bug class
    # the first one exists to hold. Nothing else under `backends/` is
    # reachable from `mcp/`.
    if(source MATCHES "/mcp/")
        list(FILTER offending EXCLUDE REGEX "backends/jsonl_framer\\.h")
    endif()
    # The same allowance for `training/`: its Python drivers speak JSONL over
    # a child's stdout, and the one framer is the one that has been tested
    # against every chunk boundary. Nothing else under `backends/` is
    # reachable from `training/`.
    if(source MATCHES "/training/")
        list(FILTER offending EXCLUDE REGEX "backends/jsonl_framer\\.h")
    endif()
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

# `events/` is a LEAF: it may include nothing from the project but itself.
# That is what lets any subsystem publish to it without an include cycle --
# and the day it includes `harness/` or `httpserver/`, a backend that publishes
# has pulled the server into the harness's dependency graph.
file(GLOB_RECURSE events_sources "${APOGEE_SOURCE_DIR}/events/*.h"
                                 "${APOGEE_SOURCE_DIR}/events/*.cpp")
if(events_sources STREQUAL "")
    message(FATAL_ERROR "no sources found under ${APOGEE_SOURCE_DIR}/events — "
                        "this check would pass vacuously")
endif()
foreach(source IN LISTS events_sources)
    file(STRINGS "${source}" project_includes REGEX "^[ \t]*#[ \t]*include[ \t]*\"")
    foreach(line IN LISTS project_includes)
        if(NOT line MATCHES "#[ \t]*include[ \t]*\"events/")
            get_filename_component(name "${source}" NAME)
            list(APPEND VIOLATIONS "  events/${name} is not a leaf: ${line}")
        endif()
    endforeach()
endforeach()
if(NOT VIOLATIONS STREQUAL "")
    string(REPLACE ";" "\n" pretty "${VIOLATIONS}")
    message(FATAL_ERROR "the events package includes the project:\n${pretty}\n"
                        "events/ is a leaf so that anything can publish to it.")
endif()

# `secrets/` sits beside the harness: it may include `harness/` (for the
# backend types) and itself, and nothing else. The day it includes
# `httpserver/` or `commands/`, the one place that returns a key has grown a
# dependency on a surface that renders -- the leak the package exists to
# make impossible.
file(GLOB_RECURSE secrets_sources "${APOGEE_SOURCE_DIR}/secrets/*.h"
                                  "${APOGEE_SOURCE_DIR}/secrets/*.cpp")
if(secrets_sources STREQUAL "")
    message(FATAL_ERROR "no sources found under ${APOGEE_SOURCE_DIR}/secrets — "
                        "this check would pass vacuously")
endif()
foreach(source IN LISTS secrets_sources)
    file(STRINGS "${source}" project_includes REGEX "^[ \t]*#[ \t]*include[ \t]*\"")
    foreach(line IN LISTS project_includes)
        if(NOT line MATCHES "#[ \t]*include[ \t]*\"(secrets|harness)/")
            get_filename_component(name "${source}" NAME)
            list(APPEND VIOLATIONS "  secrets/${name} reaches past the harness: ${line}")
        endif()
    endforeach()
endforeach()
if(NOT VIOLATIONS STREQUAL "")
    string(REPLACE ";" "\n" pretty "${VIOLATIONS}")
    message(FATAL_ERROR "the secrets package includes a surface:\n${pretty}\n"
                        "secrets/ may include only harness/ and itself.")
endif()

# `knowledge/` is a domain core: it may include the chunk store, the loop,
# the harness, the platform seam and itself -- never a surface. The day it
# includes `commands/` or `httpserver/`, the record logic every surface
# shares has grown a dependency on one of them, and the parity between the
# CLI capture, chat's /capture and the HTTP twin stops being structural.
file(GLOB_RECURSE knowledge_sources "${APOGEE_SOURCE_DIR}/knowledge/*.h"
                                    "${APOGEE_SOURCE_DIR}/knowledge/*.cpp")
if(knowledge_sources STREQUAL "")
    message(FATAL_ERROR "no sources found under ${APOGEE_SOURCE_DIR}/knowledge — "
                        "this check would pass vacuously")
endif()
foreach(source IN LISTS knowledge_sources)
    file(STRINGS "${source}" project_includes REGEX "^[ \t]*#[ \t]*include[ \t]*\"")
    foreach(line IN LISTS project_includes)
        if(NOT line MATCHES "#[ \t]*include[ \t]*\"(knowledge|embedstore|agentloop|agent|harness|platform)/")
            get_filename_component(name "${source}" NAME)
            list(APPEND VIOLATIONS "  knowledge/${name} reaches a surface: ${line}")
        endif()
    endforeach()
endforeach()
if(NOT VIOLATIONS STREQUAL "")
    string(REPLACE ";" "\n" pretty "${VIOLATIONS}")
    message(FATAL_ERROR "the knowledge package includes a surface:\n${pretty}\n"
                        "knowledge/ may include only embedstore/, agentloop/, agent/, harness/, "
                        "platform/ and itself.")
endif()

# `graph/` is a domain core like `knowledge/`: the extraction contract and
# the build loop every surface shares. It may include the chunk store, the
# records it materialises, the loop (for the one OUTPUT FORMAT wording and
# `run_structured`), the harness, the platform seam and itself -- never a
# surface, and never a backend: generation and embedding arrive as closures.
file(GLOB_RECURSE graph_sources "${APOGEE_SOURCE_DIR}/graph/*.h"
                                "${APOGEE_SOURCE_DIR}/graph/*.cpp")
if(graph_sources STREQUAL "")
    message(FATAL_ERROR "no sources found under ${APOGEE_SOURCE_DIR}/graph — "
                        "this check would pass vacuously")
endif()
foreach(source IN LISTS graph_sources)
    file(STRINGS "${source}" project_includes REGEX "^[ \t]*#[ \t]*include[ \t]*\"")
    foreach(line IN LISTS project_includes)
        if(NOT line MATCHES "#[ \t]*include[ \t]*\"(graph|embedstore|knowledge|agentloop|agent|harness|platform)/")
            get_filename_component(name "${source}" NAME)
            list(APPEND VIOLATIONS "  graph/${name} reaches a surface: ${line}")
        endif()
    endforeach()
endforeach()
if(NOT VIOLATIONS STREQUAL "")
    string(REPLACE ";" "\n" pretty "${VIOLATIONS}")
    message(FATAL_ERROR "the graph package includes a surface:\n${pretty}\n"
                        "graph/ may include only embedstore/, knowledge/, agentloop/, agent/, "
                        "harness/, platform/ and itself.")
endif()

# `training/` is a domain core like `graph/`: the Python boundary, the kits,
# the synth core and the dataset store every surface shares. It may include
# the harness, the platform seam, the loop's closures' types and itself --
# never a surface, and never a backend beyond the framer allowance above:
# generation arrives as a closure. `models/sha256.h` is a pure function the
# run item's manifests will hash with, allowed by name.
file(GLOB_RECURSE training_sources "${APOGEE_SOURCE_DIR}/training/*.h"
                                   "${APOGEE_SOURCE_DIR}/training/*.cpp")
if(training_sources STREQUAL "")
    message(FATAL_ERROR "no sources found under ${APOGEE_SOURCE_DIR}/training — "
                        "this check would pass vacuously")
endif()
foreach(source IN LISTS training_sources)
    file(STRINGS "${source}" project_includes REGEX "^[ \t]*#[ \t]*include[ \t]*\"")
    foreach(line IN LISTS project_includes)
        if(NOT line MATCHES "#[ \t]*include[ \t]*\"(training|harness|platform|agentloop|agent)/"
           AND NOT line MATCHES "#[ \t]*include[ \t]*\"backends/jsonl_framer\\.h\""
           AND NOT line MATCHES "#[ \t]*include[ \t]*\"models/sha256\\.h\"")
            get_filename_component(name "${source}" NAME)
            list(APPEND VIOLATIONS "  training/${name} reaches a surface: ${line}")
        endif()
    endforeach()
endforeach()
if(NOT VIOLATIONS STREQUAL "")
    string(REPLACE ";" "\n" pretty "${VIOLATIONS}")
    message(FATAL_ERROR "the training package includes a surface:\n${pretty}\n"
                        "training/ may include only harness/, platform/, agentloop/, agent/, "
                        "backends/jsonl_framer.h, models/sha256.h and itself.")
endif()

list(LENGTH ALL_SOURCES count)
message(STATUS "layering: ${count} sources across ${GUARDED_PACKAGES}, no backends includes - OK")
