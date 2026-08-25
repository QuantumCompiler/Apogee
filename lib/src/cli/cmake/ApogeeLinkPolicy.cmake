# Link-layer enforcement of the library-first layout.
#
# The rule (a Core constraint of the cpp-project-skeleton item): the `apogee`
# executable is a THIN FACE over `apogee_core`. Nothing may link the executable
# -- not tests, not future targets, not the HTTP server. Every capability must
# be reachable from the library alone, because that is what makes parity across
# surfaces structural rather than audited (SPEC.md -> Principles).
#
# Without this check the rule is a convention that silently rots the first time
# someone puts a helper in main.cpp and a test wants it. With it, that mistake
# is a configure-time failure with the offending target named.

set(APOGEE_CLI_TARGET "apogee" CACHE INTERNAL "The CLI executable target name")

# Collects every target defined in this directory and below.
function(_apogee_collect_targets out_var dir)
    get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(subdir IN LISTS subdirs)
        _apogee_collect_targets(sub_targets "${subdir}")
        list(APPEND targets ${sub_targets})
    endforeach()
    set(${out_var} "${targets}" PARENT_SCOPE)
endfunction()

function(apogee_assert_link_policy)
    _apogee_collect_targets(all_targets "${CMAKE_SOURCE_DIR}")

    set(violations "")
    foreach(target IN LISTS all_targets)
        if(target STREQUAL APOGEE_CLI_TARGET)
            continue()
        endif()
        get_target_property(type ${target} TYPE)
        if(type STREQUAL "INTERFACE_LIBRARY")
            continue()
        endif()
        get_target_property(libs ${target} LINK_LIBRARIES)
        if(libs AND "${APOGEE_CLI_TARGET}" IN_LIST libs)
            list(APPEND violations "${target}")
        endif()
    endforeach()

    if(violations)
        message(FATAL_ERROR
            "Link policy violation: target(s) [${violations}] link '${APOGEE_CLI_TARGET}'.\n"
            "The CLI executable is a thin face over apogee_core and must never be a "
            "link dependency. Move the shared code into a library package under "
            "lib/src/cli/source/ and link apogee_core instead.")
    endif()

    message(STATUS "Apogee: link policy OK (no target links '${APOGEE_CLI_TARGET}')")
endfunction()
