# The doctor views: Check, Providers, System

**What / why.** Three read surfaces the shell cannot show (measured 2026-10-10, the parity spike): `check` has rows and a JSON document (28h) but no view; `providers scan` renders its rows inline with **no machine read** (0 of its 1 verb); `apogee system` has its document (32a) while the shell shows only the bar's live half — 32e's recorded cut deliberately leaves the CPU model, the disk and the GPU detail off the bar. Three views over the one `tui/list_view` machinery: **Check** — the doctor's rows (Pass/Warn/Skip with details), `f` running `--fix` after an ask; **Providers** — the scan's rows (provider, tier, evidence, registered-as), `r` registering one after an ask, through the very core `providers scan --register` runs; **System** — the one snapshot as a page, worded by `operations/system_view` exactly as the command prints it. This closes the doctor's slice of wall W1 (18 of 25 subcommands surfaceless) and its slice of W2 (reads without row functions).

**Core constraint(s).** A view computes nothing: it draws a carved read and calls a carved core (32d's structure, held by the byte-parity tests). `check` **never edits config** and `--fix` keeps M11's honesty — seed the starter config only where none exists, repair the local install, decide nothing about meaning. Registration goes through the one editor and the one registration core (28b/M13 — the fetch-on-register included, on the user's word only); the view adds **no probe** of its own (`cli.no_provider_probes` gates the sweep's reachability). The System page is one read on show (`View::when_shown`), never a second watcher — the bar keeps the ticking (the no-second-estimator rule); unknowns stay said, never guessed. New machine reads follow the 28h idiom so every mode gains them ([ADR 0002](../../adrs/cli/mode-parity.md): reads have twins).

**Seam + files.**
- `lib/src/cli/source/presentation/cli/tui_doctor.h/.cpp` — the three views' composition (32d's shape: views composed in `cli/`, reaching cores, never reaching back), registered in `cli/tui_cmd.cpp`'s shell wiring.
- `cli/check.h/.cpp` — carve `read_check_rows` (the rows the JSON document already carries) so the command, the document and the view read alike; the `--fix` core already exists (`apply_fixes`).
- `cli/providers_cmd.h/.cpp` — carve the scan's rows into a read function, and ship them as `providers scan --output-format json` in the same change (the M12/28h precedent; `lib/documentation/reference/machine-mode.md` gains the document's row); the register action calls the existing registration path per row.
- `operations/system_view.h` — already renders the document; the System page is `tui/`'s `text_view` over it.
- `tests/presentation/cli/tui_doctor_test.cpp` (new), rows in the leak test, classification flips in `tui_parity_test.cpp` (37a's table: check, providers, system → views).

**Reference.** 32d — the carved-read precedent (`read_model_rows`) and the "through its own commands" rule; M11 — `check --fix`'s honesty rules and the skip rows; M12 — the composition pin test style (the register's config byte-equal to the command's); 28b — the offer's wording, reused for the ask.

**Decisions made** (dated):
- 2026-10-10 — The monitor bar is untouched: the System view is the one-shot page beside it, read on show, no tick of its own (32e's scope stands; the user's placement of the set in v0.1.6 on the spike report).

**Open calls:**
- [default: `f` (fix) asks `[y/N]` naming what a fix pass may do before running — a single key is not a typed command (32d's removal idiom); the pass's output is the command's own lines.]
- [default: `r` (register) asks per selected provider row with 28b's offer wording; a provider already registered is refused in the command's words.]

**Guardrail(s).** Byte-parity, the 32d way: the Check view's rows against `check --output-format json`; a register from the view leaving a config byte-identical to `providers scan --register`'s on a twin install (the M12 pin style); the System page's text against `apogee system`'s table for the same faked `SystemSource`. The leak test's planted key searched in all three views' rows. A stale row (the provider gone between read and act) refused as the command refuses.

**Acceptance criteria:**
- [ ] Check, Providers and System are views on the shell, switchable by number, re-read on show.
- [ ] The Check view's rows match the JSON document's; `f` runs the fix pass after an ask and reports the command's own lines.
- [ ] `providers scan --output-format json` exists, documented in machine-mode.md; the view draws those rows; `r` writes a byte-identical config to the command's register.
- [ ] The System page matches `apogee system`'s table; the bar's behavior is unchanged.
- [ ] 37a's classification rows for `check`, `providers`, `system` flip to views; the full suite is green.

**Scope note.** Earmarked for v0.1.6; gated on [37a](tui-parity-law.md). Out of scope: any new probe, any bar change, any `check` semantics change.
