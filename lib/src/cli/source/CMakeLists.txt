# The CLI application.
#
# Two kinds of target, and the split between them is load-bearing:
#
#   the modules  -- EVERY capability, one static library per module
#                   (apogee_<layer>_<module>, Architecture A4), each linked to
#                   exactly the modules cmake/modules.cmake declares for it.
#                   apogee_core aggregates them all: tests and the executable
#                   link that one name.
#   apogee       -- a thin face: argv in, exit code out, nothing else.
#
# Parity across surfaces is structural because of that split (SPEC.md ->
# Principles: "one source of truth per concern"). cmake/ApogeeLinkPolicy.cmake
# fails the configure step if anything ever links `apogee`, and walks the
# module graph against the map.

# Stamped into the binary at configure time. GIT_COMMIT is best-effort: a
# tarball export with no .git still builds, it just reports "unknown".
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE APOGEE_GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT APOGEE_GIT_COMMIT)
    set(APOGEE_GIT_COMMIT "unknown")
endif()
string(TIMESTAMP APOGEE_BUILD_DATE "%Y-%m-%d" UTC)

# One module: `apogee_add_module(<module> <sources...>)`, called from its
# layer's CMakeLists.txt with the sources relative to the layer directory.
# The library's links are the map's, never the caller's -- a dependency is
# declared in cmake/modules.cmake or it does not exist.
#
# The layer directory is the module's PUBLIC include root (A2: a package is
# included by its short name -- `#include "agentloop/loop.h"` -- whichever
# layer holds it). A module therefore sees its own layer's root and the roots
# its links carry up from below, and nothing above it: an include that reaches
# up a layer finds no such file.
function(apogee_add_module module)
    get_filename_component(layer "${CMAKE_CURRENT_SOURCE_DIR}" NAME)
    if(NOT module IN_LIST APOGEE_LAYER_${layer})
        message(FATAL_ERROR "source/${layer}/CMakeLists.txt builds the module '${module}', "
                            "which cmake/modules.cmake does not list in the ${layer} layer -- "
                            "give it a row there, with its links, first")
    endif()
    set(target apogee_${layer}_${module})
    add_library(${target} STATIC ${ARGN})
    target_include_directories(${target} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
    foreach(dependency IN LISTS APOGEE_LINKS_${module})
        if(NOT DEFINED APOGEE_MODULE_LAYER_${dependency})
            message(FATAL_ERROR "cmake/modules.cmake links '${module}' to '${dependency}', "
                                "which is no module")
        endif()
        target_link_libraries(${target}
                              PUBLIC apogee_${APOGEE_MODULE_LAYER_${dependency}}_${dependency})
    endforeach()
    apogee_set_target_options(${target})
endfunction()

# One layer: `apogee_add_layer()`, last in its CMakeLists.txt, makes
# apogee_<layer> -- an INTERFACE target over every module the map lists for
# the layer. Its one source, the layer's own CMakeLists.txt, is never compiled;
# it is what makes an INTERFACE target a build target, so
# `cmake --build <dir> --target apogee_data` builds exactly the Data modules
# and what they link.
function(apogee_add_layer)
    get_filename_component(layer "${CMAKE_CURRENT_SOURCE_DIR}" NAME)
    add_library(apogee_${layer} INTERFACE "${CMAKE_CURRENT_LIST_FILE}")
    foreach(module IN LISTS APOGEE_LAYER_${layer})
        if(NOT TARGET apogee_${layer}_${module})
            message(FATAL_ERROR "cmake/modules.cmake lists '${module}' in the ${layer} layer, "
                                "but source/${layer}/CMakeLists.txt never builds it")
        endif()
        target_link_libraries(apogee_${layer} INTERFACE apogee_${layer}_${module})
        add_dependencies(apogee_${layer} apogee_${layer}_${module})
    endforeach()
endfunction()

# The layers, lowest first: the order ADR 0001 states and the build log shows.
foreach(layer IN LISTS APOGEE_LAYERS)
    add_subdirectory(${layer})
endforeach()

# Every capability, under the one name its consumers link -- highest layer
# first, the order a static link wants (a library before what it needs).
add_library(apogee_core INTERFACE)
set(layers_down ${APOGEE_LAYERS})
list(REVERSE layers_down)
foreach(layer IN LISTS layers_down)
    target_link_libraries(apogee_core INTERFACE apogee_${layer})
endforeach()

add_executable(apogee main.cpp)
target_link_libraries(apogee PRIVATE apogee_core)
apogee_set_target_options(apogee)

# The install path: the binary, and nothing else. `make install` is a thin
# caller into this, so there is exactly one install path -- the same shape as
# the build (one CMake definition, a Makefile wrapper that adds no steps).
#
# The module libraries are deliberately NOT installed: they are static
# libraries and implementation details of the executable, and installing them
# would invite something outside this project to link the whole product.
#
# This rule seeds no ~/.apogee/ assets on purpose. The full install contract --
# the data-directory layout, the config template, shell completions, and the
# byte-identical parity between this path and scripts/install.sh validated by
# `apogee check` -- is owned by the install-check-lifecycle backlog item.
# SPEC.md -> "No silent install drift" requires any asset landing under
# ~/.apogee/ to join EVERY install path in the same commit, so this one starts
# empty rather than shipping half a contract that check cannot yet verify.
# COMPONENT is load-bearing, not decoration.
#
# `FetchContent_MakeAvailable` adds each dependency's own `install()` rules to
# THIS project, so a plain `cmake --install` installs replxx's headers, static
# library, and CMake package config alongside our binary. That is not merely
# untidy: the installed replxx config declares `Threads::Threads` without a
# `find_package(Threads)`, and our `FIND_PACKAGE_ARGS` then PREFERS that
# installed copy on the next configure -- so `make install` broke every
# subsequent build of the project. Found in the wild, 2026-09-06.
#
# Naming a component and installing only that component makes the install
# exactly what we declare, whatever a dependency declares for itself.
install(TARGETS apogee RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT apogee)
