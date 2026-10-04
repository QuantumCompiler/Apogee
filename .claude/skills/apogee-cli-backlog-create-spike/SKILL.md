---
name: apogee-cli-backlog-create-spike
description: Turn a spike's findings into the backlog — create every document in the spike's proposed split, with the evidence baked in, placed where the user says. The findings come from the conversation (a spike just run) or from an attached/named findings write-up. Use when the user says "execute the spike", "create the documents from the spike", "turn those findings into items".
---

# Create the Backlog Documents from a Spike

A spike ends as a report: walls with evidence, measurements, a proposed split, and the user's calls. This skill is the step after — the whole split becomes real backlog documents in one pass. It is the batch analog of `/apogee-cli-backlog-create-item`: that skill's quality bar applies **to every document**, and this one adds what a spike-born set needs — the evidence carried in, the user's answers recorded, the gates wired between siblings, and the track's story told in the index. It creates **documents, never code**: building remains `/apogee-cli-backlog-execute-item`'s job, later, one item at a time.

## 1. Gather the findings

The input is wherever the spike's findings live:

- **This conversation** — the usual case: the `/apogee-cli-backlog-investigate-spike` report earlier in the chat (walls, measurements, the document list, the user's answers since). Context-only spikes are *why this skill exists*: the documents become the only durable record, so nothing from the report may be left behind in the chat.
- **An attached or named write-up** — a findings document the user attaches or points to. Read all of it before writing anything. Treat it as the report: same walls-evidence-split shape expected; where it's thinner than that, say what's missing rather than inventing it.

Either way, extract before writing: the wall list with each wall's evidence, every measurement with its number, the proposed split (documents, target tables, gates), the questions asked — and which ones the user has since answered, in the conversation or in the write-up's margins.

## 2. Confirm the split and the placement

- **The split is the contract**, but the user may have changed it since the report — fewer items, different tables, a different release (possibly via `/apogee-cli-backlog-swap-version` first). Their latest word wins; re-derive ids from the destination tables' live sequences (release-prefix next letter ever assigned, or next `M<n>`/`A<n>`), never from the report's suggestions.
- **Placement is the user's call**, exactly as in `/apogee-cli-backlog-create-item`: they name the table(s); ask only where they genuinely haven't said.
- **A SPEC collision found by the spike blocks the set.** If a finding needs a SPEC revision (a non-goal touched, product shape changed), that revision is settled with the user **before** the documents exist — the amendment, dated, goes in first.

## 3. Write the documents — the spike's deltas on the quality bar

Every document follows the backlog README's format and `/apogee-cli-backlog-create-item`'s per-section bar (ground in README/CLAUDE/SPEC/ROADMAP first; kebab-case filename in the target table's subdirectory; link depths per the standing rules). On top of that:

- **Evidence is baked in, with its numbers.** Each document's What/why cites the spike's concrete measurements and `file:line` evidence for the walls *it* closes — "measured, not guessed (date, method)" — so the item builds without this conversation. A measurement that shaped the design appears where it did the shaping.
- **Walls map to items, visibly.** Each document says which walls it removes; a wall one root-cause fix collapses is claimed by exactly one document, and siblings reference it rather than re-owning it.
- **The user's answers become dated decisions**; recommendations they let stand become `[default: …]` with the recommendation recorded; genuinely user-owned questions still open become `[user]` calls that block that item's build. Nothing the spike settled is reopened — mark it `(consumed decision)` where a sibling owns it.
- **Gates wire the siblings**: the split's build order becomes 🔒 gates among the new items (name real items, no cycles); an item useful on its own carries no gate just to mirror the narrative.
- **One Reference (Ommi) pass for the set** — the spike usually checked the analog already; cite what it found per document, and an honest "no analog" where there is none.

## 4. Place the set and update the docs

1. **Index** ([backlog README](../../../lib/documentation/backlog/README.md)): one row per document in its table (creating a table and its directory for a first item), ids ascending, gates in the Status cells — and **the track preamble tells the spike's story**: when it ran, what it measured (the headline numbers), how it split, and which calls are recorded where. That paragraph is what lets a future agent trust the documents without the chat.
2. **[ROADMAP.md](../../../lib/documentation/assistant/ROADMAP.md)**: the release's section (or Maintenance/Architecture) gains one line per item; a new release section is introduced with the spike summarized the way the existing sections do it.
3. **SPEC.md** only per step 2's rule — the dated revision agreed with the user, nothing else.
4. **MILESTONES.md is never touched** — unless the spike itself shipped a kept probe or a milestone-recorded run, in which case that record already exists and is only cited, never written here.

## 5. Validate before reporting

The standard scripted routine, never by eye: rows ↔ documents both ways; Version cells match directories; every sequence ascends; every gate (including the new sibling gates) names a real pending id; the full relative-link sweep (the two illustrative MILESTONES lines are known false positives); every measurement quoted in a document appears with the same number the report gave.

## 6. Report

Tell the user: every document's path, id, table position and gate; which walls each one closes; which answers were recorded as decisions, which as defaults; **every `[user]` call across the set** — those are the questions that will block builds later; which docs were updated (index, ROADMAP, SPEC if revised); and the validation results.
