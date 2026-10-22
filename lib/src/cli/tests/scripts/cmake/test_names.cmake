# A test name must survive being handed to Catch2 as a filter argument.
#
# ctest passes each test's NAME to the Catch2 binary as its filter, so the name
# is not just a label -- it is an argument, and two shapes of it break.
#
# **A leading dash** is parsed as an OPTION. The symptom is a test that passes
# when run on its own and fails under ctest with "Unrecognised token" -- which
# reads like a broken test rather than a broken name, and cost real time twice
# before this check existed.
#
# **An embedded double quote** is worse, because it fails silently. The quote
# breaks the shell quoting of the generated command, the filter splits in two,
# neither half matches, and Catch2 prints "No tests ran" and **exits 0**. The
# ctest entry passes forever without executing anything. That is how it was
# found: a gemini-backend test asserting which model answered was silently never
# run, and a mutation breaking exactly what it asserted came back green. The
# test was correct; its name made it unrunnable.
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
    # Every line, since clang-format wraps a long name onto the line after a
    # bare `TEST_CASE(` -- 272 names were laid out that way, and a scan of
    # `TEST_CASE("` alone never saw them (a `--draft ...` name got through,
    # 53i). Such a name's first fragment is its start, which is where a dash
    # does its harm.
    file(STRINGS "${source}" lines)
    set(wrapped FALSE)
    foreach(line IN LISTS lines)
        # Saved FIRST, in the branch that matched: every if(... MATCHES ...)
        # after it resets CMAKE_MATCH_1, so testing it twice in a row silently
        # tests an empty string the second time. That footgun is what let the
        # quote case through on this check's own first draft.
        set(case_found FALSE)
        if(wrapped AND line MATCHES "^[ \t]*\"([^\"]*)\"")
            set(case_name "${CMAKE_MATCH_1}")
            set(case_found TRUE)
        elseif(line MATCHES "TEST_CASE\\(\"([^\"]*)\"")
            set(case_name "${CMAKE_MATCH_1}")
            set(case_found TRUE)
        endif()
        set(wrapped FALSE)
        if(line MATCHES "TEST_CASE\\([ \t]*$")
            set(wrapped TRUE)
        endif()
        if(case_found)
            math(EXPR scanned "${scanned} + 1")

            if(case_name MATCHES "^-")
                get_filename_component(name "${source}" NAME)
                list(APPEND offenders "${name}: leading dash in \"${case_name}\"")
            endif()
            # The capture stops at the first quote, so a name with an escaped
            # quote in it ends with the backslash that escaped it.
            if(case_name MATCHES "\\\\+$")
                get_filename_component(name "${source}" NAME)
                list(APPEND offenders "${name}: embedded quote in \"${case_name}...\"")
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
        "A test name does not survive being passed to Catch2 as a filter:\n  ${pretty}\n"
        "A leading dash is read as an option (rename to \"check --fix ...\" rather than "
        "\"--fix ...\"). An embedded double quote splits the filter so nothing matches, and "
        "Catch2 then exits 0 having run nothing -- rename it without quotes.")
endif()

message(STATUS "test-name check: ${scanned} names, all usable as ctest filters - OK")
