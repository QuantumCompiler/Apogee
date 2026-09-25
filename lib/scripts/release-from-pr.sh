#!/usr/bin/env bash
# release-from-pr.sh -- tag and publish a merged pull request's own CI build.
#
#   release-from-pr.sh <pr-number>             rehearse: every check, no changes
#   release-from-pr.sh <pr-number> --publish   tag the merge commit and publish
#
# CI's `tag and release` job runs this with --publish when a pull request into
# `stable` is merged (user decision, 2026-09-25): the release IS the archives
# that pull request's CI run built and proved runnable -- nothing is rebuilt --
# tagged with the version those binaries report. Without --publish it is a
# rehearsal: the same lookups and checks, then what it would have published.
# CI runs one on request (Actions -> CI -> Run workflow, with the PR number),
# and so can anyone with `gh` signed in, from a checkout of the repository.
#
# In order, each a hard stop:
#
#   1. The pull request: into `stable`, and merged when publishing. Its merge
#      commit -- for an open one, GitHub's test merge -- is what is released.
#   2. The version CMakeLists.txt names at that commit, and its tag. A tag at
#      ANOTHER commit means that version is out, and this merge publishes
#      nothing -- how a docs or hotfix merge declines to cut a release, with
#      no exception rule (exit 0). A tag at THIS commit was made by an earlier
#      attempt, and publishing finishes what that attempt started.
#   3. The newest successful CI run of the pull request's head holding an
#      archive for every platform that run built.
#   4. Every archive built from the merge commit's exact source TREE (its
#      .source file, written by .github/actions/package). CI builds the test
#      merge; if `stable` moved after that run, the archives are not the
#      merged source, and nothing is published.
#   5. The binary for this host, run: `apogee version` must report the
#      version of step 2 -- the tag is the executable's own version.
#   6. The tag at the merge commit and the release in ONE `gh release
#      create`, so no tag is ever left behind without its release.
#
# Needs gh (GH_TOKEN, and GH_REPO or a checkout of the repository), tar and
# unzip. Writes nothing outside its own temp directory, and nothing at all to
# GitHub without --publish.
set -euo pipefail

RELEASE_BRANCH="${RELEASE_BRANCH:-stable}"
CI_WORKFLOW="${CI_WORKFLOW:-ci.yml}"

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

# --- 2. The version, and whether it is already out ---------------------------

cmake_version="$(api "repos/{owner}/{repo}/contents/lib/src/cli/CMakeLists.txt?ref=$merge_sha" \
    -H 'Accept: application/vnd.github.raw+json' |
    sed -n 's/^[[:space:]]*VERSION[[:space:]][[:space:]]*\([0-9][0-9.]*\).*/\1/p' | head -1)" ||
    die "could not read lib/src/cli/CMakeLists.txt at $merge_sha"
[[ "$cmake_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] ||
    die "could not read VERSION from lib/src/cli/CMakeLists.txt at $merge_sha"
tag="v$cmake_version"

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
    note "  Bump \`project(... VERSION x.y.z)\` in \`lib/src/cli/CMakeLists.txt\` to cut the next release."
    exit 0
fi

# --- 3. The pull request's CI run -------------------------------------------

tree="$(api "repos/{owner}/{repo}/git/commits/$merge_sha" --jq '.tree.sha')" ||
    die "could not read the merge commit $merge_sha"
runs="$(api "repos/{owner}/{repo}/actions/workflows/$CI_WORKFLOW/runs?event=pull_request&head_sha=$head_sha&status=success&per_page=20" \
    --jq '.workflow_runs[].id')" ||
    die "could not list the $CI_WORKFLOW runs of #$pr"

run_id=""
targets=""
for id in $runs; do
    # The platforms this run built are its successful `build <target>` jobs,
    # so the set released follows the build matrix with no list kept here.
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
    die "no successful CI run of #$pr's head ${head_sha:0:7} holds an archive for every platform it built (none ran, or the artifacts expired); nothing was published -- the manual path rebuilds: make -C lib/src/cli release VERSION=$cmake_version"
note "- CI run $run_id built $(echo $targets)"

# --- 4. The archives, and the source they were built from --------------------

gh run download "$run_id" --pattern 'apogee-*' --dir "$work/artifacts" >/dev/null

# The one file named $1 under the download, wherever gh put it.
downloaded() {
    find "$work/artifacts" -type f -name "$1" | head -1
}

assets=()
for target in $targets; do
    case "$target" in
        windows-*) ext=zip ;;
        *) ext=tar.gz ;;
    esac
    archive="$(downloaded "apogee-$target.$ext")"
    source_file="$(downloaded "apogee-$target.source")"
    [ -n "$archive" ] || die "run $run_id's artifact apogee-$target holds no apogee-$target.$ext"
    [ -n "$source_file" ] ||
        die "run $run_id's artifact apogee-$target has no apogee-$target.source, so what it was built from is unknown"

    built_tree="$(awk '$1 == "tree" { print $2 }' "$source_file")"
    [ "$built_tree" = "$tree" ] ||
        die "apogee-$target was built from source tree ${built_tree:0:12}, but the merge ${merge_sha:0:7} is tree ${tree:0:12}: '$RELEASE_BRANCH' moved after #$pr's last CI run, so these archives are not the merged source. Nothing was published -- the manual path rebuilds: make -C lib/src/cli release VERSION=$cmake_version"

    case "$ext" in
        zip) listing="$(unzip -Z1 "$archive")" ;;
        *) listing="$(tar -tzf "$archive")" ;;
    esac
    printf '%s\n' "$listing" | grep -Eq '(^|[/\\])apogee(\.exe)?$' ||
        die "apogee-$target.$ext holds no apogee binary"
    printf '%s\n' "$listing" | grep -q 'completions' ||
        die "apogee-$target.$ext holds no completions"
    assets+=("$archive")
done
note "- every archive was built from the merged source tree \`${tree:0:12}\`"

# --- 5. The version the executable reports -----------------------------------

case "$(uname -s)/$(uname -m)" in
    Linux/x86_64) host=linux-x64 ;;
    Linux/aarch64 | Linux/arm64) host=linux-arm64 ;;
    Darwin/arm64) host=macos-arm64 ;;
    *) die "this host ($(uname -s) $(uname -m)) can run none of the release binaries; run this on Linux or an Apple silicon Mac" ;;
esac
printf '%s\n' $targets | grep -Fqx "$host" || die "run $run_id built no $host archive to run here"
mkdir -p "$work/probe" "$work/probe-home"
tar -xzf "$(downloaded "apogee-$host.tar.gz")" -C "$work/probe"
reported="$(APOGEE_HOME="$work/probe-home" "$work/probe/apogee" version </dev/null)" ||
    die "the $host binary from run $run_id did not run"
version="$(printf '%s\n' "$reported" | awk 'NR == 1 && $1 == "apogee" { print $2 }')"
[ "$version" = "$cmake_version" ] ||
    die "the binaries report '$reported', but CMakeLists.txt at ${merge_sha:0:7} names $cmake_version"
note "- the executable reports \`$reported\`"

# --- 6. Publish ---------------------------------------------------------------

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
