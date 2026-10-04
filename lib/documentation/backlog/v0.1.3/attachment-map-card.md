# The attachment map card

**What / why.** A folder attach hands the model content with no map. The retrieval pipeline (26d) injects per-turn excerpts, the `@` mention rides the message untouched (`mentioned_paths`, `cli/chat_attachments.h` — "The message itself is never changed"), and nothing anywhere tells the model what the attached tree *looks like*. The stress test of 2026-10-03 showed the cost: a model attached this whole repository was asked how its RAG works, invented `lib/src/core/`, failed, re-guessed, and cycled its thinking block until the turn died. This item gives a folder (or glob) attachment a **one-time structural card** that rides the attaching message the way an inlined file does: the attachment's root as the citable prefix, the directory tree to a shallow depth with per-directory file counts, the language/extension mix, and the totals already printed to the user (N files, M chunks). Deterministic, built from the `FoundFiles` walk that already happened, zero model calls, capped small. A model that knows `source/business/agentloop/` exists does not invent `lib/src/core/` — and a question it can't answer from excerpts becomes "look in one of these real places" instead of a guess.

**Core constraint(s).**
- **No model calls, no second walk.** The card is rendered from the `FoundFiles` list the attach already produced (names are already working-directory-relative); it never re-reads the disk and never touches a model.
- **Capped by construction:** depth 2, a bounded line count, overflow folded into counted "… and N more" lines — a map that scrolls is a context bomb pretending to help.
- **It rides like an inline attachment.** The card joins the attaching user message the way `render_inline_attachment` blocks do (the `pending_inline_` mechanism), costs its tokens against the attachment share (`fits_inline` / the budget rules of 26c), and persists with history — citations stay stable. Single-file attachments get no card; they are their own map.
- **Compaction honesty (26d's rule extends):** after compaction the message the card rode is gone; the card is re-ridden once on the next user message, as inlined attachments fall back to retrieval.
- Code style carries: `.h`/`.cpp` pairs, smart pointers only.

**Seam + files.**
- `agentloop/attachments.h/.cpp` — `render_map_card(name, files, caps)` beside `render_inline_attachment`: pure, golden-testable.
- `cli/chat_attachments.cpp` — `record()` builds the card for a settled folder/glob attachment and queues it through the existing pending-inline path; `after_compaction()` re-queues it once.
- `cli/complete.cpp` — the one-shot surface gets the same card through the same class (it already drives `ChatAttachments`); nothing bespoke.
- Tests: `tests/business/agentloop/attachments_test.cpp` golden cards over a fixture tree; cap/overflow tables; the compaction re-ride in the chat tests.
- Consumes: 26d (shipped) for the walk, inline mechanism and share; 26c (shipped) for the budget.

**Reference (Ommi).** No analog — Ommi has no chat-attachments pipeline (its knowledge layer is collection ingest + query, `lib/cli/documentation/internal/KNOWLEDGE.md`); the attach-with-a-map shape is Apogee's own, motivated by the stress-test transcript.

**Decisions made** (dated):
- 2026-10-03 — Split from the attachment-representation spike as the no-dependency half of "give the model a map": the card needs no graph, no new vendored code, and would have prevented the stress test's invented paths on its own. Placed at the v0.1.3 tail because it is small, self-contained, and sharpens a shipped v0.1.3 feature (26d).
- 2026-10-03 — The card is persisted history, not a transient prefix: structure is a stable fact of the attachment, and re-injecting it per turn would spend the retrieval share on something that never changes.
- 2026-10-03 — Confirmed (the default taken, the user's confirmation): depth 2 and 30 tree lines, named constants; overflow folds into counted lines.
- 2026-10-03 — Confirmed (the default taken, the user's confirmation): the mix line is extension counts, no content sniffing — instant and deterministic.

**Guardrail(s).**
- Golden card for a committed fixture tree, byte-exact, including the overflow folds.
- Cap tables: a deep tree, a flat 1000-file folder, a glob — none exceeds the line budget.
- The compaction test: card present on the attaching message, re-ridden exactly once after compaction.
- On real weights, the capability and the measure: with a source tree attached, the model families' first answer to a structure question cites only paths that exist under the attachment root (the stress-test failure, inverted into the check).

**Acceptance criteria:**
- [ ] Attaching `lib/src/cli/source` in a sandboxed chat rides a card naming the real packages (`agentloop/`, `commands/`, `embedstore/`, …) with counts, within the caps.
- [ ] A single-file attach produces no card; a glob attach produces one rooted at the glob's spelling.
- [ ] The card's tokens count against the attachment share, and an unknown-window backend (where nothing is inlined) still gets the card only if it fits — never unconditionally.
- [ ] After `/compact`, the next message carries the card again, once.

**Scope note.** Item **26q**, earmarked for **v0.1.3** (the release's end, after 26p); gated on nothing pending. Out of scope: any graph machinery (the walkable structure is [27n](../v0.1.4/attachment-code-graph.md)/[27o](../v0.1.4/attachment-graph-turns.md)); score display and floors ([26s](retrieval-reporting-floor.md)); changing what the `@` mention sends.
