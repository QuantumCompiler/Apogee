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
set(LLAMA_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER   OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TOOLS    OFF CACHE BOOL "" FORCE)
set(LLAMA_CURL           OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(llama_cpp)

message(STATUS "Apogee: llama.cpp enabled (compile-proof only -- no API use yet)")
