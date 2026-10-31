# A failed Claude CLI turn says why

**What / why.** The user, 2026-10-31: "I can't even run cloud models in the GUI application: `claude: the Claude CLI reported success`". Their log holds three such turns on `claude` (`~/.apogee/logs/apogee-2026-10-31.log`, 19:55:33–40Z), while a `gemini` session the same minute answered.

Reproduced the same day, not GUI-specific. `apogee chat -m claude --private --output-format stream-json` from a terminal on the installed v0.2.0 answered turn 1 with `{"type":"error","message":"claude: the Claude CLI reported success"}`. `claude -p --input-format stream-json --output-format stream-json --verbose` (Claude Code 2.1.289), run directly as the backend runs it, said why:

- a synthetic `assistant` message (`"model":"<synthetic>"`) with text `Failed to authenticate: OAuth session expired and could not be refreshed` and `"error":"authentication_failed"`;
- then `{"type":"result","subtype":"success","is_error":true,"result":"Failed to authenticate: OAuth session expired and could not be refreshed","terminal_reason":"api_error",…}`.

**Root cause.** Claude Code reports a failed request with `is_error: true` and **`subtype: "success"`**, with the reason in `result`. `claude_cli_events.cpp:141-143` takes `is_error` as authoritative (right) and stores `subtype` as `error_subtype`. `claude_cli.cpp:533-539` then throws `"the Claude CLI reported " + error_subtype` — "reported success" — and never uses `final_text` (`claude_cli_events.cpp:120`, parsed from `result`), which holds the one sentence that says what's wrong and what to do. The structured log line (`claude_cli_events.cpp:215`, `result:` + subtype) says `result:success` for the same event.

**Blast radius.** Every Claude Code failure of this shape reads as "reported success" on every surface: the terminal, `serve`, machine mode's `error` event, and so the macOS app. An expired login, an exhausted plan and an API outage all look identical and nonsensical. What already works: the turn does fail (no fake answer), `is_error` is honored, and a failure with a real error subtype (`error_max_turns`, `claude_cli_test.cpp:445`) is named correctly.

**Core constraint(s).** **What leaves Claude Code reaches the person in its own words**: the reason is Claude Code's `result` text, not Apogee's paraphrase. **One message for every surface** (ADR 0002): the `ProviderError` the backend throws is what terminal, `serve` and machine mode all show, so the fix is at the throw, once. **Secrets stay unleakable**: the `result` text is shown as Claude Code printed it, which carries no credential; nothing from the OAuth store is read. **The additive protocol** (ADR 0006): the `error` event's shape is unchanged; only its message improves.

**Seam + files.**
- `data/backends/claude_cli_events.cpp/.h` — the `assistant` event's message-level `error` (`authentication_failed`, …) kept on the turn's outcome; the log label names the error, not the subtype, when `is_error`.
- `data/backends/claude_cli.cpp` — the thrown message: `the Claude CLI reported an error: <result text>` when `result` has text; the subtype only when it isn't `success`; and, for `authentication_failed`, the way out: `run claude in a terminal and sign in again with /login`.
- Tests: `tests/data/backends/claude_cli_events_test.cpp`, `claude_cli_test.cpp` — the probe's two lines as the fixture.

**Reference.** The Codex and Gemini backends already append the CLI's own text to "reported an error" (`codex_cli.cpp:446`, `gemini_cli.cpp:369`): the precedent this item brings Claude in line with.

**Decisions made** (dated):
- 2026-10-31: Diagnosed from the user's log and a probe (one prompt through the user's own Claude login, the user's report being the reason). The user's immediate way out is outside Apogee: sign Claude Code in again.
- 2026-10-31: A CLI Maintenance item: the defect is in the CLI's backend; the app shows whatever the `error` event says, so it needs no change of its own.

**Open calls:**
- [default: the `/login` hint is keyed on the `authentication_failed` error alone; other failures show Claude Code's text with no added advice.]
- [default: `apogee check` does not probe the Claude login: a check that spends a request is out of scope; noted for a later item if wanted.]

**Guardrail(s).** The probe's `assistant` + `result` lines replayed through the scripted spawner: the thrown message carries "OAuth session expired and could not be refreshed" and the `/login` hint, and never "reported success". The `error_max_turns` case keeps its subtype. A `success`-subtype error with an empty `result` says "reported an error". A machine-mode run over the same fixture carries the text in its `error` event.

**Acceptance criteria:**
- [ ] The user's case, replayed, reads `claude: the Claude CLI reported an error: Failed to authenticate: OAuth session expired and could not be refreshed -- run claude in a terminal and sign in again with /login` (wording settled at build), on the terminal and in machine mode's `error` event.
- [ ] No Claude CLI failure is ever reported as "success"; the full suite is green.

**Scope note.** CLI Maintenance; ungated. Codex and Gemini messages are out of scope (they already carry their CLI's text).
