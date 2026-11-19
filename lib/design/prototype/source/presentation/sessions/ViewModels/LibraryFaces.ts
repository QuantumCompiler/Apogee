import type { ChatSummary } from '../../../business/reads/Chats'
import type { Field, ModelKind } from '../../../business/reads/Models'
import type { ChatGroupTitle } from './ChatGroup'

/*
 * The Sessions place's states and faces (design 1g): the Mac app's
 * `SessionLibraryViewModel.Listing`, `Pending` and `ChatInfo`, and what the
 * list, the door, the rename sheet and the confirms draw, as words. Apart
 * from `SessionLibraryViewModel.ts` so that file stays under the document's
 * 800 lines, the Mac's names kept.
 */

/** Where the list stands: the Mac app's `SessionLibraryViewModel.Listing`. */
export type Listing = { readonly case: 'notRead' } | { readonly case: 'reading' } | { readonly case: 'read' } | { readonly case: 'failed'; readonly message: string } | { readonly case: 'missing' }

/** A change that waits on the person's word: the Mac app's `Pending`, the old prototype's `deleteChat`, `compactChat` and `forget` confirms. */
export type Pending = { readonly case: 'delete'; readonly chat: ChatSummary } | { readonly case: 'compact'; readonly chat: ChatSummary } | { readonly case: 'forgetRecall' }

/** The confirms the library asks through the shell's host, by kind. */
export type LibraryConfirm = 'deleteChat' | 'compactChat' | 'forget'

/** The sheets the library opens in the shell's host, by kind: the details, the rename, the door, and the session's suite chooser. */
export type LibrarySheet = 'chatInfo' | 'rename' | 'execute' | 'suite'

/** A row menu's acts: the old prototype's four, Compact… giving way to Stop compacting while a compaction of it runs. */
export type RowAct = 'details' | 'rename' | 'compact' | 'stopCompacting' | 'delete'

/** A saved conversation's details, for the sheet that shows them: `chats info`'s typed facts, or why they could not be read. */
export interface ChatInfo {
  readonly chat: ChatSummary
  /** The sheet's title: the chat's name. */
  readonly title: string
  readonly fields: readonly Field[]
  readonly failure: string
  /** The command it read, as the old sheet names it. */
  readonly command: string
}

/** One row of the list, as words. */
export interface ChatRowFace {
  readonly id: string
  readonly title: string
  /** When it last changed, as the list shows it; empty while it is busy. */
  readonly time: string
  /** A compaction of it runs: its date gives way to a busy mark. */
  readonly busy: boolean
  readonly current: boolean
  readonly kind: ModelKind | 'unknown'
  /** Its tooltip: its name and backend, or the compaction's command. */
  readonly help: string
  readonly menuOpen: boolean
  readonly items: ReadonlyArray<{ readonly label: string; readonly act: RowAct; readonly destructive: boolean }>
}

/** Everything the list draws, as words (absence said explicitly). */
export interface ListFace {
  /** The SESSIONS heading's fold. */
  readonly open: boolean
  readonly count: number
  readonly anyDateOpen: boolean
  readonly datesLabel: string
  readonly query: string
  /** The private session open now, in no saved list; current, as the open session, only while Sessions is shown. */
  readonly privateRow: { readonly shown: boolean; readonly current: boolean }
  readonly groups: ReadonlyArray<{ readonly title: ChatGroupTitle; readonly count: number; readonly open: boolean; readonly hasCurrent: boolean; readonly rows: readonly ChatRowFace[] }>
  readonly noResults: string
  readonly matchText: string
  /** What the list says when it has no rows for a reason other than a search; empty otherwise. */
  readonly empty: string
  readonly searchRequests: number
}

/** The execute door's sheet, as words. */
export interface DoorFace {
  readonly suites: ReadonlyArray<{ readonly value: string; readonly label: string }>
  readonly suite: string
  readonly models: ReadonlyArray<{ readonly value: string; readonly label: string }>
  readonly model: string
  /** The folder, or `Your home folder`. */
  readonly folder: string
  readonly orchestrate: boolean
  readonly canOrchestrate: boolean
  readonly note: string
  readonly command: string
}

/** The rename sheet's field. */
export interface Renaming {
  readonly chat: ChatSummary
  readonly name: string
  readonly canRename: boolean
  readonly command: string
}

/** What a confirm says, by kind. */
export interface ConfirmWords {
  readonly title: string
  readonly body: string
  readonly confirm: string
  readonly cancel: string
}
