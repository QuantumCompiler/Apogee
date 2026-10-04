# The attachment code graph

**What / why.** When a folder of source is attached to a chat, its *structure* should become queryable, not just its text searchable. [27k](code-graph-extraction.md) builds the engine — tree-sitter parsing a source tree into the existing `kg_*` tables, model-free end to end, incremental by content hash. This item points that engine at attachments: a folder attach whose files include a supported language **builds the code graph into the chat's own attachment store** (`attachments/<chat id>.db` — an ordinary embedstore database, so the schema is already the right one), in the background worker that already indexes the chunks, with one notice line when it lands. Feasibility is measured, not assumed (2026-10-03 spike, sandboxed probe on the shipped binary): this repo's full C++ tree — 429 files, 5.4 MB — chunk-indexed inside a **1.7 s** end-to-end run, and a tree-sitter pass is the same order of work; attach-time graph building is comfortably interactive. The payoff item is [27o](attachment-graph-turns.md): expansion and the graph toolset over this graph. This one delivers the build: deterministic, free, and invisible except for the notice.

**Core constraint(s).**
- **Zero model calls — 27k's guarantee carries whole.** The attachment graph is built, resolved and updated with no LLM, no embedder, no key, no network; a chat with no embedding model configured (lexical-only index) still gets its graph.
- **One extractor.** The build calls 27k's `graph/` core over the attachment's **already-walked file list** — the `FoundFiles` the attach produced, which already honors `git ls-files` and hidden-entry skips — never a second walk with its own filter bugs. Byte-equivalence with a direct `graph build --source` over the same files is the test.
- **The worker's discipline holds (26d):** the graph build runs on the `ChatAttachments` worker after chunk indexing, `settle()` waits for it, Ctrl-C keeps what is ready and leaves the graph honestly absent (said, never half-trusted); it never delays the prompt beyond what attach already does.
- **The store stays the chat's.** Graph rows live in the chat's attachment database and die with it (`remove_index` already deletes the file — the graph goes free). Unsupported languages are skipped and counted by name (27k's rule); a tree with no supported language builds no graph and says nothing beyond the normal attach lines.
- **Never on `serve`, nothing listens** — the never-listens invariant is untouched; this is all in-process work on existing files.
- Code style carries: `.h`/`.cpp` pairs, smart pointers only; tree-sitter handles stay behind 27k's boundary class.

**Seam + files.**
- `commands/chat_attachments.cpp/.h` — the worker queues a graph pass per settled folder attachment (supported-language files only); `describe()` gains the graph's one-line state for `/attachments`; the notice through the existing `say` hook.
- `graph/build.h/.cpp` (27k's) — a build entry over an injected file list (the source-root build parameterized by explicit files), shared verbatim with `graph build --source`.
- `agentloop/attachments.h/.cpp` — the supported-language test over `FoundFiles` (by extension against 27k's grammar set), pure and table-tested.
- Tests: `tests/commands/` chat-attachment tests over the fixture mini-repo 27k commits; the byte-equivalence check against a direct build; the cancel case.
- Consumes: [27k](code-graph-extraction.md) (the extractor, the language set — its `[user]` call is **not reopened here**: whatever grammars 27k vendors are what attachments parse); 26d (shipped: the walk, the worker, the store).

**Reference (Ommi).** No analog: Ommi has neither chat attachments nor code-AST extraction (both recorded already — 26d and [27k](code-graph-extraction.md)). External prior art is Graphify's, inherited through 27k.

**Decisions made** (dated):
- 2026-10-03 — Split from the attachment-representation spike: the spike found the graph machinery collection-keyed and the attachment store outside it, with the store schema already shared — so attach-triggered building is wiring, not architecture. Targeted v0.1.7 with its track.
- 2026-10-03 — **Default on, with a notice** (the user delegated the spike's call; recorded as the default, vetoable): a folder attach with supported languages builds its graph automatically — it is seconds, free and model-free; [27p](attachment-options.md) adds the off switch and the config default.
- 2026-10-03 — **Per-chat, no cross-chat graph copying** (the delegated lifetime call): the chunk hash-cache copies embeddings across chats because embedding costs a model; the graph costs seconds of deterministic parse, so rebuilding beats a copy mechanism's complexity. The graph lives and dies with the chat's index.
- 2026-10-03 — `complete`'s one-shot temporary store skips the graph build by default: a one-shot turn has no follow-ups to walk the graph in; 27p's flag can force it.

**Open calls:**
- [default: a re-attach of the same folder into the same chat runs 27k's incremental update over the content hashes — only changed files re-parse] Re-attach behavior.
- [default: the notice is one line through the existing hook — `graph: N nodes, M edges (supported: cpp, py; skipped: 3 files)` — and `/attachments` repeats it] Surfacing.

**Guardrail(s).**
- Byte-equivalence: the attachment-built graph over the fixture tree matches `graph build --source` over the same file list exactly (nodes, edges, mentions).
- The model-free pass: a sandbox with zero backends and no embedder attaches the fixture tree and gets the full graph.
- Cancel mid-build: index usable, graph absent, the absence said; the next attach completes it.
- A no-supported-language folder: attach behaves exactly as today, zero graph rows.

**Acceptance criteria:**
- [ ] Attaching `lib/src/cli/source` in a sandboxed chat (temp `APOGEE_HOME`) prints the one notice line, and the chat's attachment database holds the same `kg_*` counts a direct 27k build reports.
- [ ] The same attach with no embedding model configured succeeds identically (lexical index + full graph).
- [ ] Touch one file, re-attach: only it re-parses (27k's update counts), and the graph converges to a fresh build.
- [ ] Deleting the chat removes the graph with the index file — nothing survives it.

**Scope note.** Item **27n** (30d under the then-v0.1.7, 31n under the then-v0.1.8, until 2026-10-03's merge and migration — the user's calls), earmarked for **v0.1.4**; **gated on [27k](code-graph-extraction.md)**. Out of scope: using the graph on turns and the toolset scoping ([27o](attachment-graph-turns.md)); flags and config defaults ([27p](attachment-options.md)); prose/LLM graph enrichment of attachments (the metered extractor stays a collection feature); watch modes.
