---
name: apogee-gui-maintenance-summarize-release
description: Draft the GUI's part of an Apogee release's notes — the GUI highlights, known limitations and reference pointers for a version, built from the GUI's own evidence (its ROADMAP, MILESTONES, the git range over lib/src/darwin and the gui docs). Use when the user asks to "write the GUI's release notes", "summarize the GUI side of this release", or when the CLI release-notes pass needs the GUI section.
---

# Summarize the GUI's Part of a Release

A repo release is one document of notes, and the CLI's skill (`/apogee-cli-maintenance-summarize-release`) owns its overall shape — the title, intro, Install, Platforms, the publishing mechanics (`lib/release/VERSION`, the archives, the ranges). This skill drafts **the GUI's contribution** to those notes, from the GUI's own evidence, so a release that carries GUI work tells that story with the same honesty bar.

## 1. Establish the GUI's range and evidence

- The version and the range base come from the repo-wide process (the CLI skill's steps; reuse its range when both run in one pass).
- The GUI's evidence for the range: [`ROADMAP.md`](../../../lib/documentation/gui/assistant/ROADMAP.md)'s section for the version (its checked lines are the candidate highlights), [`MILESTONES.md`](../../../lib/documentation/gui/assistant/MILESTONES.md) entries the range added, and `git diff --stat <base>..<end> -- lib/src/darwin lib/documentation/gui` for the ground truth.
- **Did the GUI change at all?** `lib/scripts/changed.sh gui-darwin <base> <end>` (once 1a lands it). `false` means the release carries no GUI story — say exactly that in one line and stop; never pad.

## 2. Draft the GUI highlights

In the house style the published notes use: each highlight a **bold one-sentence claim with the period inside the bold**, then one paragraph — what changed for the user first, then the mechanism, naming real things (`Apogee.app`, the window, the protocol events) in backticks. Every sentence traces to a MILESTONES entry, a ROADMAP line, or the diff; every number quoted or measured, never estimated. Known limitations are stated plainly (ad-hoc signing means right-click-to-open on other machines — say it while it is true).

## 3. Verify before handing over

- Every claim checked against the evidence files; every named artifact exists in the range.
- The draft is a **contribution, not a publication**: hand it to the user (or the CLI-notes pass) in chat; this skill never publishes, tags, or edits the release itself.
