---
name: apogee-gui-maintenance-update-release
description: Reconcile the GUI's docs at a release boundary — flip its ROADMAP section states (in progress → shipped with dates and MILESTONES links), retire shipped backlog rows the docs pass has not, and keep the GUI's story in step with the repo version roll. Use at a release boundary ("roll the GUI docs for the release", "the release shipped — update the GUI board").
---

# Update the GUI Docs at a Release Boundary

The repo's release pointer is one mechanism, owned by the repo-wide process: `lib/release/VERSION`, the CLI's own version, and the merge-is-the-release law are the CLI docs' to record and `/apogee-cli-maintenance-update-release`'s to advance. **This skill never touches the version files.** What it owns is the GUI's side of a version roll: its planning docs telling the truth about what shipped and what is next.

## 1. Ground

Read the live [`ROADMAP.md`](../../../lib/documentation/gui/assistant/ROADMAP.md), [`MILESTONES.md`](../../../lib/documentation/gui/assistant/MILESTONES.md) and the [backlog index](../../../lib/documentation/gui/backlog/README.md); `git log` for what actually merged. The repo-wide roll (version bump, CLI board states) happens through the CLI skill — run or confirm it first when both sides move.

## 2. Reconcile the GUI's documents

1. **ROADMAP:** the released version's section moves to **Shipped releases** (checked lines, the date, the release link once it exists); the next version's section opens under **In progress** with only what the user has actually queued — never speculative themes.
2. **Backlog:** rows whose items shipped are gone with their documents (the docs pass's job — verify rather than redo); gates the release satisfied are updated; the index preamble says what the release took with it.
3. **MILESTONES:** every shipped item folded (verify, don't duplicate); forward-looking references to the now-shipped version corrected.
4. **The gui README / assistant docs:** any "in development" phrasing that names the old version updated.

## 3. Validate and report

The standard scripted routine (rows ↔ documents, gates, the link sweep) over the GUI docs; then report: every state flipped, every row retired, what was deliberately left (the CLI side, the version files), and anything that still needs the user. This skill edits documents only — never publishes, tags, or rewrites history.
