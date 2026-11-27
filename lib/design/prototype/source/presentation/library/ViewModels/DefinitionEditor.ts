import { Observable } from '../../styles/Observation/Observable'

/** Why closing the editor now would lose something, so it asks first (GUI 6f): typing not yet saved, or a refused text in the file. */
export type CloseReason = 'unsaved' | 'refusedOnDisk'

/**
 * A definition file open in the app's editor: the Mac app's `DefinitionEditor`
 * (`LibraryViewModels.swift:11-107`) -- the file the CLI reads, its text as
 * it was when opened, as the file holds it now and as edited, and what the
 * binary said after the save. Its owner changes it and redraws.
 */
export class DefinitionEditor {
  /** The text the file holds, as far as this editor knows: read at open, moved by every write that landed -- a save or a put-back (GUI 6f). */
  onDisk: string
  /** Whether apogee refused the text the file holds now. */
  onDiskRefused = false
  /** The binary's word on the saved file -- its refusal verbatim, or that it stands -- `undefined` before a save. */
  verdict: string | undefined = undefined
  refused = false
  /** A save running now: the editor is read-only until it ends, so nothing typed meanwhile is lost (GUI 4h). */
  saving = false
  text: string

  constructor(
    readonly title: string,
    readonly path: string,
    readonly original: string,
    text?: string,
  ) {
    this.text = text ?? original
    this.onDisk = original
  }

  /** Whether the text differs from what the file held when opened: what Save keys on, and what Put it back as it was undoes. */
  get changed(): boolean {
    return this.text !== this.original
  }

  /** Whether the text differs from what the file holds now: typing that closing would lose. A save that landed clears it (GUI 6f). */
  get unsaved(): boolean {
    return this.text !== this.onDisk
  }

  /** Whether the file holds something other than what it held when opened. */
  get rewritten(): boolean {
    return this.onDisk !== this.original
  }

  /**
   * Why closing now would lose something; `undefined` when it loses nothing,
   * or while a save runs -- an editor closed mid-save stays closed, its text
   * already on its way to the file (GUI 4h). A refused text on disk is asked
   * about first: putting it back is the only way to the original.
   */
  get closeReason(): CloseReason | undefined {
    if (this.saving) return undefined
    if (this.onDiskRefused && this.rewritten) return 'refusedOnDisk'
    return this.unsaved ? 'unsaved' : undefined
  }

  /** The file's name, as the close prompt says it. */
  get fileName(): string {
    return this.path.split('/').at(-1) ?? this.path
  }

  /** The close prompt's question. */
  question(reason: CloseReason): string {
    return reason === 'unsaved' ? `Discard your changes to ${this.fileName}?` : `${this.fileName} holds the text apogee refused`
  }

  /** What the close prompt says under its question. */
  explanation(reason: CloseReason): string {
    if (reason === 'unsaved') return "What you typed isn't saved, and closing loses it."
    const said = "Put it back as it was writes the text it held when you opened it, and apogee reads it again. Leave it keeps the refused text in the file, and apogee can't play it until it is fixed."
    return this.unsaved ? `${said} What you typed since the save is lost either way.` : said
  }

  /** A write of `saved` landed: the file holds it now, not yet judged. */
  wrote(saved: string): void {
    this.onDisk = saved
    this.onDiskRefused = false
  }

  /**
   * A save of `saved` ended: written onto the editor as it stands now --
   * never onto the copy taken when Save was pressed, which would put back
   * whatever was typed since. Re-enabled with the binary's word; text that
   * moved on during the save is kept and said unsaved.
   */
  endSave(saved: string, verdict: string, refused: boolean): void {
    this.saving = false
    this.refused = refused
    if (this.onDisk === saved) this.onDiskRefused = refused
    this.verdict = this.text === saved ? verdict : `${verdict}\nWhat you typed since pressing Save isn't saved yet.`
  }
}

/** What a view-model that owns the editor speaks through: the shell's sheet and confirm hosts, and the toast, as closures the composition fills. */
export interface LibraryDoors {
  say?: (text: string) => void
  present?: (sheet: string) => void
  dismissSheet?: () => void
  ask?: (confirm: string) => void
}

/** The ticks a view-model waits on: the old prototype's clock. */
export interface Ticks {
  onTick(listener: (tick: number) => void): () => void
}

/**
 * The ticks a save takes before apogee's word on the file: the old
 * prototype's rule (`main.html`'s `tick()` at `b375fac9`, `clock - gs.at >=
 * 4`). The stand-in answers a read at once, so the editor's owner waits them
 * out on the clock before it asks -- a still frame never ticks, so a save
 * made in one stands saving, as the old frame does.
 */
export const saveTicks = 4

/** Resolves after `count` ticks of `clock`. */
export function afterTicks(clock: Ticks, count: number): Promise<void> {
  return new Promise((resolve) => {
    let seen = 0
    const stop = clock.onTick(() => {
      seen += 1
      if (seen < count) return
      stop()
      resolve()
    })
  })
}

/**
 * A view-model that owns a definition editor: the agents' and the
 * symphonies'. The editor sheet is drawn over it, and closing goes through
 * it, asked first when something would be lost (GUI 6f): the Mac app's
 * `DefinitionEditing` protocol and its extension.
 */
export abstract class DefinitionEditing extends Observable {
  editor: DefinitionEditor | undefined = undefined
  /** The close prompt standing now, and why; `undefined` when none is. */
  closing: CloseReason | undefined = undefined

  constructor(protected readonly doors: LibraryDoors) {
    super()
  }

  /** Whether the editor offers Put it back as it was now. */
  abstract get offersRestore(): boolean
  abstract save(): Promise<void>

  /** Puts the file back as it was when opened, and saves that. */
  async restore(): Promise<void> {
    const editor = this.editor
    if (!editor || editor.saving) return
    editor.text = editor.original
    this.changed()
    await this.save()
  }

  /** Opens `editor`, over the shell's sheet host. */
  protected open(editor: DefinitionEditor): void {
    this.closing = undefined
    this.editor = editor
    this.changed()
    this.doors.present?.('editor')
  }

  /** What is typed: never while a save runs, which holds the editor read-only. */
  setText(text: string): void {
    if (!this.editor || this.editor.saving) return
    this.editor.text = text
    this.changed()
  }

  /** Close or Esc: closes at once when nothing would be lost (nothing typed, a save that stood, a save running), else raises the prompt and leaves the editor open. */
  requestClose(): void {
    const editor = this.editor
    if (!editor) return
    const reason = editor.closeReason
    if (reason === undefined) {
      this.closeEditor()
      return
    }
    this.closing = reason
    this.changed()
    this.doors.ask?.('editorClose')
  }

  /** Closes now, asking nothing. */
  closeEditor(): void {
    this.closing = undefined
    this.editor = undefined
    this.changed()
    this.doors.dismissSheet?.()
  }

  /** The prompt's Discard: closes, the typing gone, the file as it is. */
  discardAndClose(): void {
    this.closeEditor()
  }

  /** The prompt's Leave it: closes with the refused text in the file. */
  leaveAndClose(): void {
    this.closeEditor()
  }

  /** The prompt's Keep editing (and its Esc): the editor as it was. */
  keepEditing(): void {
    this.closing = undefined
    this.changed()
  }

  /** The prompt's Put it back as it was: writes the original, and closes once apogee has answered -- and only if the file then holds the original. */
  async restoreAndClose(): Promise<void> {
    this.closing = undefined
    const path = this.editor?.path
    if (path === undefined) return
    await this.restore()
    const editor = this.editor
    if (!editor || editor.path !== path || editor.saving || editor.rewritten) return
    this.closeEditor()
  }
}
