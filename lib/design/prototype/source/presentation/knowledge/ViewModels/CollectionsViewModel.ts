import type { JobNotes } from '../../../business/jobs/JobNotes'
import { JobProgress } from '../../../business/jobs/JobProgress'
import type { JobRunning, JobStarting } from '../../../business/jobs/SupervisedJob'
import { CommandOutcome } from '../../../business/reads/CommandRunning'
import type { CommandRunning } from '../../../business/reads/CommandRunning'
import { CollectionInfo, CollectionRow } from '../../../business/reads/Knowledge'
import { ReadOutcome } from '../../../business/reads/ReadRunner'
import { Observable } from '../../styles/Observation/Observable'
import type { CollectionPageFace, CollectionsFace, NotesFace } from './KnowledgeFaces'

/** The collection the old page shows with none chosen (`knName`'s fallback): notes. */
const fallback = 'notes'

const plural = (count: number, word: string) => `${count} ${word}${count === 1 ? '' : 's'}`

const noNotes: NotesFace = { shown: false, title: '', lines: [] }

const noPage: CollectionPageFace = { shown: false, name: '', chunks: '', index: '', vectors: '', question: '', searched: false, results: [], failure: '', sourceCount: 0, sources: [] }

/**
 * The Knowledge place's collections (design 1o): the Mac app's
 * `CollectionsViewModel` (`CollectionsViewModel.swift`) -- the embed store as
 * `embed list` and `embed info` read it, its Vectors line in `embed info`'s
 * own text words, a search answered as `embed query` prints it, landing only
 * on the collection it was asked of -- drawn as the old Collections tab draws
 * them: with none chosen, notes' page shown and its row current (the old
 * `kn.coll` starts at notes), and ingest, a source's removal and the
 * collection's delete the old toasts, naming the command (the document's
 * default). A search of a collection whose search the CLI never recorded
 * says the old toast too (`kno.search`), as the old one does for every
 * collection but notes. The ingest sheet's run is the Mac's (GUI M22):
 * `embed ingest -- <c> <path>` supervised, its toast stdout's first line, and
 * what the CLI said on stderr in its house voice kept on the collection's
 * page. Plain TypeScript.
 */
export class CollectionsViewModel extends Observable {
  collections: readonly CollectionRow[] = []
  listed = false
  /** Why the list could not be read, in the CLI's words. */
  listFailure: string | undefined = undefined
  selected: string | undefined = undefined
  info: CollectionInfo | undefined = undefined
  question = ''
  /** A search's lines, as `embed query` printed them; `undefined` before one. */
  results: readonly string[] | undefined = undefined
  /** Why the last search or page read did not land, in the CLI's words. */
  failure: string | undefined = undefined
  /** An ingest running now (GUI M22). */
  run: JobProgress | undefined = undefined
  /** The last ingest's notes, for the collection it named (GUI M22). */
  doneNotes: { readonly collection: string; readonly notes: JobNotes } | undefined = undefined

  /** The toast, the composition's. */
  say?: (text: string) => void
  /** The collections whose `embed query` the CLI recorded, the composition's: a search of another says the old toast. */
  recordedSearches: readonly string[] = []
  /** What Search asks of a collection with nothing typed, the composition's: the old `kno.search`'s question of notes, filled in, where the Mac asks nothing. */
  emptyQuestions: Readonly<Record<string, string>> = {}

  private binary: string | undefined = undefined
  private job: JobRunning | undefined = undefined

  constructor(
    private readonly runner: CommandRunning,
    private readonly starter: JobStarting,
  ) {
    super()
  }

  connect(binary: string | undefined): void {
    this.binary = binary
    this.changed()
  }

  async appeared(): Promise<void> {
    await this.refresh()
  }

  /** The collection whose page is drawn: the one chosen, else notes, as the old page falls back to it. */
  get shownName(): string | undefined {
    if (this.selected !== undefined) return this.selected
    return this.collections.some((row) => row.name === fallback) ? fallback : this.collections[0]?.name
  }

  /** `embed list`, and the shown collection's `embed info`, read again; a collection gone takes its page and lines with it. */
  async refresh(): Promise<void> {
    const binary = this.binary
    if (binary === undefined) return
    const outcome = await this.runner.read(binary, ['embed', 'list'])
    const document = ReadOutcome.documentOf(outcome)
    if (document) {
      this.collections = CollectionRow.list(document)
      this.listed = true
      this.listFailure = undefined
    } else this.listFailure = ReadOutcome.failure(outcome) ?? 'the collections could not be read'
    if (this.selected !== undefined && !this.collections.some((row) => row.name === this.selected)) {
      this.selected = undefined
      this.info = undefined
      this.results = undefined
      this.doneNotes = undefined
    }
    this.changed()
    const shown = this.shownName
    if (shown !== undefined) await this.readInfo(shown)
  }

  /** A collection chosen: its page read, and the lines asked of the one before dropped. */
  async select(name: string): Promise<void> {
    if (name === this.selected) return
    const before = this.shownName
    this.selected = name
    if (before !== name) {
      this.info = undefined
      this.results = undefined
      this.failure = undefined
    }
    if (this.doneNotes?.collection !== name) this.doneNotes = undefined
    this.changed()
    await this.readInfo(name)
  }

  /** `embed info <name>`, landing only while `name` is still the one shown. */
  private async readInfo(name: string): Promise<void> {
    const binary = this.binary
    if (binary === undefined) return
    const outcome = await this.runner.read(binary, ['embed', 'info', name])
    if (this.shownName !== name) return
    const document = ReadOutcome.documentOf(outcome)
    this.info = document ? CollectionInfo.read(document) : undefined
    if (!this.info) this.failure = ReadOutcome.failure(outcome) ?? `${name} could not be read`
    this.changed()
  }

  setQuestion(question: string): void {
    this.question = question
    this.changed()
  }

  /**
   * Search: `embed query <collection> -- <question>`, its lines as printed,
   * landing on the collection they were asked of; a collection whose search
   * the CLI never recorded says the old toast and runs nothing. With nothing
   * typed it asks the collection's `emptyQuestions`, filling the field, as
   * the old `kno.search` always shows notes searched.
   */
  async search(): Promise<void> {
    const name = this.shownName
    const binary = this.binary
    if (name === undefined || binary === undefined) return
    if (!this.recordedSearches.includes(name)) {
      this.say?.(`apogee embed query ${name}: its lines, as printed (prototype)`)
      return
    }
    const question = this.question.trim() || (this.emptyQuestions[name] ?? '')
    if (question === '') return
    if (this.question.trim() === '') {
      this.question = question
      this.changed()
    }
    const outcome = await this.runner.command(binary, ['embed', 'query', name, '--', question])
    if (this.shownName !== name) return
    if (outcome.case === 'done') {
      this.results = outcome.stdout.replace(/^\n+|\n+$/g, '').split('\n')
      this.failure = undefined
    } else {
      this.results = undefined
      this.failure = CommandOutcome.failure(outcome)
    }
    this.changed()
  }

  /** Ingest… and Ingest more…: the old toast -- the sheet itself is a follow-up's; its run is `runIngest`. */
  ingest(): void {
    this.say?.('apogee embed ingest: chosen in a sheet, run supervised (prototype)')
  }

  /**
   * The ingest sheet's run, the Mac's `ingest()` (GUI M22): `embed ingest --
   * <collection> <path>`, supervised. When it ends, its collection is chosen
   * and read; a run that succeeded toasts stdout's first line and keeps what
   * it said on stderr in the CLI's house voice for that collection's page,
   * until Dismiss, the next ingest, another collection, or a refresh that
   * drops it. A failure is stderr's last line, as ever.
   */
  runIngest(collection: string, path: string): void {
    const binary = this.binary
    if (binary === undefined || this.job !== undefined) return
    this.doneNotes = undefined
    const progress = new JobProgress()
    this.run = progress
    const job = this.starter.startJob(binary, ['embed', 'ingest', '--', collection, path], false)
    this.job = job
    this.changed()
    void (async () => {
      for await (const update of job.updates) {
        progress.apply(update)
        this.changed()
      }
      this.job = undefined
      this.run = undefined
      if (this.shownName !== collection) {
        this.info = undefined
        this.results = undefined
      }
      this.selected = collection
      if (progress.status.case === 'done' && !progress.notes.isEmpty) this.doneNotes = { collection, notes: progress.notes }
      this.changed()
      await this.refresh()
      const status = progress.status
      if (status.case === 'done') this.say?.(progress.notices[0] ?? 'Ingested')
      else if (status.case === 'failed') {
        this.failure = status.message
        this.changed()
      } else if (status.case === 'stopped') this.say?.('Stopped: what was read stays')
    })()
  }

  /** Stop, as the run line's button: the run interrupted. */
  async stop(): Promise<void> {
    await this.job?.stop()
  }

  /** The notes' card dismissed. */
  dismissNotes(): void {
    this.doneNotes = undefined
    this.changed()
  }

  /** A source's Remove…: the old toast. */
  removeSource(source: string): void {
    this.say?.(`apogee embed delete ${this.shownName ?? ''} --source ${source}: confirmed first (prototype)`)
  }

  /** The collection's Delete…: the old toast. */
  deleteCollection(): void {
    this.say?.(`apogee embed delete ${this.shownName ?? ''} --yes: confirmed first (prototype)`)
  }

  /** Why there is no list, in the Mac's words; `''` -- with no apogee too, as the old Collections tab has no missing state (the document's default). */
  get note(): string {
    if (this.binary === undefined) return ''
    if (this.listFailure !== undefined) return `The collections could not be read: ${this.listFailure}`
    if (this.listed && this.collections.length === 0) return 'No collections yet. Ingest a folder of documents: it is read, chunked and indexed for retrieval.'
    return ''
  }

  face(): CollectionsFace {
    const info = this.info
    const name = this.shownName
    const run = this.run
    const notes = this.doneNotes
    return {
      run: run ? { shown: true, text: run.busy ?? run.notices.at(-1) ?? 'Working' } : { shown: false, text: '' },
      notes: notes !== undefined && notes.collection === name && info !== undefined ? { shown: true, title: 'What the ingest said', lines: notes.notes.shown } : noNotes,
      // The shown collection's row is current, chosen or not: the old kn.coll starts at notes, as shownName falls back to it.
      rows: this.collections.map((row) => ({ name: row.name, meta: `${plural(row.sources, 'source')} · ${plural(row.chunks, 'chunk')}`, current: row.name === name })),
      note: this.note,
      page:
        name === undefined
          ? noPage
          : {
              shown: true,
              name,
              chunks: info ? String(info.chunks) : '',
              index: info?.index ?? '',
              vectors: info ? CollectionInfo.vectorsLine(info) : '',
              question: this.question,
              searched: this.results !== undefined,
              results: this.results ?? [],
              failure: this.failure ?? '',
              sourceCount: info?.sources.length ?? 0,
              sources: info?.sources ?? [],
            },
    }
  }
}
