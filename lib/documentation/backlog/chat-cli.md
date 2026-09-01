# apogee chat + the shared terminal UX layer

**What / why.** The interactive surface and the presentation layer every interactive command shares, guarded here as one coverage entry that MUST be split into two backlog docs at grooming — doc A: the terminal UX layer + cliReporter + PTY test harness, with complete retrofitted onto it; doc B (gated on doc A): the chat REPL + sessions/resume/titling/context monitoring + the logging subsystem (daily operational log + per-session JSON), which no other item owns. Discovering the split mid-session would ship half of each. Phase 1, terminal UX: one self-overwriting status line all startup and progress output routes through, an animated thinking spinner (elapsed time + live token estimate, generation-counter invalidation so any permanent print stops it cleanly), and live thinking rendered per the adopted design notes (**the appendix at the bottom of this document** — read it before building; reasoning must NOT accumulate in scrollback): a header plus a rolling window of the last 2 lines repainted in place while a block streams, collapsing on completion to a single dimmed `✻ Thought for Ns` line that is all scrollback keeps. The erase arithmetic is made exact by construction — **never print a line the terminal has to wrap**: pre-wrap every painted row to width−indent (by codepoint, not byte) so painted rows equal screen rows. An empty thinking delta never opens the view (redacted-thinking models send empty payloads; the spinner runs on the token estimate alone); blank lines are dropped from the window; blocks interleave (thinking → text → thinking) and each collapses independently with an idempotent finish; --verbose keeps the full reasoning as a permanent transcript with no cursor movement; non-TTY emits no thinking, no escape codes, no summary — exactly the answer. Uniform across every backend because all of them (direct APIs and the claude CLI adapter alike) feed the same typed OnThinking seam — colored [role] prefix tags with NO_COLOR/non-TTY/--no-color handling, line/verbose/quiet modes, and a cliReporter adapter over the agentloop Reporter so the loop stays I/O-free; complete retrofits onto it. Phase 2, chat: a line-editing REPL with slash commands (/help /model /models /system /temperature /max-tokens /compact /exit minimum), all configured backends constructed up front so /model switches instantly with history carried over (uniform across providers because history is neutral IR), per-turn-persisted JSON session logs with a versioned inference-context block, faithful --resume/--continue (flag > saved > default, degrade-with-warning), background auto-titling as a SideRequest, and context monitoring Apogee must now own for ALL backends (80% warn, 90% auto-compact; exact counts via provider count-tokens or the local tokenizer, estimates flagged), plus the one-time typeahead flush before the first prompt. Chat also carries --image attachments (cloud image parts, as in complete).

**Core constraint(s).**
- Startup speaks on one line: no subprocess or construction-time notice may write raw stderr on interactive paths — notices go through an injected OnNotice-style hook (design the seam now; MCP inherits it later)
- Persisted history is always clean: no thinking, no injected context, no tool markup (grep-verified in tests); the log matches what the user saw
- Persist after every turn (crash safety); chat_id immutable, rename via custom_name only; resume precedence flag > saved > default with degrade-not-fail
- Typeahead discarded via tcflush before the first prompt only, never by toggling ECHO (exit paths can strand terminal state)
- Thinking is display-only, never in persisted history or piped output; the thinking view opens only on non-empty payload, and the rendering path and persistence path are separate code paths — only rendering ever sees thinking

**Seam + files.** lib/src/cli/source/ansi/ansi.h/.cpp (Init, Tag, Colorize, mode matrix), lib/src/cli/source/commands/status_line.h/.cpp (overwriting line, spinner thread with generation-counter invalidation), lib/src/cli/source/commands/cli_reporter.cpp (Reporter adapter), lib/src/cli/source/commands/chat.cpp + chat_history.cpp (REPL, slash dispatch, list/info/title), lib/src/cli/source/logger/session.h/.cpp (per-turn persistence, SessionConfig, LoadSession with [resume] warnings, operational log), lib/src/cli/source/commands/input_gate.cpp (tcflush typeahead discard), (consumes lib/src/cli/source/harness/context_windows.cpp, owned by harness-core), lib/src/cli/tests/commands/ (PTY exec tests via forkpty: startup line, spinner interruption, resume/degrade, typeahead).

**Reference (Ommi).** cmd/ommi chat.go + chat_history.go, src/logger (versioned SessionConfig, per-turn persistence, faithful resume), src/ansi, statusLine/StartThinking/thinkingStream, input_gate.go, checkContextUsage/compactHistory — Milestones B and P, OMMI-6 thinking display, OMMI-14 startup invariant. Divergences: Apogee owns transcripts, context management, and compaction natively for every backend — direct-API backends have no vendor session store at all, and for CLI-type backends (claude-cli and the vendor family) the vendor's session store is a resume optimization only, with Apogee's transcript authoritative — so resume and model-switching stay uniform; thinking arrives as typed events on every backend (API deltas or CLI stream events), so the display generalizes beyond Claude with no scraping.

**Decisions made** (dated):
- 2026-08-24 — Planned from Ommi's documentation (parallel digest → two planning lenses → merge → adversarial verify). Position in the queue: Ommi milestone B sits directly on the loop; chat is the richest v0.1.0 surface and exercises the resume/logging the install doctor then validates.

**Open calls:**
- [default: replxx] Line-editing library: replxx vs linenoise-ng (readline is GPL)
- [default: single JSON rewritten per turn — Ommi-proven crash-safety shape] Session file format: per-turn JSON rewrite vs append-only JSONL

**Guardrail(s).** PTY tests (forkpty) locking single-line startup, spinner-interruption, resume/degrade, and typeahead behavior — Ommi's startup_output_test pattern; a history-cleanliness grep test on session files after tool+thinking turns; a crash-safety kill-and-resume test. The thinking view is tested by injecting its output stream (see the appendix): assert on the bytes that a scrolled-off line is absent from the final paint, a repaint begins with the correct count of `\033[A\033[2K` pairs, the bytes after the last erase are exactly the collapsed summary (the assertion that proves no reasoning survived), two blocks yield two summaries, verbose mode emits no cursor movement, a non-TTY view writes nothing, and the wrap helper handles last-N selection, blank dropping, width clamping, and multibyte input; end-to-end, replay the PTY byte stream's escape codes to reconstruct the final screen. All terminal writes — spinner, thinking repaints, token stream — serialize under one mutex (a 200 ms spinner against a token stream is the collision case).

**Acceptance criteria:**
- [ ] During a turn on a TTY: spinner with elapsed seconds appears; thinking shows as a repainted-in-place 2-line rolling window that collapses to a single dimmed `✻ Thought for Ns` line before the answer takes over (never accumulating in scrollback); an empty thinking delta opens nothing; piped output contains none of it; all startup output routes through the status line (PTY test asserts single-line startup)
- [ ] A session survives kill -9 with all completed turns on disk; --continue restores backend, params, and history with [resume] warnings for anything missing, never a failure
- [ ] /model mid-session switches provider (e.g. Anthropic→mock) carrying full history
- [ ] Crossing 80%/90% of a configured context window warns then auto-compacts; /compact works manually; post-compact history contains the summary system message
- [ ] Keystrokes typed during startup are discarded once, mid-session typeahead preserved (PTY test); NO_COLOR, --no-color, and non-TTY each disable color; quiet mode still shows warnings
- [ ] Titles generate in the background after the first exchange; chat list/info/title work

**Scope note.** earmarked for v0.1.0. Gate satisfied: the shared agent loop shipped 2026-08-26 ([MILESTONES.md](../assistant/MILESTONES.md) → Milestone F). The terminal UX layer is the Reporter adapter — `commands/complete.cpp`'s `CompleteReporter` is the minimal worked example, and `commands/ask_prompt.cpp` is the placeholder terminal `ask_user` this item should replace. **Split first** — the appendix below belongs to doc A (the terminal UX layer).

---

# Appendix — Rendering thinking in the terminal

*Folded in 2026-08-25 from `lib/documentation/assistant/claude-cli-streaming-backend.md` §8 and the renderer half of §11. Backend-agnostic: it applies to any provider that emits thinking deltas, which is all of them. The wire-protocol half of those notes went to [claude-cli-backend.md](claude-cli-backend.md).*

*Provenance: written after implementing this against a real harness (Ommi, Go). **The obvious implementation is wrong in a way that only shows up after a few real turns** — that is what this appendix exists to prevent.*

## The wire behaviour first

Thinking arrives in two distinct forms and both must be handled (**[verified against claude CLI 2.1.233]**, but the shape generalizes):

1. **Thinking text streams** — real reasoning, token by token.
2. **Thinking text is redacted** — the newest models default to `display: "omitted"` at the API level. You still get thinking events, but with an **empty** text field. The only live signal is a running token estimate.

Case 2 is what makes the empty-payload guard load-bearing: if a blank thinking event opens the thinking UI, every redacted turn renders an empty reasoning block. **Guard on non-empty text before opening anything**, and drive a spinner from the token estimate instead — `✻ Thinking… (5s · ~87 tokens)` is a genuinely informative wait indicator built from nothing but that counter.

## Do not put reasoning in scrollback

The naive rendering — print a `✻ Thinking…` header, stream the reasoning underneath, then print the answer — is what Ommi shipped first. It reads fine for one turn and becomes unusable by the third: a conversation where every turn leaves a 10-line internal monologue above a 2-line answer shows more deliberation than content, and scrolling back to find what was actually *said* is miserable. A real user report triggered the rewrite.

The behaviour to copy from the Claude CLI itself:

- **While thinking:** a header plus a **rolling window of the last N wrapped lines** (N = 2 works well), repainted in place as text arrives. The user sees that reasoning is happening and roughly what about, without it accumulating.
- **When the block completes:** erase the whole region and leave **one** dimmed line — `✻ Thought for 4s`. That is what survives in scrollback.

## The constraint that makes it work

An earlier design rejected collapsing for a specific reason, recorded at the time as *"no cursor rewind over wrapped lines"* — **and that reason was correct.** You cannot erase N printed lines with `\033[A` without knowing how many **screen rows** they occupied, and any line longer than the terminal width occupies more than one. Get it wrong and the erase eats the user's prompt, or leaves orphaned fragments.

The fix is to remove the unknown rather than compute it: **never print a line the terminal has to wrap.** Pre-wrap every row to `width - indent` before printing. Then painted rows and screen rows are the same number by construction, and the arithmetic is exact.

```cpp
// Every string in `lines` is already <= width-2 display cells.
void ThinkingView::repaint() {
    auto lines = wrap_tail(tail_, width_ - 2, kTailLines);   // last N, pre-wrapped
    erase();
    out_ << "✻ Thinking…";
    for (const auto& ln : lines) out_ << "\n  " << dim(ln);
    out_.flush();
    painted_ = 1 + lines.size();                 // header + tail rows
}

// Leaves the cursor at column 0 of the row the header occupied.
void ThinkingView::erase() {
    if (painted_ == 0) return;
    out_ << "\r\033[2K";
    for (int i = 1; i < painted_; ++i) out_ << "\033[A\033[2K";
    painted_ = 0;
}

void ThinkingView::finish() {
    if (!open_) return;
    open_ = false;
    erase();
    auto secs = std::max(1, elapsed_seconds(started_));
    out_ << dim("✻ Thought for " + std::to_string(secs) + "s") << "\n\n";
}
```

`repaint()` deliberately leaves the cursor on the last painted row with no trailing newline — `erase()`'s row count depends on it.

## Details that only surface in live use

- **Bound the retained text.** You display at most N lines; keeping the whole block is pointless. A few KB of tail is generous. Cut on a UTF-8 rune boundary, not a byte.
- **Drop blank lines from the window.** A paragraph break inside the reasoning will otherwise spend one of two precious rows painting nothing. Found on the first live run.
- **Wrap by codepoint, not byte.** Obvious, and still easy to get wrong when the tail is a `std::string`.
- **Blocks interleave.** A turn can go thinking → text → thinking → text. Each block opens and collapses independently, and `finish()` must be idempotent so the end-of-turn path can call it unconditionally.
- **Keep a verbose escape hatch.** `--verbose` keeps the full reasoning as a permanent transcript, no cursor movement, no collapse. Someone debugging a model's behaviour wants every word, and the collapsed view is actively hostile to that.
- **Non-TTY: discard entirely.** On a pipe or redirect, emit no thinking, no escape codes, no summary line. Scripted output is exactly the answer. Check this explicitly; it is the difference between a usable CLI and one that cannot be composed.

## Display only, always

Thinking must not leak into persisted conversation history, the text returned to a programmatic caller, or a served API response. Strip it at the boundary. Two reasons: it bloats every subsequent prompt if it re-enters history, and an OpenAI-format client does not expect reasoning in its content field. **Keep the rendering path and the persistence path separate, and let only the rendering path see thinking.**

## Testing the renderer

Terminal rendering gets waved off as "thin I/O, untestable" — it is not, and the escape sequences are exactly where the bugs live. **Inject the output stream:** give the view an `std::ostream&` (or any sink), write to a string buffer in tests, and assert on the bytes:

- a scrolled-off line is absent from the final paint;
- a repaint begins with the correct number of `\033[A\033[2K` pairs;
- after `finish()`, the bytes following the last erase are *exactly* the collapsed summary — **this is the assertion that proves no reasoning survived**;
- two blocks in one turn produce two summaries;
- verbose mode emits no `\033[A` at all and no summary;
- a non-TTY / inactive view writes nothing whatsoever;
- the wrap helper: wrapping, last-N selection, blank dropping, width clamping, multibyte input.

For end to end, run the real binary under a pseudo-terminal, capture the raw byte stream, and *replay the escape codes* in the test to reconstruct the final screen. That is the only way to assert what the user is actually left looking at. (PTY mechanics are POSIX — per the six-target matrix they go behind `lib/src/cli/source/platform/`, with a Windows equivalent or a recorded skip.)

**Open call carried from the original notes:** [default: fixed at 2 lines] whether the thinking window's height should be user-configurable. Two is the Claude CLI's shape and needs no explanation; a setting invites someone to set it to 40 and recreate the problem this appendix exists to solve.
