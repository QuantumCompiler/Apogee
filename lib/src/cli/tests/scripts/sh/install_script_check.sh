#!/usr/bin/env bash
#
# The install scripts, run (57l).
#
# `cli.install_parity` reads the installers' text for the order of their
# steps and never runs one, so for a year nothing ran `install.sh`: every
# successful install ended `tmp: unbound variable` and exit 1 with its
# download left in $TMPDIR, put zsh's completion in a folder stock zsh does
# not read, and made fish and bash folders for shells the machine did not
# have. This runs it, end to end, in a sandbox:
#
#   * HOME, PREFIX, TMPDIR and APOGEE_HOME are folders of its own, with
#     APOGEE_MINIMAL_INSTALL=1 -- never the network, never the real install;
#   * PATH is a farm of the few tools the script needs plus stubs: a `curl`
#     that records the URL it is asked for and serves an archive packed from
#     this build's binary and completions/, a `mktemp` that makes its folder
#     under the run's TMPDIR and logs it (macOS's own takes the per-user
#     temporary folder whatever TMPDIR says, which is where the sweep found
#     the leaks), a `zsh` that answers `print -l $fpath` with a chosen fpath,
#     and a `fish` where one is meant to be present -- so which shells "the
#     machine has" is the test's call, whatever this host has installed;
#   * the installed binary's own `uninstall --yes` must then leave no file
#     the script wrote.
#
# The Makefile's install-completions runs under the same stubs, since it is
# the other installer that places completions on this platform, and its zsh
# note must be install.sh's word for word. install.ps1 cannot run here (no
# host in the suite runs PowerShell), so it is READ: its PATH line, its child
# scope and its version handling. And the three copies of zsh's directory
# list -- install.sh's, the Makefile's, and uninstall's, read from its plan --
# are held to one set.
#
# POSIX only, like cli.install_parity.
set -euo pipefail

usage="usage: install_script_check.sh <apogee-binary> <work-dir>"
APOGEE_BIN="${1:?$usage}"
WORK="${2:?$usage}"

HERE="$(cd "$(dirname "$0")" && pwd)"
CLI="$(cd "$HERE/../../.." && pwd)"
SCRIPTS="$(cd "$CLI/../../scripts" && pwd)"
README="$(cd "$CLI/../../.." && pwd)/README.md"
INSTALL_SH="$SCRIPTS/install.sh"
INSTALL_PS1="$SCRIPTS/install.ps1"
MAKEFILE="$CLI/Makefile"

. "$HERE/sandbox.sh"
WORK="$(sandbox_dir "$WORK")"
trap 'rm -rf "$WORK"' EXIT

failures=0
fail() {
    echo "FAIL: $*" >&2
    failures=$((failures + 1))
}

# Resolved before PATH is narrowed below.
REAL_ZSH="$(command -v zsh 2>/dev/null || true)"
MAKE_BIN="$(command -v make 2>/dev/null || true)"
CP_BIN="$(command -v cp)"
MKTEMP_BIN="$(command -v mktemp)"

# --- The tool farm -----------------------------------------------------------
# What install.sh, the Makefile's completion targets and the binary they run
# need, and nothing else -- no bash, zsh or fish unless a run adds a stub.
FARM="$WORK/tools"
mkdir -p "$FARM"
for tool in uname mktemp mkdir install rm tar gzip grep find cp cat sed sort ls chmod \
            dirname basename tr head wc env; do
    found="$(command -v "$tool" 2>/dev/null || true)"
    case "$found" in
        /*) ln -s "$found" "$FARM/$tool" ;;
    esac
done

# --- The archive a release ships, packed from this build ----------------------
# As lib/scripts/package.sh stages it: the binary and completions/ at the top.
stage="$WORK/stage"
mkdir -p "$stage/completions"
cp "$APOGEE_BIN" "$stage/apogee"
cp "$CLI"/completions/* "$stage/completions/"
tar -czf "$WORK/archive.tar.gz" -C "$stage" .
rm -rf "$stage"

# install.sh's own name for this host's archive; a host it does not support
# (an Intel Mac) runs only the reads below.
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) target=macos-arm64 ;;
    Linux-x86_64 | Linux-amd64) target=linux-x64 ;;
    Linux-aarch64 | Linux-arm64) target=linux-arm64 ;;
    *) target="" ;;
esac
RELEASES="https://github.com/QuantumCompiler/Apogee/releases"

# --- Stubs -------------------------------------------------------------------
# stub_bin <run>: the run's own bin folder, with the curl stub in it.
stub_bin() {
    mkdir -p "$1/bin"
    cat >"$1/bin/curl" <<'EOF'
#!/bin/sh
# curl -fsSL <url> -o <out>, as install.sh calls it.
url=""
out=""
while [ $# -gt 0 ]; do
    case "$1" in
        -o) out="$2"; shift 2 ;;
        -*) shift ;;
        *) url="$1"; shift ;;
    esac
done
printf '%s\n' "$url" >>"$STUB_CURL_LOG"
if [ -n "${STUB_CURL_404:-}" ]; then
    echo "curl: (22) The requested URL returned error: 404" >&2
    exit 22
fi
exec "$STUB_CP" "$STUB_ARCHIVE" "$out"
EOF
    chmod +x "$1/bin/curl"
    cat >"$1/bin/mktemp" <<'EOF'
#!/bin/sh
# mktemp -d, as install.sh calls it: under this run's TMPDIR, and logged.
[ "$*" = "-d" ] || exec "$STUB_MKTEMP" "$@"
made="$("$STUB_MKTEMP" -d "$TMPDIR/tmp.XXXXXXXX")" || exit 1
printf '%s\n' "$made" >>"$STUB_MKTEMP_LOG"
printf '%s\n' "$made"
EOF
    chmod +x "$1/bin/mktemp"
}

# stub_zsh <run> <fpath entry>...: a zsh whose `print -l $fpath` answers these.
stub_zsh() {
    local run="$1"
    shift
    printf '%s\n' "$@" >"$run/fpath.txt"
    cat >"$run/bin/zsh" <<'EOF'
#!/bin/sh
# zsh -i -c 'print -l $fpath', answered as a zsh with this fpath would.
while IFS= read -r entry; do printf '%s\n' "$entry"; done <"$STUB_FPATH"
EOF
    chmod +x "$run/bin/zsh"
}

# stub_fish <run>: fish is on PATH.
stub_fish() {
    printf '#!/bin/sh\nexit 0\n' >"$1/bin/fish"
    chmod +x "$1/bin/fish"
}

# --- Running install.sh --------------------------------------------------------
# install_sh <run> <bash> [VAR=value...]: install.sh run as `curl | bash`
# would, with nothing of this process's environment but what is named.
install_sh() {
    local run="$1" shell="$2"
    shift 2
    mkdir -p "$run/home" "$run/tmp"
    : >"$run/curl.log"
    : >"$run/mktemp.log"
    local status=0
    env -i HOME="$run/home" PATH="$run/bin:$FARM" TMPDIR="$run/tmp" PREFIX="$run/prefix" \
        APOGEE_HOME="$run/apogee-home" APOGEE_MINIMAL_INSTALL=1 \
        STUB_CURL_LOG="$run/curl.log" STUB_ARCHIVE="$WORK/archive.tar.gz" STUB_CP="$CP_BIN" \
        STUB_MKTEMP="$MKTEMP_BIN" STUB_MKTEMP_LOG="$run/mktemp.log" \
        STUB_FPATH="$run/fpath.txt" "$@" \
        "$shell" "$INSTALL_SH" </dev/null >"$run/out.txt" 2>"$run/err.txt" || status=$?
    echo "$status" >"$run/status"
}

status_of() { cat "$1/status"; }

# cleaned_up <run> <name>: install.sh made its one folder, and removed it.
cleaned_up() {
    local run="$1" name="$2"
    if [ "$(grep -c . "$run/mktemp.log")" != 1 ]; then
        fail "$name: install.sh made $(grep -c . "$run/mktemp.log") temporary folders, not 1"
    fi
    if [ -n "$(ls -A "$run/tmp")" ]; then
        fail "$name: install.sh left its download in TMPDIR: $(ls -A "$run/tmp" | tr '\n' ' ')"
    fi
}

# finished_clean <run> <name>: exit 0, no unbound variable, TMPDIR empty.
finished_clean() {
    local run="$1" name="$2"
    if [ "$(status_of "$run")" != 0 ]; then
        fail "$name: install.sh exited $(status_of "$run"):"
        cat "$run/out.txt" "$run/err.txt" >&2
    fi
    if grep -q 'unbound variable' "$run/err.txt"; then
        fail "$name: install.sh said: $(grep 'unbound variable' "$run/err.txt")"
    fi
    cleaned_up "$run" "$name"
}

# asked_for <run> <name> <url>: the one URL curl was asked for.
asked_for() {
    local got
    got="$(cat "$1/curl.log")"
    if [ "$got" != "$3" ]; then
        fail "$2: install.sh asked for '$got', not '$3'"
    fi
}

present() { [ -e "$1" ] || fail "$2: $1 is missing"; }
absent() { [ ! -e "$1" ] || fail "$2: $1 was made"; }

# uninstalled_clean <run> <name>: the installed binary's own uninstall
# leaves no file the script wrote, and no data directory.
uninstalled_clean() {
    local run="$1" name="$2" left
    if ! env -i HOME="$run/home" PATH="$run/prefix/bin:$FARM" APOGEE_HOME="$run/apogee-home" \
            "$run/prefix/bin/apogee" uninstall --yes </dev/null >"$run/uninstall.txt" 2>&1; then
        fail "$name: uninstall --yes failed:"
        cat "$run/uninstall.txt" >&2
    fi
    left="$(find "$run/home" "$run/prefix" -type f 2>/dev/null)"
    if [ -n "$left" ]; then
        fail "$name: uninstall left what install.sh wrote: $(printf '%s' "$left" | tr '\n' ' ')"
    fi
    absent "$run/apogee-home" "$name (uninstall)"
}

# The note, with this run's home spelled ~HOME~, so two runs compare.
zsh_note() {
    grep -A3 "^note: no writable directory on zsh's fpath" "$1" | sed "s|$2|~HOME~|g" || true
}

if [ -z "$target" ]; then
    echo "install.sh: $(uname -s)-$(uname -m) is no target install.sh serves -- its runs skipped"
else
    # --- A: zsh's fpath holds ~/.zfunc; no fish -------------------------------
    # And a framework's plugin folder on the fpath ahead of it, writable: the
    # 2026-09-06 lesson -- never chosen.
    run="$WORK/a"
    stub_bin "$run"
    mkdir -p "$run/home/.zfunc" "$run/home/.oh-my-zsh/plugins/vscode"
    stub_zsh "$run" "$run/home/.oh-my-zsh/plugins/vscode" "$run/home/.zfunc" \
        /usr/share/zsh/site-functions
    install_sh "$run" "$BASH"
    finished_clean "$run" "A"
    asked_for "$run" "A" "$RELEASES/latest/download/apogee-$target.tar.gz"
    present "$run/prefix/bin/apogee" "A"
    present "$run/apogee-home" "A"
    present "$run/home/.zfunc/_apogee" "A (zsh, on fpath)"
    present "$run/home/.local/share/bash-completion/completions/apogee" "A (bash)"
    absent "$run/home/.local/share/zsh" "A (zsh's fallback, with ~/.zfunc on fpath)"
    absent "$run/home/.oh-my-zsh/plugins/vscode/_apogee" "A (a framework's folder)"
    absent "$run/home/.config/fish" "A (no fish on PATH)"
    if grep -q 'fpath+=' "$run/out.txt"; then
        fail "A: install.sh printed the fpath note with ~/.zfunc on fpath"
    fi

    # The stub it placed completes, in a fresh zsh with that fpath. The
    # compsys builtins exist only inside a widget, so they are stood in for,
    # as cli.shell_completion does.
    if [ -n "$REAL_ZSH" ]; then
        offered="$(env -i HOME="$run/home" PATH="$run/prefix/bin:$FARM" \
            APOGEE_HOME="$run/apogee-home" "$REAL_ZSH" -f -c '
                compadd() { [[ $1 == -- ]] && shift; print -l -- "$@"; }
                _files() { print -- "<files>"; }
                _message() { :; }
                compdef() { :; }
                fpath=("$HOME/.zfunc" $fpath)
                autoload -Uz _apogee
                words=(apogee "")
                CURRENT=2
                _apogee' </dev/null 2>&1 || true)"
        if ! printf '%s\n' "$offered" | grep -qx models; then
            fail "A: 'apogee <TAB>' from ~/.zfunc/_apogee did not offer 'models'; got:" \
                 "$(printf '%s' "$offered" | tr '\n' ' ')"
        fi
    else
        echo "install.sh: no zsh on this host -- the installed stub's completion is not run"
    fi
    uninstalled_clean "$run" "A"

    # --- B: APOGEE_VERSION=0.2.0; nothing usable on zsh's fpath; fish --------
    # ~/.oh-my-zsh/completions is on the fpath but does not exist: not usable.
    run="$WORK/b"
    stub_bin "$run"
    stub_fish "$run"
    stub_zsh "$run" /usr/share/zsh/site-functions "$run/home/.oh-my-zsh/completions"
    install_sh "$run" "$BASH" APOGEE_VERSION=0.2.0
    finished_clean "$run" "B"
    asked_for "$run" "B" "$RELEASES/download/v0.2.0/apogee-$target.tar.gz"
    present "$run/home/.local/share/zsh/site-functions/_apogee" "B (zsh's fallback)"
    present "$run/home/.config/fish/completions/apogee.fish" "B (fish on PATH)"
    absent "$run/home/.oh-my-zsh" "B"
    note_sh="$(zsh_note "$run/out.txt" "$run/home")"
    if [ -z "$note_sh" ] || ! printf '%s\n' "$note_sh" | grep -qxF \
            "        fpath+=~HOME~/.local/share/zsh/site-functions"; then
        fail "B: install.sh did not print the fpath+= note for its fallback:"
        cat "$run/out.txt" >&2
    fi
    uninstalled_clean "$run" "B"

    # --- C: APOGEE_VERSION=v0.2.0; neither zsh nor fish on PATH --------------
    run="$WORK/c"
    stub_bin "$run"
    install_sh "$run" "$BASH" APOGEE_VERSION=v0.2.0
    finished_clean "$run" "C"
    asked_for "$run" "C" "$RELEASES/download/v0.2.0/apogee-$target.tar.gz"
    present "$run/home/.local/share/bash-completion/completions/apogee" "C (bash)"
    for gone in .zfunc .oh-my-zsh .local/share/zsh .config; do
        absent "$run/home/$gone" "C (no zsh or fish on PATH)"
    done
    uninstalled_clean "$run" "C"

    # --- D: a tag that does not exist ----------------------------------------
    # Passed through unchanged, failing on its URL, and the die path cleans up.
    run="$WORK/d"
    stub_bin "$run"
    install_sh "$run" "$BASH" APOGEE_VERSION=v9.9.9 STUB_CURL_404=1
    asked_for "$run" "D" "$RELEASES/download/v9.9.9/apogee-$target.tar.gz"
    if [ "$(status_of "$run")" = 0 ]; then
        fail "D: install.sh exited 0 on a download that failed"
    fi
    if ! grep -qF "could not download $RELEASES/download/v9.9.9/apogee-$target.tar.gz" \
            "$run/err.txt"; then
        fail "D: the failed download's error does not name its URL: $(cat "$run/err.txt")"
    fi
    cleaned_up "$run" "D (a failed download)"
    absent "$run/prefix" "D (nothing downloaded)"

    # --- A under macOS's own bash 3.2, the bash a stock `curl | bash` runs ---
    also=""
    if [ -x /bin/bash ] && [ "$(/bin/bash -c 'echo $BASH_VERSION')" != "$BASH_VERSION" ]; then
        also=", and the first install again under /bin/bash $(/bin/bash -c 'echo $BASH_VERSION')"
        run="$WORK/a-system-bash"
        stub_bin "$run"
        mkdir -p "$run/home/.zfunc"
        stub_zsh "$run" "$run/home/.zfunc"
        install_sh "$run" /bin/bash
        finished_clean "$run" "A (/bin/bash $(/bin/bash -c 'echo $BASH_VERSION'))"
        present "$run/home/.zfunc/_apogee" "A (/bin/bash)"
    fi
    echo "install.sh: $(find "$WORK" -maxdepth 2 -name mktemp.log | grep -c .) runs under" \
         "bash $BASH_VERSION -- three installs and their uninstalls, a failed download$also"
fi

# --- The Makefile's install-completions, under the same stubs -----------------
if [ -n "$MAKE_BIN" ]; then
    # make_completions <run>: the target as `make install` reaches it, with
    # this process's make variables kept out.
    make_completions() {
        local run="$1"
        mkdir -p "$run/home"
        local status=0
        env -i HOME="$run/home" PATH="$run/bin:$FARM" STUB_FPATH="$run/fpath.txt" \
            "$MAKE_BIN" -s --no-print-directory -C "$CLI" install-completions \
            </dev/null >"$run/out.txt" 2>"$run/err.txt" || status=$?
        if [ "$status" != 0 ]; then
            fail "make install-completions exited $status:"
            cat "$run/out.txt" "$run/err.txt" >&2
        fi
    }

    # M1: zsh alone -- no bash, no fish -- and nothing usable on its fpath.
    run="$WORK/m1"
    mkdir -p "$run/bin"
    stub_zsh "$run" /usr/share/zsh/site-functions "$run/home/.oh-my-zsh/completions"
    make_completions "$run"
    present "$run/home/.local/share/zsh/site-functions/_apogee" "make (zsh's fallback)"
    absent "$run/home/.local/share/bash-completion" "make (no bash on PATH)"
    absent "$run/home/.config/fish" "make (no fish on PATH)"
    if ! grep -qxF "installed shell completions (zsh)" "$run/out.txt"; then
        fail "make: the summary does not name only zsh: $(cat "$run/out.txt")"
    fi
    if [ -n "$target" ]; then
        note_make="$(zsh_note "$run/out.txt" "$run/home")"
        if [ -z "$note_make" ] || [ "$note_make" != "$note_sh" ]; then
            fail "the fpath+= note differs: make install says"
            printf '%s\n' "$note_make" >&2
            echo "and install.sh says" >&2
            printf '%s\n' "$note_sh" >&2
        fi
    fi

    # M2: bash, fish and zsh, with ~/.zfunc on zsh's fpath.
    run="$WORK/m2"
    mkdir -p "$run/bin" "$run/home/.zfunc"
    ln -s "$BASH" "$run/bin/bash"
    stub_fish "$run"
    stub_zsh "$run" "$run/home/.zfunc"
    make_completions "$run"
    present "$run/home/.zfunc/_apogee" "make (zsh, on fpath)"
    present "$run/home/.local/share/bash-completion/completions/apogee" "make (bash)"
    present "$run/home/.config/fish/completions/apogee.fish" "make (fish)"
    absent "$run/home/.local/share/zsh" "make (zsh's fallback, with ~/.zfunc on fpath)"
    if ! grep -qxF "installed shell completions (bash, fish, zsh)" "$run/out.txt"; then
        fail "make: the summary does not name bash, fish and zsh: $(cat "$run/out.txt")"
    fi
    echo "make install-completions: ran with zsh alone, and with all three"
else
    echo "make install-completions: no make on this host -- not run"
fi

# --- zsh's three directories: install.sh's, the Makefile's, uninstall's -------
# A curl-run script cannot read the repository, so the list lives twice in
# text; uninstall's is read from the plan the binary prints.
zsh_dirs() {  # zsh_dirs <file> <the first word as it is spelled there>
    { grep -m1 -F "for dir in \"$2/.zfunc\"" "$1" || true; } | { grep -oE '"[^"]+"' || true; } \
        | tr -d '"' | sed 's|\$(HOME)|$HOME|'
}
sh_dirs="$(zsh_dirs "$INSTALL_SH" '$HOME')"
make_dirs="$(zsh_dirs "$MAKEFILE" '$(HOME)')"
if [ "$(printf '%s\n' "$sh_dirs" | grep -c .)" -lt 3 ]; then
    fail "install.sh's zsh directories were not found (read: '$sh_dirs')"
fi
if [ "$sh_dirs" != "$make_dirs" ]; then
    fail "install.sh and the Makefile choose among different zsh directories:" \
         "$(printf '%s' "$sh_dirs" | tr '\n' ' ') vs $(printf '%s' "$make_dirs" | tr '\n' ' ')"
fi
plan="$WORK/plan"
mkdir -p "$plan/home" "$plan/prefix/bin"
cp "$APOGEE_BIN" "$plan/prefix/bin/apogee"
while IFS= read -r dir; do
    [ -n "$dir" ] || continue
    planted="${dir/\$HOME/$plan/home}/_apogee"
    mkdir -p "$(dirname "$planted")"
    echo "#compdef apogee" >"$planted"
done <<<"$sh_dirs"
mkdir -p "$plan/home/.oh-my-zsh/plugins/vscode"
echo "#compdef apogee" >"$plan/home/.oh-my-zsh/plugins/vscode/_apogee"
# No --yes and no terminal: the plan is printed and the removal refused.
env -i HOME="$plan/home" PATH="$FARM" APOGEE_HOME="$plan/apogee-home" \
    "$plan/prefix/bin/apogee" uninstall </dev/null >"$plan/out.txt" 2>&1 || true
while IFS= read -r dir; do
    [ -n "$dir" ] || continue
    planted="${dir/\$HOME/$plan/home}/_apogee"
    if ! grep -qxF "  completion    $planted" "$plan/out.txt"; then
        fail "uninstall's plan does not take $planted, where the installers may put it:"
        cat "$plan/out.txt" >&2
    fi
done <<<"$sh_dirs"
if grep -qF "plugins/vscode" "$plan/out.txt"; then
    fail "uninstall's plan takes a framework's file: $(grep -F plugins/vscode "$plan/out.txt")"
fi
if [ "$(find "$plan/home" -type f | grep -c .)" -ne "$(($(printf '%s\n' "$sh_dirs" | grep -c .) + 1))" ]; then
    fail "uninstall without --yes removed a file: $(find "$plan/home" -type f | tr '\n' ' ')"
fi
echo "zsh directories: $(printf '%s\n' "$sh_dirs" | grep -c .), one set across install.sh," \
     "the Makefile and uninstall"

# --- install.ps1, read -----------------------------------------------------------
# code_lines: its lines that are not comments, numbered.
ps1_code="$(grep -n -v -E '^[[:space:]]*#' "$INSTALL_PS1")"

# The PATH line writes the User scope from the User scope (entry 6), never
# from $env:Path -- the process's machine and user PATH merged -- and then
# puts binDir on this window's PATH too.
set_line="$(printf '%s\n' "$ps1_code" | { grep -F "SetEnvironmentVariable(" || true; } | sed -n 1p)"
if [ -z "$set_line" ]; then
    fail "install.ps1: no SetEnvironmentVariable line to read"
else
    written="${set_line#*"SetEnvironmentVariable("}"
    written="${written%"'User')"*}"
    after="${set_line##*"'User')"}"
    case "$written" in
        *'env:Path'*) fail "install.ps1 writes \$env:Path into the User PATH: $set_line" ;;
    esac
    case "$written" in
        *"GetEnvironmentVariable('Path', 'User')"*) ;;
        *) fail "install.ps1's User PATH line is not built from the User PATH: $set_line" ;;
    esac
    case "$after" in
        *'$env:Path = '*'$binDir;'*) ;;
        *) fail "install.ps1's PATH line does not put binDir on this window's PATH: $set_line" ;;
    esac
fi

# Its body runs in a child scope (entry 7): the first statement after the
# param block opens `& {`, the file ends with its `}`, and every line
# between is inside it.
param_end="$(awk '/^param\(/ { inside = 1 } inside && /^\)/ { print NR; exit }' "$INSTALL_PS1")"
open="$(printf '%s\n' "$ps1_code" | awk -F: -v after="${param_end:-0}" \
    '$1 > after && $0 !~ /^[0-9]+:[[:space:]]*$/ { print; exit }')"
last="$(grep -n -v -E '^[[:space:]]*$' "$INSTALL_PS1" | tail -n 1)"
if [ -z "$param_end" ] || [ "${open#*:}" != "& {" ] || [ "${last#*:}" != "}" ]; then
    fail "install.ps1's body does not run in a child scope: after the param block" \
         "'${open#*:}', and last '${last#*:}' (want '& {' ... '}')"
else
    outside="$(awk -v from="${open%%:*}" -v to="${last%%:*}" \
        'NR > from && NR < to && $0 != "" && $0 !~ /^[[:space:]]/ { print NR": "$0 }' \
        "$INSTALL_PS1")"
    if [ -n "$outside" ]; then
        fail "install.ps1 has statements outside its child scope: $outside"
    fi
fi

# -Version 0.2.0 names the tag v0.2.0 (entry 5): the URL never takes the
# parameter as given.
if printf '%s\n' "$ps1_code" | grep -qF 'releases/download/$Version/'; then
    fail "install.ps1 puts -Version into the URL unnormalised"
fi
if ! printf '%s\n' "$ps1_code" | grep -qF '"v$Version"'; then
    fail "install.ps1 does not give a version without its v one"
fi

# And the setting is documented where a user meets it.
if ! grep -qE '^#[[:space:]]+APOGEE_VERSION=' "$INSTALL_SH"; then
    fail "install.sh's header does not document APOGEE_VERSION"
fi
if ! grep -qF 'APOGEE_VERSION' "$README" || ! grep -qF -- '-Version' "$README"; then
    fail "README.md does not name APOGEE_VERSION and -Version"
fi
echo "install.ps1: its PATH line, child scope and version read"

if [ "$failures" -ne 0 ]; then
    echo "$failures install script check(s) failed" >&2
    exit 1
fi
echo "install scripts OK"
