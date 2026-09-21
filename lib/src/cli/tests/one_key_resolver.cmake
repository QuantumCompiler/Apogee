# One key-resolution chain, and only one.
#
# The sibling of tests/one_role_resolver.cmake, for the same reason: a
# precedence chain written inline where a backend is built gets copied the day
# a second surface builds one, and the copies drift. A CLI and a server using
# different keys for the same entry is a bug nobody notices until a bill
# arrives. So `secrets/resolve.cpp` owns the chain, and this check fails any
# other source that reads an entry's `api_key` or names a conventional
# variable -- both of which are how a second chain starts.
#
# The allowlist names every legitimate reader WITH its reason. It refuses to
# run if the allowlist swallows every file, so it cannot pass vacuously.

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR must be set")
endif()

file(GLOB_RECURSE sources "${SOURCE_DIR}/*.cpp")
list(LENGTH sources source_count)
if(source_count EQUAL 0)
    message(FATAL_ERROR "one-key-resolver check found no sources under ${SOURCE_DIR}")
endif()

set(allowed
    "secrets/resolve.cpp"          # the chain itself
    "secrets/store.cpp"            # the slot file; never names a variable
    "harness/config.cpp"           # parses the api_key field
    "harness/config_edit.cpp"      # writes the api_key field
    "harness/config_template.cpp"  # the starter config's commentary
    "commands/config_cmd.cpp"      # the --api-key flag, stored literally
    "httpserver/admin_config.cpp"  # api_key_set in the view; is_literal_api_key
    "commands/auth_cmd.cpp"        # names the variables to TELL the user which to export
)

set(offenders "")
set(scanned 0)
foreach(source IN LISTS sources)
    file(RELATIVE_PATH relative "${SOURCE_DIR}" "${source}")
    set(is_allowed FALSE)
    foreach(entry IN LISTS allowed)
        if(relative STREQUAL entry)
            set(is_allowed TRUE)
            break()
        endif()
    endforeach()
    if(is_allowed)
        continue()
    endif()

    math(EXPR scanned "${scanned} + 1")
    file(STRINGS "${source}" lines)
    set(line_number 0)
    foreach(line IN LISTS lines)
        math(EXPR line_number "${line_number} + 1")
        # Documentation must be able to name what the rule forbids.
        string(STRIP "${line}" stripped)
        if(stripped MATCHES "^(//|/\\*|\\*)")
            continue()
        endif()
        # Reading the CONFIG ENTRY's key (a provider's own options.api_key is
        # the resolved value it was handed, and is fine), or naming a
        # conventional variable in code.
        if(line MATCHES "(config|backend|entry|cfg)\\.api_key" OR
           line MATCHES "ANTHROPIC_API_KEY" OR
           line MATCHES "OPENAI_API_KEY" OR
           line MATCHES "GEMINI_API_KEY" OR
           line MATCHES "GOOGLE_API_KEY")
            list(APPEND offenders "${relative}:${line_number}: ${stripped}")
        endif()
    endforeach()
endforeach()

if(scanned EQUAL 0)
    message(FATAL_ERROR "one-key-resolver check scanned no files -- the allowlist swallowed them")
endif()

if(offenders)
    string(REPLACE ";" "\n  " pretty "${offenders}")
    message(FATAL_ERROR
        "A second key-resolution chain is starting:\n  ${pretty}\n"
        "Reading an entry's api_key or naming a conventional variable outside "
        "secrets/resolve.cpp is how the CLI and the server come to use different keys. "
        "Call secrets::resolve_api_key() instead. If this file genuinely needs the raw value, "
        "add it to the allowlist in this file WITH a reason.")
endif()

message(STATUS "one-key-resolver check: ${scanned} files, no second resolution chain - OK")
