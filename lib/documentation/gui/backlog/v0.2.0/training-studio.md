# The training studio: runs, regimes and datasets

**What / why.** The whole training track has no GUI place (the 2026-10-20 audit; the TUI has had Train and Datasets views since 37f): the kits and regimes, a run's gate/fuse/promote chain, the datasets store and its miner. This item builds the studio: **Train** — the regimes and kits listed from their documents, a run launched as a supervised job (the trainer's JSONL progress protocol is already structured — the supervisor renders its steps directly, no stderr parsing here), the history and promote/rollback state read from the run documents, promote/rollback launched with their confirmations; **Datasets** — the store's documents, the miner's runs supervised. The TUI's cut is the proven scope: every run and act is the command run as a child, typed arguments and all — the GUI's jobs core is that same shape.

**Core constraint(s).** ADR 0001/0002; every act the person's command (argv-pinned), every view a document re-read after; the trainer's protocol rendered as received (a structured stream already — the one family where no fallback is needed); promote's gate honesty shown as the CLI says it (a failing candidate never reaches inference — the wording relayed); destructive acts (rollback, dataset deletes) confirm. **Design boards land with the screens.** No training ever starts without the person's explicit launch (spend honesty — runs are hours).

**Seam + files.** `source/presentation/training/` (Train + Datasets; view-models), `source/business/reads/` (the regimes/runs/datasets documents), the jobs core (the trainer child's JSONL rendered); `lib/design/spec/` — the boards and contract rows.

**Reference.** The TUI's 37f — the view cut and the run-as-child discipline mirrored; Milestone Z's trainer protocol — the structured stream the supervisor renders; 42a/42b — the regimes and kits the documents describe.

**Decisions made** (dated):
- 2026-10-20 — From the parity audit (the user's mandate); the TUI's Train/Datasets cut mirrored.

**Open calls:**
- [default: the run view's step rendering per a new design board; details at build.]

**Guardrail(s).** View-model matrices over fixture documents and a scripted trainer stream (steps, the gate verdicts, promote/rollback states); argv pins per act; confirmations; hermetic — no venv, no real training in any test.

**Acceptance criteria:**
- [ ] Train lists regimes/kits/history from documents, launches runs supervised with the protocol's own steps, and drives promote/rollback with confirmations and read-back.
- [ ] Datasets lists and mines over the real commands; the boards exist; the full suite is green.

**Scope note.** Earmarked for v0.2.0 (track 2); gated on [`parity-law-and-table.md`](parity-law-and-table.md) (2a). Out of scope: `train setup` beyond what Settings' install consents already do, any trainer change, the cycle's unattended scheduling (the CLI's own).
