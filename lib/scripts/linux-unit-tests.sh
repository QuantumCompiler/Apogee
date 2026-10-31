#!/usr/bin/env bash
# linux-unit-tests.sh -- run CI's Linux unit-test job on this machine, in a
# container: the same command (cicd.sh --unit-tests, llama.cpp off) on the
# runner's Linux (lib/scripts/linux-ci.Dockerfile, Ubuntu 24.04).
#
#   linux-unit-tests.sh [--jobs N]  from anywhere inside a checkout
#
# A Mac hides what differs on Linux: GCC and libstdc++ where it has clang and
# libc++, glibc's allocator, and ext4's directory order where APFS's happens to
# be sorted. Each has failed a pull request's Linux jobs after the suite passed
# here (2026-09-19, 2026-10-12). The suite's temporary files are written inside
# the container, on Linux's own file system; the checkout is mounted, so the
# build (build/linux-<arch>) is kept between runs and later ones are
# incremental. It runs as this user, not root, as the runner does.
#
# Docker must be running, with room for a GCC build: at least 4 CPUs and 8 GiB.
# With colima, a VM of its own leaves any other alone:
#
#   colima start --profile apogee-ci --cpu 8 --memory 16 --disk 60
#
# Its context, colima-apogee-ci, is used when it exists, whatever Docker's
# current one is (colima start makes the new one current; `docker context use
# colima` puts the old one back); APOGEE_DOCKER_CONTEXT names another. The container is the Docker host's architecture --
# linux-arm64 on Apple silicon, which CI's ubuntu-24.04-arm runs.
#
# Exit status: the suite's, or 3 when Docker is missing, stopped or too small
# -- pr-ci.sh reports that as skipped, saying what to start.
set -euo pipefail

MIN_CPUS=4
MIN_GIB=8

log() { printf '[linux-unit-tests] %s\n' "$*"; }
die() {
    printf '[linux-unit-tests] error: %s\n' "$*" >&2
    exit 1
}
skip() {
    printf '[linux-unit-tests] cannot run here: %s\n' "$*" >&2
    printf '[linux-unit-tests]   colima start --profile apogee-ci --cpu 8 --memory 16 --disk 60\n' >&2
    exit 3
}

JOBS=""
while [ $# -gt 0 ]; do
    case "$1" in
        -j | --jobs)
            [ $# -ge 2 ] || die "--jobs needs a number"
            JOBS="$2"
            shift
            ;;
        -h | --help)
            sed -n '2,6p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *) die "unknown option: $1 (see --help)" ;;
    esac
    shift
done

command -v docker >/dev/null 2>&1 || skip "no docker command (brew install docker colima)"
context="${APOGEE_DOCKER_CONTEXT:-}"
if [ -z "$context" ] && docker context inspect colima-apogee-ci >/dev/null 2>&1; then
    context="colima-apogee-ci"
fi
dock() {
    if [ -n "$context" ]; then docker --context "$context" "$@"; else docker "$@"; fi
}

info="$(dock info --format '{{.NCPU}} {{.MemTotal}} {{.Architecture}}' 2>/dev/null)" ||
    skip "Docker is not running${context:+ (context $context)}"
read -r cpus mem arch <<<"$info"
# MemTotal is what the VM's kernel reports, a little under what it was given:
# rounded to the nearest GiB, it is what the VM was started with.
gib=$(((mem + 536870912) / 1073741824))
if [ "$cpus" -lt "$MIN_CPUS" ] || [ "$gib" -lt "$MIN_GIB" ]; then
    skip "Docker${context:+ (context $context)} has $cpus CPUs and $gib GiB; a GCC build of the suite needs $MIN_CPUS and $MIN_GIB"
fi
case "$arch" in
    aarch64 | arm64) target=linux-arm64 ;;
    x86_64 | amd64) target=linux-x64 ;;
    *) skip "Docker runs $arch, which no CI runner is" ;;
esac
# Two GiB a compile, of what the VM really has: GCC on the largest test files
# needs about that.
if [ -z "$JOBS" ]; then
    JOBS=$((mem / 1073741824 / 2))
    [ "$JOBS" -le "$cpus" ] || JOBS="$cpus"
    [ "$JOBS" -ge 1 ] || JOBS=1
fi

top="$(git rev-parse --show-toplevel 2>/dev/null)" || die "not inside a git repository"
common="$(cd "$top" && cd "$(git rev-parse --git-common-dir)" && pwd -P)"

# The image, named for its definition: a changed Dockerfile builds a new one.
file="$top/lib/scripts/linux-ci.Dockerfile"
[ -f "$file" ] || die "no $file"
digest="$( (shasum -a 256 "$file" 2>/dev/null || sha256sum "$file") | cut -c1-12)"
image="apogee-linux-ci:$digest"
if ! dock image inspect "$image" >/dev/null 2>&1; then
    log "building $image (Ubuntu 24.04, the runner's packages)"
    dock build --quiet -t "$image" -f "$file" "$top/lib/scripts" >/dev/null
fi

# The checkout at its own path, so a worktree's .git file (an absolute path
# into the repository's own) still resolves; that one read-only. A VM's file
# sharing reports the mount's owner as nobody git knows, so git is told to
# trust it (safe.directory, given as command-line config so git honours it).
mounts=(-v "$top:$top")
case "$common/" in
    "$top"/*) ;;
    *) mounts+=(-v "$common:$common:ro") ;;
esac

# A build directory an earlier container configured at another path (one that
# mounted the checkout at /work) cannot be configured again in place -- nor
# can any dependency's sub-build inside it -- so it is built afresh.
build="$top/lib/src/cli/build/$target"
if [ -d "$build" ] && [ -n "$(find "$build" -name CMakeCache.txt \
    -exec grep -LF "CMAKE_CACHEFILE_DIR:INTERNAL=$build" {} + 2>/dev/null)" ]; then
    log "build/$target was configured at another path; building it afresh"
    rm -rf "$build"
fi

log "unit tests $target in $image ($JOBS jobs${context:+, context $context})"
dock run --rm --init \
    --user "$(id -u):$(id -g)" \
    -e HOME=/tmp/home -e USER=ci -e CI=true -e LANG=C.UTF-8 \
    -e GIT_CONFIG_COUNT=1 -e GIT_CONFIG_KEY_0=safe.directory -e GIT_CONFIG_VALUE_0='*' \
    -e APOGEE_CMAKE_ARGS=-DAPOGEE_ENABLE_LLAMA=OFF \
    "${mounts[@]}" -w "$top" \
    "$image" \
    bash -c 'mkdir -p "$HOME" && exec lib/scripts/cicd.sh --app cli --platform "$1" --unit-tests --no-defer --jobs "$2"' \
    linux-unit-tests "$target" "$JOBS"
