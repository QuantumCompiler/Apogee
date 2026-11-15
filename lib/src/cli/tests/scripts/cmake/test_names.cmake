# A test name must survive being handed to Catch2 as a filter argument.
#
# ctest passes each test's NAME to the Catch2 binary as its filter, so the name
# is not just a label -- it is an argument, and some shapes of it break.
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
# **A square bracket** breaks the registration itself (57x, 2026-11-14).
# catch_discover_tests (Catch2's extras/CatchAddTests.cmake) reads the binary's
# --list-tests output into a CMake list, one name a line, and a CMake list does
# not split on a ';' that comes after an unmatched '[' until a ']' balances it.
# So one name holding "'@['" swallowed every name listed after it into a single
# ctest entry whose filter matched nothing: the binary listed 4,465 cases, ctest
# registered 2,001, and the 2,465 after it never ran under ctest, though every
# run of the binary by tag passed. A lone ']' does the same until a later '['
# balances it. A matched pair registers, but only while every other name
# balances too, so no bracket is allowed. Both discovery modes (a native build's
# POST_BUILD, a cross build's PRE_TEST) run that same parse.
#
# **A backslash** is the other escape that list reads: a name ending in one
# escapes the ';' its line break became, joining it to the next name (neither
# runs), and a "\n" prints the name across two lines, two entries that match
# nothing. A backslash inside a name registers and runs, but in the source it is
# always the start of an escape, so none is allowed -- which covers the quote.
#
# A ';' is allowed: CatchAddTests escapes it before splitting, and a name with
# one -- mid-name, after a backslash, at its end -- registered whole and ran in a
# scratch reproduction against Catch2 v3.7.1; 191 of today's names hold one.
#
# The list rule bit this check's own reading too: file(STRINGS) returns a CMake
# list, so a source line holding an unmatched '[' joined the lines after it and
# the names on them were never scanned -- 3,911 of 4,468 were. So each file is
# read whole, its brackets, semicolons and backslashes swapped for placeholders
# before it is split into lines, a name wrapped over several string literals is
# joined whole, and every `TEST_CASE(` must yield a name the check judged.
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

# Control characters, so no source text can already hold one.
string(ASCII 1 open_bracket)
string(ASCII 2 close_bracket)
string(ASCII 3 semicolon)
string(ASCII 4 backslash)

# The name as it is written in the source, for a message.
function(as_written out text)
    string(REPLACE "${open_bracket}" "[" text "${text}")
    string(REPLACE "${close_bracket}" "]" text "${text}")
    string(REPLACE "${semicolon}" ";" text "${text}")
    string(REPLACE "${backslash}" "\\" text "${text}")
    set(${out} "${text}" PARENT_SCOPE)
endfunction()

set(offenders "")
set(unread "")
set(opened 0)
set(scanned 0)
foreach(source IN LISTS sources)
    get_filename_component(file_name "${source}" NAME)
    file(READ "${source}" text)
    string(REPLACE "\r" "" text "${text}")
    string(REPLACE "[" "${open_bracket}" text "${text}")
    string(REPLACE "]" "${close_bracket}" text "${text}")
    string(REPLACE ";" "${semicolon}" text "${text}")
    string(REPLACE "\\" "${backslash}" text "${text}")
    string(REPLACE "\n" ";" lines "${text}")

    # A name is one or more string literals, the last followed by its comma:
    # inline after `TEST_CASE(`, or -- clang-format wraps a long one onto the
    # lines after a bare `TEST_CASE(` -- one literal a line (272 names were
    # laid out so when a scan of `TEST_CASE("` alone missed a `--draft ...`
    # name, 53i).
    set(collecting FALSE)
    set(pending FALSE)
    set(line_number 0)
    foreach(line IN LISTS lines)
        math(EXPR line_number "${line_number} + 1")
        # Saved FIRST, in the branch that matched: every if(... MATCHES ...)
        # after it resets CMAKE_MATCH_1, so testing it twice in a row silently
        # tests an empty string the second time. That footgun is what let the
        # quote case through on this check's own first draft.
        set(fragment_found FALSE)
        if(line MATCHES "^TEST_CASE\\((.*)$")
            set(opening "${CMAKE_MATCH_1}")
            if(pending)
                list(APPEND unread "${file_name}:${case_line}")
            endif()
            math(EXPR opened "${opened} + 1")
            set(pending TRUE)
            set(case_line ${line_number})
            set(case_name "")
            set(case_quoted FALSE)
            set(collecting FALSE)
            if(opening MATCHES "^[ \t]*$")
                set(collecting TRUE)
            elseif(opening MATCHES "^\"([^\"]*)\"(.*)$")
                set(fragment "${CMAKE_MATCH_1}")
                set(after "${CMAKE_MATCH_2}")
                set(fragment_found TRUE)
            endif()
        elseif(collecting AND line MATCHES "^[ \t]*\"([^\"]*)\"(.*)$")
            set(fragment "${CMAKE_MATCH_1}")
            set(after "${CMAKE_MATCH_2}")
            set(fragment_found TRUE)
        else()
            set(collecting FALSE)
        endif()

        if(fragment_found)
            string(APPEND case_name "${fragment}")
            # The capture stops at the first quote, so a literal with an
            # escaped quote in it ends with the backslash that escaped it -- an
            # odd run of them, since an even one ends in an escaped backslash.
            if(fragment MATCHES "(^|[^${backslash}])${backslash}(${backslash}${backslash})*$")
                set(case_quoted TRUE)
            endif()
            set(collecting FALSE)
            if(after MATCHES "^[ \t]*$" AND NOT case_quoted)
                # The literal ends its line: the name goes on to the next one.
                set(collecting TRUE)
            else()
                set(pending FALSE)
                math(EXPR scanned "${scanned} + 1")
                as_written(shown "${case_name}")
                if(case_name MATCHES "^-")
                    string(APPEND offenders "\n  ${file_name}: leading dash in \"${shown}\"")
                endif()
                if(case_quoted)
                    string(APPEND offenders "\n  ${file_name}: embedded quote in \"${shown}...\"")
                elseif(case_name MATCHES "${backslash}")
                    string(APPEND offenders "\n  ${file_name}: backslash in \"${shown}\"")
                endif()
                if(case_name MATCHES "[${open_bracket}${close_bracket}]")
                    string(APPEND offenders "\n  ${file_name}: square bracket in \"${shown}\"")
                endif()
            endif()
        endif()
    endforeach()
    if(pending)
        list(APPEND unread "${file_name}:${case_line}")
    endif()
endforeach()

if(scanned EQUAL 0)
    message(FATAL_ERROR "test-name check matched no TEST_CASE names -- the regex has rotted")
endif()
if(unread)
    list(LENGTH unread unread_count)
    string(REPLACE ";" "\n  " pretty "${unread}")
    message(FATAL_ERROR
        "test-name check could not read the name of ${unread_count} of ${opened} TEST_CASEs, "
        "so it cannot judge them -- write each name as string literals, the last followed "
        "by its comma:\n  ${pretty}")
endif()

if(offenders)
    message(FATAL_ERROR
        "A test name does not survive being passed to Catch2 as a filter:${offenders}\n"
        "A leading dash is read as an option (rename to \"check --fix ...\" rather than "
        "\"--fix ...\"). An embedded double quote splits the filter so nothing matches, and "
        "Catch2 then exits 0 having run nothing. A square bracket or a backslash breaks "
        "catch_discover_tests' list of names, so the names after it join one ctest entry "
        "that runs nothing -- rename it without them (\"'@' host-split\", not \"'@[' "
        "host-split\").")
endif()

message(STATUS "test-name check: ${scanned} names, all usable as ctest filters - OK")
