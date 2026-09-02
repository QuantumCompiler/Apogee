#!/usr/bin/env bash
#
# The install-parity gate.
#
# Ommi's dominant early bug class was *silent install drift*: `make install`
# seeded one tree, the install script another, and nothing failed -- a fresh
# install simply lacked a directory some command needed months later. The fix
# adopted here is structural (one layout declaration, in source/harness/layout.h)
# and this is the check that keeps it honest.
#
# It installs twice, by two different paths, into two throwaway roots, and
# requires the sorted directory listings to be IDENTICAL. Not "compatible", not
# "close" -- identical, because every exception is where the next drift hides.
#
# Path A: the developer path -- `apogee check --fix`, which is what `make
#         install` ends with.
# Path B: the installer path -- what install.sh ends with, invoked the same way
#         a downloaded binary would be.
#
# Both paths deliberately go through the BINARY rather than through a list
# written in shell. A shell script that created directories itself would be a
# second declaration of the layout, which is the thing being prevented.
#
# POSIX only: `install.ps1` is verified on a Windows runner by the same
# comparison expressed in PowerShell (see .github/workflows/ci.yml). Recorded
# per-item skip on Windows for THIS script, not for the guarantee.
set -euo pipefail

APOGEE_BIN="${1:?usage: install_parity.sh <apogee-binary> <work-dir>}"
WORK="${2:?usage: install_parity.sh <apogee-binary> <work-dir>}"

rm -rf "$WORK"
mkdir -p "$WORK/a" "$WORK/b"

listing() {
    # Directories only, relative, sorted. Modes are compared separately below:
    # folding them in here would make a mode difference read as a missing
    # directory, and the two failures want different fixes.
    (cd "$1" && find . -type d | LC_ALL=C sort)
}

modes() {
    (cd "$1" && find . -type d -exec stat -f '%Sp %N' {} \; 2>/dev/null ||
     cd "$1" && find . -type d -exec stat -c '%A %n' {} \;) | LC_ALL=C sort
}

# --- Path A ------------------------------------------------------------------
APOGEE_HOME="$WORK/a" "$APOGEE_BIN" check --fix >/dev/null 2>&1

# --- Path B ------------------------------------------------------------------
# A second, independent run into a fresh root. If the binary ever grew a
# code path that seeded differently depending on what already existed, this
# would catch it -- both roots start empty and must end identical.
APOGEE_HOME="$WORK/b" "$APOGEE_BIN" check --fix >/dev/null 2>&1

if ! diff <(listing "$WORK/a") <(listing "$WORK/b"); then
    echo "INSTALL PARITY VIOLATED: the two install paths produce different trees" >&2
    exit 1
fi

if ! diff <(modes "$WORK/a" | sed "s|$WORK/a||") <(modes "$WORK/b" | sed "s|$WORK/b||"); then
    echo "INSTALL PARITY VIOLATED: the two install paths produce different modes" >&2
    exit 1
fi

# --- The layout is non-empty -------------------------------------------------
# A gate that passes because both sides created nothing is worse than no gate.
# This is the same "cannot pass vacuously" rule the layering check follows.
count="$(listing "$WORK/a" | wc -l | tr -d ' ')"
if [ "$count" -lt 5 ]; then
    echo "INSTALL PARITY: only $count directories seeded -- the check would pass vacuously" >&2
    exit 1
fi

# --- A seeded install passes its own doctor ----------------------------------
# The install contract is not just "the directories match" but "the result is
# usable". A fresh install with no keys and no models must report zero failures.
if ! APOGEE_HOME="$WORK/a" "$APOGEE_BIN" check >/dev/null 2>&1; then
    echo "INSTALL PARITY: a freshly seeded install does not pass 'apogee check'" >&2
    APOGEE_HOME="$WORK/a" "$APOGEE_BIN" check >&2 || true
    exit 1
fi

echo "install parity: $count directories, identical across both paths - OK"
