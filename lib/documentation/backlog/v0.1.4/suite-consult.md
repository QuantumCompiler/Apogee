# The consult tool: the root model delegates

**What / why.** Every helper call in Apogee today is **harness-initiated** at a fixed seam — titles, compaction, query rewrites, large tool results (utility, 26b), descriptions and transcripts (vision/transcription, 26e), the rerank judge — and the spike (2026-10-03) confirmed the other direction simply has no path: the native tool registry is files, notes, git, search and web (nineteen tools inventoried; **none reaches a model**), so a root model that wants a sub-task done by the suite's 3B has no way to say so. This item is the spike's **W2** closed: a **`consult` native tool**, offered when the session's suite ([model-suites.md](model-suites.md)) designates consultable members — the model states the member role and a self-contained question, the harness runs one bounded call on that member and returns its answer as an ordinary tool result. The contract is what keeps small helpers viable (the user's framing: the root manages context): the brief must be **self-contained** — the helper sees the question and nothing else, no conversation history, fitted to the member's pinned small window — and the exchange is **serial, budgeted, and narrated**: one model call at a time as the attachments worker already disciplines, a per-turn consult cap, each call a dim labeled line in the thinking block (26n's seam, adopted whether or not it has shipped first).

**Core constraint(s).**
- **A tool like any other, registered honestly:** `consult` appears only when the active suite designates consultable members, carries the member roles in its description so selection (26g, shipped) can rank it, and rides the ordinary tool loop — result in history as tool results are, no new wire shape, machine mode carries it as any tool call.
- **Local and unmetered by construction:** consultable members are local backends; a metered backend in a consult slot is refused at config time (the no-spend-on-Apogee's-initiative principle — the model's initiative is still not the user's).
- **Ungated, but bounded** *(the spike's recommendation, recorded as the default — the user may veto)*: no permission prompt — the call is read-only, local, and side-effect-free — but never unbounded: a per-turn cap, a per-call token cap on the brief and the answer, and the member's own small window as the hard ceiling.
- **The brief is the whole context.** No history, no transparent retrieval, no attachments ride along; a root that wants the helper to know something puts it in the question. This is the load-bearing property that lets a 4K-window member serve a 32K-window root.
- **Serial, always:** consult calls join the one-model-call-at-a-time discipline; two members never generate concurrently on this item's account.
- Code style carries: `.h`/`.cpp` pairs, smart pointers only; the harness never includes backends (the tool reaches members through the provider seam the roles already use).

**Seam + files.**
- `tools/consult.h/.cpp` (new): the tool — schema (member role, question), the bounded call through the resolver (`harness/roles.h`, the suite rung), the answer as the result; registered by the chat/complete surfaces when the suite designates members.
- `harness/config` (27d's `suites:` block): the `consultable:` member list and the caps, through the one editor.
- `agentloop/` narration: the consult line in the thinking block per 26n's conventions (label, member, duration), collapsing with it.
- Tests: `tests/tools/consult` — registration lifecycle (suite with/without consultables), the brief's isolation (wire-recorded: the member's request contains exactly the brief), caps enforced with honest refusals ("consult budget spent this turn"), serial execution asserted; a metered member refused at config time.
- Consumes: [27d](model-suites.md) (the suite, member windows); 26g (shipped — ranking); 26b's resolver seams (shipped); 26n (narration conventions, adopted early).

**Reference (Ommi).** No analog — Ommi's models never called models; the harness-initiated judge/teacher calls in its training track (`initSingleBackend`) are the closest shape and were never a tool. The isolation contract (brief-only context) follows the utility seams' existing practice, made explicit.

**Decisions made** (dated):
- 2026-10-03 — Split from the suites spike; the spike's inventory (nineteen tools, none model-reaching) is the wall this closes. Placed in v0.1.8 with its track, gated on the suite unit only — not on MLX.
- 2026-10-03 — Delegation is a *tool*, not a protocol: it rides the shipped tool loop, selection, permissions model and machine-mode surface, so every backend that can use tools can consult, and nothing new crosses the wire.

**Open calls:**
- [default: caps — 4 consults per turn, 1,024-token briefs, 512-token answers; named constants, veto by config] The budget.
- [default: the answer returns as plain text; a structured-output consult (26f grammars on the member) is a natural follow-up, not this item] Answer shape.
- [default: `complete` and machine mode get the tool under the same registration rule; the task runner (27h) inherits it like any tool when that lands] Surfaces.

**Guardrail(s).**
- The isolation pin: the member's wire-recorded request equals the brief — no history, no system creep — mutation-tested.
- Caps: the fifth consult in a turn is refused with the stated reason; the refusal is a tool result, not an error.
- Registration: no suite → no tool; suite without `consultable:` → no tool; the tool's description names the members (golden).
- On real weights, the capability and the measure: on [the model families](../../assistant/DEVELOPER.md#on-real-weights-the-model-families), a root asked a question whose answer it must get from a consult (a fact planted only in the brief's domain) calls the tool unprompted and uses the answer — one family at a time, member loaded once.

**Acceptance criteria:**
- [ ] In a suite with `consultable: [utility]`, the root model sees a `consult` tool naming the utility member; without one, the tool does not exist.
- [ ] A consult call runs the member with exactly the brief (wire-recorded), returns its answer as the tool result, and paints one labeled line in the thinking block.
- [ ] The per-turn cap and token caps enforce with said refusals; a metered backend named `consultable:` is refused by the config editor with the reason.
- [ ] Machine mode shows the consult as an ordinary tool-call/result pair — no new event types.

**Scope note.** Item **27f**, earmarked for **v0.1.4**; **gated on [27d](model-suites.md)** — not on the MLX items. Out of scope: concurrent consults; member-to-member consults (the root delegates; helpers do not sub-delegate); structured consult answers (noted default); using consult for validation — that policy is [27g](suite-validation.md)'s.
