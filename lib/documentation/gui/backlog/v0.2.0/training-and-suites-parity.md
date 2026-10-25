# Training and suites reach the CLI's v0.2.0 surface: adapters, datasets pull, shared bases, a chat under a suite

**What / why.** The full-parity law (gui SPEC; ADR 0003) says every CLI capability has its place on a screen. The CLI's track 53 and the TUI's suite actions shipped after the GUI's Training and Suites screens, and the CLI/GUI audit (2026-10-22, three-lens verified) found four screen gaps:

1. **The Training place cannot promote adapters, and an entry's adapters are invisible.** `PromoteForm` (`TrainingViewModel.swift:57-79`) builds only run, `--as`, `--target`, `--quantize` and `--force`. It lacks `--adapter` and the quantized-base gate's `--suite`/`--judge` (and `--trainer`/`--keep-fused`; `train.cpp:2399-2417`). Since `target_conflict` refuses a plain promote into a ledger holding adapter versions, naming `--adapter` (`promote.cpp:264-293`), an adapter-ledger backend is unpromotable from the screen. The Models place neither shows nor edits a backend's `adapters` (`config set-backend --adapters`, 53a); grep finds no `adapters` surface in the app. Both are console-reachable, but the law calls for screens.
2. **The Datasets tab lacks `datasets pull`.** It offers create, synthesize, prepare, delete and the kits (`TrainingViewModel.swift:94`, `:113`, `:182-183`, `:271`, `:279`), but not `datasets pull` (CLI `datasets.cpp:515`, `owner/name[@revision][:file]`). The parity row (`Parity.swift:59`) doesn't mention it.
3. **The Suites place's fit double-counts bases 53d now shares, and prices no adapters.** `SuitesViewModel.swift:150-161` holds weights+cache once per distinct backend name, while the CLI's footprint counts a shared base once by `BaseKey` and prices adapters by their files (`footprint.cpp:76-87`, `:209-240`). A shared-base suite the CLI admits shows "does not fit". CLI M25 will add a member's drafter to the same footprint.
4. **No way to run or switch a chat under a suite.** In the TUI, Suites' Enter sends `/suite <name>` to the open conversation or opens a chat under it (`tui_workbench.cpp:171-175`, `tui_session.cpp:391-401`). The CLI's `chat --suite` (`chat_session.cpp:650-656`) and `/suite [name|off] [--force --warm]` exist. The app's Suites screen has only select and make-default. The slash list has no `/suite` (`SessionActions.swift:56-71`), `RunSettings` has no suite (`DriverLine.swift:56-73`), and `ChatLaunch` passes `--suite` only for execute (`:85`). A plain app chat runs under the global default suite only.

**Core constraint(s).** **The screen reaches the verb** (the full-parity law): the promote sheet carries `--adapter` with its gate inputs and the CLI's refusal texts; an entry's adapters are listed with their scale and pin state (from `config get`, once CLI 55g makes it readable, or from `check`'s adapter rows until then) and edited through `set-backend --adapters`; datasets pull is a supervised job. **The fit is the CLI's when it can be**: the Suites place renders the CLI's footprint verdict (prefer a CLI read over a GUI re-derivation, since the re-derivation already drifted once). Until such a read exists, the fallback mirrors `BaseKey` sharing and adapter pricing, said as the app's estimate. **A session's suite is chosen per session**: new chats take a suite at launch (`--suite`), and an open session switches with the CLI's `/suite` semantics. If the protocol has no line for it, the switch is a resume with `--suite`, said, mirroring the TUI. ADR 0002, read-back after every mutation.

**Seam + files.** `presentation/training/ViewModels/TrainingViewModel.swift` + views (the adapter promote mode; datasets pull as a job), `presentation/models/` (an entry's adapters listed and edited), `presentation/suites/ViewModels/SuitesViewModel.swift` (fit by base key with adapters, or the CLI's verdict), `business/session/ChatLaunch.swift` + `presentation/sessions` (suite at launch and switch; a `/suite` slash action), `presentation/shell/ViewModels/Parity.swift` (rows updated). Tests: promote argv for adapter mode with suite/judge; adapters listed from a check/config fixture; datasets pull argv; the shared-base fit fixture admitted; a chat launched and switched under a suite.

**Reference.** The gui SPEC's Full-parity principle and ADR 0003; CLI track 53 (Milestone AP/AQ); the TUI's suite actions; [CLI 55g](../../../cli/assistant/MILESTONES.md#milestone-ar--the-audits-cli-findings) (readable adapters, shipped 2026-10-25) and [CLI 55e](../../../cli/assistant/MILESTONES.md#milestone-ar--the-audits-cli-findings) (shipped 2026-10-25), both referenced and not gated; the CLI/GUI audit (2026-10-22).

**Decisions made** (dated):
- 2026-10-22: Specced from the CLI/GUI audit (the user's call). Pipeline and cycle screens are their own item ([`pipeline-and-cycle-screens.md`](pipeline-and-cycle-screens.md), 4j) because they wait on CLI 55h's JSON. These four do not.

**Open calls:**
- [default: a live session's suite switch is a pause-and-resume with `--suite`, said in the transcript, until the protocol grows a suite field on `set`.]

**Guardrail(s).** Argv pins per new flag; fixture-driven fit and adapter views; read-back after mutations.

**Acceptance criteria:**
- [ ] Adapters can be promoted, listed and edited from the screens.
- [ ] Datasets can be pulled from the Datasets tab; a shared-base suite fits as the CLI says.
- [ ] A chat can be started under, or switched to, any suite or none; the full suite is green.

**Scope note.** Earmarked for v0.2.0 (track 4); ungated.
