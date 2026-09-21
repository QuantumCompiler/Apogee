# Ollama CLI fixtures

**Recorded, not transcribed** — these are byte-for-byte captures of `ollama run`
against a signed-in cloud session on 2026-09-06:

| File | Command | Why it is here |
|---|---|---|
| `thinking_then_answer.txt` | `ollama run gpt-oss:20b-cloud --nowordwrap "Say exactly: hello"` | The normal shape: thinking in-band between `Thinking...` and `...done thinking.`, a blank line, then the answer. |
| `answer_only.txt` | the same with `--hidethinking` | Proof the markers are absent when thinking is suppressed, so the demux must not require them. |

**Pinned CLI version: `ollama` 0.33.2 (macOS).** Recorded here so a schema change
is detected rather than silently absorbed — if a future CLI stops emitting these
markers, the replay suite fails and someone re-records rather than discovering it
through a user's garbled transcript.

Not captured, deliberately: the default (word-wrapping) run. It contains
`ESC[nD ESC[K` cursor-control bytes *inside* stdout, which is precisely why
`--nowordwrap` is mandatory — a fixture of that output would only encode a shape
the backend must never ask for.
