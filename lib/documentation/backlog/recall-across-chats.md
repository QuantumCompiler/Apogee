# Recall across chats

**What / why.** A new conversation knows what earlier ones established, without being told. Each finished chat is summarised by the [utility model](helper-model-roles.md): what was asked, what was decided, the facts and preferences stated, the files involved. The summary is indexed with the embedding model, in the background, when the chat ends or the next time Apogee starts. Every turn then retrieves the few past summaries, and the [knowledge records](../assistant/MILESTONES.md#milestone-y--the-knowledge-layer), that bear on the question, and injects them within the [context budget](context-budget.md), reported on the status line (`[memory] 2 past chats, 1 decision`).

A small model has no memory beyond its window. This gives it the user's history the way `auto_rag` gives it a document collection, automatically. The knowledge layer already stores decisions deliberately captured; recall covers everything else a user would otherwise repeat.

**Core constraint(s).**
- **Never on a server.** `serve` answers remote clients and must never inject one client's history into another's turn, so recall is off on `serve`, structurally rather than by a default someone can flip.
- **Transient, budgeted, reported.** Recalled text is injected like RAG, never written into the new chat's history, trimmed by the budget, and always announced, so a user can see why the model knows something.
- **One retriever resolver.** The chats index is a collection like any other, searched through `resolve_turn_retriever` and reported as such.
- **Private.** Summaries are derived from sessions and inherit their privacy: a private layout row, `0600`, validated by `check`, declared in `harness/layout.h` in the same commit.
- **Deletions propagate.** `chats delete` removes the chat's summary from the index; a chat that is still open is never summarised.
- **Controllable.** `/recall off` for the session, `--no-recall` for a run, `memory.recall` in the config. A chat marked private (`/private`) is never summarised.

**Seam + files.**
- `agentloop/recall.h/.cpp` (new): summarising (a side request to the utility role, with the generator arriving as a closure), indexing, and per-turn retrieval.
- `commands/chat.cpp`: summarise on exit in the background, and the slash commands.
- `commands/chat_history.cpp`: deleting from the index.
- `harness/config.*` and the template: `memory.recall`.
- `harness/layout.h`: the private row for the chats index.
- `commands/check.cpp`: the index's size and mode.
- Machine mode: a `memory` meta event beside the existing retrieval one, in [machine-mode.md](../reference/machine-mode.md).

**Reference (Ommi).** None: Ommi kept sessions and captured knowledge records (which Apogee ported as Milestone Y), but never recalled past conversations automatically.

**Decisions made:**
- 2026-09-25 — Asked for by the user ("Memory across chats").
- 2026-09-25 — After [26b](helper-model-roles.md) (the summariser) and [26c](context-budget.md) (the share it injects within).

**Open calls:**
- [default: on in `chat`, off in `complete` and in agents] A one-shot or an agent's run should be reproducible from its inputs; a conversation benefits from memory.
- [default: summaries of chats with at least two turns] A single question rarely establishes anything worth recalling.
- [default: at most 3 recalled items per turn, within the budget's retrieval share] Recall supports the question; it must not crowd it.
- [default: summaries are regenerated when a chat is resumed and continued] The summary describes the chat as it now stands.

**Guardrail(s).**
- `serve` never recalls (a structural test, not a config test).
- Recalled text never enters history.
- `/private` and `--no-recall` hold.
- `chats delete` removes the summary.
- An open chat is never summarised.
- The index is private.
- Each mutation-tested.

**Acceptance criteria:**
- [ ] After a chat in which the user states their project uses Postgres 16, a new chat asked "which database version should this migration target?" answers 16 and reports one recalled chat.
- [ ] `/recall off` stops it for the session; `apogee chats delete` of the earlier chat stops it for good.
- [ ] `apogee serve` answers the same question without the recalled fact.

**Scope note.** Item **26l**; build after 26b and 26c. Out of scope: editing or pinning memories by hand (the knowledge layer is the deliberate path), and recall across machines.
