#!/usr/bin/env bash
#
# The install-parity gate.
#
# *Silent install drift* is the bug class this guards against: `make install`
# seeds one tree, the install script another, and nothing fails -- a fresh
# install simply lacks a directory some command needs months later. The fix
# adopted here is structural (one layout declaration, in source/data/contracts/layout.h)
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
# Path C (M10): a channel install -- the dev build `make install MODE=dev`
#         makes, given as the optional third argument -- run with no
#         APOGEE_HOME and no flag, so its baked root is the one it seeds. The
#         same tree, under its own root, never the release one, by a binary
#         under its suffixed name. A channel changes the root, never the
#         contents -- and the channel's uninstall takes its own root and
#         nothing of the release install's, whatever flag is on the line.
#
# POSIX only: `install.ps1` has no runner job any more (CI is the builds
# alone, user decision 2026-09-19); the guarantee it shares -- the binary,
# not a script, owns the layout -- is what `cli.install_is_ours_only` and
# this case pin on every POSIX build. Recorded per-item skip on Windows for
# THIS script, not for the guarantee.
set -euo pipefail

usage="usage: install_parity.sh <apogee-binary> <work-dir> [<channel-binary>]"
APOGEE_BIN="${1:?$usage}"
WORK="${2:?$usage}"
CHANNEL_BIN="${3:-}"

rm -rf "$WORK"
mkdir -p "$WORK/a" "$WORK/b"

listing() {
    # Directories only, relative, sorted. Modes are compared separately below:
    # folding them in here would make a mode difference read as a missing
    # directory, and the two failures want different fixes.
    (cd "$1" && find . -type d | LC_ALL=C sort)
}

# BSD stat (macOS) takes -f FORMAT; GNU stat (Linux) reads -f as "file system"
# and FORMAT as a file to report on, so ask which one this is first. The
# earlier `a || b && c` form ran the GNU branch on macOS too -- `&&` binds
# after `||` in the shell -- and printed its usage error once per directory.
modes() {
    if stat -f '%Sp' "$1" >/dev/null 2>&1; then
        (cd "$1" && find . -type d -exec stat -f '%Sp %N' {} \;) | LC_ALL=C sort
    else
        (cd "$1" && find . -type d -exec stat -c '%A %n' {} \;) | LC_ALL=C sort
    fi
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

# --- The shipped symphonies, identical on every path (27q) ----------------------
# A starter is a seeded file, not a directory, so the listings above cannot see
# it: each must be on both paths, byte for byte the repository's asset, and no
# path may seed one that does not ship.
SYMPHONY_ASSETS="$(cd "$(dirname "$0")/../../../assets/symphonies" && pwd)"
same_starters() {  # same_starters <root> <what seeded it>
    if ! diff <(ls "$SYMPHONY_ASSETS") <(ls "$1/symphonies"); then
        echo "INSTALL PARITY VIOLATED: $2 seeds a different set of symphony starters" >&2
        exit 1
    fi
    for starter in "$SYMPHONY_ASSETS"/*.yaml; do
        if ! cmp -s "$starter" "$1/symphonies/$(basename "$starter")"; then
            echo "INSTALL PARITY VIOLATED: $2 seeds symphonies/$(basename "$starter")" \
                 "unlike the shipped asset" >&2
            exit 1
        fi
    done
}
same_starters "$WORK/a" "path A"
same_starters "$WORK/b" "path B"

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
# ...and with ZERO warnings (M11): --fix seeded the starter config, and the
# opt-in rows (the training env, the MLX runtime) are skips -- so a fresh
# install leaves nothing for the user to resolve, and any future feature
# that re-warns on a fresh install fails here rather than shipping.
if ! APOGEE_HOME="$WORK/a" "$APOGEE_BIN" check --output-format json 2>/dev/null \
        | grep -q '"warnings":0'; then
    echo "INSTALL PARITY: a freshly seeded install reports warnings -- installs end green (M11)" >&2
    APOGEE_HOME="$WORK/a" "$APOGEE_BIN" check >&2 || true
    exit 1
fi

# --- Path C: a channel install, under its own root ---------------------------
# Everything here runs with APOGEE_HOME unset and HOME pointed into the work
# directory: the baked root is derived from the home directory, and the real
# one is never touched.

# `apogee` is the release build; `apogee-<channel>` the others.
channel_of() {
    local name
    name="$(basename "$1")"
    case "$name" in
        apogee) echo release ;;
        apogee-*) echo "${name#apogee-}" ;;
        *) echo "INSTALL PARITY: '$name' is no channel's binary name" >&2; exit 1 ;;
    esac
}
root_name() {
    if [ "$1" = release ]; then echo .apogee; else echo ".apogee-$1"; fi
}

# The tree at $1 is path A's, directories and modes alike.
same_tree() {
    if ! diff <(listing "$WORK/a") <(listing "$1"); then
        echo "INSTALL PARITY VIOLATED: $2 seeds a different tree" >&2
        exit 1
    fi
    if ! diff <(modes "$WORK/a" | sed "s|$WORK/a||") <(modes "$1" | sed "s|$1||"); then
        echo "INSTALL PARITY VIOLATED: $2 seeds different modes" >&2
        exit 1
    fi
    same_starters "$1" "$2"
}

if [ -n "$CHANNEL_BIN" ]; then
    channel="$(channel_of "$CHANNEL_BIN")"
    release="$(channel_of "$APOGEE_BIN")"
    if [ "$channel" = "$release" ]; then
        echo "INSTALL PARITY: the channel build ($channel) is the build under test's own channel" >&2
        exit 1
    fi

    user="$WORK/home-$channel"
    mkdir -p "$user"
    env -u APOGEE_HOME HOME="$user" "$CHANNEL_BIN" check --fix >/dev/null 2>&1
    root="$user/$(root_name "$channel")"
    if [ ! -d "$root" ]; then
        echo "INSTALL PARITY: the $channel build seeded nothing at its own root, $root" >&2
        exit 1
    fi
    if [ -e "$user/$(root_name "$release")" ]; then
        echo "INSTALL PARITY: the $channel build touched the $release root" >&2
        exit 1
    fi
    same_tree "$root" "the $channel channel's install"
    if ! env -u APOGEE_HOME HOME="$user" "$CHANNEL_BIN" check >/dev/null 2>&1; then
        echo "INSTALL PARITY: a fresh $channel install does not pass its own 'check'" >&2
        env -u APOGEE_HOME HOME="$user" "$CHANNEL_BIN" check >&2 || true
        exit 1
    fi
    # Zero warnings here too (M11): every channel's fresh install ends green.
    if ! env -u APOGEE_HOME HOME="$user" "$CHANNEL_BIN" check --output-format json 2>/dev/null \
            | grep -q '"warnings":0'; then
        echo "INSTALL PARITY: a fresh $channel install reports warnings -- installs end green (M11)" >&2
        env -u APOGEE_HOME HOME="$user" "$CHANNEL_BIN" check >&2 || true
        exit 1
    fi

    # It says which Apogee it is: the channel, the root, and why.
    said="$(env -u APOGEE_HOME HOME="$user" "$CHANNEL_BIN" version </dev/null)"
    for line in "channel: $channel" \
                "root: $root (the $channel channel's own root, baked into this build)"; do
        if ! printf '%s\n' "$said" | grep -qxF "$line"; then
            echo "INSTALL PARITY: '$(basename "$CHANNEL_BIN") version' does not say '$line':" >&2
            printf '%s\n' "$said" >&2
            exit 1
        fi
    done

    # Its uninstall removes its own install and nothing of the other's -- with
    # the other channel's flag in the air, which uninstall ignores. Run from a
    # copy, so the binary that removes itself is the sandbox's; and only after
    # `version` above has said its root is the sandbox's, the interlock that
    # keeps this away from a real install.
    env -u APOGEE_HOME HOME="$user" "$APOGEE_BIN" check --fix >/dev/null 2>&1
    stub="$user/.local/share/bash-completion/completions/apogee"
    mkdir -p "$(dirname "$stub")"
    echo "# the release install's stub" >"$stub"
    mkdir -p "$WORK/prefix/bin"
    copy="$WORK/prefix/bin/$(basename "$CHANNEL_BIN")"
    cp "$CHANNEL_BIN" "$copy"
    env -u APOGEE_HOME HOME="$user" "$copy" "--$release" uninstall --yes </dev/null >/dev/null
    for gone in "$root" "$copy"; do
        if [ -e "$gone" ]; then
            echo "INSTALL PARITY: the $channel uninstall left $gone behind" >&2
            exit 1
        fi
    done
    for kept in "$user/$(root_name "$release")" "$stub"; do
        if [ ! -e "$kept" ]; then
            echo "INSTALL PARITY: the $channel uninstall took $kept, the $release install's" >&2
            exit 1
        fi
    done

    # The other way round: the build under test, pointed at that channel's
    # root by its flag for one run, seeds the same tree there -- the flag
    # re-roots the whole layout -- and its own root stays untouched.
    user="$WORK/home-flag"
    mkdir -p "$user"
    env -u APOGEE_HOME HOME="$user" "$APOGEE_BIN" "--$channel" check --fix >/dev/null 2>&1
    same_tree "$user/$(root_name "$channel")" "'$(basename "$APOGEE_BIN") --$channel'"
    if [ -e "$user/$(root_name "$release")" ]; then
        echo "INSTALL PARITY: '--$channel' also seeded the $release root" >&2
        exit 1
    fi
    echo "install parity: the $channel channel seeds the same tree at $(root_name "$channel")," \
         "and its uninstall takes only its own - OK"
fi

echo "install parity: $count directories, identical across both paths - OK"
