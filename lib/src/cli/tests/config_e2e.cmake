# End-to-end test for the `apogee config` command family.
#
# Driven with `cmake -P` rather than a shell script so it runs identically on
# all six targets -- a .sh here would silently skip on the Windows runners,
# which is where a path or line-ending bug would actually show up.
#
# It exercises the real binary through a full lifecycle: init, add, set a role,
# read values back, delete, and confirm the file returned to its initial bytes.
# Everything lands under a throwaway APOGEE_HOME, so the suite stays hermetic.

if(NOT DEFINED APOGEE_BIN OR NOT DEFINED APOGEE_WORK_DIR)
    message(FATAL_ERROR "APOGEE_BIN and APOGEE_WORK_DIR must be set")
endif()

file(REMOVE_RECURSE "${APOGEE_WORK_DIR}")
file(MAKE_DIRECTORY "${APOGEE_WORK_DIR}")

set(CONFIG_FILE "${APOGEE_WORK_DIR}/config/config.yaml")

# Runs the binary and fails the test unless the exit code is as expected.
function(apogee_run expected_code)
    execute_process(
        COMMAND "${APOGEE_BIN}" ${ARGN}
        RESULT_VARIABLE code
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err
    )
    if(NOT code EQUAL expected_code)
        message(FATAL_ERROR
            "apogee ${ARGN}\n  expected exit ${expected_code}, got ${code}\n"
            "  stdout: ${out}\n  stderr: ${err}")
    endif()
    set(APOGEE_OUT "${out}" PARENT_SCOPE)
endfunction()

function(expect_equal actual expected what)
    string(STRIP "${actual}" actual_stripped)
    if(NOT actual_stripped STREQUAL expected)
        message(FATAL_ERROR "${what}: expected '${expected}', got '${actual_stripped}'")
    endif()
endfunction()

set(ENV{APOGEE_HOME} "${APOGEE_WORK_DIR}")

# --- init ------------------------------------------------------------------
apogee_run(0 config path)
expect_equal("${APOGEE_OUT}" "${CONFIG_FILE}" "config path")

apogee_run(0 config init)
if(NOT EXISTS "${CONFIG_FILE}")
    message(FATAL_ERROR "config init did not create ${CONFIG_FILE}")
endif()

# A second init must refuse rather than clobber the user's file.
apogee_run(1 config init)
apogee_run(0 config init --force)

file(READ "${CONFIG_FILE}" PRISTINE)

# A freshly initialised config -- no keys, no models -- must load.
apogee_run(0 config get backends)
expect_equal("${APOGEE_OUT}" "" "a fresh config has no backends")

# --- add-backend -----------------------------------------------------------
apogee_run(0 config add-backend claude --type anthropic
           --api-key "\${ANTHROPIC_API_KEY}" --model claude-sonnet-5 --context-size 200000)

apogee_run(0 config get backends)
expect_equal("${APOGEE_OUT}" "claude" "the added backend is listed")

apogee_run(0 config get backends.claude.model)
expect_equal("${APOGEE_OUT}" "claude-sonnet-5" "get a nested key")

apogee_run(0 config get backends.claude.context_size)
expect_equal("${APOGEE_OUT}" "200000" "get a numeric key")

# The secret stays a literal on disk and is redacted on the way out.
file(READ "${CONFIG_FILE}" WITH_BACKEND)
if(NOT WITH_BACKEND MATCHES "\\\${ANTHROPIC_API_KEY}")
    message(FATAL_ERROR "the api_key was not stored literally")
endif()
apogee_run(0 config get backends.claude.api_key)
if(APOGEE_OUT MATCHES "sk-")
    message(FATAL_ERROR "config get leaked an api_key without --reveal")
endif()

# Duplicates and case-collisions are refused.
apogee_run(1 config add-backend claude --type mock)
apogee_run(1 config add-backend CLAUDE --type mock)

# --- roles -----------------------------------------------------------------
apogee_run(0 config set-default claude)
apogee_run(0 config get models.default)
expect_equal("${APOGEE_OUT}" "claude" "models.default")

apogee_run(0 config set-default-extraction claude)
apogee_run(0 config get models.default_extraction)
expect_equal("${APOGEE_OUT}" "claude" "models.default_extraction")

# A role must name a backend that exists.
apogee_run(1 config set-default no-such-backend)

# --- errors ----------------------------------------------------------------
apogee_run(1 config get no.such.key)
apogee_run(1 config delete-backend no-such-backend)

# --- format is idempotent --------------------------------------------------
apogee_run(0 config format)
file(READ "${CONFIG_FILE}" FORMATTED_ONCE)
apogee_run(0 config format)
file(READ "${CONFIG_FILE}" FORMATTED_TWICE)
if(NOT FORMATTED_ONCE STREQUAL FORMATTED_TWICE)
    message(FATAL_ERROR "config format is not idempotent")
endif()

# --- delete restores the original bytes ------------------------------------
# Rebuild from pristine so the role edits above do not muddy the comparison.
file(WRITE "${CONFIG_FILE}" "${PRISTINE}")
apogee_run(0 config add-backend temp --type mock --model m)
apogee_run(0 config delete-backend temp)
file(READ "${CONFIG_FILE}" AFTER_ROUND_TRIP)
if(NOT AFTER_ROUND_TRIP STREQUAL PRISTINE)
    message(FATAL_ERROR "add-then-delete was not byte-identical")
endif()

file(REMOVE_RECURSE "${APOGEE_WORK_DIR}")
message(STATUS "apogee config end-to-end: OK")
