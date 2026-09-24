# Bash completion for apogee.
#
# Deliberately tiny: every candidate comes from `apogee __complete`, so this
# file never needs regenerating when a command or a backend is added. A
# generated static completion cannot know the user's configured backend names,
# which is the one thing worth completing here.
_apogee() {
    local IFS=$'\n'
    local words=("${COMP_WORDS[@]:1:COMP_CWORD}")
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
    fi
    # Anything but a path must not fall back to file names. bash cannot show
    # a hint, and bash 3.2 (macOS's /bin/bash) has no compopt, where the
    # fallback stays.
    if [[ $directive != :files ]] && ((${#COMPREPLY[@]} == 0)); then
        compopt +o default 2>/dev/null
    fi
}
complete -o default -F _apogee apogee
