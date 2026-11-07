#!/usr/bin/env bash
#
# Apogee installer for macOS and Linux.
#
#   curl -fsSL https://raw.githubusercontent.com/QuantumCompiler/Apogee/stable/lib/scripts/install.sh | bash
#
# Settings, each read from the environment and each optional:
#
#   APOGEE_VERSION=0.2.0       the release to install -- its tag (v0.2.0) or
#                              its number (0.2.0); "latest", the default, is
#                              the newest release
#   PREFIX=$HOME/.local        the binary goes to $PREFIX/bin
#   APOGEE_MINIMAL_INSTALL=1   skip the bootstrap (below)
#
#   curl -fsSL .../install.sh | APOGEE_VERSION=0.2.0 bash
#
# What it does, and nothing else: downloads the release archive for this
# platform, puts the binary on your PATH, installs shell completions for the
# shells you have, asks the binary to create its own data directory, and then
# to bootstrap itself (`apogee bootstrap`, 51): the Python runtime sets this
# machine can run, installed into Apogee's own environment, and its providers
# and stored models registered. APOGEE_MINIMAL_INSTALL=1 skips that last step.
#
# What it deliberately does NOT do:
#   * download a model -- Apogee bundles none and fetches none unasked
#     (SPEC.md -> Non-goals), so a fresh install is keyless and modelless and
#     that is a VALID state that `apogee check` passes
#   * execute anything it downloaded other than the binary you asked for
#   * write outside $PREFIX, $APOGEE_HOME and your shells' completion folders
#   * edit your shell's startup files or your PATH -- where one needs a line
#     added, it prints the line
#
# **The parity rule.** This script does not know what `~/.apogee/` contains --
# it runs `apogee check --fix`, and the binary creates the layout from the one
# declaration in `source/data/contracts/layout.h`. `make install` finishes with the
# same call. That is why the two paths cannot drift: neither of them holds a
# list that could go stale. That drift, with each install path seeding a
# slightly different tree, is the bug class this rule guards against.
set -euo pipefail

REPO="${APOGEE_REPO:-QuantumCompiler/Apogee}"
PREFIX="${PREFIX:-$HOME/.local}"
VERSION="${APOGEE_VERSION:-latest}"

# A release's tag carries its v (v0.2.0), and its number alone names the same
# release (57l), as the Makefile's VERSION does: 0.2.0 asked for a tag that
# does not exist. Anything else is passed through as given, so a tag that does
# not exist still fails naming its URL.
case "$VERSION" in
    latest | v*) ;;
    *) VERSION="v$VERSION" ;;
esac

# The download's scratch directory, at script scope and never `local`: the
# EXIT trap runs after main has returned, and a `local tmp` was out of scope
# by then -- under `set -u` every successful install ended `tmp: unbound
# variable`, exit 1, with its download left behind (57l). Set before the trap
# is, so the trap never reads it unset; and the trap never fails, since a
# failing command in it is the script's exit status.
tmp=""
cleanup() {
    if [ -n "$tmp" ]; then
        rm -rf "$tmp" || true
    fi
}
trap cleanup EXIT

die() { printf 'install: %s\n' "$*" >&2; exit 1; }
say() { printf '%s\n' "$*"; }

# --- Which archive does this machine need? -----------------------------------

detect_target() {
    local os arch
    case "$(uname -s)" in
        Darwin) os=macos ;;
        Linux)  os=linux ;;
        *) die "unsupported OS: $(uname -s). Windows uses install.ps1." ;;
    esac
    case "$(uname -m)" in
        arm64|aarch64) arch=arm64 ;;
        x86_64|amd64)  arch=x64 ;;
        *) die "unsupported architecture: $(uname -m)" ;;
    esac
    # No Intel Mac target (decided 2026-09-19): macOS ships for Apple silicon
    # only, so say so here rather than fail on a download that does not exist.
    if [ "$os" = macos ] && [ "$arch" = x64 ]; then
        die "Intel Macs are not a supported target; Apogee ships macOS binaries for Apple silicon only"
    fi
    printf '%s-%s' "$os" "$arch"
}

# --- Download ----------------------------------------------------------------

fetch() {
    local url="$1" out="$2"
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL "$url" -o "$out"
    elif command -v wget >/dev/null 2>&1; then
        wget -qO "$out" "$url"
    else
        die "need curl or wget"
    fi
}

main() {
    local target archive url
    target="$(detect_target)"
    archive="apogee-${target}.tar.gz"

    if [ "$VERSION" = "latest" ]; then
        url="https://github.com/${REPO}/releases/latest/download/${archive}"
    else
        url="https://github.com/${REPO}/releases/download/${VERSION}/${archive}"
    fi

    # Removed on every exit path by the trap above, a failed download and a
    # finished install alike -- a half extracted archive left in /tmp is the
    # sort of thing that gets picked up by a later run and installed.
    tmp="$(mktemp -d)"

    say "downloading ${archive}"
    fetch "$url" "$tmp/$archive" || die "could not download $url"

    tar -xzf "$tmp/$archive" -C "$tmp" || die "could not extract $archive"
    [ -f "$tmp/apogee" ] || die "archive did not contain an 'apogee' binary"

    mkdir -p "$PREFIX/bin"
    install -m 0755 "$tmp/apogee" "$PREFIX/bin/apogee"
    say "installed $PREFIX/bin/apogee"

    install_completions "$tmp"

    # The binary seeds its own layout, then verifies it. See the parity note.
    say ""
    "$PREFIX/bin/apogee" check --fix || die "post-install check failed"

    # Then it arrives ready (51): the runtime sets this machine can run, its
    # providers and its stored models registered -- each step said, never
    # fatal. APOGEE_MINIMAL_INSTALL=1 skips it; `apogee bootstrap` runs it
    # again any time.
    say ""
    "$PREFIX/bin/apogee" bootstrap || say "note: the bootstrap did not finish -- 'apogee bootstrap' runs it again"

    case ":$PATH:" in
        *":$PREFIX/bin:"*) ;;
        *) say ""
           say "note: $PREFIX/bin is not on your PATH. Add:"
           say "  export PATH=\"$PREFIX/bin:\$PATH\"" ;;
    esac
}

# Completions go only where a shell the user runs already looks (57l):
# creating a config directory for a shell the user does not run is litter,
# and uninstall would have to guess whether it was ours. bash is running this
# script, so its stub is always installed; zsh's and fish's only when that
# shell is on PATH.
install_completions() {
    local from="$1/completions"
    if [ -f "$from/apogee.bash" ]; then
        mkdir -p "$HOME/.local/share/bash-completion/completions"
        install -m 0644 "$from/apogee.bash" \
            "$HOME/.local/share/bash-completion/completions/apogee"
    fi
    if [ -f "$from/_apogee" ] && command -v zsh >/dev/null 2>&1; then
        install_zsh_completion "$from/_apogee"
    fi
    if [ -f "$from/apogee.fish" ] && command -v fish >/dev/null 2>&1; then
        mkdir -p "$HOME/.config/fish/completions"
        install -m 0644 "$from/apogee.fish" "$HOME/.config/fish/completions/apogee.fish"
    fi
}

# zsh loads completions only from a directory on its `fpath`, a per-user
# setting -- so ask zsh where it looks, and take the first directory on it
# that is conventionally a user's own to write into; otherwise the fallback,
# and a note saying the line to add. This is the Makefile's
# install-zsh-completion, whose comment records why (2026-09-06): a fixed
# ~/.local/share/zsh/site-functions is a folder stock zsh never reads, so
# completion silently did nothing -- this script still did that until 57l --
# and the first writable fpath entry under $HOME was a framework's plugin
# folder. A copy, because a script run by curl cannot read the repository:
# cli.install_script holds this list, the Makefile's and uninstall's to one
# set.
install_zsh_completion() {
    local stub="$1" fpath_dirs dir target=""
    fpath_dirs="$(zsh -i -c 'print -l $fpath' </dev/null 2>/dev/null || true)"
    for dir in "$HOME/.zfunc" "$HOME/.oh-my-zsh/completions" "$HOME/.local/share/zsh/site-functions"; do
        # A here-string, not a pipe: under pipefail, grep -q leaving early
        # could fail the printf feeding it and read as no match.
        if grep -qxF "$dir" <<<"$fpath_dirs" && [ -d "$dir" ] && [ -w "$dir" ]; then
            target="$dir"
            break
        fi
    done
    if [ -n "$target" ]; then
        install -m 0644 "$stub" "$target/_apogee"
        return
    fi
    target="$HOME/.local/share/zsh/site-functions"
    mkdir -p "$target"
    install -m 0644 "$stub" "$target/_apogee"
    say ""
    say "note: no writable directory on zsh's fpath was found, so the zsh"
    say "      completion went to $target, which zsh will NOT read until"
    say "      you add it. In ~/.zshrc, before compinit:"
    say "        fpath+=$target"
}

main "$@"
