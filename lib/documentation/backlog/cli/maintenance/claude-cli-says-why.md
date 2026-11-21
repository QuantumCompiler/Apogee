# CLI-M34 · A failed Claude CLI turn says why

| Field | Value |
|---|---|
| **Type** | 🐞 Bug |
| **Project** | CLI |
| **Status** | 🟢 Ready |
| **Fix version** | Maintenance (any time) |
| **Component** | Claude CLI backend (`data/backends/claude_cli`) |
| **Labels** | `claude-cli` `error-messages` `backend` |
| **Epic** | — (maintenance has no track) |
| **Blocked by** | — |
| **Blocks** | — |
| **Reporter** | The user (bug report, 2026-10-31) |
| **Created** | 2026-10-31 |

---

## Summary

When Claude Code fails a request it answers with `is_error: true` and `subtype: "success"`, its reason in `result`; the Claude CLI backend names the subtype and drops the reason, so every such failure reads `claude: the Claude CLI reported success`. The item delivers Claude Code's own `result` text in its place (and, for an expired login, the way back in), on every surface.

## Description

### User story

**As** a person using Apogee with the `claude` backend, from the terminal or the macOS app,
**I want** a failed Claude turn to tell me what Claude Code said went wrong, and how to fix an expired login,
**so that** I can tell an expired login, an exhausted plan and an API outage apart and get back to work, instead of reading "reported success".

### Steps to reproduce

1. Let Claude Code's login expire (here, an OAuth session that could not be refreshed).
2. Run `apogee chat -m claude --private --output-format stream-json` from a terminal on the installed v0.2.0 and send a prompt (or send a turn to a `claude` session in the GUI).
3. Read turn 1's answer.

### Actual result

- Turn 1 answers `{"type":"error","message":"claude: the Claude CLI reported success"}`.
- The GUI shows `claude: the Claude CLI reported success`.
- The structured log labels the event `result:success`.

### Expected result

- The message carries Claude Code's own reason, e.g. `claude: the Claude CLI reported an error: Failed to authenticate: OAuth session expired and could not be refreshed -- run claude in a terminal and sign in again with /login` (wording settled at build), on the terminal and in machine mode's `error` event.
- No Claude CLI failure is ever reported as "success".

### Background

The user, 2026-10-31: "I can't even run cloud models in the GUI application: `claude: the Claude CLI reported success`". It was reproduced the same day and is not GUI-specific: the defect is in the CLI's backend, and the app shows whatever the `error` event says, so it needs no change of its own.

The Codex and Gemini backends already append their CLI's own text to "reported an error"; this item brings Claude in line with them. Part of the fix has since landed through 57b (Decision log, 2026-11-01): the shared failure wording `claude_cli_failure` exists and the user's case already reads "reported an error" with Claude Code's text. What stays this item's is the message-level `error` kept on the outcome, the `/login` hint keyed on it, and the log label.

### Evidence

1. **The user's log, 2026-10-31.** Three such turns on `claude` in `~/.apogee/logs/apogee-2026-10-31.log`, 19:55:33–40Z, while a `gemini` session the same minute answered.
2. **Reproduced from a terminal, 2026-10-31.** `apogee chat -m claude --private --output-format stream-json` on the installed v0.2.0 answered turn 1 with `{"type":"error","message":"claude: the Claude CLI reported success"}`.
3. **Claude Code run directly, 2026-10-31.** `claude -p --input-format stream-json --output-format stream-json --verbose` (Claude Code 2.1.289), run as the backend runs it, said why:
   - a synthetic `assistant` message (`"model":"<synthetic>"`) with text `Failed to authenticate: OAuth session expired and could not be refreshed` and `"error":"authentication_failed"`;
   - then `{"type":"result","subtype":"success","is_error":true,"result":"Failed to authenticate: OAuth session expired and could not be refreshed","terminal_reason":"api_error",…}`.

   The probe sent one prompt through the user's own Claude login, the user's report being the reason.
4. **Root cause.** Claude Code reports a failed request with `is_error: true` and **`subtype: "success"`**, with the reason in `result`. `claude_cli_events.cpp:141-143` takes `is_error` as authoritative (right) and stores `subtype` as `error_subtype`. `claude_cli.cpp:533-539` then throws `"the Claude CLI reported " + error_subtype` — "reported success" — and never uses `final_text` (`claude_cli_events.cpp:120`, parsed from `result`), which holds the one sentence that says what's wrong and what to do. The structured log line (`claude_cli_events.cpp:215`, `result:` + subtype) says `result:success` for the same event. The line numbers in this Root cause item are the 2026-10-31 diagnosis's.
5. **Blast radius.** Every Claude Code failure of this shape reads as "reported success" on every surface: the terminal, `serve`, machine mode's `error` event, and so the macOS app. An expired login, an exhausted plan and an API outage all look identical and nonsensical.
6. **What already works.** The turn does fail (no fake answer), `is_error` is honored, and a failure with a real error subtype (`error_max_turns`, `claude_cli_test.cpp:445`) is named correctly.
7. **State since 57b, 2026-11-01.** `claude_cli_failure` (`data/backends/claude_cli.h/.cpp`) throws `the Claude CLI reported an error: <result text>` through `throw_if_failed` for the conversation's turn, a side request and `complete_structured` alike; the user's case already reads `claude: the Claude CLI reported an error: Failed to authenticate: OAuth session expired and could not be refreshed`, held by `claude_cli_test` and `cli.machine_mode`. The `/login` hint and the log label (`describe`'s `result:success`) are not yet done.

## Acceptance criteria

- [ ] **Given** the user's case (the probe's `assistant` + `result` lines), **when** it is replayed, **then** it reads `claude: the Claude CLI reported an error: Failed to authenticate: OAuth session expired and could not be refreshed -- run claude in a terminal and sign in again with /login` (wording settled at build), on the terminal and in machine mode's `error` event.
- [ ] **Given** any Claude CLI failure, **when** it is reported, **then** it is never reported as "success"; the full suite is green. (Holds already since 57b, per the Decision log, 2026-11-01.)

## Constraints

- **What leaves Claude Code reaches the person in its own words.** The reason is Claude Code's `result` text, not Apogee's paraphrase.
- **One message for every surface** (ADR 0002). The `ProviderError` the backend throws is what terminal, `serve` and machine mode all show, so the fix is at the throw, once.
- **Secrets stay unleakable.** The `result` text is shown as Claude Code printed it, which carries no credential; nothing from the OAuth store is read.
- **The additive protocol** (ADR 0006). The `error` event's shape is unchanged; only its message improves.
- **One failure wording for Claude.** The `/login` hint is added inside `claude_cli_failure`, never a second wording (the shared decision with 57b).

## Technical approach

Paths under `data/backends/`.

| File | Change |
|---|---|
| `claude_cli_events.cpp/.h` | The `assistant` event's message-level `error` (`authentication_failed`, …) kept on the turn's outcome; the log label names the error, not the subtype, when `is_error`. |
| `claude_cli.cpp` | The thrown message: `the Claude CLI reported an error: <result text>` when `result` has text; the subtype only when it isn't `success`; and, for `authentication_failed`, the way out: `run claude in a terminal and sign in again with /login`. Since 57b this lives in `claude_cli_failure` (`claude_cli.h/.cpp`); the hint is added there. |

**Reference.** The Codex and Gemini backends already append the CLI's own text to "reported an error" (`codex_cli.cpp:446`, `gemini_cli.cpp:369`): the precedent this item brings Claude in line with.

## Testing

Tests in `tests/data/backends/claude_cli_events_test.cpp` and `claude_cli_test.cpp`, with the probe's two lines as the fixture, replayed through the scripted spawner.

- **The user's case.** The probe's `assistant` + `result` lines: the thrown message carries "OAuth session expired and could not be refreshed" and the `/login` hint, and never "reported success".
- **A real subtype.** The `error_max_turns` case keeps its subtype.
- **An empty reason.** A `success`-subtype error with an empty `result` says "reported an error".
- **Machine mode.** A machine-mode run over the same fixture carries the text in its `error` event.

## Open questions

| Question | Status | Answer |
|---|---|---|
| Which failures get the `/login` hint? | `[default]` | The `/login` hint is keyed on the `authentication_failed` error alone; other failures show Claude Code's text with no added advice. |
| Does `apogee check` probe the Claude login? | `[default]` | No: a check that spends a request is out of scope; noted for a later item if wanted. |
| What is Claude's one failure wording (the message function)? | `(consumed decision)` | 57b, which built first and wrote `claude_cli_failure` ([Milestone AT](../../../assistant/cli/MILESTONES.md#milestone-at--the-second-sweeps-cli-findings)) |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-10-31 | Diagnosed from the user's log and a probe (one prompt through the user's own Claude login, the user's report being the reason). The user's immediate way out is outside Apogee: sign Claude Code in again. | — |
| 2026-10-31 | A CLI Maintenance item, ungated: the defect is in the CLI's backend; the app shows whatever the `error` event says, so it needs no change of its own. | the spec |
| 2026-11-01 | 57b built first ([Milestone AT](../../../assistant/cli/MILESTONES.md#milestone-at--the-second-sweeps-cli-findings)), and by the two items' shared decision ("whichever builds second calls the other's message function, so Claude has one failure wording") it wrote that function: `claude_cli_failure` (`data/backends/claude_cli.h/.cpp`) -- `the Claude CLI reported an error: <result text>`, the subtype named only when it is not `success` (`error_max_turns` kept), "success" never said -- thrown through `throw_if_failed` by the conversation's turn, a side request and `complete_structured` alike. So the user's case already reads `claude: the Claude CLI reported an error: Failed to authenticate: OAuth session expired and could not be refreshed` (held by `claude_cli_test` and `cli.machine_mode`), and this item's second acceptance criterion holds. What stays this item's: the `assistant` event's message-level `error` (`authentication_failed`) kept on the outcome, the `/login` hint keyed on it -- added inside `claude_cli_failure`, never a second wording -- and the log label (`describe`'s `result:success`). The line numbers under Root cause are the 2026-10-31 diagnosis's. | — |

## Out of scope

- Codex and Gemini messages (they already carry their CLI's text).
- `apogee check` probing the Claude login: a check that spends a request (noted for a later item if wanted).
- Any change in the macOS app: it shows whatever the `error` event says.

## Links

- **Blocked by:** — · **Blocks:** —
- **Relates to:** 57b and [Milestone AT](../../../assistant/cli/MILESTONES.md#milestone-at--the-second-sweeps-cli-findings) (the shared Claude failure wording, `claude_cli_failure`); ADR 0002 (one message for every surface); ADR 0006 (the additive protocol)
