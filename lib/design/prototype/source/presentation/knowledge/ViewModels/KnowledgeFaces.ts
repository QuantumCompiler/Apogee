/**
 * What the Knowledge place's views draw (design 1o): every value a string,
 * a number, a flag or a list, each absence said (`''`, `shown: false`), so
 * the per-preset walk finds nothing unread.
 */

/** The Knowledge place's two tabs, as the old header's segmented choice names them. */
export type KnowledgeTab = 'records' | 'collections'

/** One record in the list: its decision, its score when a question was asked, and its branch, discipline and day. */
export interface RecordRowFace {
  readonly id: string
  readonly decision: string
  readonly score: string
  readonly meta: string
  readonly current: boolean
}

/** A record's raw conversation: offered where it has an archive, read by `knowledge info --raw`. */
export interface RawFace {
  readonly shown: boolean
  readonly open: boolean
  readonly text: string
  /** The Mac's words for an archive the CLI cannot read. */
  readonly note: string
  readonly failure: string
  /** The command Show runs, its button's title. */
  readonly command: string
}

/** The record open in the pane. */
export interface RecordFace {
  readonly shown: boolean
  readonly id: string
  readonly decision: string
  readonly facts: ReadonlyArray<{ readonly label: string; readonly value: string }>
  readonly raw: RawFace
}

/** The Records tab. */
export interface RecordsFace {
  /** The question field's text. */
  readonly question: string
  /** The branch picked; `''` every branch. */
  readonly branch: string
  /** What a question found and how, `3 found · lexical`; `''` with none asked. */
  readonly found: string
  /** Why the question was not answered, in the CLI's words; `''`. */
  readonly failure: string
  readonly rows: readonly RecordRowFace[]
  /** Why the list is empty, said; `''` when it is not. */
  readonly empty: string
  readonly record: RecordFace
  /** What the pane says with no record open; `''` when one is. */
  readonly noSelection: string
  /** A capture running (GUI M22). */
  readonly run: RunFace
  /** The last capture's notes (GUI M22). */
  readonly notes: NotesFace
}

/** What the CLI said of a finished run on stderr, on the said-card (GUI M22): the Mac's `doneNotes`, each line as printed. */
export interface NotesFace {
  readonly shown: boolean
  readonly title: string
  readonly lines: readonly string[]
}

/** A run going now: its busy line or what it said last, and Stop (the Mac's `RunLine`). */
export interface RunFace {
  readonly shown: boolean
  readonly text: string
}

/** One collection in the list. */
export interface CollectionRowFace {
  readonly name: string
  readonly meta: string
  readonly current: boolean
}

/** A collection's page, from its `embed info`. */
export interface CollectionPageFace {
  readonly shown: boolean
  readonly name: string
  readonly chunks: string
  readonly index: string
  readonly vectors: string
  readonly question: string
  readonly searched: boolean
  /** `embed query`'s lines, as printed. */
  readonly results: readonly string[]
  readonly failure: string
  readonly sourceCount: number
  readonly sources: readonly string[]
}

/** The Collections tab. */
export interface CollectionsFace {
  readonly rows: readonly CollectionRowFace[]
  /** Why there is no list, said; `''`. */
  readonly note: string
  readonly page: CollectionPageFace
  /** An ingest running (GUI M22). */
  readonly run: RunFace
  /** The last ingest's notes, shown on its collection's page while that page is the one shown (GUI M22). */
  readonly notes: NotesFace
}
