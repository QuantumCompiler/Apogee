# The console and every argv are safe: carve-outs, doors, quoting and `--`

**What / why.** ADR 0003 makes the console (2b) the app's catch-all, and every screen assembles argv for the binary. The CLI/GUI audit (2026-10-22, three-lens verified, plus one completeness-pass lead with its own probe) found five ways a command runs that shouldn't, or runs the wrong command:

1. **The console runs carved-out commands when a root option precedes them** (high). The gate checks only the first word against `ParityTable.refusal(of:)` (`ConsoleViewModel.swift:155`, `:163`; `Parity.swift:84-87` matches the exact top-level word). The binary accepts root options (`--config`, `--release`, `--dev`, `--test`, `--custom`) before the subcommand, so `--release serve`, `--dev reset -y` or `--release uninstall -y` bypass the refusal and start exactly what the table carves out. With `-y`, reset and uninstall proceed unprompted, since `SupervisedJob` closes the child's stdin (`:29-30`). The recorded intent (GUI MILESTONES:837) is "the three carve-outs refused by their reasons with nothing started".
2. **The console lacks the exec line's door and `$EDITOR` refusals.** The TUI's exec line also refuses `chat`, `execute` and `tui` with a redirect, and `agents edit`/`symphonies edit` because `$EDITOR` needs a terminal (`tui_parity.cpp:88-110`). In the console, `chat` reads EOF on its closed stdin and ends silently with "Done.", and `agents edit <name>` runs `$EDITOR` (default vi) through `run_foreground` with no terminal (`agents_cmd.cpp:249-260`). A first-word lookup cannot express the two-word refusals. 2a's record scoped refusals to the carve-outs without considering interactive commands.
3. **Free text passed as a bare argument is read as an option** (completeness pass, with probe; reproduce first). `video compose "-m cats brief" -o b.mp4` took the brief as `-m` and answered "there is nothing to compose". `voice say "--draft"` answered "text is required". `voice say -o x -- "-a cat"` works, so the CLI supports `--`. The affected call sites are `ComposeViewModel.swift:132`, `VoiceViewModel.swift:203`, `ClipForm.swift:53-62`, `TasksViewModel.swift:319,352` (`task run` goal), `GraphViewModel.swift:103` (`graph query`) and `SessionLibraryViewModel.swift:489` (`chats title`). One catch: `ReadRunner.read` appends `--output-format json` at the end, so a `--` before the text would swallow that flag too.
4. **A bare `apogee` in the console answers "A quote is left open."** `arguments()` strips the leading word (`:104-109`), so the guard at `:155-158` sees no command and reports an open quote.
5. **Tab completion corrupts the line when the current word was quoted or escaped.** `accept()` removes `split.partial.count` characters (`:136-142`), but `words()` drops quotes and backslashes from the partial (`:72-101`). `models info "My Mo"` + Tab turned into `models info "M"My Model" ` (refused); `My\ Mo` became `M"My Model" ` (runs with the wrong argument `MMy Model`).

**Core constraint(s).** **The console refuses what the table refuses, however it is typed**: refusal checks the parsed subcommand path after root options (the binary's own grammar: root options, then the command words), covering the carve-outs and the exec line's door and editor refusals, each with the table's recorded reason. **One refusal table** (ADR 0003): the console's refusals and the TUI's are the same set, grown in `ParityTable`, never a second list. **Free text is never an option**: every argv builder places user text after `--`, and the format flag the read runner adds goes before it. **Completion edits what was typed**: replacement spans are measured in typed characters. ADR 0002; pure argv and parse tests.

**Seam + files.** `presentation/console/ViewModels/ConsoleViewModel.swift` (parse root options then the command path; empty-line message; typed-span completion), `presentation/shell/ViewModels/Parity.swift` (the door and editor refusals added, two-word entries), `business/reads/ReadRunner.swift` (the format flag before `--`), each listed call site (text after `--`). Tests: the refusal matrix with every root option in front; `chat`/`agents edit` refused with their reasons; each call site with dash-leading text, argv pinned; bare `apogee`; completion over quoted and escaped words.

**Reference.** GUI ADR 0003 and 2b's console; the TUI's refusals (`tui_parity.cpp`, ADR 0010), the reference set; the CLI's `--` handling (verified by probe); the CLI/GUI audit (2026-10-22).

**Decisions made** (dated):
- 2026-10-22: Specced from the CLI/GUI audit (the user's call). The `--` finding came from the completeness pass, reproduced first.

**Open calls:**
- [default: the console mirrors the TUI exec line's refusal set exactly. The two-word `edit` refusals stand until the GUI's native editor (2e) is offered as the redirect.]

**Guardrail(s).** The refusal matrix over parsed commands; argv pins per call site; completion cases.

**Acceptance criteria:**
- [ ] No carved-out, door or editor command starts from the console, whatever root options precede it; each refusal names its reason.
- [ ] No user text is ever read as an option; bare `apogee` gets a fitting message; completion never corrupts the line.
- [ ] The full suite is green.

**Scope note.** Earmarked for v0.2.0 (track 4); ungated.
