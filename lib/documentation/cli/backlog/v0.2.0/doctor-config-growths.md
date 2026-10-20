# The doctor and the config catch up: ffmpeg's filters, scalar keys, forgetting recall, typed model documents

**What / why.** Four smaller gaps, found by the design's contract board and the GUI spike's probes (2026-10-20): **(G14)** `check` says nothing about ffmpeg's assembly abilities — whether captions can burn (libass) or the final render can encode (x264) is learned at the first composition's failure; **(G15)** plain scalar config keys have no setter — the web-search address and `memory.recall` are read by `config get` and set only in an editor, so the app's Settings toggles shell out to nothing; **(G16)** there is **no way to forget the recall index** (the spike swept the help tree: no command) — the design's "Forget all session summaries" resolves to a real gap; **(G17)** `models info <set> --output-format json` answers `{name, fields[]}` of display strings — the spike measured a safetensors set carrying lineage as prose but **no window, cache or byte fields at all** — so the app's "memory at a window" and made-from views would parse sentences. This item closes all four: `check` gains rows probing ffmpeg's encoders and filters through the seam's one child, each row with its fix; **`config set <key> <value>`** lands for an **allow-listed scalar set** through the one editor (a secret-shaped key refused by name toward `auth add`); a recall-forget verb clears the index with consent, named at build (`chats recall --forget` the working shape); and the model-set document gains a **typed `data` object** beside the display fields — format, bytes, window and cache arithmetic where the format knows them (the GGUF reader's and `mlx_info`'s facts), lineage as structured entries — additive, the prose fields kept.

**Core constraint(s).** **One config mutation path**: `config set` is a transform in `contracts/config_edit.h` like every sibling, comment-preserving, allow-listed keys only — the allow-list a declared table, a key outside it refused naming the right door (`auth add` for anything secret-shaped, the typed verbs where one exists). **The doctor's honesty rules**: a probe that cannot run reports skipped, never passing; every FAIL row's hint is a runnable command (M14's bar). **Typed beside prose, additively** (28h): the `fields[]` display stays byte-identical; unknown stays unknown — no guessed sizes (the store's law). The recall-forget verb is destructive-mannered (shown plan, asked, `--yes`), classified (ADR 0010), completed (ADR 0007); the admin config slice gains `set`'s twin through the same transform (ADR 0002), the recall verb stays CLI-only (26l).

**Seam + files.** `presentation/cli/check.cpp` (+ the ffmpeg rows over `platform/ffmpeg`'s probe), `data/contracts/config_edit.h` (+ `set_scalar` and its allow-list table), `presentation/cli/config_cmd` + the admin twin, the recall index's owner (`agentloop/recall` + its store) for the forget verb, `presentation/cli/models_pull.cpp`/`operations/read_views` (the typed `data` on the set document, from `modelstore/`'s readers), `machine-mode.md` (28h rows). Tests: the doctor rows over a scripted probe (present/missing/skipped); the transform's golden round-trips per allow-listed key and the refusal table; the forget verb's plan/ask/absence; the typed document over fixture sets of each format with the prose fields byte-pinned; parity twins byte-identical.

**Reference.** The contract board's G14/G15 rows, the spike's probes (G16's empty sweep, G17's measured `{name, fields[]}`); the one-mutation-path invariant; M14 — runnable doctor hints; 26a/27b — the window and cache facts the typed document surfaces.

**Decisions made** (dated):
- 2026-10-20 — Specced from the GUI design spike, the user's call; the two verify-rows resolved to gaps by probe are owned here (G16, G17).
- 2026-10-20 — `config set` is allow-listed, never arbitrary: the config's typed units keep their typed verbs, secrets keep `auth add`.

**Open calls:**
- [default: the scalar allow-list's first entries (the web-search address, `memory.recall`, and peers found at build) declared as one table the refusal quotes.]
- [default: the forget verb's name and placement (`chats recall --forget` the working shape) at build; it clears summaries only, never transcripts.]

**Guardrail(s).** Transform goldens and refusals; doctor-row matrix with skipped-never-pass; the forget absence pin; typed-document conformance with prose byte-identity; admin twin equality; no network, no models.

**Acceptance criteria:**
- [ ] `check` reports ffmpeg's encode/filter readiness with runnable fixes, skipped where unprobeable; `config set` moves the allow-listed keys comment-preservingly and refuses the rest by name.
- [ ] The forget verb empties the recall index with consent and leaves transcripts; `models info <set> --output-format json` carries the typed `data` beside unchanged prose fields.
- [ ] machine-mode.md, the schema and the admin twin agree; the full suite is green.

**Scope note.** Earmarked for v0.2.0 (track 52); ungated. Out of scope: non-scalar config writes (the typed verbs own them), a recall re-summarize, G10's suite-fit read (deferred with its Later marking — the app computes it until asked).
