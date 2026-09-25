#!/usr/bin/env bash
# version-check.sh -- CI's `version bump` check: the version this checkout
# would release must not be released already.
#
#   version-check.sh
#
# A merge into `stable` publishes the version `project(... VERSION x.y.z)` in
# lib/src/cli/CMakeLists.txt names (user decision, 2026-09-22). A pull request
# that forgets to bump it merges cleanly and then releases NOTHING -- a silent
# no-op that looks exactly like a deliberate skip. This makes that loud at
# review time instead. It is a check on an OPEN pull request on purpose: after
# the merge the same condition is how a docs or hotfix merge declines to cut a
# release, and release-from-pr.sh handles it by publishing nothing.
#
# Released means the release exists: `gh release view`, when gh is installed
# and signed in (on a runner, GH_TOKEN). Without gh -- pr-ci.sh on a developer
# machine -- it asks the remote for the tag instead. The two agree, because
# the tag and its release are created in one `gh release create` and never
# apart (release-from-pr.sh, make release).
#
# Moved out of ci.yml (2026-09-25) so pr-ci.sh runs the same check the
# workflow does.
set -euo pipefail

fail() {
    if [ -n "${GITHUB_ACTIONS:-}" ]; then
        printf '::error file=lib/src/cli/CMakeLists.txt::%s\n' "$*"
    fi
    printf 'version-check.sh: %s\n' "$*" >&2
    exit 1
}

root="$(git rev-parse --show-toplevel 2>/dev/null)" || fail "not inside a git repository"
cmake_lists="$root/lib/src/cli/CMakeLists.txt"
version="v$(sed -n 's/^[[:space:]]*VERSION[[:space:]][[:space:]]*\([0-9][0-9.]*\).*/\1/p' "$cmake_lists" | head -1)"
[ "$version" != "v" ] || fail "could not read VERSION from $cmake_lists"

# A token in the environment is how a runner signs gh in; ask `auth status`
# only on a machine without one.
if command -v gh >/dev/null 2>&1 &&
    { [ -n "${GH_TOKEN:-}${GITHUB_TOKEN:-}" ] || gh auth status >/dev/null 2>&1; }; then
    how="the release, through gh"
    if gh release view "$version" >/dev/null 2>&1; then
        fail "$version is already released -- bump project(... VERSION x.y.z) or this merge will publish nothing"
    fi
else
    how="the tag on origin (gh is not installed or not signed in)"
    set +e
    git -C "$root" ls-remote --exit-code --tags origin "refs/tags/$version" >/dev/null 2>&1
    status=$?
    set -e
    case "$status" in
        0) fail "$version is already released (its tag is on origin) -- bump project(... VERSION x.y.z) or this merge will publish nothing" ;;
        2) ;; # no such tag
        *) fail "could not ask origin whether $version is released (git ls-remote exited $status)" ;;
    esac
fi

message="$version is unreleased (checked $how); merging this PR will publish it"
printf '%s\n' "$message"
if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
    printf '%s\n' "$message" >>"$GITHUB_STEP_SUMMARY"
fi
