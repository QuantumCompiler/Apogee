# Markdown and code rendering keep every line the model wrote

**What / why.** M8 and M9 (shipped 2026-10-22) render chat answers as Markdown and colour code. The CLI/GUI audit (2026-10-22, three-lens verified, each probed against the real sources compiled standalone) found five places where rendered output loses text the model wrote:

1. **Reference-style link definitions vanish.** Foundation's `AttributedString(markdown:)` treats `[1]: https://…` as link metadata, so `"Sources:\n\n[1]: https://a.example/docs\n[2]: https://b.example/guide"` renders only `Sources:`. While streaming, the line shows as `[2`, then `[2]:`, then disappears as the URL arrives. The CLI prints these lines as text (`markdown/inline.cpp`'s `link()` requires the inline-link form, a closing bracket followed by a parenthesis). Not covered by M8's recorded decisions.
2. **Streaming hold-back hides whole short answers.** The `markers` set (`MarkdownDocument.swift:59`) includes digits, space and pipe, and `heldBack` (`:65-70`) withholds a trailing line made only of them. `MarkdownDocument(parsing: "128 * 8 = 1024", streaming: true).blocks == []`, and the same for `"42"` and `"2 + 2 = 4"`. An answer that is only such a line shows nothing until the stream ends.
3. **Pipe-led paragraphs are hidden for the whole stream.** The pipe-paragraph rule (`:43-45`) removes the entire last paragraph whose first span starts with `|`. A pipe-led block that never gains a delimiter row (an ASCII table, quoted pipe output, or prose like `|x| means the absolute value`) stays invisible until the answer finishes. Completed numeric table rows also lag one delta.
4. **A finished answer ending at a bare fence opener renders that line as nothing.** The fallback (`:37-39`) fires only when parsing throws, but `"```python"` parses to zero runs. A turn cancelled or failed right after opening a fence (`Transcript.swift:299`/`:315` then `TranscriptRow.swift:29`'s `streaming: !answer.finished`) loses visible output.
5. **`CodeLexer`'s heredoc false positive swallows the rest of a block.** `heredocMarker` (`CodeLexer.swift:963-988`) rejects only `<<<`, a digit or a space after `<<`, and its caller (`:848`) never checks arithmetic context. `echo $((a<<b))\necho after\nmore code` colours both following lines as a string, and Ruby's `x<<Const` does the same.

**Core constraint(s).** **Every character the model wrote reaches the screen**: the renderer may style, never drop. Lines Foundation would swallow (link definitions) are kept as text, matching the CLI's renderer (one product, two front ends). **Hold-back is bounded**: only an unfinished construct's own marker line is held, never a completed content line and never a paragraph for the whole stream. A finished answer renders exactly as written when its parse yields nothing for a non-empty line. **The lexer prefers false negatives**: an ambiguous `<<` in arithmetic or after an identifier with no space is a shift, not a heredoc. ADR 0002: `MarkdownDocument`/`CodeLexer` live in `styles`; tests are pure parses.

**Seam + files.** `presentation/styles/Components/MarkdownDocument.swift` (link definitions as text; the marker set narrowed to genuine construct markers; the pipe rule bounded to a table's first two lines; the empty-parse fallback for finished answers), `presentation/styles/Components/CodeLexer.swift` (heredoc context). Tests: each probe pinned as a parse case (streaming and finished), the CLI renderer's behavior as the oracle for link definitions, lexer cases for `$((a<<b))`, `x<<Const`, `1 << 2` and real heredocs.

**Reference.** GUI M8/M9 (shipped 2026-10-22); the CLI's `markdown/` renderer (the reference behavior for text it keeps); the CLI/GUI audit (2026-10-22).

**Decisions made** (dated):
- 2026-10-22: Specced from the CLI/GUI audit (the user's call).

**Open calls:**
- [default: reference-style definitions render as plain text lines, as the CLI prints them, not resolved into links.]

**Guardrail(s).** The parse matrix (streaming and finished) over every probe; the existing M8/M9 tests kept green.

**Acceptance criteria:**
- [ ] Link definitions, short arithmetic answers and pipe-led prose all appear while streaming and when finished.
- [ ] A finished answer ending at a fence opener shows that line; shifts are never coloured as heredocs.
- [ ] The full suite is green.

**Scope note.** Earmarked for v0.2.0 (track 4); ungated.
