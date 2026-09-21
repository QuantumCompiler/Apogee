#!/usr/bin/env bash
# ci-annotate.sh -- turn a failed cicd.sh log into GitHub error annotations.
#
#   ci-annotate.sh "<label>" <log-file>
#
# Why this exists: on a public repository the check-run ANNOTATIONS are public
# through the REST API, while the raw job log needs admin rights on the
# repository. A failure only the owner can read is a failure nobody else can
# fix, so the workflow's build and test steps tee their output and, when the
# step fails, hand the log here. Each annotation is at most ~4 KB (GitHub
# truncates beyond that) and a step shows at most ten, so this emits:
#
#   1. one annotation with the ctest summary -- the "The following tests
#      FAILED" block -- or, when the log has no ctest summary (a configure or
#      compile failure), the last 25 lines;
#   2. one annotation per failed test, up to nine, holding that test's own
#      output as ctest --output-on-failure printed it.
#
# Lines are compacted first: ctest pads names with dots to the terminal width,
# and 40 padded lines alone overflowed the limit and hid the summary the first
# time this was tried (2026-09-20).
set -euo pipefail

label="${1:?usage: ci-annotate.sh <label> <log-file>}"
log="${2:?usage: ci-annotate.sh <label> <log-file>}"
[[ -f "$log" ]] || { printf '::error title=%s::log not found: %s\n' "$label" "$log"; exit 0; }

# Compact a stream: drop ctest's dot padding, cap line length, then encode
# for a single-line annotation message (%, CR and LF are the reserved bytes).
compact() {
    sed -E 's/ \.{5,}/ /; s/\r$//' | cut -c1-220
}
encode() {
    sed 's/%/%25/g; s/\r/%0D/g' | awk '{printf "%s%%0A", $0}'
}
emit() {  # emit <title> <text>
    local title="$1" text="$2"
    # ~3800 bytes keeps the whole message under GitHub's limit after encoding.
    printf '::error title=%s::%s\n' "$title" "$(printf '%s\n' "$text" | head -c 3800 | encode)"
}

if grep -q 'The following tests FAILED' "$log"; then
    summary="$(awk '/tests passed|The following tests FAILED/{p=1} p' "$log" | compact)"
    emit "${label}: ctest summary" "$summary"

    # The failed test numbers, from the summary block.
    numbers="$(awk '/The following tests FAILED/{p=1; next} p && /^[[:space:]]*[0-9]+ - /{print $1}' "$log" | head -n 9)"
    for n in $numbers; do
        # With --output-on-failure a test's output follows its result line and
        # runs to the next "Start N:" line (or the summary).
        block="$(awk -v n="$n" '
            $0 ~ ("^[[:space:]]*" n "/[0-9]+ Test +#" n ":") {p=1; print; next}
            p && /^[[:space:]]+Start [0-9]+:/ {exit}
            p && /tests passed|tests failed/ {exit}
            p {print}' "$log" | compact)"
        name="$(awk -v n="$n" '/The following tests FAILED/{p=1; next} p && $1==n {sub(/^[[:space:]]*[0-9]+ - /, ""); print; exit}' "$log")"
        emit "${label}: test ${n} -- ${name}" "$block"
    done
else
    emit "${label}: last 25 lines" "$(tail -n 25 "$log" | compact)"
fi
