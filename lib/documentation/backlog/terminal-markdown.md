# Terminal markdown rendering

**What / why.** Models answer in Markdown, and a terminal shows it raw: `**Weather.com**`, `*you*`, `- ` bullets and `|`-ruled tables arrive as punctuation the reader has to parse by eye (Taylor, 2026-09-23: "the formatting here is HORRIBLE"). This item renders an answer's Markdown as it streams on a terminal — bold and italic as styles, inline code coloured without its backticks, headings, bullet and numbered lists with hanging indents, block quotes behind a gutter, fenced code in a dim block with its language named, rules, links, and tables laid out in columns — while a pipe, machine mode and the saved transcript keep the model's text byte for byte.

**Core constraint(s).**
- **The answer stream's contract holds.** Rendering happens only where `CliReporter` decorates (a TTY, not `--quiet`). A pipe receives exactly the model's text; machine mode's events carry it untouched; chat history and `knowledge capture` store it untouched. Rendering is a view, never a transform of the answer.
- **Not a TUI** (a non-goal in [ROADMAP.md](../assistant/ROADMAP.md)). It styles the scrolling transcript in place, the way the thinking view does; it never takes the screen, never positions the cursor absolutely, and never repaints anything that may have scrolled off. Only the line still being written is ever redrawn.
- **Streaming is the design, not an afterthought.** Tokens arrive mid-word and mid-marker (`**bo` … `ld**`). Whatever is committed to the scrollback must be final, and feeding the same answer in any chunking must end in the same screen.
- **Never the terminal's last column** — the rule `ThinkingView::content_width` learned on 2026-09-23. Rows wrap one short of the width, measured at every repaint, with `display_width`'s cell counts (wide characters two, combining marks none).

**Seam + files.**
- A new `source/markdown/` package (a leaf: `ansi/` and the width helpers only): `StreamRenderer` with `feed(chunk)` / `finish()` producing *render operations* — "commit these styled rows", "the in-progress line is now these rows" — rather than bytes, so the logic is testable without a terminal and the painter owns the escape codes.
- `commands/cli_reporter.cpp`: `on_answer_token` feeds the renderer when decorating (the whitespace holding added on 2026-09-23 moves into it); a small `AnswerView` next to `ThinkingView` paints the operations: committed rows once, the in-progress line erased and repainted with the same row arithmetic.
- `display_width` / `cells_of` / `wrap_tail` move out of `commands/thinking_view.cpp` into a shared text-width unit both views use.
- Surfaces: `apogee chat` and `apogee complete` on a TTY first; `analyze`'s human summary and `knowledge export --format md` previews later, through the same view.

**Reference (Ommi).** Ommi rendered answers raw. No port; the reference points are Claude Code's streaming Markdown (block-at-a-time with the open line redrawn) and `glow`'s styling for what a terminal can express.

**Decisions made** (dated):
- 2026-09-23 — **Line-at-a-time commits, the open line redrawn in place.** A completed line is rendered once and never touched again; the line still arriving is shown provisionally (unclosed `**` shown as written) and redrawn each chunk, erased by counted rows. Block-at-a-time would hold a paragraph back until its blank line — a long answer would appear in lumps. Redrawing more than the open line would mean erasing text that may have scrolled away.
- 2026-09-23 — **Tables are held until they end.** Column widths need every row. While a table streams, one dim `table · N rows…` line stands in for it; at the first non-table line the table is laid out and committed. A table wider than the terminal falls back to its raw rows, wrapped.
- 2026-09-23 — **Pipes, machine mode and history are untouched** — see the core constraint; there is no flag to render into a pipe.

**Open calls:**
- **[user]** A hand-written renderer for the subset models actually emit (CommonMark block structure plus GFM tables, strikethrough and task lists), or a parsing library (md4c: C, MIT, GFM-complete, re-run over each completed block)? The hard part is streaming, which neither library solves; a library adds edge-case correctness for inline parsing and a dependency. *Recommendation: hand-written, with a fixture corpus of real model answers as the correctness bar.*
- **[user]** Syntax highlighting inside fenced code: out of the first cut (dim block, language label), or in it? *Recommendation: out; a follow-up with a small keyword table per common language.*
- [default: `--raw` on `chat` and `complete` turns rendering off for a session, and a `ui.markdown: false` config key turns it off everywhere] Whether rendering can be switched off, and where.
- [default: links render as underlined text plus an OSC 8 hyperlink where the terminal advertises support, else `text (url)`] How links appear.
- [default: headings bold with `#` dropped, level 1–2 also coloured; bullets `•`/`◦`/`▪` by depth; block quotes behind a dim `│ `] The house style — veto freely, it is taste.

**Guardrail(s).**
- Golden tests per construct over `StreamRenderer`'s operations, plain and styled.
- **Chunking invariance:** every fixture fed whole, per character, and at random split points ends in the same screen — asserted through a small terminal model in the tests (printable cells, `\r`, `\n`, `ESC[2K`, `ESC[A`, deferred wrap), the same one that reproduced the thinking view's leftover lines.
- A pipe receives the model's text byte-identical to the unrendered stream (the existing `cli_reporter_test` contract, extended).
- A fixture corpus of recorded real answers (Qwen3.5, Claude, GPT) with lists, nested emphasis, a table, fenced code and a quote, rendered at widths 40, 80 and 120.

**Acceptance criteria:**
- [ ] `apogee chat` on a TTY shows bold, italic, inline code, headings, lists with hanging indents, quotes, fenced code, rules, links and tables rendered, as they stream.
- [ ] No row ever reaches the terminal's last column; a resize mid-answer changes the width of what is painted next.
- [ ] `apogee chat … | cat`, `apogee complete … > file` and machine mode carry the model's text unchanged; the saved transcript is unchanged.
- [ ] The same answer in any chunking produces the same screen.
- [ ] Turning rendering off (per the open call's answer) shows the raw text with today's layout.

**Scope note.** Unscheduled; the next terminal item after v0.1.2. Out of scope: a pager, rendering inside the thinking view (reasoning stays dim plain text), images, and HTML blocks (shown as written). Hiding type-ahead echo during a turn (the duplicated question in the same report) rides with this item's painter work: it needs a Ctrl-C handler that restores terminal modes before the process dies, which chat does not have today.
