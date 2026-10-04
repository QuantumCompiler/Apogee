# Layering check: the module map's mutation check, and the named rules only a
# scan can express (Architecture A4 rescoped it; the coarse four-layer law is
# the build's now -- cmake/modules.cmake and cmake/ApogeeLinkPolicy.cmake).
#
# The first named rule is the oldest: neither the harness nor the agent loop
# may include backends. A Core constraint of harness-core, extended to agentloop
# when the loop landed (CLAUDE.md said it would). The dependency runs one way
# — backends include harness — and `harness::ModelBehavior` exists as plain
# data precisely so the loop can ask about a model family without reaching
# back. Ommi has the same seam for the same reason: in Go the compiler enforces
# it, because the reverse edge is an import cycle and the build simply fails.
#
# The loop matters as much as the harness here. A loop that includes a backend
# starts special-casing one vendor's tool dialect, and "one shared loop for all
# surfaces" quietly becomes "one loop with an Anthropic branch".
#
# `cli/` is deliberately NOT checked: it is the composition root, and
# assembling providers is its job (`views/` and `machine/`, the other two
# modules `commands/` split into in A3, are held below).
#
# The link graph cannot hold this one: `backends/` sits in Data, below the
# harness, so the include would compile and link. The named rules are the
# seams finer than a layer -- a module's narrower floor, a single header
# allowed by name -- and a row in the map that loosens one still fails here.
#
# Driven with `cmake -P` so it runs on all five targets, Windows included.

if(NOT DEFINED APOGEE_SOURCE_DIR)
    message(FATAL_ERROR "APOGEE_SOURCE_DIR must be set")
endif()
if(NOT DEFINED APOGEE_MODULE_MAP)
    message(FATAL_ERROR "APOGEE_MODULE_MAP must be set")
endif()

# ---- The map, and the tree it describes (Architecture A1, A2, A4) -----------
#
# Every package is a module of one layer, and the layers are ADR 0001's:
# Infrastructure -> Data -> Business -> Presentation, dependencies pointing
# down. Since A4 the map lives in cmake/modules.cmake -- each module's layer
# and the modules it links -- and the build enforces the coarse law from it:
# a module is a library linked to exactly its row, the link policy fails the
# configure step on a link up a layer or a cycle, and an include that reaches
# up a layer does not compile. What stays here is what only a scan can see.
include("${APOGEE_MODULE_MAP}")

# The tree says the layers too: every module sits in source/<layer>/<module>/,
# and the directory must agree with the map -- a module moved without its row,
# or a row changed without the move, fails.
set(PACKAGES "")
file(GLOB top_level LIST_DIRECTORIES true "${APOGEE_SOURCE_DIR}/*")
foreach(dir IN LISTS top_level)
    if(IS_DIRECTORY "${dir}")
        get_filename_component(name "${dir}" NAME)
        if(NOT name IN_LIST APOGEE_LAYERS)
            message(FATAL_ERROR "'${name}' sits directly under ${APOGEE_SOURCE_DIR} -- a package "
                                "lives in its layer's directory (presentation, business, data or "
                                "infrastructure)")
        endif()
    endif()
endforeach()
foreach(layer IN LISTS APOGEE_LAYERS)
    file(GLOB layer_dirs LIST_DIRECTORIES true "${APOGEE_SOURCE_DIR}/${layer}/*")
    foreach(dir IN LISTS layer_dirs)
        if(IS_DIRECTORY "${dir}")
            get_filename_component(package "${dir}" NAME)
            if(NOT DEFINED APOGEE_MODULE_LAYER_${package})
                message(FATAL_ERROR "the package '${package}' is in no layer -- give it a row, "
                                    "with its links, in cmake/modules.cmake")
            endif()
            if(NOT APOGEE_MODULE_LAYER_${package} STREQUAL layer)
                message(FATAL_ERROR "the package '${package}' sits in ${layer}/ but "
                                    "cmake/modules.cmake puts it in "
                                    "${APOGEE_MODULE_LAYER_${package}} -- move one to match "
                                    "the other")
            endif()
            list(APPEND PACKAGES ${package})
        endif()
    endforeach()
endforeach()
foreach(package IN LISTS APOGEE_MODULES)
    if(NOT package IN_LIST PACKAGES)
        message(FATAL_ERROR "cmake/modules.cmake names '${package}', which is not a package "
                            "under ${APOGEE_SOURCE_DIR}/${APOGEE_MODULE_LAYER_${package}} -- a "
                            "package that moved must move there too")
    endif()
    set(PACKAGE_DIR_${package} "${APOGEE_SOURCE_DIR}/${APOGEE_MODULE_LAYER_${package}}/${package}")
endforeach()

# ---- The map's mutation check: includes and links agree ----------------------
#
# The build sees a layer, not a module: within its own layer and below, an
# include of a module the row does not link still compiles, because the layer
# root is one include directory. So the scan holds the map to the code in both
# directions -- an include of another module the row does not declare fails,
# and so does a declared link nothing includes. Deleting a link from the map,
# or adding one nobody uses, fails here by name.
set(MISMATCHES "")
set(layered_sources 0)
set(edges 0)
foreach(package IN LISTS PACKAGES)
    file(GLOB_RECURSE package_sources "${PACKAGE_DIR_${package}}/*.h"
                                      "${PACKAGE_DIR_${package}}/*.cpp")
    set(included "")
    foreach(source IN LISTS package_sources)
        math(EXPR layered_sources "${layered_sources} + 1")
        file(STRINGS "${source}" project_includes REGEX "^[ \t]*#[ \t]*include[ \t]*\"[a-z_]+/")
        foreach(line IN LISTS project_includes)
            string(REGEX REPLACE "^[ \t]*#[ \t]*include[ \t]*\"([a-z_]+)/.*" "\\1" target "${line}")
            if(NOT DEFINED APOGEE_MODULE_LAYER_${target} OR target STREQUAL package)
                continue()
            endif()
            if(NOT target IN_LIST included)
                list(APPEND included ${target})
            endif()
            if(NOT target IN_LIST APOGEE_LINKS_${package})
                file(RELATIVE_PATH relative "${APOGEE_SOURCE_DIR}" "${source}")
                string(CONCAT mismatch "  ${relative} includes ${target}/, which "
                                       "cmake/modules.cmake does not link to ${package}")
                list(APPEND MISMATCHES "${mismatch}")
            endif()
        endforeach()
    endforeach()
    foreach(target IN LISTS APOGEE_LINKS_${package})
        math(EXPR edges "${edges} + 1")
        if(NOT target IN_LIST included)
            string(CONCAT mismatch "  cmake/modules.cmake links ${package} to ${target}, but "
                                   "nothing in ${package}/ includes ${target}/")
            list(APPEND MISMATCHES "${mismatch}")
        endif()
    endforeach()
endforeach()
if(layered_sources EQUAL 0)
    message(FATAL_ERROR "no sources found under ${APOGEE_SOURCE_DIR} -- this check would pass "
                        "vacuously")
endif()
if(NOT MISMATCHES STREQUAL "")
    list(JOIN MISMATCHES "\n" pretty)
    message(FATAL_ERROR "the module map and the includes disagree:\n${pretty}\n"
                        "cmake/modules.cmake declares what each module links; an include of "
                        "another module is a declared link, and a declared link is used. What "
                        "a lower layer needs from a higher one crosses as an interface declared "
                        "below and implemented above (contracts/provider.h's ProviderRegistry), "
                        "or as plain data -- never as a link up.")
endif()

# ---- The named rules ------------------------------------------------------------

set(GUARDED_PACKAGES harness agentloop agent secrets tools mcp knowledge graph training)

set(ALL_SOURCES "")
foreach(package IN LISTS GUARDED_PACKAGES)
    file(GLOB_RECURSE package_sources "${PACKAGE_DIR_${package}}/*.h"
                                      "${PACKAGE_DIR_${package}}/*.cpp")
    if(package_sources STREQUAL "")
        message(FATAL_ERROR "no sources found under ${PACKAGE_DIR_${package}} — "
                            "this check would pass vacuously")
    endif()
    list(APPEND ALL_SOURCES ${package_sources})
endforeach()

set(VIOLATIONS "")
foreach(source IN LISTS ALL_SOURCES)
    file(STRINGS "${source}" offending REGEX "^[ \t]*#[ \t]*include[ \t]*[\"<]backends/")
    # `mcp/` and `training/` read newline-delimited JSON off a child's pipe
    # with the one JSONL framer, tested against every chunk boundary -- since
    # A1 it is `transport/jsonl_framer.h`, a primitive beside the HTTP client,
    # so no allowance into `backends/` is needed: nothing under it is
    # reachable from either.
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
file(GLOB_RECURSE events_sources "${PACKAGE_DIR_events}/*.h"
                                 "${PACKAGE_DIR_events}/*.cpp")
if(events_sources STREQUAL "")
    message(FATAL_ERROR "no sources found under ${PACKAGE_DIR_events} — "
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

# `secrets/` sits on the contracts: it may include `contracts/` (for the
# backend types and the config editor) and itself, and nothing else. The day it includes
# `httpserver/` or `cli/`, the one place that returns a key has grown a
# dependency on a surface that renders -- the leak the package exists to
# make impossible.
file(GLOB_RECURSE secrets_sources "${PACKAGE_DIR_secrets}/*.h"
                                  "${PACKAGE_DIR_secrets}/*.cpp")
if(secrets_sources STREQUAL "")
    message(FATAL_ERROR "no sources found under ${PACKAGE_DIR_secrets} — "
                        "this check would pass vacuously")
endif()
foreach(source IN LISTS secrets_sources)
    file(STRINGS "${source}" project_includes REGEX "^[ \t]*#[ \t]*include[ \t]*\"")
    foreach(line IN LISTS project_includes)
        if(NOT line MATCHES "#[ \t]*include[ \t]*\"(secrets|contracts)/")
            get_filename_component(name "${source}" NAME)
            list(APPEND VIOLATIONS "  secrets/${name} reaches past the contracts: ${line}")
        endif()
    endforeach()
endforeach()
if(NOT VIOLATIONS STREQUAL "")
    string(REPLACE ";" "\n" pretty "${VIOLATIONS}")
    message(FATAL_ERROR "the secrets package includes a surface:\n${pretty}\n"
                        "secrets/ may include only contracts/ and itself.")
endif()

# `markdown/` renders an answer for a terminal: it may include `ansi/` (the
# attributes a span carries and the width arithmetic) and itself, and nothing
# else. The logic is kept free of the terminal, the reporter and the command
# line so it is testable as operations with no terminal at all -- the day it
# includes `cli/` or `views/`, the painter's bytes and the renderer's decisions have
# become one thing again.
file(GLOB_RECURSE markdown_sources "${PACKAGE_DIR_markdown}/*.h"
                                   "${PACKAGE_DIR_markdown}/*.cpp")
if(markdown_sources STREQUAL "")
    message(FATAL_ERROR "no sources found under ${PACKAGE_DIR_markdown} — "
                        "this check would pass vacuously")
endif()
foreach(source IN LISTS markdown_sources)
    file(STRINGS "${source}" project_includes REGEX "^[ \t]*#[ \t]*include[ \t]*\"")
    foreach(line IN LISTS project_includes)
        if(NOT line MATCHES "#[ \t]*include[ \t]*\"(markdown|ansi)/")
            get_filename_component(name "${source}" NAME)
            list(APPEND VIOLATIONS "  markdown/${name} reaches past ansi/: ${line}")
        endif()
    endforeach()
endforeach()
if(NOT VIOLATIONS STREQUAL "")
    string(REPLACE ";" "\n" pretty "${VIOLATIONS}")
    message(FATAL_ERROR "the markdown package includes more than ansi/:\n${pretty}\n"
                        "markdown/ may include only ansi/ and itself.")
endif()

# `knowledge/` is a domain core: it may include the chunk store, the loop,
# the harness, the platform seam and itself -- never a surface. The day it
# includes `cli/` or `httpserver/`, the record logic every surface
# shares has grown a dependency on one of them, and the parity between the
# CLI capture, chat's /capture and the HTTP twin stops being structural.
file(GLOB_RECURSE knowledge_sources "${PACKAGE_DIR_knowledge}/*.h"
                                    "${PACKAGE_DIR_knowledge}/*.cpp")
if(knowledge_sources STREQUAL "")
    message(FATAL_ERROR "no sources found under ${PACKAGE_DIR_knowledge} — "
                        "this check would pass vacuously")
endif()
foreach(source IN LISTS knowledge_sources)
    file(STRINGS "${source}" project_includes REGEX "^[ \t]*#[ \t]*include[ \t]*\"")
    foreach(line IN LISTS project_includes)
        if(NOT line MATCHES "#[ \t]*include[ \t]*\"(knowledge|embedstore|agentloop|agent|harness|contracts|platform)/")
            get_filename_component(name "${source}" NAME)
            list(APPEND VIOLATIONS "  knowledge/${name} reaches a surface: ${line}")
        endif()
    endforeach()
endforeach()
if(NOT VIOLATIONS STREQUAL "")
    string(REPLACE ";" "\n" pretty "${VIOLATIONS}")
    message(FATAL_ERROR "the knowledge package includes a surface:\n${pretty}\n"
                        "knowledge/ may include only embedstore/, agentloop/, agent/, harness/, "
                        "contracts/, platform/ and itself.")
endif()

# `graph/` is a domain core like `knowledge/`: the extraction contract and
# the build loop every surface shares. It may include the chunk store, the
# records it materialises, the loop (for the one OUTPUT FORMAT wording and
# `run_structured`), the harness, the platform seam and itself -- never a
# surface, and never a backend: generation and embedding arrive as closures.
file(GLOB_RECURSE graph_sources "${PACKAGE_DIR_graph}/*.h"
                                "${PACKAGE_DIR_graph}/*.cpp")
if(graph_sources STREQUAL "")
    message(FATAL_ERROR "no sources found under ${PACKAGE_DIR_graph} — "
                        "this check would pass vacuously")
endif()
foreach(source IN LISTS graph_sources)
    file(STRINGS "${source}" project_includes REGEX "^[ \t]*#[ \t]*include[ \t]*\"")
    foreach(line IN LISTS project_includes)
        if(NOT line MATCHES "#[ \t]*include[ \t]*\"(graph|embedstore|knowledge|agentloop|agent|harness|contracts|platform)/")
            get_filename_component(name "${source}" NAME)
            list(APPEND VIOLATIONS "  graph/${name} reaches a surface: ${line}")
        endif()
    endforeach()
endforeach()
if(NOT VIOLATIONS STREQUAL "")
    string(REPLACE ";" "\n" pretty "${VIOLATIONS}")
    message(FATAL_ERROR "the graph package includes a surface:\n${pretty}\n"
                        "graph/ may include only embedstore/, knowledge/, agentloop/, agent/, "
                        "harness/, contracts/, platform/ and itself.")
endif()

# `training/` is a domain core like `graph/`: the Python boundary, the kits,
# the synth core and the dataset store every surface shares. It may include
# the harness, the contracts, the platform seam, the loop's closures' types
# and itself -- never a surface, and never a backend: generation arrives as a
# closure. The JSONL framer, `transport/jsonl_framer.h`, is allowed by name;
# the run manifests hash with `contracts/sha256.h`.
file(GLOB_RECURSE training_sources "${PACKAGE_DIR_training}/*.h"
                                   "${PACKAGE_DIR_training}/*.cpp")
if(training_sources STREQUAL "")
    message(FATAL_ERROR "no sources found under ${PACKAGE_DIR_training} — "
                        "this check would pass vacuously")
endif()
foreach(source IN LISTS training_sources)
    file(STRINGS "${source}" project_includes REGEX "^[ \t]*#[ \t]*include[ \t]*\"")
    foreach(line IN LISTS project_includes)
        if(NOT line MATCHES "#[ \t]*include[ \t]*\"(training|harness|contracts|platform|agentloop|agent)/"
           AND NOT line MATCHES "#[ \t]*include[ \t]*\"transport/jsonl_framer\\.h\"")
            get_filename_component(name "${source}" NAME)
            list(APPEND VIOLATIONS "  training/${name} reaches a surface: ${line}")
        endif()
    endforeach()
endforeach()
if(NOT VIOLATIONS STREQUAL "")
    string(REPLACE ";" "\n" pretty "${VIOLATIONS}")
    message(FATAL_ERROR "the training package includes a surface:\n${pretty}\n"
                        "training/ may include only harness/, contracts/, platform/, agentloop/, "
                        "agent/, transport/jsonl_framer.h and itself.")
endif()

# The packages A1 carved, each held to the floor it was carved for.
# `contracts/` is the Data floor every implementor reads its interfaces from:
# it includes the platform seam and itself only -- a convenience that drags
# business logic down into it is the drift the layer exists to prevent.
# `modelstore/` (model files as data) and `transport/` (the HTTP client, the
# SSE parser, the JSONL framer) stand on the contracts and the platform, and
# nothing beside them.
foreach(rule "contracts:contracts|platform"
             "modelstore:modelstore|contracts|platform"
             "transport:transport|contracts|platform")
    string(REPLACE ":" ";" parts "${rule}")
    list(GET parts 0 package)
    list(GET parts 1 allowed)
    file(GLOB_RECURSE carved_sources "${PACKAGE_DIR_${package}}/*.h"
                                     "${PACKAGE_DIR_${package}}/*.cpp")
    if(carved_sources STREQUAL "")
        message(FATAL_ERROR "no sources found under ${PACKAGE_DIR_${package}} — "
                            "this check would pass vacuously")
    endif()
    foreach(source IN LISTS carved_sources)
        file(STRINGS "${source}" project_includes REGEX "^[ \t]*#[ \t]*include[ \t]*\"")
        foreach(line IN LISTS project_includes)
            if(NOT line MATCHES "#[ \t]*include[ \t]*\"(${allowed})/")
                get_filename_component(name "${source}" NAME)
                list(APPEND VIOLATIONS "  ${package}/${name} reaches past ${allowed}: ${line}")
            endif()
        endforeach()
    endforeach()
endforeach()
if(NOT VIOLATIONS STREQUAL "")
    string(REPLACE ";" "\n" pretty "${VIOLATIONS}")
    message(FATAL_ERROR "a carved package includes more than its floor:\n${pretty}\n"
                        "contracts/ may include only platform/ and itself; modelstore/ and "
                        "transport/ only contracts/, platform/ and themselves.")
endif()

# The presentation modules A3 split `commands/` into. `views/` paints -- the
# status line, the answer, the line reader, the terminal adapter -- and never
# parses argv or reaches the commands: it includes itself, `ansi/`,
# `markdown/`, `platform/`, `contracts/` and `agentloop/` (the Reporter seam
# and `ask_user` its adapters implement), never `cli/`, `machine/` or CLI11.
# `machine/` is the machine-mode adapter and never paints: itself,
# `agentloop/`, `agent/` and `contracts/` -- never `views/`, `ansi/` or
# `markdown/`. `cli/`, the composition root, assembles both.
foreach(rule "views:views|ansi|markdown|platform|contracts|agentloop"
             "machine:machine|agentloop|agent|contracts")
    string(REPLACE ":" ";" parts "${rule}")
    list(GET parts 0 package)
    list(GET parts 1 allowed)
    file(GLOB_RECURSE module_sources "${PACKAGE_DIR_${package}}/*.h"
                                     "${PACKAGE_DIR_${package}}/*.cpp")
    if(module_sources STREQUAL "")
        message(FATAL_ERROR "no sources found under ${PACKAGE_DIR_${package}} — "
                            "this check would pass vacuously")
    endif()
    foreach(source IN LISTS module_sources)
        file(STRINGS "${source}" project_includes REGEX "^[ \t]*#[ \t]*include[ \t]*[\"<]")
        foreach(line IN LISTS project_includes)
            if(line MATCHES "#[ \t]*include[ \t]*<CLI/" OR
               (line MATCHES "#[ \t]*include[ \t]*\"" AND
                NOT line MATCHES "#[ \t]*include[ \t]*\"(${allowed})/"))
                get_filename_component(name "${source}" NAME)
                list(APPEND VIOLATIONS "  ${package}/${name} reaches past ${allowed}: ${line}")
            endif()
        endforeach()
    endforeach()
endforeach()
if(NOT VIOLATIONS STREQUAL "")
    string(REPLACE ";" "\n" pretty "${VIOLATIONS}")
    message(FATAL_ERROR "a presentation module reaches past its rule:\n${pretty}\n"
                        "views/ paints and never parses argv: views/, ansi/, markdown/, "
                        "platform/, contracts/, agentloop/ only. machine/ never paints: "
                        "machine/, agentloop/, agent/, contracts/ only.")
endif()

list(LENGTH ALL_SOURCES count)
string(REPLACE ";" ", " guarded "${GUARDED_PACKAGES}")
message(STATUS "layering: ${layered_sources} sources, every include of another module one of "
               "the map's ${edges} links; ${count} across ${guarded} with no backends "
               "include - OK")
