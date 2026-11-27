# Non-ASCII text and paths, end to end on the real binary (57j).
#
# On Windows the CLI read argv and the environment in the ANSI code page: a
# profile folder or APOGEE_HOME with an accent made every command exit 1
# ("Cannot convert character sequence"), and `--system "¿Qué tal el café?"`
# reached the model -- and the saved session -- as "\ufffdQu\ufffd tal el
# caf\ufffd?". The executable's manifest now names UTF-8 the code page, and the
# seam reads the environment wide. This drives what a person types: a home
# named with an accent, an accented system prompt and prompt, and an
# attachment whose own name has one -- each run exiting 0 and the session
# file holding the text byte for byte.
#
# cmake -P rather than a shell script so it runs on Windows too, where the
# scripts/sh e2e tests do not -- and where it is the proof that matters. This
# file is UTF-8, and so is every string CMake hands the child.

if(NOT DEFINED APOGEE_BIN OR NOT DEFINED APOGEE_WORK_DIR)
    message(FATAL_ERROR "APOGEE_BIN and APOGEE_WORK_DIR must be set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/sandbox.cmake")
apogee_sandbox_dir(APOGEE_WORK_DIR "${APOGEE_WORK_DIR}")

# The home a user named José has, and Apogee's root inside a folder with an
# accent of its own. The runs below resolve the root through APOGEE_HOME; the
# installers' step at the end unsets it, so the baked rung -- the channel's
# folder under the accented profile, install.ps1's own case -- resolves too.
set(PROFILE "${APOGEE_WORK_DIR}/José")
set(ROOT "${APOGEE_WORK_DIR}/Josè-home")
file(MAKE_DIRECTORY "${PROFILE}")
set(ENV{HOME} "${PROFILE}")
set(ENV{USERPROFILE} "${PROFILE}")
set(ENV{APOGEE_HOME} "${ROOT}")
unset(ENV{ANTHROPIC_API_KEY})
unset(ENV{OPENAI_API_KEY})
unset(ENV{GEMINI_API_KEY})
unset(ENV{GOOGLE_API_KEY})

set(PROMPT "¿Qué tal el café?")

# Every invocation gets an explicit stdin, as complete_e2e's do.
function(apogee_run expected_code)
    cmake_parse_arguments(RUN "" "INPUT_FILE" "" ${ARGN})
    set(stdin_file "${RUN_INPUT_FILE}")
    if(NOT stdin_file)
        set(stdin_file "${APOGEE_WORK_DIR}/.empty-stdin")
        if(NOT EXISTS "${stdin_file}")
            file(WRITE "${stdin_file}" "")
        endif()
    endif()
    # ENCODING UTF-8: the binary writes UTF-8, and a `cmake -P` script leaves
    # CMP0176 unset (OLD), under which Windows decodes a child's output in the
    # console's code page, or the ANSI one with no console -- `résumé.txt`
    # would read as cp437 or cp1252 and the attachment check below fail for a
    # reason that is not the CLI's. Ignored off Windows; since CMake 3.11.
    execute_process(
        COMMAND "${APOGEE_BIN}" ${RUN_UNPARSED_ARGUMENTS}
        RESULT_VARIABLE code
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err
        INPUT_FILE "${stdin_file}"
        ENCODING UTF-8
    )
    if(NOT code EQUAL expected_code)
        message(FATAL_ERROR
            "apogee ${RUN_UNPARSED_ARGUMENTS}\n  expected exit ${expected_code}, got ${code}\n"
            "  stdout: ${out}\n  stderr: ${err}")
    endif()
    set(APOGEE_OUT "${out}" PARENT_SCOPE)
    set(APOGEE_ERR "${err}" PARENT_SCOPE)
endfunction()

function(expect_not_contains haystack needle what)
    string(FIND "${haystack}" "${needle}" found)
    if(NOT found EQUAL -1)
        message(FATAL_ERROR "${what}: expected NOT to find '${needle}' in:\n${haystack}")
    endif()
endfunction()

# The sessions written so far, as a list.
function(sessions out)
    file(GLOB found "${ROOT}/sessions/*.json")
    set(${out} "${found}" PARENT_SCOPE)
endfunction()

# --- the root resolves under the accented folders ---------------------------
# Under `C:\Users\José` the first of these threw, as every command did.
apogee_run(0 config init)
expect_not_contains("${APOGEE_ERR}" "Cannot convert" "an accented root resolves")
if(NOT EXISTS "${ROOT}/config")
    message(FATAL_ERROR "config init wrote nothing under the accented root ${ROOT}")
endif()
apogee_run(0 config add-backend mock --type mock --model mock-1)

# --- typed text reaches the session as typed ---------------------------------
set(LINE_FILE "${APOGEE_WORK_DIR}/line.txt")
file(WRITE "${LINE_FILE}" "hello\n")
apogee_run(0 chat -m mock --system "${PROMPT}" INPUT_FILE "${LINE_FILE}")
sessions(saved)
list(LENGTH saved count)
if(NOT count EQUAL 1)
    message(FATAL_ERROR "expected one session under ${ROOT}/sessions, found ${count}: ${saved}")
endif()
file(READ "${saved}" session)
string(JSON system_prompt GET "${session}" params system_prompt)
if(NOT system_prompt STREQUAL PROMPT)
    message(FATAL_ERROR "the session's system_prompt is '${system_prompt}', not '${PROMPT}'")
endif()
string(JSON first GET "${session}" messages 0 content)
if(NOT first STREQUAL PROMPT)
    message(FATAL_ERROR "the session's system message is '${first}', not '${PROMPT}'")
endif()
# U+FFFD, the replacement the ANSI code page's bytes were mended into.
expect_not_contains("${session}" "�" "no character was replaced")

# --- a positional prompt, and a path named with accents ----------------------
apogee_run(0 complete "${PROMPT}")
string(STRIP "${APOGEE_OUT}" answer)
if(NOT answer STREQUAL "mock response")
    message(FATAL_ERROR "complete answered '${answer}'")
endif()

set(ATTACHMENT "${APOGEE_WORK_DIR}/résumé.txt")
file(WRITE "${ATTACHMENT}" "notes about the café\n")
apogee_run(0 chat -m mock --attach "${ATTACHMENT}" INPUT_FILE "${LINE_FILE}")
set(said "${APOGEE_OUT}${APOGEE_ERR}")
string(FIND "${said}" "attached" attached)
string(FIND "${said}" "résumé.txt" named)
if(attached EQUAL -1 OR named EQUAL -1)
    message(FATAL_ERROR "the accented attachment was not attached by name:\n${said}")
endif()

# --- the installers' own step ------------------------------------------------
# install.ps1 runs `check --fix` on a fresh install with no APOGEE_HOME, and
# throws "post-install check failed" when it does not pass -- under
# `C:\Users\José`, on every install. So APOGEE_HOME goes: the root is the baked
# rung, the channel's folder under the accented profile (HOME and USERPROFILE
# above, still inside this run's sandbox).
unset(ENV{APOGEE_HOME})
apogee_run(0 check --fix)
expect_not_contains("${APOGEE_OUT}${APOGEE_ERR}" "Cannot convert" "check resolves the root")
file(GLOB baked LIST_DIRECTORIES true "${PROFILE}/.apogee*")
list(LENGTH baked count)
if(NOT count EQUAL 1 OR NOT IS_DIRECTORY "${baked}/config")
    message(FATAL_ERROR
        "check --fix seeded no root under the accented profile ${PROFILE}: '${baked}'")
endif()

file(REMOVE_RECURSE "${APOGEE_WORK_DIR}")
message(STATUS "non-ASCII homes, prompts and paths end-to-end: OK")
