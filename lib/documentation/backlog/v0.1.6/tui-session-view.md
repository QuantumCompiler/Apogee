# The session view: chat on the shell's stage

**What / why.** The shell ([32b](tui-shell.md)) is an empty stage; this item puts the conversation on it — the view bare `apogee` lands in. The spike's architecture finding (2026-10-04) is what makes this a rendering job rather than a second chat: **the interactive loop already crosses one seam.** The agentloop's Reporter has three sibling adapters (`cli_reporter` for the terminal, `json_reporter` for machine mode, `sse_reporter` for a served stream); the machine-mode protocol — conformance-pinned in both directions and proven by a naive external host — carries thinking and answer deltas, tool status, and structured questions including permission prompts with the full `yes`/`no`/`always`/`session` semantics. The session view is the **fourth adapter**: a Reporter implementation that feeds the shell's widgets. In-process it is deliberately *richer than the wire* — `on_side_call` (26n's narration: the embedder, utility rewrites, the rerank judge, consults) never crosses machine mode, but an in-process adapter receives it, so the TUI paints the side-call lines a stdio driver cannot see. The transcript reuses what exists: `markdown/` emits **render operations** with the painter kept separate by design (`answer_view` is the terminal's), so the TUI transcript is a second painter of the same operations — never a second Markdown renderer. Input is the shell's editor with the palette and completions answered by the shipped `__complete` resolver; sessions persist, resume and compact exactly as chat's session core does — the view drives the same core `chat` and the specced execute mode (27s, v0.1.4 — built before this) share.

**Core constraint(s).**
- **A Reporter adapter, not a loop:** the view implements the Reporter seam and drives the shared session core; it owns zero conversation logic, zero tool logic, zero permission logic. The permission prompt renders the existing question shapes and returns the existing answers — `always` writes through the same config path 26o writes, because it *is* 26o's flow behind a widget.
- **One Markdown renderer:** the transcript consumes `markdown/`'s render operations; a formatting fix lands in `markdown/` and both painters inherit it. A TUI-side Markdown parser is a banned second implementation.
- **Session fidelity:** a session opened in the TUI resumes in `apogee chat` and vice versa — one persistence format, no TUI-only fields that raw chat cannot read (additive metadata only, absent-safe per the backwards-compatibility ADR).
- **The palette renders the resolver:** completions, slash verbs and pickers are fed by the shipped `__complete` machinery and the one command table — the TUI offers what the CLI offers, from the same source, so the offer-is-a-contract bar (ADR 0007) holds without a second list.
- **Honest degradation:** a turn's stream interrupted (backend death, cancel) leaves the transcript stating what happened, exactly as the terminal does — never a frozen widget.
- Code style carries: `.h`/`.cpp` pairs, smart pointers only; tests mirror the module.

**Seam + files.**
- `presentation/tui/session_view.h/.cpp`: the view — transcript widget (render-operation painter), input editor, status strip, the question/permission modal; `tui/tui_reporter.h/.cpp`: the fourth Reporter adapter (deltas, tool status, side calls, questions → widget state).
- `presentation/views/` untouched in behavior; the render-operation types it shares with `markdown/` consumed as they are.
- The session core chat opens (as 27s parameterized it, shipped by this table's turn): the TUI is its third consumer after chat and execute.
- Tests: `tests/presentation/tui/` — headless goldens for a scripted turn (thinking → tool status → side call → answer ops → result), the permission modal returning each of the four answers and `always` writing the config through the one editor (asserted byte-stable), resume round-trip TUI↔chat, the interrupted-stream rendering.
- Consumes: [32b](tui-shell.md) (the stage), the Reporter seam (shipped), `markdown/` render ops (shipped), 26n side calls (shipped), 26o permission presets (shipped), the `__complete` resolver (shipped), 27s's session-core parameterization (v0.1.4, builds first).

**Reference.** The in-house precedents carry the whole design: the three existing Reporter adapters (the seam this view joins), `answer_view` (the painter pattern duplicated deliberately, the renderer never), and the machine-mode question shapes (the modal's exact contract). The reference project's removed TUI kept its chat view honest by hand; this one is honest by seam.

**Decisions made** (dated):
- 2026-10-04 — Split from the shell so each is one focused session: the stage and the first play are different work.
- 2026-10-04 — The fourth-adapter architecture recorded with the spike's evidence: the in-process seam carries `on_side_call`, which the wire deliberately does not — the TUI session is *fuller* than a machine-mode driver's, for free.
- 2026-10-04 — One session format, both doors: TUI and raw chat open each other's sessions; divergence is a bug, pinned by the round-trip test.

**Open calls:**
- [default: the view opens on a session picker when saved chats exist and a fresh session otherwise; `apogee` straight into the picker's "new chat" is one keypress — veto flips to always-fresh with the picker behind a key]

**Guardrail(s).**
- Scripted-turn goldens over the fourth adapter: every Reporter callback renders, side calls included; call counts prove no second loop ran.
- The permission modal: all four answers round-trip; `always` produces the byte-identical config edit the CLI's prompt produces.
- Resume round-trip: a TUI session resumed in `apogee chat` and back, transcript equal; the suite's chat battery untouched and green.
- The one-renderer pin: the TUI transcript test consumes planted render operations — a Markdown string reaching the TUI directly fails the seam's type.

**Acceptance criteria:**
- [ ] Bare `apogee` lands in the session view; a full tool-using, permission-prompted turn runs to its answer entirely on-screen, side calls narrated, markdown rendered.
- [ ] The permission modal's `always` is reflected in the config exactly as the terminal prompt's would be; `session` grants die with the process.
- [ ] A session started in the TUI resumes with `apogee chat --resume` (and vice versa) with nothing lost.
- [ ] The palette completes commands, models and sessions from the `__complete` resolver — nothing offered that the CLI would refuse.

**Scope note.** Item **32c**, earmarked for **v0.1.6**; **gated on [32b](tui-shell.md)**. Out of scope: non-chat views ([32d](tui-workbench-views.md)); machine-mode changes (none, ever, from this set); execute-mode's suite/symphony surfaces in the TUI (they arrive as content once 27s/27q ship — interplay, not a gate).
