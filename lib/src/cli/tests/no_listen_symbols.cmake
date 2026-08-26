# The interactive-never-listens invariant, locked at the symbol level.
#
# SPEC.md -> Principles: "No interactive turn on any backend opens a listening
# socket; only `serve` owns a port."
#
# This is the DETERMINISTIC half of that lock: `apogee_core` must not reference
# listen(), accept(), or bind() at all. No timing, no sampling, no flakiness --
# either the symbol is in the library or it is not.
#
# Why it exists alongside the runtime lsof check: sampling a live process from
# outside cannot see a socket held for a millisecond, and a deliberately
# violating build DID pass the runtime check for exactly that reason. The two
# are complementary and neither is sufficient:
#
#   symbols  -- catches Apogee's own code calling listen/accept/bind, however
#               briefly. Cannot see a CHILD PROCESS that listens.
#   runtime  -- catches a spawned server (a local inference server, a proxy)
#               holding a port. Cannot see a sub-millisecond window.
#
# When `serve` lands it will need its own target excluded from this check --
# and that exclusion should be a deliberate, reviewed edit, which is the point.

if(NOT DEFINED APOGEE_CORE_LIB)
    message(FATAL_ERROR "APOGEE_CORE_LIB must be set")
endif()

if(NOT EXISTS "${APOGEE_CORE_LIB}")
    message(FATAL_ERROR "not found: ${APOGEE_CORE_LIB}")
endif()

find_program(NM_TOOL NAMES nm llvm-nm)
if(NOT NM_TOOL)
    # Windows would need dumpbin /IMPORTS; recorded as an open per-item skip in
    # CLAUDE.md -> Platforms rather than left silently unchecked.
    message(STATUS "no-listen symbol check: nm not found; skipping")
    return()
endif()

execute_process(
    COMMAND "${NM_TOOL}" -u "${APOGEE_CORE_LIB}"
    OUTPUT_VARIABLE SYMBOLS
    ERROR_VARIABLE NM_ERR
    RESULT_VARIABLE NM_STATUS
)
if(NOT NM_STATUS EQUAL 0)
    message(FATAL_ERROR "nm failed on ${APOGEE_CORE_LIB}: ${NM_ERR}")
endif()

string(REPLACE "\n" ";" SYMBOL_LINES "${SYMBOLS}")

set(FOUND "")
foreach(line IN LISTS SYMBOL_LINES)
    string(STRIP "${line}" line)
    # Undefined symbols appear as "U _listen" (macOS) or "U listen" (ELF).
    if(line MATCHES "(^|[ \t])_?(listen|accept|accept4)$")
        list(APPEND FOUND "${line}")
    endif()
endforeach()

if(FOUND)
    string(REPLACE ";" "\n  " pretty "${FOUND}")
    message(FATAL_ERROR
        "INVARIANT VIOLATED: apogee_core references socket-server calls:\n  ${pretty}\n"
        "No interactive turn may open a listening socket -- only 'apogee serve' "
        "owns a port. See SPEC.md -> Principles.")
endif()

message(STATUS "no-listen symbol check: apogee_core references no listen/accept - OK")
