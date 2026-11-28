# A finished run's notes are said on stderr in the house voice (CLI-M58).
#
# stdout is a run's result -- its outcome, a listing, a count, a read's
# qualifier, a dry run's prediction, next-step advice -- and every other note
# about the run is one stderr line, `apogee <verb>: <words>`, written by the
# one writer (`cli/notes.h`'s `say_note`). Before CLI-M58 fourteen notes of
# `graph build`/`update`/`communities`, `embed ingest`, `knowledge capture`/
# `reindex` and `agents delete` were written on stdout in five voices --
# `[graph] `, `warning:`, `Warning:`, `Note:`, `(could not ...)` -- and the
# Mac app, which keeps one stdout line and the house-voice stderr lines,
# lost them or took them for the outcome.
#
# The per-site tests are the guard; this is the backstop for the two voices a
# literal shows. Under `presentation/cli`, a `std::cout` statement -- from
# `std::cout <<` to the `;` that ends it, over as many lines as it runs -- may
# hold no string literal that starts with
#   - a tag, `[<lowercase word>] ` (`"[graph] source tree "`) -- so a read's
#     `[Knowledge graph: ...]` header, capitalised, is not one;
#   - or `warning:` / `Warning:`;
# unless the line is on the list below, each entry its file, a piece of the
# line (or a whole file) and its reason. A note written from a variable
# (`std::cout << decision.note`) is invisible to it -- that is what the
# per-site tests hold. A `std::ostream&` bound to stdout is invisible too.
#
# Each file is read whole, its brackets, semicolons and backslashes swapped
# for placeholders before it is split into lines (test_names.cmake's lesson:
# a CMake list splits on `;` and holds an unmatched `[`). Comment lines are
# exempt, so documentation can name what the rule forbids.
#
# It refuses to pass vacuously: it fails when it finds no source, when it
# never sees a `std::cout` statement, and when a list entry names a file that
# is gone or matches no line the rule flags. Every problem is reported in one
# failure, the lines that broke the rule first, so a run over a planted tree
# (`cli.no_stdout_notes_planted`) names the planted file and line.

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR must be set")
endif()

file(GLOB sources "${SOURCE_DIR}/presentation/cli/*.cpp" "${SOURCE_DIR}/presentation/cli/*.h")
list(LENGTH sources source_count)
if(source_count EQUAL 0)
    message(FATAL_ERROR "no-stdout-notes check found no sources under "
                        "${SOURCE_DIR}/presentation/cli")
endif()

# Control characters, so no source text can already hold one.
string(ASCII 1 open_bracket)
string(ASCII 2 close_bracket)
string(ASCII 3 semicolon)
string(ASCII 4 backslash)

function(as_read out text)
    string(REPLACE "[" "${open_bracket}" text "${text}")
    string(REPLACE "]" "${close_bracket}" text "${text}")
    string(REPLACE ";" "${semicolon}" text "${text}")
    string(REPLACE "\\" "${backslash}" text "${text}")
    set(${out} "${text}" PARENT_SCOPE)
endfunction()

function(as_written out text)
    string(REPLACE "${open_bracket}" "[" text "${text}")
    string(REPLACE "${close_bracket}" "]" text "${text}")
    string(REPLACE "${semicolon}" ";" text "${text}")
    string(REPLACE "${backslash}" "\\" text "${text}")
    set(${out} "${text}" PARENT_SCOPE)
endfunction()

# Whole files: every line the rule flags in them is allowed. Two strings an
# entry: the file and the reason. None since CLI-M60, which moved the models
# family's notes -- `models_pull.cpp`, listed whole until then -- onto the
# writer.
set(allowed_files "")
# Lines: three strings an entry -- the file, a piece of the line, the reason.
set(allowed_lines
    "presentation/cli/knowledge.cpp" "\"Warning: a real capture would fail -- \""
    "knowledge capture --dry-run's prediction of what a real run would do: its result"
)

list(LENGTH allowed_files files_length)
math(EXPR files_remainder "${files_length} % 2")
list(LENGTH allowed_lines lines_length)
math(EXPR lines_remainder "${lines_length} % 3")
if(NOT files_remainder EQUAL 0 OR NOT lines_remainder EQUAL 0)
    message(FATAL_ERROR "the allow-list is malformed: a whole file is two strings (the file, the "
                        "reason), a line three (the file, a piece of the line, the reason)")
endif()

set(problems "")
set(allowed_file_names "")
set(file_entry 0)
math(EXPR files_last "${files_length} - 1")
# An empty list has no range to walk (`foreach(RANGE 0 -1)` is an error).
if(files_length GREATER 0)
    foreach(at RANGE 0 ${files_last} 2)
        list(GET allowed_files ${at} entry_file)
        list(APPEND allowed_file_names "${entry_file}")
        if(NOT EXISTS "${SOURCE_DIR}/${entry_file}")
            list(APPEND problems "the allow-list names ${entry_file}, which is not under "
                                 "${SOURCE_DIR} -- a file that moved must move here too")
        endif()
        set(file_matched_${file_entry} FALSE)
        math(EXPR file_entry "${file_entry} + 1")
    endforeach()
endif()
set(line_entry 0)
math(EXPR lines_last "${lines_length} - 1")
foreach(at RANGE 0 ${lines_last} 3)
    list(GET allowed_lines ${at} entry_file)
    if(NOT EXISTS "${SOURCE_DIR}/${entry_file}")
        list(APPEND problems "the allow-list names ${entry_file}, which is not under ${SOURCE_DIR} "
                             "-- a file that moved must move here too")
    endif()
    set(line_matched_${line_entry} FALSE)
    math(EXPR line_entry "${line_entry} + 1")
endforeach()

# Whether `line` (placeholders in) of `relative` is allowed; marks the entry.
macro(check_allowed relative line result)
    set(${result} FALSE)
    list(FIND allowed_file_names "${relative}" _file_at)
    if(NOT _file_at EQUAL -1)
        set(${result} TRUE)
        set(file_matched_${_file_at} TRUE)
    endif()
    set(_entry 0)
    foreach(_at RANGE 0 ${lines_last} 3)
        list(GET allowed_lines ${_at} _file)
        if(_file STREQUAL "${relative}")
            math(EXPR _piece_at "${_at} + 1")
            list(GET allowed_lines ${_piece_at} _piece)
            as_read(_piece "${_piece}")
            string(FIND "${line}" "${_piece}" _found)
            if(NOT _found EQUAL -1)
                set(${result} TRUE)
                set(line_matched_${_entry} TRUE)
            endif()
        endif()
        math(EXPR _entry "${_entry} + 1")
    endforeach()
endmacro()

# A literal's opening quote, never an escaped one inside another literal.
set(opening "(^|[^${backslash}])\"")
set(tag_literal "${opening}${open_bracket}[a-z]+${close_bracket} ")
set(warning_literal "${opening}[Ww]arning:")

set(offenders "")
set(statements 0)
foreach(source IN LISTS sources)
    file(RELATIVE_PATH relative "${SOURCE_DIR}" "${source}")
    file(READ "${source}" text)
    string(REPLACE "\r" "" text "${text}")
    as_read(text "${text}")
    string(REPLACE "\n" ";" lines "${text}")

    set(in_statement FALSE)
    set(line_number 0)
    foreach(line IN LISTS lines)
        math(EXPR line_number "${line_number} + 1")
        string(STRIP "${line}" stripped)
        if(stripped MATCHES "^(//|/\\*|\\*)")
            continue()
        endif()
        if(line MATCHES "std::cout[ \t]*<<")
            set(in_statement TRUE)
            math(EXPR statements "${statements} + 1")
        endif()
        if(NOT in_statement)
            continue()
        endif()
        if(line MATCHES "${tag_literal}" OR line MATCHES "${warning_literal}")
            check_allowed("${relative}" "${line}" is_allowed)
            if(NOT is_allowed)
                list(APPEND offenders "${relative}:${line_number}: ${stripped}")
            endif()
        endif()
        if(line MATCHES "${semicolon}")
            set(in_statement FALSE)
        endif()
    endforeach()
endforeach()

if(statements EQUAL 0)
    list(APPEND problems "the check never saw a `std::cout <<` statement -- its matcher found "
                         "nothing, so it would pass on anything")
endif()
set(file_entry 0)
if(files_length GREATER 0)
    foreach(at RANGE 0 ${files_last} 2)
        list(GET allowed_files ${at} entry_file)
        if(EXISTS "${SOURCE_DIR}/${entry_file}" AND NOT file_matched_${file_entry})
            list(APPEND problems "the allow-list exempts ${entry_file}, where the rule flags no "
                                 "line -- remove the entry")
        endif()
        math(EXPR file_entry "${file_entry} + 1")
    endforeach()
endif()
set(line_entry 0)
foreach(at RANGE 0 ${lines_last} 3)
    list(GET allowed_lines ${at} entry_file)
    math(EXPR piece_at "${at} + 1")
    list(GET allowed_lines ${piece_at} entry_piece)
    if(EXISTS "${SOURCE_DIR}/${entry_file}" AND NOT line_matched_${line_entry})
        list(APPEND problems "the allow-list exempts ${entry_file}: ${entry_piece}, a line the "
                             "rule no longer flags -- remove the entry, or move it with its line")
    endif()
    math(EXPR line_entry "${line_entry} + 1")
endforeach()

if(offenders OR problems)
    set(report "")
    if(offenders)
        list(LENGTH offenders offender_count)
        string(REPLACE ";" "\n  " pretty "${offenders}")
        as_written(pretty "${pretty}")
        string(APPEND report
            "A finished run's note is written on stdout, on ${offender_count} line(s):\n"
            "  ${pretty}\n"
            "stdout is the run's result. Say a note with say_note (cli/notes.h): one stderr line, "
            "`apogee <verb>: <words>`, the words as they were with the tag dropped and "
            "`Warning:` lower-cased. A line that IS the result -- a read's qualifier, a listing, "
            "a dry run's prediction, next-step advice, an outcome -- goes on the list in this "
            "file WITH a reason.\n")
    endif()
    if(problems)
        string(REPLACE ";" "\n  " pretty "${problems}")
        as_written(pretty "${pretty}")
        string(APPEND report "The check's own list or reading is wrong:\n  ${pretty}\n")
    endif()
    message(FATAL_ERROR "${report}")
endif()

message(STATUS "no-stdout-notes check: ${source_count} files, ${statements} std::cout "
               "statements, no note written on stdout - OK")
