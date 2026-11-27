# Fish completion for apogee. See apogee.bash for why this only forwards.
function __apogee_complete
    # The words as the command will get them: the protocol matches words
    # literally, so each is sent tokenized (`-o`) -- unescaped, its quotes
    # removed even while one is still open (`'cl`), nothing expanded. The
    # token under the cursor too; only the path completion below takes it as
    # typed, since __fish_complete_path reads its argument as command-line
    # text. Candidates go back raw, since fish quotes them for the line itself.
    set -l tokens (commandline -opc)
    # Quoted, so an empty word under the cursor is still passed: without it
    # `apogee models <TAB>` reads as "still typing models".
    set -l current (commandline -ot)
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
            __fish_complete_path (commandline -ct)
        case ':hint*'
            return
        case '*'
            printf '%s\n' $lines
    end
end
complete -c apogee -f -a '(__apogee_complete)'
