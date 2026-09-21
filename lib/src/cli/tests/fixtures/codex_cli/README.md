# Codex CLI fixtures

**Recorded, not transcribed** — byte-for-byte captures of `codex exec --json`
against a ChatGPT login on 2026-09-06.

| File | What it captures |
|---|---|
| `simple_text.jsonl` | The basic shape: `thread.started` → `turn.started` → `item.completed` → `turn.completed`, with real usage. |
| `multiline_answer.jsonl` | A twelve-line answer — **still one `item.completed`**. This is the fixture that proves streaming is message-level, not token-level. |
| `structured_output.jsonl` | `--output-schema` run: the conforming JSON arrives as the `agent_message.text` itself, not in a separate field. |
| `resumed_thread.jsonl` | `codex exec resume` continuing a thread, correctly recalling the previous turn. Same `thread_id` reported again. |

**Pinned CLI version: `codex-cli` 0.153.4 (macOS, ChatGPT login).** Recorded so a
schema change is detected rather than silently absorbed.

Two things deliberately absent, because the CLI does not produce them:
a reasoning/thinking item (reasoning is *counted* in `usage.reasoning_output_tokens`
and never surfaced), and any delta event at all.
