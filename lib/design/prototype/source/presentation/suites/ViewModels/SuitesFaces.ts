/** One suite in the list: its name, whether it is the default, and the CLI's verdict in its few words. */
export interface SuiteRowFace {
  readonly name: string
  readonly isDefault: boolean
  readonly summary: string
  /** The verdict says it does not fit: the summary in the error color. */
  readonly over: boolean
  readonly current: boolean
}

/** One member as the CLI priced it: its roles, its backend, the window the suite pins, what it holds. */
export interface SuiteMemberFace {
  readonly role: string
  readonly backend: string
  readonly window: string
  readonly holds: string
}

/** The chosen suite's page, as the old `sui` draws it. */
export interface SuiteDetailFace {
  readonly shown: boolean
  readonly name: string
  /** The description the CLI states for the suite (`duo   (two)`); empty when it states none, and then not drawn. */
  readonly desc: string
  readonly defaultLabel: string
  /** The CLI's total, or why there is none. */
  readonly total: string
  /** How the total is colored: it fits (the accent), it does not (the error), or neither. */
  readonly tone: 'fits' | 'over' | 'plain'
  readonly members: readonly SuiteMemberFace[]
  /** Lines of `config get suites.<name>` that read as no role, kept as said. */
  readonly unread: readonly string[]
  readonly footer: string
  readonly openLabel: string
  readonly openHelp: string
  readonly switchLabel: string
  readonly switchHelp: string
  readonly canSwitch: boolean
}

/** Everything the Suites place draws. */
export interface SuitesFace {
  readonly list: readonly SuiteRowFace[]
  /** The list's line when it has no row: the CLI's refusal of its read; empty when there is nothing to say. */
  readonly empty: string
  readonly detail: SuiteDetailFace
}
