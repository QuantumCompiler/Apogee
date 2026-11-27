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
        $words += $extent.Text
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
    $oneToken = ($typing -is [System.Management.Automation.Language.StringConstantExpressionAst] -or
        $typing -is [System.Management.Automation.Language.ExpandableStringExpressionAst])
    if ($comma -ge 0 -and -not $oneToken) { $kept = $current.Substring(0, $comma + 1) }
    & apogee __complete @words 2>$null | ForEach-Object {
        $candidate = [string]$_
        if ($kept -and $candidate.StartsWith($kept, [System.StringComparison]::Ordinal)) {
            $candidate = $candidate.Substring($kept.Length)
        }
        if ($candidate) {
            [System.Management.Automation.CompletionResult]::new($candidate, $candidate, 'ParameterValue', $candidate)
        }
    }
}
