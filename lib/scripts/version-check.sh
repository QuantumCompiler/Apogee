#!/usr/bin/env bash
# version-check.sh -- CI's `version bump` check: what merging this pull request
# would publish, and whether the versions allow it.
#
#   version-check.sh [--cli true|false] [--latest <tag>]
#
# Two versions, since 2026-09-25 (user decision):
#
#   lib/release/VERSION              the RELEASE. A merge into `stable`
#                                    publishes v<VERSION> unless that is
#                                    already released.
#   project(... VERSION x.y.z) in    the CLI's own version, what `apogee version`
#   lib/src/cli/CMakeLists.txt       reports. It changes only when the CLI does,
#                                    and then to the release it ships in.
#
# So, with the CLI changed since the latest release (changed.sh cli):
#
#   - lib/release/VERSION must be unreleased, or the merge publishes nothing and the CLI
#     change ships in no release -- silently, which is why this check exists;
#   - the CLI's version must equal it: the binary this run built is the
#     release's, and reports it.
#
# With the CLI unchanged, the release (if its version is unreleased) carries the
# latest release's CLI archives, copied; if it is released too, the merge
# publishes nothing -- a documentation merge, say -- and that is not an error.
#
# --cli and --latest are CI's `what changed` answers; without them this works
# them out itself (latest-release.sh, changed.sh cli ... HEAD), which is what
# pr-ci.sh relies on.
#
# Released means the release exists: `gh release view`, when gh is installed
# and signed in (on a runner, GH_TOKEN). Without gh it asks the remote for the
# tag instead. The two agree, because the tag and its release are created in
# one `gh release create` and never apart (release-from-pr.sh, make release).
set -euo pipefail

fail() {  # fail <file> <message>
    if [ -n "${GITHUB_ACTIONS:-}" ]; then
        printf '::error file=%s::%s\n' "$1" "$2"
    fi
    printf 'version-check.sh: %s\n' "$2" >&2
    exit 1
}

cli=""
latest=""
while [ $# -gt 0 ]; do
    case "$1" in
        --cli) cli="${2:-}"; shift 2 ;;
        --latest) latest="${2:-}"; shift 2 ;;
        *) fail lib/release/VERSION "usage: version-check.sh [--cli true|false] [--latest <tag>]" ;;
    esac
done

root="$(git rev-parse --show-toplevel 2>/dev/null)" || fail lib/release/VERSION "not inside a git repository"
scripts="$(cd "$(dirname "$0")" && pwd)"

version="$(tr -d ' \r\n' <"$root/lib/release/VERSION" 2>/dev/null)" || true
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] ||
    fail lib/release/VERSION "lib/release/VERSION must hold the release as x.y.z (got '$version')"
tag="v$version"
cli_version="$(sed -n 's/^[[:space:]]*VERSION[[:space:]][[:space:]]*\([0-9][0-9.]*\).*/\1/p' \
    "$root/lib/src/cli/CMakeLists.txt" | head -1)"

# What CI's `what changed` would have answered.
if [ -z "$cli" ]; then
    # A failed lookup fails the check: "no release" is an answer, "could not
    # ask" is not.
    found="$("$scripts/latest-release.sh")" || fail lib/release/VERSION "could not look up the latest release"
    latest="" commit=""
    [ -z "$found" ] || read -r latest commit <<<"$found"
    cli="$("$scripts/changed.sh" cli "$commit" HEAD | tail -1)"
    cli="${cli#cli=}"
fi
case "$cli" in
    true | false) ;;
    *) fail lib/release/VERSION "--cli must be true or false (got '$cli')" ;;
esac

# Is v<VERSION> released? A token in the environment is how a runner signs gh
# in; ask `auth status` only on a machine without one.
if command -v gh >/dev/null 2>&1 &&
    { [ -n "${GH_TOKEN:-}${GITHUB_TOKEN:-}" ] || gh auth status >/dev/null 2>&1; }; then
    how="the release, through gh"
    if gh release view "$tag" >/dev/null 2>&1; then released=true; else released=false; fi
else
    how="the tag on origin (gh is not installed or not signed in)"
    set +e
    git -C "$root" ls-remote --exit-code --tags origin "refs/tags/$tag" >/dev/null 2>&1
    status=$?
    set -e
    case "$status" in
        0) released=true ;;
        2) released=false ;;
        *) fail lib/release/VERSION "could not ask origin whether $tag is released (git ls-remote exited $status)" ;;
    esac
fi

since="${latest:-no release yet}"
if [ "$cli" = true ]; then
    [ "$released" = false ] ||
        fail lib/release/VERSION "the CLI changed since $since, but $tag is already released -- bump lib/release/VERSION (and the CLI's project(... VERSION) with it), or this merge publishes nothing and the change ships in no release"
    [ "$cli_version" = "$version" ] ||
        fail lib/src/cli/CMakeLists.txt "the CLI changed since $since, so it ships in $tag and must report it: set project(... VERSION $version) in lib/src/cli/CMakeLists.txt (it says $cli_version)"
    message="merging publishes $tag, with the CLI $cli_version this run builds (checked $how)"
elif [ "$released" = false ]; then
    message="merging publishes $tag, with the CLI copied from $since -- the CLI is unchanged (checked $how)"
else
    message="$tag is already released and the CLI is unchanged since $since: merging publishes nothing (checked $how)"
fi

printf '%s\n' "$message"
if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
    printf '%s\n' "$message" >>"$GITHUB_STEP_SUMMARY"
fi
