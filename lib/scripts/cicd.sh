#!/usr/bin/env bash
# cicd.sh — Apogee's CI/CD entry point.
#
# Builds the project from the branch the repo is currently on, for one or more
# of the five release targets:
#
#   linux-x64  linux-arm64  macos-arm64  windows-x64  windows-arm64
#
# A host can only natively compile its own target (its OS and its architecture).
# Requested targets this host cannot build are reported and DEFERRED to CI:
# the GitHub Actions matrix runs one native runner per target, and each runner
# invokes THIS script with its native --platform — one build path, everywhere.
#
# Each application under lib/src/<app>/ is a self-contained CMake project that
# owns its presets, cmake helpers, pinned third-party code, and style config.
# This script builds each one into lib/src/<app>/build/<target>/. Today that is
# just the CLI; the GUI applications join the APPS list when they land.
#
# With --fresh it performs a CI-style clean-room build: clone the repo at the
# current branch into a temp directory and build there.
#
# The CI pipeline (user decision, 2026-09-19) is three stages, and each one is
# a call into this script: `--clone-llama` proves the llama.cpp pin resolves,
# then one build per platform (`--platform T --no-defer`), then one test run
# per platform on the build tree the build stage handed over (`--test-only`).
#
# Tab completion: source lib/scripts/cicd-completion.bash (see that file).
set -euo pipefail

REPO_URL="https://github.com/QuantumCompiler/Apogee"
ALL_TARGETS=(linux-x64 linux-arm64 macos-arm64 windows-x64 windows-arm64)

# Each application under lib/src/<app>/ owns its own self-contained CMake build
# (presets, cmake helpers, third_party, style config). This script drives them;
# the GUI applications join this list when they land.
APPS=(cli)

CLEAN=0
RUN_TESTS=0
TEST_ONLY=0
CLONE_LLAMA=0
FRESH=0
NO_DEFER=0
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
      --test-only      Run the test suite on an EXISTING build directory, with
                       no configure and no build: the third CI stage, on the
                       tree the build stage handed over
      --clone-llama    Clone llama.cpp at the commit the CLI's third_party
                       build pins, prove it resolved, and stop: the first CI
                       stage. Nothing else runs.
  -f, --fresh          CI-style clean-room build: clone ${REPO_URL}
                       at the current branch into a temp dir and build there
  -b, --branch NAME    Branch to build (implies --fresh; default: the branch
                       currently checked out)
  -j, --jobs N         Parallel build jobs (default: ${JOBS})
      --no-defer       Fail instead of deferring a requested target this host
                       cannot build natively. CI passes it: on a runner, a
                       deferred target is a misdetected host, not a convenience.
  -h, --help           Show this help

Environment:
  APOGEE_CMAKE_ARGS    Extra arguments for the configure step, appended after
                       the preset (e.g. a toolchain file for a cross-compile).
EOF
}

log() { printf '[cicd] %s\n' "$*"; }
die() { printf '[cicd] error: %s\n' "$*" >&2; exit 1; }

host_target() {
    local os arch
    case "$(uname -s)" in
        Darwin)                 os="macos" ;;
        Linux)                  os="linux" ;;
        # Git for Windows' bash, and every MSYS2 environment: MINGW64_NT,
        # UCRT64_NT, CLANG64_NT, CLANGARM64_NT, MSYS_NT. CI builds Windows in
        # the MSYS2 shells (MinGW-w64 toolchains).
        MINGW*|MSYS*|CYGWIN*|UCRT*|CLANG*)
                                os="windows" ;;
        *)                      die "unsupported host OS: $(uname -s)" ;;
    esac
    # On Windows the shell (Git for Windows' bash) may itself be an x64 build
    # running under emulation on an ARM64 machine, and `uname -m` then reports
    # the SHELL's architecture, not the machine's. The processor variables are
    # the OS's own answer: PROCESSOR_ARCHITEW6432 is set for an emulated
    # process and names the real machine; PROCESSOR_ARCHITECTURE otherwise.
    local machine
    if [[ "$os" == "windows" ]]; then
        machine="${PROCESSOR_ARCHITEW6432:-${PROCESSOR_ARCHITECTURE:-$(uname -m)}}"
    else
        machine="$(uname -m)"
    fi
    case "$machine" in
        arm64|aarch64|ARM64)   arch="arm64" ;;
        x86_64|amd64|AMD64)    arch="x64" ;;
        *)                     die "unsupported host arch: ${machine}" ;;
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
        macos)   [[ "$target" == "$host" ]] ;;
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
        --test-only) TEST_ONLY=1 ;;
        --clone-llama) CLONE_LLAMA=1 ;;
        -f|--fresh)  FRESH=1 ;;
        --no-defer)  NO_DEFER=1 ;;
        -b|--branch) [[ $# -ge 2 ]] || die "--branch needs a name"; BRANCH="$2"; FRESH=1; shift ;;
        -j|--jobs)   [[ $# -ge 2 ]] || die "--jobs needs a number"; JOBS="$2"; shift ;;
        -h|--help)   usage; exit 0 ;;
        *)           die "unknown option: $1 (see --help)" ;;
    esac
    shift
done

HOST="$(host_target)"
[[ ${#PLATFORMS[@]} -gt 0 ]] || PLATFORMS=("$HOST")
[[ $TEST_ONLY -eq 1 && $CLEAN -eq 1 ]] && die "--test-only tests an existing build; it cannot be combined with --clean"
[[ $TEST_ONLY -eq 1 && $FRESH -eq 1 ]] && die "--test-only tests an existing build; it cannot be combined with --fresh"

# Stage one of the CI pipeline: prove the llama.cpp pin resolves. The pin lives
# in ONE place -- the FetchContent_Declare in the CLI's third_party build --
# and is read from there, so this can never disagree with what the build
# fetches. A shallow fetch of the one commit, checked out and compared.
clone_llama() {
    local decl="$1/lib/src/cli/third_party/CMakeLists.txt"
    local repo sha dir head
    repo="$(awk '/FetchContent_Declare\(llama_cpp/{f=1} f && /GIT_REPOSITORY/{print $2; exit}' "$decl")"
    sha="$(awk '/FetchContent_Declare\(llama_cpp/{f=1} f && /GIT_TAG/{print $2; exit}' "$decl")"
    [[ -n "$repo" && -n "$sha" ]] || die "could not read the llama.cpp pin from ${decl}"
    dir="$(mktemp -d "${TMPDIR:-/tmp}/apogee-llama.XXXXXX")"
    log "cloning llama.cpp ${sha} from ${repo}"
    git -C "$dir" init -q
    git -C "$dir" remote add origin "$repo"
    git -C "$dir" fetch --depth 1 origin "$sha"
    git -C "$dir" checkout -q FETCH_HEAD
    head="$(git -C "$dir" rev-parse HEAD)"
    [[ "$head" == "$sha" ]] || die "llama.cpp clone resolved to ${head}, not the pinned ${sha}"
    log "llama.cpp pin resolves: ${head} ($(git -C "$dir" log -1 --format='%cs %s' | cut -c1-72))"
    rm -rf "$dir"
}

# Builds one application for one target, in the checkout at $1.
# $3 is the app name; its build root is <root>/lib/src/<app>.
build_target() {
    local root="$1" target="$2" app="$3"
    local app_dir="${root}/lib/src/${app}"
    local build_dir="build/${target}"
    cd "$app_dir"

    if [[ ! -f CMakeLists.txt ]]; then
        log "NOTICE: no CMakeLists.txt at ${app_dir} — skipping app '${app}'."
        return 0
    fi

    # The third CI stage: the tree was built by the second and unpacked here.
    if [[ $TEST_ONLY -eq 1 ]]; then
        [[ -d "${build_dir}" ]] || die "[${app}/${target}] --test-only: no build directory at ${app_dir}/${build_dir}"
        [[ "$target" == "$HOST" ]] || die "[${app}/${target}] --test-only: cross-built binaries can't run on ${HOST}"
        log "[${app}/${target}] running tests on the existing build"
        ctest --test-dir "${build_dir}" --output-on-failure
        log "[${app}/${target}] done (branch '$(git -C "$root" rev-parse --abbrev-ref HEAD)')"
        return 0
    fi

    [[ $CLEAN -eq 1 ]] && { log "cleaning ${app_dir}/${build_dir}/"; rm -rf "${build_dir}"; }

    # Extra configure arguments from the environment -- how a caller hands in
    # a toolchain file without this script growing a per-platform branch.
    #
    # The arrays below expand as ${a[@]+"${a[@]}"}, not "${a[@]}": under
    # `set -u`, bash 3.2 -- macOS's /bin/bash, and the bash on the macOS
    # runners -- treats an EMPTY array's expansion as an unbound variable and
    # exits. bash 4.4 lifted that, which is why the plain form works on Linux
    # and in MSYS2 and failed only on the merge-blocking runner.
    local -a env_args=()
    if [[ -n "${APOGEE_CMAKE_ARGS:-}" ]]; then
        read -r -a env_args <<< "${APOGEE_CMAKE_ARGS}"
    fi

    # Each app ships one CMake preset per target, named exactly after it.
    if [[ -f CMakePresets.json ]] && cmake --list-presets 2>/dev/null | grep -q "\"${target}\""; then
        log "[${app}/${target}] configuring (cmake --preset ${target}${env_args[*]:+ ${env_args[*]}})"
        cmake --preset "${target}" ${env_args[@]+"${env_args[@]}"}
    else
        local extra=()
        if [[ "${target%%-*}" == "macos" ]]; then
            extra+=(-DCMAKE_OSX_ARCHITECTURES=arm64)
        fi
        log "[${app}/${target}] configuring (cmake -S . -B ${build_dir})"
        cmake -S . -B "${build_dir}" ${extra[@]+"${extra[@]}"} ${env_args[@]+"${env_args[@]}"}
    fi

    log "[${app}/${target}] building with ${JOBS} jobs"
    cmake --build "${build_dir}" -j "${JOBS}"

    if [[ $RUN_TESTS -eq 1 ]]; then
        if [[ "$target" == "$HOST" ]]; then
            log "[${app}/${target}] running tests"
            ctest --test-dir "${build_dir}" --output-on-failure
        else
            log "[${app}/${target}] skipping tests: cross-built binaries can't run on ${HOST}"
        fi
    fi

    log "[${app}/${target}] done (branch '$(git -C "$root" rev-parse --abbrev-ref HEAD)')"
}

build_all_requested() {
    local root="$1" target app deferred=()
    for target in "${PLATFORMS[@]}"; do
        if buildable_here "$HOST" "$target"; then
            for app in "${APPS[@]}"; do
                build_target "$root" "$target" "$app"
            done
        else
            deferred+=("$target")
        fi
    done
    if [[ ${#deferred[@]} -gt 0 ]]; then
        if [[ $NO_DEFER -eq 1 ]]; then
            die "cannot build ${deferred[*]} natively on this host (${HOST}) and --no-defer was given"
        fi
        log "deferred to CI (not natively buildable on ${HOST}): ${deferred[*]}"
        log "the GitHub Actions matrix builds these on their native runners via this same script"
    fi
}

repo_root="$(git rev-parse --show-toplevel 2>/dev/null)" || die "not inside a git repository"
current_branch="$(git -C "$repo_root" rev-parse --abbrev-ref HEAD)"
[[ -n "$BRANCH" ]] || BRANCH="$current_branch"

if [[ $CLONE_LLAMA -eq 1 ]]; then
    clone_llama "$repo_root"
    exit 0
fi

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
