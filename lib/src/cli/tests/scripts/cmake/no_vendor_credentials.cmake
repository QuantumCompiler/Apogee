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
# so this looks for their DOT-directories and credential stores instead.
#
# Covers claude, gemini, and codex. `~/.ollama/` is deliberately NOT listed:
# that directory is a MODEL store, and reading it is a supported source in the
# model-acquisition item. Ollama's account credentials are the CLI's own, and
# the generic keychain/oauth patterns below catch an attempt to reach them.
set(forbidden
    "[.]claude/"
    "[.]claude\"'"
    "\\.claude'"
    "claude[.]json"
    "credentials[.]json"
    # gemini. A Google CLI is likelier than most to hold an OAuth refresh
    # token, which makes the rule matter more here, not less.
    "[.]gemini/"
    "[.]gemini\"'"
    "\\.gemini'"
    "google_accounts[.]json"
    "oauth_creds[.]json"
    # codex.
    "[.]codex/"
    "[.]codex\"'"
    "\\.codex'"
    "apiKeyHelper"
    "oauth"
    "OAuth"
    "keychain"
    "Keychain"
    "security find-generic-password"
)

# The one file that may NAME a vendor's login files (28a): the providers'
# knowledge table, which lists them so the detector can test that they EXIST.
# Only the path patterns are lifted there -- never the OAuth or credential-store
# ones -- and in exchange the table and the probe that reads it are held to
# having no way to read a file at all (below), so naming a path cannot become
# opening it.
set(knowledge_table "data/backends/provider_table.cpp")
set(path_patterns "[.]claude/" "claude[.]json" "[.]gemini/" "google_accounts[.]json" "[.]codex/")
set(existence_only "data/backends/provider_table.cpp" "data/backends/provider_probe.cpp"
    "data/backends/provider_status.cpp")
set(reading
    "fstream"
    "fopen"
    "fread"
    "istreambuf_iterator"
    "getline"
    "read_config_file"
    "ReadFile"
    "mmap"
)

set(offenders "")
set(seen_existence_only "")
foreach(source IN LISTS sources)
    file(RELATIVE_PATH relative "${SOURCE_DIR}" "${source}")
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
        # The detector's no-reading rule reads `#include` lines too: an
        # `#include <fstream>` is the first sign of a reader.
        if(relative IN_LIST existence_only AND NOT stripped MATCHES "^(//|/\\*|\\*)")
            foreach(pattern IN LISTS reading)
                if(line MATCHES "${pattern}")
                    get_filename_component(name "${source}" NAME)
                    list(APPEND offenders
                        "${name}:${line_number}: ${stripped} (the detector tests existence only)")
                endif()
            endforeach()
        endif()
        if(stripped MATCHES "^(//|/\\*|\\*|#)")
            continue()
        endif()
        foreach(pattern IN LISTS forbidden)
            # Apogee's OWN credential store is `config/credentials.json`
            # (provider-credential-store, 2026-09-13). The one place that
            # names it is the constant in secrets/store.h; everything else
            # goes through `kCredentialsFileName`, so this is the whole
            # exemption -- a vendor's file of the same name is still caught
            # anywhere else, including secrets/store.cpp.
            if(relative STREQUAL "data/secrets/store.h" AND pattern STREQUAL "credentials[.]json")
                continue()
            endif()
            if(relative STREQUAL knowledge_table AND pattern IN_LIST path_patterns)
                continue()
            endif()
            if(line MATCHES "${pattern}")
                get_filename_component(name "${source}" NAME)
                list(APPEND offenders "${name}:${line_number}: ${stripped}")
            endif()
        endforeach()
    endforeach()
    if(relative IN_LIST existence_only)
        list(APPEND seen_existence_only "${relative}")
    endif()
endforeach()

# Not vacuous: a moved or renamed detector fails here rather than passing.
foreach(required IN LISTS existence_only)
    if(NOT required IN_LIST seen_existence_only)
        message(FATAL_ERROR "no-vendor-credentials check: ${required} not found -- the detector "
                            "moved; update the exemption with it")
    endif()
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
