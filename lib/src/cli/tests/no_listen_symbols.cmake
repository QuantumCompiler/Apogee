# The interactive-never-listens invariant, locked at the symbol level.
#
# SPEC.md -> Principles: "No interactive turn on any backend opens a listening
# socket; only `serve` owns a port."
#
# This is the DETERMINISTIC half of that lock. No timing, no sampling, no
# flakiness -- either an object file references listen()/accept() or it does
# not. It runs beside the runtime lsof check, and neither is sufficient alone:
#
#   symbols  -- catches code calling listen/accept, however briefly, in our
#               objects AND in every third-party archive linked beside them
#               (llama.cpp runs in-process). Cannot see a CHILD PROCESS that
#               listens.
#   runtime  -- catches a spawned server holding a port. Cannot see a
#               sub-millisecond window.
#
# ## The one reviewed exception
#
# `apogee serve` exists, so the binary DOES reference listen() and accept() --
# from exactly one object: httpserver/serve.cpp, the listener. That object is
# allow-listed here BY NAME (APOGEE_LISTENER_OBJECT), and the check attributes
# every reference to the object that made it: `nm -A -u` per archive rather
# than `nm -u` on the linked executable, where the symbols would appear once
# with no way to say which code brought them. Every other object -- ours or a
# dependency's -- referencing listen/accept fails the build.
#
# Moving the listener to another file is a deliberate edit to the allow-list.
# It cannot happen by accident, because the check ALSO requires the allow-listed
# object to be seen referencing the symbols: a check that saw no symbols at all
# -- nm's format changed, the listener moved, the archive list went stale --
# would otherwise pass vacuously, which is the failure the two guards below
# exist to make impossible.
#
# ## Shared libraries are scanned too
#
# llama.cpp builds as SHARED libraries under the llama preset, and a shared
# library's own imports never appear in the executable's undefined-symbol
# table -- so the earlier version of this check, which scanned the linked
# binary, was blind to llama.cpp as actually built, while claiming to cover
# it (found when the archive walk listed three files where llama.cpp should
# have been). Every library THIS build produces is therefore scanned, static
# or shared; a shared one is reported by file rather than by member object.
# Imported system libraries are not: libcurl legitimately calls accept() for
# FTP, and the platform's libraries are not Apogee's code.
#
# Inputs:
#   APOGEE_ARCHIVES         `|`-separated libraries: apogee_core plus every
#                           static or shared library the executable links,
#                           transitively, that this build produces (collected
#                           by tests/CMakeLists.txt so a new dependency is
#                           scanned without anyone remembering).
#   APOGEE_LISTENER_OBJECT  the one object allowed to reference listen/accept.

if(NOT DEFINED APOGEE_ARCHIVES)
    message(FATAL_ERROR "APOGEE_ARCHIVES must be set")
endif()
if(NOT DEFINED APOGEE_LISTENER_OBJECT)
    set(APOGEE_LISTENER_OBJECT "serve.cpp.o")
endif()

string(REPLACE "|" ";" ARCHIVES "${APOGEE_ARCHIVES}")
list(LENGTH ARCHIVES archive_count)
if(archive_count EQUAL 0)
    message(FATAL_ERROR "no-listen symbol check: no archives to scan -- this check would pass vacuously")
endif()

find_program(NM_TOOL NAMES nm llvm-nm)
if(NOT NM_TOOL)
    # Windows would need dumpbin /IMPORTS; recorded as an open per-item skip in
    # CLAUDE.md -> Platforms rather than left silently unchecked.
    message(STATUS "no-listen symbol check: nm not found; skipping")
    return()
endif()

set(VIOLATIONS "")
set(LISTENER_SEEN FALSE)
set(SCANNED 0)
foreach(archive IN LISTS ARCHIVES)
    if(NOT EXISTS "${archive}")
        message(FATAL_ERROR "no-listen symbol check: not found: ${archive}")
    endif()
    execute_process(
        COMMAND "${NM_TOOL}" -A -u "${archive}"
        OUTPUT_VARIABLE SYMBOLS
        ERROR_VARIABLE NM_ERR
        RESULT_VARIABLE NM_STATUS
    )
    if(NOT NM_STATUS EQUAL 0)
        message(FATAL_ERROR "nm failed on ${archive}: ${NM_ERR}")
    endif()
    math(EXPR SCANNED "${SCANNED} + 1")
    get_filename_component(archive_name "${archive}" NAME)

    string(REPLACE "\n" ";" SYMBOL_LINES "${SYMBOLS}")
    foreach(line IN LISTS SYMBOL_LINES)
        string(STRIP "${line}" line)
        # Undefined symbols, attributed to their member object. Formats:
        #   macOS archive   "libapogee_core.a:serve.cpp.o: _listen"
        #   GNU archive     "libapogee_core.a:serve.cpp.o:                 U listen"
        #   shared library  "libllama.dylib: _listen"  /  "libllama.so: U listen@GLIBC_2.2.5"
        # A glibc version suffix is tolerated; a member is present only for an
        # archive, so a shared library is reported by its file name.
        if(line MATCHES "[:(]([^:( ]+\\.o)\\)?:[ \t]*(U[ \t]+)?_?(listen|accept|accept4)(@.*)?$")
            set(member "${CMAKE_MATCH_1}")
            set(symbol "${CMAKE_MATCH_3}")
            if(member STREQUAL "${APOGEE_LISTENER_OBJECT}")
                set(LISTENER_SEEN TRUE)
            else()
                list(APPEND VIOLATIONS "${archive_name}(${member}): ${symbol}")
            endif()
        elseif(line MATCHES "^[^ ]+:[ \t]*(U[ \t]+)?_?(listen|accept|accept4)(@.*)?$")
            list(APPEND VIOLATIONS "${archive_name}: ${CMAKE_MATCH_2}")
        endif()
    endforeach()
endforeach()

if(VIOLATIONS)
    string(REPLACE ";" "\n  " pretty "${VIOLATIONS}")
    message(FATAL_ERROR
        "INVARIANT VIOLATED: socket-server calls are reachable outside the listener:\n  ${pretty}\n"
        "No interactive turn may open a listening socket -- only 'apogee serve' "
        "owns a port, and only ${APOGEE_LISTENER_OBJECT} may reference listen/accept. "
        "See SPEC.md -> Principles and CLAUDE.md -> Invariants.")
endif()

if(NOT LISTENER_SEEN)
    message(FATAL_ERROR
        "no-listen symbol check: ${APOGEE_LISTENER_OBJECT} referenced neither listen nor "
        "accept in any of ${SCANNED} librar(y/ies). Either the listener moved -- update "
        "APOGEE_LISTENER_OBJECT deliberately -- or nm's output is not being parsed, and a "
        "check that sees no symbols passes vacuously.")
endif()

message(STATUS "no-listen symbol check: ${SCANNED} librar(y/ies) scanned; listen/accept referenced only by ${APOGEE_LISTENER_OBJECT} - OK")
