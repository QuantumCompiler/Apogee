# Tab completion for lib/scripts/cicd.sh.
#
# bash — add to ~/.bashrc:
#   source /path/to/Apogee/lib/scripts/cicd-completion.bash
#
# zsh — add to ~/.zshrc:
#   autoload -U +X bashcompinit && bashcompinit
#   source /path/to/Apogee/lib/scripts/cicd-completion.bash

_apogee_cicd() {
    local cur="${COMP_WORDS[COMP_CWORD]}"
    local prev="${COMP_WORDS[COMP_CWORD-1]}"

    case "$prev" in
        -p|--platform)
            COMPREPLY=( $(compgen -W "linux-x64 linux-arm64 macos-x64 macos-arm64 windows-x64 windows-arm64 all" -- "$cur") )
            return
            ;;
        -b|--branch)
            # Complete local branch names.
            COMPREPLY=( $(compgen -W "$(git for-each-ref --format='%(refname:short)' refs/heads 2>/dev/null)" -- "$cur") )
            return
            ;;
        -j|--jobs)
            COMPREPLY=()
            return
            ;;
    esac

    COMPREPLY=( $(compgen -W "-p --platform -c --clean -t --test -f --fresh -b --branch -j --jobs -h --help" -- "$cur") )
}

complete -F _apogee_cicd cicd.sh ./cicd.sh lib/scripts/cicd.sh ./lib/scripts/cicd.sh
