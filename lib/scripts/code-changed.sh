#!/usr/bin/env bash
# code-changed.sh -- does a change touch anything but documentation?
#
#   git diff --name-only <range> | code-changed.sh
#
# Reads the changed paths, one per line, and answers on its last line:
#
#   code=true    something outside the documentation changed: CI runs everything
#   code=false   documentation only, or nothing: CI's jobs report success and do
#                no work (user decision, 2026-09-25)
#
# On a runner the same line is appended to $GITHUB_OUTPUT, so the job that runs
# this hands the answer to every other job; pr-ci.sh asks it the same way.
#
# Documentation, declared here and nowhere else:
#
#   lib/documentation/**   the contributor docs, the backlog, the references
#   .claude/**             the repo-local agent skills
#   <root>/*.md            Markdown at the top of the repository (README.md)
#
# Everything else is code -- source, tests and their fixtures (the Markdown
# under tests/fixtures/ is test input), scripts, workflows, CMake.
#
# Skipping loses no test: nothing CI runs reads those paths. The source suite
# never opens them, and the two checks that pin the reference docs to the code
# (cli.http_api_conformance, cli.machine_schema_conformance) are ctest entries,
# which CI does not run -- `make test` does, before a push.
set -euo pipefail

is_docs() {
    case "$1" in
        lib/documentation/*) return 0 ;;
        .claude/*) return 0 ;;
        */*) return 1 ;;
        *.md) return 0 ;;
    esac
    return 1
}

total=0
code=0
while IFS= read -r path || [ -n "$path" ]; do
    path="${path%$'\r'}"
    [ -n "$path" ] || continue
    total=$((total + 1))
    if ! is_docs "$path"; then
        code=$((code + 1))
        # The first few, so a log says what made this a code change.
        [ "$code" -le 10 ] && printf 'code: %s\n' "$path"
    fi
done

if [ "$code" -gt 0 ]; then
    answer="code=true"
    printf '%s of %s changed files are code\n' "$code" "$total"
else
    answer="code=false"
    printf 'documentation only (%s changed files): nothing to build or test\n' "$total"
fi
printf '%s\n' "$answer"
if [ -n "${GITHUB_OUTPUT:-}" ]; then
    printf '%s\n' "$answer" >>"$GITHUB_OUTPUT"
fi
if [ -n "${GITHUB_STEP_SUMMARY:-}" ] && [ "$code" -eq 0 ]; then
    printf 'Documentation only (%s changed files): every job reports success without building.\n' \
        "$total" >>"$GITHUB_STEP_SUMMARY"
fi
