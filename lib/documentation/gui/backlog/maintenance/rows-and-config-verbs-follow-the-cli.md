# List rows and config verbs follow the CLI: interrupted tasks, drafters, one provider, allowed hosts

**What / why.** The CLI's M32 and M33 (shipped 2026-10-26, [Milestone AO](../../../cli/assistant/MILESTONES.md#milestone-ao--the-desktop-clients-cli-growths)) put facts on list rows and finished four config verbs. The app still works around each:
- **`task list` rows carry `interrupted`.** The app's task list draws a task killed mid-round as running; only its page, from `task status`, knows (4h).
- **`models list`'s `mlx` rows carry `draft: {path, can_draft, refusal?}`.** The Backends table reads every `mlx` entry's `models info` on each refresh to mark a paired base and draw a drafter that can't draft (M5, `presentation/models/ViewModels/ModelsViewModel.swift:219`).
- **`providers register <id>`** registers one scanned provider as the TUI's `r` does: the default set when none is, said, and the roster fetched (`--output-format json`, `object: "provider.registration"`). Settings composes `config add-backend <id> --type <type>` in its place (4k, `SettingsViewModel.swift:93`), with no default and no roster.
- **`config get tools.allowed_hosts`** reads the allowed hosts, one per line. Settings › Permissions adds and removes hosts (`SettingsViewModel.swift:417–422`); check how it reads them today, and read them with this where the binary has it.
- **Two config verbs now say more.** `config add-backend` and `config scan --register` say `models.default set to '<name>' (none was set)`, and `config delete-backend` (with `models delete --and-backends`) says `models.<field> cleared: it named '<name>'`. The app's Register, Register all and delete narrations should show those lines, not drop them.
- **A role takes only its own kind.** The app's Roles already narrow the chat picker (M4), and the CLI now refuses the rest in its words, which the app shows.

**Core constraint(s).**
- **The binary's word decides:** the row's field, the verb's said lines, the refusal's words.
- **An older binary keeps today's behaviour:**
  - a row without `interrupted` or `draft` falls back to today's per-row read;
  - a binary without `providers register` keeps `config add-backend`.

  The app tells them apart by the field's presence, or by the machine schema's date (`2026-10-26` or later) for a verb. Record which.
- **Secrets never pass through the app.** `providers register` takes no key; keys stay the CLI's own flows.
- **ADR 0004:** each changed state has a preview over recorded documents.

**Seam + files.**
- **`business/reads/`:** `TaskSummary.interrupted` decoded from a row, and `ModelRow.draft`.
- **`presentation/tasks/`:** the task list's row says a killed task was interrupted, with its Resume.
- **`presentation/models/ViewModels/ModelsViewModel.swift`:** the drafter glyph and the cannot-draft state from the row's `draft`, the per-entry `models info` read kept only for a row without it.
- **`presentation/settings/`:**
  - Register through `providers register <id> --output-format json` where the binary has it, its said lines shown;
  - the allowed hosts read through `config get tools.allowed_hosts`.
- **The narrations** (Register, Register all, the delete card from 4e): the CLI's new lines shown.
- **Fixtures:** recorded from the branch binary over a sandbox, the home written `/Users/someone`, with fake provider CLIs and no keys:
  - a `task list` with an interrupted row;
  - a `models list` with paired and cannot-draft rows;
  - `providers register`'s JSON;
  - `config get tools.allowed_hosts`;
  - the add and delete lines.

**Reference.**
- GUI 4e (delete narration), 4h (interrupted tasks) and 4k (Register, Permissions) in [Milestone E](../../assistant/MILESTONES.md#milestone-e--the-audits-gui-findings).
- M5 (the drafter glyph) and M4 (Roles) in [Milestone D](../../assistant/MILESTONES.md#milestone-d--mlx-kinds-in-the-app).
- The CLI's M32 and M33 records.

**Decisions made** (dated):
- 2026-10-26 — **Opened from the CLI and GUI Maintenance run's record** (the user's call: "spec them into the backlog and then execute them").

**Open calls:**
- [default: an interrupted task's row says "interrupted" in the warning colour beside its status, as its page does, and offers Resume from the row's menu.]
- [default: Register uses `providers register` on a binary whose machine schema is dated `2026-10-26` or later, and `config add-backend` otherwise, the roster fetched only as the CLI fetches it.]

**Guardrail(s).**
- **Over recorded rows:** the interrupted task and the drafter states are drawn with no per-row read.
- **Over older rows:** today's per-row reads still run.
- **Register's argv** by binary; the said lines shown in every narration.

**Acceptance criteria:**
- [ ] A killed task reads interrupted in the task list.
- [ ] The Backends table marks drafters from `models list` alone where it can.
- [ ] Register registers one provider as the CLI does, its default and roster said.
- [ ] Permissions reads the allowed hosts through the CLI.
- [ ] The add and delete lines the CLI says are shown.
- [ ] Older binaries work as today; previews, `check-previews`, the layering check and `lib/scripts/cicd.sh --test` pass, with no Swift warning.

**Scope note.** GUI Maintenance, claimable by name, no gate. The app's adoption of the CLI's M32 and M33.
