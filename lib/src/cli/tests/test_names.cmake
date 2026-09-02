# A test name must not begin with a dash.
#
# ctest passes each test's NAME to the Catch2 binary as its filter argument, so
# a name starting with `-` or `--` is parsed as an OPTION instead. The symptom
# is a test that passes when run on its own and fails under ctest with
# "Unrecognised token" -- which reads like a broken test rather than a broken
# name, and cost real time twice before this check existed.
#
# Cheap, mechanical, and it refuses to run against an empty file list so it
# cannot pass vacuously -- the same rule the layering check follows.

if(NOT DEFINED TEST_SOURCE_DIR)
    message(FATAL_ERROR "TEST_SOURCE_DIR must be set")
endif()

file(GLOB_RECURSE sources "${TEST_SOURCE_DIR}/*.cpp")
list(LENGTH sources source_count)
if(source_count EQUAL 0)
    message(FATAL_ERROR "test-name check found no sources under ${TEST_SOURCE_DIR}")
endif()

set(offenders "")
set(scanned 0)
foreach(source IN LISTS sources)
    file(STRINGS "${source}" lines REGEX "TEST_CASE\\(\"")
    foreach(line IN LISTS lines)
        if(line MATCHES "TEST_CASE\\(\"([^\"]*)\"")
            math(EXPR scanned "${scanned} + 1")
            if(CMAKE_MATCH_1 MATCHES "^-")
                get_filename_component(name "${source}" NAME)
                list(APPEND offenders "${name}: \"${CMAKE_MATCH_1}\"")
            endif()
        endif()
    endforeach()
endforeach()

if(scanned EQUAL 0)
    message(FATAL_ERROR "test-name check matched no TEST_CASE names -- the regex has rotted")
endif()

if(offenders)
    string(REPLACE ";" "\n  " pretty "${offenders}")
    message(FATAL_ERROR
        "A test name starts with a dash, which ctest hands to Catch2 as an option:\n  ${pretty}\n"
        "Rename it so the first character is not '-' (e.g. \"check --fix ...\" rather than \"--fix ...\").")
endif()

message(STATUS "test-name check: ${scanned} names, none start with a dash - OK")
