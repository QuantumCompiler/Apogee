#
# Windows refuses to load an executable whose image is 2 GiB or more: it
# reports it as "not a valid Win32 application" (ERROR_BAD_EXE_FORMAT), which
# a build sees as "inappropriate file type or format" with no size in sight.
# MSVC's linker refuses to write one (LNK1248); a MinGW linker writes it.
#
# v0.1.6's test binary crossed it on windows-x64 (2026-10-12): a MinGW linker
# puts DWARF inside the image, where it was 97% of it, so the Windows presets
# now build without -g (CMakePresets.json). This guard keeps the failure
# legible if anything grows an image towards the limit again: it reads the
# image size from the PE header after each link, warns past three quarters
# of the limit, and fails the link at it, naming the size and the limit.
#
# Used as a module, it defines apogee_guard_image_size(<target>), a no-op off
# Windows. Run as a script (cmake -DIMAGE=<file> -P ApogeeImageSize.cmake), it
# is the check itself.

set(APOGEE_IMAGE_SIZE_SCRIPT "${CMAKE_CURRENT_LIST_FILE}")

function(apogee_guard_image_size target)
    if(NOT WIN32)
        return()
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -DIMAGE=$<TARGET_FILE:${target}>
                -P "${APOGEE_IMAGE_SIZE_SCRIPT}"
        VERBATIM)
endfunction()

# Script mode: the check.
if(CMAKE_SCRIPT_MODE_FILE AND DEFINED IMAGE)
    # Reads `count` bytes at `offset` as a little-endian unsigned number.
    function(_apogee_read_le file offset count out)
        file(READ "${file}" bytes OFFSET ${offset} LIMIT ${count} HEX)
        string(LENGTH "${bytes}" length)
        set(value "")
        set(at ${length})
        while(at GREATER 0)
            math(EXPR at "${at} - 2")
            string(SUBSTRING "${bytes}" ${at} 2 byte)
            string(APPEND value "${byte}")
        endwhile()
        math(EXPR value "0x${value}")
        set(${out} ${value} PARENT_SCOPE)
    endfunction()

    file(READ "${IMAGE}" magic LIMIT 2 HEX)
    if(NOT magic STREQUAL "4d5a")
        message(FATAL_ERROR "${IMAGE} is not a Windows executable (no MZ header)")
    endif()
    _apogee_read_le("${IMAGE}" 60 4 pe_offset)
    # SizeOfImage: the PE signature (4 bytes), the COFF header (20), then 56
    # bytes into the optional header, in both PE32 and PE32+.
    math(EXPR at "${pe_offset} + 4 + 20 + 56")
    _apogee_read_le("${IMAGE}" ${at} 4 image_size)

    set(limit 2147483648)
    math(EXPR warn "${limit} / 4 * 3")
    math(EXPR mib "${image_size} / 1048576")
    get_filename_component(name "${IMAGE}" NAME)
    if(image_size GREATER_EQUAL limit)
        message(FATAL_ERROR
            "${name}'s image is ${mib} MiB: Windows refuses to load an image of 2 GiB or "
            "more, so this executable cannot run (see cmake/ApogeeImageSize.cmake)")
    elseif(image_size GREATER_EQUAL warn)
        message(WARNING
            "${name}'s image is ${mib} MiB, past three quarters of the 2 GiB Windows "
            "refuses to load (see cmake/ApogeeImageSize.cmake)")
    endif()
endif()
