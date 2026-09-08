# RAG config: auto-registered collections and always-on injection

**What / why.** **The retrieval floor shipped 2026-09-07** ([MILESTONES.md](../assistant/MILESTONES.md) → Milestone Q): a SQLite/FTS5 chunk store, BM25 retrieval that needs no model or network, `apogee embed ingest|query|list|info|delete`, and `--rag` on `complete` and `chat` splicing context into the outgoing request without touching persisted history. What remains is the **config layer**, which was the one acceptance criterion left unmet.

**Do not re-plan the shipped parts.** The store, the FTS index, the chunker, the ingest path, the CLI, and the `--rag` wiring exist and are test-locked.

## 1. An `embeddings:` config section, auto-registered

Today a collection is addressed only by name on the command line. It should also be a config entry, so a collection can carry settings (its chunk sizes, later its embedding backend) and so `apogee config get` can show what exists.

- **Auto-registration:** `apogee embed ingest <name> <path>` on a collection the config does not mention adds the entry.
- **Through the comment-preserving helpers, always.** `harness/config_edit.h` is the only path that writes a config file ([⚠ One config mutation path](../assistant/CLAUDE.md#-one-config-mutation-path)); load-modify-save would delete the user's comments. The acceptance criterion is a **byte diff** proving everything but the new entry is untouched.
- The section is also where `embedding-clients` will later hang a per-collection backend, so its shape wants a moment's thought now rather than a migration then.

## 2. `auto_rag` — injection without the flag

A config key naming a collection to retrieve from on every turn, read at turn build, so a user whose whole workflow is one corpus does not type `--rag notes` forever.

- **Read at turn build, not at startup**, so editing the config mid-session takes effect on the next turn like every other config value.
- `--rag` on the command line overrides it; `--rag ""` disables it for one run. A key that cannot be turned off for a single invocation is a key people stop using.
- **The status line must still report** chunks, top score and retriever when injection came from config rather than a flag. Silent injection is the failure mode here: a user who does not know context was added cannot tell why an answer went sideways.

**Core constraint(s).** Unchanged, and binding on both:
- **One config mutation path.** Every write goes through `config_edit.h`; nothing else touches a config file.
- **Retrieval never depends on a model.** Whatever this adds must keep working with no embedding backend configured.
- **Every surface reports which retriever produced a score.** Scales are incomparable.
- **Injected context stays transient**, on the config path exactly as on the flag path.
- Mutating embed actions sit in the admin plane's documented-skip list until their HTTP twins are backfilled ([admin-plane-foundation.md](admin-plane-foundation.md)); this area owns that backfill.

**Seam + files.** Extended: `harness/config.h/.cpp` (an `embeddings:` section), `harness/config_edit.h/.cpp` (an add-collection helper beside the existing add-backend one), `commands/embed.cpp` (register on ingest), `commands/chat.cpp` and `commands/complete.cpp` (read `auto_rag` at turn build), `assets/config.yaml` (document both).

**Reference (Ommi).** `src/embedstore` for the collection-config shape, and `cmd/ommi embed.go` for how registration and the CLI interact.

**Decisions made** (dated):
- 2026-08-24 — Lexical first, because Ommi's RAG survived an embedding freeze only where it was model-free.
- 2026-09-07 — **SQLite fetched rather than found on the system** (recorded default, confirmed against the target list: Windows ships none, and FTS5 is a compile-time flag).
- 2026-09-07 — **UTF-8 handled without utf8proc**, deviating from the recorded default: chunking needs codepoint boundaries, not normalisation.
- 2026-09-07 — **Query terms OR-joined**, found by running it: implicit AND returns nothing for any real question, because a question carries words its answer does not.
- 2026-09-07 — **The floor shipped and this document was reduced to the config layer** rather than deleted; one criterion was unmet and the queue should not claim work that does not exist.

**Open calls:**
- [default: per-collection, under `embeddings:`] Whether chunk sizes live on the collection entry or stay flags. A corpus of ADRs wants 768 where prose wants 512, and re-typing that on every ingest is how corpora end up inconsistent.
- [default: a single collection name] Whether `auto_rag` may name several collections. Multiple means merging incomparable score scales, which is `vector-hybrid-rerank`'s problem and should not be pre-empted here.

**Guardrail(s).** The **byte-diff test** on auto-registration: a comment-dense config gains exactly the new entry and nothing else moves. A test that `--rag` on the command line beats `auto_rag`, and that `--rag ""` disables it. The transient-history grep extended to the config path, so injection-without-a-flag is held to the same rule. Every guardrail mutation-tested.

**Acceptance criteria:**
- [ ] `apogee embed ingest` on an unregistered collection writes an `embeddings:` entry through the config-engine helpers, verified by byte diff over a comment-dense fixture
- [ ] `auto_rag` injects on every turn with no flag, and the status line still reports chunks, top score and retriever
- [ ] `--rag` overrides `auto_rag`, and `--rag ""` disables injection for one run
- [ ] Injected context still never reaches persisted history when it came from config

**Scope note.** Gated ring, RAG track, the residue of the first of three. **The floor shipped 2026-09-07.** No gate: this is config-layer work needing no model. [embedding-clients.md](embedding-clients.md) gates on the floor, which now exists, so it is claimable independently of this. Out of scope: everything already shipped — the store, the index, the chunker, ingest, the CLI, and the `--rag` flag. Read Milestone Q before touching any of it.
