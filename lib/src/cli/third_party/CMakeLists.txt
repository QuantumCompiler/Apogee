# Third-party code. READ-ONLY: nothing under this directory is ever edited as
# part of Apogee work (a Core constraint -- see CLAUDE.md -> Invariants). Fixes
# go upstream; local needs are met by wrapping, not patching.

if(NOT APOGEE_ENABLE_LLAMA)
    # Default path. llama.cpp's build is heavy (Metal/CUDA kernel compilation),
    # so it stays off the per-push hot path and is compile-proofed by a separate
    # non-blocking CI job until the llamacpp-backend item consumes its API.
    message(STATUS "Apogee: llama.cpp disabled (-DAPOGEE_ENABLE_LLAMA=ON to build it)")
    return()
endif()

include(FetchContent)

# Pinned to the exact revision Ommi's local-inference path is built against, so
# the llamacpp-backend item starts from ground already proven in the reference
# implementation. Updating this pin is a DELIBERATE event: it gets its own
# commit, and the message says what changed upstream and why we want it.
FetchContent_Declare(llama_cpp
    GIT_REPOSITORY https://github.com/ggml-org/llama.cpp
    GIT_TAG        549b9d84330c327e6791fa812a7d60c0cf63572e
)

# Library only -- Apogee links llama.cpp in-process (SPEC.md -> Background) and
# has no use for its bundled binaries, and each one we skip is build time saved.
#
# LLAMA_BUILD_COMMON stays ON because `mtmd` (below) needs it: upstream puts the
# multimodal library under tools/, and its companion CLIs link llama-common.
# Everything else in tools/ remains off.
set(LLAMA_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER   OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TOOLS    OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_COMMON   ON  CACHE BOOL "" FORCE)
set(LLAMA_CURL           OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(llama_cpp)

# Multimodal (vision) support.
#
# Upstream ships this as a library called `mtmd` under `tools/mtmd`, which means
# the ONLY documented way to get it is LLAMA_BUILD_TOOLS=ON -- and that builds
# llama-bench, perplexity, quantize, and half a dozen other binaries Apogee has
# no use for. Adding the one subdirectory is the narrow version of the same
# thing: `mtmd` is a self-contained target (`PUBLIC ggml llama`, `PRIVATE
# Threads`) with no dependency on the rest of tools/.
#
# It is added AFTER MakeAvailable so `llama` and `ggml` already exist as targets,
# and only when the source tree actually carries it -- a future pin bump that
# moved or renamed it should produce a clear message here rather than a wall of
# missing-target errors.
if(EXISTS "${llama_cpp_SOURCE_DIR}/tools/mtmd/CMakeLists.txt")
    # `mtmd` versions itself from LLAMA_INSTALL_VERSION, which llama.cpp sets in
    # its own top-level scope. Adding the subdirectory from HERE is a sibling
    # scope, so that variable is not inherited and set_target_properties fails
    # with an argument-count error that says nothing about the real cause.
    # Mirroring upstream's own definition is enough.
    if(NOT DEFINED LLAMA_INSTALL_VERSION)
        set(LLAMA_INSTALL_VERSION 0.0.0)
    endif()
    add_subdirectory("${llama_cpp_SOURCE_DIR}/tools/mtmd" "${llama_cpp_BINARY_DIR}/tools/mtmd")
    message(STATUS "Apogee: llama.cpp multimodal (mtmd) enabled")
else()
    message(WARNING
        "Apogee: llama.cpp has no tools/mtmd at this pin -- local vision will report "
        "unavailable. Check whether upstream moved it.")
endif()

message(STATUS "Apogee: llama.cpp enabled (compile-proof only -- no API use yet)")
