---
name: apogee-pull-request
description: Draft the merge-request description for the current branch — what shipped, which backlog items it closed, decisions made, docs updated, how it was verified. Use when the user is opening an MR/PR or asks to "write the PR description", "describe this branch", "draft the merge request".
---

# Apogee Pull Request Description

Drafts the MR description for the current branch against `stable` (the repo's main branch; development happens on version-named branches). In this repo the diff largely narrates itself — a deleted backlog document plus a new MILESTONES entry *is* the story of a shipped item — so the description is assembled from evidence, not memory.

## 0. Precondition

The docs should already be reconciled — if `apogee-document-update` hasn't been run on this branch, run it first. Describing stale docs produces a stale description.

## 1. Gather the evidence

```bash
git log --oneline stable..HEAD
```

```bash
git diff --stat stable...HEAD
```

Plus `git status` — warn if uncommitted work would be missing from the MR. Then read the branch's documentation deltas, which carry the narrative:

- **Backlog documents deleted** on this branch + their new [`MILESTONES.md`](../../../lib/documentation/assistant/MILESTONES.md) entries → the items this MR **ships** (pull each item's goal and acceptance criteria from the milestone entry).
- **Index rows marked in progress** → items **advanced but not finished**; say what state they're left in.
- **New dated entries** in item documents' Decisions made → the decisions this branch settled (including exercised `[default:]` calls).
- [`ROADMAP.md`](../../../lib/documentation/assistant/ROADMAP.md) checkbox/section changes → release-level framing.
- New/changed files under `lib/src/`, `lib/scripts/`, `.claude/skills/` → the implementation surface.

## 2. Write the description

Structure (omit empty sections; keep it readable — a reviewer who knows nothing of this session should understand the branch from the description alone):

```markdown
## Summary
<2–4 sentences: what this branch delivers and why, in user-facing terms.>

## Backlog items
- **Shipped:** <item> — <its goal>; acceptance criteria met (see MILESTONES.md → <milestone>).
- **Advanced:** <item> — <state it's left in>.

## Decisions made on this branch
- <date> — <decision + where it's recorded>.

## Documentation
<Docs updated, per the Documentation and Status flow — one line each.>

## Verification
<How it was tested: `lib/scripts/cicd.sh --test` result, platforms exercised, notable fixtures/PTY/lsof suites touched.>

## Review notes
<Where reviewer attention pays: risky seams, invariants touched, deliberate divergences.>

## Open questions
<Any [user] open calls surfaced or deliberately deferred by this branch.>
```

Suggest an MR **title** in the repo's style: short, imperative, naming the shipped item(s) (e.g. `v0.1.0: config engine — comment-preserving loader + apogee config`).

## 3. Deliver

Present the drafted description to the user as the final output. Do **not** push, open the MR, or run `gh pr create` unless the user explicitly asks — the target is `stable`, and opening it is the user's call. If they do ask, use the drafted description as the body verbatim.
