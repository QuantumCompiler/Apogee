---
name: apogee-backlog-investigate-spike
description: Run an evidence-gathering spike for Apogee — CLI, GUI or mod — probing the real binary or codebase in a sandbox and reporting findings only, including the backlog documents that would need creating (never creating them); the skill resolves which application the question belongs to and asks when it cannot. Use when the user asks for a spike ("do a spike into X", "spike this", "investigate whether we could…").
---

# Run an Apogee Spike

A spike answers a design question with **evidence, not opinion**. Its deliverable is a findings report in chat that maps each obstacle to the backlog item that would remove it — so that when the user later says "execute the spike," the items practically write themselves. A spike that only reasons from memory of the code is an essay; run the real thing.

## 0. Which application?

Apogee's documentation is per application ([the router](../../../lib/documentation/CLAUDE.md)): the CLI's home is [`lib/documentation/cli/`](../../../lib/documentation/cli/README.md), the GUI's [`lib/documentation/gui/`](../../../lib/documentation/gui/README.md), and the mod's (the Claude plugin) [`lib/documentation/mod/`](../../../lib/documentation/mod/README.md). Resolve which queue this invocation means before touching anything, in order:

1. **The arguments.** An id or table names its queue — the CLI's version tables (`v0.1.<x>`, `v0.2.0`) with their numeric tracks (`52a`) and the standing `M<n>`/`A<n>` queues; the GUI's and the mod's indexes with their lettered tracks. A named document resolves by which backlog holds it. A bare version can mean any queue — they all carry work per release.
2. **The conversation.** What the work touches: the harness's commands, backends and seams are the CLI's; screens, windows and the design are the GUI's; the Claude plugin (panes, slash commands, `$`-API work, `lib/src/mod/`) is the mod's. Work that spans queues gets cross-queue siblings, gated both ways.
3. **Still ambiguous? Ask.** One AskUserQuestion — which application's queue (the CLI, the GUI, or the mod) — before anything is treated as decided. Never guess between them.

Everything below reads from the chosen home, written `<home>`: its backlog README is the authority on that queue's tables, numbering, document format and link depths, and its assistant docs (`<home>/assistant/CLAUDE.md` first) carry the invariants. Where this skill says `<home>`, substitute the chosen application's directory.

## 1. What a spike is — and is not

- A spike **produces a report, and only a report**: walls, measurements, the documents that would need to be created, and the questions only the user can answer.
- A spike **does not build** anything shippable, does not create backlog documents, and does not edit SPEC/ROADMAP/README — even when the split is obvious. Those happen only if the user asks afterwards.
- **"Execute the spike" / "execute the plan" means create the backlog documents** (via `/apogee-backlog-create-spike`, the whole split in one pass with the evidence baked in; a single stray item can go through `/apogee-backlog-create-item`) — it never means implementing. Building happens later via `/apogee-backlog-execute-item`.
- Findings live **in the report, not the repo**. Do not save a spike write-up file unless the user explicitly asks for one; "internalize it in your context" is the default.

## 2. Ground first

1. `<home>/backlog/README.md` (`<home>/backlog/README.md`) — the index: what's already queued, the gate convention, where a resulting split would land (the home's tables; both homes when the question spans them).
2. `<home>/assistant/CLAUDE.md` (`<home>/assistant/CLAUDE.md`) and `SPEC.md` (`<home>/assistant/SPEC.md`) — the invariants and non-goals the findings must respect. If the spike's premise collides with a SPEC non-goal, surface that **as a finding** — a SPEC revision is the user's call, made before any items exist.
3. `ROADMAP.md` (`<home>/assistant/ROADMAP.md`) — release context; which open `[user]` decisions might interact with this question.
4. **Precedents** — if a shipped Apogee item or an external project has already solved an analog, read it before probing; it tells you which walls are real and which are already-solved problems.

## 3. Probe the real thing

Prefer a probe over an argument: run the actual binary, drive it the way the question implies, or parse the actual sources (e.g. build the include/dependency graph) — and measure.

- **Sandbox everything.** A probe that touches state runs against a temp `APOGEE_HOME` under the scratchpad directory. Never touch the real `~/.apogee`.
- **Probe scripts live in the scratchpad**, not the repo. Promote a probe into `lib/src/.../tests/` only when the user asks to keep it.
- Shell facts for probing on this machine: Bash-tool stdin is a pipe, so smoke-test the binary with `</dev/null`; macOS has no `timeout` command (enforce deadlines inside the probe script); use absolute paths.
- A probe that needs a live conversation drives the installed `apogee` binary with the scripted mock backend (the fake-actor idiom) — no network, no real models. A CLI spike that truly needs real weights follows the model families rules (`lib/documentation/cli/assistant/DEVELOPER.md` → On real weights) — one family at a time, model loaded once; most spikes don't, so don't load a model to answer a plumbing question.
- **Play the naive outsider.** When the question is "can X be plugged into / driven by Y," write the probe the way a stranger would — no insider shortcuts, no reading our source to cheat — so every wall a real integrator would hit shows up as a measured failure instead of staying invisible.

## 4. Frame findings as walls

For each obstacle, record three things: the **evidence** (the command or probe step, its actual output, or `file:line`), the **wall** in one plain sentence, and the **minimal capability that removes it**. Number the walls (W1, W2, …) so the report and the eventual item split can reference them.

Just as important: record **what already works with zero changes**. A spike that lists only problems hides the shortest path, and over-scopes the split.

Quantify where possible — "93% of include edges already conform; 6 edge-types violate, 5 of them from one root cause" decides an item split; "the layering is mostly okay" decides nothing.

## 5. Report

Deliver everything in the final chat message:

1. **The question**, as asked.
2. **Method** — what was probed and how (one short paragraph; name the sandbox).
3. **Walls** — numbered, each with its evidence and the capability that removes it; plus the already-works list.
4. **The documents that would need to be created** — the walls grouped into right-sized backlog items (one focused session each), presented as the concrete document list: a working title per item, its target table (release / Architecture / Maintenance), its gate, and which walls it closes. Note which walls one root-cause fix collapses. This list is the handoff: when the user says "execute", each entry becomes one document with nothing left to decide but the open calls.
5. **Questions for the user** — only the genuinely user-owned calls, phrased in plain architecture language (no ML jargon); everything agent-decidable gets a stated default instead. If the user asks to be interviewed, use AskUserQuestion; otherwise list the questions and stop.

Then **wait**. Do not create documents, items, or code until the user says what to execute.
