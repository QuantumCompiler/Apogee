# Layer context check (Architecture A5): the law next to the code, held true.
#
# Each layer root, in source/ and in tests/, carries a short CLAUDE.md -- a
# card a model reads for free whenever it opens a file in that layer -- that
# states the layer's law in brief and links the ADRs for depth
# (lib/documentation/adrs/cli/). A card is only worth having while it is
# right, and an ADR's statement of the law only while it matches the law the
# build enforces. So this check holds four things to the module map,
# cmake/modules.cmake, which the build and harness.layering already read:
#
#   the cards    -- all eight present, each within its budget (a card that
#                   restates DEVELOPER.md does not fit), each linking ADR 0001,
#                   every relative link resolving, and a source card's
#                   **Modules:** line naming exactly its layer's modules;
#   the mirror   -- every tests/<layer>/<dir> is a module of that layer, and
#                   every module has its test directory (ADR 0004), `version`
#                   excepted by name: the root smoke test covers it;
#   the index    -- every ADR the index lists exists, numbered in order, with
#                   its title, status and date; every ADR file is indexed;
#   the law      -- ADR 0001's numbered layers are the map's, lowest first, and
#                   its rule sentence stands; ADR 0008's table is the map --
#                   each layer's modules and what it may depend on.
#
# Driven with `cmake -P`, like the layering check, so it runs on every target.

foreach(input APOGEE_CLI_DIR APOGEE_ADR_DIR APOGEE_MODULE_MAP)
    if(NOT DEFINED ${input})
        message(FATAL_ERROR "${input} must be set")
    endif()
endforeach()
include("${APOGEE_MODULE_MAP}")

set(CARD_MAX_LINES 30)
set(CARD_MAX_BYTES 3000)
# A module whose tests live elsewhere, by name: `version` is the root smoke
# test's subject (the stamp, the linked dependencies).
set(UNMIRRORED_MODULES version)
set(UNMIRRORED_COVER_version "${APOGEE_CLI_DIR}/tests/smoke_test.cpp")

set(VIOLATIONS "")
# One violation, its arguments concatenated one by one -- `${ARGN}` would split
# a list's items (and a literal semicolon) into separate pieces. Lists read as
# "a, b"; the semicolons go, so the message is one item of VIOLATIONS.
function(violation)
    set(message "  ")
    math(EXPR last "${ARGC} - 1")
    foreach(index RANGE ${last})
        string(APPEND message "${ARGV${index}}")
    endforeach()
    string(REPLACE "; " ", " message "${message}")
    string(REPLACE ";" ", " message "${message}")
    set(VIOLATIONS ${VIOLATIONS} "${message}" PARENT_SCOPE)
endfunction()

# The names in backticks in `text`, sorted.
function(backticked out_var text)
    string(REGEX MATCHALL "`[a-z_]+`" found "${text}")
    set(names "")
    foreach(item IN LISTS found)
        string(REPLACE "`" "" item "${item}")
        list(APPEND names ${item})
    endforeach()
    list(SORT names)
    set(${out_var} "${names}" PARENT_SCOPE)
endfunction()

function(sorted out_var)
    set(items ${ARGN})
    list(SORT items)
    set(${out_var} "${items}" PARENT_SCOPE)
endfunction()

# ---- The cards ------------------------------------------------------------------
foreach(layer IN LISTS APOGEE_LAYERS)
    foreach(tree source tests)
        set(card "${APOGEE_CLI_DIR}/${tree}/${layer}/CLAUDE.md")
        set(name "${tree}/${layer}/CLAUDE.md")
        if(NOT EXISTS "${card}")
            violation("${name} is missing -- every layer root carries its card (A5)")
            continue()
        endif()
        file(READ "${card}" text)
        file(STRINGS "${card}" lines ENCODING UTF-8)
        string(REGEX MATCHALL "\n" newlines "${text}")
        list(LENGTH newlines line_count)
        string(LENGTH "${text}" byte_count)
        if(line_count GREATER CARD_MAX_LINES OR byte_count GREATER CARD_MAX_BYTES)
            violation("${name} is ${line_count} lines, ${byte_count} bytes -- a card is at most "
                      "${CARD_MAX_LINES} lines and ${CARD_MAX_BYTES} bytes: it summarizes and "
                      "links, the ADRs and DEVELOPER.md hold the rest")
        endif()
        if(NOT text MATCHES "adrs/cli/layer-enforcement\\.md")
            violation("${name} does not link ADR 0001 (layer-enforcement.md)")
        endif()
        # Each link target, `](` swapped for a marker first: a `]` in a list
        # item is a bracket to CMake, and the matches would run together.
        string(REPLACE "](" "@LINK@(" marked "${text}")
        string(REGEX MATCHALL "@LINK@\\([^)#]+" links "${marked}")
        foreach(link IN LISTS links)
            string(REPLACE "@LINK@(" "" target "${link}")
            if(target MATCHES "^[a-z]+:")
                continue()
            endif()
            get_filename_component(resolved "${APOGEE_CLI_DIR}/${tree}/${layer}/${target}" ABSOLUTE)
            if(NOT EXISTS "${resolved}")
                violation("${name} links ${target}, which does not exist")
            endif()
        endforeach()
        if(tree STREQUAL "source")
            set(modules_line "")
            foreach(line IN LISTS lines)
                if(line MATCHES "^\\*\\*Modules:\\*\\*")
                    set(modules_line "${line}")
                endif()
            endforeach()
            # The names before the first " -- ": what follows is prose.
            string(FIND "${modules_line}" " -- " cut)
            if(modules_line STREQUAL "" OR cut EQUAL -1)
                violation("${name} has no **Modules:** line naming its modules")
            else()
                string(SUBSTRING "${modules_line}" 0 ${cut} named)
                backticked(card_modules "${named}")
                sorted(map_modules ${APOGEE_LAYER_${layer}})
                if(NOT card_modules STREQUAL map_modules)
                    violation("${name} names [${card_modules}] but cmake/modules.cmake puts "
                              "[${map_modules}] in ${layer}")
                endif()
            endif()
        endif()
    endforeach()
endforeach()

# ---- The mirror (ADR 0004) ------------------------------------------------------
foreach(layer IN LISTS APOGEE_LAYERS)
    file(GLOB entries LIST_DIRECTORIES true "${APOGEE_CLI_DIR}/tests/${layer}/*")
    set(test_dirs "")
    foreach(entry IN LISTS entries)
        if(IS_DIRECTORY "${entry}")
            get_filename_component(dir "${entry}" NAME)
            list(APPEND test_dirs ${dir})
            if(NOT dir IN_LIST APOGEE_LAYER_${layer})
                violation("tests/${layer}/${dir}/ mirrors no ${layer} module -- a test directory "
                          "sits in the layer of what it tests")
            endif()
        endif()
    endforeach()
    foreach(module IN LISTS APOGEE_LAYER_${layer})
        if(module IN_LIST test_dirs)
            continue()
        endif()
        if(module IN_LIST UNMIRRORED_MODULES)
            if(NOT EXISTS "${UNMIRRORED_COVER_${module}}")
                violation("'${module}' has no test directory and its named cover, "
                          "${UNMIRRORED_COVER_${module}}, is gone")
            endif()
            continue()
        endif()
        violation("the module '${module}' has no tests/${layer}/${module}/ -- a new module "
                  "brings its test directory in the same change")
    endforeach()
endforeach()

# ---- The index ------------------------------------------------------------------
set(index "${APOGEE_ADR_DIR}/README.md")
if(NOT EXISTS "${index}")
    message(FATAL_ERROR "no ADR index at ${index}")
endif()
file(STRINGS "${index}" rows ENCODING UTF-8 REGEX "^\\| \\[[0-9][0-9][0-9][0-9]\\]\\(")
set(indexed "")
set(expected 1)
foreach(row IN LISTS rows)
    string(REGEX REPLACE "^\\| \\[([0-9]+)\\]\\(([^)]+)\\).*" "\\1;\\2" parts "${row}")
    list(GET parts 0 number)
    list(GET parts 1 file)
    list(APPEND indexed ${file})
    math(EXPR as_int "${number}")
    if(NOT as_int EQUAL expected)
        violation("the ADR index lists ${number} where ${expected} comes next")
    endif()
    math(EXPR expected "${as_int} + 1")
    set(record "${APOGEE_ADR_DIR}/${file}")
    if(NOT EXISTS "${record}")
        violation("the ADR index lists ${number} as ${file}, which does not exist")
        continue()
    endif()
    file(STRINGS "${record}" head ENCODING UTF-8 LIMIT_COUNT 3)
    list(GET head 0 title)
    if(NOT title MATCHES "^# ADR ${number} — ")
        violation("${file} is indexed as ${number} but titled '${title}'")
    endif()
    file(READ "${record}" body)
    if(NOT body MATCHES "\\*\\*Status:\\*\\* [^\n]+\\*\\*Date:\\*\\* 20[0-9][0-9]-[0-9][0-9]-[0-9][0-9]")
        violation("${file} has no **Status:** and **Date:** line")
    endif()
endforeach()
if(indexed STREQUAL "")
    message(FATAL_ERROR "the ADR index lists nothing -- this check would pass vacuously")
endif()
file(GLOB records RELATIVE "${APOGEE_ADR_DIR}" "${APOGEE_ADR_DIR}/*.md")
foreach(record IN LISTS records)
    if(NOT record STREQUAL "README.md" AND NOT record IN_LIST indexed)
        violation("${record} is an ADR the index does not list")
    endif()
endforeach()

# ---- The law: ADR 0001 and ADR 0008 against the map ------------------------------
set(adr_0001 "${APOGEE_ADR_DIR}/layer-enforcement.md")
file(STRINGS "${adr_0001}" numbered ENCODING UTF-8 REGEX "^[1-9]\\. \\*\\*[A-Za-z]+\\*\\*")
set(stated "")
foreach(line IN LISTS numbered)
    string(REGEX REPLACE "^[1-9]\\. \\*\\*([A-Za-z]+)\\*\\*.*" "\\1" layer "${line}")
    string(TOLOWER "${layer}" layer)
    list(APPEND stated ${layer})
endforeach()
if(NOT stated STREQUAL APOGEE_LAYERS)
    violation("ADR 0001 numbers the layers [${stated}], lowest first; cmake/modules.cmake has "
              "[${APOGEE_LAYERS}]")
endif()
file(READ "${adr_0001}" text)
if(NOT text MATCHES "A package may depend only on packages in its own layer or in layers below it\\.")
    violation("ADR 0001 no longer states the rule: \"A package may depend only on packages in its "
              "own layer or in layers below it.\" -- the rule the link policy enforces")
endif()

set(adr_0008 "${APOGEE_ADR_DIR}/module-map.md")
set(tabled "")
if(NOT EXISTS "${adr_0008}")
    violation("ADR 0008 (module-map.md), the map's table, is missing")
else()
    file(STRINGS "${adr_0008}" table ENCODING UTF-8 REGEX "^\\| [A-Z][a-z]+ \\| `")
    foreach(row IN LISTS table)
        string(REGEX REPLACE "^\\| ([A-Za-z]+) \\| ([^|]+) \\| ([^|]+) \\|$" "\\1;\\2;\\3" cells "${row}")
        list(LENGTH cells cell_count)
        if(NOT cell_count EQUAL 3)
            violation("ADR 0008's table row '${row}' is not | layer | modules | may depend on |")
            continue()
        endif()
        list(GET cells 0 layer)
        list(GET cells 1 module_cell)
        list(GET cells 2 depends_cell)
        string(TOLOWER "${layer}" layer)
        if(NOT layer IN_LIST APOGEE_LAYERS)
            violation("ADR 0008's table names a layer '${layer}' the map does not have")
            continue()
        endif()
        list(APPEND tabled ${layer})
        backticked(table_modules "${module_cell}")
        sorted(map_modules ${APOGEE_LAYER_${layer}})
        if(NOT table_modules STREQUAL map_modules)
            violation("ADR 0008 puts [${table_modules}] in ${layer}; cmake/modules.cmake puts "
                      "[${map_modules}]")
        endif()
        string(TOLOWER "${depends_cell}" depends_cell)
        string(REGEX MATCHALL "[a-z]+" table_depends "${depends_cell}")
        list(SORT table_depends)
        set(allowed "")
        foreach(other IN LISTS APOGEE_LAYERS)
            if(NOT APOGEE_LAYER_RANK_${other} GREATER APOGEE_LAYER_RANK_${layer})
                list(APPEND allowed ${other})
            endif()
        endforeach()
        list(SORT allowed)
        if(NOT table_depends STREQUAL allowed)
            violation("ADR 0008 says ${layer} may depend on [${table_depends}]; the rule gives "
                      "[${allowed}] -- its own layer and every layer below")
        endif()
    endforeach()
    sorted(tabled_sorted ${tabled})
    sorted(layers_sorted ${APOGEE_LAYERS})
    if(NOT tabled_sorted STREQUAL layers_sorted)
        violation("ADR 0008's table has rows for [${tabled}]; the map's layers are "
                  "[${APOGEE_LAYERS}]")
    endif()
endif()

if(NOT VIOLATIONS STREQUAL "")
    list(JOIN VIOLATIONS "\n" pretty)
    message(FATAL_ERROR "the layer context has drifted from the map:\n${pretty}\n"
                        "The cards and the ADRs state the law cmake/modules.cmake declares; "
                        "change them in the same change as the map.")
endif()
list(LENGTH indexed adr_count)
message(STATUS "layer context: 8 cards, the test tree mirroring the modules, ${adr_count} ADRs "
               "indexed, ADR 0001 and ADR 0008 matching cmake/modules.cmake - OK")
