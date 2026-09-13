# The HTTP API reference, pinned to the route table.
#
# `documentation/reference/http-api.md` is what a remote-client author reads,
# and a reference that drifts is worse than none: they build against it and
# debug Apogee for a fault that is in the prose. So every route in
# `httpserver/mux.cpp`'s table must have a heading in the document, and every
# route the document promises must be in the table. The same rule
# `cli.machine_schema_conformance` applies to the JSONL protocol.
#
# Refuses to run against an empty list on either side, so it cannot pass
# vacuously.

if(NOT DEFINED MUX_SOURCE OR NOT DEFINED API_DOC)
    message(FATAL_ERROR "MUX_SOURCE and API_DOC must be set")
endif()

set(METHODS "GET|POST|PUT|PATCH|DELETE")

file(STRINGS "${MUX_SOURCE}" table_lines REGEX "\\{\"(${METHODS})\", \"/")
set(CODE_ROUTES "")
foreach(line IN LISTS table_lines)
    if(line MATCHES "\\{\"(${METHODS})\", \"(/[^\"]*)\"")
        list(APPEND CODE_ROUTES "${CMAKE_MATCH_1} ${CMAKE_MATCH_2}")
    endif()
endforeach()

file(STRINGS "${API_DOC}" heading_lines REGEX "^### `(${METHODS}) /")
set(DOC_ROUTES "")
foreach(line IN LISTS heading_lines)
    if(line MATCHES "^### `(${METHODS}) (/[^`]*)`")
        list(APPEND DOC_ROUTES "${CMAKE_MATCH_1} ${CMAKE_MATCH_2}")
    endif()
endforeach()

list(LENGTH CODE_ROUTES code_count)
list(LENGTH DOC_ROUTES doc_count)
if(code_count EQUAL 0)
    message(FATAL_ERROR "no routes found in ${MUX_SOURCE} -- the table's shape changed, or this check is reading the wrong file")
endif()
if(doc_count EQUAL 0)
    message(FATAL_ERROR "no route headings found in ${API_DOC} -- expected lines like '### `POST /v1/chat/completions`'")
endif()

set(PROBLEMS "")
foreach(route IN LISTS CODE_ROUTES)
    if(NOT route IN_LIST DOC_ROUTES)
        list(APPEND PROBLEMS "served but undocumented: ${route}")
    endif()
endforeach()
foreach(route IN LISTS DOC_ROUTES)
    if(NOT route IN_LIST CODE_ROUTES)
        list(APPEND PROBLEMS "documented but not served: ${route}")
    endif()
endforeach()

if(PROBLEMS)
    string(REPLACE ";" "\n  " pretty "${PROBLEMS}")
    message(FATAL_ERROR "the HTTP API reference and the route table disagree:\n  ${pretty}")
endif()

message(STATUS "http-api.md matches the route table: ${code_count} routes - OK")
