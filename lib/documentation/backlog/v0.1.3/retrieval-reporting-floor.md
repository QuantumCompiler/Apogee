# Honest retrieval reporting and the relevance floor

**What / why.** The attachments line misleads, and the injection behind it has no floor. Both halves were measured on the shipped binary (2026-10-03, sandboxed probe over a temp `APOGEE_HOME`): a hybrid turn's scores are Reciprocal Rank Fusion with `kRrfK = 60` (`embedstore/vector.h`), so the *best possible* score — rank 1 in both lists — is 2/61 ≈ **0.033**; the stress-test lines the user read as junk ("top 0.023", "top 0.031") were actually mid-to-high rank agreement, and nothing on the line says so. Meanwhile the injection site (`package_attachments`, `agentloop/rag.cpp`) checks only emptiness: the probe got a **0.000-score excerpt** handed to the model from a one-file attach, and an entirely unrelated question ("What is the capital of France?" over this repo's assistant docs) injected excerpts at **0.950 lexical** — the same score the on-topic question got, because normalized BM25 saturates. Two deliverables, one item: **(a)** the line reports match strength on a scale a person can read — per retriever, since the three scales are incomparable (`agentloop/retriever.h` says exactly this) — keeping the raw score and the retriever tag; **(b)** a per-retriever relevance floor: a turn whose best hit falls below it injects **nothing** and the line says "nothing relevant in the attachments", because misleading context is worse than none — the stress-test model built its hallucination spiral on excerpts that did not answer a structural question.

**Core constraint(s).**
- **One renderer.** Every surface's attachments line comes from the one composer in `cli/helpers.cpp` today; it stays one function, so chat, `complete` and machine mode cannot drift.
- **Scales never mix.** Strength is computed per retriever (RRF, cosine, normalized BM25) and the retriever tag stays on the line — the rule `RagResult::retriever` already states ("a number shown without its retriever invites exactly the comparison that cannot be made") extends to the floor: there is no single threshold constant.
- **The floor is honest, not silent.** Below-floor is a said outcome ("nothing relevant…"), never a quiet absence; the existing principle that answering without retrieved context beats refusing is untouched — the turn still runs.
- **Lexical saturation is respected.** Normalized BM25 cannot tell the probe's irrelevant question from its on-topic one by score; its floor must use a different signal (question-term coverage of the hit) or be honestly absent — never a score threshold that the measurement shows cannot work.
- **Raw facts survive for machines.** Wherever the result reaches a machine surface (28h's JSON reads, machine-mode events), the raw score and retriever ride along; the readable strength is presentation.
- Code style carries: `.h`/`.cpp` pairs, smart pointers only.

**Seam + files.**
- `agentloop/rag.cpp` / `rag.h` — the floor at the two packaging sites (`package_attachments` and the collection path); `RagResult` gains the strength/floored facts the renderer needs.
- `agentloop/retriever.h/.cpp` — the per-retriever strength mapping and floor live beside the scale knowledge (RRF's ceiling from `embedstore/vector.h`'s `kRrfK`, cosine as-is, BM25's coverage signal).
- `cli/helpers.cpp` — the one line renderer: strength first, raw + retriever kept.
- Tests: `tests/business/agentloop/` rag tables — boundary cases per retriever, the floored-turn wording, the one-renderer golden.
- Consumes: 26d's attachment pipeline (shipped), 26c's budget behavior (shipped) — unchanged by this item.

**Reference (Ommi).** No analog: Ommi's knowledge layer (`lib/cli/documentation/internal/KNOWLEDGE.md`) reports hits without a floor or a strength scale — this failure mode was inherited faithfully and is being fixed in Apogee first.

**Decisions made** (dated):
- 2026-10-03 — Found by the attachment-representation spike: no floor at the injection site (a 0.000-score excerpt injected), RRF's 0.033 ceiling misread as percent by user and assistant alike, lexical saturating at 0.950 for an irrelevant question. A Maintenance item because it is release-agnostic polish on a shipped pipeline.
- 2026-10-03 — Floors are per-retriever by measurement, not preference: the probe shows a shared threshold cannot exist (0.000 must floor, 0.023 RRF must not, 0.950 lexical proves nothing).
- 2026-10-03 — Moved into the **v0.1.3** tail as **26s** (the user's call, later the same day), leaving Maintenance's lineage item (M4) as the standing queue's one row.

**Open calls:**
- [default: strength displayed as a word band — `strong / fair / weak match` — computed per retriever (RRF: score ÷ its ceiling for the lists fused; cosine: fixed bands; lexical: question-term coverage), with the raw score and retriever tag kept in parentheses] The display shape.
- [default: floors — RRF below 25% of its ceiling; cosine below 0.25; lexical below one content-word of coverage; each a named constant beside the mapping, tuned freely by tests] The initial floor values.
- [default: the floor applies to attachment turns and `auto_rag` collection turns alike; an explicit `--retriever` request still honors it (the line explains), since the user asked for a method, not for noise] Where the floor applies.

**Guardrail(s).**
- Table tests per retriever at the floor boundary: just-below injects nothing and says so; just-above injects.
- The probe's two cases as fixtures: a 0.000-score hit never reaches the model; an off-topic question over a lexical-only index floors on coverage.
- One-renderer golden: chat, `complete` and machine mode produce the identical line for identical `RagResult`s.
- Machine surfaces keep raw score + retriever (asserted on the JSON/event shape where one exists).

**Acceptance criteria:**
- [ ] A one-line file attached and an unrelated question asked: no excerpt injected, the line reads "nothing relevant in the attachments", and the turn still answers.
- [ ] A hybrid turn ranked top in both lists reports a strong match, not "top 0.032".
- [ ] An on-topic lexical turn injects as today; an off-topic one with saturated scores floors on term coverage and says so.
- [ ] The raw score and retriever remain visible (parenthesized on the human line, fields on machine surfaces).

**Scope note.** Item **26s**, earmarked for **v0.1.3** (the release's end, after 26r; Maintenance's M5 — and before that M6 — until 2026-10-03's moves); gated on nothing pending. Out of scope: reranking changes (the judge's rules are 26b's, untouched); embedding-quality work; the structural map card ([26q](attachment-map-card.md)) and graph wiring ([27n](../v0.1.4/attachment-code-graph.md), [27o](../v0.1.4/attachment-graph-turns.md)), which fix the *other* failure the stress test showed.
