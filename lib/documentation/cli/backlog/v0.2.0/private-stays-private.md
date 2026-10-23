# Private stays private: nothing typed, titled or raced

**What / why.** 52d shipped `chat --private` with a promise said at start and at exit: *nothing of this chat is written*. The CLI/GUI audit (2026-10-22, a three-lens adversarially verified sweep over the branch) found two paths where that is false, and one adjacent race in the same seam:

1. **Every typed line lands in `chat_history`.** At a terminal, `chat --private` (and `execute --private`) keeps the line editor's history file: `chat_session.cpp:1752` sets `reader_options.history_path = default_history_path()` with no private guard, `:1904` remembers every non-empty input, and `views/line_reader.cpp:205-207` / `:370-373` root the file at `apogee_home()/"chat_history"` and write it on destruction. Probe (branch binary, sandboxed `APOGEE_HOME`, mock backend, PTY): typed `zebra confidential gamma` into `chat --private`, exited with `/exit`. The exit line said `private: nothing of this chat was written`, and `$APOGEE_HOME/chat_history` held `zebra confidential gamma`. The lines also come back as up-arrow history in later non-private sessions. The machine-mode pipe path (`PlainLineReader`) is clean. 52d's tests pinned absence over `sessions/`, attachments, memory and the recall index, but never `chat_history`.
2. **A private chat's questions go to the utility backend to make a title nobody keeps** (found by the audit's completeness pass with its own probe; reproduce before fixing). `BackgroundTitle::start_if_due` (`chat_session.cpp:474-490`) checks the turn count and the name, never `never_written`/`private_chat`, and runs at `:1667` (machine mode) and `:1970`/`:2557` (terminal). The title is then thrown away, because `logger::save` does nothing for a private chat (`data/logger/session.cpp:549`). Probe: with `models.default_utility` a `claude-cli` entry whose binary was a logging script, `chat --private` sent that script `"The conversation's questions:\n- our confidential merger with Acme closes Friday"`. A private conversation's content went to a second provider, possibly a cloud one, for a result discarded.
3. **The same title call races machine mode's `compact` and `set model` lines.** The session's own rule (`chat_session.cpp:430-435`): *a local provider is not safe to drive from two threads, so everything that may reach the model calls `settle()` first*. The REPL honors it before every slash dispatch (`:1907`). Machine mode does not: the `compact` handler (`:1526-1529` → `compact_attached` `:364-371` → `harness.chat` at `business/agentloop/content.cpp:133`) and `set`'s model branch (`:1548-1553` → `switch_model` → `move_pin` → `backends::rebuild_providers`) run straight off the driver loop while the async title call launched at `:1667` may still be in flight. The window is once per session (the first untitled exchange).

This item closes all three in their shared seam. A private session keeps **no line history** (the editor's history path is empty when private, and nothing is remembered into the shared file). A private session **never starts a title call**. Every machine-mode line that can reach a model (`compact`, `set` with a model) **settles the title first**, as the REPL's slash dispatch does.

**Core constraint(s).** **Private is absolute** (52d's own law, "never written, not written-then-deleted"): the fix proves absence of every artifact. That covers `chat_history` and the recall index, and no provider call beyond the conversation's own model. **One model call at a time** is the session's stated rule, and every driver line honors it exactly as the REPL does, through the existing `settle()`, never a second mechanism. **Non-private sessions are unchanged**: history and titles behave byte-identically. Terminal and machine mode stay one session core (27s), so the fix lands there and both doors get it.

**Seam + files.** `presentation/cli/chat_session.cpp` (the reader options' history path under private, the title's private guard in `start_if_due`, `settle()` before the `compact` and model-`set` handlers), `presentation/views/line_reader.cpp` only if an empty path needs a guard. Tests: a PTY-driven private session asserting `chat_history` absent or unchanged; a private machine-mode session over a scripted utility backend that records calls, asserting zero title calls; a driven `compact` and `set model` immediately after a first answer, asserting the title settled before (a scripted slow title call proves the ordering).

**Reference.** 52d (shipped, CLI MILESTONES Milestone AO), the private promise and its absence tests, extended here; the session's `settle()` discipline (`chat_session.cpp:430-435`); the CLI/GUI audit (2026-10-22): findings and probes above.

**Decisions made** (dated):
- 2026-10-22: Specced from the user-requested CLI/GUI audit (the user's call: every issue found becomes a v0.2.0 item in its queue). The three findings share one seam, so they are one item.
- 2026-10-22: The title-leak finding came from the audit's completeness pass with a probe, not the three-lens panel. Its first build step is reproducing it.

**Open calls:**
- [default: a private terminal session keeps an in-memory history for the session's own up-arrow use, written nowhere; the shared file is untouched.]

**Guardrail(s).** The PTY absence probe pinned as a test (`chat_history` byte-identical before and after a private session); the zero-title-call pin over a recording utility backend; the settle-ordering pin for both driver lines; the existing 52d absence tests kept green.

**Acceptance criteria:**
- [ ] A private terminal session writes nothing to `chat_history`, and its lines never appear in a later session's history.
- [ ] A private session (either door) makes no title call to any backend.
- [ ] `compact` and `set model` lines settle any in-flight title call before reaching the model; the full suite is green.

**Scope note.** Earmarked for v0.2.0 (track 55, the CLI/GUI audit's CLI findings); ungated. Related GUI work: the app's handling of private sessions across navigation is [`private-sessions-survive.md`](../../../gui/backlog/v0.2.0/private-sessions-survive.md) (GUI 4a); neither gates the other.
