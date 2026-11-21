/**
 * What the Library's views draw (design 1n), as words: every absence said
 * (`shown: false`, `''`), so the per-preset walk reads no `undefined`.
 */

/** The place's three tabs, the Mac app's and the old header's. */
export type LibraryTab = 'agents' | 'symphonies' | 'mcp'

export interface LibraryTabFace {
  readonly tab: LibraryTab
  readonly label: string
  readonly current: boolean
}

/** One row of the 300 px list. */
export interface LibraryRowFace {
  readonly name: string
  readonly meta: string
  readonly current: boolean
  /** Whether the row picks: a symphony's does; an agent's and a server's do not, as the old rows' empty `pick`. */
  readonly pickable: boolean
}

/** The agent's page: its name, Delete…, its words, its files with Edit, and the command that runs it. */
export interface AgentPageFace {
  readonly shown: boolean
  readonly name: string
  readonly canDelete: boolean
  readonly description: string
  readonly facts: string
  readonly files: readonly string[]
  readonly runs: string
}

/** A symphony's page: its stages, where it comes from, Play with its input and image, a play running, and the last play's answers. */
export interface SymphonyPageFace {
  readonly shown: boolean
  readonly name: string
  readonly canEdit: boolean
  readonly description: string
  readonly stages: readonly string[]
  readonly note: string
  readonly inputDescription: string
  readonly input: string
  readonly image: { readonly takes: boolean; readonly chosen: string; readonly types: string }
  readonly canPlay: boolean
  readonly needsImage: string
  readonly playing: { readonly shown: boolean; readonly line: string }
  readonly played: { readonly shown: boolean; readonly stages: ReadonlyArray<{ readonly label: string; readonly answer: string }>; readonly output: string }
}

/** The server's page, and the presets Apogee ships under it. */
export interface MCPPageFace {
  readonly shown: boolean
  readonly name: string
  readonly toggle: string
  readonly command: string
  readonly facts: string
  readonly presetsNote: string
  readonly presets: ReadonlyArray<{ readonly name: string; readonly text: string; readonly refused: boolean }>
}

export interface LibraryFace {
  readonly tabs: readonly LibraryTabFace[]
  /** New agent…, on the agents tab alone. */
  readonly newAgent: boolean
  readonly rows: readonly LibraryRowFace[]
  /** What the list says under its rows: no apogee, or why it could not be read. */
  readonly note: string
  /** What the page says with nothing to show: "Choose an agent." */
  readonly emptyDetail: string
  readonly agent: AgentPageFace
  readonly symphony: SymphonyPageFace
  readonly mcp: MCPPageFace
}

/** The editor sheet's content, title and buttons. */
export interface EditorFace {
  readonly shown: boolean
  readonly title: string
  readonly path: string
  readonly text: string
  readonly saving: boolean
  /** The busy line while a save waits for apogee's word; `''` otherwise. */
  readonly busy: string
  readonly verdict: { readonly text: string; readonly refused: boolean }
  readonly restore: { readonly shown: boolean; readonly enabled: boolean }
  readonly canSave: boolean
}

/** A confirm's words and buttons, for the shell's confirm host. */
export interface LibraryConfirmWords {
  readonly title: string
  readonly body: string
  readonly cancel: string
  /** A second, destructive way: "Leave it". `''` when there is none. */
  readonly alternative: string
  readonly confirm: string
  readonly destructive: boolean
}

export type LibraryConfirm = 'editorClose' | 'mcpRemove'
