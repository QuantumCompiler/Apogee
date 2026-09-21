# Fish completion for apogee. See apogee.bash for why this only forwards.
function __apogee_complete
    set -l tokens (commandline -opc) (commandline -ct)
    apogee __complete $tokens[2..-1] 2>/dev/null
end
complete -c apogee -f -a '(__apogee_complete)'
