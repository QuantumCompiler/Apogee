# PowerShell completion for apogee. See apogee.bash for why this only forwards.
#
# Windows ships binaries from the first tagged release (decided 2026-09-01), so
# it gets a real completion rather than an omission.
Register-ArgumentCompleter -Native -CommandName apogee -ScriptBlock {
    param($wordToComplete, $commandAst, $cursorPosition)
    $tokens = @($commandAst.CommandElements | Select-Object -Skip 1 | ForEach-Object { $_.ToString() })
    if ($tokens.Count -eq 0 -or $wordToComplete -eq '') { $tokens += '' }
    & apogee __complete @tokens 2>$null | ForEach-Object {
        [System.Management.Automation.CompletionResult]::new($_, $_, 'ParameterValue', $_)
    }
}
