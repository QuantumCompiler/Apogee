#!/usr/bin/env bash
# cicd.sh — Apogee's CI/CD entry point.
#
# Builds the project from the branch the repo is currently on, for one or more
# of the six release targets:
#
#   linux-x64  linux-arm64  macos-x64  macos-arm64  windows-x64  windows-arm64
#
# A host can only natively compile the targets its own OS supports (macOS hosts
# build both Mac architectures; Linux and Windows hosts build their own arch).
# Requested targets this host cannot build are reported and DEFERRED to CI:
# the GitHub Actions matrix runs one native runner per target, and each runner
# invokes THIS script with its native --platform — one build path, everywhere.
#
# With --fresh it performs a CI-style clean-room build: clone the repo at the
# current branch into a temp directory and build there.
#
# Tab completion: source lib/scripts/cicd-completion.bash (see that file).
set -euo pipefail

REPO_URL="https://github.com/QuantumCompiler/Apogee"
ALL_TARGETS=(linux-x64 linux-arm64 macos-x64 macos-arm64 windows-x64 windows-arm64)

CLEAN=0
RUN_TESTS=0
FRESH=0
BRANCH=""
PLATFORMS=()
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)"

usage() {
    cat <<EOF
Usage: cicd.sh [options]

Builds Apogee from the branch this repo is currently on.

Options:
  -p, --platform T     Target to build: ${ALL_TARGETS[*]}, or 'all'.
                       Repeatable. Default: this machine's native target.
                       Targets this host cannot compile are listed and left
                       to the CI matrix (whose runners call this script).
  -c, --clean          Remove the target's build directory before building
  -t, --test           Run the test suite (ctest) after a successful build
                       (host-native target only — cross-built binaries can't run here)
  -f, --fresh          CI-style clean-room build: clone ${REPO_URL}
                       at the current branch into a temp dir and build there
  -b, --branch NAME    Branch to build (implies --fresh; default: the branch
                       currently checked out)
  -j, --jobs N         Parallel build jobs (default: ${JOBS})
  -h, --help           Show this help
EOF
}

log() { printf '[cicd] %s\n' "$*"; }
die() { printf '[cicd] error: %s\n' "$*" >&2; exit 1; }

host_target() {
    local os arch
    case "$(uname -s)" in
        Darwin)                 os="macos" ;;
        Linux)                  os="linux" ;;
        MINGW*|MSYS*|CYGWIN*)   os="windows" ;;
        *)                      die "unsupported host OS: $(uname -s)" ;;
    esac
    case "$(uname -m)" in
        arm64|aarch64)  arch="arm64" ;;
        x86_64|amd64)   arch="x64" ;;
        *)              die "unsupported host arch: $(uname -m)" ;;
    esac
    printf '%s-%s' "$os" "$arch"
}

valid_target() {
    local t
    for t in "${ALL_TARGETS[@]}"; do [[ "$1" == "$t" ]] && return 0; done
    return 1
}

# Can this host natively compile the given target?
buildable_here() {
    local host="$1" target="$2"
    case "${host%%-*}" in
        macos)   [[ "${target%%-*}" == "macos" ]] ;;   # both arches via CMAKE_OSX_ARCHITECTURES
        linux)   [[ "$target" == "$host" ]] ;;
        windows) [[ "$target" == "$host" ]] ;;
    esac
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -p|--platform)
            [[ $# -ge 2 ]] || die "--platform needs a target"
            if [[ "$2" == "all" ]]; then
                PLATFORMS=("${ALL_TARGETS[@]}")
            else
                valid_target "$2" || die "unknown platform '$2' (valid: ${ALL_TARGETS[*]}, all)"
                PLATFORMS+=("$2")
            fi
            shift ;;
        -c|--clean)  CLEAN=1 ;;
        -t|--test)   RUN_TESTS=1 ;;
        -f|--fresh)  FRESH=1 ;;
        -b|--branch) [[ $# -ge 2 ]] || die "--branch needs a name"; BRANCH="$2"; FRESH=1; shift ;;
        -j|--jobs)   [[ $# -ge 2 ]] || die "--jobs needs a number"; JOBS="$2"; shift ;;
        -h|--help)   usage; exit 0 ;;
        *)           die "unknown option: $1 (see --help)" ;;
    esac
    shift
done

HOST="$(host_target)"
[[ ${#PLATFORMS[@]} -gt 0 ]] || PLATFORMS=("$HOST")

# Builds one target in the checkout at $1. Succeeds with a notice while the
# repo is docs-only; becomes a real CMake build once the C++ skeleton lands.
build_target() {
    local root="$1" target="$2"
    local build_dir="build/${target}"
    cd "$root"

    if [[ ! -f CMakeLists.txt ]]; then
        log "NOTICE: no CMakeLists.txt at ${root}."
        log "Nothing to build yet — the C++ skeleton (backlog: cpp-project-skeleton) hasn't landed."
        log "This script is the standing CI/CD entry point and will build via CMake once it does."
        return 0
    fi

    [[ $CLEAN -eq 1 ]] && { log "cleaning ${build_dir}/"; rm -rf "${build_dir}"; }

    # The skeleton item defines one CMake preset per target, named after it.
    if [[ -f CMakePresets.json ]] && cmake --list-presets 2>/dev/null | grep -q "\"${target}\""; then
        log "[${target}] configuring (cmake --preset ${target})"
        cmake --preset "${target}"
    else
        local extra=()
        if [[ "${target%%-*}" == "macos" ]]; then
            [[ "${target#macos-}" == "arm64" ]] && extra+=(-DCMAKE_OSX_ARCHITECTURES=arm64) \
                                                || extra+=(-DCMAKE_OSX_ARCHITECTURES=x86_64)
        fi
        log "[${target}] configuring (cmake -S . -B ${build_dir})"
        cmake -S . -B "${build_dir}" "${extra[@]}"
    fi

    log "[${target}] building with ${JOBS} jobs"
    cmake --build "${build_dir}" -j "${JOBS}"

    if [[ $RUN_TESTS -eq 1 ]]; then
        if [[ "$target" == "$HOST" ]]; then
            log "[${target}] running tests"
            ctest --test-dir "${build_dir}" --output-on-failure
        else
            log "[${target}] skipping tests: cross-built binaries can't run on ${HOST}"
        fi
    fi

    log "[${target}] done (branch '$(git -C "$root" rev-parse --abbrev-ref HEAD)')"
}

build_all_requested() {
    local root="$1" target deferred=()
    for target in "${PLATFORMS[@]}"; do
        if buildable_here "$HOST" "$target"; then
            build_target "$root" "$target"
        else
            deferred+=("$target")
        fi
    done
    if [[ ${#deferred[@]} -gt 0 ]]; then
        log "deferred to CI (not natively buildable on ${HOST}): ${deferred[*]}"
        log "the GitHub Actions matrix builds these on their native runners via this same script"
    fi
}

repo_root="$(git rev-parse --show-toplevel 2>/dev/null)" || die "not inside a git repository"
current_branch="$(git -C "$repo_root" rev-parse --abbrev-ref HEAD)"
[[ -n "$BRANCH" ]] || BRANCH="$current_branch"

if [[ $FRESH -eq 1 ]]; then
    workdir="$(mktemp -d "${TMPDIR:-/tmp}/apogee-cicd.XXXXXX")"
    log "fresh clean-room build of branch '${BRANCH}' from ${REPO_URL}"
    log "workdir: ${workdir}"
    git clone --branch "$BRANCH" --recurse-submodules "$REPO_URL" "${workdir}/Apogee"
    build_all_requested "${workdir}/Apogee"
    log "clean-room checkout kept at ${workdir}/Apogee (delete it when done)"
else
    log "building branch '${current_branch}' in place — targets: ${PLATFORMS[*]} (host: ${HOST})"
    build_all_requested "$repo_root"
fi
