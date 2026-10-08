# Provider probes never run on a hot path (28a).
#
# Detecting a provider costs seconds -- a cold `claude --version` took 2.6 s
# and `gemini --version` 0.7-0.9 s on every run (the 2026-10-03 spike) -- so a
# probe on a startup path would multiply Apogee's startup, not slow it. The
# rule: detection happens inside `check`, an explicit `providers scan`, or not
# at all; everything else reads the cache (`backends/provider_cache.h`).
#
# It is held structurally rather than by care:
#
#   - only the files listed in `allowed` may include `backends/provider_probe.h`,
#     and every one of them is a .cpp -- a header in the list would carry the
#     prober to everything that includes it;
#   - the files that start a turn, a session or a server must exist (so a
#     rename cannot make this pass vacuously) and may neither include the
#     prober nor name its entry points.
#
# It requires the prober itself to be seen, so a moved prober fails rather than
# passes.

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR must be set")
endif()

file(GLOB_RECURSE sources "${SOURCE_DIR}/*.cpp" "${SOURCE_DIR}/*.h")
list(LENGTH sources source_count)
if(source_count EQUAL 0)
    message(FATAL_ERROR "no-provider-probes check found no sources under ${SOURCE_DIR}")
endif()

# The only files that may run a probe: the prober, and the explicit surfaces.
set(allowed
    "data/backends/provider_probe.cpp"
)

# Chat, execute, complete, serve, task run/resume, symphonies play and machine
# mode (which is chat and complete with another output format) -- every path
# that starts something a person is waiting on.
set(hot_paths
    "main.cpp"
    "presentation/cli/chat.cpp"
    "presentation/cli/chat_session.cpp"
    "presentation/cli/chat_turn.cpp"
    "presentation/cli/execute.cpp"
    "presentation/cli/complete.cpp"
    "presentation/cli/serve_cmd.cpp"
    "presentation/cli/task_cmd.cpp"
    "presentation/cli/symphonies_cmd.cpp"
    "presentation/machine/json_reporter.cpp"
)
set(entry_points "scan_host_providers" "scan_providers" "host_probe_runner")

foreach(entry IN LISTS allowed)
    if(NOT entry MATCHES "[.]cpp$")
        message(FATAL_ERROR "no-provider-probes: ${entry} is a header; only .cpp files may include "
                            "the prober")
    endif()
endforeach()

set(offenders "")
set(seen_prober FALSE)
set(seen_hot "")
foreach(source IN LISTS sources)
    file(RELATIVE_PATH relative "${SOURCE_DIR}" "${source}")
    file(STRINGS "${source}" includes REGEX "#include \"backends/provider_probe[.]h\"")
    if(includes)
        if(relative STREQUAL "data/backends/provider_probe.cpp")
            set(seen_prober TRUE)
        endif()
        if(NOT relative IN_LIST allowed)
            list(APPEND offenders "${relative} includes backends/provider_probe.h")
        endif()
    endif()
    if(relative IN_LIST hot_paths)
        list(APPEND seen_hot "${relative}")
        file(STRINGS "${source}" lines)
        foreach(line IN LISTS lines)
            string(STRIP "${line}" stripped)
            if(stripped MATCHES "^(//|/\\*|\\*)")
                continue()
            endif()
            foreach(entry IN LISTS entry_points)
                if(line MATCHES "${entry}")
                    list(APPEND offenders "${relative} names ${entry}: ${stripped}")
                endif()
            endforeach()
        endforeach()
    endif()
endforeach()

if(NOT seen_prober)
    message(FATAL_ERROR "no-provider-probes: data/backends/provider_probe.cpp was not seen "
                        "including its header -- the prober moved; update this check with it")
endif()
foreach(hot IN LISTS hot_paths)
    if(NOT hot IN_LIST seen_hot)
        message(FATAL_ERROR "no-provider-probes: ${hot} not found -- a hot path moved; update "
                            "this check with it")
    endif()
endforeach()

if(offenders)
    string(REPLACE ";" "\n  " pretty "${offenders}")
    message(FATAL_ERROR
        "A provider probe is reachable from outside the explicit surfaces:\n  ${pretty}\n"
        "Detection runs inside `check` or `providers scan` only; a startup path reads the cache "
        "(backends/provider_cache.h). See backlog 28a and backends/provider_probe.h.")
endif()

message(STATUS "no-provider-probes check: ${source_count} files, the prober reachable only "
               "from ${allowed} - OK")
