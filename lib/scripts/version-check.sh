#!/usr/bin/env bash
# version-check.sh -- CI's `version bump` check: what merging this pull request
# would publish, and whether the versions allow it.
#
#   version-check.sh [--cli true|false] [--app true|false] [--latest <tag>]
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
# Two deliverables, since GUI M17 (2026-10-31): the CLI, and the macOS app
# (Apogee.app, built by darwin.yml), which carries no version of its own --
# its build stamps lib/release/VERSION into the bundle. So, with either changed
# since the latest release (changed.sh cli, changed.sh gui-darwin):
#
#   - lib/release/VERSION must be unreleased, or the merge publishes nothing and
#     the change ships in no release -- silently, which is why this check
#     exists (for the app too since 57m: an app-only change once passed here
#     green and shipped nowhere);
#   - with the CLI changed, the CLI's version must equal it: the binary this
#     run built is the release's, and reports it.
#
# A deliverable that is unchanged is copied from the latest release into this
# one (if its version is unreleased); if it is released too and neither
# changed, the merge publishes nothing -- a documentation merge, say -- and
# that is not an error.
#
# --cli, --app and --latest are CI's `what changed` answers; without them this
# works them out itself (latest-release.sh, then changed.sh cli and changed.sh
# gui-darwin from the latest release's commit to HEAD), which is what a bare
# local run relies on.
#
# Released means the release exists: `gh api repos/{owner}/{repo}/releases/tags/<tag>`,
# when gh is installed and signed in (on a runner, GH_TOKEN). A 200 is
# released, a 404 is not, and anything else -- a 5xx, a rate limit, a bad
# token -- fails the check with gh's own message: "not released" is an answer,
# "could not ask" is not (57m; a failed `gh release view` once read as not
# released, and passed a CLI change with no bump). Without gh it asks the
# remote for the tag instead, by the same rule. The two agree, because the tag
# and its release are created in one `gh release create` and never apart
# (release-from-pr.sh, on either path).
set -euo pipefail

fail() {  # fail <file> <message>
    if [ -n "${GITHUB_ACTIONS:-}" ]; then
        printf '::error file=%s::%s\n' "$1" "$2"
    fi
    printf 'version-check.sh: %s\n' "$2" >&2
    exit 1
}

usage="usage: version-check.sh [--cli true|false] [--app true|false] [--latest <tag>]"
cli=""
app=""
latest=""
while [ $# -gt 0 ]; do
    case "$1" in
        --cli) cli="${2:-}"; shift 2 || fail lib/release/VERSION "$usage" ;;
        --app) app="${2:-}"; shift 2 || fail lib/release/VERSION "$usage" ;;
        --latest) latest="${2:-}"; shift 2 || fail lib/release/VERSION "$usage" ;;
        *) fail lib/release/VERSION "$usage" ;;
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

# What CI's `what changed` would have answered, for whichever was not given.
if [ -z "$cli" ] || [ -z "$app" ]; then
    # A failed lookup fails the check: "no release" is an answer, "could not
    # ask" is not.
    found="$("$scripts/latest-release.sh")" || fail lib/release/VERSION "could not look up the latest release"
    found_tag="" commit=""
    [ -z "$found" ] || read -r found_tag commit <<<"$found"
    [ -n "$latest" ] || latest="$found_tag"
    if [ -z "$cli" ]; then
        cli="$("$scripts/changed.sh" cli "$commit" HEAD | tail -1)"
        cli="${cli#cli=}"
    fi
    if [ -z "$app" ]; then
        app="$("$scripts/changed.sh" gui-darwin "$commit" HEAD | tail -1)"
        app="${app#gui-darwin=}"
    fi
fi
case "$cli" in
    true | false) ;;
    *) fail lib/release/VERSION "--cli must be true or false (got '$cli')" ;;
esac
case "$app" in
    true | false) ;;
    *) fail lib/release/VERSION "--app must be true or false (got '$app')" ;;
esac

# Is v<VERSION> released? A token in the environment is how a runner signs gh
# in; ask `auth status` only on a machine without one.
if command -v gh >/dev/null 2>&1 &&
    { [ -n "${GH_TOKEN:-}${GITHUB_TOKEN:-}" ] || gh auth status >/dev/null 2>&1; }; then
    how="the release, through gh"
    # As release-from-pr.sh's `api`: a 404 is the answer "no such release";
    # any other failure is gh's, said in its own words.
    set +e
    asked="$(gh api "repos/{owner}/{repo}/releases/tags/$tag" --silent 2>&1)"
    status=$?
    set -e
    if [ "$status" -eq 0 ]; then
        released=true
    elif grep -q 'HTTP 404' <<<"$asked"; then
        released=false
    else
        why="$(printf '%s\n' "$asked" | sed -n '/./{p;q;}')"
        fail lib/release/VERSION "could not ask whether $tag is released (gh api exited $status: ${why:-no message}) -- run the check again once GitHub answers"
    fi
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
if [ "$cli" = true ] && [ "$app" = true ]; then
    changes="the CLI and the macOS app"
elif [ "$cli" = true ]; then
    changes="the CLI"
elif [ "$app" = true ]; then
    changes="the macOS app"
else
    changes=""
fi

if [ -n "$changes" ] && [ "$released" = true ]; then
    if [ "$cli" = true ]; then
        bump="bump lib/release/VERSION (and the CLI's project(... VERSION) with it)"
    else
        bump="bump lib/release/VERSION (the app's version is stamped from it)"
    fi
    fail lib/release/VERSION "$changes changed since $since, but $tag is already released -- $bump, or this merge publishes nothing and the change ships in no release"
fi
if [ "$cli" = true ]; then
    [ "$cli_version" = "$version" ] ||
        fail lib/src/cli/CMakeLists.txt "the CLI changed since $since, so it ships in $tag and must report it: set project(... VERSION $version) in lib/src/cli/CMakeLists.txt (it says $cli_version)"
fi

if [ "$cli" = true ]; then
    with_cli="the CLI $cli_version this run builds"
else
    with_cli="the CLI copied from $since"
fi
if [ "$app" = true ]; then
    with_app="the macOS app this run's darwin.yml builds"
else
    with_app="the macOS app copied from $since"
fi
if [ "$released" = false ]; then
    if [ -z "$changes" ]; then
        message="merging publishes $tag, with the CLI and the macOS app copied from $since -- both are unchanged (checked $how)"
    else
        message="merging publishes $tag, with $with_cli and $with_app (checked $how)"
    fi
else
    message="$tag is already released and the CLI and the macOS app are unchanged since $since: merging publishes nothing (checked $how)"
fi

printf '%s\n' "$message"
if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
    printf '%s\n' "$message" >>"$GITHUB_STEP_SUMMARY"
fi
