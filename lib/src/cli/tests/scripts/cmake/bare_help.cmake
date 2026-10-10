# A bare `apogee` off a terminal is the help, byte for byte (32b).
#
# The full-screen shell opens only when stdin and stdout are both terminals;
# a pipe on either side -- a script, CI, `apogee | cat` -- prints exactly the
# help the root always printed. Pinned three ways: bare with stdin from a file
# and stdout captured, `--help`, and the golden checked in beside the suite,
# which moves only when the root's commands or flags do.
#
# Inputs:
#   APOGEE    the executable
#   GOLDEN    the root help as committed
#   WORK_DIR  a scratch directory for the empty stdin

foreach(required APOGEE GOLDEN WORK_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "bare_help: ${required} must be set")
    endif()
endforeach()

file(MAKE_DIRECTORY "${WORK_DIR}")
set(empty_stdin "${WORK_DIR}/empty-stdin")
file(WRITE "${empty_stdin}" "")

execute_process(COMMAND "${APOGEE}" INPUT_FILE "${empty_stdin}"
                OUTPUT_VARIABLE bare ERROR_VARIABLE bare_err RESULT_VARIABLE bare_code)
execute_process(COMMAND "${APOGEE}" --help INPUT_FILE "${empty_stdin}"
                OUTPUT_VARIABLE help ERROR_VARIABLE help_err RESULT_VARIABLE help_code)
file(READ "${GOLDEN}" golden)

# Windows' text-mode stdout says each newline as CRLF; the help is the words.
foreach(text bare help golden)
    string(REPLACE "\r\n" "\n" ${text} "${${text}}")
endforeach()

if(NOT bare_code EQUAL 0 OR NOT help_code EQUAL 0)
    message(FATAL_ERROR "bare_help: exit ${bare_code} bare, ${help_code} with --help")
endif()
if(NOT bare_err STREQUAL "")
    message(FATAL_ERROR "bare_help: a bare run off a terminal said something on stderr:\n${bare_err}")
endif()
if(NOT bare STREQUAL help)
    message(FATAL_ERROR "bare_help: a bare run printed something other than --help:\n${bare}")
endif()
if(NOT bare STREQUAL golden)
    message(FATAL_ERROR
        "bare_help: the root help is not the committed one (${GOLDEN}). When the root's "
        "commands or flags changed on purpose, regenerate it with `apogee --help`; "
        "this run printed:\n${bare}")
endif()
message(STATUS "bare_help: bare == --help == the committed help")
