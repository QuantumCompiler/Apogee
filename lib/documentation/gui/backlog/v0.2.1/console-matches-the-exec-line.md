# The console splits a line as the CLI's exec line does, and Tab completes the word it was asked about

**What / why.** The console (⇧⌘K, GUI 2b) is the parity law's catch-all: any `apogee` command run as typed, as the CLI's exec line (37h) is the TUI's. Its promise is that a line typed there reaches the binary as it would from a shell or the exec line, and that Tab edits the word it asked about. The second CLI/GUI bug sweep (2026-10-31) found both promises broken in `ConsoleViewModel` (`lib/src/darwin/source/presentation/console/ViewModels/ConsoleViewModel.swift`), each confirmed against the code at HEAD:

1. **A backslash inside double quotes is dropped before any character.** In `split` (`ConsoleViewModel.swift:96-129`), the backslash branch at `:107` (`character == "\\" && quote != "'"`) runs ahead of the quote branch at `:111`, so inside double quotes every backslash escapes the character after it and is itself dropped (`:104-106`). A POSIX shell, and the CLI's own splitter that the exec line runs (`lib/src/cli/source/presentation/cli/line_tokens.cpp:52-59`), drop it only before `"`, `\`, `$` or a backtick, and keep it before anything else. So in the console, `embed query docs -- "match \d+ in C:\temp"` (or a `--require` regex, or a Windows-style path in a title) reaches the binary as `match d+ in C:temp`; at a shell or the exec line the same line passes `match \d+ in C:\temp`. Probed 2026-10-31 by compiling `split` from HEAD into a harness: `["embed", "query", "docs", "--", "match d+ in C:temp"]`. No test covers the case: the console's split test (`tests/presentation/console/ConsoleViewModelTests.swift:20-30`) has no backslash inside double quotes. Regression of GUI 2b `gui-console`, whose record says the line is "split as a shell splits it" (`MILESTONES.md:857`).
2. **A completion that lands after more typing replaces the wrong word.** `complete()` (`ConsoleViewModel.swift:169-187`) splits the input at `:170` (`guard let binary, let split = Self.words(input)`), awaits `apogee __complete` at `:176`, and then acts on whatever the input is when the answer arrives: one candidate goes to `accept` at `:183`, which splits the *current* input again (`:193`) and replaces its partial (`:194`, `input = String(input.prefix(split.partialStart)) + Self.quoted(candidate) + " "`); several become `suggestions` at `:185`. The view starts each Tab as its own `Task` (`Views/ConsoleView.swift:97-98`), so several quick presses run concurrently and their answers can arrive out of order. Scenario: type `models in`, press Tab, and keep typing ` x` before `apogee __complete models in` answers (the run loads the config, which can take a while); the single candidate `info` replaces the partial `x`, and the line becomes `models in info `. This extends GUI 4g `console-and-argv-safety`, whose "Completion edits what was typed" (`MILESTONES.md:1999`) made Tab replace the word as it was typed but did not check that the word is still the one asked about.

What already works: quotes group and an open quote runs nothing; a single-quoted backslash is kept (`quote != "'"`); a backslash outside quotes escapes the next character, as the CLI's does (`line_tokens.cpp:69-77`); a candidate is re-quoted so it splits back whole (`quoted`, `:201-206`, escaping `"` and `\`, which stays right under the fixed rule); and an answer that lands while the input is unchanged is applied as 4g built it.

**Core constraint(s).**
- **The GUI is a machine-mode client** (GUI `CLAUDE.md`): completion stays the binary's own `apogee __complete`, never a list the app carries; the split stays the app's only part of a line, and it splits as the CLI's one splitter does (`cli/line_tokens`, 37f/37h), so what a person types in the console reaches the binary as it would from the exec line.
- **The quoting table is the CLI's, exactly**: inside double quotes a backslash escapes only `"`, `\`, `$` and a backtick, and is kept before anything else; inside single quotes nothing escapes; outside quotes a backslash takes the next character; nothing expands. No further shell behaviour (no `$` expansion, no globbing) comes with it.
- **An answer applies only to the line it was asked about.** A completion whose input has changed since it was asked is dropped, never applied to a different word.
- **Presentation keeps the MVVM shape** (ADR 0002): the fix is in the SwiftUI-free view-model; the view's Tab handler is unchanged.
- **Hermetic tests** (GUI `CLAUDE.md`, Implementing a Feature): scripted fakes, no binary, no network.
- **Development runs on the dev channel** (GUI `CLAUDE.md`): the fix is proved by the hermetic suite; any live pass drives `apogee-dev`, never the release `apogee` or `~/.apogee`, and waits on M18 ([`app-dev-channel.md`](../maintenance/app-dev-channel.md)) or the user's word, as that section says.

**Seam + files.**
- `lib/src/darwin/source/presentation/console/ViewModels/ConsoleViewModel.swift`:
  - `split` (`:96-129`): inside double quotes, a backslash followed by `"`, `\`, `$` or a backtick appends that character and skips both; before any other character, or at the line's end, it is an ordinary character of the word. Outside quotes it escapes as today; inside single quotes it is literal as today. `partialStart` is unchanged (it is set where the word begins), and the doc comments on `Split`, `words` and `split` say which characters escape.
  - `complete()` (`:169-187`): the input asked about is kept beside the split at `:170`; after the await at `:176`, an answer is dropped unless `input` still equals it, before `accept` or `suggestions` is touched.
- Tests, mirroring the module (`lib/src/darwin/tests/presentation/console/`):
  - `ConsoleViewModelTests.swift` — the split test gains the CLI's quoting table (`lib/src/cli/tests/presentation/cli/line_tokens_test.cpp:32-39`), translated case for case.
  - `ConsoleSafetyTests.swift` — a stale-answer test and an out-of-order test.
  - `Scripted.swift` — a runner whose `__complete` answer the test holds and releases, so an answer can land after the input changed, and two can land in either order.
- Docs on ship: GUI `MILESTONES.md`; the Codebase Map's console line already says "a line split as a shell splits it", which becomes true.

**Reference.**
- GUI 2b `gui-console` (`MILESTONES.md:850-862`): the split and the binary's completion that finding 1 regresses.
- GUI 4g `console-and-argv-safety` (`MILESTONES.md:1962`, `:1999`): completion edits what was typed, which finding 2 extends.
- CLI 37h, the exec line, and `cli/line_tokens` (`line_tokens.cpp`; its tests in `line_tokens_test.cpp`, "double quotes unescape only their four"): the table the console matches.

**Decisions made** (dated):
- 2026-10-31: Specced from the second CLI/GUI bug sweep (the user's call: every finding becomes a v0.2.1 item in its queue, CLI track 57, GUI track 6).
- 2026-10-31: One item for both findings: they are the console's two halves of one promise (a typed line reaches the binary, and Tab edits it, exactly as the exec line does), both in `ConsoleViewModel`, proved by the same test bundle.
- 2026-10-31: The CLI's splitter is the reference, not a shell in general: the console is the exec line's counterpart, and the CLI already pins its table (`line_tokens_test.cpp:34`).

**Open calls:**
- [default: a stale answer is detected by comparing the current `input` with the one asked about (one string kept in `complete()`), rather than a request counter; two Tabs on the same input get the same answer, so whichever lands is right, and an input typed away and back is still the line the answer was for.]
- [default: a dropped answer leaves `suggestions` as they stand; clearing the offered candidates on every edit is not part of this fix.]
- [default: the Swift tests carry the CLI's quoting cases as their own table (the app cannot link the CLI's splitter, by the machine-mode rule), each row citing `line_tokens_test.cpp`.]

**Guardrail(s).**
- The split pinned on the CLI's quoting table: `"a\"b" "c\\d" "e\$f" "g\h"` gives `a"b`, `c\d`, `e$f`, `g\h`; `'a\b'` gives `a\b`; `a\ b c\'d` gives `a b`, `c'd`; and the finding's own line, `embed query docs -- "match \d+ in C:\temp"`, gives `match \d+ in C:\temp` as its last argument. `quoted` round-trips through `split` for candidates holding `\d`, `$` and a backtick.
- A held `__complete models in` answering `info`, released after the input became `models in x`: the input stays `models in x`, and `suggestions` are untouched.
- Two Tabs held and released in reverse order, the input changed between them: only the answer asked about the current input applies.
- An answer released with the input unchanged still completes, as 4g's tests pin.

**Acceptance criteria:**
- [ ] A double-quoted `\d` or `C:\temp` typed in the console reaches the binary with its backslashes, as the exec line passes it; `\"`, `\\`, `\$` and a backtick escape as the CLI's splitter has them.
- [ ] A completion that arrives after the person typed more, or after a later Tab, never edits the line; one that arrives with the line unchanged completes it as before.
- [ ] The console's tests pin both; the full darwin suite is green (`lib/scripts/cicd.sh --app darwin --test`).

**Scope note.** Earmarked for v0.2.1 (track 6); ungated. Out: further shell behaviour in the console (expansion, globbing, here-documents); clearing offered candidates as the person types; the CLI's splitter and exec line, which are already right.
