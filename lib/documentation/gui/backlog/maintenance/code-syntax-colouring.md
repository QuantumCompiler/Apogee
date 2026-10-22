# Code blocks coloured by syntax

**What / why.** Since M8 (2026-10-22) a chat answer's fenced code draws as the design draws it, with one gap that item recorded and left: the code is a single colour. M8's code block draws `Text(verbatim: code.text)` in the mono face on `Palette.code` (`lib/src/darwin/source/presentation/styles/Components/MarkdownText.swift:222–223`). The design colours it, read 2026-10-22:
- **Five code colours,** in all three themes, as `json/tokens.json` holds them and `ColorTokens` lists them ("def, async, return", "Quoted text in code", "Notes after #", "10, 0.5, 0x1F", "run_batch, create_task"):

  | Token | Light | Dark | Contrast |
  |---|---|---|---|
  | `code.keyword` | `#7A3FC0` | `#C3A6F0` | `#D8B9FF` |
  | `code.string` | `#2F7A1F` | `#A5D68E` | `#B8F0A0` |
  | `code.comment` | `#67686A` | `#8C887F` | `#B5B5B5` |
  | `code.number` | `#8F5A00` | `#E5B567` | `#FFC04D` |
  | `code.function` | `#1F5FA8` | `#8EC3F0` | `#9ED4FF` |

  The prototype marks code up with them (`lib/design/spec/html/main.html:18`: `.k`, `.s`, `.c`, `.n`, `.f`; its Python answer at `:318–327`).
- **A diff block** colours its added lines `status.accent` and its context `text-tertiary` (`main.html:414–424`, "diff · src/ingest/runner.py"). The app's permission-card diff already colours `+` `Palette.local` and `-` `Palette.error` (`presentation/session/Views/SessionExtras.swift:258–259`).
- **The app's `Palette` carries none of the five** (`presentation/styles/Palette.swift:91–117`). Only `surface.code` (`:97`).

Every pair clears the design's 4.5:1 rule on `surface.code` in each theme (computed 2026-10-22; the lowest is `code.string` in light at 4.81:1):

| Token | Light | Dark | Contrast |
|---|---|---|---|
| keyword | 5.72 | 9.10 | 11.60 |
| string | 4.81 | 11.44 | 15.09 |
| comment | 5.02 | 5.39 | 9.66 |
| number | 5.20 | 10.10 | 12.16 |
| function | 5.79 | 10.17 | 12.55 |

**Core constraint(s).**
- **No package, no linked library** (the darwin layering check fails any). The colouring is a small lexer of the app's own, Foundation only, beside M8's `MarkdownDocument`, in `styles` (ADR 0002). It is not a parser: it finds keywords, strings, comments, numbers and the names a call or definition introduces, never a tree.
- **Every colour a token:** the five join `Palette` as `ColorToken`s with the design's three values, pinned by the palette's tests as the others are, so Settings › Appearance still changes everything.
- **Honest by default.** A fence colours only for a language it names that the lexer knows. An unnamed fence, or a language it doesn't know, draws in one colour as today, never guessed.
- **Streaming stays smooth.** The lexer is linear and runs where M8's parse runs, once per redraw. An open fence colours as it grows, and an unterminated string or block comment at the edge colours to the end without reshaping what came before.
- **The code itself is unchanged:** selection and the copy button take the plain text, with no colour or markup.
- **ADR 0004:** previews of coloured blocks over recorded answers, in each theme.

**Seam + files.**
- **`presentation/styles/Palette.swift`:** `codeKeyword`, `codeString`, `codeComment`, `codeNumber`, `codeFunction` tokens and colours, in `Palette.tokens`.
- **`presentation/styles/Components/CodeLexer.swift`** (new, Foundation only). Languages are a table of rules: keyword sets, line and block comment markers, string delimiters (escapes, triple quotes, template and raw strings where the language has them), number forms, and the call or definition name. Its output is spans over the code's text.
- **`presentation/styles/Components/MarkdownText.swift`:** the code block draws its spans in the five colours. A `diff` or `patch` fence colours `+` lines `Palette.accent`, `-` lines `Palette.error` and `@@` hunk headers `Palette.textTertiary`.
- **Tests (`tests/presentation/styles/`):**
  - each language's lexer over a sample with every span kind;
  - aliases resolving;
  - an unknown or unnamed fence uncoloured;
  - unterminated strings and comments at the stream's edge;
  - each code token's 4.5:1 ratio on `surface.code` in each theme;
  - copy taking the plain text;
  - the five tokens pinned to the design's values.
- **Fixtures:** a recorded answer from the scripted mock carrying fences in several languages and a diff, recorded as M8's was (`markdown-answer.jsonl`'s way).

**Reference.** M8 (`MarkdownDocument`, `MarkdownText`, its streaming edge rules, its recording). The design's code tokens (`json/tokens.json`, `ColorTokens.dc.html`) and its answer and diff figures (`main.html:300–345`, `:414–424`). The permission card's `DiffView` colours.

**Decisions made** (dated):
- 2026-10-22 — **Opened from M8's record** (the user's call: "spec the syntax colouring for code blocks"). M8 left the design's code colours out by its own open call.
- 2026-10-22 — **A lexer of the app's own,** since the layering check forbids a package. Its scope is the design's five colours, which a token-level lexer serves fully.

**Open calls:**
- [default: the languages are Python, Swift, JavaScript and TypeScript (with `jsx`/`tsx`), C and C++ and Objective-C, Rust, Go, Java, Kotlin, C#, Ruby, shell (`sh`, `bash`, `zsh`), SQL, JSON, YAML and TOML, with their common aliases (`py`, `js`, `ts`, `c++`, `cpp`, `objc`, `rs`, `kt`, `cs`, `rb`, `yml`). Any other language is uncoloured until added.]
- [default: no language is guessed from the code. An unnamed fence stays one colour.]
- [default: a `diff` or `patch` fence colours by line (`+` accent, `-` error, `@@` tertiary), as the design and the permission card's diff do, rather than by the language of the file it patches.]
- [default: inline code (single backticks) stays uncoloured, as the design draws it.]

**Guardrail(s).**
- **Each language's sample** colours every span kind it has: keywords, strings, comments, numbers and function names. The lexer's spans are pinned.
- **A plain, unnamed or unknown fence** draws exactly as M8 draws it.
- **Contrast:** every code token meets 4.5:1 on `surface.code` in light, dark and contrast, by test.
- **Streaming:** each prefix of the recorded answer colours without reshaping settled lines, and an unterminated string at the edge colours to the end.

**Acceptance criteria:**
- [ ] A fenced Python, Swift, JavaScript, Rust or shell block in a chat answer is coloured as the design colours code, live and resumed, in all three themes.
- [ ] A diff block colours its added, removed and hunk lines.
- [ ] An unnamed or unknown fence reads as today, and copy gives the plain code.
- [ ] Previews, `check-previews`, the layering check and `lib/scripts/cicd.sh --test` pass, with no Swift warning.

**Scope note.** GUI Maintenance, claimable by name, no gate (M8 has shipped). Colouring code elsewhere (the permission card's diff body, Review's findings, the console) can reuse the lexer later, unasked. Semantic colouring (types, variables, a real parse) is out of scope by the design's five colours.
