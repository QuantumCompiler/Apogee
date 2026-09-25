#!/usr/bin/env bash
# changed.sh -- has a deliverable's source changed between two commits?
#
#   changed.sh <deliverable> <from> <to>   answer on the last line: <deliverable>=true|false
#   changed.sh --paths <deliverable>       the paths it is built from, one per line
#
# Each deliverable the repository ships has a PIPELINE -- the jobs that build,
# test and package it -- and that pipeline runs only when what the deliverable
# is built from has changed (user decision, 2026-09-25). When it has not, the
# deliverable is copied from the latest release instead of built again:
# CI copies it into the run's artifacts, and a merge's release publishes the
# copy. This file declares, once, what each deliverable is built from. Today
# there is one, the CLI; a GUI application adds its own entry here, and its
# own pipeline asks this script the same question.
#
# CI asks it with <from> the latest release's commit and <to> the pull
# request's test merge -- so the question is "does what would merge differ
# from what was last released?", which is the question that decides whether a
# copy of the release is still true. An empty <from> (nothing released yet),
# or a commit this repository does not have, answers true: when in doubt,
# build.
#
# On a runner the answer is also appended to $GITHUB_OUTPUT, and a line to
# the run's summary.
set -euo pipefail

usage() {
    sed -n '4,5p' "$0" | sed 's/^# \{0,1\}//' >&2
    exit 2
}

# What each deliverable is built from, as git pathspecs: a directory with its
# trailing slash, a file by its path.
paths_of() {
    case "$1" in
        cli)
            # The CLI's own tree: source, tests, build, pins, assets and
            # completions.
            echo "lib/src/cli/"
            # The scripts that build, package, release and install it.
            echo "lib/scripts/"
            # Its pipeline's own definition -- a change to how the CLI is
            # built must be exercised by building it.
            echo ".github/workflows/ci.yml"
            echo ".github/workflows/release.yml"
            echo ".github/actions/package/"
            # Line endings on checkout: the CLI's embedded assets are pinned
            # byte for byte to their files (it broke Windows once, 2026-09-20).
            echo ".gitattributes"
            ;;
        *)
            printf 'changed.sh: no deliverable named %s (known: cli)\n' "$1" >&2
            exit 2
            ;;
    esac
}

answer() {  # answer <deliverable> <true|false> <why>
    printf '%s\n' "$3"
    printf '%s=%s\n' "$1" "$2"
    if [ -n "${GITHUB_OUTPUT:-}" ]; then
        printf '%s=%s\n' "$1" "$2" >>"$GITHUB_OUTPUT"
    fi
    if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
        printf '**%s:** %s\n\n' "$1" "$3" >>"$GITHUB_STEP_SUMMARY"
    fi
    exit 0
}

case "${1:-}" in
    --paths)
        [ $# -eq 2 ] || usage
        paths_of "$2"
        exit 0
        ;;
    "" | -h | --help) usage ;;
esac
[ $# -eq 3 ] || usage
name="$1"
from="$2"
to="$3"

# Through a variable, not `< <(...)`: an unknown name must stop the script,
# not leave an empty list that diffs the whole tree.
specs="$(paths_of "$name")"
pathspecs=()
while IFS= read -r spec; do
    pathspecs+=("$spec")
done <<<"$specs"

[ -n "$from" ] || answer "$name" true "nothing released yet: the $name pipeline runs"
for commit in "$from" "$to"; do
    git cat-file -e "$commit^{commit}" 2>/dev/null ||
        answer "$name" true "commit $commit is not in this repository: the $name pipeline runs"
done

changed="$(git diff --name-only "$from" "$to" -- "${pathspecs[@]}")"
if [ -z "$changed" ]; then
    answer "$name" false "unchanged since ${from:0:12}: the $name pipeline builds nothing, and its deliverables are copied from the release"
fi
count="$(printf '%s\n' "$changed" | wc -l | tr -d ' ')"
# The first few, so a log says what made this a change.
printf '%s\n' "$changed" | head -n 10 | sed 's/^/changed: /'
answer "$name" true "$count of its files changed since ${from:0:12}: the $name pipeline runs"
