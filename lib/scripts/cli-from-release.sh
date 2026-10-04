#!/usr/bin/env bash
# cli-from-release.sh -- copy one target's CLI archive from a release, and
# prove the copied binary runs.
#
#   cli-from-release.sh <tag> <target> <out-dir>
#
# The other half of package.sh. When the CLI's source is unchanged since the
# latest release (changed.sh cli), its pipeline builds nothing and the CLI
# deliverables are COPIED from that release instead (user decision,
# 2026-09-25) -- so a pull request that changes something else, a GUI
# application one day, still has the CLI in its run, byte for byte what was
# released. CI's build job for <target> runs this in place of the build, on
# that target's own runner, and uploads the result as the same artifact a
# build would: `apogee-<target>`, holding
#
#   apogee-<target>.tar.gz | .zip   the released archive, unmodified
#   apogee-<target>.source          `release <tag>`: where it came from
#
# Downloaded from the release's public URL, which needs no token on a public
# repository. Writes nothing outside <out-dir>.
set -euo pipefail

die() {
    if [ -n "${GITHUB_ACTIONS:-}" ]; then
        printf '::error title=cli-from-release.sh::%s\n' "$*"
    fi
    printf 'cli-from-release.sh: %s\n' "$*" >&2
    exit 1
}

[ $# -eq 3 ] || die "usage: cli-from-release.sh <tag> <target> <out-dir>"
tag="$1"
target="$2"
out="$3"
[ -n "$tag" ] || die "no release to copy from"

repo="${GH_REPO:-${GITHUB_REPOSITORY:-}}"
if [ -z "$repo" ]; then
    url="$(git remote get-url origin 2>/dev/null)" || die "no origin remote to name the repository"
    repo="$(printf '%s\n' "$url" | sed -E 's#^(git@|ssh://git@|https://)github\.com[:/]##; s#\.git$##')"
fi

case "$target" in
    windows-*) ext=zip exe=apogee.exe ;;
    *) ext=tar.gz exe=apogee ;;
esac
archive="apogee-$target.$ext"

# Absolute, as package.sh keeps it: on a runner it is $RUNNER_TEMP.
case "$out" in
    /* | [A-Za-z]:*) ;;
    *) out="$PWD/$out" ;;
esac
mkdir -p "$out"
rm -rf "$out/unpacked" "$out/verify-home" "$out/apogee-$target".*

curl -fsSL --retry 3 -o "$out/$archive" "https://github.com/$repo/releases/download/$tag/$archive" ||
    die "release $tag of $repo has no $archive to copy"
printf 'release %s\n' "$tag" >"$out/apogee-$target.source"

# Proof the copy runs HERE, on its own platform: a release's asset is only as
# good as the binary in it. Unpacked beside the archive, never over it.
mkdir -p "$out/unpacked"
case "$ext" in
    zip) (cd "$out/unpacked" && 7z x -y "$out/$archive" >/dev/null) ;;
    *) tar -xzf "$out/$archive" -C "$out/unpacked" ;;
esac
[ -f "$out/unpacked/$exe" ] || die "$archive from $tag holds no $exe"
APOGEE_HOME="$out/verify-home" "$out/unpacked/$exe" version </dev/null
rm -rf "$out/unpacked" "$out/verify-home"

printf 'copied %s from %s\n' "$archive" "$tag"
