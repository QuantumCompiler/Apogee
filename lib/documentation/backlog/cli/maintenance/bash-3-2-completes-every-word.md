# CLI-M41 · macOS's bash 3.2 completes every word

| Field | Value |
|---|---|
| **Type** | 🐞 Bug |
| **Project** | CLI |
| **Status** | 🟢 Ready |
| **Fix version** | Maintenance (any time) |
| **Component** | Tab completion › the bash stub (`completions/apogee.bash`) |
| **Labels** | `completion` `bash` `macos` |
| **Epic** | — (maintenance has no track) |
| **Blocked by** | — |
| **Blocks** | [M42](stubs-read-the-words-as-the-shell-does.md) |
| **Reporter** | CLI-M37's probes (2026-11-22), then a sandboxed investigation and a skeptic's re-run the same day |
| **Created** | 2026-11-22 |

---

## Summary

On macOS's own `/bin/bash` (3.2.57), the bash stub sends `__complete` the whole line as one word, so nothing below the top level completes. A top-level Tab with two or more matches pastes every match into the line. A 3.2 expansion fault causes it: the stub's function-wide `local IFS=$'\n'` makes bash 3.2 join every quoted array slice into one word. The item sets a default IFS and splits the answer with `read`, a two-line change, and runs the existing check under `/bin/bash` too, plus a CI-visible case that names it.

## Description

### User story

**As** someone whose interactive shell is macOS's `/bin/bash` and who has loaded Apogee's bash completion,
**I want** `apogee models <TAB>` and `apogee chat -m <TAB>` to offer what bash 5 offers,
**so that** Tab completes the verb or backend instead of a file name, and never fills my line with every verb.

### Steps to reproduce

The sandbox, 2026-11-22, with the built binary `apogee 0.2.0 (33ea685d, 2026-11-22, macos-arm64)`: a throwaway `APOGEE_HOME` and `HOME`, `env -i`, stdin from `/dev/null`, and three mock backends (`claudy`, `local`, `mockb`). The cwd holds `fileA.txt` and `fileB.txt`. Real `/bin/bash --norc --noprofile -i` (3.2.57) is driven through zsh's zpty with the repo's `completions/apogee.bash` sourced, and a shim logs each `__complete` argv.

1. Type `apogee models ` and press Tab.
2. Type `apogee chat -m cl` and press Tab.
3. Type `apogee c` and press Tab.
4. Run `lib/src/cli/tests/scripts/sh/shell_completion_check.sh <binary> <work>` with `PATH=/usr/bin:/bin`, so its bash is `/bin/bash`.

### Actual result

1. The stub sends `[__complete] [models ]`, one word. The binary answers `:values` with no candidates, bash 3.2 falls back to file names, and the line becomes `apogee models file`.
2. The stub sends `[chat -m cl]`, and the line stays `apogee chat -m cl`.
3. The line becomes `apogee chat check chats complete config `. Enter would run that. `apogee <TAB>` pastes all 30 verbs.
4. `26 completion check(s) failed`, exit 1, every one in bash. For example: `FAIL: bash: 'apogee <TAB>' did not offer 'models'; got: acp agents analyze …`.

### Expected result

What `/opt/homebrew/bin/bash` 5.3.9 does on the same lines, measured the same day:
1. `[models] []`, and models' ten verbs listed.
2. `[chat] [-m] [cl]`, and the line becomes `apogee chat -m claudy `.
3. `chat chats check complete config` listed, the line unchanged.
4. `shell completion OK`.

The one 3.2 difference the stub already records (`apogee.bash:48-50`) stays: with no `compopt`, a hint or an empty answer still falls back to file names.

### Background

The stub sets `local IFS=$'\n'` for its whole function (`apogee.bash:8`) because one line needed it: the newline split of the binary's answer at `:26`. Every other line in the function also runs under it.

The fault is old:
- v0.1.0 (502e45de, 2026-09-01) already sliced `COMP_WORDS` under that IFS, so 3.2 never completed below the top level.
- f046bc7b (2026-09-23) added the directive strip (`:33`), which brought the top-level paste.
- c5d1a258 (2026-09-24) carried the slice into the `COMP_LINE` path (`:19`) and added the `:` trim (`:45`).

CLI-M37's Out of scope named it as its own item.

### Evidence

All measured 2026-11-22 by the investigator, then again by a skeptic with its own sandbox and its own copy of the fix.

1. **The construct**, reduced in a matrix of array expansions under six IFS values (`reduce.sh`, `reduce_ifs.sh`):
   - On bash 3.2.57, under an IFS that is non-empty and holds no space, a quoted array expansion that carries an operator expands to ONE word, its elements joined by spaces. That covers `"${a[@]:n}"`, `"${a[@]:n:m}"`, `"${a[@]#pat}"`, `"${a[@]/pat/rep}"` and `"${@:n}"`, as an argument, in an assignment, in `local -a` or in a `for` list.
   - For example, under `IFS=$'\n'`, `b=("${a[@]:1}")` gives `1: [chat -m ]`.
   - `IFS=$'\n'`, `$'\t\n'` and `:` join; `''`, `$' \n'`, `$'\n '`, `$'\t \n'` and `': '` do not.
   - Plain `"${a[@]}"`, an index loop, and `unset 'a[0]'; a=("${a[@]}")` are right under every IFS.
   - bash 5.3.9 is right in every row.
   - bash's CHANGES lists fixes to `${array[@]:foo}` expansion between 3.2 and 4.0 (`CHANGES:5916-5917`, `:5791-5793`). Their stated condition is not quite the measured one, so "4.0 fixed it" is an inference. Only 5.3's correctness was measured.
2. **The four sites**, each shown by an instrumented scratch copy of the stub, the repo file untouched:
   - `:19` `words=("${words[@]:1}")`: 2 words in, 1 out (`[models ]`).
   - `:21` `words=("${COMP_WORDS[@]:1:COMP_CWORD}")`: the same.
   - `:33` `lines=("${lines[@]:1}")`: 6 lines in (`[:values] [chat] [check] …`), 1 out (`[chat check chats complete config]`).
   - `:45` `COMPREPLY=("${COMPREPLY[@]#"$kept"}")`: joins too (`[3b llama3.2:1b]` with a stand-in answer), hidden behind `:19` and `:33`.
3. **What is not at fault**, measured right on 3.2:
   - the cut at the cursor, `${COMP_LINE:0:COMP_POINT}` (`:16`); `chat -m cl▮ --tools` sends no `--tools`;
   - the blank split, `IFS=$' \t' read -r -a` (`:17`);
   - the newline split, `lines=($(…))` (`:26`), and `"${words[@]}"`;
   - `COMPREPLY=("${lines[@]}")` (`:39`).

   `COMP_WORDBREAKS` plays no part.
4. **The binary is right.** Given split words, `__complete models ''` answers `:values|list|info|…`, and `chat -m cl` answers `claudy`. Given the joined word, `[models ]` answers `:values|` with nothing.
5. **The fix, proven in scratch.** Two edits:
   - `:8` becomes `local IFS=$' \t\n'`;
   - `:26` becomes `out=$(…)` plus `IFS=$'\n' read -r -d '' -a lines <<<"$out"`.

   Each was measured on 3.2.57 and 5.3.9:
   - the repo's own check passes under each bash: `shell completion OK`;
   - real 3.2 lists `models`' verbs, lists five `c` verbs and leaves the line alone, gives `chat -m claudy --tools` mid-line, `llama3.2:3b` after `llama3.2:3`, and `…/111111111111` for a store id;
   - a reduced unit run of the stub fails 5 of 6 rows on 3.2 with the repo stub, and 0 with the fix;
   - a caller's `IFS=$'\n'`, `nullglob`, `failglob`, `IFS=:` or `set -u` gives the same answers.
6. **The answer lines are globbed today.** The unquoted `$(…)` at `:26` expands each answer line against the cwd: with a stand-in, `file*` came back as `fileA.txt fileB.txt`.
   - `models pull`'s hint line carries `owner/repo[:file.gguf]`. Under a caller's `failglob`, bash 5.3 prints `apogee.bash: line 26: no match: :hint ref MODEL_REF …`. Under `nullglob` the directive line disappears and file names come back.
   - The fix's `read` removes this on every bash. Every candidate byte is kept (spaces, tabs, a backslash, quotes, a glob). An empty line is dropped, as today.
7. **One gap the fix leaves.** Under a caller's `set -u` on 3.2, an empty `:values` answer fails with `lines[@]: unbound variable` (the repo stub at `:39` too). This is 3.2's empty-array rule, which `cicd.sh:352-356` already works around with `${a[@]+"${a[@]}"}`.
8. **Why the tests miss it.**
   - `shell_completion_check.sh` runs PATH's `bash` (`:1`, `:16-17`, `:118`), which on the development Mac is Homebrew's 5.3.
   - It is a ctest entry under `if(UNIX)` (`tests/CMakeLists.txt:719`, `:775-780`). CI runs only `cicd.sh --unit-tests`, the `apogee_tests` binary (`cicd.sh:378-401`, `ci.yml:475`, `:486`), so it never runs there.
   - `cicd.sh:352-356` says the macOS runners' bash is 3.2. That is the repo's own claim; the runner was not measured.
   - `install_script_check.sh:325-336` already re-runs under `/bin/bash` when its version differs, but only for `install.sh`.
9. **Who is hit.**
   - Anyone running bash older than 4.0 with the stub loaded, which in practice means macOS's `/bin/bash`: a login shell switched to it, a Terminal profile set to it, or `bash` typed with no newer bash on PATH. This host's login shell is `/bin/zsh`.
   - Nothing loads the stub on 3.2 by itself:
     - both installers put it in bash-completion v2's per-user folder (`install.sh:158-163`, `Makefile:231-236`);
     - bash-completion@2's loader returns below bash 4.2 (`bash_completion.sh:6-7`);
     - bash-completion 1.3 reads only `bash_completion.d` and `~/.bash_completion`;
     - bash 3.2 has no `complete -D`.

     So the affected people are the 3.2 users who `source` the stub by hand. A 3.2 user who did nothing gets no Apogee completion at all, and no doc says how to load it.
   - `install.sh:155-157` says "bash is running this script, so its stub is always installed"; on a stock Mac, `curl | bash` is 3.2.
10. **Not affected:** bash 4.0 and later (every Linux, MSYS2 and Git Bash, Homebrew's bash), zsh (25 rows pass under either bash), fish, PowerShell (its own bug is CLI-M37), and the protocol.
11. **No other script has the construct** (`lib/scripts/cicd-completion.bash` has no IFS and no slices).

## Acceptance criteria

- [ ] **Given** `/bin/bash` 3.2 with the stub sourced, **when** `apogee models <TAB>` is pressed, **then** the stub sends `[models] []` and models' verbs are listed, never file names.
- [ ] **Given** the same shell, **when** `apogee chat -m cl<TAB>` is pressed, **then** the line becomes `apogee chat -m claudy `, and mid-line at `apogee chat -m cl▮ --tools` it becomes `apogee chat -m claudy --tools`.
- [ ] **Given** the same shell, **when** `apogee c<TAB>` and `apogee <TAB>` are pressed, **then** the matches are listed and the line is unchanged: no candidate list is pasted into it.
- [ ] **Given** the same shell, **when** `apogee models pull llama3.2:3<TAB>` is pressed, **then** the line becomes `apogee models pull llama3.2:3b `.
- [ ] **Given** bash 5, **then** every line above behaves as today.
- [ ] **Given** a caller's `IFS=$'\n'`, `nullglob`, `failglob` or `set -u`, **then** the answers are the same, on 3.2 and 5.
- [ ] **Given** a host whose `/bin/bash` is another version than PATH's `bash`, **when** `cli.shell_completion` runs, **then** its bash section runs under both and names both versions. With today's stub it fails under 3.2; with the fix it passes.
- [ ] **Given** CI's macOS unit-test row, **then** a case in `apogee_tests` sources the stub in `/bin/bash`, and fails with today's stub.

## Constraints

- **The stub stays a stub** ([ADR 0007](../../../adrs/cli/tab-completion.md)): it only calls `__complete`, and holds no candidate.
- **The protocol is untouched** ([ADR 0006](../../../adrs/cli/backwards-compatibility.md)): an old binary with the new stub, and the reverse, behave as today, including a binary that sends bare candidates.
- **bash 3.2 compatible.** No `compopt` assumed (`:48-52` stays), no `mapfile`, no associative arrays. Every array expansion that carries an operator runs under an IFS that holds a space.
- **Nothing writes shell configuration** (ADR 0007's delivery rule): no rc edit, no second install path.
- **The quote-blind split stays as it is.** `:17`'s `read -a` is bug 2's, CLI-M42's, and behaves the same on 3.2 and 5.3. This item does not touch it.

## Technical approach

Paths under `lib/src/cli/`.

| File | Change |
|---|---|
| `completions/apogee.bash` | `:8`: `local IFS=$' \t\n'`, with a comment saying why (bash 3.2 joins a quoted `"${a[@]:n}"` or `"${a[@]#x}"` into one word when IFS holds no space). `:26`: `local out; out=$(APOGEE_COMPLETION_PROTOCOL=2 apogee __complete "${words[@]}" 2>/dev/null)`, then `local -a lines=(); IFS=$'\n' read -r -d '' -a lines <<<"$out" \|\| :`. `:39`: `COMPREPLY=(${lines[@]+"${lines[@]}"})`, so 3.2's `set -u` empty answer passes. Lines `:19`, `:21`, `:33` and `:45` stay, now under a default IFS. |
| `tests/scripts/sh/shell_completion_check.sh` | The bash section runs once per distinct bash: PATH's `bash`, plus `/bin/bash` when `/bin/bash -c 'echo $BASH_VERSION'` differs (`install_script_check.sh:325-336`'s rule). `bash_complete` (`:117-136`) takes the bash binary as its first argument. Failures and the summary name the version (`bash: checked 25 lines, plus a word-break, under 5.3.9 and 3.2.57`). The header (`:16-17`) says so. A row asserts that two top-level candidates (`c` gives both `chat` and `config`) arrive as separate words. |
| `tests/presentation/cli/lifecycle_test.cpp` | A case, "the bash stub sends the words it is given, on every bash the host has". For `/bin/bash` and PATH's `bash` (deduplicated by version), it sources `completions/apogee.bash` (through `APOGEE_SOURCE_DIR "/../completions"`, `tests/CMakeLists.txt:455`) in `<bash> --norc --noprofile -c`, with a fake `apogee` `sh` script on a scratch PATH, and compares `COMPREPLY`. It SKIPs when `!platform::supports_child_processes()` or no bash is found. The rows are under Testing. |
| `README.md` (repository root) | One line in the install notes: on macOS's `/bin/bash`, add `source ~/.local/share/bash-completion/completions/apogee` to `~/.bash_profile`. bash-completion v2 does not load on bash 3.2. |

**Reference.** `install_script_check.sh:325-336`, which already re-runs a script under `/bin/bash` when it is another version. `cicd.sh:352-356`, the repo's 3.2 `set -u` rule. `child_process_test.cpp` and `git_test.cpp`, source-level cases in `apogee_tests` that already start `/bin/sh`.

**Docs when it ships:**
- DEVELOPER.md's `cli.shell_completion` paragraph (`:998`): "bash always" becomes PATH's bash, plus `/bin/bash` where it is another version, with the 3.2 expansion rule named.
- DEVELOPER.md's `lifecycle_test` row (`:902`): the bash-stub case.
- MILESTONES: a dated entry.

## Testing

The bug becomes two fixtures, each failing on today's stub and passing with the fix (both measured in scratch, 2026-11-22).

- **The end-to-end check under 3.2.** `shell_completion_check.sh`'s existing 25 bash rows, plus the word-break row, run under `/bin/bash` as well. Today it fails 26 checks under 3.2; the fix gives 0. It stays a ctest developer gate.
- **A CI-visible unit case.** In `lifecycle_test.cpp`: one fake `apogee` answers `:values`, then each argument it was sent as `[word]`, one per line, so `COMPREPLY` is the argv the stub sent. A second fake answers `:values`, `llama3.2:3b` and `llama3.2:1b`. Rows (`line` sets `COMP_LINE` and `COMP_POINT`; `words` sets only `COMP_WORDS` and `COMP_CWORD`):

  | Input | `COMPREPLY`, joined with `\|` |
  |---|---|
  | line `apogee ` | `[]` |
  | line `apogee models ` | `[models]\|[]` |
  | line `apogee chat -m ` | `[chat]\|[-m]\|[]` |
  | line `apogee chat -m cl` | `[chat]\|[-m]\|[cl]` |
  | words `apogee chat -m ` | `[chat]\|[-m]\|[]` |
  | the colon fake, line `apogee models pull llama3.2:` | `3b\|1b` |
  | a caller `IFS=$'\n'`, line `apogee chat -m ` | `[chat]\|[-m]\|[]` |
  | a caller `set -u`, an empty answer | no error, `COMPREPLY` empty |

  On CI's macOS row `/bin/bash` is Apple's 3.2, so the case runs there; the Linux rows run bash 5. The Windows rows SKIP, since they start no child.
- **Shared harness.** CLI-M37's planned `pwsh` case in the same file also uses a fake argv-echoing `apogee`. Whichever item lands first builds the harness, and the other reuses it.
- **Gate:** `lib/scripts/cicd.sh --app cli --test`.

## Open questions

| Question | Status | Answer |
|---|---|---|
| Which fix shape: a default IFS at `:8` plus a `read` split at `:26`, or keep `IFS=$'\n'` and rewrite the four expansions as index loops? | `[default]` | The default IFS and `read`. That is two sites, not four, and it keeps any later slice in the function safe on 3.2. It also ends the globbing of answer lines, and ignores an unusual IFS in the caller's shell. |
| Should the 3.2 lane run in CI, or only in the developer's ctest gate? | `[default]` | Both. The Catch2 case needs no built binary, only a fake on PATH, so it stays inside the rule that CI runs the source-level suite. The check also runs under `/bin/bash` in ctest. |
| Is `/bin/bash` 3.2 a supported completion shell? | `[default]` | Yes, already: the stub degrades for it on purpose (`:48-50`), and `cicd.sh` and the install check hold scripts to 3.2. This item restores what the stub claims. |
| Should the docs tell a 3.2 user how to load the stub, since nothing loads it there by itself? | `[default]` | One README line, as in the file table. No rc edit and no second install path (ADR 0007). |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-22 | Specced from CLI-M37's probes, a sandboxed investigation and a skeptic's re-run (Evidence). Placed in **Maintenance** as M41. | the spec |
| 2026-11-22 | **Its own item, ahead of the quote fix (CLI-M42).** The quote investigator proposed folding both into one bash rewrite. The 3.2 investigator and both skeptics measured that this fault has its own two-line fix, and that the quote-blind split at `:17` behaves the same on 3.2 and 5.3 and is left alone by it. A severe, simple fix should not wait on a tokenizer and on fish and PowerShell halves no one has run. M42 builds on these lines and must pass this item's `/bin/bash` lane. | the spec |

## Out of scope

- **Quote characters and quoted values with spaces**, in every stub: [CLI-M42](stubs-read-the-words-as-the-shell-does.md).
- **The PowerShell stub's cursor**: [CLI-M37](../../../assistant/cli/MILESTONES.md#2026-11-27--powershell-completes-the-word-at-the-cursor-maintenance-item-m37-a-tab-completes-the-word-at-the-cursor-on-every-powershell), shipped 2026-11-27.
- **Running `cli.shell_completion` in CI.** It needs the built binary, and CI runs the source-level suite. The Catch2 case is the CI lane.

## Links

- **Blocked by:** — · **Blocks:** [CLI-M42](stubs-read-the-words-as-the-shell-does.md), which rewrites the same lines
- **Relates to:**
  - [CLI-M37](../../../assistant/cli/MILESTONES.md#2026-11-27--powershell-completes-the-word-at-the-cursor-maintenance-item-m37-a-tab-completes-the-word-at-the-cursor-on-every-powershell): its Reference held up `apogee.bash:15-19` and `:40-46` as the model cut and trim. The cut is right; on 3.2 the slice at `:19` and the trim at `:45` are this bug. M37's `[powershell]` case in `lifecycle_test.cpp` is the fake-`apogee` harness to share.
  - [CLI-M36](../../../assistant/cli/MILESTONES.md#2026-11-27--grant-and-deny-flags-offer-what-they-take-maintenance-item-m36-the-permission-flags-offer-what-their-verbs-take): its corrected offers reach a 3.2 user only once this ships.
  - ADR 0007, the stubs' rules.
  - The commits that brought each site: 502e45de (v0.1.0), f046bc7b and c5d1a258.
