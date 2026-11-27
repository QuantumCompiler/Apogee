# Bash completion for apogee.
#
# Deliberately tiny: every candidate comes from `apogee __complete`, so this
# file never needs regenerating when a command or a backend is added. A
# generated static completion cannot know the user's configured backend names,
# collections, chats or models, which are the things worth completing here.
#
# The protocol matches words literally, so the stub sends each word as the
# command will get it -- one level of the shell's quoting removed, nothing
# expanded -- and inserts each candidate quoted as the shell will read it.

# The words of a typed line as bash will hand them to the command, with no
# eval and nothing expanded (`$(...)`, `$VAR`, `~` and globs stay as typed):
# unquoted blanks split words; '...' is literal; "..." unescapes \" \\ \$ and
# \`; $'...' unescapes \' and \\ and keeps any other escape; a backslash
# outside quotes takes the next character. The rules of the binary's own
# splitter (`line_tokens`), plus a quote left open. Into the caller's
# `words`, the last being the word under the cursor (empty after a blank);
# that word's open quote (', " or $') in `quote`, its typed text in `raw`, and
# in `brk` how much of `raw` comes before its last unquoted `:`.
__apogee_words() {
    local text=$1 word= begun= c next
    local -i i=0 n=${#1}
    words=()
    quote=
    raw=
    brk=0
    while ((i < n)); do
        c=${text:i:1}
        next=${text:i+1:1}
        i=$((i + 1))
        if [[ -z $begun && -z $quote ]] && [[ $c == [[:blank:]] || $c == $'\n' ]]; then
            continue
        fi
        begun=1
        raw=$raw$c
        case $quote in
        \')
            if [[ $c == \' ]]; then quote=; else word=$word$c; fi
            ;;
        \")
            if [[ $c == \" ]]; then
                quote=
            elif [[ $c == \\ && -n $next && $next == [\"\\\$\`] ]]; then
                word=$word$next
                raw=$raw$next
                i=$((i + 1))
            else
                word=$word$c
            fi
            ;;
        \$\')
            if [[ $c == \' ]]; then
                quote=
            elif [[ $c == \\ && -n $next && $next == [\'\\] ]]; then
                word=$word$next
                raw=$raw$next
                i=$((i + 1))
            else
                word=$word$c
            fi
            ;;
        *)
            if [[ $c == [[:blank:]] || $c == $'\n' ]]; then
                words+=("$word")
                word=
                begun=
                raw=
                brk=0
            elif [[ $c == \' || $c == \" ]]; then
                quote=$c
            elif [[ $c == \$ && $next == \' ]]; then
                quote=\$\'
                raw=$raw$next
                i=$((i + 1))
            elif [[ $c == \\ ]]; then
                if [[ -n $next ]]; then
                    word=$word$next
                    raw=$raw$next
                    i=$((i + 1))
                fi
            else
                word=$word$c
                if [[ $c == : ]]; then brk=${#raw}; fi
            fi
            ;;
        esac
    done
    words+=("$word")
}

# The value of a word's typed text: its last word, into the caller's `kept`.
__apogee_value() {
    local -a words
    local quote raw brk
    __apogee_words "$1"
    kept=${words[${#words[@]}-1]}
}

_apogee() {
    # A default IFS for the whole function, whatever the caller's shell has
    # set: bash 3.2 (macOS's /bin/bash) joins a quoted "${a[@]:n}" or
    # "${a[@]#x}" -- any array expansion with an operator -- into ONE word
    # when IFS holds no space, so `apogee models <TAB>` once sent `models `.
    local IFS=$' \t\n'
    # The words, from the line itself, up to the cursor: bash splits
    # COMP_WORDS at every character in COMP_WORDBREAKS, `:` included, so
    # `models pull llama3:8b` would arrive as three words, and it keeps the
    # quoting -- `'my notes'` -- which the protocol would take literally. (No
    # COMP_LINE -- a caller driving the function by hand -- falls back to
    # COMP_WORDS.)
    local -a words=() sent=()
    local quote= raw= brk=0
    local -i k
    if [[ -n ${COMP_LINE-} ]]; then
        __apogee_words "${COMP_LINE:0:${COMP_POINT:-${#COMP_LINE}}}"
    else
        for ((k = 0; k <= ${COMP_CWORD:-0}; k++)); do
            words+=("${COMP_WORDS[k]-}")
        done
        raw=${words[${#words[@]}-1]}
    fi
    # Built by appending, never by a quoted slice (see IFS above): every
    # word after the command's own.
    for ((k = 1; k < ${#words[@]}; k++)); do
        sent+=("${words[k]}")
    done
    ((${#sent[@]})) || sent=("")

    # One candidate a line, split on newlines alone and taken as it stands:
    # an unquoted $(...) would also match each line against the folder. No
    # status escapes: under a caller's `set -e`, a failed binary or a missing
    # compopt would close the shell.
    local out
    out=$(APOGEE_COMPLETION_PROTOCOL=2 apogee __complete "${sent[@]}" 2>/dev/null) || :
    local -a lines=()
    IFS=$'\n' read -r -d '' -a lines <<<"$out" || :
    # The first line says what the rest mean; a binary too old to send one
    # sends bare candidates, or nothing -- which leaves file completion to
    # `-o default`, as before.
    local directive=:files
    if [[ ${lines[0]-} == :* ]]; then
        directive=${lines[0]}
        lines=("${lines[@]:1}")
    elif ((${#lines[@]})); then
        directive=:values
    fi
    COMPREPLY=()
    if [[ $directive == :values ]]; then
        # Readline replaces only its own word, `$2`: what follows the typed
        # word's last unquoted `:` (a COMP_WORDBREAKS character), or all of
        # it inside the quote it opened. So a candidate drops what the typed
        # word holds before `$2` -- `llama3:8b` is offered as `8b` once
        # `llama3:` is typed -- and nothing when `$2` is the whole word, as
        # for an escaped `llama3\:`. A whole word replaced is replaced quote
        # and all, so its candidate lands bare: readline reads `$'it\'s` as a
        # quote already closed. (A caller driving the function by hand passes
        # no `$2`: readline's word is then taken to start past the typed
        # word's last unquoted `:`.)
        local replaced=$raw kept=
        if (($# >= 2)); then
            replaced=$2
            if [[ $replaced == "$raw" ]]; then quote=; fi
        elif [[ ${COMP_WORDBREAKS-} == *:* ]]; then
            replaced=${raw:brk}
        fi
        if ((${#replaced} < ${#raw})) && [[ ${raw:${#raw}-${#replaced}} == "$replaced" ]]; then
            __apogee_value "${raw:0:${#raw}-${#replaced}}"
        fi
        # Each candidate quoted for where it lands: inside the quote the
        # typed word opened, which readline then closes, or bare -- escaped
        # only when it holds a character the shell would read as syntax
        # (`%q` escapes `,`, which a comma list must keep).
        local candidate sq="'\\''" bs='\'
        for ((k = 0; k < ${#lines[@]}; k++)); do
            candidate=${lines[k]#"$kept"}
            case $quote in
            \') candidate=${candidate//\'/$sq} ;;
            \")
                candidate=${candidate//"$bs"/"$bs$bs"}
                candidate=${candidate//\"/\\\"}
                candidate=${candidate//\$/\\\$}
                candidate=${candidate//\`/\\\`}
                ;;
            \$\')
                candidate=${candidate//"$bs"/"$bs$bs"}
                candidate=${candidate//\'/\\\'}
                ;;
            *)
                if [[ $candidate == *[![:alnum:]._/:=,@%+-]* ]]; then
                    printf -v candidate %q "$candidate"
                fi
                # bash 3.2's %q leaves a leading `~`, which would expand.
                if [[ $candidate == \~* ]]; then candidate=\\$candidate; fi
                ;;
            esac
            COMPREPLY+=("$candidate")
        done
    fi
    # Anything but a path must not fall back to file names. bash cannot show
    # a hint, and bash 3.2 (macOS's /bin/bash) has no compopt, where the
    # fallback stays.
    if [[ $directive != :files ]] && ((${#COMPREPLY[@]} == 0)); then
        compopt +o default 2>/dev/null || :
    fi
}
complete -o default -F _apogee apogee
