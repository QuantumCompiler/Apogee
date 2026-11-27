import type { BoundedTail } from '../../infrastructure/process/BoundedTail'
import { LineFramer } from '../../infrastructure/process/LineFramer'

/** How many notes are kept; the rest are counted. */
const limit = 20

/**
 * A finished run's notes (GUI M22): the Mac app's `JobNotes`
 * (`BusinessJobs`, `JobNotes.swift`) -- what the CLI said a person must know
 * about a run that still exited 0, on stderr, in its house voice (`apogee
 * embed: could not register 'my notes' in config -- …`), kept word for word
 * and in order, the first 20, the rest counted. A note is told by its shape
 * alone: `apogee`, then one or more lowercase words, then `: `. Never the
 * verb-less `apogee: …` line, never the `[graph] …` narration.
 */
export class JobNotes {
  static readonly limit = limit
  /** None. */
  static readonly none = new JobNotes([], 0)

  private constructor(
    /** The notes, the first `limit`, in order, as printed. */
    readonly lines: readonly string[],
    /** How many more the run said past them. */
    readonly more: number,
  ) {}

  /** The notes among `lines`: every line of a run's stderr, as the supervisor framed them. */
  static fromLines(lines: readonly string[]): JobNotes {
    const notes = lines.filter((line) => JobNotes.isNote(line))
    return notes.length === 0 ? JobNotes.none : new JobNotes(notes.slice(0, limit), Math.max(0, notes.length - limit))
  }

  /** The notes in a run's stderr tail -- a child that framed no lines: a cut tail's first line is skipped, a fragment never a note. */
  static inTail(tail: BoundedTail): JobNotes {
    const lines = LineFramer.lines(tail.text)
    return JobNotes.fromLines(tail.cut ? lines.slice(1) : lines)
  }

  /** Whether `line` is a note: `apogee`, then one or more lowercase words (a letter, then letters, digits, `-` or `_`), each after one space, then `: `. */
  static isNote(line: string): boolean {
    return /^apogee( [a-z][a-z0-9_-]*)+: /.test(line)
  }

  /** Whether the run said none. */
  get isEmpty(): boolean {
    return this.lines.length === 0
  }

  /** What a card shows: every note kept, and -- past the limit -- one line counting the rest, in the app's words. */
  get shown(): readonly string[] {
    return this.more === 0 ? this.lines : [...this.lines, `… and ${this.more} more note${this.more === 1 ? '' : 's'}`]
  }
}
