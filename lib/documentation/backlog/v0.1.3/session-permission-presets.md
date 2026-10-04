# Session permission presets: launch flags and `/allow`

**What / why.** The permission gate asks at the moment of use, and the `[s]ession` answer already grants a tool for the rest of the run — but only *reactively*: the user must wait for the first prompt to say what they already know they want. This item makes the session answer **declarative**, the way the claude CLI takes permissions at launch (the user's reference, 2026-09-30): `apogee chat --allow run_command --allow write_file` starts the session with those answers pre-given, `--allow-host docs.python.org` pre-answers `fetch_url`'s per-website ask the same way, and `--deny <tool>` records a session-wide no that resolves the prompt without asking. Mid-chat, the same ability as slash commands — `/allow <tool>`, `/deny <tool>`, `/revoke <tool>`, `/permissions` to list the effective state — each a row in the one command table item 24 shipped, so `/allow ` completes the gated tool names exactly like `/model ` completes backends. The mechanism is already built: `cli/permissions.h`'s `SessionApprovals` is "what the user answered `session` for", shared between the checker and the prompt, resolved config → session → ask. Presets seed it; slash commands edit it; nothing about the gate itself changes.

**The design principle: a preset is the `session` answer, given early.** A launch flag or `/allow` can never reach further than pressing `s` at the prompt could — the resolution ladder consults config first, so whatever config forbids, a session entry never loosens. The one place this grants genuinely new reach is deliberate and named: `apogee complete` has nobody to ask, so `ask` resolves to deny today — `complete --allow write_file` is the user answering *at invocation*, which is what makes a one-shot script able to use a destructive tool on purpose. Deny stays the default absent the flag, on every surface.

**Core constraint(s).**
- **The gate's semantics are untouched.** One checker, one ladder (config, then the session's answers, then ask), one prompt; presets populate the same `SessionApprovals` the `s` key populates, so there is no second permission path to audit. A config-level deny is never loosened by a session entry — already the ladder's behavior, now asserted.
- **A session deny tightens, and tightening always wins:** `--deny`/`/deny` resolves the prompt to no for the rest of the session, even over a config `allow` — the user at the keyboard saying no in advance is the safest input the gate can get. Denial remains a tool result the model reads, never an error.
- **Grants die with the process.** `SessionApprovals` is in-memory; a resumed chat starts clean, and nothing a preset does writes config — `always` (through the one config editor) remains the only persistence path. `/permissions` says which source each effective answer comes from, so the three never blur.
- **Parity:** the flags exist on `chat` and `complete` identically, which means a machine-mode child takes them on its argv for free — a front-end can preset its session the same way (the per-run wiring item (28e) wants for tools, already answered here for permissions). The machine-mode protocol itself does not move: the `question` event and its answers are unchanged.
- **Completion through the one table** (consumed decision — item 24): the three verbs register with argument completers; a command offered on Tab and rejected at dispatch stays impossible by construction.
- **Secrets and hosts hygiene:** `--allow-host` takes a hostname, matched the way `tools.allowed_hosts` already matches; nothing in a preset names a credential, and the leak conventions cover `/permissions` output.

**Seam + files.**
- `cli/permissions.h/.cpp`: `seed_approvals(SessionApprovals&, flags)` plus list/insert/erase helpers the slash verbs call; the source-attribution for `/permissions` (config level vs session answer vs default); the session-deny entry beside the existing approvals (consulted before the prompt fires).
- `cli/chat.cpp`: `--allow`/`--deny`/`--allow-host` (repeatable) seeding before the first turn; `/allow`, `/deny`, `/revoke`, `/permissions` in the command table — `/allow` completing the *gated* tool names from the live registry (writes-tools plus gated MCP tools), `/revoke` completing the session's current entries.
- `cli/complete.cpp`: the same flags, with the invocation-is-the-answer rule above.
- Tests: `tests/presentation/cli/permissions_test.cpp` — the equivalence table (flag-seeded vs `s`-answered sessions resolve identically, exhaustively over levels), the deny-tightens cases, config-deny-never-loosened; `chat_completer` goldens for the new verbs' completion; `chat_test` for the slash dispatch and `/permissions` attribution; a `complete` e2e where `--allow` lets a scripted destructive call run and its absence denies it.

**Reference (Ommi).** No analog — Ommi's tools ran ungated, and the gate itself is Apogee's divergence (Milestone V). External prior art: the claude CLI's launch-time permission flags, named by the user as the reference. In-house precedents consumed: the `permissions:` schema and prompt ladder (Milestone V), `SessionApprovals`, and item 24's command table with per-verb argument completion.

**Decisions made** (dated):
- 2026-09-30 — Asked for by the user: permissions preset at launch like the claude CLI, and adjustable mid-chat by slash command with today's completability; **end of v0.1.3**, lettered **26o** per the release-prefix rule.
- 2026-09-30 — **The equivalence principle** (a preset = the `session` answer given early) over a new grant vocabulary: it reuses the shipped structure, inherits the ladder's config-first safety, and sidesteps the grant-ceiling question entirely — which stays where it belongs, on [27i](../v0.1.4/task-autonomy-policy.md)'s **[user]** calls for *unattended* tasks. This item is the attended sibling: a human typed the grant.
- 2026-09-30 — Single-argument verbs, so completion needs nothing the table doesn't have. The user's chained-command wish (`/permissions set <tool> <level>`, two completable positions) is recorded as the extension path: the table would grow per-position completers — deferred, not refused.
- 2026-10-03 — Confirmed (the default taken, the user's confirmation): bare `/allow` behaves as `/permissions` — listing beats erroring.
- 2026-10-03 — Confirmed (the default taken, the user's confirmation): `--deny`/`/deny` take hosts too — one vocabulary for both ask dimensions, matched like `tools.allowed_hosts`.
- 2026-10-03 — Confirmed (the default taken, the user's confirmation): `/revoke` reaches session entries only, pointing at `config set-permission` for config-level answers — chat never mutates config permissions.
- 2026-10-03 — Confirmed (the default taken, the user's confirmation): an unknown tool name is refused naming the gated set — a typo must not silently grant nothing.

**Guardrail(s).**
- The equivalence table, exhaustively and mutation-tested: for every (config level × preset × prompt answer) combination, a flag-seeded session and an `s`-answered session resolve identically; config `deny` unloosened in all of them.
- The `complete` pair: with `--allow` the scripted destructive call runs; without it, the denial-as-tool-result contract holds byte-for-byte as today.
- Completion goldens: `/allow ` offers exactly the gated set; `/revoke ` offers exactly the live session entries; the one-table test (dispatch ↔ offered) covers the new verbs automatically.
- `/permissions` attribution golden over a mixed state (one config allow, one session allow, one session deny).
- Resume: a resumed chat holds zero session entries, asserted.

**Acceptance criteria:**
- [ ] `apogee chat --allow write_file` writes on the first ask-level call with no prompt; the same session still prompts for `run_command`; `apogee chat` fresh afterwards prompts for both.
- [ ] `/allow run_command` mid-chat stops further prompts for it; `/revoke run_command` restores them; `/deny write_file` stops the prompt and records the refusal as a tool result; `/permissions` shows each with its source.
- [ ] `/allow `, `/deny ` and `/revoke ` complete their arguments on Tab exactly as `/model ` does, and appear in `/help` — the one table, extended by three rows.
- [ ] `apogee complete --tools --allow write_file "<prompt>"` performs the write in one shot; without the flag it denies exactly as today.
- [ ] `--allow-host example.org` pre-answers `fetch_url`'s per-website ask for that host and no other.
- [ ] A config-level `deny` is not loosened by any flag or verb, asserted in the equivalence table.

**Scope note.** Item **26o**, earmarked for **v0.1.3** (the end — the user's call); gated on nothing pending. Interplay, not gates: [27i](../v0.1.4/task-autonomy-policy.md) owns the *unattended* grant ceiling for tasks and its **[user]** calls stand apart; the per-run wiring item (28e) inherits these flags' precedent for hosts/front-ends. Out of scope: chained two-position slash completion (recorded as the table's extension path); persistent grants (that is `always` and `config set-permission`, unchanged); pattern or wildcard grants (`--allow 'mcp__*'` waits for a demonstrated need).
