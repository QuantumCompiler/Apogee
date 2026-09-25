#!/usr/bin/env bash
# pr-ci.sh -- rehearse, on this machine, the CI run a pull request into
# `stable` starts: the same jobs, in the same order, on the same source.
#
#   pr-ci.sh [--base <ref>] [--jobs N] [--uncommitted]
#                                         rehearse; exits non-zero if CI would fail
#   pr-ci.sh --clean                      remove its worktrees, builds and logs
#
# CI does not build the branch. It builds GitHub's TEST MERGE of the branch
# into `stable` (refs/pull/N/merge), and so does this: it fetches `stable`,
# merges it into this branch's HEAD, and runs ci.yml's pull-request jobs on
# the result, each through the script the workflow itself calls:
#
#   what changed        lib/scripts/code-changed.sh -- documentation only
#                       means CI builds nothing, and neither does this
#   version bump        lib/scripts/version-check.sh
#   clone llama.cpp     lib/scripts/cicd.sh --clone-llama
#   unit tests <host>   lib/scripts/cicd.sh --unit-tests, llama.cpp off
#   build <host>        lib/scripts/cicd.sh --platform <host> --no-defer,
#                       then lib/scripts/package.sh -- the archive a release
#                       ships, and the proof that its binary runs
#
# As in the workflow, `build` needs `clone llama.cpp` and `unit tests` to pass,
# and `version bump` gates nothing but the verdict. Only this host's target can
# be built here; the other four are CI's runners', and the summary says so.
#
# One check CI does not have, because this host needs it: `gcc compile`
# (lib/scripts/gcc-check.py) compiles every first-party file with GCC's
# standard library -- a stand-in for the Linux and Windows x64 compiles, which
# a Mac's libc++ cannot vouch for (a missing #include lost four platforms on
# 2026-09-25). It needs a GCC (`brew install mingw-w64`) and is reported as
# skipped, never failed, without one.
#
# What it does NOT rehearse, on purpose: uncommitted changes -- a pull request
# carries commits, and a note says when some are left out; --uncommitted
# rehearses them too, as a commit object no branch points at -- and `tag and
# release`, which runs only after a merge (release-from-pr.sh <pr> rehearses
# that against a real pull request).
#
# The jobs run in two worktrees of this repository -- CI gives each job a
# checkout of its own, and the unit tests build with llama.cpp OFF while the
# build has it ON, so sharing one build directory would rebuild half the tree
# on every switch. They live under $APOGEE_PR_CI_DIR (default
# ~/.cache/apogee/pr-ci) and are reset to the new test merge on each run, so
# builds are incremental after the first. Nothing in this checkout is touched.
set -euo pipefail

log() { printf '[pr-ci] %s\n' "$*"; }
die() {
    printf '[pr-ci] error: %s\n' "$*" >&2
    exit 1
}

usage() {
    sed -n '2,7p' "$0" | sed 's/^# \{0,1\}//'
}

BASE="origin/stable"
JOBS=""
CLEAN=0
UNCOMMITTED=0
while [ $# -gt 0 ]; do
    case "$1" in
        --base)
            [ $# -ge 2 ] || die "--base needs a ref"
            BASE="$2"
            shift
            ;;
        -j | --jobs)
            [ $# -ge 2 ] || die "--jobs needs a number"
            JOBS="$2"
            shift
            ;;
        --clean) CLEAN=1 ;;
        --uncommitted) UNCOMMITTED=1 ;;
        -h | --help)
            usage
            exit 0
            ;;
        *) die "unknown option: $1 (see --help)" ;;
    esac
    shift
done

root="$(git rev-parse --show-toplevel 2>/dev/null)" || die "not inside a git repository"
dir="${APOGEE_PR_CI_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/apogee/pr-ci}"
unit_wt="$dir/unit-tests"
build_wt="$dir/build"

remove_worktree() {
    if [ -e "$1/.git" ]; then
        git -C "$root" worktree remove --force "$1" 2>/dev/null || true
    fi
    rm -rf "$1"
}

if [ $CLEAN -eq 1 ]; then
    remove_worktree "$unit_wt"
    remove_worktree "$build_wt"
    rm -rf "$dir"
    git -C "$root" worktree prune
    log "removed $dir"
    exit 0
fi

host="$("$root/lib/scripts/cicd.sh" --host)"
branch="$(git -C "$root" rev-parse --abbrev-ref HEAD)"
head="$(git -C "$root" rev-parse HEAD)"

if [ -n "$(git -C "$root" status --porcelain)" ]; then
    if [ $UNCOMMITTED -eq 1 ]; then
        # The working tree as a commit on top of HEAD: a scratch index, every
        # change and every new file .gitignore allows, and a commit object that
        # no branch, stash or reflog points at. This checkout is not touched.
        scratch="$(mktemp -d "${TMPDIR:-/tmp}/apogee-pr-ci.XXXXXX")"
        GIT_INDEX_FILE="$scratch/index" git -C "$root" read-tree HEAD
        GIT_INDEX_FILE="$scratch/index" git -C "$root" add -A
        snapshot="$(GIT_INDEX_FILE="$scratch/index" git -C "$root" write-tree)"
        rm -rf "$scratch"
        head="$(git -C "$root" -c user.name="pr-ci" -c user.email="pr-ci@localhost" \
            commit-tree "$snapshot" -p "$head" -m "pr-ci: uncommitted changes")"
        log "including this checkout's uncommitted changes (as $(git -C "$root" rev-parse --short "$head"))"
    else
        log "note: this checkout has uncommitted changes. A pull request carries"
        log "      commits, so they are not part of this rehearsal (--uncommitted adds them)."
    fi
fi

# The base, as fresh as the remote has it: CI merges into what `stable` is
# when the run starts, not what it was when this clone last fetched.
case "$BASE" in
    */*)
        remote="${BASE%%/*}"
        if git -C "$root" remote | grep -qx "$remote"; then
            if ! git -C "$root" fetch --quiet "$remote" "${BASE#*/}"; then
                log "note: could not fetch $BASE; merging into the copy this clone already has"
            fi
        fi
        ;;
esac
base_sha="$(git -C "$root" rev-parse --verify --quiet "$BASE^{commit}")" || die "no such base: $BASE"

# A worktree at `commit`, whatever state an earlier run left it in. Only the
# tracked files are reset: the build directories are ignored, and kept.
prepare_worktree() {
    local wt="$1" commit="$2"
    if [ -e "$wt/.git" ]; then
        git -C "$wt" merge --abort >/dev/null 2>&1 || true
        git -C "$wt" reset --quiet --hard
        git -C "$wt" checkout --quiet --detach --force "$commit"
    else
        rm -rf "$wt"
        mkdir -p "$(dirname "$wt")"
        git -C "$root" worktree prune
        git -C "$root" worktree add --quiet --detach --force "$wt" "$commit"
    fi
}

# --- the test merge -----------------------------------------------------------
prepare_worktree "$unit_wt" "$head"
if git -C "$root" merge-base --is-ancestor "$base_sha" "$head"; then
    merged="$head"
    log "$branch already contains $BASE: the test merge is $branch itself"
else
    if ! git -C "$unit_wt" -c user.name="pr-ci" -c user.email="pr-ci@localhost" \
        merge --quiet --no-ff --no-edit -m "Test merge of $branch into $BASE" "$base_sha" >/dev/null 2>&1; then
        conflicts="$(git -C "$unit_wt" diff --name-only --diff-filter=U | tr '\n' ' ')"
        git -C "$unit_wt" merge --abort >/dev/null 2>&1 || true
        die "$branch does not merge cleanly into $BASE (${conflicts% }) -- GitHub runs no CI on a conflicting pull request; merge $BASE into $branch first"
    fi
    merged="$(git -C "$unit_wt" rev-parse HEAD)"
    log "test merge of $branch into $BASE: $(git -C "$unit_wt" rev-parse --short HEAD)"
fi
prepare_worktree "$build_wt" "$merged"
tree="$(git -C "$root" rev-parse "$merged^{tree}")"

# --- what changed -------------------------------------------------------------
# CI's first job: a pull request that changes only documentation builds
# nothing, and every job reports success. Asked of the test merge's own
# script, as the workflow asks the one it checked out; a branch from before
# the rule has none, and runs everything.
code=true
if [ -x "$unit_wt/lib/scripts/code-changed.sh" ]; then
    answer="$(git -C "$root" diff --name-only "$base_sha...$head" | "$unit_wt/lib/scripts/code-changed.sh")"
    printf '%s\n' "$answer" | sed 's/^/[pr-ci] /'
    case "$(printf '%s\n' "$answer" | tail -1)" in
        code=false) code=false ;;
    esac
fi

# --- the jobs -----------------------------------------------------------------
logs="$dir/logs"
artifacts="$dir/artifacts"
rm -rf "$logs" "$artifacts"
mkdir -p "$logs"
jobs_args=()
[ -n "$JOBS" ] && jobs_args=(--jobs "$JOBS")

names=()
results=()
record() {
    names+=("$1")
    results+=("$2")
}

# run_job <name> <worktree> <command...>: the command in that worktree, its
# output to the terminal and to logs/<name>.log. Returns its status.
run_job() {
    local name="$1" wt="$2" file status
    shift 2
    file="$logs/$(printf '%s' "$name" | tr ' ' '-').log"
    printf '\n[pr-ci] ===== %s =====\n' "$name"
    set +e
    (cd "$wt" && "$@") 2>&1 | tee "$file"
    status=${PIPESTATUS[0]}
    set -e
    if [ "$status" -eq 0 ]; then
        record "$name" passed
    else
        record "$name" "FAILED (exit $status, $file)"
    fi
    return "$status"
}

if [ "$code" = false ]; then
    record "what changed" "documentation only"
    # As CI reports them: `version bump` skipped whole (a skip passes), the
    # others run under their own names with every step skipped.
    record "version bump" "skipped (documentation only)"
    for job in "clone llama.cpp" "unit tests $host" "build $host"; do
        record "$job" "passed (documentation only: no steps run)"
    done
fi

[ "$code" = false ] || run_job "version bump" "$unit_wt" lib/scripts/version-check.sh || true

if [ "$code" = true ]; then
    clone_ok=0
    run_job "clone llama.cpp" "$unit_wt" lib/scripts/cicd.sh --clone-llama && clone_ok=1

    unit_ok=0
    run_job "unit tests $host" "$unit_wt" \
        env APOGEE_CMAKE_ARGS=-DAPOGEE_ENABLE_LLAMA=OFF \
        lib/scripts/cicd.sh --platform "$host" --unit-tests --no-defer ${jobs_args[@]+"${jobs_args[@]}"} &&
        unit_ok=1

    if [ $clone_ok -eq 1 ] && [ $unit_ok -eq 1 ]; then
        # The workflow's build job sets no configure arguments of its own; one
        # left in this shell's environment must not leak into the rehearsal.
        run_job "build $host" "$build_wt" \
            env -u APOGEE_CMAKE_ARGS bash -c '
                set -euo pipefail
                lib/scripts/cicd.sh --platform "$1" --no-defer "${@:3}"
                lib/scripts/package.sh "$1" "$2"
            ' pr-ci "$host" "$artifacts" ${jobs_args[@]+"${jobs_args[@]}"} || true
    else
        record "build $host" "skipped (needs clone llama.cpp and unit tests $host)"
    fi

    # Not a CI job: this host's stand-in for the Linux and Windows x64 compiles.
    # The build's compile commands when it configured (llama.cpp on, as CI's
    # builds), else the unit tests' (off).
    database="$build_wt/lib/src/cli/build/$host"
    [ -f "$database/compile_commands.json" ] || database="$unit_wt/lib/src/cli/build/$host"
    if [ ! -x "$unit_wt/lib/scripts/gcc-check.py" ]; then
        record "gcc compile (local)" "skipped (the branch has no lib/scripts/gcc-check.py)"
    elif [ ! -f "$database/compile_commands.json" ]; then
        record "gcc compile (local)" "skipped (nothing configured to take compile commands from)"
    else
        set +e
        run_job "gcc compile (local)" "$unit_wt" \
            python3 lib/scripts/gcc-check.py "$database" ${jobs_args[@]+"${jobs_args[@]}"}
        status=$?
        set -e
        if [ "$status" -eq 3 ]; then
            # No GCC on this host: say how to get one, and do not fail for it.
            results[${#results[@]} - 1]="skipped (no GCC here: brew install mingw-w64)"
        fi
    fi
fi

# --- the verdict --------------------------------------------------------------
failed=0
printf '\n[pr-ci] rehearsed the pull request %s -> %s\n' "$branch" "$BASE"
printf '[pr-ci]   source: %s, tree %s\n' "$(git -C "$root" rev-parse --short "$merged")" "$(git -C "$root" rev-parse --short "$tree")"
i=0
while [ $i -lt ${#names[@]} ]; do
    printf '[pr-ci]   %-22s %s\n' "${names[$i]}" "${results[$i]}"
    # Only a job that ran and failed fails the verdict: a skip says why it
    # was skipped, and the only one that follows a failure is the build's.
    case "${results[$i]}" in FAILED*) failed=1 ;; esac
    i=$((i + 1))
done
others=""
for target in linux-x64 linux-arm64 macos-arm64 windows-x64 windows-arm64; do
    [ "$target" = "$host" ] || others="$others${others:+, }$target"
done
printf '[pr-ci]   not rehearsed: unit tests and build for %s -- each needs its own CI runner\n' "$others"
[ "$code" = false ] || printf '[pr-ci]   (their compiles are what `gcc compile` stands in for; their tests are not)\n'
if ls "$artifacts"/apogee-"$host".* >/dev/null 2>&1; then
    printf '[pr-ci]   archive: %s\n' "$(ls "$artifacts"/apogee-"$host".tar.gz "$artifacts"/apogee-"$host".zip 2>/dev/null | head -1)"
fi
printf '[pr-ci]   logs: %s\n' "$logs"

if [ $failed -ne 0 ]; then
    printf '[pr-ci] CI would fail on this pull request.\n'
    exit 1
fi
printf '[pr-ci] every job this host can run passed.\n'
