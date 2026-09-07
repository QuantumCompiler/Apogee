# There is exactly one role-resolution chain, and it lives in harness/roles.cpp.
#
# **Ommi shipped this logic twice and the copies disagreed.** Its CLI and its
# HTTP admin plane each grew their own chain, so a request ran on one backend
# from the terminal and another over HTTP. The fix there was one exported
# function both call. Apogee starts from that fix — and this check is what keeps
# it true, because the duplicate is never written deliberately: it appears as
# one innocent-looking line inside whatever command needs a backend name.
#
# So the shape of that line is what is banned. Reading `models.default_backend`
# (or either role pointer) anywhere outside the resolver, the config engine, and
# the config-display command means a second chain has started.
#
# This is the structural half of the item's acceptance criterion. The table test
# in tests/harness/roles_test.cpp proves the chain is CORRECT; this proves it is
# the ONLY one. Neither is sufficient alone: a correct resolver nobody calls
# fixes nothing.
#
# It refuses to run against an empty source list, so it cannot pass vacuously.

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR must be set")
endif()

file(GLOB_RECURSE sources "${SOURCE_DIR}/*.cpp")
list(LENGTH sources source_count)
if(source_count EQUAL 0)
    message(FATAL_ERROR "one-role-resolver check found no sources under ${SOURCE_DIR}")
endif()

# Files entitled to touch the raw fields:
#   harness/roles.cpp    -- the resolver itself; this IS the one chain
#   harness/config.cpp   -- parses them out of YAML
#   harness/config_edit.cpp / config_template.cpp -- writes and documents them
#   harness/harness.cpp  -- holds default_model() for display; calls the resolver
#   commands/config_cmd.cpp -- `config get models.default` must print the raw value
#   commands/check.cpp   -- validates each pointer AS WRITTEN, which is the one
#                           place the unresolved value is the point
set(allowed
    "harness/roles.cpp"
    "harness/config.cpp"
    "harness/config_edit.cpp"
    "harness/config_template.cpp"
    "harness/harness.cpp"
    "commands/config_cmd.cpp"
    "commands/check.cpp"
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

        if(line MATCHES "models\\.default_backend" OR
           line MATCHES "models\\.default_embedding" OR
           line MATCHES "models\\.default_extraction")
            list(APPEND offenders "${relative}:${line_number}: ${stripped}")
        endif()
    endforeach()
endforeach()

if(scanned EQUAL 0)
    message(FATAL_ERROR "one-role-resolver check scanned no files -- the allowlist swallowed them")
endif()

if(offenders)
    string(REPLACE ";" "\n  " pretty "${offenders}")
    message(FATAL_ERROR
        "A second role-resolution chain is starting:\n  ${pretty}\n"
        "Reading models.default_* directly is how the CLI and the HTTP plane came to disagree in "
        "Ommi. Call harness::resolve_backend_key() (or resolve_chat_backend()) instead -- it takes "
        "the override and any per-feature pin and applies the whole chain. If this file genuinely "
        "needs the raw value, add it to the allowlist in this file WITH a reason.")
endif()

message(STATUS
    "one-role-resolver check: ${scanned} files, no second resolution chain - OK")
