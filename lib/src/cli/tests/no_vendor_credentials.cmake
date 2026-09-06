# The vendor-CLI backends never read a vendor's credentials.
#
# SPEC.md -> Principles, "A vendor CLI is spawned, never opened": when Apogee
# drives a vendor's official CLI it does not open that CLI's credential store,
# does not implement an OAuth flow, and does not parse its session files off
# disk. The CLI authenticates; Apogee only spawns it.
#
# This is checked mechanically rather than reviewed, because the shortcut it
# forbids is genuinely tempting: when a `--resume` fails, reading the vendor's
# own session file *would* recover the conversation. That is exactly the line
# this rule draws -- the supported recovery path is replaying Apogee's own
# transcript, which is why disk-reading is never necessary.
#
# It refuses to run against an empty source list, so it cannot pass vacuously.

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR must be set")
endif()

file(GLOB_RECURSE sources "${SOURCE_DIR}/*.cpp" "${SOURCE_DIR}/*.h")
list(LENGTH sources source_count)
if(source_count EQUAL 0)
    message(FATAL_ERROR "no-vendor-credentials check found no sources under ${SOURCE_DIR}")
endif()

# Paths and mechanisms that would mean reading a vendor's credentials. The
# CLI's own name on a command line is fine -- spawning it is the whole point --
# so this looks for its DOT-directories and credential stores instead.
set(forbidden
    "[.]claude/"
    "[.]claude\"'"
    "\\.claude'"
    "claude[.]json"
    "credentials[.]json"
    "apiKeyHelper"
    "oauth"
    "OAuth"
    "keychain"
    "Keychain"
    "security find-generic-password"
)

set(offenders "")
foreach(source IN LISTS sources)
    file(STRINGS "${source}" lines)
    set(line_number 0)
    foreach(line IN LISTS lines)
        math(EXPR line_number "${line_number} + 1")
        # Documentation must be allowed to name what the rule forbids -- a
        # check that cannot be explained in a comment is a check nobody will
        # understand when it fires. Skipped: C++ comments, and the `#` comments
        # inside the config template, which is user-facing documentation that
        # lives as a string literal.
        string(STRIP "${line}" stripped)
        if(stripped MATCHES "^(//|/\\*|\\*|#)")
            continue()
        endif()
        foreach(pattern IN LISTS forbidden)
            if(line MATCHES "${pattern}")
                get_filename_component(name "${source}" NAME)
                list(APPEND offenders "${name}:${line_number}: ${stripped}")
            endif()
        endforeach()
    endforeach()
endforeach()

if(offenders)
    string(REPLACE ";" "\n  " pretty "${offenders}")
    message(FATAL_ERROR
        "A source file appears to read a vendor CLI's credentials:\n  ${pretty}\n"
        "Apogee spawns a vendor CLI; it never opens that CLI's credential store, implements an "
        "OAuth flow, or parses its session files. Crash recovery goes through `--resume` and, "
        "failing that, replaying Apogee's own transcript. See SPEC.md -> Principles.")
endif()

message(STATUS "no-vendor-credentials check: ${source_count} files, none read vendor credentials - OK")
