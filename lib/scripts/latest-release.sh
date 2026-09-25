#!/usr/bin/env bash
# latest-release.sh -- the newest published release: its tag and its commit.
#
#   latest-release.sh [--exclude <tag>]   prints "<tag> <commit>", or nothing
#                                         when no release is published
#
# "Published" is a release on GitHub, not a tag: a draft or a pre-release is
# not a release anyone installs, and a tag without its release is not one
# either. --exclude skips one tag -- the release being made, which an earlier
# attempt may already have created.
#
# The commit is the tag's, as the REMOTE has it, never a local tag: a local
# tag can be stale (one was, 2026-09-25 -- an old annotated v0.1.1 on the
# v0.1.0 merge), and the comparison this answers is against what shipped.
#
# Asks GitHub with gh when gh is signed in (on a runner, GH_TOKEN), else the
# public API with curl and python3. Run from inside the checkout; `origin`
# names the repository.
set -euo pipefail

die() {
    if [ -n "${GITHUB_ACTIONS:-}" ]; then
        printf '::error title=latest-release.sh::%s\n' "$*"
    fi
    printf 'latest-release.sh: %s\n' "$*" >&2
    exit 1
}

exclude=""
case "${1:-}" in
    --exclude)
        [ $# -eq 2 ] || die "usage: latest-release.sh [--exclude <tag>]"
        exclude="$2"
        ;;
    "") ;;
    *) die "usage: latest-release.sh [--exclude <tag>]" ;;
esac

# owner/repo: the runner says; elsewhere, origin's URL does.
repo="${GH_REPO:-${GITHUB_REPOSITORY:-}}"
if [ -z "$repo" ]; then
    url="$(git remote get-url origin 2>/dev/null)" || die "no origin remote to name the repository"
    repo="$(printf '%s\n' "$url" | sed -E 's#^(git@|ssh://git@|https://)github\.com[:/]##; s#\.git$##')"
fi

# Published releases, newest first: tag names only.
if command -v gh >/dev/null 2>&1 &&
    { [ -n "${GH_TOKEN:-}${GITHUB_TOKEN:-}" ] || gh auth status >/dev/null 2>&1; }; then
    tags="$(GH_REPO="$repo" gh api "repos/$repo/releases?per_page=30" \
        --jq '.[] | select((.draft | not) and (.prerelease | not)) | .tag_name')" ||
        die "could not list the releases of $repo"
else
    auth=()
    [ -n "${GH_TOKEN:-}" ] && auth=(-H "Authorization: Bearer $GH_TOKEN")
    tags="$(curl -fsSL ${auth[@]+"${auth[@]}"} "https://api.github.com/repos/$repo/releases?per_page=30" |
        python3 -c 'import json, sys
for r in json.load(sys.stdin):
    if not r["draft"] and not r["prerelease"]:
        print(r["tag_name"])')" || die "could not list the releases of $repo"
fi

tag=""
while IFS= read -r candidate; do
    [ -n "$candidate" ] || continue
    [ "$candidate" = "$exclude" ] && continue
    tag="$candidate"
    break
done <<<"$tags"
[ -n "$tag" ] || exit 0

# The commit the remote's tag names, peeled if it is annotated.
commit="$(git ls-remote --tags origin "refs/tags/$tag" "refs/tags/$tag^{}" |
    awk -v t="refs/tags/$tag" '{ sha[$2] = $1 } END { print (t "^{}" in sha) ? sha[t "^{}"] : sha[t] }')"
[ -n "$commit" ] || die "release $tag has no tag on origin"
printf '%s %s\n' "$tag" "$commit"
