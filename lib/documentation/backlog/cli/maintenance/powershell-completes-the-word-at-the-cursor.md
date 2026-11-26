# CLI-M37 · PowerShell completes the word at the cursor

| Field | Value |
|---|---|
| **Type** | 🐞 Bug |
| **Project** | CLI |
| **Status** | 🟢 Ready |
| **Fix version** | Maintenance (any time) |
| **Component** | Shell completion stubs › `completions/apogee.ps1` |
| **Labels** | `completion` `powershell` `windows` |
| **Epic** | — (maintenance has no track) |
| **Blocked by** | — |
| **Blocks** | [M42](stubs-read-the-words-as-the-shell-does.md) |
| **Reporter** | CLI-M36's probes (2026-11-22), then a sandboxed investigation and a skeptic's re-run the same day |
| **Created** | 2026-11-22 |

---

## Summary

The PowerShell completion stub sends `apogee __complete` the whole command line and ignores the cursor. So a Tab in the middle of a line completes the line's last word and puts the answer where the cursor is, and on Windows PowerShell 5.1 every fresh word below the top level re-offers the word before it. The item makes the stub send what the bash and zsh stubs send: the words up to the cursor, and the word under it up to the cursor.

## Description

### User story

**As** a person on Windows (or running `pwsh` anywhere) who uses Tab with `apogee`,
**I want** Tab to complete the word I am on, wherever the cursor is, on every PowerShell I might have,
**so that** going back to fill in `-m` or a model name gives me the right candidates, not a flag that belongs at the end of the line.

### Steps to reproduce

`pwsh` is not on the dev Mac and is not to be installed, so the stub's argv was worked out from its code (`apogee.ps1:7-9`) and Microsoft's documentation (Evidence 2), then fed to the built binary. The setup: a throwaway `APOGEE_HOME` and `HOME`, `env -i`, stdin from `/dev/null`, three mock backends (`claudy`, `local`, `mockb`), 2026-11-22.

1. Type `apogee chat -m  --tools`, move the cursor back into the gap after `-m`, press Tab.
2. Type `apogee chat -m cl --tools`, cursor after `cl`, press Tab.
3. On Windows PowerShell 5.1 (or `$PSNativeCommandArgumentPassing = 'Legacy'`), type `apogee models ` and press Tab.

### Actual result

1. The stub sends `__complete chat -m --tools ''` and gets chat's flags (`--allow|--allow-bypass|--allow-host|--attach|…`). PowerShell inserts one at the cursor, as `-m`'s value.
2. It sends `__complete chat -m cl --tools` and gets `--tools`, and PowerShell replaces `cl` with `--tools`.
3. The `''` is dropped, so it sends `__complete models` and gets `models`, the word already typed.

### Expected result

1. `__complete chat -m ''`, answered `claudy|local|mockb`, as the zsh stub sends.
2. `__complete chat -m cl`, answered `claudy`.
3. `__complete models ''`, answered `list|info|status|rename|pull|delete|quantize|convert|repair|migrate`.

### Background

Every stub only forwards the line to `apogee __complete` ([ADR 0007](../../../adrs/cli/tab-completion.md)), and the protocol takes its last argument as the word being typed and everything before it as context (`complete_protocol.h:31-37`, `complete_protocol.cpp:665-672`). The bash stub cuts the line at `COMP_POINT` and the zsh stub at `CURRENT`, both since c5d1a258 (2026-09-24). The PowerShell stub is unchanged since v0.1.0 (502e45de, 2026-09-01). CLI-M36's probes found the gap while measuring what completion sees, and listed it as its own item.

### Evidence

All 2026-11-22, by reading the stub, by Microsoft's documentation and PowerShell's source (read that day), and by feeding the binary each argv in a sandbox (`apogee 0.2.0 (33ea685d, 2026-11-22, macos-arm64)`).

1. **The stub** (`apogee.ps1:6-9`): `param($wordToComplete, $commandAst, $cursorPosition)`; every CommandElement after the first, as `.ToString()`; `''` appended only when there are no tokens or `$wordToComplete` is empty; `$cursorPosition` never read; no `APOGEE_COMPLETION_PROTOCOL`.
2. **What PowerShell passes.**
   - In a `-Native` completer, `$commandAst` is "the AST for the current input line" and `$cursorPosition` "the position of the cursor when the user pressed Tab" (Register-ArgumentCompleter, learn.microsoft.com).
   - PowerShell passes the cursor as `context.CursorPosition.Offset`, in the same offsets as each element's `Extent`.
   - It replaces the whole token at the cursor, or inserts at the cursor in a gap (`CompletionCompleters.cs`, `CompletionAnalysis.cs`).
3. **The other stubs cut at the cursor**, checked by driving real shells through zpty. zsh 5.9 and bash 5.3, at col 15 of `apogee chat -m  --tools`, send `[chat] [-m] []`; at col 17 of `apogee chat -m claudy --tools`, zsh sends `[chat] [-m] [claudy]` and bash `[chat] [-m] [cl]` (`_apogee:10`, `apogee.bash:15-19`).
4. **Mid-line, 9 of 10 probed lines got a different argv from zsh; 8 of those were wrong answers.**

   | Line (cursor at `▮`) | The PowerShell stub sends | It gets | zsh sends | It gets |
   |---|---|---|---|---|
   | `apogee chat -m ▮ --tools` | `chat -m --tools ''` | chat's flags | `chat -m ''` | `claudy local mockb` |
   | `apogee chat -m cl▮ --tools` | `chat -m cl --tools` | `--tools` | `chat -m cl` | `claudy` |
   | `apogee chat -m cl▮audy --tools` | `chat -m claudy --tools` | `--tools` | `chat -m claudy` | `claudy` |
   | `apogee models delete org▮ --yes` | `models delete org --yes` | `--yes` | `models delete org` | `org--m org--m/gguf/…` |
   | `apogee config add-backend x --type ▮ --model m` | `… --type --model m ''` | add-backend's flags | `… --type ''` | the ten backend types |

   The gap before a value-less `--yes` got the right names by luck.
5. **Legacy passing drops the empty word.** Windows PowerShell 5.1 and pwsh 7.0–7.2 drop an empty argument to a native command; 7.3 made "Empty strings as arguments are now preserved" (about_Parsing). With the `''` left out, `[models]` gets `models`, `[chat] [-m]` gets `-m`, and only `apogee <TAB>` (no words) and a partial word at the end of the line still work. `install.ps1` has no `#Requires`, so a stock Windows user runs it under 5.1.
6. **A comma list loses its head.** PowerShell tokenises `,` as an array separator in argument mode (about_Parsing). So at `--consultable vision,ut▮`, PowerShell replaces only `ut`. The protocol answers `vision,utility` (probed: `__complete config add-suite s --consultable vision,ut` gives `:values|vision,utility|`), so picking it would leave `vision,vision,utility`. It is unverified without pwsh, but it follows from the tokenizer and the replacement range. It affects `--consultable`, `review --skills`, `models pull`'s comma options and `COLLECTION,...`, as bash's `:` trim (`apogee.bash:40-46`) already handles for that shell.
7. **A cursor right after a flag** (`apogee chat -m▮ --tools`) sends `chat -m --tools`, so `--tools` is read as `-m`'s value and PowerShell falls back to file names.
8. **The bare protocol is a recorded choice**: "PowerShell keeps the bare protocol, where an empty answer already means its own path completion" (MILESTONES, 2026-09-24). So free text and an empty list fall back to file names by design, not by this bug.
9. **Blast radius.**
   - Every PowerShell user who sources the stub, on 5.1 and every pwsh. `install.ps1:107-113` copies it to `Documents\PowerShell\Completions` and prints the profile line to add; it does not source it itself.
   - `package.sh:73` ships it in every archive, so a pwsh user on macOS or Linux can source it too.
   - No test runs `apogee.ps1`: `cli.shell_completion` covers bash, zsh and fish, is registered under `if(UNIX)` and does not run in CI (`shell_completion_check.sh`, `tests/CMakeLists.txt`).
10. **What already works**:
    - the protocol, given the cut argv;
    - end-of-line Tab on pwsh 7.3+, where `apogee.exe` gets Standard passing and the argv equals zsh's;
    - `apogee <TAB>` on every version;
    - a partial word at the end of the line;
    - candidates returned as `CompletionResult 'ParameterValue'`.
11. **Where a test can run**: GitHub's `ubuntu-24.04` image carries PowerShell 7.6.10, and the Linux unit-test rows run `apogee_tests` directly (`ci.yml`). A Catch2 case that spawns `pwsh` skips on the Windows rows, which start no child (`child_process.cpp:286-291`), and on a host without pwsh.

## Acceptance criteria

- [ ] **Given** each line in Evidence 4's table, **when** Tab is pressed at the cursor, **then** the stub sends `__complete` the argv the zsh stub sends for the same line: the words before the cursor, then the word under the cursor up to the cursor (`''` in a gap).
- [ ] **Given** the cursor directly after a flag with no space (`apogee chat -m▮ --tools`), **then** the stub sends `chat -m`, with `-m` the word being typed, and never `--tools` as `-m`'s value.
- [ ] **Given** Windows PowerShell 5.1, pwsh 7.0–7.2, or `$PSNativeCommandArgumentPassing = 'Legacy'`, **when** Tab is pressed at a fresh word (`apogee models ▮`), **then** the binary receives an empty last word and offers `models`' verbs, not `models`.
- [ ] **Given** pwsh 7.2 with the experimental `PSNativeCommandArgumentPassing` feature on and set to `Standard`, **then** the empty word is sent as `''`, not as a literal `""`.
- [ ] **Given** a comma list being typed (`--consultable vision,ut▮`), **when** `vision,utility` is chosen, **then** the line reads `--consultable vision,utility`.
- [ ] **Given** `cd x; apogee models ▮`, **then** the stub sends `models ''`, with the offsets taken from the elements, not from a joined string.
- [ ] **Given** the bare protocol, **then** answers are unchanged: names come back as `CompletionResult` values, and an empty answer still lets PowerShell complete paths.

## Constraints

- **Stubs know nothing** ([ADR 0007](../../../adrs/cli/tab-completion.md)): the stub only cuts the line and forwards it; no candidate is written in it.
- **The protocol is untouched** ([ADR 0006](../../../adrs/cli/backwards-compatibility.md)). An old binary with the new stub, and the reverse, still work.
- **The bare protocol stays** (2026-09-24, recorded). The stub sets no `APOGEE_COMPLETION_PROTOCOL`.
- **Cut by the elements' extents**, which share `$cursorPosition`'s offsets, never by `Substring` of a joined string.
- **No PowerShell on the dev Mac.** The test is gated on `pwsh` being found and on child processes; nothing installs it.

## Technical approach

Paths under `lib/src/cli/`.

| File | Change |
|---|---|
| `completions/apogee.ps1` | Build the argv from the elements before the cursor (an element starting at or after `$cursorPosition` ends the walk). The element the cursor is in or touches is cut to the cursor. A fresh word adds `''`. Under Legacy passing the empty word is sent as `'""'`, by cobra's three-way check (version below 7.3, the experimental feature off, or `$PSNativeCommandArgumentPassing -eq 'Legacy'`). When the word being typed holds a `,`, strip each candidate up to its last comma before returning it, as `apogee.bash` strips a `:`. Elements keep `Extent.Text` as today. |
| `tests/presentation/cli/lifecycle_test.cpp` | A case gated by `SKIP` when `pwsh` is not on `PATH` or `!platform::supports_child_processes()`. It puts a fake `apogee` on a scratch `PATH` (a `sh` script printing each argument as `[arg]`) and dot-sources the stub in `pwsh -NoProfile -NonInteractive`. For each line it runs `TabExpansion2 -inputScript <line> -cursorColumn <n>` and compares `CompletionMatches.CompletionText` (the argv the stub sent) with the expected argv. The table runs twice, Standard and after `$PSNativeCommandArgumentPassing = 'Legacy'`. |
| `tests/scripts/sh/shell_completion_check.sh` | Optional: a pwsh section beside zsh and fish, reported and not failed when pwsh is absent, running the same table end to end against the real binary. |

The stub's shape, unrun without pwsh (the builder proves it through the test):

```powershell
param($wordToComplete, $commandAst, $cursorPosition)
$words = @(); $fresh = $true
foreach ($e in @($commandAst.CommandElements | Select-Object -Skip 1)) {
    if ($e.Extent.StartOffset -ge $cursorPosition) { break }
    $text = $e.Extent.Text
    if ($e.Extent.EndOffset -ge $cursorPosition) {
        $words += $text.Substring(0, $cursorPosition - $e.Extent.StartOffset); $fresh = $false; break
    }
    $words += $text
}
if ($fresh) { $words += '' }
```

**Reference.** `apogee.bash:15-19`, the `COMP_LINE`/`COMP_POINT` cut, and `apogee.bash:40-46`, the `:` trim. cobra's `powershell_completions.go` cuts at `$CursorPosition` and sends `""` under Legacy passing, with the three-way guard. Our cut uses the elements' extents instead of its `Substring`, so `cd x; apogee …` and doubled spaces stay right.

**Docs when it ships:** DEVELOPER.md's completion-stub notes ("PowerShell is not covered" becomes "covered where pwsh exists") and the `lifecycle_test` row; MILESTONES (a dated entry, the 2026-09-24 bare-protocol choice kept).

## Testing

The stub's logic is pinned in process through `TabExpansion2` with a fake `apogee` that echoes its argv, so the test needs no binary and no network.

- **The cut.** Each row of Evidence 4's table must produce zsh's argv. So must the flag-touching cursor, the cursor touching the start of a word (it sends what bash sends), and `cd x; apogee models ▮`. Today's stub fails every mid-line row.
- **Legacy passing.** The same table after `$PSNativeCommandArgumentPassing = 'Legacy'`; `apogee models ▮` must still end in `[]` (today `[models]`).
- **Comma lists.** `--consultable vision,ut▮`: the CompletionText offered is `utility` (the head stripped), with the fake `apogee` answering `vision,utility`.
- **Where it runs:** the Linux and (if pwsh is there) macOS unit-test rows. It skips on the Windows rows and on a host without pwsh, and the skip names why.
- **Gate:** `lib/scripts/cicd.sh --app cli --test`, plus the CI run whose Linux rows carry pwsh.

## Open questions

| Question | Status | Answer |
|---|---|---|
| Should PowerShell move to protocol 2, so a hint or free text gets no file names? | `[default]` | No: the 2026-09-24 choice stands. A switch would be its own small item, and would have to accept an older binary that answers bare, as the other stubs do. |
| Should CI's Windows rows run the stub under `powershell.exe` 5.1? | `[default]` | No. The 2026-09-19 and 2026-09-22 decisions keep CI to the builds and the source-level suite. The pwsh case with Legacy passing pins the logic; 5.1 itself is covered by its documented argument rules. |
| With the cursor inside a word, does the stub send the word up to the cursor (bash, fish) or whole (zsh)? | `[default]` | Up to the cursor, as bash does. PowerShell then replaces the whole token with the chosen candidate. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-22 | Specced from CLI-M36's probes and a sandboxed investigation with a skeptic's re-run (Evidence); placed in **Maintenance** as M37. | the spec |
| 2026-11-22 | **The fix is the stub alone.** The protocol answers correctly for the cut argv (Evidence 3–4), and a protocol that took the raw line and a position would make the binary tokenise PowerShell syntax. | the spec |
| 2026-11-22 | **Quote characters are left as they are.** The zsh and bash stubs also send `'models'` with its quotes (measured through zpty), so stripping them in PowerShell alone would put it ahead of the others, not level with them. | the spec |
| 2026-11-22 | **The quote work across the four stubs is CLI-M42**, gated on this item: it takes PowerShell's quotes on top of this item's element walk and extends its `pwsh` case. This item still leaves quotes as they are. On bash 3.2 this item's Reference, `apogee.bash:15-19` and `:40-46`, is CLI-M41's bug at `:19` and `:45`. The cut is still the model; the slice and the trim are not, on 3.2. | the spec |

## Out of scope

- **Quote characters, in every stub.** All of them pass a quoted word with its quotes, and bash splits a quoted value with spaces. That is one item across the stubs: [CLI-M42](stubs-read-the-words-as-the-shell-does.md).
- **macOS's `/bin/bash` 3.2 sends the whole line as one word** (`"${words[@]:1}"` under `local IFS=$'\n'`, `apogee.bash:8,19`), so nothing below the top level completes in that shell. `cli.shell_completion` runs PATH's bash 5.3 and never sees it. Its own item: [CLI-M41](bash-3-2-completes-every-word.md).
- **The protocol-2 directives for PowerShell** (above).

## Links

- **Blocked by:** — · **Blocks:** [CLI-M42](stubs-read-the-words-as-the-shell-does.md), which takes PowerShell's quotes on top of this item's walk
- **Relates to:** [CLI-M41](bash-3-2-completes-every-word.md) (bash 3.2, which shares this item's fake-`apogee` harness); [CLI-M36](grant-and-deny-flags-offer-what-they-take.md), whose rule "the line is read up to the cursor" holds on PowerShell only once this ships; [ADR 0007](../../../adrs/cli/tab-completion.md); [ADR 0006](../../../adrs/cli/backwards-compatibility.md); the 2026-09-24 completion work (c5d1a258: bash's cursor cut and protocol 2 for the POSIX stubs).
