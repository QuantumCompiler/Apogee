// Time and timers, the host's as processes are (the layering check holds
// $.clock to Infrastructure since 1b): what the polls and the async window of
// the layers above stand on.
import type { EngineInterface, Timer } from 'claude-code'

/** Milliseconds since the epoch, now. */
export async function now($: EngineInterface): Promise<number> {
  return $.clock.now()
}

/** Calls `fn` once after `ms`; `cancel()` before then stops it. */
export function after($: EngineInterface, ms: number, fn: () => void): Timer {
  return $.clock.after(ms, fn)
}

/** Calls `fn` every `ms` until `cancel()`, or until the module reloads. */
export function every($: EngineInterface, ms: number, fn: () => void): Timer {
  return $.clock.every(ms, fn)
}
