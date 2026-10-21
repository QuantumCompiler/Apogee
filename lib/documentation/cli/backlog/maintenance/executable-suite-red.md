# The executable suite is red: thirteen cli.* script tests fail on the dev host

**What / why.** `make test` on the dev host fails **13 of 3,593** (the user's run, 2026-10-20, on the v0.2.0 branch after its day of heavy shipping) — all thirteen in the executable-spawning `cli.*` script tier, which **does not gate CI** (the recorded 2026-09-22 decision: CI runs the source suite only; `make test` is the developer's gate) — exactly how this rot stayed silent while CI stayed green. The failures, by symptom class, messages verbatim:

1. **Stale homes and cleanup drift** — `cli.video_lifecycle`, `cli.datasets_lifecycle`: setup refused (`config already exists; pass --force to overwrite it`) and teardown leaving `rm: …/training/kits: Directory not empty` — the scripts enumerate what they remove, and the layout has since grown under them. An enumerated cleanup list is a second declaration of the layout — the very drift class the install-parity invariant exists to kill.
2. **`check --fix` failing in sandboxes** — `cli.analyze_lifecycle`, `cli.knowledge_lifecycle`, `cli.graph_lifecycle` each stop at `check --fix`.
3. **Setup sequencing** — `cli.graph_navigation_lifecycle`: `cannot open config file (run 'apogee config init' to create one)`; `cli.graph_artifacts_lifecycle`: the build's output file never written.
4. **A shipped rule the fixture predates** — `cli.code_graph_lifecycle`: `two source trees are both named 'src' -- a graph's trees need distinct directory names`.
5. **A lifecycle leak** — `cli.mlx_lifecycle`: `a driver outlived its apogee: 69255 … mlx_generate.py --model …`.
6. **No answer / missing output** — `cli.complete_opens_no_listening_socket` (`the turn produced no answer`, `out.txt` never written), `cli.serve_lifecycle` (`serve did not announce that it stopped`, `serve.err` absent), `cli.review_mr` (`the dry review failed:` with an empty error), `cli.train_lifecycle` (`gate wording:` empty, `gate.err` absent).

This item is the diagnosis **and** the green: each class reproduced in isolation to its root cause (`file:line` — a product regression, a fixture that predates a shipped rule, or a script's own rot — each named as which), then fixed at the cause, one fix per cause, never thirteen patches.

**Core constraint(s).** **Diagnosis before fixes**: a failure class's cause is established and recorded (dated, in this document as the work proceeds) before its fix lands; a signal that pattern-matches a known failure may have a different cause. **A product regression found here gets its regression pin** (the bug-becomes-a-fixture bar), not just a passing rerun. **Cleanup lists die**: a test's sandbox home is created fresh and torn down whole — never by enumerating layout rows — so the next layout growth cannot re-rot a script (class 1's root fix). **The suite's honesty stands**: the two standing skips (libass, pdftotext) stay skips; nothing is weakened to pass; CI's gating is **not** widened here (the 2026-09-22 decision stands — these remain `make test`'s tier).

**Seam + files.** `lib/src/cli/tests/scripts/sh/*.sh` (the thirteen scripts' setup/teardown and the shared idioms they should share — a common sandbox helper if diagnosis shows they each reinvented it); product seams only as diagnosis names them (class 5 points at the mlx child lifecycle; class 6 at turn/serve shutdown paths); `tests/CMakeLists.txt` untouched unless a case's wiring is the cause.

**Reference.** The user's full run log (2026-10-20) — the thirteen names and messages above; the install-parity invariant's history ("two implementations of the layout is the bug, even when both are correct") — class 1's law; the 2026-09-22 CI-tier decision — why this tier's rot was silent.

**Decisions made** (dated):
- 2026-10-20 — Specced from the user's failing run (their call: queue it as Maintenance, documents only). Causes deliberately **not** guessed here; the classes above are symptom groupings, and the diagnosis is the item's first deliverable.
- 2026-10-20 — Sandbox teardown moves to whole-directory removal as part of this item, whatever the per-class causes turn out to be.

**Open calls:**
- [default: whether the thirteen scripts gain one shared sandbox helper (setup + whole teardown) is diagnosis's call — recommended if three or more share the pattern.]

**Guardrail(s).** The suite itself: `make test` full green on the dev host is the bar; a product regression found gets its pinned reproduction in the owning layer's tests; the teardown change verified by running the tier twice back-to-back (a second run meeting a stale home was the class-1 symptom).

**Acceptance criteria:**
- [ ] All 3,593 pass locally (`make -C lib/src/cli test`), twice back-to-back, the two standing skips intact.
- [ ] Each failure class's root cause is recorded in this item's ship notes, named as regression / stale fixture / script rot, with its fix's seam.
- [ ] Any product regression carries a new pinned test; no test was weakened; CI's tier gating is unchanged.

**Scope note.** Maintenance item — claimable at any time by name, ungated. Out of scope: widening CI's gate to the executable tier (a recorded standing decision), the CI log-granularity ask ([`ci-test-log-granularity.md`](ci-test-log-granularity.md)), any new test coverage beyond the regression pins diagnosis demands.
