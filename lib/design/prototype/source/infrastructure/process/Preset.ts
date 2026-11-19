import type { ClockSettings } from '../clock/Clock'
import type { ApogeeMode, Rules } from './StandInChild'

/**
 * One start screen of the prototype, as the stand-in serves it (design 1d): the
 * old prototype's start presets, rebuilt on recordings. It sits here, under
 * the stand-in, so a place's `Previews/` can name one (a place may not import
 * `app`).
 */
export interface Preset {
  /** The reads, commands, jobs and chats it answers, each with a recording. */
  readonly rules: Rules
  /** Where the app opens: a plain string, the old prototype's `start` (`monitor`, `chat:wal`). How the page picks it is 1e's. */
  readonly route: string
  /** Its start tick and `still`, its fixed "now" and its pace. */
  readonly clock: ClockSettings
  /** Whether `apogee` is installed, missing, or refuses its config. */
  readonly apogee: ApogeeMode
}
