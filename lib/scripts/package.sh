#!/usr/bin/env bash
# package.sh -- stage one target's release archive, and prove its binary runs.
#
#   package.sh <target> <out-dir>
#
# Expects the target already built by `lib/scripts/cicd.sh --platform <target>`
# in this checkout, and writes two files into <out-dir>:
#
#   apogee-<target>.tar.gz | .zip   the archive: the binary and the four
#                                   completion stubs (they are part of the
#                                   install contract, so they ride along)
#   apogee-<target>.source          the commit and the source TREE it was
#                                   built from -- how the release after a
#                                   merge proves the archive is exactly the
#                                   merged source (release-from-pr.sh)
#
# The tree, not the commit, is the identity that matters: CI builds a pull
# request's test merge commit, and the merge GitHub then makes is a different
# commit with the same tree.
#
# The one packaging definition (2026-09-25). .github/actions/package calls it
# on every CI build and on the manual release, and pr-ci.sh calls it when a
# pull request's workflow is rehearsed locally -- so no path can package
# differently from another. Moved out of the action for that reason: a step
# that lived only in YAML could not be run anywhere but a runner.
#
# Run from anywhere inside the checkout. Writes nothing outside <out-dir>.
set -euo pipefail

die() {
    if [ -n "${GITHUB_ACTIONS:-}" ]; then
        printf '::error title=package.sh::%s\n' "$*"
    fi
    printf 'package.sh: %s\n' "$*" >&2
    exit 1
}

[ $# -eq 2 ] || die "usage: package.sh <target> <out-dir>"
target="$1"
out="$2"
root="$(git rev-parse --show-toplevel 2>/dev/null)" || die "not inside a git repository"

# Absolute, because the Windows archive is written from inside the stage
# directory. Left as given otherwise: on a runner it is $RUNNER_TEMP, a native
# Windows path that 7z must be handed as it is.
case "$out" in
    /* | [A-Za-z]:*) ;;
    *) out="$PWD/$out" ;;
esac
mkdir -p "$out"
stage="$out/stage"
rm -rf "$stage" "$out/verify-home" "$out/apogee-$target".*
mkdir -p "$stage/completions"

# The binary, wherever this generator put it.
binary=""
for candidate in \
    "$root/lib/src/cli/build/$target/source/apogee" \
    "$root/lib/src/cli/build/$target/source/apogee.exe" \
    "$root/lib/src/cli/build/$target/source/Release/apogee.exe"; do
    if [ -f "$candidate" ]; then
        binary="$candidate"
        break
    fi
done
[ -n "$binary" ] || die "no binary built for $target -- run lib/scripts/cicd.sh --platform $target first"
cp "$binary" "$stage/"
cp "$root"/lib/src/cli/completions/* "$stage/completions/"

case "$target" in
    windows-*) (cd "$stage" && 7z a -tzip "$out/apogee-$target.zip" .) ;;
    *) tar -czf "$out/apogee-$target.tar.gz" -C "$stage" . ;;
esac

{
    echo "commit $(git -C "$root" rev-parse HEAD)"
    echo "tree $(git -C "$root" rev-parse 'HEAD^{tree}')"
} >"$out/apogee-$target.source"

# Proof the artifact RUNS, not merely that a file exists -- "distributed
# binaries are verified by RUNNING them post-install". A staged binary that
# cannot start is exactly what a file listing cannot tell you. On a Windows
# runner the action calls this from Git Bash, OUTSIDE the MSYS2 shell, on
# purpose: a user's machine has no MSYS2, and the preset's static link is what
# makes the executable start there -- this is where that is proven.
export APOGEE_HOME="$out/verify-home"
exe="$stage/apogee"
[ -f "$exe" ] || exe="$stage/apogee.exe"
"$exe" version
"$exe" check --fix
"$exe" check

printf 'packaged %s\n' "$out"/apogee-"$target".*
