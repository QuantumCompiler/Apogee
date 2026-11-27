# PowerShell completion for apogee. See apogee.bash for why this only forwards.
#
# Windows ships binaries from the first tagged release (decided 2026-09-01), so
# it gets a real completion rather than an omission.
Register-ArgumentCompleter -Native -CommandName apogee -ScriptBlock {
    param($wordToComplete, $commandAst, $cursorPosition)
    # The words up to the cursor, as the bash and zsh stubs send them: an
    # element starting at or after the cursor ends the walk, and the one the
    # cursor is in or touches is the word being typed, cut at the cursor. Cut
    # by the elements' own extents, which share the cursor's offsets, so text
    # before the command (`cd x; apogee ...`) or a doubled space shifts nothing.
    # Each word as the command will get it, since the protocol matches words
    # literally: a string's value, its quotes and escapes removed and no
    # variable replaced ('models', "Be brief", my` notes); anything else as typed.
    $words = @()
    $current = ''
    $typing = $null
    $elements = $commandAst.CommandElements
    for ($i = 1; $i -lt $elements.Count; $i++) {
        $extent = $elements[$i].Extent
        if ($extent.StartOffset -ge $cursorPosition) { break }
        if ($extent.EndOffset -ge $cursorPosition) {
            $current = $extent.Text.Substring(0, $cursorPosition - $extent.StartOffset)
            $typing = $elements[$i]
            break
        }
        if ($elements[$i] -is [System.Management.Automation.Language.StringConstantExpressionAst] -or
            $elements[$i] -is [System.Management.Automation.Language.ExpandableStringExpressionAst]) {
            $words += $elements[$i].Value
        } else {
            $words += $extent.Text
        }
    }
    # A string under the cursor is sent as its value too: whole, when the
    # cursor is at its end, or else cut there, its opening quote dropped and
    # a doubled quote undoubled -- so `-m 'cl` sends `cl`.
    $oneToken = ($typing -is [System.Management.Automation.Language.StringConstantExpressionAst] -or
        $typing -is [System.Management.Automation.Language.ExpandableStringExpressionAst])
    $opened = $oneToken -and ($current.StartsWith("'") -or $current.StartsWith('"'))
    if ($oneToken -and $cursorPosition -ge $typing.Extent.EndOffset) {
        $current = $typing.Value
    } elseif ($opened) {
        $quote = $current.Substring(0, 1)
        $current = $current.Substring(1).Replace($quote + $quote, $quote)
    }
    # Windows PowerShell 5.1, pwsh 7.0-7.2 (unless 7.2's experimental argument
    # passing is on) and Legacy passing drop an empty argument to a program, so
    # a fresh word arrived as "still typing the last one". There `""` reaches
    # the program as the empty word, as cobra's completions send it; anywhere
    # else `''` does.
    $legacy = ($PSVersionTable.PSVersion -lt [version]'7.2.0' -or
        ($PSVersionTable.PSVersion -lt [version]'7.3.0' -and
            -not [ExperimentalFeature]::IsEnabled('PSNativeCommandArgumentPassing')) -or
        $PSNativeCommandArgumentPassing -eq 'Legacy')
    if ($current -eq '' -and $legacy) { $words += '""' } else { $words += $current }
    # A comma separates array elements in a bare word, and PowerShell replaces
    # only what follows the last one: `vision,utility` is offered as `utility`
    # once `vision,` is typed, as apogee.bash trims a candidate at a `:`. A
    # string -- quoted, or a comma escaped -- is one token, replaced whole.
    $kept = ''
    $comma = $current.LastIndexOf([char]',')
    if ($comma -ge 0 -and -not $oneToken) { $kept = $current.Substring(0, $comma + 1) }
    & apogee __complete @words 2>$null | ForEach-Object {
        $candidate = [string]$_
        if ($kept -and $candidate.StartsWith($kept, [System.StringComparison]::Ordinal)) {
            $candidate = $candidate.Substring($kept.Length)
        }
        if ($candidate) {
            # Inserted as the line will read it: single-quoted, any quote in
            # it doubled, where a bare word would not run as this one word or
            # would be expanded (pwsh globs and expands `~` in a native
            # command's bare words off Windows), or where the typed word opened
            # a quote. Listed by its name.
            $text = $candidate
            if ($opened -or $candidate -match '[\s`$&;,(){}@#|<>''"*?\[\]]|^~') {
                $text = "'" + $candidate.Replace("'", "''") + "'"
            }
            [System.Management.Automation.CompletionResult]::new($text, $candidate, 'ParameterValue', $candidate)
        }
    }
}
