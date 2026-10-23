# Children end cleanly and are seen: grandchildren, discovery, PATH and the monitor

**What / why.** The app supervises every `apogee` child through `infrastructure/process` and `business/jobs`. The CLI/GUI audit (2026-10-22, three-lens verified) found five defects in that machinery and its surroundings:

1. **A grandchild holding stdout wedges a supervised job's end after Stop.** `pump()` iterates `for await line in child.lines` before `waitForExit` (`SupervisedJob.swift:49-58`), and the lines stream finishes only at stdout EOF (`ProcessSpawner.swift:74-87`). The 2-second grandchild failsafe exists for stderr only (`:107-109`, the hazard named in its comment). `stop()`'s interrupt/terminate/kill signal only the child's pid (`SupervisedJob.swift:76-84`), so a surviving grandchild (ffmpeg, a driver) that inherited stdout keeps the pipe open. `.ended` is never yielded, and every screen shows the job running with Stop apparently ignored until the app restarts.
2. **`FoundationChild` leaks when a grandchild holds a pipe.** The timeout path (`ProcessSpawner.swift:107-109`) calls `stderrClosed()` without clearing `readabilityHandler`, and both the stderr (`:88-96`) and stdout (`:74-87`) handlers strongly capture `self`. A grandchild pins the object indefinitely.
3. **A failing binary override silently falls back to another install.** `locate()` returns `.found(found, skipped:)` with the override's failure reason, and every consumer discards it (`SessionViewModel.swift:179`, `SessionLibraryViewModel.swift:163`: `case .found(let found, _)`). The app runs a different binary than the one pointed at. Settings shows only "From the next launch: <override>", which reads as "not applied yet" rather than "tried and failed". Discovery itself is tested (`BinaryDiscoveryTests.swift:79-92`); no consumer surfaces it.
4. **`ShellPath` reads a junk PATH from a profile that prints its environment.** `between()` (`ShellPath.swift:218-225`) takes the text between the first two unanchored occurrences of the marker, and the marker's exact value sits in the shell's environment (`APOGEE_PATH_MARKER`, `:182-189`). A login profile that runs `env`, `export -p` or `set` prints `APOGEE_PATH_MARKER=<marker>` first, so the "PATH" becomes the rest of the dump. It is reported as `.shell`, applied to every child, and shown in Settings as from the login shell, contradicting the module's own "a profile that prints is harmless". The test covers only a marker-like decoy (`ShellPathTests.swift:67`).
5. **The system monitor misses the heaviest children.** `liveChildren` (`AppViewModel.swift:111-117`) lists the session, downloads and tasks only. Video, Compose, Voice and Training runs (and graph, knowledge, symphony and review supervised runs) expose no children, though `JobRunning` carries `processIdentifier` for exactly this (`SupervisedJob.swift:16-17`, `:144-150`). The monitor shipped (1k) before those screens ran jobs and was never extended.

**Core constraint(s).** **A stopped job always ends**: after termination, both streams finish within a bounded failsafe (stdout mirroring stderr's), and stop signals the child's process group so grandchildren end with it. **No retain cycles on pipes**: handlers are cleared on every end path and capture weakly. **The binary in use is the one said**: a skipped override's reason is shown where the in-use path is shown (Settings › General) and once at launch. **A marker stands alone**: `ShellPath` matches the marker only on its own line. **Every supervised child is seen**: jobs register with one children registry the monitor reads, never per-screen properties. ADR 0001: process facts in Infrastructure, the registry in Business, the monitor in Presentation.

**Seam + files.** `infrastructure/process/ProcessSpawner.swift` (stdout failsafe; handlers cleared and weak; process-group signalling), `business/jobs/SupervisedJob.swift` (stop by group; ends registered), a `business/jobs` children registry, `presentation/app/ViewModels/AppViewModel.swift` (`liveChildren` from the registry), `data/discovery/BinaryDiscovery.swift` consumers + `presentation/settings` (skipped reasons said), `infrastructure/process/ShellPath.swift` (line-anchored marker). Tests: a fake child whose grandchild holds stdout ends after stop within the failsafe; handler release on timeout; an override failing with a release install answering is surfaced; a profile printing `export -p` resolves the real PATH; every screen's job appears in `liveChildren`.

**Reference.** GUI MILESTONES 1b/1i/1k: the spawner, the supervisor and the monitor; the CLI's captured-child invariant mirrored; the CLI/GUI audit (2026-10-22).

**Decisions made** (dated):
- 2026-10-22: Specced from the CLI/GUI audit (the user's call).

**Open calls:**
- [default: children are spawned in their own process group and `stop()` escalates to the group, so grandchildren end with the job.]

**Guardrail(s).** The process-tree cases over real short-lived scripts in the test bundle (no `apogee` binary needed); the registry pin for every job-running view-model.

**Acceptance criteria:**
- [ ] Stop always ends a job's card, even when a grandchild held a pipe; no `FoundationChild` outlives its process.
- [ ] A failing override is said with its reason; a printing profile never poisons PATH.
- [ ] The monitor lists every supervised child; the full suite is green.

**Scope note.** Earmarked for v0.2.0 (track 4); ungated.
