import type { ReadDocument } from '../../data/protocol/ReadDocument'
import { Field } from './Models'

/** How a suite's total ends, in the CLI's words (`admission_line`). */
export type SuiteVerdict = 'fits' | 'fitsAsFarAsKnown' | 'doesNotFit' | 'budgetUnknown' | 'nothingHeld' | 'unread'

/** One member: the backend, the roles it speaks for, and what it holds, in the CLI's words. */
export interface SuiteMember {
  readonly backend: string
  readonly roles: readonly string[]
  readonly said: string
}

/**
 * A suite as the CLI prices it on this machine, as `models status --suite
 * <name>` states it in its `footprint` field's text: the suite, a line per
 * member, then the total and its verdict. The Mac app's `SuiteFootprint`
 * (`BusinessReads`, `System.swift`), read from the text; the app shows it and
 * decides nothing from its numbers -- the verdict is the CLI's. (The typed
 * `footprint` object of the CLI's M31 is read once a preset records it.)
 */
export class SuiteFootprint {
  private constructor(
    readonly suite: string,
    readonly members: readonly SuiteMember[],
    /** The total in words: the line after `total: `, the sum, the budget, each member's part and the verdict. */
    readonly total: string,
    readonly verdict: SuiteVerdict,
  ) {}

  /** The footprint a `models status --suite <name>` document carries; `undefined` when it carries none. */
  static of(document: ReadDocument): SuiteFootprint | undefined {
    const text = Field.list(document).find((field) => field.label === 'footprint')?.value
    return text === undefined ? undefined : SuiteFootprint.fromText(text)
  }

  /** `suite shared` / `root (chat): …` / `total: suite shared needs …`. */
  static fromText(text: string): SuiteFootprint | undefined {
    const lines = text.split(/\r?\n/).map((line) => line.trim())
    const head = lines[0]
    const totalLine = [...lines].reverse().find((line) => line.startsWith('total: '))
    if (head === undefined || !head.startsWith('suite ') || totalLine === undefined) return undefined
    const members = lines.slice(1).flatMap((line) => {
      if (line.startsWith('total: ')) return []
      const open = line.indexOf(' (')
      const close = open < 0 ? -1 : line.indexOf('): ', open + 2)
      if (open < 0 || close < 0) return []
      return [{ backend: line.slice(0, open), roles: line.slice(open + 2, close).split(', '), said: line.slice(close + 3) }]
    })
    const total = totalLine.slice('total: '.length)
    return new SuiteFootprint(head.slice('suite '.length), members, total, SuiteFootprint.verdictOf(total))
  }

  /** The verdict a total line ends in. */
  static verdictOf(total: string): SuiteVerdict {
    if (total.endsWith(': no member holds memory on this machine')) return 'nothingHeld'
    const dash = total.lastIndexOf(' -- ')
    if (dash < 0) return 'unread'
    switch (total.slice(dash + 4)) {
      case 'fits':
        return 'fits'
      case 'fits as far as known':
        return 'fitsAsFarAsKnown'
      case 'it does not fit':
      case 'over budget, run anyway (--force)':
        return 'doesNotFit'
      case "this machine's budget is not known here":
        return 'budgetUnknown'
      default:
        return 'unread'
    }
  }

  /** The verdict in the CLI's own few words: what the total ends in. */
  get verdictWords(): string {
    if (this.verdict === 'nothingHeld') return 'no member holds memory on this machine'
    const dash = this.total.lastIndexOf(' -- ')
    return dash < 0 ? this.total : this.total.slice(dash + 4)
  }
}

/** `config get suites`: the configured suites, one name a line. */
export const Suites = {
  names(text: string): string[] {
    return text
      .split(/\r?\n/)
      .map((line) => line.trim())
      .filter((line) => line !== '')
  },
}
