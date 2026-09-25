# Bash completion for apogee.
#
# Deliberately tiny: every candidate comes from `apogee __complete`, so this
# file never needs regenerating when a command or a backend is added. A
# generated static completion cannot know the user's configured backend names,
# collections, chats or models, which are the things worth completing here.
_apogee() {
    local IFS=$'\n'
    # The words, from the line itself: bash splits COMP_WORDS at every
    # character in COMP_WORDBREAKS, `:` included, so `models pull llama3:8b`
    # would arrive as three words and the walk would count positionals wrong.
    # Split on whitespace only, up to the cursor. (No COMP_LINE -- a caller
    # driving the function by hand -- falls back to COMP_WORDS.)
    local -a words
    if [[ -n ${COMP_LINE-} ]]; then
        local line=${COMP_LINE:0:${COMP_POINT:-${#COMP_LINE}}}
        IFS=$' \t' read -r -a words <<<"$line"
        [[ $line == *[[:space:]] ]] && words+=("")
        words=("${words[@]:1}")
    else
        words=("${COMP_WORDS[@]:1:COMP_CWORD}")
    fi
    ((${#words[@]})) || words=("")
    local current=${words[${#words[@]}-1]}

    local lines=($(APOGEE_COMPLETION_PROTOCOL=2 apogee __complete "${words[@]}" 2>/dev/null))
    # The first line says what the rest mean; a binary too old to send one
    # sends bare candidates, or nothing -- which leaves file completion to
    # `-o default`, as before.
    local directive=:files
    if [[ ${lines[0]} == :* ]]; then
        directive=${lines[0]}
        lines=("${lines[@]:1}")
    elif ((${#lines[@]})); then
        directive=:values
    fi
    COMPREPLY=()
    if [[ $directive == :values ]]; then
        COMPREPLY=("${lines[@]}")
        # Bash replaces only what follows the last word-break character in
        # the word, so a candidate must drop what precedes it: `llama3:8b`
        # is offered as `8b` once `llama3:` is typed.
        if [[ $current == *:* && $COMP_WORDBREAKS == *:* ]]; then
            local kept=${current%"${current##*:}"}
            COMPREPLY=("${COMPREPLY[@]#"$kept"}")
        fi
    fi
    # Anything but a path must not fall back to file names. bash cannot show
    # a hint, and bash 3.2 (macOS's /bin/bash) has no compopt, where the
    # fallback stays.
    if [[ $directive != :files ]] && ((${#COMPREPLY[@]} == 0)); then
        compopt +o default 2>/dev/null
    fi
}
complete -o default -F _apogee apogee
