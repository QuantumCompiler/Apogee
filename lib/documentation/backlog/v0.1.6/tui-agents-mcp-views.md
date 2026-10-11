# The tooling views: Agents, MCP, Auth

**What / why.** The last uncovered slice (measured 2026-10-10, the parity spike): `agents` and `mcp` each have a view-ready read already (`agents create|list` and `mcp list` print JSON documents) and no view; `auth` has neither. Three views over `tui/list_view`: **Agents** — the workflows listed from `agents list`'s document, Enter the definition's card; **MCP** — the servers from `mcp list`'s document, `t` running the bounded handshake test, `e`/`d` enabling and disabling through the config cores; **Auth** — the stored credentials **as metadata only** (`CredentialMetadata`: provider and stored-at, a type with structurally no key), `add` staying the real prompt's job and `clear` the exec line's ([37h](tui-command-runner.md)). Two walls are settled here by recorded carve-out rather than capability: **the `$EDITOR` wall** — `agents edit` and `symphonies edit` run the user's editor through `platform::run_foreground`, the one documented stdio exception, which assumes a free terminal and cannot run while FTXUI owns the screen — and **the secret-input wall** — `auth add` is an echo-off prompt, and a new secret-bearing input surface under the secrets-unleakable invariant is a deliberate design, not a keystroke convenience. Closes W1's remainder and records W5/W6.

**Core constraint(s).** Secrets are unleakable by construction: the Auth view renders `CredentialMetadata` and nothing else — there is no field to leak, and the leak test proves it stays that way. A child's stderr never reaches the terminal: `t`'s handshake test keeps its tail off the frame — the result line and the notice row are the only outputs (the child-stderr invariant, which the shell's notice row already serves). Enable/disable are config edits through the one editor, byte-identical to the commands'. `run_foreground` gains no shell-compatible variant here: the carve-outs are recorded in 37a's classification with these reasons, and a suspend-the-shell seam is a later item if ever wanted.

**Seam + files.**
- `lib/src/cli/source/presentation/cli/tui_agents.h/.cpp` — the three views (or the workbench extended; follow 32d's composition), registered in `cli/tui_cmd.cpp`.
- `cli/agents_cmd.h/.cpp` — the list/show rows from the existing documents (carve where the callback holds them).
- `cli/mcp_cmd.h/.cpp` — the rows from `mcp list`'s document; the test/enable/disable cores carved (the 32d idiom); the test's bounded run on the view's worker thread, its tail folded into the result line as the command folds it.
- `cli/auth_cmd.h/.cpp` — carve `read_credential_rows` over `secrets/store.h`'s metadata (and ship `auth list --output-format json`, metadata only, per the 28h idiom with the secrets rules unchanged — `machine-mode.md` gains its row).
- `tests/presentation/cli/tui_agents_test.cpp` (new); the leak test's planted key searched in all three views — the Auth view's case is the point; classification flips in 37a's table (`agents`, `mcp`, `auth`; `analyze`'s carve-out revisited per the Open call).

**Reference.** The secrets invariant and `secrets/store.h`'s metadata shape (the structural no-key type this view inherits); the MCP client's bounded test and tail rules (Milestone W); 28h — the one document `auth list` would gain; 32d — carved cores, worker-thread actions, ask-first.

**Decisions made** (dated):
- 2026-10-10 — `agents edit` and `symphonies edit` are runner refusals naming the prompt (the `$EDITOR` reason); `auth add` through [37h](tui-command-runner.md)'s exec line ends in the command's own measured "no key entered" (a child's stdin is closed), so storing a key stays the real prompt's job, while `auth clear` runs through the exec line — all recorded in 37a's classification table, so revisiting any is a deliberate flip, never drift (the spike's defaults as revised by the same day's runner spike; the user's placement of the set in v0.1.6 on the spike report).
- 2026-10-10 — `analyze` gets no curated action even with the Agents view present: runner-covered through [37h](tui-command-runner.md)'s exec line, an Agents-view "run against an input" action a later call (the default, confirmed 2026-10-10).
- 2026-10-10 — `agents create` and `mcp create` get no curated key — they run through the exec line with their flags, degrading on the child's pipe exactly as in a script; `e`/`d` ask nothing, being reversible config edits whose result line names the file (the default, confirmed 2026-10-10).
- 2026-10-10 — `agents delete` is `x` with the ask, the 32d removal idiom (the default, confirmed 2026-10-10).

**Guardrail(s).** The leak test: one distinctive key planted in every rung, the Auth view's every heading, row and card searched for it (the existing idiom extended to the new surfaces). Byte-parity: enable/disable leaving exactly the commands' files; the MCP test's result line the command's own words, nothing of the child's stderr on the frame (the scripted noisy server from the MCP suites reused). Rows golden against `agents list`, `mcp list` and the new `auth list` documents.

**Acceptance criteria:**
- [ ] Agents, MCP and Auth are views on the shell; every list re-reads on show.
- [ ] The Auth view shows metadata only; the planted-key leak case covers it and stays green; `auth list --output-format json` exists, metadata only, documented.
- [ ] `t` tests a server bounded, its tail folded into the result line, the frame clean; `e`/`d` write byte-identical configs to the commands'.
- [ ] The `$EDITOR` and secret-input carve-outs are recorded in 37a's classification with their reasons.
- [ ] 37a's classification rows for `agents`, `mcp` and `auth` flip; the full suite is green.

**Scope note.** Earmarked for v0.1.6; gated on 37a (shipped, [Milestone AH](../../assistant/MILESTONES.md#milestone-ah--the-full-screen-tui)). Out of scope: a suspend-the-shell `$EDITOR` seam, any secret input in the shell, `analyze` (per the Open call), any MCP client behavior change.
