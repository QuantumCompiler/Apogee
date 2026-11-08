---
name: apogee-backlog-execute-item
description: Take the next recommended Apogee backlog item (or a named one) — CLI, GUI or mod — and work it per that application's docs-first process; the skill resolves which queue is meant and asks when it cannot. Use when the user says "take the next backlog item", "continue", "work the queue", "next item", or names a specific backlog document to implement.
---

# Work an Apogee Backlog Item

Apogee is built docs-first: every work item has its own document in `lib/documentation/backlog/<app>/`, and an agent builds **straight from that document**. This skill is the procedure for taking one.

## 0. Which application?

Apogee's documentation is split by kind, then by platform ([the router](../../../lib/documentation/CLAUDE.md)): `lib/documentation/{assistant,backlog,adrs,reference}/<app>/`, where `<app>` is **`cli`** (the harness), **`darwin`** (the Apple apps, macOS, iPadOS and iOS, with an item for one OS under `darwin/macos/`, `darwin/ipados/` or `darwin/ios/`, and what binds every GUI app under `gui/`), or **`mod`** (the Claude plugin). `unix/` and `windows/` hold the apps not yet built. Resolve which application this invocation means before touching anything, in order:

1. **The arguments.** An id or table names its queue — the CLI's version tables (`v0.1.<x>`, `v0.2.0`) with their numeric tracks (`52a`) and the standing `M<n>`/`A<n>` queues; the GUI's and the mod's indexes with their lettered tracks. A named document resolves by which backlog holds it. A bare version can mean any queue — they all carry work per release.
2. **The conversation.** What the work touches: the harness's commands, backends and seams are the CLI's; screens, windows and the design are the GUI's; the Claude plugin (panes, slash commands, `$`-API work, `lib/src/mod/`) is the mod's. Work that spans queues gets cross-queue siblings, gated both ways.
3. **Still ambiguous? Ask.** One AskUserQuestion — which application's queue (the CLI, the GUI, or the mod) — before anything is treated as decided. Never guess between them.

Everything below reads from the chosen application's folders, written `<app>`: its backlog index (`lib/documentation/backlog/<app>/README.md`) is the authority on that queue's tables, numbering, document format and link depths, and its contributor docs (`lib/documentation/assistant/<app>/CLAUDE.md` first) carry the invariants. Where this skill says `<app>`, substitute the chosen application's folder name.

## 1. Load the working process first

Before touching the queue, read the contributor docs in `lib/documentation/assistant/<app>/` — they are the source of truth and override anything remembered from prior sessions:

1. `CLAUDE.md` (`lib/documentation/assistant/<app>/CLAUDE.md`) — **read fully**: the working process, adopted invariants, Code Style (the home's own hard rules), and the Documentation and Status flow that defines "done".
2. `SPEC.md` (`lib/documentation/assistant/<app>/SPEC.md`) — product shape, non-goals, principles.
3. `ROADMAP.md` (`lib/documentation/assistant/<app>/ROADMAP.md`) — release context and the outstanding `[user]` decisions.
4. `DEVELOPER.md` (`lib/documentation/assistant/<app>/DEVELOPER.md`) — architecture reference, as needed while building.

## 2. Pick the item

Read `lib/documentation/backlog/<app>/README.md` (`lib/documentation/backlog/<app>/README.md`) — lifecycle, document format, the **gate convention**, and the priority-ordered index.

- **The next item** is the topmost 🟢 row per the home README's own rule (🔒 rows wait on the items their Status cell names; every pending item assumes everything already shipped, and needs only its own gate chain).
- If the user named a specific item, take that one instead — but verify its gate is satisfied and say so if it isn't.

## 3. Read the item's document — it IS the working spec

Read the whole document: What/why, Core constraints, Seam + files, **Reference**, Decisions made, Open calls, Guardrails, Acceptance criteria, Scope note.

- **Split-first rule:** if the item is marked **split first** (in its Scope note or index row), do NOT build from it. The work is to groom it into its listed sub-documents (confirm the split with the user), add them to the index, and stop.
- **Reference:** read the in-house precedents and external prior art the section names before designing — the item docs cite them because they carry proven behavior and recorded lessons.

## 4. Resolve the Open calls before writing code

Open calls are tagged:

- `[user]` — a user-owned decision that **blocks the build**. Ask the user (AskUserQuestion) and wait. Do not start implementation until every `[user]` call on the item is answered.
- `[default: …]` — take the stated default, and record it as a new dated entry under **Decisions made** in the item document. The user may veto later.
- `(consumed decision)` — already decided in another item; do not reopen it.

## 5. Build

- Mark the item's index row **in progress** in the backlog README so parallel sessions see it's taken.
- One item at a time. Honor the item's Core constraints and the repo-wide rules (CLAUDE.md → Invariants and Code Style).
- **Superpowers at this step** ([Superpowers fits the docs-first lifecycle](../../../lib/documentation/assistant/cli/CLAUDE.md#superpowers-fits-the-docs-first-lifecycle), the user's call, 2026-11-07). The item's document is the approved design, so `superpowers:brainstorming` does not run again, and the request to build is the approval. Use `superpowers:test-driven-development` for each change. For an item of several tasks, `superpowers:writing-plans`, then `superpowers:subagent-driven-development` (or `executing-plans` where no subagent tool exists), in a worktree from `superpowers:using-git-worktrees`. Its defaults give way here:
  - The plan restates nothing the document decides, and it is a session-only file in the scratchpad (with none, a `mktemp -d` directory outside the repository), **never committed**. writing-plans' hand-off is self-review against the document, with no wait.
  - The worktree is under `.claude/worktrees/`, made while on the release branch (`worktree.baseRef` is `head`). Before the first task, `git merge-base --is-ancestor <release-branch> HEAD`. Task commits stay on its branch.
  - Every ledger `Ruling:` becomes a dated **Decisions made** entry in the item's document before it ships. A ruling that would reverse a `[user]` decision is not made: stop and ask.
  - Before calling it done, run `superpowers:verification-before-completion` and `superpowers:requesting-code-review`. The review's range is `git merge-base <release-branch> HEAD`..`HEAD` for a worktree item, or the working tree against `HEAD` for an uncommitted one, never from `main` or `stable`.
- **Development runs on [the dev channel](../../../lib/documentation/assistant/cli/CLAUDE.md#development-runs-on-the-dev-channel)** (the user's call, 2026-10-31): a build installed for the user to try is `make -C lib/src/cli install MODE=dev` (or `update MODE=dev`), and a live pass names `apogee-dev` over `~/.apogee-dev`. Never a bare `make install`/`make update`, and never a write to `~/.apogee`, the user's production install, which only a release's installers and updaters change. Until GUI M18 lands, don't launch a development build of the macOS app for a live pass (it would drive production and share the installed app's settings) — ask the user.
- Tests per the item's Guardrails; verify with `lib/scripts/cicd.sh --test` before calling anything done.
- For a **CLI item**, a check on real weights names the capability and the measure, not the models, per the model families rules (`lib/documentation/assistant/cli/DEVELOPER.md` → On real weights). A **GUI item**'s guardrails are hermetic — scripted fakes and fixture event streams, no network, no binary required — with any live pass against the installed binary optional and said; real models are always the user's run. A GUI probe may drive the installed `apogee` binary against a sandboxed `APOGEE_HOME` with the scripted mock backend — never the real `~/.apogee`.
- If the item changes the pipeline — a CI job added, removed or renamed, a matrix row, a new workflow, or what the CLI is built from — follow DEVELOPER.md → Changing the pipeline (`lib/documentation/assistant/cli/DEVELOPER.md#changing-the-pipeline`). Its required-checks step comes after the pull request's run passes and before the merge, and `--apply` changes repository settings: tell the user it is needed, and run it only when they say so.

## 6. Ship — the Documentation and Status flow

*Superpowers' `finishing-a-development-branch`* ([Superpowers fits the docs-first lifecycle](../../../lib/documentation/assistant/cli/CLAUDE.md#superpowers-fits-the-docs-first-lifecycle)): its menu gives way to one landing. From the main checkout, already on the release branch, with no checkout or pull, run `git merge --squash <item-branch>`, stage only the item's paths, and the docs flow below lands in the same change. Then remove the worktree (ExitWorktree) and the branch (`git branch -D`). The commit itself is the user's move, as ever (a group run makes it at the boundary). Never merge into `stable` or open a pull request into it: that merge is the release, cut at the boundary. Pushing is on the user's word.

When the acceptance criteria pass, in the same change (per CLAUDE.md):

1. Record the work in `MILESTONES.md` (`lib/documentation/assistant/<app>/MILESTONES.md`) (fold into the matching milestone, or start a new lettered one).
2. **Delete the item's backlog document and its index row** — the backlog holds pending work only. Update any other document whose gate this satisfied.
3. Check the box / move the line in ROADMAP.md.
4. Update the Codebase Map in CLAUDE.md and DEVELOPER.md for new/moved files.
5. Update SPEC.md only if scope, non-goals, or principles actually changed.

Then **stop and wait** for the user to ask to continue. (A bare "Continue" means: re-read CLAUDE.md, then take the topmost claimable item.)
