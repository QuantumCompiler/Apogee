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

/** One member of a suite as `config get suites.<name>` prints it: the role, its backend, and the window the suite pins on it. */
export interface SuiteRole {
  readonly role: string
  readonly backend: string
  /** The window the suite pins, when it pins one. */
  readonly window: number | undefined
}

/**
 * A suite as `config get suites.<name>` prints it (design 1p): one member a
 * role, `chat: root` or `utility: helper (window 4096)`; a line that does not
 * read as a role and its backend kept as said. The Mac app's `Suite`
 * (`System.swift`).
 */
export interface Suite {
  readonly name: string
  readonly members: readonly SuiteRole[]
  readonly unread: readonly string[]
}

/** The roles a suite speaks for. */
const suiteRoles: readonly string[] = ['chat', 'embedding', 'extraction', 'vision', 'transcription', 'utility']

export const Suite = {
  read(name: string, text: string): Suite {
    const members: SuiteRole[] = []
    const unread: string[] = []
    for (const raw of text.split(/\r?\n/)) {
      const line = raw.trim()
      if (line === '') continue
      const colon = line.indexOf(':')
      const role = colon < 0 ? '' : line.slice(0, colon)
      const rest = colon < 0 ? '' : line.slice(colon + 1).trim()
      const backend = rest.split(' ')[0] ?? ''
      if (!suiteRoles.includes(role) || backend === '') {
        unread.push(line)
        continue
      }
      const pinned = /\(window (\d+)/.exec(rest)
      members.push({ role, backend, window: pinned ? Number(pinned[1]) : undefined })
    }
    return { name, members, unread }
  },

  /** The window the suite pins on `backend`, in the place's words: `4096 pinned`, or `its own`. */
  windowOf(suite: Suite | undefined, backend: string): string {
    const window = suite?.members.find((member) => member.backend === backend)?.window
    return window === undefined ? 'its own' : `${window} pinned`
  },
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
