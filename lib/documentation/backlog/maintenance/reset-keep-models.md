# A selective reset: fresh state, models kept

**What / why.** The user's ask (2026-10-04): building from source, do "pretty much a fresh build but replace everything except the models directory in the application directory." Today that middle path does not exist — it is all-or-nothing: `apogee uninstall` removes `~/.apogee` whole (models included — tens of gigabytes to re-pull) or `--keep-data` spares it whole; `make install` ends in `apogee check --fix`, which only *creates missing* layout rows, never resets existing state; and hand-deleting the non-models rows means guessing the layout by hand — the exact drift class the one-declaration rule exists to close. This item adds **`apogee reset`**: the data directory back to a verified first-run state, selectively. The plan is built from `layout.h`'s single declaration (the `plan_uninstall` machinery generalized with a **keep set**), shown whole before anything is deleted; `--keep <row>` is repeatable over the top-level layout rows — `--keep models` is the asked-for carve-out — with the rows tab-completable ([ADR tab-completion](../../adrs/cli/tab-completion.md)); user-data rows (models, chats, memory, the secrets store, training runs) are warned by name exactly as uninstall warns them; and the run ends with the `check --fix` skeleton recreation, so what remains is a *verified* fresh state, not a hollowed directory. A **`make reinstall`** target chains the from-source flow the ask describes: fresh build → install → `apogee reset --keep models`, with the reset's confirmation left interactive — the Makefile never answers a destructive prompt for the user. The verb lives in the binary, not the Makefile, for uninstall's own recorded reason: two installers, and a removal implemented in shell drifts.

**Core constraint(s).**
- **One layout declaration:** the plan enumerates `layout.h`'s rows at runtime — no list of directories in the command, the Makefile, or the docs that could go stale against the layout (the install-parity bug class, closed the same way uninstall closed it).
- **Uninstall's confirmation discipline, verbatim:** the whole plan shown first (kept rows marked kept, removed rows marked removed, user-data rows warned by name); a terminal gets the typed confirmation, `--yes` skips it, and a pipe without `--yes` refuses — a piped reset must not decide on the user's behalf.
- **Secrets are user data:** the 0600 store resets with everything else and the plan says so by name — credentials gone is a consequence to state loudly, never discover.
- **Nothing outside `APOGEE_HOME`:** the binary, completions and shell files are uninstall's business; reset touches the data directory only.
- **Kept means untouched:** a kept row is not recreated, migrated or rewritten — `models/` after a reset is byte-identical, store records included, so every registered path a fresh config later names still resolves.
- **Partial failure is reported, not hidden:** the `errors` collection pattern from `execute_uninstall`, carried — a row that could not be removed is named, and `check` then tells the truth about the directory's state.
- Code style carries: `.h`/`.cpp` pairs, smart pointers only; tests in the layer of what they test ([ADR](../../adrs/cli/tests-mirror-architecture.md)).

**Seam + files.**
- `presentation/cli/uninstall.h/.cpp`: the plan machinery generalized — `plan_uninstall` and a reset plan share the row walk and the user-data detection; `describe_plan` gains the kept/removed marking.
- `presentation/cli/reset.h/.cpp` (new command, or beside uninstall in its module per A3's allow-lists): `apogee reset [--keep <row>]... [--yes]`; ends by running the `check --fix` pass in-process.
- The completion seam: `--keep` offers the top-level layout rows (the store-aware precedent, M7).
- `lib/src/cli/Makefile`: `reinstall` — fresh build, `install`, then `apogee reset --keep models` (interactive); the help text names what it keeps.
- Tests: `tests/presentation/cli/` over sandboxed temp `APOGEE_HOME`s — plan goldens (kept/removed/user-data wording), the keep set honored (kept rows byte-identical after), skeleton recreated and `check` green, refuse-on-pipe, partial-failure reporting; the completion golden.
- Consumes: the layout contract (shipped), uninstall's plan/confirm/execute shapes (shipped), `check --fix` (shipped), M7's completion precedent (shipped).

**Reference (Ommi).** The analog is `make uninstall` (`lib/cli/Makefile`): shell-implemented, prompts, and all-or-nothing on `~/.ommi` — plus a `nuke` target chaining clean + uninstall. No selective reset exists there; the shell implementation is the drift lesson Apogee's uninstall header already records, and this item follows the in-binary divergence, not the Ommi shape.

**Decisions made** (dated):
- 2026-10-04 — Asked for by the user; placed in **Maintenance** (their call), M9 by the ever-assigned rule.
- 2026-10-04 — A new `reset` verb rather than widening `uninstall`: uninstall's contract is "Apogee is gone"; reset's is "Apogee starts over" — overloading one verb with both reads blurs the one prompt that must never be misread.
- 2026-10-04 — The keep set is generic (`--keep <row>`, completable) with models the motivating case *(recorded as the default, vetoable)*: chats or training are the same shape of ask one week later, and a generic flag costs nothing over a `--keep-models` one-off.
- 2026-10-04 — Bare `apogee reset` resets **every** row *(the default, vetoable)*: predictable symmetry, with the kept carve-out always explicit — a default carve-out nobody typed is state surviving a "fresh start" by surprise.
- 2026-10-04 — The Makefile target never passes `--yes`: a destructive confirmation belongs to the person, even mid-target.

**Open calls:**
- [default: the make target is named `reinstall` — it reads as what it does from the source tree; `fresh-install` stays available if the user prefers the ask's own word]

**Guardrail(s).**
- Sandbox goldens: a populated temp `APOGEE_HOME` (models + chats + secrets + training) reset with `--keep models` keeps `models/` byte-identical, removes the rest, recreates the skeleton, and `apogee check` reports green; bare reset leaves only the skeleton.
- The wording pins: the plan names every user-data row it will remove (secrets included) before the prompt; refuse-on-pipe without `--yes` asserted.
- The no-second-list pin: a row added to `layout.h` appears in the reset plan with no change to the command (asserted by a test fixture row, the parity test's spirit).
- Completion: `apogee reset --keep <TAB>` offers exactly the top-level layout rows.

**Acceptance criteria:**
- [ ] On a populated install: `apogee reset --keep models` shows the full plan, asks, removes everything but `models/`, recreates the skeleton, and a following `apogee models list` still shows every stored model; registered backends re-resolve once config is re-created.
- [ ] `make -C lib/src/cli reinstall` does a fresh build, installs, and lands in the interactive reset with models kept — one command for the ask's whole flow.
- [ ] Bare `apogee reset` (confirmed) leaves exactly the verified first-run skeleton; a piped `apogee reset` without `--yes` refuses with the stated remediation.
- [ ] The plan warns secrets and chats by name before removal; a row that cannot be removed is reported and `check` reflects the real state.

**Scope note.** Maintenance item **M9**; gated on nothing. Out of scope: widening `uninstall` with per-row keeps (its all-or-nothing contract stands); resetting anything outside `APOGEE_HOME`; migrating or rewriting kept rows (kept means untouched); a config-preserving merge-reset (a reset config is a fresh config — `config upgrade`'s teaching template arrives via `check --fix`'s init path as on any first run).
