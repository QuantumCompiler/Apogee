#!/usr/bin/env bash
# refresh-parity-fixture.sh -- refresh the command words the parity table is
# held to (ADR 0003), from a real `apogee` binary, deliberately.
#
#   refresh-parity-fixture.sh [apogee-binary]     default: ~/.local/bin/apogee
#
# Writes tests/presentation/shell/Fixtures/command-words.txt: the binary's own
# top-level command words, as its completion protocol answers an empty line
# (`apogee __complete ""`), one a line, sorted. ParityTests holds the table to
# it, so a command a new CLI adds fails the app's suite the day this is run
# against that CLI -- until it is classified -- and one it drops does too.
#
# The binary runs in a throwaway APOGEE_HOME: nothing of ~/.apogee touched.
set -euo pipefail

binary="${1:-$HOME/.local/bin/apogee}"
[[ -x "$binary" ]] || { echo "refresh-parity-fixture.sh: no executable apogee at ${binary}" >&2; exit 1; }
app_dir="$(cd "$(dirname "$0")/.." && pwd)"
out="${app_dir}/tests/presentation/shell/Fixtures/command-words.txt"
sandbox="$(mktemp -d "${TMPDIR:-/tmp}/apogee-parity.XXXXXX")"
trap 'rm -rf "$sandbox"' EXIT

words="$(APOGEE_HOME="$sandbox" "$binary" __complete "" </dev/null | LC_ALL=C sort)"
[[ -n "$words" ]] || { echo "refresh-parity-fixture.sh: ${binary} answered no command words" >&2; exit 1; }
mkdir -p "$(dirname "$out")"
printf '%s\n' "$words" >"$out"
echo "wrote $(wc -l <"$out" | tr -d ' ') command words from $("$binary" version </dev/null | head -1) to ${out#"${app_dir}/"}"
