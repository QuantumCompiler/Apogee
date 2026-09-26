#!/usr/bin/env bash
# release-from-pr.sh -- tag and publish the release a merged pull request makes.
#
#   release-from-pr.sh <pr-number>             rehearse: every check, no changes
#   release-from-pr.sh <pr-number> --publish   tag the merge commit and publish
#
# CI's `tag and release` job runs this with --publish when a pull request into
# `stable` is merged (user decision, 2026-09-25). Nothing is rebuilt: each
# deliverable is either the pull request's OWN CI build, or -- when its source
# is unchanged since the latest release -- copied from that release (user
# decision, 2026-09-25). Today the one deliverable is the CLI. Without
# --publish it is a rehearsal: the same lookups and checks, then what it would
# have published. CI runs one on request (Actions -> CI -> Run workflow, with
# the PR number), and so can anyone with `gh` signed in, from a checkout.
#
# In order, each a hard stop:
#
#   1. The pull request: into `stable`, and merged when publishing. Its merge
#      commit -- for an open one, GitHub's test merge -- is what is released.
#   2. The release: `lib/release/VERSION` at that commit names it. A tag v<VERSION> at
#      ANOTHER commit means that version is out, and this merge publishes
#      nothing -- how a docs or hotfix merge declines to cut a release (exit
#      0). A tag at THIS commit was made by an earlier attempt, and publishing
#      finishes what that attempt started.
#   3. The CLI: changed since the latest release (lib/scripts/changed.sh cli),
#      or not?
#   4a. Changed -- the pull request's build. The newest successful CI run of
#      its head holding an archive for every platform that run built; every
#      archive's .source naming the merge commit's exact source TREE (CI
#      builds the test merge; if `stable` moved after that run, the archives
#      are not the merged source, and nothing is published); and the binary
#      for this host, run: `apogee version` must report that version -- a CLI
#      that changed ships in this release and says so.
#   4b. Unchanged -- the latest release's CLI archives, downloaded and
#      re-published as they are. The binary for this host is run, to prove
#      the copy starts; it reports the CLI version it was built as.
#   5. The tag at the merge commit and the release in ONE `gh release
#      create`, so no tag is ever left behind without its release.
#
# Needs gh (GH_TOKEN, and GH_REPO or a checkout of the repository), a checkout
# with the history back to the latest release (CI's is complete), tar and
# unzip. Writes nothing outside its own temp directory, and nothing at all to
# GitHub without --publish.
set -euo pipefail

RELEASE_BRANCH="${RELEASE_BRANCH:-stable}"
CI_WORKFLOW="${CI_WORKFLOW:-ci.yml}"
scripts="$(cd "$(dirname "$0")" && pwd)"

usage() {
    sed -n '2,6p' "$0" | sed 's/^# \{0,1\}//'
}

die() {
    if [ -n "${GITHUB_ACTIONS:-}" ]; then
        printf '::error title=release-from-pr::%s\n' "$*"
    else
        printf 'release-from-pr: %s\n' "$*" >&2
    fi
    exit 1
}

# To the log, and to the run's summary page when there is one.
note() {
    printf '%s\n' "$*"
    if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
        printf '%s\n' "$*" >>"$GITHUB_STEP_SUMMARY"
    fi
}

pr=""
publish=0
for arg in "$@"; do
    case "$arg" in
        --publish) publish=1 ;;
        -h | --help) usage; exit 0 ;;
        *)
            [ -z "$pr" ] || die "one pull request number, please (got '$pr' and '$arg')"
            pr="$arg"
            ;;
    esac
done
[[ "$pr" =~ ^[0-9]+$ ]] || { usage >&2; exit 2; }
git rev-parse --git-dir >/dev/null 2>&1 || die "run this from a checkout of the repository"

work="$(mktemp -d "${RUNNER_TEMP:-${TMPDIR:-/tmp}}/apogee-release.XXXXXX")"
trap 'rm -rf "$work"' EXIT

# `gh api`, with a 404 as an answer rather than a failure: it returns 3. Any
# other failure prints gh's own error and returns 1, and the caller dies with
# what it was trying to learn -- never from in here, where a message would
# land in the caller's $(...) instead of the log.
api() {
    if gh api "$@" 2>"$work/api.err"; then
        return 0
    fi
    if grep -q 'HTTP 404' "$work/api.err"; then
        return 3
    fi
    cat "$work/api.err" >&2
    return 1
}

# A commit this checkout may not have yet -- a test merge, say -- fetched by id.
have_commit() {
    git cat-file -e "$1^{commit}" 2>/dev/null ||
        git fetch --quiet --no-tags origin "$1" 2>/dev/null ||
        die "could not fetch commit $1 from origin"
}

# --- 1. The pull request -----------------------------------------------------

facts="$(api "repos/{owner}/{repo}/pulls/$pr" \
    --jq '[.state, (.merged | tostring), .base.ref, .head.sha, (.merge_commit_sha // "")] | @tsv')" ||
    die "could not read pull request #$pr"
IFS=$'\t' read -r state merged base head_sha merge_sha <<<"$facts"

[ "$base" = "$RELEASE_BRANCH" ] ||
    die "#$pr merges into '$base'; only a merge into '$RELEASE_BRANCH' is released"
if [ "$publish" = 1 ] && [ "$merged" != true ]; then
    die "#$pr is not merged (it is $state); only a merged pull request is published"
fi
[ -n "$merge_sha" ] ||
    die "GitHub has no merge commit for #$pr (a conflict, or not computed yet), so there is nothing to release"
have_commit "$merge_sha"

if [ "$publish" = 1 ]; then
    note "### Release from #$pr"
else
    note "### Release rehearsal for #$pr -- nothing is published"
fi
note ""
if [ "$merged" = true ]; then
    note "- merged as \`$merge_sha\`"
else
    note "- $state; rehearsing against GitHub's test merge \`$merge_sha\`"
fi

# --- 2. The release, and whether it is already out ---------------------------

version="$(git show "$merge_sha:lib/release/VERSION" 2>/dev/null | tr -d ' \r\n')" ||
    die "the merge $merge_sha has no lib/release/VERSION to name the release"
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] ||
    die "lib/release/VERSION at $merge_sha must hold the release as x.y.z (got '$version')"
tag="v$version"

# `git/ref/tags/<tag>` and never a bare name: the version branch shares it.
tag_sha=""
rc=0
ref="$(api "repos/{owner}/{repo}/git/ref/tags/$tag" --jq '[.object.type, .object.sha] | @tsv')" || rc=$?
if [ "$rc" = 0 ]; then
    IFS=$'\t' read -r kind tag_sha <<<"$ref"
    if [ "$kind" = tag ]; then  # annotated: the commit it points at
        tag_sha="$(api "repos/{owner}/{repo}/git/tags/$tag_sha" --jq '.object.sha')" ||
            die "could not read the annotated tag $tag"
    fi
elif [ "$rc" != 3 ]; then
    die "could not read the tag $tag"
fi

if [ -z "$tag_sha" ]; then
    action=create
elif [ "$tag_sha" = "$merge_sha" ]; then
    action=finish
    note "- $tag already tags this commit: an earlier attempt made it, and this one finishes the release"
else
    note "- **$tag is already released**, from \`${tag_sha:0:7}\`, so this merge publishes nothing."
    note "  Bump \`lib/release/VERSION\` to cut the next release."
    exit 0
fi

# --- 3. The CLI: built, or copied? -------------------------------------------

found="$("$scripts/latest-release.sh" --exclude "$tag")" || die "could not look up the latest release"
latest="" latest_sha=""
[ -z "$found" ] || read -r latest latest_sha <<<"$found"
[ -z "$latest_sha" ] || have_commit "$latest_sha"
cli_changed="$("$scripts/changed.sh" cli "$latest_sha" "$merge_sha" | tail -1)"
cli_changed="${cli_changed#cli=}"

# The one file named $1 under $2, wherever the download put it.
found_file() {
    find "$2" -type f -name "$1" | head -1
}

# check_archive <target> <archive>: the binary and the completions are in it.
check_archive() {
    local listing
    case "$2" in
        *.zip) listing="$(unzip -Z1 "$2")" ;;
        *) listing="$(tar -tzf "$2")" ;;
    esac
    printf '%s\n' "$listing" | grep -Eq '(^|[/\\])apogee(\.exe)?$' ||
        die "$(basename "$2") holds no apogee binary"
    printf '%s\n' "$listing" | grep -q 'completions' ||
        die "$(basename "$2") holds no completions"
}

# The binary this host can run, from whichever archives were chosen.
case "$(uname -s)/$(uname -m)" in
    Linux/x86_64) host=linux-x64 ;;
    Linux/aarch64 | Linux/arm64) host=linux-arm64 ;;
    Darwin/arm64) host=macos-arm64 ;;
    *) die "this host ($(uname -s) $(uname -m)) can run none of the release binaries; run this on Linux or an Apple silicon Mac" ;;
esac
# run_host_binary <dir>: sets `reported` to what `apogee version` says. Not
# called in a $(...): a die in there would be swallowed with the output.
run_host_binary() {
    local archive
    archive="$(found_file "apogee-$host.tar.gz" "$1")"
    [ -n "$archive" ] || die "there is no $host archive to run here"
    rm -rf "$work/probe" "$work/probe-home"
    mkdir -p "$work/probe" "$work/probe-home"
    tar -xzf "$archive" -C "$work/probe"
    reported="$(APOGEE_HOME="$work/probe-home" "$work/probe/apogee" version </dev/null)" ||
        die "the $host binary did not run"
}

assets=()
if [ "$cli_changed" = true ]; then
    # --- 4a. The pull request's own build ------------------------------------
    tree="$(git rev-parse "$merge_sha^{tree}")"
    runs="$(api "repos/{owner}/{repo}/actions/workflows/$CI_WORKFLOW/runs?event=pull_request&head_sha=$head_sha&status=success&per_page=20" \
        --jq '.workflow_runs[].id')" ||
        die "could not list the $CI_WORKFLOW runs of #$pr"

    run_id=""
    targets=""
    for id in $runs; do
        # The platforms this run built are its successful `build <target>`
        # jobs, so the set released follows the build matrix with no list
        # kept here.
        built="$(api "repos/{owner}/{repo}/actions/runs/$id/jobs?per_page=100" \
            --jq '.jobs[] | select(.conclusion == "success") | .name | select(startswith("build ")) | ltrimstr("build ")')" ||
            die "could not list the jobs of run $id"
        [ -n "$built" ] || continue
        have="$(api "repos/{owner}/{repo}/actions/runs/$id/artifacts?per_page=100" \
            --jq '.artifacts[] | select(.expired | not) | .name')" ||
            die "could not list the artifacts of run $id"
        complete=1
        for target in $built; do
            printf '%s\n' "$have" | grep -Fqx "apogee-$target" || { complete=0; break; }
        done
        if [ "$complete" = 1 ]; then
            run_id="$id"
            targets="$built"
            break
        fi
    done
    [ -n "$run_id" ] ||
        die "no successful CI run of #$pr's head ${head_sha:0:7} holds an archive for every platform it built (none ran, or the artifacts expired); nothing was published -- the manual path rebuilds: make -C lib/src/cli release VERSION=$version"
    note "- the CLI changed since ${latest:-the start}: CI run $run_id built $(echo $targets)"

    gh run download "$run_id" --pattern 'apogee-*' --dir "$work/artifacts" >/dev/null

    for target in $targets; do
        case "$target" in
            windows-*) ext=zip ;;
            *) ext=tar.gz ;;
        esac
        archive="$(found_file "apogee-$target.$ext" "$work/artifacts")"
        source_file="$(found_file "apogee-$target.source" "$work/artifacts")"
        [ -n "$archive" ] || die "run $run_id's artifact apogee-$target holds no apogee-$target.$ext"
        [ -n "$source_file" ] ||
            die "run $run_id's artifact apogee-$target has no apogee-$target.source, so what it was built from is unknown"

        built_tree="$(awk '$1 == "tree" { print $2 }' "$source_file")"
        [ "$built_tree" = "$tree" ] ||
            die "apogee-$target was not built from the merge's source (its record: $(tr '\n' ' ' <"$source_file")-- the merge ${merge_sha:0:7} is tree ${tree:0:12}): '$RELEASE_BRANCH' moved after #$pr's last CI run, so these archives are not the merged source. Nothing was published -- the manual path rebuilds: make -C lib/src/cli release VERSION=$version"
        check_archive "$target" "$archive"
        assets+=("$archive")
    done
    note "- every archive was built from the merged source tree \`${tree:0:12}\`"

    run_host_binary "$work/artifacts"
    cli_version="$(printf '%s\n' "$reported" | awk 'NR == 1 && $1 == "apogee" { print $2 }')"
    [ "$cli_version" = "$version" ] ||
        die "the CLI changed, so it ships in $tag and must report $version -- but the binaries report '$reported' (set project(... VERSION $version) in lib/src/cli/CMakeLists.txt)"
    note "- the executable reports \`$reported\`"
else
    # --- 4b. The latest release's, copied ------------------------------------
    gh release download "$latest" --pattern 'apogee-*' --dir "$work/copied" ||
        die "could not download the CLI archives of $latest"
    for archive in "$work/copied"/apogee-*.tar.gz "$work/copied"/apogee-*.zip; do
        [ -f "$archive" ] || continue
        target="$(basename "$archive")"
        target="${target#apogee-}"
        target="${target%.tar.gz}"
        target="${target%.zip}"
        check_archive "$target" "$archive"
        assets+=("$archive")
    done
    [ "${#assets[@]}" -gt 0 ] || die "$latest carries no CLI archives to copy"
    run_host_binary "$work/copied"
    note "- the CLI is unchanged since $latest: its ${#assets[@]} archives are copied from it (the executable reports \`$reported\`)"
fi

# --- 5. Publish ---------------------------------------------------------------

if [ "$publish" != 1 ]; then
    if [ "$action" = create ]; then
        note "- **would tag $tag at \`${merge_sha:0:7}\` and publish its release** with ${#assets[@]} archives"
    else
        note "- **would finish $tag's release** with ${#assets[@]} archives"
    fi
    exit 0
fi

if [ "$action" = create ]; then
    # --target makes the tag at the merge commit in the same call as the release.
    gh release create "$tag" --target "$merge_sha" --title "$tag" --generate-notes "${assets[@]}"
else
    # gh release create can make the release and then fail partway through the
    # uploads; this attempt uploads into it rather than failing on it.
    gh release view "$tag" >/dev/null 2>&1 ||
        gh release create "$tag" --verify-tag --title "$tag" --generate-notes
    gh release upload "$tag" "${assets[@]}" --clobber
fi
note "- **published $tag** at \`${merge_sha:0:7}\` with ${#assets[@]} archives"
