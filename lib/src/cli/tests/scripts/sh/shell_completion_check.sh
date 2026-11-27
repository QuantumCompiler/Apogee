#!/usr/bin/env bash
#
# The shell stubs, run in the real shells against the real binary.
#
# The protocol's own tests (`tests/presentation/cli/lifecycle_test.cpp`) prove what
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
# runs under it -- PATH's bash, and /bin/bash too where it is another version:
# macOS's own is 3.2, which joins a quoted "${a[@]:n}" into one word when IFS
# holds no space, so the stub once sent the whole line as one word there
# (CLI-M41). A missing zsh or fish is reported, not failed -- CI runners do not
# all carry them, and this is a developer gate (`make test`).
#
# Then the real line editors (CLI-M42): zsh's compsys and every bash's
# readline, driven as at a terminal through zsh's zpty, over quoted lines --
# the argv each stub sends and the line each Tab leaves, both checked. The
# stand-ins cannot see quoting: a stub that forwarded the words as typed sent
# `'cl` for a word inside an open quote, and the protocol, which matches
# words literally, offered nothing.
set -euo pipefail

binary="$1"
work="$2"
stubs="$(cd "$(dirname "$0")/../../../completions" && pwd)"

. "$(dirname "$0")/sandbox.sh"
work="$(sandbox_dir "$work")"
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/bin" "$work/home"
ln -s "$binary" "$work/bin/apogee"
export PATH="$work/bin:$PATH"
export APOGEE_HOME="$work/home"
unset APOGEE_CONFIG

apogee config init </dev/null >/dev/null
apogee config add-backend claude --type anthropic --model claude-sonnet-5 </dev/null >/dev/null
apogee config add-backend local --type llamacpp --model-path /tmp/x.gguf </dev/null >/dev/null

# Names that exist: a collection, and a model in a stand-in Ollama store --
# whose ref carries the `:` bash splits words at.
printf 'a note about completion\n' >"$work/note.txt"
apogee embed ingest notes "$work/note.txt" --retriever lexical </dev/null >/dev/null 2>&1
export OLLAMA_MODELS="$work/ollama"
mkdir -p "$OLLAMA_MODELS/manifests/registry.ollama.ai/library/llama3.2" "$OLLAMA_MODELS/blobs"
printf '{"layers":[{"mediaType":"application/vnd.ollama.image.model","digest":"sha256:ab12","size":1}]}' \
    >"$OLLAMA_MODELS/manifests/registry.ollama.ai/library/llama3.2/3b"
printf 'x' >"$OLLAMA_MODELS/blobs/sha256-ab12"

# The model store (M7): one GGUF a backend points at, one no backend does.
# Not real GGUFs -- completion reads directories and the config, never a header.
gguf="$APOGEE_HOME/models/org--m/gguf"
mkdir -p "$gguf/111111111111" "$gguf/222222222222"
printf 'x' >"$gguf/111111111111/m-F16.gguf"
printf 'x' >"$gguf/222222222222/m-Q4_K_M.gguf"
apogee config add-backend m-f16 --type llamacpp --model-path "$gguf/111111111111/m-F16.gguf" \
    </dev/null >/dev/null
# The last scan (M33): what `providers register` offers is read from the
# provider cache, never a scan -- written here as a scan leaves it.
mkdir -p "$APOGEE_HOME/cache"
printf '%s' '{"schema":1,"scanned_at":"2026-10-26T12:00:00Z","providers":{"claude":{"type":"claude-cli","installed":true,"credentials":"found"},"codex":{"type":"codex-cli","installed":false}}}' \
    >"$APOGEE_HOME/cache/providers.json"
apogee config add-allowed-host docs.python.org </dev/null >/dev/null

# The quoted lines' own home (CLI-M42): one backend `cl` picks out (`claudy`,
# beside `local`), a collection with a space -- copied in, since `embed ingest`
# half-makes one (CLI-M46) -- a GGUF for `--from`, and allowed hosts a bare
# line would split or glob, hand-written as `add-allowed-host` refuses them.
le="$work/le"
le_home="$le/home"
mkdir -p "$le/bin" "$le_home" "$le/user" "$le/empty" "$le/globs" "$le/files/docs"
: >"$le/inputrc"
: >"$le/globs/x.example.com"
: >"$le/files/docs/a note.txt"
APOGEE_HOME="$le_home" apogee config init </dev/null >/dev/null
APOGEE_HOME="$le_home" apogee config add-backend claudy --type mock --model mock-1 </dev/null >/dev/null
APOGEE_HOME="$le_home" apogee config add-backend local --type mock --model mock-2 </dev/null >/dev/null
mkdir -p "$le_home/embeddings" "$le_home/models/org--m/gguf/222222222222"
cp "$APOGEE_HOME/embeddings/notes.db" "$le_home/embeddings/my notes.db"
printf 'x' >"$le_home/models/org--m/gguf/222222222222/m-Q4_K_M.gguf"
sed -e 's|"allowed_hosts": \[\]|"allowed_hosts": ["*.example.com", "https://x.test/a?b=1\&c=2"]|' \
    "$le_home/config/config.json" >"$le/config.json"
mv "$le/config.json" "$le_home/config/config.json"
grep -q 'x.test/a?b=1&c=2' "$le_home/config/config.json"

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
    # M30: the named specs' reads, offered the day they exist.
    "train|pipeline||show"
    "config||add-graph"
    "kn||capture"
    "config|set-default||claude"
    "train|eval|r1|--judge||local"
    "models|pull|--s|--safetensors"
    "|models"
    "config|add-backend|x|--type||anthropic"
    "config|add-backend|x|-t|cl|claude-cli"
    # Names read from the config and the data directory.
    "embed|query||notes"
    "complete|--rag||notes"
    "config|get|backends.cl|backends.claude"
    "datasets|synth|x|--kit||reasoning"
    # 53b: a flag is offered the day it exists.
    "train|promote|r1|--ad|--adapter"
    "models|pull|ll|llama3.2:3b"
    # Words a command validates itself, offered from the same list.
    "knowledge|query|q|--status||rejected"
    "complete|--retriever|h|hybrid"
    # M7: a backend whose model is stored is something to delete, and so is
    # the store's own name for it; a stored GGUF no backend has is a name
    # add-backend fills itself from.
    "models|delete|m-|m-f16"
    "models|delete|org--m/gguf/2|org--m/gguf/222222222222"
    "config|add-backend||m-Q4_K_M"
    # M33: the one-provider verb and what the last scan found to register,
    # and the allowed hosts as a key config get reads.
    "providers||register"
    "providers|register||claude"
    "config|get|tools.al|tools.allowed_hosts"
)

# ---- bash -------------------------------------------------------------------
# Every bash the host has, each once by version: PATH's, then /bin/bash where
# it is another (3.2 on a Mac whose PATH finds Homebrew's first).
bashes=("$(command -v bash)")
if [ -x /bin/bash ] && [ "$(/bin/bash -c 'echo $BASH_VERSION')" != "$(bash -c 'echo $BASH_VERSION')" ]; then
    bashes+=(/bin/bash)
fi
# bash_complete <bash> <words...>
bash_complete() {
    local shell="$1"
    shift
    "$shell" --norc --noprofile -c '
        source "$1"; shift
        # As bash hands them over: the raw line to the cursor, and words
        # split at COMP_WORDBREAKS -- which the stub must not trust.
        COMP_LINE="apogee $*"
        COMP_POINT=${#COMP_LINE}
        COMP_WORDS=(apogee)
        for word in "$@"; do
            while [[ $word == *:* ]]; do
                COMP_WORDS+=("${word%%:*}" ":")
                word=${word#*:}
            done
            COMP_WORDS+=("$word")
        done
        COMP_CWORD=$(( ${#COMP_WORDS[@]} - 1 ))
        _apogee
        printf "%s\n" "${COMPREPLY[@]}"
    ' _ "$stubs/apogee.bash" "$@"
}
versions=()
for shell in "${bashes[@]}"; do
    version="$("$shell" -c 'echo "${BASH_VERSINFO[0]}.${BASH_VERSINFO[1]}.${BASH_VERSINFO[2]}"')"
    versions+=("$version")
    for case in "${cases[@]}"; do
        IFS='|' read -r -a parts <<<"$case|"
        words=("${parts[@]:0:${#parts[@]}-1}")
        needle="${parts[${#parts[@]}-1]}"
        expect "bash $version" "$(bash_complete "$shell" "${words[@]}")" "$needle" "${words[*]}"
    done
    # Past a `:`, bash replaces only what follows it: the candidate is the rest.
    expect "bash $version" "$(bash_complete "$shell" models pull llama3.2:)" "3b" "models pull llama3.2:"
    # Two top-level candidates are two words, never one line of both.
    output="$(bash_complete "$shell" c)"
    expect "bash $version" "$output" "chat" "c"
    expect "bash $version" "$output" "config" "c"
done
under="${versions[0]}"
if [ "${#versions[@]}" -gt 1 ]; then
    under="$under and ${versions[1]}"
fi
echo "bash: checked ${#cases[@]} lines, plus a word-break and two candidates, under $under"

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
            # As compsys sets them: the word under the cursor, with no quote
            # open -- which the stub reads in place of words[CURRENT].
            PREFIX=$words[CURRENT] SUFFIX=
            typeset -A compstate
            _apogee
        ' _ "$mode" "$stubs" "$@"
    }
    for mode in fpath source; do
        for case in "${cases[@]}"; do
            IFS='|' read -r -a parts <<<"$case|"
            words=("${parts[@]:0:${#parts[@]}-1}")
            needle="${parts[${#parts[@]}-1]}"
            # A fresh shell per line, so every one is a FIRST <TAB>.
            output="$(zsh_complete "$mode" "${words[@]}")"
            expect "zsh ($mode)" "$output" "$needle" "${words[*]}"
            # The word being typed was the word sent: every name offered
            # starts with it. A stub that lost it offers every name there is,
            # the needle among them.
            partial="${words[${#words[@]}-1]}"
            if [ -n "$partial" ]; then
                strays=()
                while IFS= read -r offered; do
                    case "$offered" in
                    "" | "<"* | "$partial"*) ;;
                    *) strays+=("$offered") ;;
                    esac
                done <<<"$output"
                if [ "${#strays[@]}" -gt 0 ]; then
                    fail "zsh ($mode): 'apogee ${words[*]}<TAB>' did not complete '$partial': offered ${strays[*]}"
                fi
            fi
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
        # A name-or-path word falls back to files once no name matches.
        expect "zsh ($mode)" "$(zsh_complete "$mode" datasets synth x --kit ./)" "<files>" \
            "datasets synth x --kit ./"
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
    # CLI-M42: quoted words reach the binary unquoted, in the quoted lines'
    # home, and a path inside an open quote still completes.
    fish_quoted=(
        "apogee 'models' pu|pull"
        "apogee chat --system 'Be brief' -m cl|claudy"
        "apogee chat -m 'cl|claudy"
        "apogee embed query my|my notes"
        "apogee embed query 'my no|my notes"
        "apogee chat --attach 'docs/a no|docs/a note.txt"
    )
    for case in "${fish_quoted[@]}"; do
        line="${case%|*}"
        output="$(cd "$le/files" && APOGEE_HOME="$le_home" fish --no-config \
            -c 'source $argv[1]; complete -C $argv[2]' "$stubs/apogee.fish" "$line" | cut -f1)"
        expect fish "$output" "${case##*|}" "${line#apogee }"
    done
    echo "fish: checked ${#cases[@]} lines, plus ${#fish_quoted[@]} quoted"
else
    echo "fish: not installed, skipped"
fi

# ---- the real line editors (CLI-M42) ----------------------------------------
# Each quoted line typed into a real shell at a terminal, then Tab: zsh -f with
# compinit, autoloading the stub from fpath as the installers place it, and
# every bash above, the stub sourced. zsh's zpty is the terminal. A logging
# `apogee` first on PATH records the argv each __complete is sent; a zsh widget
# writes $BUFFER, and bash -- 3.2 has no READLINE_LINE -- accepts the line as a
# comment (insert-comment, M-#), so nothing on it runs, and writes it out of
# its history. A fresh shell per line, each read under a deadline.
cat >"$le/bin/apogee" <<'SH'
#!/bin/sh
# Each __complete's words, one bracket a word, on a line of their own.
if [ "$1" = __complete ]; then
    shift
    line=
    for word in "$@"; do line="$line[$word]"; done
    printf '%s\n' "$line" >>"$APOGEE_LE_ARGV"
    exec "$APOGEE_LE_BINARY" __complete "$@"
fi
exec "$APOGEE_LE_BINARY" "$@"
SH
chmod +x "$le/bin/apogee"
cat >"$le/drive.zsh" <<'ZSH'
# drive.zsh <zsh|bash> <shell> <stubs> <buffer log> <line>: types <line> and a
# Tab into <shell> at a terminal and prints the line the Tab left.
zmodload zsh/zpty zsh/zselect || exit 2
kind=$1 shell=$2 stubs=$3 log=$4 line=$5
typeset -F SECONDS
seen=
drain() { local chunk; while zpty -r -t T chunk 2>/dev/null; do seen+=$chunk; done }
# Until the condition holds or the deadline passes, reading all the while: a
# pty nobody reads fills, and the shell blocks writing to it.
wait_for() {
    local deadline=$((SECONDS + $1))
    shift
    while ((SECONDS < deadline)); do
        drain
        "$@" && return 0
        zselect -t 5
    done
    drain
    "$@"
}
ready() { [[ $seen == *READY* ]] }
logged() { [[ -s $log ]] }
case $kind in
zsh)
    zpty -b T "${(q)shell} -f -i" || exit 2
    zpty -w T " stty cols 200 rows 50; PS1='> '; fpath=(${(q)stubs} \$fpath);"\
" autoload -Uz compinit; compinit -u -D; __log() { print -r -- \"\$BUFFER\" >|${(q)log}; };"\
" zle -N __log; bindkey '^X^L' __log; print RE''ADY"
    keys=$'\t\C-x\C-l'
    ;;
bash)
    # No HISTFILE, so nothing writes the history but the prompt's own
    # `history -w`; the setup line, starting with a blank, is not kept; and
    # none of the caller's history settings (a timestamp line, an ignored
    # comment) reach the shell.
    zpty -b T "unset SHELLOPTS BASHOPTS HISTIGNORE HISTTIMEFORMAT;"\
" HISTFILE= HISTCONTROL=ignorespace ${(q)shell} --norc --noprofile -i" || exit 2
    zpty -w T " stty cols 200 rows 50; PS1='> '; set +H; PROMPT_COMMAND='history -w '${(q)log};"\
" source ${(q)stubs}/apogee.bash; echo RE''ADY"
    keys=$'\t\e#'
    ;;
esac
if ! wait_for 30 ready; then
    print -r -- "no prompt within 30 s; the terminal showed: ${(q+)seen}" >&2
    zpty -d T
    exit 3
fi
zpty -w -n T "$line$keys"
if ! wait_for 30 logged; then
    print -r -- "no line within 30 s; the terminal showed: ${(q+)seen}" >&2
    zpty -d T
    exit 3
fi
buffer=$(<$log)
zpty -d T
[[ $kind == bash ]] && buffer=${buffer#\#}
print -r -- "$buffer"
ZSH

# le_row <shells> <folder> <line> <argv> <line after Tab>
le_shells=() le_cwd=() le_lines=() le_argv=() le_after=()
le_row() {
    le_shells+=("$1")
    le_cwd+=("$2")
    le_lines+=("$3")
    le_argv+=("$4")
    le_after+=("$5")
}
le_row "zsh bash" empty "apogee 'models' pu" '[models][pu]' "apogee 'models' pull "
le_row "zsh bash" empty 'apogee "models" pu' '[models][pu]' 'apogee "models" pull '
le_row "zsh bash" empty "apogee chat --system 'Be brief' -m cl" '[chat][--system][Be brief][-m][cl]' \
    "apogee chat --system 'Be brief' -m claudy "
le_row "zsh bash" empty "apogee chat -m 'cl" '[chat][-m][cl]' "apogee chat -m 'claudy' "
le_row "zsh bash" empty 'apogee chat -m "cl' '[chat][-m][cl]' 'apogee chat -m "claudy" '
le_row "zsh bash" empty "apogee models quantize 'org--m' --from 2" '[models][quantize][org--m][--from][2]' \
    "apogee models quantize 'org--m' --from 222222222222 "
le_row "zsh bash" empty 'apogee embed query my\ notes ' '[embed][query][my notes][]' \
    'apogee embed query my\ notes '
le_row "zsh bash" empty 'apogee embed query "my notes" ' '[embed][query][my notes][]' \
    'apogee embed query "my notes" '
le_row "zsh bash" empty 'apogee embed query my' '[embed][query][my]' 'apogee embed query my\ notes '
le_row "zsh bash" empty "apogee embed query 'my no" '[embed][query][my no]' "apogee embed query 'my notes' "
le_row "zsh bash" empty 'apogee chat --system a\\b -m cl' '[chat][--system][a\b][-m][cl]' \
    'apogee chat --system a\\b -m claudy '
le_row "zsh bash" empty 'apogee chat -m "a\"b' '[chat][-m][a"b]' 'apogee chat -m "a\"b'
le_row "zsh bash" empty "apogee chat --system \"\$(touch $le/EVALUATED)\" -m cl" \
    "[chat][--system][\$(touch $le/EVALUATED)][-m][cl]" \
    "apogee chat --system \"\$(touch $le/EVALUATED)\" -m claudy "
le_row "zsh bash" empty 'apogee task run "Write the report" --agent co' \
    '[task][run][Write the report][--agent][co]' 'apogee task run "Write the report" --agent code-review '
le_row bash empty 'apogee models pull llama3.2:' '[models][pull][llama3.2:]' 'apogee models pull llama3.2:3b '
le_row bash empty "apogee models pull 'llama3.2:" '[models][pull][llama3.2:]' "apogee models pull 'llama3.2:3b' "
# readline's word is all of `llama3.2\:`: the typed word kept, never `3b`.
le_row bash empty 'apogee models pull llama3.2\:' '[models][pull][llama3.2:]' 'apogee models pull llama3.2:3b '
le_row "zsh bash" empty 'apogee config delete-allowed-host ht' '[config][delete-allowed-host][ht]' \
    'apogee config delete-allowed-host https://x.test/a\?b=1\&c=2 '
# A path is the shell's own file completion, inside a quote or escaped.
le_row "zsh bash" files "apogee chat --attach 'docs/a no" '[chat][--attach][docs/a no]' \
    "apogee chat --attach 'docs/a note.txt' "
le_row "zsh bash" files 'apogee chat --attach docs/a\ no' '[chat][--attach][docs/a no]' \
    'apogee chat --attach docs/a\ note.txt '
# The folder holds x.example.com, which an unquoted `*.example.com` would match.
le_row "zsh bash" globs 'apogee config delete-allowed-host *.ex' '[config][delete-allowed-host][*.ex]' \
    'apogee config delete-allowed-host \*.example.com '

if command -v zsh >/dev/null 2>&1 && zsh -fc 'zmodload zsh/zpty' 2>/dev/null; then
    editors=("zsh $(command -v zsh)")
    for shell in "${bashes[@]}"; do
        editors+=("bash $shell")
    done
    checked=0
    for editor in "${editors[@]}"; do
        kind="${editor%% *}"
        shell="${editor#* }"
        if [ "$kind" = zsh ]; then
            name="zsh $(zsh -fc 'echo $ZSH_VERSION')"
        else
            name="bash $("$shell" -c 'echo "${BASH_VERSINFO[0]}.${BASH_VERSINFO[1]}.${BASH_VERSINFO[2]}"')"
        fi
        for ((row = 0; row < ${#le_lines[@]}; row++)); do
            case " ${le_shells[row]} " in
            *" $kind "*) ;;
            *) continue ;;
            esac
            line="${le_lines[row]}"
            rm -f "$le/argv" "$le/buffer" "$le/EVALUATED"
            if ! after="$(cd "$le/${le_cwd[row]}" &&
                env HOME="$le/user" INPUTRC="$le/inputrc" TERM=xterm APOGEE_HOME="$le_home" \
                    PATH="$le/bin:$PATH" APOGEE_LE_ARGV="$le/argv" APOGEE_LE_BINARY="$binary" \
                    BASH_SILENCE_DEPRECATION_WARNING=1 \
                    zsh -f "$le/drive.zsh" "$kind" "$shell" "$stubs" "$le/buffer" "$line" 2>&1)"; then
                fail "$name: '$line<TAB>' at a terminal: $after"
                continue
            fi
            checked=$((checked + 1))
            sent="$(cat "$le/argv" 2>/dev/null || :)"
            if [ -z "$sent" ] || [ -n "$(printf '%s\n' "$sent" | grep -vxF -- "${le_argv[row]}")" ]; then
                fail "$name: '$line<TAB>' sent $(printf '%s' "$sent" | tr '\n' ' '), not ${le_argv[row]}"
            fi
            if [ "$after" != "${le_after[row]}" ]; then
                fail "$name: '$line<TAB>' left the line as '$after', not '${le_after[row]}'"
            fi
            if [ -e "$le/EVALUATED" ]; then
                fail "$name: '$line<TAB>' ran the substitution on its line"
            fi
        done
    done
    echo "real line editors: checked $checked quoted lines under ${#editors[@]} shells"
else
    echo "real line editors: no zsh with zpty, skipped"
fi

if [ "$failures" -ne 0 ]; then
    echo "$failures completion check(s) failed" >&2
    exit 1
fi
echo "shell completion OK"
