---
name: apogee-cli-backlog-investigate-bug
description: Investigate a bug in Apogee to root cause with evidence — reproduce it in a sandbox, find the seam in the source, measure the blast radius — then spec the fix as a Maintenance item in the backlog index. Use when the user reports broken or misbehaving behavior ("this is wrong", "look at this transcript", "why does X do Y?", "investigate this bug").
---

# Investigate a Bug and Queue Its Fix

A bug report arrives as a symptom — a transcript, a wrong number, a hang, "very poor performance". This skill turns it into two things: a **root cause with evidence**, and a **Maintenance item** that an agent can later build from without this conversation. It differs from `/apogee-cli-backlog-investigate-spike` on purpose: a spike answers a design question and only reports; a bug investigation ends with the fix **specced and queued** — investigation and item creation in one pass, because a diagnosed bug with no queued fix is how evidence gets lost.

## 1. Ground first

1. [`lib/documentation/backlog/README.md`](../../../lib/documentation/backlog/README.md) — the Maintenance table (a permanent fixture: it exists even when empty), its `M<n>` numbering, the document format and tag semantics.
2. **Scan the backlog for an existing home.** If a pending item already owns this seam, the evidence extends *that* document instead of spawning a near-duplicate — one concern, one home. Likewise check MILESTONES: a bug in something that just shipped may be a regression against a recorded guarantee, which is worth saying in the diagnosis.
3. [`CLAUDE.md`](../../../lib/documentation/assistant/CLAUDE.md) / [`SPEC.md`](../../../lib/documentation/assistant/SPEC.md) — the invariants the fix must honor, and the principles that bound it (e.g. open models run; honesty lives in framing, not refusals).

## 2. Investigate — symptom, root cause, blast radius

Evidence, not inference; the probing rules are the spike skill's:

- **Reproduce it honestly.** A probe that touches state runs against a temp `APOGEE_HOME` under the scratchpad — never the real `~/.apogee`. The user's own transcript is already real-binary evidence; keep its exact bytes (markers, scores, timings) for the document. Shell facts: Bash stdin is a pipe (`</dev/null`), no macOS `timeout` (deadlines inside the script), absolute paths. Real weights only if the bug needs them, per [the model families](../../../lib/documentation/assistant/DEVELOPER.md#on-real-weights-the-model-families); display bugs reproduce better from recorded PTY bytes replayed through a terminal model than from live runs.
- **Find the seam, not just the symptom:** the root cause as `file:line` in the source, and the one place the fix belongs. A signal that pattern-matches a known failure may have a different cause — verify in the code before naming it.
- **Measure the blast radius** where numbers exist: how often, how slow, how wrong, which surfaces (the `models list` 14 s → "a seek per vocabulary string" precedent — the measurement decided the fix). Record what *already works* around the bug too; it bounds the item's scope.
- Probe scripts live in the scratchpad; promote one into `tests/` only if the user asks.

## 3. Write the Maintenance item

One document in `maintenance/`, kebab-case, to `/apogee-cli-backlog-create-item`'s full quality bar, with the bug's specifics:

- **What / why** opens with the motivating evidence verbatim-where-it-matters (the transcript's bytes, the measured numbers, the probe's output) and the root cause with its `file:line` — "measured, not guessed (date, method)".
- **Guardrail(s): the bug becomes a fixture.** The reproduction is replayed as a regression test — recorded bytes, a scripted store, a golden — so the bug cannot return silently. This is the section that distinguishes a bug item; a fix without the pinned reproduction is half an item.
- **Acceptance criteria** include the inverted symptom: the exact failing case from the evidence, now passing, stated observably.
- Open calls tagged as ever — `[user]` only for genuinely user-owned calls, `[default: …]` elsewhere; the diagnosis itself is recorded as dated **Decisions made** entries.
- If the investigation reveals something **bigger than one focused session or not release-agnostic** — product shape, a feature gap, a SPEC collision — stop and say so with the evidence: that is spike/`/apogee-cli-backlog-create-item` territory and the user decides where it goes. Split-first is also available for a genuinely two-session fix.

## 4. Place it and update the docs

1. **Index**: the row takes the next `M<n>` ever assigned, at the Maintenance table's end (claimable at any time by name; a gate only if it truly consumes a pending item). The Maintenance preamble gains the item's one-sentence origin — the table and its preamble are permanent fixtures, present even when the table is empty, so there is always a place to land.
2. **ROADMAP**: one line in the **Maintenance** section, in its style — the evidence summarized, the fix's shape, the link.
3. **MILESTONES is never touched**; SPEC only if the user agreed a principle actually changed (rare for a bug — a bug is behavior diverging from the docs, not the docs changing).
4. Validate with the standard scripted routine (rows ↔ docs, sequences, gates, links) before reporting.

## 5. Report

Tell the user: the symptom → root cause chain with the evidence (file:line, numbers, the probe); the new item's id, path and position; the regression fixture the guardrail pins; every `[user]` call; and anything the investigation found that is deliberately *not* in this item (bigger-than-maintenance findings, adjacent oddities worth their own look).
