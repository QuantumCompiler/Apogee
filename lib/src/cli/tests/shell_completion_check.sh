#!/usr/bin/env bash
#
# The shell stubs, run in the real shells against the real binary.
#
# The protocol's own tests (`tests/commands/lifecycle_test.cpp`) prove what
# `apogee __complete` answers. They cannot see what a stub SENDS it, and that
# is where two of the bugs lived that made nested verbs never complete in zsh:
#
#   - an unquoted `${words[2,CURRENT]}` drops the empty word under the cursor,
#     so `apogee models <TAB>` arrived as `__complete models` -- "still typing
#     models" -- and completed to `models` again;
#   - the file is autoloaded from fpath as the BODY of `_apogee`, so a body that
#     only defined the function and called `compdef` completed nothing on the
#     first <TAB> of every shell.
#
# Each shell present on the host is checked; bash always is, since this script
# runs under it. A missing zsh or fish is reported, not failed -- CI runners do
# not all carry them, and this is a developer gate (`make test`).
set -euo pipefail

binary="$1"
work="$2"
stubs="$(cd "$(dirname "$0")/../completions" && pwd)"

rm -rf "$work"
mkdir -p "$work/bin" "$work/home"
ln -s "$binary" "$work/bin/apogee"
export PATH="$work/bin:$PATH"
export APOGEE_HOME="$work/home"
unset APOGEE_CONFIG

apogee config init </dev/null >/dev/null
apogee config add-backend claude --type anthropic --model claude-sonnet-5 </dev/null >/dev/null
apogee config add-backend local --type llamacpp --model-path /tmp/x.gguf </dev/null >/dev/null

failures=0
fail() {
    echo "FAIL: $*" >&2
    failures=$((failures + 1))
}

# expect <shell> <output> <must-contain> <line typed>
expect() {
    local shell="$1" output="$2" needle="$3" line="$4"
    if ! printf '%s\n' "$output" | grep -qx -- "$needle"; then
        fail "$shell: 'apogee $line<TAB>' did not offer '$needle'; got: $(printf '%s' "$output" | tr '\n' ' ')"
    fi
}

# Each case: the words after `apogee`, the last one being the word under the
# cursor (empty for a fresh <TAB>), and one candidate that must be offered.
cases=(
    "models||pull"
    "train|pipeline||resume"
    "config||add-graph"
    "kn||capture"
    "config|set-default||claude"
    "train|eval|r1|--judge||local"
    "models|pull|--s|--safetensors"
    "|models"
    "config|add-backend|x|--type||anthropic"
    "config|add-backend|x|-t|cl|claude-cli"
)

# ---- bash -------------------------------------------------------------------
bash_complete() {
    bash --norc --noprofile -c '
        source "$1"; shift
        COMP_WORDS=(apogee "$@")
        COMP_CWORD=$(( ${#COMP_WORDS[@]} - 1 ))
        _apogee
        printf "%s\n" "${COMPREPLY[@]}"
    ' _ "$stubs/apogee.bash" "$@"
}
for case in "${cases[@]}"; do
    IFS='|' read -r -a parts <<<"$case|"
    words=("${parts[@]:0:${#parts[@]}-1}")
    needle="${parts[${#parts[@]}-1]}"
    expect bash "$(bash_complete "${words[@]}")" "$needle" "${words[*]}"
done
echo "bash: checked ${#cases[@]} lines"

# ---- zsh --------------------------------------------------------------------
# compadd and _files only exist inside a completion widget, so they are stood
# in for: each prints what it was handed. Both ways the file is loaded are
# checked -- autoloaded from fpath (what both installers do), then sourced.
if command -v zsh >/dev/null 2>&1; then
    zsh_complete() {
        local mode="$1"
        shift
        zsh -f -c '
            compadd() { [[ $1 == -- ]] && shift; print -l -- "$@"; }
            _files() { print -- "<files>"; }
            # As the real one reads it: the word after -r IS the message.
            _message() { [[ $1 == -r ]] && shift; print -r -- "<message> $1"; }
            compdef() { print -- "<compdef>"; }
            mode=$1; stubs=$2; shift 2
            if [[ $mode == fpath ]]; then
                fpath=("$stubs" $fpath)
                autoload -Uz _apogee
            else
                source "$stubs/_apogee"
            fi
            words=(apogee "$@")
            CURRENT=${#words}
            _apogee
        ' _ "$mode" "$stubs" "$@"
    }
    for mode in fpath source; do
        for case in "${cases[@]}"; do
            IFS='|' read -r -a parts <<<"$case|"
            words=("${parts[@]:0:${#parts[@]}-1}")
            needle="${parts[${#parts[@]}-1]}"
            # A fresh shell per line, so every one is a FIRST <TAB>.
            expect "zsh ($mode)" "$(zsh_complete "$mode" "${words[@]}")" "$needle" "${words[*]}"
        done
    done
    # Free text: the shell is told what the word is, and is not handed the
    # working directory's file names -- the TAB that "stopped working" at
    # `config add-backend --model na`.
    for mode in fpath source; do
        output="$(zsh_complete "$mode" config add-backend --model na)"
        expect "zsh ($mode)" "$output" "<message> --model TEXT: Model name" "config add-backend --model na"
        if printf '%s\n' "$output" | grep -qx -- "<files>"; then
            fail "zsh ($mode): 'apogee config add-backend --model na<TAB>' offered file names for free text"
        fi
        # A path still gets the shell's file completion.
        expect "zsh ($mode)" "$(zsh_complete "$mode" config add-backend x --model-path "")" "<files>" \
            "config add-backend x --model-path "
    done
    echo "zsh: checked ${#cases[@]} lines, autoloaded and sourced, plus hints and paths"
else
    echo "zsh: not installed, skipped"
fi

# ---- fish -------------------------------------------------------------------
if command -v fish >/dev/null 2>&1; then
    for case in "${cases[@]}"; do
        IFS='|' read -r -a parts <<<"$case|"
        words=("${parts[@]:0:${#parts[@]}-1}")
        needle="${parts[${#parts[@]}-1]}"
        line="apogee ${words[*]}"
        output="$(fish --no-config -c 'source $argv[1]; complete -C $argv[2]' "$stubs/apogee.fish" "$line" | cut -f1)"
        expect fish "$output" "$needle" "${words[*]}"
    done
    echo "fish: checked ${#cases[@]} lines"
else
    echo "fish: not installed, skipped"
fi

if [ "$failures" -ne 0 ]; then
    echo "$failures completion check(s) failed" >&2
    exit 1
fi
echo "shell completion OK"
