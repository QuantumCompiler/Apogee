/**
 * What the Code graph place's views draw (design 1o): every value a string,
 * a number, a flag or a list, each absence said (`''`, `shown: false`), so
 * the per-preset walk finds nothing unread.
 */

/** The old prototype's three views: the code graph's Functions and Files, and the collection's prose graph. */
export type GraphView = 'functions' | 'files' | 'prose'

/** A format `graph export` writes, or the report `graph report --out` saves: the Mac's `ExportFormat`, and its report. */
export type SaveKind = 'html' | 'graphml' | 'mermaid' | 'report'

/** One row of the file tree: a folder, or a file of the graph. */
export interface TreeRowFace {
  readonly label: string
  readonly indent: number
  /** The file's path in the graph; `''` for a folder. */
  readonly path: string
  readonly current: boolean
}

/** One card on the canvas. */
export interface NodeFace {
  readonly name: string
  readonly label: string
  readonly where: string
  readonly x: number
  readonly y: number
  readonly current: boolean
  /** Joined to the selected card by a drawn edge: drawn at full strength. */
  readonly near: boolean
  /** Neither selected nor near while one is: drawn faded. */
  readonly faded: boolean
}

/** One edge, as the old canvas draws it; `hot` when it touches the selected card. */
export interface EdgeFace {
  readonly x1: number
  readonly y1: number
  readonly x2: number
  readonly y2: number
  readonly hot: boolean
}

/** A neighbour in the selected node's pane, which walks to it. */
export interface NeighbourFace {
  readonly name: string
  readonly label: string
}

/** The selected node's pane. */
export interface CardFace {
  readonly shown: boolean
  readonly kind: string
  readonly label: string
  readonly where: string
  readonly sig: string
  /** `CALLED BY` and `CALLS`, or `IMPORTED BY` and `IMPORTS` on Files. */
  readonly inLabel: string
  readonly outLabel: string
  readonly callers: readonly NeighbourFace[]
  readonly callees: readonly NeighbourFace[]
}

/** The code graph's view: the tree, the canvas and the pane. */
export interface CodeFace {
  readonly shown: boolean
  readonly tree: readonly TreeRowFace[]
  readonly nodes: readonly NodeFace[]
  readonly edges: readonly EdgeFace[]
  readonly card: CardFace
}

/** A decision attached to the prose graph, and its id, branch and discipline. */
export interface DecisionFace {
  readonly text: string
  readonly meta: string
}

/** The prose graph's busiest entity, explained. */
export interface EntityFace {
  readonly shown: boolean
  readonly kind: string
  readonly name: string
  readonly description: string
  readonly degree: string
  readonly communities: readonly string[]
  readonly decisions: readonly DecisionFace[]
  readonly relations: ReadonlyArray<{ readonly heading: string; readonly names: readonly string[] }>
}

/** The collection's prose graph: its report, and its busiest entity explained. */
export interface ProseFace {
  readonly shown: boolean
  readonly summary: string
  readonly communitiesTotal: number
  readonly communities: ReadonlyArray<{ readonly summary: string; readonly members: readonly string[] }>
  readonly decisionsTotal: number
  readonly decisions: readonly DecisionFace[]
  readonly hubs: ReadonlyArray<{ readonly name: string; readonly meta: string }>
  readonly entity: EntityFace
}

/** One item of the More menu, or the rule between them. */
export interface MoreItemFace {
  readonly label: string
  readonly rule: boolean
  readonly destructive: boolean
  readonly act: 'html' | 'graphml' | 'mermaid' | 'report' | 'dedupe' | 'delete' | ''
}

/** The place. */
/** What the CLI said of a finished run on stderr, on the said-card (GUI M22): the Mac's `doneNotes`, each line as printed. */
export interface NotesFace {
  readonly shown: boolean
  readonly title: string
  readonly lines: readonly string[]
}

export interface GraphFace {
  /** The header's line: the graph's counts, as the old header says them; `''` with none read. */
  readonly header: { readonly line: string }
  /** The last run's notes, for the graph shown (GUI M22). */
  readonly notes: NotesFace
  readonly views: ReadonlyArray<{ readonly view: GraphView; readonly label: string; readonly current: boolean }>
  readonly rebuild: { readonly enabled: boolean }
  readonly findCommunities: boolean
  readonly more: { readonly open: boolean; readonly enabled: boolean; readonly items: readonly MoreItemFace[] }
  /** The graph not there -- deleted, never read, no apogee -- said where the canvas stands. */
  readonly gone: { readonly shown: boolean; readonly text: string }
  readonly code: CodeFace
  readonly prose: ProseFace
}

/** The save panel: the system's, offering the CLI's own file name, in Documents. */
export interface SavePanelFace {
  readonly shown: boolean
  readonly file: string
  readonly place: string
  readonly command: string
  readonly canSave: boolean
}

/** The dedupe sheet: its dry run first, Merge only when it names a group, the merge supervised. */
export interface DedupeSheetFace {
  readonly shown: boolean
  readonly title: string
  readonly intro: string
  /** What Merge does, said under the dry run when it offers one; `''`. */
  readonly note: string
  /** What the CLI printed: the dry run, or the merge. */
  readonly printed: string
  /** What runs now: the dry run's command, or `merging`; `''`. */
  readonly busy: string
  readonly failure: string
  /** Cancel and Merge them offered. */
  readonly merges: boolean
  /** Done offered and on. */
  readonly done: boolean
}

/** A confirm's words: Delete the graph, or a node's explain refused. */
export interface GraphConfirmWords {
  readonly title: string
  readonly body: string
  /** `''` for an alert with OK alone. */
  readonly cancel: string
  readonly confirm: string
  readonly destructive: boolean
}

/** The confirms the place asks through the shell's host. */
export type GraphConfirm = 'graphDelete' | 'explainRefused'
