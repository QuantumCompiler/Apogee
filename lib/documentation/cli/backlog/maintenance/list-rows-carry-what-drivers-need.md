# List rows carry what drivers need: a task's interruption, a backend's drafter

**What / why.** Two list reads omit a fact their detail read states, so a driver must read each row's detail to draw the list honestly. The macOS app found both: 4h (an interrupted task) and M5 (a paired drafter), both shipped 2026-10-26. Each was reproduced again on `apogee 0.2.0 (4b828e91)` over sandbox homes on 2026-10-26:
1. **`task list` rows carry no `interrupted`.** For a task killed with SIGKILL mid-round, `task status` shows `interrupted: true, process: null`, while `task list --output-format json` shows `{"status":"running",…}` and no `interrupted`. The app's task list therefore draws a killed task as running; only its page knows. `business/tasks/view.cpp:111`'s `make_task_summary` has no lock holder to ask, while `make_task_view` already decides it (`:75–84`).
2. **`models list` names no drafter pairing.** `models list --output-format json` is byte-identical before and after `config set-backend e4b-4bit --draft …`. Only `models info`'s prose `draft:` line names the pairing, so the app's Backends table reads every `mlx` entry's `models info` on each refresh to mark a paired base. `presentation/cli/models.cpp:444–505` never reads `backend.draft` for an `mlx` row (it would be set near `:488`). 53i declined a typed `draft_head` "for a client that needs one" (CLI MILESTONES around `:4008`); the app is that client now.

**Core constraint(s).**
- **Additive fields** (ADR 0006). Each is optional on an existing row, absent where it doesn't apply, and named in `machine-mode.md` and, for tasks, `http-api.md`, whose admin twin serves the same rows.
- **One decision, one place.** A row's `interrupted` is decided by the same rule as the view's (`view.cpp:75–84`), never a second copy.
- **A pairing's state is the CLI's word.** The row says whether the drafter can draft, as `models info`'s draft line decides it (53i, M25), never the client's guess.

**Seam + files.**
- **`business/tasks/view.{h,cpp}`:** `TaskSummary` (`view.h:136–142`) gains `interrupted`, filled by `make_task_summary` (`:111`) through `make_task_list` (`:190`) and `to_json` (`:182`). Its callers stay as they are: `task_cmd.cpp:1303`, `httpserver/admin_tasks.cpp:25` and `tui_task.cpp:234`.
- **`presentation/cli/models.{h,cpp}`:** `ModelRow` (`models.h:35`) gains `draft`: the drafter's path, and its refusal when it can't draft. It is set for an `mlx` row from `backend.draft` near `models.cpp:488`, and written by `model_row_json` (`:1043–1051`).
- **`lib/documentation/cli/reference/machine-mode.md`** (`:815` for task rows) and **`http-api.md`** (`:1283`).

**Reference.**
- 53e's optional `kind` on the same model rows: the shape `draft` follows.
- 53i and M25's draft line in `models info`: the words the row carries.

**Decisions made** (dated):
- 2026-10-26 — **Opened from the macOS app's 4h and M5 records** (the user's call: "spec them into the backlog"; placed in CLI Maintenance the same day).

**Open calls:**
- [default: `interrupted` is written on every task row (`false` when not), as the view writes it.]
- [default: an `mlx` row's `draft` is `{path, can_draft, refusal?}`, absent when no drafter is paired.]

**Guardrail(s).**
- **A killed task's list row says `interrupted`:** `tests/business/tasks/view_test.cpp:143/207`, `task_test.cpp:1120`, `admin_tasks_test.cpp:96`, and `task_e2e.sh:349–367` (`cli.task_lifecycle`).
- **A paired base's row carries its drafter,** and a deleted drafter's row its refusal: `models_mlx_test.cpp:1348/2505`, `read_documents_test.cpp:94`, `mlx_models_e2e.sh:453` (`cli.mlx_models_lifecycle`).

**Acceptance criteria:**
- [ ] `task list` rows say whether a task was interrupted, as `task status` does.
- [ ] `models list` rows say an `mlx` base's drafter and whether it can draft.
- [ ] `lib/scripts/cicd.sh --test` passes with zero warnings, and the changed files pass the MinGW scan.

**Scope note.** CLI Maintenance, claimable by name, no gate; small. The macOS app's adoption (its task list's row, and its Backends table dropping the per-entry `models info` read) is a later GUI item, not gated.
