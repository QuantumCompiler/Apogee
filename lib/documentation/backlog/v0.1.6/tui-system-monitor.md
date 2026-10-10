# The system monitor: the machine on the bottom bar

**What / why.** The user's ask (2026-10-04): whatever the TUI's interface looks like, the view's **bottom bar carries a system monitor**. This item is the composition of two siblings that already exist in this table: [32a](../../assistant/MILESTONES.md#milestone-ag--system-insight) built the one honest machine read (`MachineSnapshot` — CPU, memory, GPU, disk, every unanswerable field an explicit unknown), and [32b](../../assistant/MILESTONES.md#milestone-ah--the-full-screen-tui) built the stage with its top-level frame. The monitor is a **persistent bottom-bar component** in the shell's frame, present across every view ([32c](tui-session-view.md)'s session, [32d](tui-workbench-views.md)'s workbench): a compact, live strip — CPU utilization and load, memory used/total, Apogee's own footprint with its resident-model count, and the GPU story where the platform can tell one — refreshed on a slow tick while the shell runs. The refresh is **the shell's tick, not a change to 32a's contract**: `apogee system` stays a one-shot command with no watch loop (its recorded scope), and the TUI samples the same probe on a timer that lives and dies with the shell's event pump — in-process, no daemon, nothing persisted. The bar renders 32a's honesty as-is: an `unknown` field is dimmed or absent by design, never guessed, and a sample that fails leaves the previous values standing with a stale marker rather than flickering or lying.

**Core constraint(s).**
- **One probe, no second estimator** (32a's rule, consumed): the bar calls the shared `system_info` read — the same function `apogee system` and the 26a sizing use. A TUI-side sampler computing its own numbers is the drift this table's 32a exists to prevent.
- **The tick is bounded and invisible:** a slow cadence (seconds, not frames), sampled off the render path so a slow OS call can never stall a streaming turn's repaint; the monitor's cost must be unmeasurable next to a running model. While a turn streams, the tick keeps its cadence — it never bursts.
- **Honest rendering carries:** unknown fields render dim/absent; a failed sample marks the strip stale with its age rather than blanking or repeating silently; units and precision match `apogee system`'s table so the two surfaces never disagree about the same machine.
- **The bar is the shell's one strip:** it joins 32b's frame as a component, under the one-screen-owner rule — no second painter, no separate refresh loop fighting the compositor.
- **Zero model loads, zero children, never-listens untouched** — inherited from 32a's probe by construction; the tick adds no new capability, only repetition.
- Code style carries: `.h`/`.cpp` pairs, smart pointers only; tests in the layer of what they test ([ADR](../../adrs/cli/tests-mirror-architecture.md)).

**Seam + files.**
- `presentation/tui/monitor_bar.h/.cpp` (new): the component — snapshot → strip rendering (compact formatting, unknown dimming, the stale marker), registered into 32b's frame as the bottom bar.
- `presentation/tui/pump.h/.cpp` (32b's): the timer tick — one slow repeating event through the existing pump seam, the sample taken off the render thread and handed to the bar as data.
- `infrastructure/platform/system_info` (32a's): consumed as-is; if the bar needs a cheaper partial read (utilization and memory only, skipping disk), that variant is carved *in 32a's seam* as a second entry over the same sources — never a TUI-side copy.
- Tests: `tests/presentation/tui/` — strip goldens over planted snapshots (full, partial-unknown, all-unknown, stale); the tick's discipline over a scripted clock (cadence held during a streaming turn, no burst after a stall); the off-render-path assertion (a blocked probe never delays a frame, via the seam's fake).
- Consumes: [32a](../../assistant/MILESTONES.md#milestone-ag--system-insight) (the probe, the honesty idiom, the units), [32b](../../assistant/MILESTONES.md#milestone-ah--the-full-screen-tui) (the frame, the pump, the one-owner rule); interplay with [32c](tui-session-view.md)/[32d](tui-workbench-views.md) (the bar persists across their views — theirs to not cover, not to implement).

**Reference.** In-house: M1's busy-line discipline (one painter, silent cost — the spirit this bar inherits at component scale) and 32a's unknown-never-guessed idiom. External: the status bars of terminal multiplexers (tmux's status line) as the product shape — persistent, compact, slow-ticking.

**Decisions made** (dated):
- 2026-10-04 — Asked for by the user ("a system monitor component in the view for the bottom bar"); placed in **v0.1.6** (their call), 32e by the release-prefix rule, gated on the two items it composes.
- 2026-10-04 — The refresh is the shell's tick, not 32a's: the command keeps its recorded no-watch-loop scope; the TUI's timer lives in the pump and dies with the shell. One probe, two consumers, two lifetimes.
- 2026-10-04 — Stale-over-silent: a failed sample keeps the last values with an age marker — a monitor that blanks on every hiccup trains the user to ignore it, and one that silently repeats is lying.

**Open calls:**
- [default: the cadence is 2 seconds, a named constant; config exposure waits for someone to want it — a knob nobody asked for is surface without a user]
- [default: the strip shows CPU %, load, memory used/total, Apogee's resident-model count and footprint; GPU joins only where 32a has a real story (Apple silicon) — disk stays off the bar, it changes too slowly to monitor]

**Guardrail(s).**
- Strip goldens: every snapshot shape (full / partial / all-unknown / stale) renders deterministically; unknown fields dimmed, never fabricated; units equal to `apogee system`'s for the same values.
- The cadence pin: over a scripted clock, ticks hold their interval during a simulated streaming turn and never burst to catch up after a stall.
- The isolation pin: with the probe seam blocked, frames keep painting and the bar goes stale with its age — asserted via the fake.
- The one-probe pin: the bar's data path reaches the `system_info` symbol 32a tests pin (call-counted through the seam) — no second reader of `/proc` or `sysctl` anywhere in `tui/`.

**Acceptance criteria:**
- [ ] The bottom bar is present in the session view and every workbench view, showing live CPU, memory and Apogee-resident values that match `apogee system`'s output for the same moment (units and precision equal).
- [ ] On a platform where GPU is unknown, the bar simply omits it — dimmed or absent, exactly as 32a reports it; nothing is invented.
- [ ] With the probe artificially stalled, the shell stays fully responsive and the bar shows its stale age; recovery resumes the cadence without a burst.
- [ ] The tick stops with the shell — no timer, thread or child survives exit (the restore-on-exit test extended one assertion).

**Scope note.** Item **32e**, earmarked for **v0.1.6**; **gated on nothing pending** — 32b shipped 2026-10-10 ([Milestone AH](../../assistant/MILESTONES.md#milestone-ah--the-full-screen-tui): the shell's frame and its bottom-bar slot, `Shell::set_bottom_bar`, and the pump), and 32a shipped 2026-10-10 ([Milestone AG](../../assistant/MILESTONES.md#milestone-ag--system-insight): `platform/system_info`'s `SystemSource` and `read_machine`, `operations/system_view`'s units and view). Out of scope: any change to `apogee system`'s one-shot contract (no `--watch`; a later deliberate item if ever); per-process breakdowns and vendor GPU tooling (32a's exclusions, inherited); history/sparklines (the bar is a reading, not a chart — a chart view would be a 32d-style item under the earning rule); configurable layout of the strip.
