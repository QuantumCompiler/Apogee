# sandbox.cmake -- a run's own scratch directory, for the executable tier's
# CMake scripts (M17, 2026-10-20): tests/scripts/sh/sandbox.sh's rule in CMake.
#
#   include("${CMAKE_CURRENT_LIST_DIR}/sandbox.cmake")
#   apogee_sandbox_dir(APOGEE_WORK_DIR "${APOGEE_WORK_DIR}")
#
# ctest hands each script a fixed path in the build tree, and two runs of the
# suite in one tree shared it -- one run's setup wiping the other's home
# mid-test. A run now takes a fresh directory beside that path and removes it
# when it passes; a directory beside it that a failed or killed run left,
# older than any test may live (the longest ctest timeout is 15 minutes), is
# swept on the way in.
function(apogee_sandbox_dir out base)
    get_filename_component(parent "${base}" DIRECTORY)
    get_filename_component(name "${base}" NAME)
    file(MAKE_DIRECTORY "${parent}")

    string(TIMESTAMP now "%s" UTC)
    file(GLOB siblings LIST_DIRECTORIES true "${parent}/${name}.*")
    foreach(sibling IN LISTS siblings)
        file(TIMESTAMP "${sibling}" stamp "%s" UTC)
        if(stamp)
            math(EXPR age "${now} - ${stamp}")
            if(age GREATER 3600)
                file(REMOVE_RECURSE "${sibling}")
            endif()
        endif()
    endforeach()

    # Seeded by the microsecond, so two runs started in the same second do
    # not draw the same name; a name already taken is drawn again.
    string(TIMESTAMP seed "%f" UTC)
    while(TRUE)
        string(RANDOM LENGTH 8 ALPHABET "abcdefghijklmnopqrstuvwxyz0123456789"
               RANDOM_SEED "${seed}" suffix)
        set(dir "${parent}/${name}.${suffix}")
        if(NOT EXISTS "${dir}")
            break()
        endif()
        math(EXPR seed "${seed} + 1")
    endwhile()
    file(MAKE_DIRECTORY "${dir}")
    set(${out} "${dir}" PARENT_SCOPE)
endfunction()
