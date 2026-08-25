---
name: apogee-document-update
description: Pre-MR documentation reconciliation — audit every doc in the repo against what the current branch actually changed, fix drift, and report what still needs the user. Run before opening a merge request ("update the docs", "docs pass before the MR", "reconcile documentation", "about to open an MR/PR").
---

# Apogee Document Update (pre-MR pass)

Apogee's docs are the source of truth — a merge request whose docs lag its code ships drift that the next agent will faithfully build on. This skill reconciles every document against the branch's actual changes before the MR is opened.

## 1. Establish what this branch changed

```bash
git diff --stat stable...HEAD
```

plus `git status` for anything uncommitted. The diff is the audit scope: every claim the docs make about changed areas gets checked; docs about untouched areas are left alone (no drive-by rewrites — small diffs review better).

## 2. Walk the documents against the diff

Work through the system in this order (read [`CLAUDE.md`](../../../lib/documentation/assistant/CLAUDE.md) → Documentation and Status first — it defines the flow this skill enforces):

1. **Backlog** ([`lib/documentation/backlog/`](../../../lib/documentation/backlog/README.md)):
   - Any item this branch **completed** → its work is recorded in MILESTONES.md, its document is **deleted**, its index row removed, and the phase numbering/track preamble renumbered. A completed item still sitting in the backlog is the system's cardinal violation.
   - Any item this branch **started** → index row marked in progress.
   - Any **gate the branch satisfied** → downstream items' status lines updated.
   - Any decision made while building → a dated entry in the affected item's **Decisions made** (and `[default:]` calls that were exercised recorded as decisions).
2. **[MILESTONES.md](../../../lib/documentation/assistant/MILESTONES.md)** — shipped work folded into the right lettered milestone (goal, what was built, trade-offs), or a new milestone for a genuinely new area.
3. **[ROADMAP.md](../../../lib/documentation/assistant/ROADMAP.md)** — checkboxes for shipped themes; the "decisions needed" list still accurate; nothing shipped still listed as pending.
4. **[CLAUDE.md](../../../lib/documentation/assistant/CLAUDE.md)** — the Codebase Map covers every file/package the branch added, moved, or deleted; Stack & environment still true; new hard rules earned on this branch promoted into Invariants (`## ⚠` once test-locked); Code Style additions recorded.
5. **[DEVELOPER.md](../../../lib/documentation/assistant/DEVELOPER.md)** — directory tree current; package sections exist for new packages; build/test command sections match reality (including `lib/scripts/cicd.sh` behavior); "Adding a new X" recipes added when the branch created an extension point.
6. **[SPEC.md](../../../lib/documentation/assistant/SPEC.md)** — only if scope, non-goals, principles, or surfaces actually changed; revisions carry a date, per house style.
7. **Root [`CLAUDE.md`](../../../../CLAUDE.md) and `README.md`** — the root pointer still resolves; if a root README exists and the branch changed how the project builds/installs, it says so.
8. **Skills** (`.claude/skills/*/SKILL.md`) — any paths or process steps they reference that this branch moved or renamed.

## 3. Mechanical validation

After the edits, verify the whole system (script it — don't eyeball):

- every relative `.md` link in `lib/documentation/` and `.claude/skills/` resolves;
- the backlog index rows exactly match the documents on disk, numbering is sequential, and every gate names an existing item earlier in the order;
- every open call in every item is tagged `[user]` / `[default: …]` / `(consumed decision)`;
- no template markers or `_TODO:_` placeholders were newly introduced without being deliberate.

## 4. Report

Summarize for the MR description: which docs changed and why, which backlog items were completed/started/re-gated, any decisions recorded, and — explicitly — anything found that needs the **user** (an unanswered `[user]` call the branch stepped around, a SPEC-shape change made without a recorded decision). Remind that `lib/scripts/cicd.sh --test` should be green before the MR is opened — this skill covers the docs half of "done", not the build half.
