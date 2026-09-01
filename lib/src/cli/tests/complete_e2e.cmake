# End-to-end test for `apogee complete`, run against the real binary.
#
# This is the pattern every later CLI test reuses: a throwaway APOGEE_HOME, a
# config whose only backend is `mock`, and assertions on stdout/stderr/exit
# code. Fully offline -- no network, no API key, no charges -- which is what
# lets the walking skeleton be exercised on every push.
#
# cmake -P rather than a shell script so it runs on all six targets.

if(NOT DEFINED APOGEE_BIN OR NOT DEFINED APOGEE_WORK_DIR)
    message(FATAL_ERROR "APOGEE_BIN and APOGEE_WORK_DIR must be set")
endif()

file(REMOVE_RECURSE "${APOGEE_WORK_DIR}")
file(MAKE_DIRECTORY "${APOGEE_WORK_DIR}")
set(ENV{APOGEE_HOME} "${APOGEE_WORK_DIR}")

# Every invocation gets an explicit stdin. Without one the child inherits
# ctest's, which may be a pipe that never delivers EOF -- so a command that
# falls through to reading stdin hangs the whole suite instead of failing.
function(apogee_run expected_code)
    cmake_parse_arguments(RUN "" "INPUT_FILE" "" ${ARGN})
    set(stdin_file "${RUN_INPUT_FILE}")
    if(NOT stdin_file)
        set(stdin_file "${APOGEE_WORK_DIR}/.empty-stdin")
        if(NOT EXISTS "${stdin_file}")
            file(WRITE "${stdin_file}" "")
        endif()
    endif()
    set(extra INPUT_FILE "${stdin_file}")
    execute_process(
        COMMAND "${APOGEE_BIN}" ${RUN_UNPARSED_ARGUMENTS}
        RESULT_VARIABLE code
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err
        ${extra}
    )
    # `any-failure` is for a case whose exit code legitimately differs by build
    # configuration -- the only one so far is a local backend, which fails at
    # CONSTRUCTION (a user error) without llama.cpp compiled in and at REQUEST
    # time (a backend error) with it. Pinning either number would make this
    # suite pass in one configuration and fail in the other.
    if(expected_code STREQUAL "any-failure")
        if(code EQUAL 0)
            message(FATAL_ERROR
                "apogee ${RUN_UNPARSED_ARGUMENTS}\n  expected a failure, got exit 0\n"
                "  stdout: ${out}\n  stderr: ${err}")
        endif()
    elseif(NOT code EQUAL expected_code)
        message(FATAL_ERROR
            "apogee ${RUN_UNPARSED_ARGUMENTS}\n  expected exit ${expected_code}, got ${code}\n"
            "  stdout: ${out}\n  stderr: ${err}")
    endif()
    set(APOGEE_OUT "${out}" PARENT_SCOPE)
    set(APOGEE_ERR "${err}" PARENT_SCOPE)
endfunction()

function(expect_contains haystack needle what)
    string(FIND "${haystack}" "${needle}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "${what}: expected to find '${needle}' in:\n${haystack}")
    endif()
endfunction()

function(expect_not_contains haystack needle what)
    string(FIND "${haystack}" "${needle}" found)
    if(NOT found EQUAL -1)
        message(FATAL_ERROR "${what}: expected NOT to find '${needle}' in:\n${haystack}")
    endif()
endfunction()

# --- a config whose only backend is the offline mock ------------------------
apogee_run(0 config init)
apogee_run(0 config add-backend mock --type mock --model mock-1)
apogee_run(0 config set-default mock)

# --- the walking skeleton: config -> harness -> backend -> terminal ---------
apogee_run(0 complete "hello")
expect_contains("${APOGEE_OUT}" "mock response" "a prompt produces an answer")

# Piped output is EXACTLY the answer -- no decoration, no status lines. This is
# what makes `apogee complete ... | jq` work at all.
string(STRIP "${APOGEE_OUT}" stripped)
if(NOT stripped STREQUAL "mock response")
    message(FATAL_ERROR "piped output carried decoration: '${stripped}'")
endif()

# --- stdin ------------------------------------------------------------------
set(PROMPT_FILE "${APOGEE_WORK_DIR}/prompt.txt")
file(WRITE "${PROMPT_FILE}" "from stdin\n")
apogee_run(0 complete INPUT_FILE "${PROMPT_FILE}")
expect_contains("${APOGEE_OUT}" "mock response" "a piped prompt is read from stdin")

# An empty pipe is a user error, not an empty request to the model.
set(EMPTY_FILE "${APOGEE_WORK_DIR}/empty.txt")
file(WRITE "${EMPTY_FILE}" "")
apogee_run(1 complete INPUT_FILE "${EMPTY_FILE}")

# --- flags ------------------------------------------------------------------
apogee_run(0 complete -m mock "x")
apogee_run(0 complete -m mock-1 "x")          # by the entry's model: field
apogee_run(0 complete -s "be brief" -t 0.2 -n 64 "x")
apogee_run(0 complete --context "reference" "x")
apogee_run(0 complete --quiet "x")
apogee_run(0 complete --verbose "x")

# --- exit codes distinguish the failure kinds -------------------------------
# A typo'd -m is a USER error, not a silent fallback to the default backend.
apogee_run(1 complete -m no-such-backend "x")
expect_contains("${APOGEE_ERR}" "no backend named" "a typo'd model is reported")
expect_contains("${APOGEE_ERR}" "mock" "the error lists what IS configured")

# A missing image file is a user error.
apogee_run(1 complete --image "${APOGEE_WORK_DIR}/nope.png" "x")

# An unsupported image type is a user error naming the accepted set.
file(WRITE "${APOGEE_WORK_DIR}/notes.txt" "not an image")
apogee_run(1 complete --image "${APOGEE_WORK_DIR}/notes.txt" "x")
expect_contains("${APOGEE_ERR}" "unsupported image type" "an unsupported type is named")

# --- images -----------------------------------------------------------------
# The whole attachment path -- read the file, base64-encode it, build the IR
# content part, hand it to the provider. The bytes need not be a real PNG: the
# media type comes from the extension, and the mock does not decode. What is
# being asserted is that the path holds together end to end.
file(WRITE "${APOGEE_WORK_DIR}/pixel.png" "pretend-png-bytes")
apogee_run(0 complete --image "${APOGEE_WORK_DIR}/pixel.png" "describe this")
expect_contains("${APOGEE_OUT}" "mock response" "an image attachment reaches the backend")

# Repeatable, and NOT greedy: the positional prompt must survive beside it.
apogee_run(0 complete --image "${APOGEE_WORK_DIR}/pixel.png" --image "${APOGEE_WORK_DIR}/pixel.png" "two images")
expect_contains("${APOGEE_OUT}" "mock response" "--image is repeatable")

# --- an explicitly named backend that cannot build says so ------------------
# The default build has no llama.cpp in it, so a llamacpp backend is configured
# but unavailable. The point of these two assertions is the SECOND one: before
# the capability probe landed, `-m local` fell through to the default backend
# and answered from the mock, which is the worst possible response -- a real
# answer from a model the user did not choose, with nothing to indicate it.
# Two configurations, two legitimate failure points: without llama.cpp compiled
# in the backend cannot be CONSTRUCTED, and with it the GGUF cannot be LOADED.
# Both must fail, both must name the backend, and neither may answer.
apogee_run(0 config add-backend local --type llamacpp --model-path /nonexistent.gguf)
apogee_run(any-failure complete -m local "describe")
expect_contains("${APOGEE_ERR}" "local" "the failing backend is named")
expect_not_contains("${APOGEE_OUT}" "mock response" "it never falls through to another backend")
apogee_run(0 config delete-backend local)

# --- --all-backends ---------------------------------------------------------
apogee_run(0 config add-backend second --type mock --model mock-2)
apogee_run(0 complete --all-backends "x")
expect_contains("${APOGEE_OUT}" "=== mock ===" "each backend is labelled")
expect_contains("${APOGEE_OUT}" "=== second ===" "every backend runs")
apogee_run(0 config delete-backend second)

# --- the agent loop ---------------------------------------------------------
# --tools routes the run through the shared loop. The mock answers without
# calling anything, so this asserts the loop path produces the same clean output
# as the direct path -- no status lines leaking into a piped answer.
apogee_run(0 complete --tools "hi")
string(STRIP "${APOGEE_OUT}" stripped_tools)
if(NOT stripped_tools STREQUAL "mock response")
    message(FATAL_ERROR "--tools leaked decoration into piped output: '${stripped_tools}'")
endif()

# --search enables the provider's own server-side tool where it has one; the
# mock has none and must simply ignore it rather than failing.
apogee_run(0 complete --search "hi")
apogee_run(0 complete --tools --search "hi")

# --- no usable backend ------------------------------------------------------
apogee_run(0 config delete-backend mock)
apogee_run(1 complete "x")
expect_contains("${APOGEE_ERR}" "no usable backend" "an empty config is a user error")

# A configured-but-unbuildable backend explains WHY rather than saying nothing.
apogee_run(0 config add-backend claude --type anthropic)
apogee_run(1 complete "x")
expect_contains("${APOGEE_ERR}" "api_key" "the reason a backend was skipped is surfaced")

file(REMOVE_RECURSE "${APOGEE_WORK_DIR}")
message(STATUS "apogee complete end-to-end: OK")
