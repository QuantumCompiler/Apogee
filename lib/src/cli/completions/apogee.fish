# Fish completion for apogee. See apogee.bash for why this only forwards.
function __apogee_complete
    set -l tokens (commandline -opc)
    # Quoted, so an empty word under the cursor is still passed: without it
    # `apogee models <TAB>` reads as "still typing models".
    set -l current (commandline -ct)
    apogee __complete $tokens[2..-1] "$current" 2>/dev/null
end
complete -c apogee -f -a '(__apogee_complete)'
