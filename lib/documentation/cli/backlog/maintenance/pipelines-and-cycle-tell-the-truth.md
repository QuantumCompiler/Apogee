# Pipelines and the cycle tell the truth: halts kept, failures said, specs listed

**What / why.** The macOS app's 4j (shipped 2026-10-26) drew the Training place's Pipelines and Cycle tabs on 55h's documents and streams. Its probes, reproduced again on `apogee 0.2.0 (4b828e91)` over sandbox homes with the mock trainer on 2026-10-26, found five places where the pipeline and cycle verbs say less than the truth, or nothing:
1. **A halt taken during a pass is lost.** With a pass blocked holding the cycle's lock (its stream into an unread FIFO), `train cycle halt` answers `cycle halted -- …`, exits 0, and history.json says `halted: true`. Once the pass drains it ends `passed`, and history.json reads `{'halted': False, 'halt_reason': ''}`. The next `train cycle run` is not refused: `cycle PASSED -- nightly-model promoted to v2`.

   The cause is `business/training/cycle.cpp:403`, which loads the history once; its `save` lambda (`:417`) writes that stale copy at every outcome. The halt verb (`presentation/cli/train.cpp:3606–3626`) writes without the lock. `cycle resume` taken mid-pass loses its fail-count reset the same way (read from the code).
2. **A failed pass under `stream-json` writes nothing to stderr.** A regressing day exits 2 with 0 bytes on stderr. The stream ends `pass failed`, `pass halted`, the document, then `error "cycle FAILED -- …"`, while `machine-mode.md`'s training section says "a refusal or a failure is said there as well". The cause is `train.cpp:3541–3544`: the gate-fail branch calls `job->failed()` and throws without writing stderr, unlike the pipeline's (`:2948–2953`).
3. **A resumed stage repeats the previous attempt's score.** After an abort with stage b failed at 0.5, `train pipeline resume … --output-format stream-json` emits `b training 0.5` and `b evaluating 0.5`, then `b passed 1.0`. Read mid-resume, the manifest shows `status: training, cumulative_score: 0.5` with the old `eval_results`. The cause is `business/training/pipeline.cpp:102`: re-training sets `status = training` without resetting `eval`, `cumulative_score`, `cumulative_passed` or the stale `fused_dir`/`adapter_dir`. The relay (`train_stream.cpp:21–22`) forwards what the manifest holds.
4. **No read lists the named pipeline specs or their stage plan.** `train pipeline list` answers `A subcommand is required`, `train pipeline status` lists runs, and `config get training.pipelines` says `no such config key`. Only `__complete train pipeline run --pipeline ""` names them, and `check`'s `pipeline: nightly` row gives a name and a stage count. The `pipeline` group (`train.cpp:2797–2852`) registers no `list` or `show`. The app's Pipelines tab takes the names from completion.
5. **`datasets pull` has no stream.** `datasets pull org/ds --output-format stream-json` answers `The following arguments were not expected`, exit 109.
   - `presentation/cli/datasets.cpp:512–578` registers no `add_job_format` and opens no `JobStream`.
   - It uses `stdout_download_options` (`:563`), has no Ctrl-C handling (`:525`), and hard-wires `CurlTransport` (`:523`), so it has no seam for a hermetic test.

**Core constraint(s).**
- **The cycle honours a halt from anywhere,** as tasks honour a halt left by another process (`business/tasks/ledger.cpp:179/192`, `runner.cpp:51`). The pass's save re-reads the halt fields under the lock and keeps them; nothing a person halted is quietly resumed.
- **`machine-mode.md` already decided** that a failure is said on stderr too. This item makes the cycle keep its word.
- **A manifest field means what it says.** A stage that trains again holds no score until it is evaluated again.
- **The protocol grows additively** (ADR 0006).
  - **The new reads:** `train pipeline list` and `show`, with `--output-format json`.
  - **`datasets pull`'s stream** uses existing events.
  - **Their lines in `machine-mode.md`:** each is named there, or `cli.machine_schema_conformance` fails.
- **A pull is cancellable,** as every `models pull` is since 55c: Ctrl-C ends the stream on its `error` and leaves nothing half-written.

**Seam + files.**
- **`business/training/cycle.cpp`:** the `save` lambda (`:417`) re-reads the history's halt fields (and the fail count `resume` reset) under the lock before writing. **`presentation/cli/train.cpp`:** `cycle halt` and `cycle resume` (`:3606–3626`) take the same lock, or a lock-free halt request is kept as tasks keep theirs.
- **`train.cpp:3541–3544`:** the failed pass written to stderr, as the pipeline's branch does (`:2948–2953`, `fail_user` at `:70–77`).
- **`business/training/pipeline.cpp:102`:** a re-trained stage's `eval`, `cumulative_score`, `cumulative_passed`, `fused_dir` and `adapter_dir` reset, as a fresh stage record is made (around `:505`).
- **`train.cpp`'s `pipeline` group:** `list` and `show <name>`, mirroring `train regime list|show` (42a, `train.cpp:3074–3161`), with completion.
- **`presentation/cli/datasets.cpp:512–578`:**
  - `add_job_format` and a `JobStream`, with `job_download_options`;
  - `tell_job`/`fail`, and Ctrl-C;
  - a `TransportMaker` seam, as `models pull` has (`models_pull.cpp:178`, `:73–93`; `models_pull.h:582`).
- **`lib/documentation/cli/reference/machine-mode.md`:** the new reads and the pull's stream.

**Reference.**
- 55h's `train_reads` (`operations/`), the documents the new reads join.
- 42a's `train regime list|show`.
- `models pull`'s job stream and transport seam (52b, 55c).
- Tasks' halt request read across processes (`business/tasks/`).

**Decisions made** (dated):
- 2026-10-26 — **Opened from the macOS app's 4j record** (the user's call: "spec them into the backlog"; placed in CLI Maintenance the same day). Every finding was reproduced again on the branch binary before this document was written.

**Open calls:**
- [default: the pass keeps a halt taken mid-pass and says so at its end as a `notice`, never as the `pass` state `halted`, which is documented as the circuit breaker tripping.]
- [default: `train pipeline list` prints each named spec with its stage count, and `train pipeline show <name>` prints its stages (name, dataset, gate), both with `--output-format json`.]
- [default: `datasets pull`'s stream ends with a `document` naming where the dataset landed (the folder the app's Prepare it takes), its shape added to `machine-mode.md`.]

**Guardrail(s).**
- **The bug becomes a fixture:**
  - **a halt written mid-pass** from a callback survives the pass's save (`tests/business/training/cycle_test.cpp`), and the next `cycle run` is refused;
  - **the same over the binary,** with the pass held on its lock (`train_test.cpp` around `:1951`, `cli.train_lifecycle`).
- **A failed pass under `stream-json` writes stderr,** pinned as the pipeline's is (`train_test.cpp` around `:2526`, mirroring `:2361–2364`).
- **A resumed stage's stream and manifest** carry no stale score (`pipeline_test.cpp:161`, `train_test.cpp:2302`).
- **The spec reads:**
  - `train pipeline list` and `show` beside the regime cases (`train_test.cpp:1747–1789`), with completion (`cli.shell_completion`) and conformance's doc lines (`cli.machine_schema_conformance`);
  - each verb's `--help` and completion added in the same change (ADR 0007).
- **`datasets pull`'s stream** over a scripted transport (`datasets_test.cpp`, modelled on `job_stream_test.cpp:140`), Ctrl-C included.

**Acceptance criteria:**
- [ ] A cycle halted during a pass stays halted, and the next run is refused.
- [ ] A failed cycle pass under `stream-json` says so on stderr too.
- [ ] A resumed stage reports no score until it is evaluated again, in its stream and its manifest.
- [ ] The named pipeline specs and their stages are listed by `train pipeline list` and `show`, as text and JSON, and completable.
- [ ] `datasets pull` streams as a job, cancellable, with a document at its end.
- [ ] `lib/scripts/cicd.sh --test` passes with zero warnings, and the changed files pass the MinGW scan.

**Scope note.** CLI Maintenance, claimable by name, no gate. The macOS app's Pipelines tab names its specs from completion and its datasets pull reads prose. Adopting the new reads and stream is a later GUI item, not gated. The design's open G21 (`train run` has no stream) uses the same relay pieces as finding 5, but stays out of this item.
