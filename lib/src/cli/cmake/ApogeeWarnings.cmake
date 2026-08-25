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
