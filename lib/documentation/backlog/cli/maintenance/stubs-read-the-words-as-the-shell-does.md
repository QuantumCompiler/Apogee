# CLI-M42 · The completion stubs read the words as the shell does

| Field | Value |
|---|---|
| **Type** | 🐞 Bug |
| **Project** | CLI |
| **Status** | 🔒 Blocked |
| **Fix version** | Maintenance (any time) |
| **Component** | Tab completion › the four stubs (`completions/_apogee`, `apogee.bash`, `apogee.fish`, `apogee.ps1`) |
| **Labels** | `completion` `bash` `zsh` `fish` `powershell` `quoting` |
| **Epic** | — (maintenance has no track) |
| **Blocked by** | [M41](bash-3-2-completes-every-word.md) |
| **Blocks** | — |
| **Reporter** | CLI-M37's probes (2026-11-22), then a sandboxed investigation and a skeptic's re-run the same day |
| **Created** | 2026-11-22 |

---

## Summary

Every completion stub hands `__complete` each word as it was typed, quote characters and backslashes included. The protocol matches words literally. So a quoted verb isn't a verb, a quoted value a later list reads isn't found, and a word typed inside an open quote completes nothing. bash also splits every quoted value with spaces into several words, and inserts candidates raw, so a name with a space or `&` lands as two words or a background job. The item makes each stub send the words the command will receive, with one level of its own shell's quoting removed and nothing expanded, and insert each candidate quoted as that shell reads it. The protocol stays as it is.

## Description

### User story

**As** someone who quotes a value or a name on an Apogee command line, in any of the four shells,
**I want** Tab to read the line as the shell will run it,
**so that** `apogee chat -m 'cl<TAB>` completes the backend, `models quantize 'org--m' --from <TAB>` finds the model's GGUFs, and a name with a space is inserted so it runs as one word.

### Steps to reproduce

The sandbox, 2026-11-22, with the built binary `apogee 0.2.0 (33ea685d, 2026-11-22, macos-arm64)`: a throwaway `APOGEE_HOME` and `HOME`, `env -i`, stdin from `/dev/null`. It has three mock backends, a stand-in GGUF at `models/org--m/gguf/222222222222/`, and a collection with a space made by the real verb (`embed ingest "my notes" "docs/a note.txt"`). `tools.allowed_hosts` was hand-written to hold `*.example.com` and `https://x.test/a?b=1&c=2`. Real `zsh -f -i` (5.9) and `bash --norc --noprofile -i` (5.3.9 and 3.2.57) are driven through zpty with the repo's stubs, and a shim logs each `__complete` argv.

1. In zsh and bash, type `apogee 'models' ` and press Tab.
2. Type `apogee chat -m 'cl` and press Tab.
3. Type `apogee models quantize 'org--m' --from ` and press Tab.
4. In bash, type `apogee embed query "my notes" ` and press Tab.
5. In bash, type `apogee embed query my` and press Tab, then Enter.
6. In bash, type `apogee config delete-allowed-host ht` and press Tab.

### Actual result

1. Both send `['models'] []` and get the root flags; the line becomes `apogee 'models' -`.
2. zsh sends `['cl]` (its own `PREFIX` was `cl`, with `compstate[quote]` `'`), and bash sends `['cl]` (its `$2` was `cl`). Nothing is offered.
3. Both send `['org--m']` and get `:hint --from GGUF_ID`. Unquoted, the same line offers `222222222222`.
4. bash sends `["my] [notes"] []`, gets `embed query`'s flags, and inserts `-` where the query text belongs.
5. bash inserts `apogee embed query my notes `, which runs as collection `my`: `apogee embed: no collection named 'my'`.
6. bash inserts `https://x.test/a?b=1&c=2 ` raw. Run, that backgrounds `config delete-allowed-host https://x.test/a?b=1` and sets `c=2`.

### Expected result

1. `[models] []`, and models' verbs offered.
2. `[chat] [-m] [cl]`, and `claudy` inserted as `'claudy' `.
3. `[models] [quantize] [org--m] [--from] []`, and `222222222222` offered.
4. `[embed] [query] [my notes] []`: the `text TEXT` hint, nothing inserted.
5. `apogee embed query my\ notes `.
6. `https://x.test/a\?b=1\&c=2 ` (zsh already inserts it so).

### Background

Each stub forwards the text of each word, not its value, and only the stub knows its shell's quoting:
- **zsh** (`_apogee:10`) sends compsys' `words`, which hold the line's syntax (`'models'`, `my\ notes`, `'cl`). compsys already holds the value under the cursor in `PREFIX`, `SUFFIX` and `compstate[quote]`. Its insertion is already right: `compadd -- $lines` (`:20`), with no `-Q`, quotes for the line.
- **bash** (`apogee.bash:15-19`) splits `COMP_LINE` with `IFS=$' \t' read -r -a`, which knows no quotes. That came with c5d1a258 (2026-09-24), which left `COMP_WORDS` to stop the `:` split. Raw insertion (`:39`) and the unquoted `$(…)` read date from v0.1.0. (CLI-M41 replaces that read with a glob-free `read`.)
- **fish** (`apogee.fish:3`, `:6`) takes the context from `commandline -opc`, which is tokenized and probably unescaped. The current token, from `commandline -ct`, is raw, and goes to `__fish_complete_path` raw too (`:17`).
- **PowerShell** (`apogee.ps1:7`, `:10`) sends each element's `Extent.Text`, quotes included, and returns the raw candidate as CompletionText.

The protocol is literal by contract (`complete_protocol.h:33-37`, `complete_protocol.cpp:557-672`), and it should stay literal. The binary can't know which grammar a word was quoted in (zsh's `$'…'`, POSIX's `'it'\''s'`, PowerShell's `'it''s'` and backtick, fish's escapes). Unquoting there would double-unquote under a fixed stub, and mangle a real quote character (`--system "it's"`).

CLI-M37's Decision log left quotes alone in PowerShell for parity with the other stubs. This item is that parity, done in all four.

### Evidence

All measured 2026-11-22 in zsh 5.9, bash 5.3.9 and bash 3.2.57 by the investigator, then again by a skeptic with its own sandbox. fish and PowerShell are not on the host, so they are by reading.

1. **The protocol matches literally**: `['models'] []` gives the root flags, `[models] []` the verbs; `['cl]` gives nothing, `[cl]` gives `claudy`. The walk compares words as given (`complete_protocol.cpp:557`, `:561`, `:569`), and sources look them up as given (`complete_sources.cpp:247`).
2. **What breaks**, ordered from everyday to rare:
   - **Typing inside an open quote completes nothing, in every POSIX stub.** Measured: `-m 'cl`, `--type 'cl`, `embed query 'my no`, `models pull 'll`, `config delete-allowed-host '*.ex`. Path words are spared in zsh and bash, which hand them to the shell's own file completion. They are not spared in fish, whose `:files` branch gets the raw token.
   - **A quoted or escaped value that a later list reads is not found.** Measured: `models quantize|convert '<model>' --from` (`complete_sources.cpp:241-247`). By reading, the same holds for a drafter keyed on a quoted `--model-path` (`:694-697`), knowledge records keyed on `--db` (`:767-770`), the `--mlx` voice and video sets (`:293-323`), and `--custom '<path>'` (`complete_protocol.cpp:521-526`). Paths with spaces are ordinary on macOS.
   - **bash splits every value with blanks.** `--system 'Be brief'` arrives as `['Be] [brief']`, and `task run "Write the report"` as three words. `embed query my\ notes ` sends `[my\] [notes] []` and inserts `-`.
   - **A quoted verb or flag is not seen**: `apogee 'models' ` inserts `-`. This is rare.
   - **bash inserts candidates raw.** `my notes` lands as two words and runs as collection `my`. The host `https://x.test/a?b=1&c=2` lands with a bare `&`. The unquoted `$(…)` at `apogee.bash:26` turns `*.example.com` into `x.example.com` in a cwd holding that file. The harm is real but rare: a collection with a space is itself half-made (Out of scope), and `add-allowed-host` refuses a URL or a pattern, so such hosts are hand edits.
3. **What already works**, and must stay:
   - the common shape, a quoted free-text value followed by a flag, in zsh and bash 5.3: `--system 'Be brief' -m <TAB>` gives backends, and `task run "Write the report" --agent co` inserts `code-review `;
   - nothing is evaluated: `--system "$(touch EVALUATED)"` stayed text in zsh and both bashes, and the file was never created;
   - zsh's insertion (`my\ notes `, `'my notes' ` inside a quote, `\*.example.com `);
   - bash's `:` trim on an unquoted word (`models pull llama3.2:` gives `llama3.2:3b `);
   - fish's insertion, which fish quotes itself.
4. **The fix, proven in scratch copies** against real zsh and both bashes. Every row in Testing gives the right argv and buffer on zsh 5.9, bash 5.3.9 and bash 3.2.57.
   - **zsh** sends `"${(@Q)words[2,CURRENT-1]}"` plus `${(Q)${:-$q$PREFIX$SUFFIX${q#\$}}}` (`q=$compstate[quote]`). `(Q)` removes one level and never evaluates: `"$(touch …)"`, `$HOME` and `~/x` stay literal, `'it'\''s'` becomes `it's`, and `$'a\tb'` becomes a tab. The skeptic ran 32 rows, `$'cl` and `'*.ex` among them.
   - **bash** uses an eval-free tokenizer over `COMP_LINE` up to `COMP_POINT`, quotes each candidate for where it lands, and applies the `:` trim. It was identical on 3.2 and 5.3.
5. **One regression the skeptic found in the bash prototype.** Its `:` trim was keyed on whether a quote was open. For `models pull llama3.2\:<TAB>`, readline's `$2` is the whole `llama3.2\:`. The prototype trimmed to `3b`, and the line became `apogee models pull 3b `, destroying the typed word; today's stub leaves the line unchanged. The trim must remove only what precedes readline's own `$2`, the text it will replace.
6. **The binary already has the rules.** `line_tokens.h:4-11` is "the one splitter of a typed command line": single quotes literal; double quotes unescape `\"`, `\\`, `\$` and `` \` ``; a backslash takes the next character; nothing expanded. The bash prototype matched it. `line_tokens` refuses an open quote, and the stub needs one, so the stub can't call it.
7. **The gate's zsh stand-in would hide the change.** `shell_completion_check.sh:152-171` sets only `words` and `CURRENT`. With the new stub, `chat -m cl` there offers all three backends, so the row passes without its partial honoured. `models pull --s` then fails.
8. **zsh's own behaviour after a closed quote**: `apogee chat -m 'claudy'▮<TAB>` gives `'claudy '`, which a control with a plain `compadd` also gives. It is not a stub defect, and the fixture must not pin it.
9. **The binary bent around the gap**: chat names with a space are not offered (`complete_sources.cpp:401-402`, "A name with a space cannot be one word on a command line").
10. **Not pinned.** `cli.shell_completion` has no quoted row, its zsh stand-in fakes `words` and `CURRENT` only, and it ran green on 2026-11-22.

## Acceptance criteria

- [ ] **Given** zsh, bash 5 or bash 3.2, **when** each line in Testing's table is completed, **then** the stub sends the argv in its row, and the buffer after Tab is the one in its row.
- [ ] **Given** a word inside an open `'` or `"`, **when** Tab completes it, **then** the candidate is offered and inserted inside that quote (`apogee chat -m 'claudy' `).
- [ ] **Given** a candidate holding a space, `&`, `?` or `*`, **when** bash or zsh inserts it bare, **then** it is escaped so the line runs it as one word (`my\ notes`, `\*.example.com`).
- [ ] **Given** `models pull llama3.2\:<TAB>` in bash, **then** the typed word is kept, never replaced by `3b`.
- [ ] **Given** `--system "$(touch <file>)"` on the line, **when** Tab is pressed in any stub, **then** the argv holds `$(touch <file>)` literally and the file is not created.
- [ ] **Given** fish or PowerShell on a host that has it, **then** the quoted rows in Testing pass (`'models'`, `--system 'Be brief'`, `-m 'cl`, a candidate with a space), and a fish `:files` word inside a quote completes the path.
- [ ] **Given** an old binary with the new stubs, and the reverse, **then** each behaves as today on an unquoted line.

## Constraints

- **The stub owns its shell's quoting, and still knows nothing** ([ADR 0007](../../../adrs/cli/tab-completion.md)). It may read its own shell's words and quote a candidate for its own line. It may never hold, filter or invent a candidate.
- **One level of quoting removed, nothing expanded.** No stub evaluates or expands a word: `$(…)`, `$VAR`, `~` and globs stay literal. No `eval`. That also rules out bash-completion's `_comp_dequote`, which evals.
- **The protocol is untouched** ([ADR 0006](../../../adrs/cli/backwards-compatibility.md)). Every pairing of a stub and a binary keeps working.
- **bash 3.2 compatible**, building on CLI-M41's lines: no quoted array slice under an IFS without a space, and no `compopt`, `mapfile` or `-o filenames`. `-o filenames` would also append `/` to a name matching a directory in the cwd.
- **The bash tokenizer follows `line_tokens.h`'s rules**, plus an open quote recorded for the word under the cursor. One shared table pins both.

## Technical approach

Paths under `lib/src/cli/`.

| File | Change |
|---|---|
| `completions/_apogee` | `:10`: send `"${(@Q)words[2,CURRENT-1]}"` plus the current word `${(Q)${:-$q$PREFIX$SUFFIX${q#\$}}}`, with `q=$compstate[quote]`. Rewrapping is needed because `(Q)` leaves an unbalanced `'cl` as it is. The current word stays whole (`PREFIX` and `SUFFIX`), as today. `compadd -- $lines` stays, with no `-Q`. A comment says why: compsys' words are the line's syntax, and `PREFIX` and `compstate` hold the value. |
| `completions/apogee.bash` | An eval-free `__apogee_words` tokenizer over `COMP_LINE[0:COMP_POINT]`, in place of `read -r -a` (`:15-19`). Unquoted blanks split words; `'…'` is literal; `"…"` unescapes `\"`, `\\`, `\$` and `` \` ``; a backslash outside quotes takes the next character; the last word is the one under the cursor (empty after a blank), and its open quote is recorded. `$'…'` opens a quote that ends at `'`, honouring `\'` and `\\`, with other escapes kept. Words are built by appending, never by a quoted slice. Each candidate is quoted for where it lands: inside `'`, `'` becomes `'\''`; inside `"`, `\`, `"`, `$` and `` ` `` are escaped; bare, `printf -v c %q` only when it holds a character outside `[A-Za-z0-9._/:=,@%+-]` (`%q` escapes `,`, which `vision,utility` must keep). The `:` trim (`:43-46`) removes from each candidate what the typed word holds before readline's `$2`, keyed on `$2`, never on the quote state. |
| `completions/apogee.fish` | `:6`: the current token unescaped (`commandline -ot`, with `""` for a fresh word), in the `__complete` call and in the `:files` branch (`:17`). `commandline -opc` stays for the context. Candidates stay raw, since fish quotes them on insertion. If `-ot` fails on fish 3.x or 4.x, use `commandline -ct \| string unescape` with the closing quote appended first. |
| `completions/apogee.ps1` | On CLI-M37's element walk: each element sends `.Value` when it is a `StringConstantExpressionAst` or an `ExpandableStringExpressionAst` (no variables replaced, in the 5.1 and 7.x SDKs), else `Extent.Text`. The element at the cursor, after M37's cut, drops an opening `'` or `"` and undoubles `''`. M37's comma trim skips that element when it is a string, which PowerShell replaces whole; the guard stays, and M37's `quoted` row (`'vision,ut'`, answered `'vision,utility`) changes with this contract. CompletionText is `'…'` with `'` doubled when the candidate holds whitespace or `` ` $ & ; , ( ) { } @ # \| < > ' " ``, or when the typed word opened a quote. ListItemText stays the bare name. |
| `tests/scripts/sh/shell_completion_check.sh` | A real-line-editor section (see Testing). The zsh stand-in (`:152-171`) gains `PREFIX=$words[CURRENT] SUFFIX=` and `typeset -A compstate`, and each partial-word row also checks that the partial was honoured (`local` absent), not only that the needle is present. The fish section gains the quoted rows. |
| `tests/presentation/cli/lifecycle_test.cpp` | CLI-M37's `pwsh`-gated `TabExpansion2` case gains the quoted rows. With the fake `apogee` answering `my notes`, the CompletionText is `'my notes'`, and with `it's` it is `'it''s'`. A table case runs one list of closed lines through `line_tokens` and through the bash tokenizer (sourced in bash) and compares the words. It SKIPs where no bash is found or no child can start. |

**Reference.** `line_tokens.h`, the binary's own splitter, whose rules the bash tokenizer takes. zsh's `(Q)` flag and compsys' `PREFIX`, `SUFFIX` and `compstate[quote]`. CLI-M41's default IFS and glob-free `read`, which this item keeps. cobra's stubs evaluate a word to dequote it; ours never have, and the measurement shows why that matters.

**Docs when it ships:**
- CLAUDE.md's completion rules and DEVELOPER.md's `cli.shell_completion` paragraph (`:998`): a stub reads its own shell's quoting and quotes a candidate for its own line, still holding none. The quoted-line table and the real-line-editor section.
- MILESTONES: a dated entry.

## Testing

The bug becomes a quoted-line table, run through the real line editors so the argv sent and the buffer after Tab are both checked.

**The real-line-editor section** in `shell_completion_check.sh` drives zsh `-f` with `compinit`, PATH's bash and `/bin/bash` when it is another version, through zsh's zpty, with a deadline inside each read loop. A logging shim on PATH records the argv. A zsh widget logs `$BUFFER`, bash 4 and later use `bind -x` with `READLINE_LINE`, and on bash 3.2 the line is run through a printing function. The home is the check's own, plus a collection named `my notes`, `tools.allowed_hosts` holding `*.example.com` and `https://x.test/a?b=1&c=2` (hand-written, as CLI-M40's tests write one), and a cwd holding `x.example.com`.

| # | Line, then Tab | Argv sent | Buffer after |
|---|---|---|---|
| 1 | `apogee 'models' pu` (and `"models"`) | `[models] [pu]` | `apogee 'models' pull ` |
| 2 | `apogee chat --system 'Be brief' -m cl` | `[chat] [--system] [Be brief] [-m] [cl]` | `… -m claudy ` |
| 3 | `apogee chat -m 'cl` (and `"cl`) | `[chat] [-m] [cl]` | `apogee chat -m 'claudy' ` |
| 4 | `apogee models quantize 'org--m' --from 2` | `[models] [quantize] [org--m] [--from] [2]` | `… --from 222222222222 ` |
| 5 | `apogee embed query my\ notes ` and `… "my notes" ` | `[embed] [query] [my notes] []` | unchanged, no `-` |
| 6 | `apogee embed query my` | `[embed] [query] [my]` | `apogee embed query my\ notes ` |
| 7 | `apogee embed query 'my no` | `[embed] [query] [my no]` | `apogee embed query 'my notes' ` |
| 8 | `apogee chat --system a\\b -m cl` | `[…] [a\b] […]` | `… -m claudy ` |
| 9 | `apogee chat -m "a\"b` | `[chat] [-m] [a"b]` | unchanged |
| 10 | `apogee chat --system "$(touch $work/EVALUATED)" -m cl` | `$(touch …)` literal | `$work/EVALUATED` does not exist |
| 11 | `apogee task run "Write the report" --agent co` | `[task] [run] [Write the report] [--agent] [co]` | `… --agent code-review ` |
| 12 | bash: `apogee models pull llama3.2:` | `[models] [pull] [llama3.2:]` | `… llama3.2:3b ` |
| 13 | bash: `apogee models pull 'llama3.2:` | `[models] [pull] [llama3.2:]` | `… 'llama3.2:3b' ` |
| 14 | bash: `apogee models pull llama3.2\:` | `[models] [pull] [llama3.2:]` | the typed word kept, never `… 3b ` |
| 15 | `apogee config delete-allowed-host ht` | `[config] [delete-allowed-host] [ht]` | `… https://x.test/a\?b=1\&c=2 ` |
| 16 | `apogee config delete-allowed-host *.ex` (cwd holds `x.example.com`) | `[…] [*.ex]` | `… \*.example.com ` |

**Failing today** (2026-11-22): rows 1-4 and 7-9 in zsh; rows 1-9 and 12-16 in bash 5.3 and 3.2. Rows 5, 6, 15 and 16 already pass in zsh. Rows 10 and 11 pass today and guard against regression.

- **The zsh stand-in** keeps its 25 rows running against the new stub, with `PREFIX`, `SUFFIX` and `compstate` set and the partial checked.
- **The shared tokenizer table**: closed lines (`a 'b c' d`, `"x\"y"`, `a\ b`, `''`, `"$HOME"`) give the same words from `line_tokens` and from the bash tokenizer.
- **fish**: rows 1-3, 6 and 7 through `complete -C`, and an open-quoted path, skipped where fish is absent.
- **PowerShell**: in CLI-M37's case, under both Standard and Legacy passing. `'models' ` gives `[models] []`, `--system 'Be brief' -m ` gives `[chat] [--system] [Be brief] [-m] []`, `-m 'cl` gives `[chat] [-m] [cl]`, and the two CompletionText rows above. Skipped where `pwsh` is absent.
- **Gate:** `lib/scripts/cicd.sh --app cli --test`.

## Open questions

| Question | Status | Answer |
|---|---|---|
| Does "a stub reads its own shell's quoting" need a new ADR beside ADR 0007? | `[default]` | No. 0007's "stubs know nothing" governs candidates: a stub that removes its own shell's quoting still only calls `__complete` and holds no candidate. The reading goes into CLAUDE.md's completion rules and DEVELOPER.md. If it is read as a changed decision, the record is a new ADR naming 0007, never an edit to it (ADRs are append-only). |
| Should a stub expand `$VAR`, `~` or globs in the words it sends? | `[default]` | No: one level of quote removal only, the same in every stub. Expansion needs `eval`, which would run `$(…)` on Tab. |
| zsh: the whole word under the cursor (`PREFIX` plus `SUFFIX`, as today) or cut at the cursor? | `[default]` | The whole word, as today. This item changes quoting only, and `compadd` matches `PREFIX` and `SUFFIX` itself. |
| bash: quote candidates by hand, or switch to `-o filenames`? | `[default]` | By hand. bash 3.2 has no `compopt`, and `-o filenames` appends `/` to a name that matches a directory in the cwd. The cost: an ambiguous list is shown in its quoted form (`my\ notes`). |
| fish: which call gives the current token unescaped, open quote included, on fish 3.x and 4.x? | `[default]` | `commandline -ot`, with `""` for a fresh word. The builder proves it on a host with fish. Failing that, `commandline -ct \| string unescape` with the closing quote appended. |
| PowerShell's half: here, or folded into CLI-M37? | `[default]` | Here, on top of M37's element walk, so M37 stays the cursor fix it was specced as, and the quote contract is written once for four stubs. This item is gated on M37 for it. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-22 | Specced from CLI-M37's probes, a sandboxed investigation and a skeptic's re-run (Evidence). Placed in **Maintenance** as M42. | the spec |
| 2026-11-22 | **The fix lives in the stubs, and the protocol stays literal.** Only a stub knows its shell's grammar. The binary's `line_tokens` refuses an open quote, and a new line-mode argument would break older binaries (ADR 0006). | the spec |
| 2026-11-22 | **Gated on CLI-M41 and CLI-M37.** M41 rewrites bash's `:8` and `:26`, and this item's bash tokenizer builds on them and must pass M41's `/bin/bash` lane. M37 rewrites PowerShell's element loop and builds the `pwsh` harness this item extends. | the spec |
| 2026-11-27 | **CLI-M37 shipped**, so this item waits on CLI-M41 alone. Its walk records the element at the cursor (`$typing`), and its comma trim skips a string element, which PowerShell replaces whole: this item keeps that guard when it strips the quote. Its `[powershell]` case in `lifecycle_test.cpp` is the harness this item extends. | CLI-M37's ship |
| 2026-11-22 | **The bash `:` trim is keyed on readline's `$2`**, after the skeptic measured that a quote-keyed trim destroys `llama3.2\:` (Evidence 5). | the spec |

## Out of scope

- **Offering chat names with a space** (`complete_sources.cpp:401-402`). That is a follow-up once all four stubs quote. Until then, a spaced name would still break in any stub not yet fixed.
- **The half-made spaced collection.** `embed ingest "my notes" <file>` writes and offers `embeddings/my notes.db`, refuses to register `my notes` in the config, and exits 0: [CLI-M46](a-collection-the-config-cannot-hold-is-refused-before-it-is-written.md), with the read side's rule in [CLI-M45](a-collection-is-named-not-pathed-at-every-reader.md).
- **macOS bash 3.2's one-word argv**: [CLI-M41](bash-3-2-completes-every-word.md).
- **PowerShell's cursor**: [CLI-M37](../../../assistant/cli/MILESTONES.md#2026-11-27--powershell-completes-the-word-at-the-cursor-maintenance-item-m37-a-tab-completes-the-word-at-the-cursor-on-every-powershell), shipped 2026-11-27.

## Links

- **Blocked by:** [CLI-M41](bash-3-2-completes-every-word.md) · **Blocks:** —
- **Relates to:**
  - [CLI-M36](../../../assistant/cli/MILESTONES.md#2026-11-27--grant-and-deny-flags-offer-what-they-take-maintenance-item-m36-the-permission-flags-offer-what-their-verbs-take): its rule that "the line is read up to the cursor" gains "and read as the shell reads it". It does not depend on this item.
  - [CLI-M40](delete-allowed-host-removes-what-the-list-holds.md): `delete-allowed-host` offers entries as written, and this item's bash quoting is what makes inserting `*.example.com` or a raw URL safe there (zsh already is).
  - [CLI-M48](a-suggested-command-runs-as-printed.md), which quotes a printed command as the shell reads it: the same POSIX and PowerShell facts, on the binary's side rather than the stubs'.
  - ADR 0007, the stubs' rules; `line_tokens.h` (37f), the binary's splitter.
