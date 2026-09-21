# Gemini CLI fixtures

**Recorded, not transcribed** — byte-for-byte captures of
`gemini --skip-trust -o stream-json` against a **Google login** (the
subscription path) on 2026-09-06. Session UUIDs were rewritten to stable
placeholders so tests can assert on them; nothing else was touched.

| File | What it captures |
|---|---|
| `simple_text.jsonl` | The basic shape: `init` → `message`(user) → `message`(assistant) → `result`, with real per-model usage. |
| `streamed_deltas.jsonl` | A twenty-line answer arriving in **three** assistant deltas. The fixture that proves streaming is real — and note chunk 2→3 splits mid-number (`…12\n1` then `3\n14…`), which is why the adapter concatenates blindly instead of assuming token or line boundaries. |
| `resumed_session.jsonl` | `--resume <uuid>` continuing the `streamed_deltas` session and correctly recalling it stopped at 20. The **same** `session_id` is reported back. |
| `tool_use_plan_mode.jsonl` | `--approval-mode plan` refusing a file write, with the `tool_use` / `tool_result` pair the CLI emits for its own internal tools. |

**Pinned CLI version: `gemini` 0.46.0 (macOS, Google login).** Recorded so a
schema change is detected rather than silently absorbed.

## What the recording corrected in the item document

- **`--resume` accepts a UUID.** The characterization written before a login
  existed said it took `latest` or an index number "(not a uuid)". It takes the
  id, which is why this backend never has to capture one from the stream: it
  *chooses* the id with `--session-id` and resumes by the same value.
- **`--skip-trust` is mandatory, not optional.** Without it a headless
  invocation in an untrusted directory refuses to run at all — the earlier note
  had it only suppressing the approval-mode override.
- **The read-only pin holds.** `--approval-mode plan --skip-trust` did not write
  the canary file and printed no override warning, so the combination is safe;
  the item's warning applies to `plan` *without* `--skip-trust`.

## Two properties worth stating

**stdout is pure JSONL.** The 256-color warning and the `[STARTUP]` lines go to
stderr on their own — nothing had to be filtered out of stdout to make these
fixtures. The family's stream-separation constraint is satisfied by the CLI's
own behaviour here, and `stdout_is_pure_jsonl` pins it.

**`model` is `auto`, and one turn uses several models.** `init` reports
`"model":"auto"` and `result.stats.models` names each model that actually ran —
two per turn in every recording (`gemini-3.1-flash-lite` + `gemini-3.5-flash`,
and a `-customtools` variant once tools are involved). The CLI routes
internally, so "which model answered" is a *set*, not a value. See
`gemini_cli_events.h` for what the adapter reports and why.
