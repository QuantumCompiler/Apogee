# Install bootstrap: a fresh install that arrives ready

**What / why.** A fresh install today ends with a ritual the user ran by hand and rejected (the transcript, 2026-10-19): four `apogee train setup --with …` invocations (`convert`, `mlx`, `mlx-vlm`, `video` — each creating-or-extending the owned venv and installing its set) followed by `apogee providers scan --register` and `apogee config scan --register` — six commands before the box does what it can. This item makes the install arrive ready: a new **`apogee bootstrap`** verb — the runtime sets installed (the whole set table, enumerated from `train setup`'s own one declaration so future sets like the voice runtime's join automatically), then the providers scanned and registered, then the model store scanned and registered — **each step said as it runs, non-fatal offline (skipped and named, the install still succeeding), idempotent** (a second run prints the "already" lines the transcript shows), and re-runnable any time. Both installers and `make install` run it **after** `apogee check --fix`, on every install path identically; `--minimal` and `APOGEE_MINIMAL_INSTALL` skip it whole for CI and air-gapped boxes, the skip said with the re-run hint. Two recorded shapes were deliberately changed by the user's call, both dated: the SPEC's installers-download-nothing non-goal is **amended** (runtime packages at install, said and skippable; *models* stay the user's to pull, always), and 28b's registration-as-offer becomes **registration-as-action at install** (the offer machinery untouched for later re-scans).

**Core constraint(s).** **No second declaration anywhere**: the sets come from `train setup`'s own table, the registrations are the shipped scan cores (`providers scan --register`, `config scan --register`) called as they are — bootstrap composes, never reimplements. **`check` is untouched**: it still never edits config and `--fix` still never downloads; bootstrap is its own verb, after. **Offline honesty**: a step that cannot reach the network (or a machine with nothing to register) skips with its reason and the re-run line; the install's exit stays green — fail-loud is for broken layouts, not absent Wi-Fi. **Install parity holds**: every install path (install.sh, install.ps1, `make install`, every channel) runs the same bootstrap at the same point, and `cli.install_parity` grows to pin it — under `APOGEE_MINIMAL_INSTALL` in the test (no network in tests, ever), asserting the identical skip behavior across paths. The verb is classified (ADR 0010), completion free (ADR 0007), and a `--output-format json` summary rides per 28h.

**Seam + files.**
- `presentation/cli/bootstrap_cmd.h/.cpp` — the verb: the set walk over `train setup`'s core, the two scan-register cores, the per-step narration, `--minimal`, the summary (+ JSON); the parity-table row.
- `lib/scripts/install.sh`, `lib/scripts/install.ps1`, `lib/src/cli/Makefile` — the one added call after `check --fix` (each script stays layout-ignorant: the binary does everything).
- `lib/documentation/assistant/SPEC.md` — the dated non-goal amendment (landed with this item's creation, 2026-10-19).
- Tests: the verb over fakes (each step's core invoked once, order pinned; offline skips said; idempotence — the "already" lines; `--minimal` skipping whole with the hint; the JSON summary conformant); `cli.install_parity` grown (every path bootstraps identically under the minimal env); existing setup/scan suites untouched.

**Reference.** The user's fresh-install transcript (2026-10-19) — the ritual this removes, including the "environment already exists / installing the set" lines the idempotence pins; `train setup` (Milestone Z's environment machinery — the one set table); 28b and M12 — the two scan-register cores consumed as-is, the offer's shape change recorded; M11 — installs-end-green, which this extends from "healthy" to "ready".

**Decisions made** (dated):
- 2026-10-19 — The item lands in v0.1.9 (the user's placement call); 51 by the ever-assigned rule.
- 2026-10-19 — **The install bootstraps automatically** (the user's call, from the transcript): the runtime sets, then both scan-registers, after `check --fix`, on every path.
- 2026-10-19 — The SPEC non-goal amended (the user's call): runtime packages install at install time, said and skippable; models stay the user's to pull, always.
- 2026-10-19 — Registration-as-offer becomes registration-as-action at install (the user's call); the 28b offer machinery stays for later runs and re-scans.
- 2026-10-19 — The set list is `train setup`'s whole table, future sets included automatically — the voice runtime's joins the day 50a ships, with no edit here.
- 2026-10-19 — **One minimal switch**: `--minimal`/`APOGEE_MINIMAL_INSTALL` skip everything after `check --fix` — sets and scans both — the skip said with the `apogee bootstrap` re-run hint (the default, confirmed at grooming).
- 2026-10-19 — **Offline stays green**: offline and nothing-to-register steps exit the install green with their skips named; only a broken layout fails an install — M11's law unchanged (the default, confirmed at grooming).
- 2026-10-19 — **The re-run door is said**: the install output's last line names `apogee bootstrap`; exact wording at build (the default, confirmed at grooming).

**Open calls:** none — groomed 2026-10-19.

**Guardrail(s).** The step matrix over fakes (order, once-each, offline skips, idempotence, minimal, the summary); `cli.install_parity` across paths under the minimal env — no network in any test; the SPEC amendment present and dated; the scan cores proven untouched (their own suites green); a planted second set-list fails the one-declaration scan.

**Acceptance criteria:**
- [ ] A fresh install (any path, any channel) ends bootstrapped: sets installed, providers and store registered — each step said — or skipped-and-said offline/minimal, the install green either way.
- [ ] `apogee bootstrap` re-runs idempotently with the "already" lines; `--minimal` skips whole with the hint; the JSON summary conforms.
- [ ] `check`'s behavior is byte-stable; the scan cores are unchanged; install parity holds across every path.
- [ ] The SPEC amendment is in place, dated; the full suite is green.

**Scope note.** Earmarked for v0.1.9; ungated — it consumes shipped cores (the voice set joins automatically when 50a ships; no gate). Out of scope: any new set, any change to `train setup`'s table shape, model pulls of any kind, the first-launch offer's removal (it stays for installs bootstrapped minimal).
