# Shared compile options for first-party targets.
#
# Applied via apogee_set_target_options(<target>). Deliberately NOT applied to
# anything under third_party/ -- vendored code is read-only and is not held to
# Apogee's warning bar (see CLAUDE.md -> Invariants).

function(apogee_set_target_options target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc)
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wnon-virtual-dtor
            -Wold-style-cast
            -Wcast-align
            -Wunused
            -Woverloaded-virtual
            -Wconversion
            -Wsign-conversion
        )
    endif()
endfunction()

# The other half of the rule above: third-party code is compiled with its
# warnings OFF, every target under `directory` and its subdirectories.
#
# Not ours to fix -- vendored and fetched code is read-only here, and fixes go
# upstream -- and not harmless to leave on: a clean llama.cpp build printed
# around a hundred lines of upstream warnings (deprecated Metal API on the
# macOS 27 SDK, a C++20 enum-arithmetic deprecation repeated once per mtmd
# model file), and a first-party warning in that wall is one nobody reads.
# `apogee_sqlite3` in third_party/CMakeLists.txt was the first case of this.
#
# Imported targets (a dependency found on the system) are skipped: they are
# already built, so there is nothing to compile quietly.
function(apogee_quiet_third_party directory)
    get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS targets)
        get_target_property(type ${target} TYPE)
        get_target_property(imported ${target} IMPORTED)
        if(imported OR NOT type MATCHES "^(STATIC|SHARED|MODULE|OBJECT)_LIBRARY$|^EXECUTABLE$")
            continue()
        endif()
        if(MSVC)
            target_compile_options(${target} PRIVATE /w)
        else()
            target_compile_options(${target} PRIVATE -w)
        endif()
    endforeach()
    get_property(subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(subdirectory IN LISTS subdirectories)
        apogee_quiet_third_party("${subdirectory}")
    endforeach()
endfunction()
