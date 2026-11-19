import { ReadDocument } from '../../data/protocol/ReadDocument'
import { BoundedTail } from '../../infrastructure/process/BoundedTail'
import { ChildExit } from '../../infrastructure/process/ChildExit'
import type { StandInChild } from '../../infrastructure/process/StandInChild'
import { CommandOutcome, failedSaying, timedOutSaying } from './CommandRunning'
import type { CommandRunning } from './CommandRunning'

/** A read's outcome: the Mac app's `ReadOutcome` (`ReadRunner.swift:90-98`), its five cases. */
export type ReadOutcome =
  | { readonly case: 'document'; readonly document: ReadDocument }
  /** The read exited badly: the stderr tail is the CLI's reason. */
  | { readonly case: 'failed'; readonly exit: ChildExit; readonly stderrTail: BoundedTail }
  /** It printed something that is not one JSON document (the first 4 KiB of it). */
  | { readonly case: 'undecodable'; readonly stdout: string; readonly stderrTail: BoundedTail }
  /** `after` is in milliseconds. */
  | { readonly case: 'timedOut'; readonly after: number; readonly stderrTail: BoundedTail }
  | { readonly case: 'couldNotStart'; readonly reason: string }

export const ReadOutcome = {
  document: (document: ReadDocument): ReadOutcome => ({ case: 'document', document }),
  failed: (exit: ChildExit, stderrTail: BoundedTail): ReadOutcome => ({ case: 'failed', exit, stderrTail }),
  undecodable: (stdout: string, stderrTail: BoundedTail): ReadOutcome => ({ case: 'undecodable', stdout, stderrTail }),
  timedOut: (after: number, stderrTail: BoundedTail): ReadOutcome => ({ case: 'timedOut', after, stderrTail }),
  couldNotStart: (reason: string): ReadOutcome => ({ case: 'couldNotStart', reason }),

  /** Why there is no document, in the CLI's words where it gave some; `undefined` for a document. */
  failure(outcome: ReadOutcome): string | undefined {
    switch (outcome.case) {
      case 'document':
        return undefined
      case 'failed':
        return failedSaying(outcome.exit, outcome.stderrTail)
      case 'undecodable':
        return 'apogee printed something that is not one JSON document'
      case 'timedOut':
        return timedOutSaying(outcome.after, outcome.stderrTail)
      case 'couldNotStart':
        return outcome.reason
    }
  },

  /** The document, when there is one. */
  documentOf(outcome: ReadOutcome): ReadDocument | undefined {
    return outcome.case === 'document' ? outcome.document : undefined
  },
}

/**
 * A read's outcome and what its child said on stderr, kept whatever the exit:
 * the Mac app's `ReadWithStderr`.
 */
export class ReadWithStderr {
  constructor(
    readonly outcome: ReadOutcome,
    readonly stderrTail: BoundedTail,
  ) {}

  /** What stderr said, line by line: each with anything on it, trimmed, in the order written. */
  get stderrLines(): string[] {
    return this.stderrTail.text
      .split(/\r\n|\r|\n/)
      .map((line) => line.trim())
      .filter((line) => line !== '')
  }
}

/**
 * Runs one read and hands back its document, or why there is none, in the
 * CLI's own words: the Mac app's `ReadRunner` (`BusinessReads`,
 * `ReadRunner.swift`), turning what a child printed into outcomes as Swift's
 * does. Its child is the stand-in (design 1d), so nothing is spawned: a
 * read's rule is matched on its arguments as the caller gives them, before
 * `--output-format json`, as `PreviewRunner` matches them.
 */
export class ReadRunner implements CommandRunning {
  constructor(
    readonly child: StandInChild,
    /** Milliseconds, as the Mac app's 60-second default. */
    readonly timeout = 60_000,
  ) {}

  async read(binary: string, args: readonly string[]): Promise<ReadOutcome> {
    return (await this.readKeepingStderr(binary, args)).outcome
  }

  async readKeepingStderr(_binary: string, args: readonly string[]): Promise<ReadWithStderr> {
    const answer = this.child.run('reads', args)
    if (!answer.started) return new ReadWithStderr(ReadOutcome.couldNotStart(answer.reason), new BoundedTail())
    const run = answer.run
    let outcome: ReadOutcome
    const document = run.stdoutTruncated ? undefined : ReadDocument.from(run.stdout)
    if (run.timedOut) outcome = ReadOutcome.timedOut(this.timeout, run.stderrTail)
    else if (!ChildExit.succeeded(run.exit)) outcome = ReadOutcome.failed(run.exit, run.stderrTail)
    else if (document) outcome = ReadOutcome.document(document)
    else outcome = ReadOutcome.undecodable(run.stdout.slice(0, 4096), run.stderrTail)
    return new ReadWithStderr(outcome, run.stderrTail)
  }

  /** A read whose document stands whatever its exit. */
  async report(_binary: string, args: readonly string[]): Promise<ReadOutcome> {
    const answer = this.child.run('reads', args)
    if (!answer.started) return ReadOutcome.couldNotStart(answer.reason)
    const run = answer.run
    const document = ReadDocument.from(run.stdout)
    if (document) return ReadOutcome.document(document)
    if (run.timedOut) return ReadOutcome.timedOut(this.timeout, run.stderrTail)
    return ChildExit.succeeded(run.exit)
      ? ReadOutcome.undecodable(run.stdout.slice(0, 4096), run.stderrTail)
      : ReadOutcome.failed(run.exit, run.stderrTail)
  }

  async command(_binary: string, args: readonly string[], input?: string): Promise<CommandOutcome> {
    const answer = this.child.run('commands', args, input)
    if (!answer.started) return CommandOutcome.couldNotStart(answer.reason)
    const run = answer.run
    if (run.timedOut) return CommandOutcome.timedOut(this.timeout, run.stderrTail)
    if (!ChildExit.succeeded(run.exit)) return CommandOutcome.failed(run.exit, run.stderrTail)
    return CommandOutcome.done(run.stdout)
  }
}
