import { AgentEntry } from '../../../business/reads/Library'
import type { CommandRunning } from '../../../business/reads/CommandRunning'
import { ReadOutcome } from '../../../business/reads/ReadRunner'
import type { FolderWriting } from '../../../infrastructure/process/StandInFolders'
import { afterTicks, DefinitionEditing, DefinitionEditor, saveTicks } from './DefinitionEditor'
import type { LibraryDoors, Ticks } from './DefinitionEditor'
import { LibraryListing, splitPath } from './LibraryListing'

/**
 * The agent the page shows: `triage`, the person's own, by its name. The old
 * prototype draws its first row current and its canned list puts triage
 * first; the CLI lists it eighth of eight (`agents-list.json`), so the page
 * is chosen by name, and an agent row does not pick (the document's default:
 * drawing other agents' pages is new design).
 */
export const shownAgent = 'triage'

/**
 * The Library's agents (design 1n): the Mac app's `AgentsViewModel`
 * (`LibraryViewModels.swift:217-372`) -- `agents list`, and the person's own
 * prompt edited in place, put back as it was after any save, and closed
 * asked first when typing would be lost (GUI 6f). New agent… and Delete…
 * stay as the old prototype draws them, saying its toast (the document's
 * default: the create form and the delete are new design). Plain
 * TypeScript: it never imports React.
 */
export class AgentsViewModel extends DefinitionEditing {
  agents: readonly AgentEntry[] = []
  listing: LibraryListing = LibraryListing.notRead
  readonly selected = shownAgent
  /** The data directory the agents' files are relative to: `config path`'s file sits in `<data>/config/`. */
  private data: string | undefined = undefined
  private binary: string | undefined = undefined

  constructor(
    private readonly runner: CommandRunning,
    private readonly folders: FolderWriting,
    private readonly clock: Ticks,
    doors: LibraryDoors,
  ) {
    super(doors)
  }

  connect(binary: string | undefined): void {
    this.binary = binary
    if (binary === undefined) this.listing = LibraryListing.missing
    this.changed()
  }

  /** The data directory (once), then `agents list`; a failure is said where the list is, the rows read before kept. */
  async refresh(): Promise<void> {
    const binary = this.binary
    if (binary === undefined) return
    if (this.data === undefined) {
      const said = await this.runner.command(binary, ['config', 'path'])
      const line = said.case === 'done' ? said.stdout.split('\n')[0] : undefined
      if (line !== undefined && line !== '') this.data = splitPath(splitPath(line).folder).folder
    }
    const outcome = await this.runner.read(binary, ['agents', 'list'])
    const document = ReadOutcome.documentOf(outcome)
    if (document) this.agents = AgentEntry.list(document)
    this.listing = LibraryListing.of(outcome)
    this.changed()
  }

  /** What the list says under its rows: no apogee, or why it could not be read. */
  get emptyText(): string | undefined {
    return LibraryListing.text(this.listing, 'agents')
  }

  get current(): AgentEntry | undefined {
    return this.agents.find((agent) => agent.name === this.selected)
  }

  /** New agent…: the old prototype's toast, word for word. */
  newAgent(): void {
    this.doors.say?.('apogee agents create <name> (prototype)')
  }

  /** Delete…: the old prototype's toast, word for word, its agent the one shown. */
  askToDelete(): void {
    this.doors.say?.(`apogee agents delete ${this.current?.name ?? shownAgent} --purge | --keep-files: confirmed first (prototype)`)
  }

  /**
   * Opens one of the person's own agent files in the editor, as the CLI's
   * data directory holds it. A shipped agent's files are Apogee's, read
   * only. A file the folders do not hold -- the schema, which no recording
   * gives -- says the old prototype's toast, word for word.
   */
  edit(file: string): void {
    const agent = this.current
    if (!agent || agent.bundled || this.data === undefined) return
    const path = `${this.data}/${file}`
    const { folder, name } = splitPath(path)
    const text = this.folders.files(folder).find((entry) => entry.name === name)?.text
    if (text === undefined) {
      this.doors.say?.(`Edited in the app, saved to ${file} (prototype)`)
      return
    }
    this.open(new DefinitionEditor(`${agent.name} · ${file}`, path, text))
  }

  /**
   * Writes the file the CLI reads, and reads the agents back once the save's
   * ticks have passed. Nothing in this build checks an agent's files short
   * of running it; that is said. The editor is read-only until the read-back
   * ends, and the end lands on the editor as it stands then: one closed
   * meanwhile stays closed.
   */
  async save(): Promise<void> {
    const editor = this.editor
    if (!editor || editor.saving) return
    const saved = editor.text
    const { folder, name } = splitPath(editor.path)
    this.folders.write(folder, { name, text: saved })
    editor.wrote(saved)
    editor.saving = true
    this.changed()
    await afterTicks(this.clock, saveTicks)
    await this.refresh()
    if (this.editor?.path !== editor.path) return
    this.editor.endSave(saved, "Saved. Apogee checks an agent's prompt and schema when it runs -- its first run says what is wrong, if anything.", false)
    this.changed()
  }

  /** Put it back as it was is offered once a save has rewritten the file: nothing checks an agent's files before it runs, so any save is a point to undo from. */
  get offersRestore(): boolean {
    return this.editor?.rewritten === true
  }
}
