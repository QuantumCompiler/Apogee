# Link-layer enforcement of the library-first layout and of the layers.
#
# Two rules, both failing the configure step with the offending target named:
#
# 1. The `apogee` executable is a THIN FACE over the modules (a Core constraint
#    of the cpp-project-skeleton item). Nothing may link the executable -- not
#    tests, not future targets, not the HTTP server. Every capability must be
#    reachable from the libraries alone, because that is what makes parity
#    across surfaces structural rather than audited (SPEC.md -> Principles).
#    Without this check the rule is a convention that silently rots the first
#    time someone puts a helper in main.cpp and a test wants it.
#
# 2. The module graph is the map (Architecture A4, ADR 0001). Every module in
#    cmake/modules.cmake is a library, apogee_<layer>_<module>, that links
#    exactly the modules the map declares -- a link added by hand anywhere else
#    fails -- each in its own layer or below, with no cycle among them (CMake
#    itself allows a cycle between static libraries; this does not). Each
#    module compiles only its own directory's sources, and a layer's test
#    library (tests/CMakeLists.txt) links its own layer and below, never above
#    (ADR 0004).

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

# The module that `target` names, or "" when it names none.
function(_apogee_module_of out_var target)
    set(module "")
    foreach(candidate IN LISTS APOGEE_MODULES)
        if(target STREQUAL "apogee_${APOGEE_MODULE_LAYER_${candidate}}_${candidate}")
            set(module ${candidate})
            break()
        endif()
    endforeach()
    set(${out_var} "${module}" PARENT_SCOPE)
endfunction()

# Appends one violation, its parts concatenated, to the caller's `violations`.
macro(_apogee_violation)
    string(CONCAT _apogee_message ${ARGN})
    list(APPEND violations "${_apogee_message}")
endmacro()

function(_apogee_assert_module_graph)
    set(violations "")
    set(edges 0)
    # Ours: a module, a layer aggregate, apogee_core. Not apogee_sqlite3 and
    # its like, which are third-party code under our prefix.
    string(JOIN "|" layers ${APOGEE_LAYERS})
    set(ours "^apogee_(core|(${layers})(_[a-z_]+)?)$")
    foreach(module IN LISTS APOGEE_MODULES)
        set(layer ${APOGEE_MODULE_LAYER_${module}})
        set(target apogee_${layer}_${module})
        if(NOT TARGET ${target})
            _apogee_violation("'${module}' is in the map but no target ${target} exists")
            continue()
        endif()

        # What it links, against what the map declares for it.
        get_target_property(libs ${target} LINK_LIBRARIES)
        set(linked "")
        foreach(lib IN LISTS libs)
            if(lib MATCHES "${ours}")
                _apogee_module_of(dependency "${lib}")
                if(dependency STREQUAL "")
                    _apogee_violation("${target} links ${lib}, which is not a module -- "
                                      "a module links modules, never an aggregate")
                    continue()
                endif()
                list(APPEND linked ${dependency})
                if(APOGEE_MODULE_RANK_${dependency} GREATER APOGEE_MODULE_RANK_${module})
                    _apogee_violation("${target} (${layer}) links ${lib} "
                                      "(${APOGEE_MODULE_LAYER_${dependency}}) -- a link up a layer")
                endif()
            endif()
        endforeach()
        foreach(dependency IN LISTS linked)
            if(NOT dependency IN_LIST APOGEE_LINKS_${module})
                _apogee_violation("${target} links '${dependency}', which "
                                  "cmake/modules.cmake does not declare for '${module}'")
            endif()
        endforeach()
        foreach(dependency IN LISTS APOGEE_LINKS_${module})
            if(NOT dependency IN_LIST linked)
                _apogee_violation("cmake/modules.cmake declares ${module} -> ${dependency}, "
                                  "but ${target} does not link it")
            endif()
            math(EXPR edges "${edges} + 1")
        endforeach()

        # What it compiles: its own directory, and nothing beside it.
        get_target_property(sources ${target} SOURCES)
        get_target_property(source_dir ${target} SOURCE_DIR)
        foreach(source IN LISTS sources)
            cmake_path(ABSOLUTE_PATH source BASE_DIRECTORY "${source_dir}" NORMALIZE
                       OUTPUT_VARIABLE absolute)
            string(FIND "${absolute}" "${source_dir}/${module}/" at)
            if(NOT at EQUAL 0)
                _apogee_violation("${target} compiles ${source}, which is not under "
                                  "${layer}/${module}/")
            endif()
        endforeach()
    endforeach()

    # No cycle: peel modules whose declared links are all peeled already; what
    # never peels sits on a cycle or stands on one.
    set(remaining ${APOGEE_MODULES})
    set(peeled "")
    set(progress TRUE)
    while(progress)
        set(progress FALSE)
        set(still "")
        foreach(module IN LISTS remaining)
            set(ready TRUE)
            foreach(dependency IN LISTS APOGEE_LINKS_${module})
                if(NOT dependency IN_LIST peeled)
                    set(ready FALSE)
                    break()
                endif()
            endforeach()
            if(ready)
                list(APPEND peeled ${module})
                set(progress TRUE)
            else()
                list(APPEND still ${module})
            endif()
        endforeach()
        set(remaining ${still})
    endwhile()
    # What remains is the cycle and whatever stands on it; peel what nothing
    # left links to, so the message names the cycle alone.
    set(progress TRUE)
    while(progress)
        set(progress FALSE)
        set(still "")
        foreach(module IN LISTS remaining)
            set(needed FALSE)
            foreach(other IN LISTS remaining)
                if(module IN_LIST APOGEE_LINKS_${other})
                    set(needed TRUE)
                    break()
                endif()
            endforeach()
            if(needed)
                list(APPEND still ${module})
            else()
                set(progress TRUE)
            endif()
        endforeach()
        set(remaining ${still})
    endwhile()
    if(remaining)
        string(REPLACE ";" ", " cycle "${remaining}")
        _apogee_violation("the map has a cycle among [${cycle}]")
    endif()

    # A layer's test library: its own layer and below.
    foreach(layer IN LISTS APOGEE_LAYERS)
        set(tests apogee_tests_${layer})
        if(NOT TARGET ${tests})
            continue()
        endif()
        get_target_property(libs ${tests} LINK_LIBRARIES)
        foreach(lib IN LISTS libs)
            set(reached "")
            if(lib STREQUAL "apogee_core")
                set(reached presentation)
            elseif(lib MATCHES "^apogee_([a-z]+)$" AND CMAKE_MATCH_1 IN_LIST APOGEE_LAYERS)
                set(reached ${CMAKE_MATCH_1})
            else()
                _apogee_module_of(dependency "${lib}")
                if(NOT dependency STREQUAL "")
                    set(reached ${APOGEE_MODULE_LAYER_${dependency}})
                endif()
            endif()
            if(NOT reached STREQUAL ""
               AND APOGEE_LAYER_RANK_${reached} GREATER APOGEE_LAYER_RANK_${layer})
                _apogee_violation("${tests} links ${lib} (${reached}) -- a ${layer} test "
                                  "links its own layer and below (ADR 0004)")
            endif()
        endforeach()
    endforeach()

    if(violations)
        string(REPLACE ";" "\n  " pretty "${violations}")
        message(FATAL_ERROR
            "Link policy violation -- the module graph is not the map:\n  ${pretty}\n"
            "cmake/modules.cmake declares every module's layer and links; a module links "
            "only what its row names, in its own layer or below (ADR 0001).")
    endif()
    list(LENGTH APOGEE_MODULES count)
    message(STATUS "Apogee: module graph OK (${count} modules, ${edges} declared links, "
                   "layered, acyclic)")
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
            "The CLI executable is a thin face over the modules and must never be a "
            "link dependency. Move the shared code into a module under "
            "lib/src/cli/source/<layer>/ and link apogee_core (or the module) instead.")
    endif()

    message(STATUS "Apogee: link policy OK (no target links '${APOGEE_CLI_TARGET}')")

    _apogee_assert_module_graph()
endfunction()
