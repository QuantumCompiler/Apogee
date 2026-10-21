# Convert, quantize and check: the store's long runs as jobs

**What / why.** 2h gives every stored set a place. This item makes the store's long runs work from there, the way the CLI runs them. It is the second half of the user's 2026-10-20 ask: "look at like the safe tensors and then convert them and everything else like you can in the CLI".

Measured the same day, by reading the source and probing the installed `apogee 0.1.9 (73495a5)`:
- **The GUI reaches neither verb.** No path leads to `models convert` or `models quantize`.
- **A check holds up the place.** `models repair` runs as a one-shot command through `ModelsViewModel.repair` → `run` (`ModelsViewModel.swift:299–327`). It holds the place's `working` state through a re-hash of up to tens of gigabytes, with no Stop.
- **Only pulls ride the jobs core.** It takes any verb (`SupervisedJob` spawns `apogee <arguments>`), but only pulls use it (`DownloadsViewModel`, `streamingVerbs` at `:104`).
- **None of the three verbs has a machine face.** `models convert`, `models quantize` and `models repair` take no `--output-format`: 0 of 3. Their progress and results are text. The contract board records these as gaps G18 (convert and quantize progress) and G20 (a check's result), and the design draws a fallback for each.

What this item builds:
- **The convert sheet** (the board `ScreenModelsConvert`) makes a GGUF for llama.cpp or an MLX model for Apple silicon from a SafeTensors set:
  - **Precision `-t`.** GGUF: `f16` (the default), `bf16`, `f32`, `q8_0`, `auto`. MLX: `4bit` (the default), `3bit`, `6bit`, `8bit`, `mxfp4`.
  - **Further levels.** GGUF: quantize to `Q2_K` through `Q8_0`. MLX: one conversion per further precision. Either way it is `--register-with`; each level is its own backend, and choosing one turns registering on.
  - **Register what's made** (`--register`), with the names' beginning (`--base-name`, defaulting to the model's base).
  - **MLX's `--model-type` override** (the CLI's M15).
  - **The steps listed**, with sizes estimated from bits per weight and said as estimates.
  - **The command, shown as typed.** It is one command: `apogee models convert <set> [--mlx] [-t …] [--register | --register-with …] [--base-name …] [--model-type …]`.
- **The quantize sheet** works from an unquantized GGUF (`F16`, `BF16`, `F32`):
  - the level, `Q4_K_M` by default (the CLI's default), with estimated sizes;
  - **Register it**: `models quantize` has no `--register`, so the app then runs `config add-backend <name> --type llamacpp --model-path <the new file>`, with the new file read from the store's read-back;
  - the name, defaulting to `<base>-<level>`.
- **Check the files** runs `models repair <handle>` as a job.
- **The Jobs tab** grows from 1i's Downloads. It holds pulls, conversions, quantizes and checks, each a card with its steps. Until G18 lands:
  - each step ticks as its new set appears in `models list --all`, read again on each line the verb prints and at its end;
  - the running step shows how long it has run, with no percent;
  - the CLI's text goes to the log;
  - Stop is Ctrl-C's interrupt: the CLI removes its staging, and the sets already made stay;
  - a resume command the CLI names runs as typed (1i).
- **At the end**: the CLI's own last words, the store read back, and **Show the weights**, which opens the first new set.

**Core constraint(s).**
- **One supervisor.** Every run goes through `BusinessJobs`; there is no second process path.
- **The argv is exactly what the sheet shows.**
- **Nothing is decided from prose.** Stdout lines and the busy line are only shown; the store's read-back decides what landed.
- **The CLI's refusals and notices appear in its words.** Among them: an architecture its converter can't take, a missing MLX runtime, a conversion already made (`find_conversion`), and a level already made.
- **No `--yes` is added** (1i's decision for pulls, mirrored).
- **Sizes are estimates, said as such**, and never used to decide anything.
- **Stop is an interrupt, never a kill first.**
- ADR 0001 and ADR 0002 hold.

**Seam + files.**
- `source/presentation/models/` (`PresentationModels`):
  - `DownloadsViewModel` grows into the Jobs tab's view-model, holding pulls and store jobs.
  - `ConvertForm` and `QuantizeForm`: pure argv builders, SwiftUI-free.
  - `Views/ConvertSheet` and `Views/QuantizeSheet`.
  - `DownloadsView` → the Jobs view.
  - `ModelsViewModel.repair` moves onto the jobs core.
- `source/business/jobs/`: `JobProgress` folds the read-back's landed sets into step ticks, if the fold belongs there rather than in the view-model; settle this at build.
- `source/business/reads/Models.swift`: the read-back's difference, saying which sets are new.
- `lib/design/spec/`: the boards already exist (`ScreenModelsConvert`, and `ScreenModelsDownloads`, retitled "Models · jobs"); fix drift on both sides.

**Reference.**
- **1i:** the supervisor, Stop as Ctrl-C, the named resume command run as typed, the fallback's step words, and "nothing decides on them".
- **The CLI's items:**
  - M3: the register chain, its resume line and kept work.
  - M15: `--model-type` over an overlay, recorded in provenance.
  - M16: MLX `--register-with`, one conversion per precision.
- **The CLI's DEVELOPER.md, the `models_pull.h/.cpp` row:** convert's live progress from the growing `.partial`; `InterruptScope` removing the staging; `find_conversion`; quantize working from the newest unquantized GGUF and carrying the projector.
- **The design:** the contract board's G18 and G20 rows.

**Decisions made** (dated):
- 2026-10-20 — The sheets build the CLI's own chain: one `models convert … --register-with …`, not the app sequencing convert, quantize and add-backend itself. M3's resume line and kept work stay the CLI's.
- 2026-10-20 — Until G18, progress is the store's read-back plus elapsed time, with no percent drawn (the design's fallback, canvas version 33).
- 2026-10-20 — The Downloads tab becomes Jobs (the design's cut).
- 2026-10-20 — No CLI sibling now. G18 and G20 are later CLI growth (2c's precedent): the fallbacks are complete without them, and a stream would be preferred once announced, as 1i prefers 52b's.

**Open calls:**
- [default: quantize's Register it runs `config add-backend` after the quantize ends, with the new file taken from the read-back. If the CLI refuses (a taken name, say), the GGUF stays made and the refusal is said in the CLI's words.]
- [default: estimated sizes are bits per weight over the set's `bytes` from `models info`, labelled "about".]
- [default: Check the files moves from the one-shot `run` to a job. Its result is the exit status plus the CLI's last line (G20's fallback).]

**Guardrail(s).**
- **Pure argv matrices for the convert form:** GGUF and MLX × precision × levels × register × base name × model type, each command byte-exact. The flags are pinned against the binary's own `--help` words, recorded as a fixture.
- **Pure argv matrices for the quantize form**, with and without its follow-on `add-backend`.
- **Scripted supervised runs for each verb:**
  - steps ticking from scripted read-backs;
  - Stop mid-run, leaving the made sets kept;
  - the CLI's refusals and its resume command relayed and run as typed;
  - a check as a job, passing and failing.
- **Hermetic.** No conversion runs in the tests: the standing rule against downloading or converting real models. A real conversion through the app is the user's run.

**Acceptance criteria:**
- [ ] From a SafeTensors set, Convert to GGUF… and Convert to MLX… run the sheet's command as a job on the Jobs tab. The made sets appear under Stored weights as they land, registered under the names shown.
- [ ] From an unquantized GGUF, Quantize… makes the level and, when asked, registers it.
- [ ] Check the files runs as a job, with its result in the CLI's words.
- [ ] Stop interrupts the run, the made sets stay, and the CLI's resume command runs.
- [ ] The full suite is green, and the boards match the screen.

**Scope note.** Earmarked for v0.2.0 (track 2). Gated on 2h, the model store screen ([shipped 2026-10-21](../../assistant/MILESTONES.md#milestone-c--cli-parity)): the sheets open from its set details, and its Jobs tab is the home this grows.

Out of scope:
- the CLI's G18 and G20 machine faces (later CLI items);
- pulls, which are 1i's and unchanged;
- the training studio's own conversions (2d).
