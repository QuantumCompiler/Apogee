---
name: apogee-backlog-investigate-bug
description: Investigate a bug in Apogee — CLI or GUI — to root cause with evidence: reproduce it in a sandbox, find the seam, measure the blast radius, then spec the fix into that application's backlog as a maintenance-natured item; the skill resolves which application and asks when it cannot. Use when the user reports broken or misbehaving behavior ("this is wrong", "look at this transcript", "why does X do Y?", "investigate this bug").
---

# Investigate a Bug and Queue Its Fix

A bug report arrives as a symptom — a transcript, a wrong number, a hang, "very poor performance". This skill turns it into two things: a **root cause with evidence**, and a **Maintenance item** that an agent can later build from without this conversation. It differs from `/apogee-backlog-investigate-spike` on purpose: a spike answers a design question and only reports; a bug investigation ends with the fix **specced and queued** — investigation and item creation in one pass, because a diagnosed bug with no queued fix is how evidence gets lost.

## 0. Which application?

Apogee's documentation is per application ([the router](../../../lib/documentation/CLAUDE.md)): the CLI's home is [`lib/documentation/cli/`](../../../lib/documentation/cli/README.md) and the GUI's is [`lib/documentation/gui/`](../../../lib/documentation/gui/README.md). Resolve which queue this invocation means before touching anything, in order:

1. **The arguments.** An id or table names its queue — the CLI's version tables (`v0.1.<x>`, `v0.2.0`) with their numeric tracks (`52a`) and the standing `M<n>`/`A<n>` queues; the GUI's index with its lettered tracks (`1e`). A named document resolves by which backlog holds it. A bare version (`v0.2.0`) can mean either queue — both carry work per release.
2. **The conversation.** What the work touches: the harness's commands, backends and seams are the CLI's; screens, windows, the design and the machine-mode client are the GUI's. Work that needs both gets cross-queue siblings, gated both ways.
3. **Still ambiguous? Ask.** One AskUserQuestion — the CLI's queue or the GUI's — before anything is treated as decided. Never guess between them.

Everything below reads from the chosen home, written `<home>`: its backlog README is the authority on that queue's tables, numbering, document format and link depths, and its assistant docs (`<home>/assistant/CLAUDE.md` first) carry the invariants. Where this skill says `<home>`, substitute the chosen application's directory.

## 1. Ground first

1. `<home>/backlog/README.md` (`<home>/backlog/README.md`) — where a fix lands in the home (the CLI's Maintenance queue with its `M<n>` numbering; the GUI's maintenance-natured rows, a table born with its first item), the document format and tag semantics.
2. **Scan the backlog for an existing home.** If a pending item already owns this seam, the evidence extends *that* document instead of spawning a near-duplicate — one concern, one home. Likewise check MILESTONES: a bug in something that just shipped may be a regression against a recorded guarantee, which is worth saying in the diagnosis.
3. `CLAUDE.md` (`<home>/assistant/CLAUDE.md`) / `SPEC.md` (`<home>/assistant/SPEC.md`) — the invariants the fix must honor, and the principles that bound it (e.g. open models run; honesty lives in framing, not refusals).

## 2. Investigate — symptom, root cause, blast radius

Evidence, not inference; the probing rules are the spike skill's:

- **Reproduce it honestly.** A probe that touches state runs against a temp `APOGEE_HOME` under the scratchpad — never the real `~/.apogee`. The user's own transcript is already real-binary evidence; keep its exact bytes (markers, scores, timings) for the document. Shell facts: Bash stdin is a pipe (`</dev/null`), no macOS `timeout` (deadlines inside the script), absolute paths. A GUI probe drives the installed `apogee` binary with the scripted mock backend. Real weights only if a CLI bug needs them, per the model families rules (`lib/documentation/cli/assistant/DEVELOPER.md` → On real weights); display bugs reproduce better from recorded PTY bytes replayed through a terminal model than from live runs.
- **Find the seam, not just the symptom:** the root cause as `file:line` in the source, and the one place the fix belongs. A signal that pattern-matches a known failure may have a different cause — verify in the code before naming it.
- **Measure the blast radius** where numbers exist: how often, how slow, how wrong, which surfaces (the `models list` 14 s → "a seek per vocabulary string" precedent — the measurement decided the fix). Record what *already works* around the bug too; it bounds the item's scope.
- Probe scripts live in the scratchpad; promote one into `tests/` only if the user asks.

## 3. Write the Maintenance item

One document in `maintenance/`, kebab-case, to `/apogee-backlog-create-item`'s full quality bar, with the bug's specifics:

- **What / why** opens with the motivating evidence verbatim-where-it-matters (the transcript's bytes, the measured numbers, the probe's output) and the root cause with its `file:line` — "measured, not guessed (date, method)".
- **Guardrail(s): the bug becomes a fixture.** The reproduction is replayed as a regression test — recorded bytes, a scripted store, a golden — so the bug cannot return silently. This is the section that distinguishes a bug item; a fix without the pinned reproduction is half an item.
- **Acceptance criteria** include the inverted symptom: the exact failing case from the evidence, now passing, stated observably.
- Open calls tagged as ever — `[user]` only for genuinely user-owned calls, `[default: …]` elsewhere; the diagnosis itself is recorded as dated **Decisions made** entries.
- If the investigation reveals something **bigger than one focused session or not release-agnostic** — product shape, a feature gap, a SPEC collision — stop and say so with the evidence: that is spike/`/apogee-backlog-create-item` territory and the user decides where it goes. Split-first is also available for a genuinely two-session fix.

## 4. Place it and update the docs

1. **Index**: the row takes the home's next maintenance id ever assigned, at its table's end (claimable at any time by name; a gate only if it truly consumes a pending item); the preamble gains the item's one-sentence origin, and a home whose maintenance table does not exist yet gains it with this first item, the CLI's conventions mirrored.
2. **ROADMAP**: one line in the home's matching section, in its style — the evidence summarized, the fix's shape, the link.
3. **MILESTONES is never touched**; SPEC only if the user agreed a principle actually changed (rare for a bug — a bug is behavior diverging from the docs, not the docs changing).
4. Validate with the standard scripted routine (rows ↔ docs, sequences, gates, links) before reporting.

## 5. Report

Tell the user: the symptom → root cause chain with the evidence (file:line, numbers, the probe); the new item's id, path and position; the regression fixture the guardrail pins; every `[user]` call; and anything the investigation found that is deliberately *not* in this item (bigger-than-maintenance findings, adjacent oddities worth their own look).
