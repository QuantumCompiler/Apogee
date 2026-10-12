#!/usr/bin/env bash
# cross-windows.sh -- build the windows-x64 target on a Mac or Linux machine
# with MinGW-w64, as CI's `build windows-x64` job builds it on its runner.
#
#   cross-windows.sh [--jobs N]     from anywhere inside a checkout
#
# The same preset as the runner (windows-x64: GCC, static, llama.cpp on, every
# target): each file compiled with the toolchain CI uses on that platform,
# everything linked, and each image's size checked against the 2 GiB Windows
# refuses to load (cmake/ApogeeImageSize.cmake). Nothing it builds can run
# here, so it rehearses the compile and the link, not the tests; their
# discovery waits for ctest on Windows (tests/CMakeLists.txt). The output is
# the checkout's build/windows-x64.
#
# It needs x86_64-w64-mingw32-gcc and -g++ (brew install mingw-w64), cmake,
# ninja and pkg-config. The runner's libcurl is MSYS2's, which a Mac has no
# copy of, so a static libcurl with Schannel is built here once, from curl's
# source at a pinned tag, into $APOGEE_CROSS_DIR (default
# ~/.cache/apogee/cross/windows-x64), and kept.
#
# Exit status: the build's, or 3 when the toolchain is missing -- pr-ci.sh
# reports that as skipped, saying how to install it.
set -euo pipefail

CURL_TAG="curl-8_11_1"
TRIPLE="x86_64-w64-mingw32"

log() { printf '[cross-windows] %s\n' "$*"; }
die() {
    printf '[cross-windows] error: %s\n' "$*" >&2
    exit 1
}

JOBS="$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)"
while [ $# -gt 0 ]; do
    case "$1" in
        -j | --jobs)
            [ $# -ge 2 ] || die "--jobs needs a number"
            JOBS="$2"
            shift
            ;;
        -h | --help)
            sed -n '2,5p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *) die "unknown option: $1 (see --help)" ;;
    esac
    shift
done

missing=""
for tool in "$TRIPLE-gcc" "$TRIPLE-g++" "$TRIPLE-windres" cmake ninja pkg-config git; do
    command -v "$tool" >/dev/null 2>&1 || missing="$missing $tool"
done
if [ -n "$missing" ]; then
    printf '[cross-windows] cannot cross-build here: missing%s (brew install mingw-w64 ninja pkg-config)\n' "$missing" >&2
    exit 3
fi

top="$(git rev-parse --show-toplevel 2>/dev/null)" || die "not inside a git repository"
cli="$top/lib/src/cli"
dir="${APOGEE_CROSS_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/apogee/cross/windows-x64}"
prefix="$dir/prefix"
toolchain="$dir/mingw-x64.cmake"
mkdir -p "$dir"

# The toolchain: the compilers by name, and the libraries looked for only in
# MinGW's own tree and the prefix below -- never the host's.
sysroot="$(dirname "$(dirname "$(command -v "$TRIPLE-gcc")")")"
for candidate in "$sysroot/$TRIPLE" "$sysroot/toolchain-x86_64/$TRIPLE" \
    "$(brew --prefix mingw-w64 2>/dev/null)/toolchain-x86_64/$TRIPLE"; do
    [ -d "$candidate/include" ] && { sysroot="$candidate"; break; }
done
cat >"$toolchain.new" <<EOF
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER $TRIPLE-gcc)
set(CMAKE_CXX_COMPILER $TRIPLE-g++)
set(CMAKE_RC_COMPILER $TRIPLE-windres)
set(CMAKE_FIND_ROOT_PATH "$sysroot" "$prefix")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
EOF
if cmp -s "$toolchain.new" "$toolchain"; then rm "$toolchain.new"; else mv "$toolchain.new" "$toolchain"; fi

# libcurl for Windows, once: static, Schannel (the runner's curl-winssl),
# HTTP only, nothing else linked in.
if [ ! -f "$prefix/lib/libcurl.a" ] || [ "$(cat "$prefix/.tag" 2>/dev/null)" != "$CURL_TAG" ]; then
    log "building a static libcurl ($CURL_TAG, Schannel) for Windows into $prefix"
    rm -rf "$dir/curl-src" "$dir/curl-build" "$prefix"
    git clone --quiet --depth 1 --branch "$CURL_TAG" https://github.com/curl/curl.git "$dir/curl-src"
    cmake -S "$dir/curl-src" -B "$dir/curl-build" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$toolchain" -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$prefix" -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON \
        -DCURL_USE_SCHANNEL=ON -DHTTP_ONLY=ON -DCURL_USE_LIBPSL=OFF -DCURL_USE_LIBSSH2=OFF \
        -DUSE_NGHTTP2=OFF -DCURL_ZLIB=OFF -DCURL_BROTLI=OFF -DCURL_ZSTD=OFF -DUSE_LIBIDN2=OFF \
        -DBUILD_CURL_EXE=OFF -DBUILD_TESTING=OFF -DBUILD_LIBCURL_DOCS=OFF -DBUILD_MISC_DOCS=OFF \
        -DENABLE_CURL_MANUAL=OFF >"$dir/curl.log" 2>&1 || die "configuring curl failed (see $dir/curl.log)"
    cmake --build "$dir/curl-build" -j "$JOBS" >>"$dir/curl.log" 2>&1 || die "building curl failed (see $dir/curl.log)"
    cmake --install "$dir/curl-build" >>"$dir/curl.log" 2>&1 || die "installing curl failed (see $dir/curl.log)"
    printf '%s\n' "$CURL_TAG" >"$prefix/.tag"
fi

# The runner's preset, with the compilers named again: a preset's cache entry
# (gcc, g++) beats the toolchain file, and on a Mac `gcc` is Apple's clang.
cd "$cli"
log "configuring windows-x64 ($TRIPLE-g++ $("$TRIPLE-g++" -dumpfullversion))"
export PKG_CONFIG_LIBDIR="$prefix/lib/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR=""
cmake --preset windows-x64 \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
    -DCMAKE_C_COMPILER="$TRIPLE-gcc" -DCMAKE_CXX_COMPILER="$TRIPLE-g++" \
    -DCURL_DIR="$prefix/lib/cmake/CURL"
grep -q "^CMAKE_CXX_COMPILER:[A-Z]*=.*$TRIPLE-g++" build/windows-x64/CMakeCache.txt ||
    die "build/windows-x64 is not configured with $TRIPLE-g++ -- remove its CMakeCache.txt and run again"

log "building every target with $JOBS jobs"
cmake --build build/windows-x64 -j "$JOBS"
for image in build/windows-x64/source/*.exe build/windows-x64/tests/apogee_tests.exe; do
    [ -f "$image" ] && log "built $image ($(($(wc -c <"$image") / 1048576)) MiB)"
done
log "windows-x64 compiles and links; its tests run on CI's Windows runner"
