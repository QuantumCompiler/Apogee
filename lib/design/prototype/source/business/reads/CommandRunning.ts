import type { BoundedTail } from '../../infrastructure/process/BoundedTail'
import { ChildExit } from '../../infrastructure/process/ChildExit'
import type { ReadOutcome, ReadWithStderr } from './ReadRunner'

/**
 * What a screen runs through: a read, and a command that changes something.
 * The Mac app's `CommandRunning` (`BusinessReads`, `CommandRunning.swift:7-22`),
 * its five methods: `read`, `command`, `command` with input (one method here,
 * its input optional), `report` and `readKeepingStderr`. `ReadRunner` is the
 * prototype's, over the stand-in child; a later port swaps a real CLI in
 * under the same interface. `binary` is where the binary was found.
 */
export interface CommandRunning {
  /** `apogee <args> --output-format json`, its document decoded. */
  read(binary: string, args: readonly string[]): Promise<ReadOutcome>
  /** `apogee <args>`, exactly as a person would type it; `input` written to its stdin -- a secret goes this way, never on a command line. */
  command(binary: string, args: readonly string[], input?: string): Promise<CommandOutcome>
  /** A read whose document stands whatever its exit: `check` exits non-zero when a row fails, and its document says which. */
  report(binary: string, args: readonly string[]): Promise<ReadOutcome>
  /** A read, with the stderr it left beside its document. */
  readKeepingStderr(binary: string, args: readonly string[]): Promise<ReadWithStderr>
}

/** How a command that changes something ended: the Mac app's `CommandOutcome` (`CommandRunning.swift:90-96`). */
export type CommandOutcome =
  /** It did what it said; `stdout` is what it printed for a person. */
  | { readonly case: 'done'; readonly stdout: string }
  /** It refused or failed: the stderr tail is the CLI's reason. */
  | { readonly case: 'failed'; readonly exit: ChildExit; readonly stderrTail: BoundedTail }
  | { readonly case: 'timedOut'; readonly after: number; readonly stderrTail: BoundedTail }
  | { readonly case: 'couldNotStart'; readonly reason: string }

export const CommandOutcome = {
  done: (stdout: string): CommandOutcome => ({ case: 'done', stdout }),
  failed: (exit: ChildExit, stderrTail: BoundedTail): CommandOutcome => ({ case: 'failed', exit, stderrTail }),
  /** `after` is in milliseconds. */
  timedOut: (after: number, stderrTail: BoundedTail): CommandOutcome => ({ case: 'timedOut', after, stderrTail }),
  couldNotStart: (reason: string): CommandOutcome => ({ case: 'couldNotStart', reason }),

  succeeded(outcome: CommandOutcome): boolean {
    return outcome.case === 'done'
  },

  /** Why it did not succeed, as the design's failed state says it: the stderr's last line, word for word, and how it exited. */
  failure(outcome: CommandOutcome): string | undefined {
    switch (outcome.case) {
      case 'done':
        return undefined
      case 'failed':
        return failedSaying(outcome.exit, outcome.stderrTail)
      case 'timedOut':
        return timedOutSaying(outcome.after, outcome.stderrTail)
      case 'couldNotStart':
        return outcome.reason
    }
  },
}

/** `<stderr's last line> (exited with status 1)`. */
export function failedSaying(exit: ChildExit, tail: BoundedTail): string {
  return `${tail.lastLine ?? 'apogee said nothing'} (${ChildExit.description(exit)})`
}

/** `<stderr's last line> -- it did not finish within 60.0 seconds`, as Swift's `Duration` says it. */
export function timedOutSaying(after: number, tail: BoundedTail): string {
  const seconds = after / 1000
  const said = Number.isInteger(seconds) ? `${seconds}.0` : `${seconds}`
  return `${tail.lastLine === undefined ? '' : `${tail.lastLine} -- `}it did not finish within ${said} seconds`
}
