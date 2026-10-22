# Chat answers render their markdown

**What / why.** The macOS app draws every chat answer as plain text. Found while building M7 (2026-10-22), where resumed turns share the live answer row, and read in the source the same day:
- **The answer row is a plain `Text(answer.text)`** (`lib/src/darwin/source/presentation/session/Views/TranscriptRow.swift:25–31`). SwiftUI parses markdown only from a string *literal*, never from a `String` value. So a model's `**bold**`, `` `code` ``, lists, headings and fenced code blocks show as their raw characters, live and resumed alike.
- **No markdown is rendered anywhere in the app.** The only mention of markdown in its source is Knowledge's "Export as Markdown".
- **The design already draws answers formatted** (`lib/design/spec/html/main.html:300–345`, the chat's answer):
  - prose (`.prose`, the `prose` type step, line height 1.6);
  - inline `code` in the mono face on `surface.control`, rounded (`main.html:17`);
  - a code block as a bordered figure on `surface.code`, under a caption naming its language and file, with a **Copy** button;
  - its code coloured by the `code.*` tokens.

  The app carries `surface.code` (`presentation/styles/Palette.swift:97`) but none of the `code.*` colours.

Every model the user runs answers in markdown, so every answer with structure reads as noise in the app. The same answer reads cleanly in the CLI's terminal, which renders it (`lib/src/cli/source/presentation/markdown/`).

**Core constraint(s).**
- **No package, no linked library.** The darwin layering check fails any package dependency, product or `linkedLibrary` (CLAUDE.md → the machine-mode client). Parsing uses Foundation's own markdown (`AttributedString(markdown:options:)`, its full syntax yielding `PresentationIntent` blocks), and the layout is the app's own SwiftUI.
- **One component, in `styles`** (ADR 0002): the renderer is a shared component, so Review, Knowledge and Tasks can adopt it later. They are not changed here.
- **Every colour a token, every size a `TypeScale` step,** so Settings › Appearance still changes everything.
- **Streaming stays smooth.** An answer renders as its deltas arrive, never waits for the end, and never flickers between raw and formatted. A fence or emphasis still open at the stream's edge renders tolerantly: the code block in progress, or the marker shown until it closes.
- **The app never reaches the network for an answer.**
  - **Images:** a markdown image draws as its alt text and its address, never fetched.
  - **Links:** a link opens through the system only for `http(s)` and `mailto`, never a `file:` URL, and its target shows on hover.
- **Text stays selectable,** as the plain row is today (`.textSelection(.enabled)`).
- **ADR 0004:** each rendered shape has a preview over recorded answers.

**Seam + files.**
- **`lib/src/darwin/source/presentation/styles/Components/MarkdownText.swift`** (new): the answer's text parsed into blocks, each drawn in the design's terms.
  - **Paragraphs** carry emphasis, strong, inline code and links.
  - **Headings** use `TypeScale` steps.
  - **Lists**, ordered and not, are nested.
  - **Block quotes** are drawn.
  - **Code blocks** sit under the language caption and a Copy button.
  - **Thematic breaks** are drawn, and **tables** as a grid where the parser yields them.
- **`presentation/session/Views/TranscriptRow.swift`:** the answer row draws through it, and `ActivityView` is unchanged. The user's own words stay as typed (the open call).
- **Tests:**
  - the block segmentation over recorded answers;
  - an answer streamed in pieces, each prefix rendering without raw markers once closed;
  - an open fence at the edge;
  - a link's scheme rule;
  - an image never fetched.
- **Fixtures and previews:**
  - a stream from the scripted mock backend whose answer carries every shape: paragraphs, emphasis, inline code, a list, a heading, a fenced block and a table;
  - its `chats show`, so a resumed answer renders the same;
  - recorded from the binary over a sandbox home, as M7's were.

**Decisions made** (dated):
- 2026-10-22 — **Opened from M7's record** (the user's call: they asked for this to be queued, from the card M7's shipping proposed). M7's MILESTONES entry names the gap under "Not done".
- 2026-10-22 — **Foundation's parser, not a package.** The layering check forbids a package dependency, and `AttributedString`'s full markdown syntax gives block structure (`PresentationIntent`) the app can lay out itself.

**Open calls:**
- [default: syntax colouring is out of this item. Code blocks are mono on `surface.code` with their language caption and Copy. Adding the design's `code.*` tokens to `Palette` and colouring code is a later item.]
- [default: the user's own messages stay as typed, unrendered, so a literal `*` they write is never reshaped. Only answers render.]
- [default: the whole answer is re-parsed as each delta arrives, coalesced to the transcript's existing redraw. A long recorded answer is measured, and only completed blocks are cached if the measure shows a cost.]

**Guardrail(s).**
- **The recorded markdown answer** renders every shape: its block list pinned, and its preview drawn.
- **Streamed in pieces,** no prefix leaves a closed marker raw, and an open fence renders as a code block in progress.
- **A plain answer with no markdown** reads exactly as today.
- **Links and images:** a `file:` link is not opened, and an image is never fetched, by test.

**Acceptance criteria:**
- [ ] A chat answer with emphasis, inline code, lists, headings and a fenced code block renders as the design draws it, live and resumed, with Copy on the code block.
- [ ] Streaming an answer shows its formatting as it arrives, without flicker.
- [ ] Previews, `check-previews`, the layering check and `lib/scripts/cicd.sh --test` pass, with no Swift warning.

**Scope note.** GUI Maintenance, claimable by name, no gate. Rendering markdown elsewhere (Review findings, Knowledge's answers, task results) reuses the component later, unasked here. Syntax colouring is the later item named in the open calls.
