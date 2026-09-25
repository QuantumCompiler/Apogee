# Automatic attachments: documents, code and folders

**What / why.** Attach a file, a folder or a glob to a conversation, and Apogee does the rest without being asked: it reads it, decides whether it fits whole, indexes it with the embedding model, and on every later turn hands the model the parts that bear on the question, cited by file and page or line. A small local model with a 32K window can then work over a 300-page PDF or a whole repository, which it cannot do by pasting. Today `chat` and `complete` take `--image` on the first message only; there is no way to attach a document. Ommi's `--file` pasted one file's text into the first message.

This item covers text, code, Markdown, PDF and HTML. Images, audio and video are [media attachments](attachments-media.md), on the same machinery.

**How it works.**
1. **Attach:** `chat --attach <path>` (repeatable), `/attach <path|folder|glob>` mid-chat, `/attachments` to list, `/detach <name>`; `complete --attach`; a machine-mode `attach` message for the GUI.
2. **Read:** text and code as they are (the ingest's binary sniff refuses what is not text); PDF through `pdftotext` with page breaks kept; HTML through the [reader](fetch-url-reader.md) when it has landed and `strip_html` until then. A folder is walked recursively, skipping hidden directories and, inside a git repository, what git ignores.
3. **Index, always,** in the background, into the chat's own collection: chunked by the existing chunker, embedded by the `embedding` role when one is configured, and lexical-only (FTS5) when not, reported as such.
4. **Inline when it fits.** An attachment whose text fits the [context budget](context-budget.md)'s attachment share also enters the conversation whole, at the point it was attached, so the model reads it as the user gave it and the prefix cache keeps it. When it no longer fits, because the budget drops it as history grows, retrieval takes over from the index.
5. **Retrieve every turn** from the chat's collection, through the one retriever resolver, the query rewritten by the [utility model](helper-model-roles.md) when the question depends on earlier turns. Injected transiently, never saved into history, each chunk labelled `report.pdf p. 41` or `src/parser.cpp:120–168`.
6. **Keep it with the chat.** The session records each attachment (path, sha256, type, size, how it was read). The index lives beside the chat and is deleted with it. Before anything is embedded, the sha256 and embedding model are looked up in the other chats' indexes, and a match is copied rather than processed again.

**Core constraint(s).**
- **One retriever per turn, resolved once, reported honestly.** The chat's collection is searched through `resolve_turn_retriever` like any collection; an `auto_rag` collection in the same turn is resolved and reported separately, and nothing claims a retriever that did not run.
- **Transient context never reaches history**, apart from an inlined attachment, which the user placed into the conversation on purpose. The saved transcript records it as an attachment by reference (path and sha256), not a second copy of the text, when it is larger than the inline threshold.
- **Private like sessions.** An attachment's text is as sensitive as the chat it belongs to, so the chat indexes live under a private layout row, declared in `harness/layout.h` in the same commit (the parity rule), and `check` validates their mode.
- **Parity.** `chat`, `complete` and machine mode share one attachment core. Machine mode's `attach` message is documented in [machine-mode.md](../reference/machine-mode.md) and pinned by its conformance check.
- **External converters, optional.** `pdftotext` (already used by `embed ingest`) is found on `PATH`. A missing converter skips that file with its reason, never silently. `check` reports which converters are present.
- **Background work settles before it is needed.** Indexing runs while the user types, on the pattern `BackgroundTitle` set (one model call at a time, settled before the next turn). A turn that needs an unfinished attachment waits with progress on the status line; Ctrl-C answers with what is ready.

**Seam + files.**
- `agentloop/attachments.h/.cpp` (new): the core that reads, decides inline or retrieve (asking the budget), indexes, retrieves and labels. A guarded package, so the embedder and generator arrive as closures, as they do for `knowledge/` and `graph/`.
- `embedstore/ingest.h/.cpp`: a single-file and single-folder entry the attachment core calls, returning per-file reports. PDF page numbers are kept from `pdftotext`'s form feeds.
- `logger/session.h/.cpp`: an `attachments` list in the session, under a `schema_version` bump (an absent list means none).
- `commands/chat.cpp`, `commands/complete.cpp`, `commands/helpers.cpp`: the flags, the slash commands (added to `slash_commands()` so completion offers them), and the machine-mode message.
- `commands/chat_history.cpp` (`chats delete`): removes the chat's index. `commands/check.cpp`: converters, the layout row, index sizes.
- `harness/layout.h`: the private row for chat indexes.

**Reference (Ommi).** Ommi's `chat --file` (CHAT.md) attached one file's text to the first message, whole, with no index and no size handling; this replaces that design rather than porting it. Retrieval, chunking and the resolver are Apogee's own (Milestones Q–S).

**Decisions made:**
- 2026-09-25 — Asked for by the user: attachments ingested by the embedding model and handed to the model automatically.
- 2026-09-25 — **Kept with the chat, cached by file hash** (the user's call, over deleting at the end of a chat or saving every attachment to a named collection).
- 2026-09-25 — **External converters on `PATH`** (the user's call, over bundled libraries).
- 2026-09-25 — Every attachment is indexed, and small ones are also inlined, so that the budget can drop an inline copy later without losing the document.

**Open calls:**
- [default: `complete --attach` indexes into a temporary store removed at exit, and still checks the hash cache] A one-shot has no chat to keep it with.
- [default: a folder over 500 files or 50 MB asks on a terminal and is refused on a pipe] Protects against attaching a home directory by accident.
- [default: `.gitignore` honoured inside a repository, via `git ls-files`] The same `git` the git toolset already uses.
- [default: Word, Excel and PowerPoint files are refused by name for now] Each needs a converter decision of its own; `pandoc` is the likely one.
- [default: retrieval injects up to the budget's share, labelled, with neighbouring chunks merged when adjacent] Contiguous text reads better than fragments.

**Guardrail(s).**
- Inline versus retrieve decided by the budget, table-tested.
- Labels carry page or line ranges.
- A missing `pdftotext` skips the PDF with its reason.
- The hash cache copies instead of re-embedding (asserted by counting embed calls).
- `chats delete` removes the index.
- Resume reuses the index with no embed calls.
- A folder's ignored and hidden files are skipped; the size guard asks or refuses.
- The index directory's mode is private.
- The machine-mode message round-trips.
- Each mutation-tested.

**Acceptance criteria:**
- [ ] `apogee chat -m <Qwen3.8-27B>` with `/attach` on a 300-page PDF answers a question about a detail on one page, citing that page, with the chat's window at 32K.
- [ ] Attaching a small source file puts it in the conversation whole; a later question about it uses no retrieval.
- [ ] Attaching a repository folder, then asking where a function is defined, retrieves the right file and lines.
- [ ] The same PDF attached in a second chat is not embedded again.
- [ ] Resuming the chat and asking again embeds nothing; `apogee chats delete` removes the index.

**Scope note.** Phase 4, item **25d**; build after [25c](context-budget.md). It uses [25b](helper-model-roles.md)'s query rewriting when present, and works without it. Out of scope: images, audio and video ([25e](attachments-media.md)); uploads over `serve`; Office formats; watching an attached folder for changes.
