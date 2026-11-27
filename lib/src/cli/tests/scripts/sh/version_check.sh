#!/usr/bin/env bash
#
# CI's `version bump` check, run (57m).
#
#   version_check.sh <work-dir> [<scripts-dir>]
#
# lib/scripts/version-check.sh is the one thing between a pull request that
# forgot to bump lib/release/VERSION and a merge that publishes nothing. The
# second bug sweep (2026-10-31) found two ways it passed such a pull request
# green: it knew nothing of the macOS app, so a change to lib/src/darwin alone
# with VERSION released said "merging publishes nothing" and exited 0; and it
# read ANY failure of `gh release view` -- a 502, a rate limit, a bad token --
# as "not released", so a CLI change with no bump passed on a bad day.
#
# This runs the real script -- with the real changed.sh and latest-release.sh
# beside it -- in a throwaway repository with a bare `origin` of its own,
# under `env -i`, with a PATH of a tool farm and a stand-in `gh` whose answers
# are the case's: which releases exist, whether it is signed in, whether
# GitHub answers at all. Nothing reaches the network or the real repository.
#
#   - the app changed and VERSION released fails, naming the app and the bump;
#     changed and unreleased passes, saying the release carries the app this
#     run's darwin.yml builds; neither changed and released publishes nothing;
#   - the CLI's rules as they were: changed and released fails, a CLI version
#     other than the release's fails, both changed names both;
#   - a `gh` that cannot answer (HTTP 502) fails "could not ask whether v0.2.0
#     is released" whatever --cli and --app say, never "merging publishes";
#     a 404 is "not released";
#   - without gh, or with gh not signed in, the tag on origin: 0 released, 2
#     not, anything else fails;
#   - with no --cli and no --app the script asks changed.sh for both, from the
#     latest release's commit.
#
# The optional <scripts-dir> runs another copy of lib/scripts -- how the check
# was held against the script from before 57m. POSIX only, like
# cli.install_parity.
set -euo pipefail

usage="usage: version_check.sh <work-dir> [<scripts-dir>]"
WORK="${1:?$usage}"
HERE="$(cd "$(dirname "$0")" && pwd)"
SCRIPTS="$(cd "${2:-$HERE/../../../../../scripts}" && pwd)"
CHECK="$SCRIPTS/version-check.sh"
[ -x "$CHECK" ] || { echo "no executable version-check.sh in $SCRIPTS" >&2; exit 2; }

. "$HERE/sandbox.sh"
WORK="$(sandbox_dir "$WORK")"
trap 'rm -rf "$WORK"' EXIT

checks=0
failures=0
pass() { checks=$((checks + 1)); }
fail() {
    checks=$((checks + 1))
    failures=$((failures + 1))
    echo "FAIL: $*" >&2
}

# --- The tool farm -------------------------------------------------------------
# What the three scripts run, and nothing else: no gh unless a case puts the
# stand-in in front of it, no curl (latest-release.sh's other path), no python.
FARM="$WORK/tools"
mkdir -p "$FARM"
for tool in bash env git sed tr head tail cat grep dirname awk wc mkdir rm; do
    found="$(command -v "$tool" 2>/dev/null || true)"
    case "$found" in
        /*) ln -s "$found" "$FARM/$tool" ;;
        *) echo "this host has no $tool" >&2; exit 2 ;;
    esac
done

# --- The stand-in gh ------------------------------------------------------------
#   GH_STUB_RELEASES  the published release tags, newest first, space-separated
#   GH_STUB_AUTH      `gh auth status`'s exit status (default 1: not signed in)
#   GH_STUB_FAIL      502: every API call fails as GitHub's gateway does
# Each call is logged to $GH_STUB_LOG; anything it was not written for fails.
GHDIR="$WORK/gh"
mkdir -p "$GHDIR"
cat >"$GHDIR/gh" <<'STUB'
#!/usr/bin/env bash
printf '%s\n' "$*" >>"$GH_STUB_LOG"
released() {
    for tag in ${GH_STUB_RELEASES:-}; do [ "$tag" = "$1" ] && return 0; done
    return 1
}
gateway() {
    if [ "${GH_STUB_FAIL:-}" = 502 ]; then
        echo "HTTP 502: Bad Gateway (https://api.github.com/$1)" >&2
        exit 1
    fi
}
case "$1 ${2:-}" in
    "auth status") exit "${GH_STUB_AUTH:-1}" ;;
    "api "*)
        case "$2" in
            repos/*/releases/tags/*)
                gateway "$2"
                released "${2##*/}" || { echo "gh: Not Found (HTTP 404)" >&2; exit 1; }
                case " $* " in *" --silent "*) ;; *) printf '{"tag_name":"%s"}\n' "${2##*/}" ;; esac
                exit 0
                ;;
            "repos/"*"/releases?per_page=30")
                gateway "$2"
                for tag in ${GH_STUB_RELEASES:-}; do printf '%s\n' "$tag"; done
                exit 0
                ;;
        esac
        ;;
    "release view")
        gateway "releases/tags/$3"
        released "$3" || { echo "release not found" >&2; exit 1; }
        exit 0
        ;;
esac
echo "stand-in gh: not written for: gh $*" >&2
exit 1
STUB
chmod +x "$GHDIR/gh"

# --- The throwaway repository -------------------------------------------------
# Released as v0.2.0 on its own bare origin: VERSION 0.2.0, the CLI 0.2.0, an
# app file and a document. Each case starts from that tag.
REPO="$WORK/repo"
ORIGIN="$WORK/origin.git"
mkdir -p "$WORK/home"
g() {
    env HOME="$WORK/home" GIT_CONFIG_NOSYSTEM=1 git -c user.name=version-check \
        -c user.email=version-check@localhost -c commit.gpgsign=false -c tag.gpgsign=false \
        -c init.defaultBranch=main "$@"
}
cli_version() {  # cli_version <x.y.z>
    printf 'project(Apogee\n    VERSION %s\n    LANGUAGES CXX)\n' "$1" >"$REPO/lib/src/cli/CMakeLists.txt"
}
g init -q --bare "$ORIGIN"
g init -q "$REPO"
mkdir -p "$REPO/lib/release" "$REPO/lib/src/cli" "$REPO/lib/src/darwin" "$REPO/docs"
echo 0.2.0 >"$REPO/lib/release/VERSION"
cli_version 0.2.0
echo 'int main() { return 0; }' >"$REPO/lib/src/cli/main.cpp"
echo 'import SwiftUI' >"$REPO/lib/src/darwin/App.swift"
echo '# Docs' >"$REPO/docs/README.md"
g -C "$REPO" add -A
g -C "$REPO" commit -q -m "release 0.2.0"
g -C "$REPO" tag v0.2.0
g -C "$REPO" remote add origin "$ORIGIN"
g -C "$REPO" push -q origin main v0.2.0

# scenario <change...>: a fresh commit on top of v0.2.0 making those changes.
scenario() {
    g -C "$REPO" checkout -q --detach v0.2.0
    g -C "$REPO" remote set-url origin "$ORIGIN"
    for change in "$@"; do
        case "$change" in
            app) echo '// a GUI fix' >>"$REPO/lib/src/darwin/App.swift" ;;
            cli) echo '// a CLI fix' >>"$REPO/lib/src/cli/main.cpp" ;;
            docs) echo 'More words.' >>"$REPO/docs/README.md" ;;
            release=*) echo "${change#release=}" >"$REPO/lib/release/VERSION" ;;
            cli-version=*) cli_version "${change#cli-version=}" ;;
            *) echo "unknown change $change" >&2; exit 2 ;;
        esac
    done
    g -C "$REPO" commit -q -a --allow-empty -m "the pull request: $*"
}

# run [VAR=value...] -- <version-check arguments...>: the real check, in the
# repository, under env -i. Its output in $out, its exit status in $rc, the
# stand-in's calls in $calls. NO_GH=1 leaves the stand-in off PATH: no gh at
# all.
run() {
    local path="$GHDIR:$FARM"
    local vars=()
    while [ $# -gt 0 ] && [ "$1" != -- ]; do
        if [ "$1" = NO_GH=1 ]; then
            path="$FARM"
        else
            vars+=("$1")
        fi
        shift
    done
    shift
    : >"$WORK/gh.log"
    set +e
    out="$(cd "$REPO" && env -i HOME="$WORK/home" GIT_CONFIG_NOSYSTEM=1 GH_REPO=example/apogee \
        GH_STUB_LOG="$WORK/gh.log" PATH="$path" ${vars[@]+"${vars[@]}"} "$CHECK" "$@" 2>&1)"
    rc=$?
    set -e
    calls="$(cat "$WORK/gh.log")"
}

# expect <name> <exit status> <text the output must hold> [<text it must not>]
expect() {
    local name="$1" status="$2" holds="$3" lacks="${4:-}"
    if [ "$rc" -ne "$status" ]; then
        fail "$name: exit $rc, wanted $status -- $out"
    elif [[ "$out" != *"$holds"* ]]; then
        fail "$name: no '$holds' in: $out"
    elif [ -n "$lacks" ] && [[ "$out" == *"$lacks"* ]]; then
        fail "$name: '$lacks' in: $out"
    else
        pass
    fi
}

SIGNED=(GH_TOKEN=x GH_STUB_RELEASES=v0.2.0)

# --- The macOS app (the sweep's probe, inverted) ---------------------------------
scenario app
run "${SIGNED[@]}" -- --cli false --app true --latest v0.2.0
expect "app changed, v0.2.0 released" 1 \
    "the macOS app changed since v0.2.0, but v0.2.0 is already released -- bump lib/release/VERSION (the app's version is stamped from it)" \
    "merging publishes"

scenario app release=0.2.1
run "${SIGNED[@]}" -- --cli false --app true --latest v0.2.0
expect "app changed, v0.2.1 unreleased" 0 \
    "merging publishes v0.2.1, with the CLI copied from v0.2.0 and the macOS app this run's darwin.yml builds (checked the release, through gh)"

scenario docs
run "${SIGNED[@]}" -- --cli false --app false --latest v0.2.0
expect "neither changed, v0.2.0 released" 0 \
    "v0.2.0 is already released and the CLI and the macOS app are unchanged since v0.2.0: merging publishes nothing"

scenario release=0.2.1
run "${SIGNED[@]}" -- --cli false --app false --latest v0.2.0
expect "neither changed, v0.2.1 unreleased" 0 \
    "merging publishes v0.2.1, with the CLI and the macOS app copied from v0.2.0 -- both are unchanged"

# --- The CLI's rules, as they were ---------------------------------------------
scenario cli
run "${SIGNED[@]}" -- --cli true --app false --latest v0.2.0
expect "CLI changed, v0.2.0 released" 1 \
    "the CLI changed since v0.2.0, but v0.2.0 is already released -- bump lib/release/VERSION (and the CLI's project(... VERSION) with it)" \
    "merging publishes"

scenario cli app
run "${SIGNED[@]}" -- --cli true --app true --latest v0.2.0
expect "both changed, v0.2.0 released" 1 \
    "the CLI and the macOS app changed since v0.2.0, but v0.2.0 is already released" "merging publishes"

scenario cli release=0.2.1
run "${SIGNED[@]}" -- --cli true --app false --latest v0.2.0
expect "CLI changed, its version behind the release" 1 \
    "set project(... VERSION 0.2.1) in lib/src/cli/CMakeLists.txt (it says 0.2.0)" "merging publishes"

# A 404 is "not released".
scenario cli release=0.2.1 cli-version=0.2.1
run "${SIGNED[@]}" -- --cli true --app false --latest v0.2.0
expect "CLI changed, v0.2.1 a 404" 0 \
    "merging publishes v0.2.1, with the CLI 0.2.1 this run builds and the macOS app copied from v0.2.0 (checked the release, through gh)"
if [[ "$calls" == *"api repos/{owner}/{repo}/releases/tags/v0.2.1 --silent"* ]] && [[ "$calls" != *"release view"* ]]; then
    pass
else
    fail "the lookup is gh api repos/{owner}/{repo}/releases/tags/<tag>, never gh release view -- gh was asked: $calls"
fi

scenario cli app release=0.2.1 cli-version=0.2.1
run "${SIGNED[@]}" -- --cli true --app true --latest v0.2.0
expect "both changed, v0.2.1 unreleased" 0 \
    "merging publishes v0.2.1, with the CLI 0.2.1 this run builds and the macOS app this run's darwin.yml builds"

# --- A gh that cannot answer -----------------------------------------------------
# The sweep's reproduction: HTTP 502, exit 1, a token set. Whatever --cli and
# --app say, the check fails, saying so in gh's words.
scenario cli
for flags in "--cli true --app false" "--cli false --app false" "--cli false --app true" "--cli true --app true"; do
    # shellcheck disable=SC2086 # the flags are words
    run "${SIGNED[@]}" GH_STUB_FAIL=502 -- $flags --latest v0.2.0
    expect "gh answers 502 ($flags)" 1 "could not ask whether v0.2.0 is released" "merging publishes"
    expect "gh's own words ($flags)" 1 "HTTP 502: Bad Gateway"
done

# --- Without gh: the tag on origin, 0/2/other ----------------------------------
scenario docs
run NO_GH=1 -- --cli false --app false --latest v0.2.0
expect "no gh, the tag on origin" 0 \
    "merging publishes nothing (checked the tag on origin (gh is not installed or not signed in))"

scenario release=0.2.1
run NO_GH=1 -- --cli false --app true --latest v0.2.0
expect "no gh, no tag on origin" 0 "merging publishes v0.2.1"

scenario app
g -C "$REPO" remote set-url origin "$WORK/no-such-origin.git"
run NO_GH=1 -- --cli false --app true --latest v0.2.0
expect "no gh, origin unreachable" 1 "could not ask origin whether v0.2.0 is released (git ls-remote exited" \
    "merging publishes"

# gh installed but not signed in, and no token: the same path.
scenario app
run GH_STUB_RELEASES=v0.2.0 GH_STUB_AUTH=1 -- --cli false --app true --latest v0.2.0
expect "gh not signed in, the tag on origin" 1 "the macOS app changed since v0.2.0, but v0.2.0 is already released"
if [[ "$calls" == "auth status" ]]; then
    pass
else
    fail "a gh not signed in is asked only whether it is -- gh was asked: $calls"
fi

# --- Worked out itself: changed.sh for both ------------------------------------
scenario app
run "${SIGNED[@]}" --
expect "self-computed, app changed" 1 "the macOS app changed since v0.2.0, but v0.2.0 is already released"

scenario cli
run "${SIGNED[@]}" --
expect "self-computed, CLI changed" 1 "the CLI changed since v0.2.0, but v0.2.0 is already released" "macOS app changed"

scenario docs
run "${SIGNED[@]}" --
expect "self-computed, neither changed" 0 \
    "v0.2.0 is already released and the CLI and the macOS app are unchanged since v0.2.0: merging publishes nothing"

scenario app
run "${SIGNED[@]}" -- --cli false --latest v0.2.0
expect "self-computed --app beside a given --cli" 1 "the macOS app changed since v0.2.0"

scenario docs
run "${SIGNED[@]}" GH_STUB_FAIL=502 --
expect "self-computed, the latest release unknown" 1 "could not look up the latest release" "merging publishes"

# --- Its arguments ---------------------------------------------------------------
scenario docs
run "${SIGNED[@]}" -- --cli false --app maybe --latest v0.2.0
expect "--app neither true nor false" 1 "--app must be true or false (got 'maybe')"

echo "version_check: $checks checks, $failures failed"
[ "$failures" -eq 0 ]
