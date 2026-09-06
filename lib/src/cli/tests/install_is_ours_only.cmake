# `make install` installs Apogee, and nothing else.
#
# This exists because the opposite happened. `FetchContent_MakeAvailable` adds
# each dependency's own `install()` rules to this project, so a plain
# `cmake --install` also installed replxx's headers, its static library, and its
# CMake package config into the prefix.
#
# That was not merely untidy. The installed replxx config declares
# `Threads::Threads` with no `find_package(Threads)` behind it, and our
# `FIND_PACKAGE_ARGS` then PREFERRED that installed copy on the next configure
# -- so running `make install` once broke every subsequent build of the project,
# with an error pointing at replxx rather than at the install that caused it.
# Found in the wild on a developer's machine, 2026-09-06.
#
# The fix is `COMPONENT apogee` on our rule plus `--component apogee` on the
# invocation. This check is what keeps it fixed: a dependency added later brings
# its own install rules, and nobody will think to re-test this by hand.

if(NOT DEFINED BUILD_DIR OR NOT DEFINED WORK_DIR OR NOT DEFINED MAKEFILE)
    message(FATAL_ERROR "BUILD_DIR, WORK_DIR and MAKEFILE must be set")
endif()

# Check the command a USER actually runs, not one this test composes.
#
# The first version of this check called `cmake --install --component apogee`
# itself, and so passed happily while the Makefile was installing without the
# component and leaking replxx into the prefix. A guard that supplies the very
# argument it is verifying tests nothing.
file(STRINGS "${MAKEFILE}" install_lines REGEX "cmake --install")
if(NOT install_lines)
    message(FATAL_ERROR "no `cmake --install` line found in ${MAKEFILE}")
endif()
foreach(line IN LISTS install_lines)
    if(NOT line MATCHES "--component apogee")
        message(FATAL_ERROR
            "The Makefile installs without `--component apogee`:\n  ${line}\n"
            "Without it, every dependency's own install() rules run too -- see the header.")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --prefix "${WORK_DIR}" --component apogee
    RESULT_VARIABLE status
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "cmake --install failed:\n${out}\n${err}")
endif()

file(GLOB_RECURSE installed RELATIVE "${WORK_DIR}" "${WORK_DIR}/*")
list(SORT installed)

# Exactly one file: the executable. `apogee_core` is a static library and an
# implementation detail; no dependency may appear at all.
set(expected "bin/apogee")
if(NOT installed STREQUAL expected)
    string(REPLACE ";" "\n  " pretty "${installed}")
    message(FATAL_ERROR
        "install put more than Apogee into the prefix:\n  ${pretty}\n"
        "Expected exactly: ${expected}\n"
        "A dependency's install() rules have leaked in. Keep our rule on "
        "COMPONENT apogee and install with --component apogee.")
endif()

message(STATUS "install check: exactly '${installed}' - OK")
