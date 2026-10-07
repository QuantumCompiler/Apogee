# tree-sitter and the language grammars the code graph parses with (27k).
# READ-ONLY like everything under third_party/: nothing fetched here is ever
# edited -- a grammar that parses something wrong is fixed upstream and its
# pin moved, or worked around in graph/code_languages.cpp's queries.
#
# **Fetched, never found, and each grammar its own pin.** The code graph's
# golden fixtures pin the exact nodes and edges a parse produces, and those
# follow from the exact grammar: a system tree-sitter or a packaged grammar of
# another version would parse the same file into a different tree. So, like
# cpp-httplib, there is no FIND_PACKAGE_ARGS here, and every grammar names its
# own commit -- moving one is a deliberate event: its own commit, the message
# saying what changed upstream, and the goldens it moves regenerated in the
# same change. The extractor version in graph/code_languages.cpp names each
# grammar's tag, so a pin move re-parses every file of that language on the
# next `graph update` rather than mixing two grammars' facts in one graph.
#
# **Archives at exact commits, content-hashed.** A grammar's repository
# carries every regeneration of its multi-megabyte parser.c in its history, so
# a clone is hundreds of megabytes for one file; the archive of the pinned
# commit is about a megabyte, and URL_HASH makes the pin exact twice over.
#
# **Built as plain C, here.** Upstream's own CMake (core and grammars alike)
# builds a shared library, generates pkg-config files and installs; Apogee
# links everything statically into one executable (the top-level
# CMakeLists says why) and wants none of that. SOURCE_SUBDIR points at a
# directory no archive has, so FetchContent only downloads and unpacks; the
# targets below compile the sources directly -- the core as its one-file
# amalgamation (lib/src/lib.c, upstream's AMALGAMATED build), each grammar
# as its generated parser.c plus its hand-written scanner.c when it has one.
# All C11, no C++ -- the grammars build on every target the CLI does, MinGW
# included, with no toolchain beyond the C compiler SQLite already needs.
#
# **Out of the lint and format scope** like llama.cpp: only source/ and
# tests/ are formatted or linted (Makefile), compiled with warnings off
# (third-party warnings are not ours to fix), headers SYSTEM.
#
# Measured cost (2026-10-04, macOS arm64, Apple clang, RelWithDebInfo): the
# runtime and the twelve grammars compile in about 8 s of CPU time run one
# after another (the runtime's amalgamation the longest), well under 1% of a
# no-llama build's compile work; MinGW-w64 GCC 16 builds the same sources
# without a warning in about 11 s. In the executable they are about 17.6 MB
# of code and parse tables -- C# 5.1, C++ 3.3, Ruby 2.0 -- and only +1.4 MB
# compressed. A configure downloads about 7 MB for them, once. Recorded in
# MILESTONES.md (Milestone AE) and third_party/README.md.
include(FetchContent)

# One fetch: an archive at an exact commit, its hash, and no add_subdirectory.
macro(apogee_fetch_tree_sitter name url sha256)
    FetchContent_Declare(${name}
        URL           ${url}
        URL_HASH      SHA256=${sha256}
        SOURCE_SUBDIR apogee-populate-only
    )
    FetchContent_MakeAvailable(${name})
endmacro()

# The runtime: v0.27.0. Supports grammar ABI 13..15; every grammar below is
# ABI 14 or 15 (each parser.c's LANGUAGE_VERSION), checked when pinned.
apogee_fetch_tree_sitter(tree_sitter
    https://github.com/tree-sitter/tree-sitter/archive/6070dbfefd326bd735e5683eb128cc1b57dad0c0.tar.gz
    ae938ad46922cecb97eb3f1ec315aa39d9f4443ed3af62a24bd244a93e9321c9)

add_library(apogee_tree_sitter STATIC "${tree_sitter_SOURCE_DIR}/lib/src/lib.c")
target_include_directories(apogee_tree_sitter SYSTEM PUBLIC "${tree_sitter_SOURCE_DIR}/lib/include")
target_include_directories(apogee_tree_sitter PRIVATE
    "${tree_sitter_SOURCE_DIR}/lib/src"
    "${tree_sitter_SOURCE_DIR}/lib/src/wasm")
# Upstream's own definitions for the runtime (its CMakeLists.txt); the Wasm
# feature stays off, as upstream defaults it.
target_compile_definitions(apogee_tree_sitter PRIVATE
    _POSIX_C_SOURCE=200112L _DEFAULT_SOURCE _BSD_SOURCE _DARWIN_C_SOURCE)
set_target_properties(apogee_tree_sitter PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)

# One static library per grammar: `apogee_ts_<id>`, from `<dir>/parser.c` and
# `<dir>/scanner.c` when the grammar has an external scanner. A grammar
# includes its own `tree_sitter/parser.h` from beside its parser.c, so its
# include directory is its own src/ and nothing else.
set(APOGEE_TREE_SITTER_GRAMMARS "")
function(apogee_tree_sitter_grammar id src_dir)
    set(sources "${src_dir}/parser.c")
    if(EXISTS "${src_dir}/scanner.c")
        list(APPEND sources "${src_dir}/scanner.c")
    endif()
    add_library(apogee_ts_${id} STATIC ${sources})
    target_include_directories(apogee_ts_${id} PRIVATE "${src_dir}")
    set_target_properties(apogee_ts_${id} PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
    set(APOGEE_TREE_SITTER_GRAMMARS ${APOGEE_TREE_SITTER_GRAMMARS} apogee_ts_${id} PARENT_SCOPE)
endfunction()

# C -- v0.24.2, MIT.
apogee_fetch_tree_sitter(tree_sitter_c
    https://github.com/tree-sitter/tree-sitter-c/archive/b780e47fc780ddc8da13afa35a3f4ed5c157823d.tar.gz
    dc3979e40fce678c27273ba8ab93b48b5acd6b91d1dfde6a4451222d366d689f)
apogee_tree_sitter_grammar(c "${tree_sitter_c_SOURCE_DIR}/src")

# C++ -- v0.23.4, MIT.
apogee_fetch_tree_sitter(tree_sitter_cpp
    https://github.com/tree-sitter/tree-sitter-cpp/archive/f41e1a044c8a84ea9fa8577fdd2eab92ec96de02.tar.gz
    ad4b9b56602e9b14082637988f0042521006819960ed8c308bc24ded9ceb9331)
apogee_tree_sitter_grammar(cpp "${tree_sitter_cpp_SOURCE_DIR}/src")

# Python -- v0.25.0, MIT.
apogee_fetch_tree_sitter(tree_sitter_python
    https://github.com/tree-sitter/tree-sitter-python/archive/293fdc02038ee2bf0e2e206711b69c90ac0d413f.tar.gz
    b74d8ba6730535354175436208fe1452fd464f235263959ca40b8bf32cb23dc6)
apogee_tree_sitter_grammar(python "${tree_sitter_python_SOURCE_DIR}/src")

# JavaScript (and JSX) -- v0.25.0, MIT.
apogee_fetch_tree_sitter(tree_sitter_javascript
    https://github.com/tree-sitter/tree-sitter-javascript/archive/44c892e0be055ac465d5eeddae6d3e194424e7de.tar.gz
    a13fa28148e41bb939b2c2b8cd73c4b4295273b70cbac55a7a589020f52b9611)
apogee_tree_sitter_grammar(javascript "${tree_sitter_javascript_SOURCE_DIR}/src")

# TypeScript and TSX -- v0.23.2, MIT: one repository, two grammars.
apogee_fetch_tree_sitter(tree_sitter_typescript
    https://github.com/tree-sitter/tree-sitter-typescript/archive/f975a621f4e7f532fe322e13c4f79495e0a7b2e7.tar.gz
    4de2e82e557810eecb93cb3b31fbb3bd28ba4c91ba9a6c6164bcaa074125b7b5)
apogee_tree_sitter_grammar(typescript "${tree_sitter_typescript_SOURCE_DIR}/typescript/src")
apogee_tree_sitter_grammar(tsx "${tree_sitter_typescript_SOURCE_DIR}/tsx/src")

# Go -- v0.25.0, MIT.
apogee_fetch_tree_sitter(tree_sitter_go
    https://github.com/tree-sitter/tree-sitter-go/archive/1547678a9da59885853f5f5cc8a99cc203fa2e2c.tar.gz
    a71543d63f82b917457f47a3f6dbecd8087592fabc49a757da82c3e00c0e93de)
apogee_tree_sitter_grammar(go "${tree_sitter_go_SOURCE_DIR}/src")

# Rust -- v0.24.2, MIT.
apogee_fetch_tree_sitter(tree_sitter_rust
    https://github.com/tree-sitter/tree-sitter-rust/archive/77a3747266f4d621d0757825e6b11edcbf991ca5.tar.gz
    dee82ddfd01bfc3a8ed201cc03b56448107d3217a4b3a7a8fc7fa6bc32b2405b)
apogee_tree_sitter_grammar(rust "${tree_sitter_rust_SOURCE_DIR}/src")

# Java -- v0.23.5, MIT.
apogee_fetch_tree_sitter(tree_sitter_java
    https://github.com/tree-sitter/tree-sitter-java/archive/94703d5a6bed02b98e438d7cad1136c01a60ba2c.tar.gz
    39dea56cc3bb5fb1658f79b195a92b6f08885886bca636adf2dc6243af641c47)
apogee_tree_sitter_grammar(java "${tree_sitter_java_SOURCE_DIR}/src")

# C# -- v0.23.5, MIT. The largest parser.c of the set (28 MB of tables).
apogee_fetch_tree_sitter(tree_sitter_c_sharp
    https://github.com/tree-sitter/tree-sitter-c-sharp/archive/cac6d5fb595f5811a076336682d5d595ac1c9e85.tar.gz
    e4cd8a1c118a1e176931e9e0a635fc55e3d0f69e1649e40b74d72bda918e948d)
apogee_tree_sitter_grammar(c_sharp "${tree_sitter_c_sharp_SOURCE_DIR}/src")

# Ruby -- v0.23.1, MIT.
apogee_fetch_tree_sitter(tree_sitter_ruby
    https://github.com/tree-sitter/tree-sitter-ruby/archive/71bd32fb7607035768799732addba884a37a6210.tar.gz
    e520d4cda2805db82b5a44ec84ba048e30d91770851b4a173490e0d45e4cb555)
apogee_tree_sitter_grammar(ruby "${tree_sitter_ruby_SOURCE_DIR}/src")

# Bash -- v0.25.1, MIT.
apogee_fetch_tree_sitter(tree_sitter_bash
    https://github.com/tree-sitter/tree-sitter-bash/archive/a06c2e4415e9bc0346c6b86d401879ffb44058f7.tar.gz
    879e8951ea2cc82455407e3eda0293319657ec53e83191e0fb5a67430d10d804)
apogee_tree_sitter_grammar(bash "${tree_sitter_bash_SOURCE_DIR}/src")

# Third-party code: its warnings are not ours to fix (the SQLite precedent).
foreach(_apogee_ts_target apogee_tree_sitter ${APOGEE_TREE_SITTER_GRAMMARS})
    if(MSVC)
        target_compile_options(${_apogee_ts_target} PRIVATE /w)
    else()
        target_compile_options(${_apogee_ts_target} PRIVATE -w)
    endif()
endforeach()

# What the code graph links: the runtime and every grammar, as one name.
add_library(apogee_tree_sitter_all INTERFACE)
target_link_libraries(apogee_tree_sitter_all INTERFACE apogee_tree_sitter ${APOGEE_TREE_SITTER_GRAMMARS})

message(STATUS "Apogee: tree-sitter v0.27.0 with ${APOGEE_TREE_SITTER_GRAMMARS}")
