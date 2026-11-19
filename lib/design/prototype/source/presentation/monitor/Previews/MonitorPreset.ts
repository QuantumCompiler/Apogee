import suitesSystem from '@recorded/business/reads/Fixtures/suites-system.json?raw'
import type { ClockSettings } from '../../../infrastructure/clock/Clock'
import type { Preset } from '../../../infrastructure/process/Preset'
import type { ApogeeMode, Rule } from '../../../infrastructure/process/StandInChild'

/**
 * The `monitor` start preset (design 1e), on the CLI's own recording: the
 * real binary's `system` document, `suites-system.json`, read in place (1d's
 * decision). The old preset (`applyStart('monitor')`, `main.html` at
 * `b375fac9`) opens the monitor over Sessions with the system strip open;
 * which screen that is, is the composition's (`app`).
 */

/** How a start preset is asked for: whether it stands still, and the `apogee` mode. */
export interface PresetOptions {
  readonly still: boolean
  readonly apogee: ApogeeMode
}

/**
 * What `apogee system` answers, one a call, the last repeating: a short run
 * of readings for the history bars -- the recorded document each time; no
 * other reading was recorded, and none is made up -- with one read that ran
 * past its time for the stale state, which the next reading clears.
 */
export const monitorReads: readonly Rule[] = [
  {
    arguments: ['system'],
    answer: [{ stdout: suitesSystem }, { stdout: suitesSystem }, { timedOut: true }, { stdout: suitesSystem }],
  },
]

/**
 * The clock: 15 start ticks, so a still frame has read four times (at once,
 * then every 5 ticks) and its history shows the run, after the failed read.
 */
export const monitorClock: ClockSettings = { start: 15 }

/** The `monitor` preset. */
export function monitorPreset({ still, apogee }: PresetOptions): Preset {
  return { rules: { reads: monitorReads }, route: 'monitor', clock: { ...monitorClock, still }, apogee }
}
