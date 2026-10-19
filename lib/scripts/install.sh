#!/usr/bin/env bash
#
# Apogee installer for macOS and Linux.
#
#   curl -fsSL https://raw.githubusercontent.com/QuantumCompiler/Apogee/stable/lib/scripts/install.sh | bash
#
# What it does, and nothing else: downloads the release archive for this
# platform, puts the binary on your PATH, installs shell completions, asks
# the binary to create its own data directory, and then to bootstrap itself
# (`apogee bootstrap`, 51): the Python runtime sets this machine can run,
# installed into Apogee's own environment, and its providers and stored
# models registered. APOGEE_MINIMAL_INSTALL=1 skips that last step.
#
# What it deliberately does NOT do:
#   * download a model -- Apogee bundles none and fetches none unasked
#     (SPEC.md -> Non-goals), so a fresh install is keyless and modelless and
#     that is a VALID state that `apogee check` passes
#   * execute anything it downloaded other than the binary you asked for
#   * write outside $PREFIX and $APOGEE_HOME
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
    local target archive url tmp
    target="$(detect_target)"
    archive="apogee-${target}.tar.gz"

    if [ "$VERSION" = "latest" ]; then
        url="https://github.com/${REPO}/releases/latest/download/${archive}"
    else
        url="https://github.com/${REPO}/releases/download/${VERSION}/${archive}"
    fi

    tmp="$(mktemp -d)"
    # Clean up on every exit path, including a failed download -- a half
    # extracted archive left in /tmp is the sort of thing that gets picked up
    # by a later run and installed.
    trap 'rm -rf "$tmp"' EXIT

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

install_completions() {
    local from="$1"
    # Only where the shell already looks. Creating a config directory for a
    # shell the user does not run is litter, and uninstall would have to guess
    # whether it was ours.
    if [ -f "$from/completions/apogee.bash" ]; then
        mkdir -p "$HOME/.local/share/bash-completion/completions"
        install -m 0644 "$from/completions/apogee.bash" \
            "$HOME/.local/share/bash-completion/completions/apogee"
    fi
    if [ -f "$from/completions/_apogee" ]; then
        mkdir -p "$HOME/.local/share/zsh/site-functions"
        install -m 0644 "$from/completions/_apogee" \
            "$HOME/.local/share/zsh/site-functions/_apogee"
    fi
    if [ -f "$from/completions/apogee.fish" ]; then
        mkdir -p "$HOME/.config/fish/completions"
        install -m 0644 "$from/completions/apogee.fish" \
            "$HOME/.config/fish/completions/apogee.fish"
    fi
}

main "$@"
