# Fish completion for apogee. See apogee.bash for why this only forwards.
function __apogee_complete
    set -l tokens (commandline -opc)
    # Quoted, so an empty word under the cursor is still passed: without it
    # `apogee models <TAB>` reads as "still typing models".
    set -l current (commandline -ct)
    set -l lines (env APOGEE_COMPLETION_PROTOCOL=2 apogee __complete $tokens[2..-1] "$current" 2>/dev/null)
    test (count $lines) -gt 0; or return
    # The first line says what the rest mean: names to offer, a path (fish's
    # own file completion, which `-f` below otherwise switches off), or free
    # text (nothing -- fish has no way to show a hint without a candidate).
    # A binary too old to send it sends bare candidates.
    switch $lines[1]
        case ':values'
            test (count $lines) -gt 1; and printf '%s\n' $lines[2..-1]
        case ':files'
            __fish_complete_path "$current"
        case ':hint*'
            return
        case '*'
            printf '%s\n' $lines
    end
end
complete -c apogee -f -a '(__apogee_complete)'
