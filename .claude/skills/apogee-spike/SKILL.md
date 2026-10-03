---
name: apogee-spike
description: Run an evidence-gathering spike for Apogee — answer a "can we / how should we" question by probing the real binary or codebase in a sandbox, then report the walls hit and a proposed backlog-item split. Use when the user asks for a spike ("do a spike into X", "spike this", "investigate whether we could…").
---

# Run an Apogee Spike

A spike answers a design question with **evidence, not opinion**. Its deliverable is a findings report in chat that maps each obstacle to the backlog item that would remove it — so that when the user later says "execute the spike," the items practically write themselves. A spike that only reasons from memory of the code is an essay; run the real thing.

## 1. What a spike is — and is not

- A spike **produces findings**: walls, measurements, a proposed item split, and the questions only the user can answer.
- A spike **does not build** anything shippable, does not create backlog documents, and does not edit SPEC/ROADMAP/README. Those happen only if the user asks afterwards.
- **"Execute the spike" / "execute the plan" means create the backlog documents** (via `/apogee-create-backlog-item`, one per item in the split, placed in the right index table) — it never means implementing. Building happens later via `/apogee-backlog-item`.
- Findings live **in the report, not the repo**. Do not save a spike write-up file unless the user explicitly asks for one; "internalize it in your context" is the default.

## 2. Ground first

1. [`lib/documentation/backlog/README.md`](../../../lib/documentation/backlog/README.md) — the index: what's already queued, the gate convention, where a resulting split would land (release table vs. the standing Architecture/Maintenance tables).
2. [`lib/documentation/assistant/CLAUDE.md`](../../../lib/documentation/assistant/CLAUDE.md) and [`SPEC.md`](../../../lib/documentation/assistant/SPEC.md) — the invariants and non-goals the findings must respect. If the spike's premise collides with a SPEC non-goal, surface that **as a finding** — a SPEC revision is the user's call, made before any items exist.
3. [`ROADMAP.md`](../../../lib/documentation/assistant/ROADMAP.md) — release context; which open `[user]` decisions might interact with this question.
4. **Ommi** (`~/Data/Development/Projects/Ommi`) — if the reference implementation has an analog, read it before probing; it tells you which walls are real and which are already-solved problems.

## 3. Probe the real thing

Prefer a probe over an argument: run the actual binary, drive it the way the question implies, or parse the actual sources (e.g. build the include/dependency graph) — and measure.

- **Sandbox everything.** A probe that touches state runs against a temp `APOGEE_HOME` under the scratchpad directory. Never touch the real `~/.apogee`.
- **Probe scripts live in the scratchpad**, not the repo. Promote a probe into `lib/src/.../tests/` only when the user asks to keep it.
- Shell facts for probing on this machine: Bash-tool stdin is a pipe, so smoke-test the binary with `</dev/null`; macOS has no `timeout` command (enforce deadlines inside the probe script); use absolute paths.
- If the probe needs real weights, follow [the model families](../../../lib/documentation/assistant/DEVELOPER.md#on-real-weights-the-model-families) rules — one family at a time, model loaded once, honoring the exclusions listed there. Most spikes don't need weights; don't load a model to answer a plumbing question.
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
4. **Proposed item split** — walls grouped into right-sized backlog items (one focused session each), with suggested gates between them and the target table (release / Architecture / Maintenance). Note which walls one root-cause fix collapses.
5. **Questions for the user** — only the genuinely user-owned calls, phrased in plain architecture language (no ML jargon); everything agent-decidable gets a stated default instead. If the user asks to be interviewed, use AskUserQuestion; otherwise list the questions and stop.

Then **wait**. Do not create documents, items, or code until the user says what to execute.
