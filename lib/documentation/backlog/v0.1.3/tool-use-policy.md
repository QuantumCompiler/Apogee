# Reaching for tools: the policy in the environment note

**What / why.** A model with tools uses them when told and hesitates when not. The motivating transcript (Taylor, 2026-10-03, Qwen3-VL-8B with `--tools` and search configured): "What is the current temperature in Lehi Utah?" → *"I can't provide real-time weather information"* — then "Search the internet and then tell me" → a correct search and answer. The refusal prior is trained in; nothing in today's prompt pushes back on it. The tool handed to the model doesn't help its own case: `web_search`'s description says what it *returns* ("Returns the top N results… Use it to find pages"), not when to *reach* for it, and the pinned environment note (25d) carries the date, OS and folder but no policy about tools versus the model's own knowledge. This item is the generic fix, two levers at seams every backend shares: **a tool-use policy paragraph composed into the environment note from the live registry** — roughly *"Today is \<date\>. For anything current, recent, or outside what you can know — weather, news, prices, the contents of pages — use the matching tool before answering. Never claim you lack access to information a listed tool provides."* — and **trigger-oriented descriptions** on the reach-sensitive tools, naming the cases that should fire them. No per-model code, no protocol change; every surface inherits it through the one shared loop.

**Core constraint(s).**
- **The note never promises what the session lacks.** The policy is composed from what is actually registered: no search claim without `web_search`, no page-reading claim without `fetch_url`. A note that tells a toolless-search model to "search before answering" manufactures exactly the hallucinated-capability failure this item fights.
- **The cache property holds, now asserted:** the environment note is rendered once per turn and pinned so a local model's prefix cache survives turn to turn (25d's design). The policy text is static given the date and tool set, and a test pins the note byte-identical across a session's turns — so a future edit cannot quietly start re-costing every turn's prefix.
- **Truthful, not coercive:** the policy says *use the matching tool for what you cannot know*; it does not command tool use on every turn (a model that knows the answer should answer — "Hello, how are you?" must not trigger a search), and the model's refusal remains *correct* behavior when no matching tool exists.
- **One home per concern:** the policy lives in the registry's environment note (where 25d put the per-session context); descriptions live on the tool definitions. No second prompt-injection point, no scattering of guidance across backends — the llama.cpp chat layer, the cloud wires and the vendor CLIs all receive it through the same request assembly.
- **The budget rule stands** (consumed decision — 26c): the note is pinned and never trimmed; this item grows it by one paragraph and must stay well under any size that would pressure that rule.

**Seam + files.**
- `tools/toolsets.cpp` (or the unit that builds `ToolRegistry::environment()` today): `tool_use_policy(registry)` — composes the paragraph from the registered reach-sensitive tools, appended to the existing note; pure, golden-testable.
- `agent/web_search.cpp`, `agent/fetch_url.cpp`: descriptions led by trigger cases ("current events, weather, prices, scores, anything time-sensitive or after your training data" / "read a page a search found, or any URL the user or a result names") ahead of the output shape.
- Tests: `tests/business/tools/` goldens for the note per registry composition (search+fetch; fetch only; neither — the policy paragraph shrinking honestly each time); the byte-stable-across-turns assertion; description trigger-phrase assertions so a later rewrite cannot silently regress the reach behavior.
- Live verification on ship (recorded in MILESTONES per house style): the motivating transcript's question against Qwen3-VL-8B — 8B-class, per the standing no-27B-until-26i rule — issues a `web_search` on the first ask.

**Reference (Ommi).** No analog — Ommi never put tools in front of local models at all (the 25b finding), so the reach problem could not arise there. The in-house precedents consumed: 25d's environment note (the vehicle), 24e's `web_search` and 25f's `fetch_url` (the subjects), and Milestone P's profile registry (the named escalation path below).

**Decisions made** (dated):
- 2026-10-03 — Asked for by the user from the live transcript; **end of v0.1.3**, lettered **26p** per the release-prefix rule.
- 2026-10-03 — **Prompt-side policy over behavioral heuristics:** no refusal-detection, no automatic re-prompting, no second model call judging the first — those treat the symptom per conversation at inference cost; the note and descriptions treat the cause once. The deep fix for local models remains the tool-use training kits (the re-authoring noted on 25b), unchanged by this item.
- 2026-10-03 — **The per-family nudge is named but not built:** Milestone P's profile registry is where a family-specific line would go if some family ignores the generic paragraph — deferred until a family demonstrates the need, so the item ships no speculative plumbing.

**Open calls:**
- [default: the policy wording above, refined against live runs during the build — taste, veto freely; the goldens pin whatever lands] The paragraph's final text.
- [default: first-pass description rewrites are `web_search` and `fetch_url` only; fs/shell/git descriptions join only if live runs show missed reaches for them] The description set.
- [default: the policy names categories (current events, weather, prices) rather than enumerating tools by name, so MCP tools and future toolsets benefit without edits] Categories versus tool names.

**Guardrail(s).**
- Note goldens per registry composition, including the no-network-tools case where the policy paragraph is absent and the date/OS/folder note stands alone.
- The byte-stability assertion across a multi-turn session (the prefix-cache property, mutation-tested where convention applies).
- Trigger-phrase assertions on both rewritten descriptions.
- The small-talk guard in the live check: "Hello, how are you?" still answers without a tool call.

**Acceptance criteria:**
- [ ] On Qwen3-VL-8B with `--tools` and search configured, "What is the current temperature in Lehi Utah?" issues a `web_search` call on the first ask — no second prompt — and small talk still triggers nothing (verified live, recorded on ship).
- [ ] The environment note carries the policy exactly when reach-sensitive tools are registered, scaled to what is present, and is byte-identical across a session's turns.
- [ ] `web_search` and `fetch_url` descriptions lead with their trigger cases, pinned by test.
- [ ] A session with tools but no `web_search` produces a note with no search claim — and the model's "I can't check that" remains the honest answer there.

**Scope note.** Item **26p**, earmarked for **v0.1.3** (the end — the user's call); gated on nothing pending. Interplay, not gates: [26g](../../assistant/MILESTONES.md#milestone-f--the-shared-agent-loop) (shipped 2026-10-03) decides *which* tools a step sees, this decides whether the model *reaches*; [26i](thinking-control.md) may further improve the decision on reasoning models. Out of scope: refusal detection or automatic re-prompting; per-family nudges until demonstrated; the tool-use training kits (their own deferred work, noted on 25b).
