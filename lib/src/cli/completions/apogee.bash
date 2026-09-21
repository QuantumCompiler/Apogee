# Bash completion for apogee.
#
# Deliberately tiny: every candidate comes from `apogee __complete`, so this
# file never needs regenerating when a command or a backend is added. A
# generated static completion cannot know the user's configured backend names,
# which is the one thing worth completing here.
_apogee() {
    local IFS=$'\n'
    local words=("${COMP_WORDS[@]:1:COMP_CWORD}")
    COMPREPLY=($(apogee __complete "${words[@]}" 2>/dev/null))
}
complete -o default -F _apogee apogee
