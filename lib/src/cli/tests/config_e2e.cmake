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
    set(APOGEE_ERR "${err}" PARENT_SCOPE)
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

# --- a collection registers itself on first ingest -------------------------
# The one config write nobody types `config` for, held to the same byte-diff
# bar: the shipped template is comment-dense, and after an ingest it must be
# PRISTINE plus exactly one new section and nothing else.
set(CORPUS_DIR "${APOGEE_WORK_DIR}/corpus")
file(MAKE_DIRECTORY "${CORPUS_DIR}")
set(SENTENCE "the zarquon protocol requires seventeen widgets, and each widget must be logged. ")
string(REPEAT "${SENTENCE}" 6 CORPUS_TEXT)
file(WRITE "${CORPUS_DIR}/notes.md" "${CORPUS_TEXT}\n")

apogee_run(0 embed ingest notes "${CORPUS_DIR}")
if(NOT APOGEE_OUT MATCHES "registered 'notes' in")
    message(FATAL_ERROR "the first ingest did not report registering the collection: ${APOGEE_OUT}")
endif()

file(READ "${CONFIG_FILE}" AFTER_INGEST)
set(EXPECTED_AFTER_INGEST "${PRISTINE}\nembeddings:\n  notes:\n    chunk_size: 512\n    chunk_overlap: 64\n")
if(NOT AFTER_INGEST STREQUAL EXPECTED_AFTER_INGEST)
    message(FATAL_ERROR "ingest changed more than the one new entry:\n${AFTER_INGEST}")
endif()

apogee_run(0 config get embeddings)
expect_equal("${APOGEE_OUT}" "notes" "the registered collection is listed")
apogee_run(0 config get embeddings.notes.chunk_size)
expect_equal("${APOGEE_OUT}" "512" "the collection's chunk size is readable")

# A second ingest of the same name registers nothing again, and reads its
# chunking from the entry rather than from a flag nobody passed.
apogee_run(0 embed ingest notes "${CORPUS_DIR}")
if(APOGEE_OUT MATCHES "registered")
    message(FATAL_ERROR "a known collection was registered twice")
endif()
# ...and did not TRY to: a failed re-registration is reported on stderr, and a
# re-ingest that complains every time is a re-ingest people stop trusting.
if(NOT APOGEE_ERR STREQUAL "")
    message(FATAL_ERROR "a re-ingest of a known collection complained: ${APOGEE_ERR}")
endif()
file(READ "${CONFIG_FILE}" AFTER_SECOND_INGEST)
if(NOT AFTER_SECOND_INGEST STREQUAL EXPECTED_AFTER_INGEST)
    message(FATAL_ERROR "a re-ingest edited the config")
endif()

# The entry REMEMBERS the chunking, and a later ingest without flags reads it
# back -- the reason chunk sizes live on the collection at all. A corpus of
# ADRs wants 768 where prose wants 512, and re-typing that on every ingest is
# how corpora end up chunked inconsistently.
apogee_run(0 embed ingest small "${CORPUS_DIR}" --chunk-size 100 --chunk-overlap 0)
string(REGEX MATCH "([0-9]+) chunk\\(s\\)" _ "${APOGEE_OUT}")
set(FLAGGED_CHUNKS "${CMAKE_MATCH_1}")
if(NOT FLAGGED_CHUNKS GREATER 1)
    message(FATAL_ERROR "the corpus is too short for chunk size to matter: ${APOGEE_OUT}")
endif()
apogee_run(0 config get embeddings.small.chunk_size)
expect_equal("${APOGEE_OUT}" "100" "the flagged chunk size was written to the entry")

apogee_run(0 embed ingest small "${CORPUS_DIR}")
string(REGEX MATCH "([0-9]+) chunk\\(s\\)" _ "${APOGEE_OUT}")
if(NOT CMAKE_MATCH_1 STREQUAL FLAGGED_CHUNKS)
    message(FATAL_ERROR
        "a re-ingest without flags chunked differently (${CMAKE_MATCH_1} vs ${FLAGGED_CHUNKS}): "
        "the entry's chunk_size was not read back")
endif()

# Back to pristine for the rest of the lifecycle.
file(WRITE "${CONFIG_FILE}" "${PRISTINE}")

# --- add-backend -----------------------------------------------------------
apogee_run(0 config add-backend claude --type anthropic
           --api-key "\${ANTHROPIC_API_KEY}" --model claude-sonnet-5 --context-size 200000)

apogee_run(0 config get backends)
expect_equal("${APOGEE_OUT}" "claude" "the added backend is listed")

apogee_run(0 config get backends.claude.model)
expect_equal("${APOGEE_OUT}" "claude-sonnet-5" "get a nested key")

apogee_run(0 config get backends.claude.context_size)
expect_equal("${APOGEE_OUT}" "200000" "get a numeric key")

# A cloud entry can name the model it embeds with, separately from the one it
# chats with.
apogee_run(0 config add-backend gpt --type openai --api-key "\${OPENAI_API_KEY}"
           --model gpt-5 --embedding-model text-embedding-3-large)
apogee_run(0 config get backends.gpt.embedding_model)
expect_equal("${APOGEE_OUT}" "text-embedding-3-large" "the embedding model is its own field")
apogee_run(0 config delete-backend gpt)

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

# --- auto_rag: injection with no flag, reported, and switchable off --------
apogee_run(0 config add-backend mock --type mock --model mock-1)
file(APPEND "${CONFIG_FILE}" "\nauto_rag: notes\n")
apogee_run(0 config get auto_rag)
expect_equal("${APOGEE_OUT}" "notes" "auto_rag is readable")

# No --rag anywhere on this command line. The status line must still say what
# was injected, from where, and by which retriever -- silent injection is the
# failure mode this key was designed against.
apogee_run(0 complete -m mock -v "what does the zarquon protocol require?")
set(TURN_OUTPUT "${APOGEE_OUT}${APOGEE_ERR}")
if(NOT TURN_OUTPUT MATCHES "chunk\\(s\\) from 'notes'")
    message(FATAL_ERROR "auto_rag did not retrieve:\n${TURN_OUTPUT}")
endif()
if(NOT TURN_OUTPUT MATCHES "\\[lexical\\]")
    message(FATAL_ERROR "the retriever was not named:\n${TURN_OUTPUT}")
endif()
if(NOT TURN_OUTPUT MATCHES "\\(auto_rag\\)")
    message(FATAL_ERROR "injection from config was not announced as such:\n${TURN_OUTPUT}")
endif()

# The off switch -- `--rag ""` for one run -- is NOT exercised here, and the
# reason is worth keeping: execute_process drops an empty list element, so
# there is no way to hand the binary an empty argument from this script, and
# `--rag=` reaches CLI11 as a flag still waiting for its value (it then
# swallows the prompt and blocks on stdin). tests/auto_rag_e2e.sh covers it
# from a real shell; the precedence itself is table-tested in helpers_test.

# A named flag beats the key.
apogee_run(0 complete -m mock -v --rag other "what does the zarquon protocol require?")
set(TURN_OUTPUT "${APOGEE_OUT}${APOGEE_ERR}")
# There is no `other` collection, so the flag winning shows up as retrieval
# being reported unavailable for other.db -- and NOT as a hit from `notes`.
if(NOT TURN_OUTPUT MATCHES "other\\.db")
    message(FATAL_ERROR "--rag other did not beat auto_rag:\n${TURN_OUTPUT}")
endif()
if(TURN_OUTPUT MATCHES "from 'notes'")
    message(FATAL_ERROR "auto_rag's collection was searched despite --rag other:\n${TURN_OUTPUT}")
endif()
if(TURN_OUTPUT MATCHES "\\(auto_rag\\)")
    message(FATAL_ERROR "a flagged collection was announced as auto_rag:\n${TURN_OUTPUT}")
endif()

# And on the chat surface, the injected context never reaches the saved
# session -- the transient rule, on the config path, against a file on disk.
file(WRITE "${APOGEE_WORK_DIR}/question.txt" "what does the zarquon protocol require?\n")
execute_process(
    COMMAND "${APOGEE_BIN}" chat -m mock
    INPUT_FILE "${APOGEE_WORK_DIR}/question.txt"
    RESULT_VARIABLE chat_code
    OUTPUT_VARIABLE chat_out
    ERROR_VARIABLE chat_err
)
if(NOT chat_code EQUAL 0)
    message(FATAL_ERROR "chat under auto_rag failed: ${chat_out}\n${chat_err}")
endif()
if(NOT "${chat_out}${chat_err}" MATCHES "\\(auto_rag\\)")
    message(FATAL_ERROR "chat did not announce auto_rag injection:\n${chat_out}\n${chat_err}")
endif()
file(GLOB SESSION_FILES "${APOGEE_WORK_DIR}/sessions/*.json")
list(LENGTH SESSION_FILES session_count)
if(session_count EQUAL 0)
    message(FATAL_ERROR "chat persisted no session, so the transient check has nothing to read")
endif()
foreach(session_file ${SESSION_FILES})
    file(READ "${session_file}" SESSION_TEXT)
    if(SESSION_TEXT MATCHES "seventeen widgets")
        message(FATAL_ERROR "auto_rag context leaked into the saved session: ${session_file}")
    endif()
    if(NOT SESSION_TEXT MATCHES "zarquon protocol require")
        message(FATAL_ERROR "the session did not record the question, so the check is vacuous")
    endif()
endforeach()

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
