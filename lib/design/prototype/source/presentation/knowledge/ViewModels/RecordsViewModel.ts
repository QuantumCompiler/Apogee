import { CommandOutcome } from '../../../business/reads/CommandRunning'
import type { CommandRunning } from '../../../business/reads/CommandRunning'
import { KnowledgeQuery, KnowledgeRecord, RawConversation } from '../../../business/reads/Knowledge'
import { ReadOutcome } from '../../../business/reads/ReadRunner'
import { ReadDocument } from '../../../data/protocol/ReadDocument'
import { Observable } from '../../styles/Observation/Observable'
import type { RawFace, RecordFace, RecordsFace } from './KnowledgeFaces'

/** Where the list stands: the Mac app's `RecordsViewModel.Listing`. */
type Listing = { readonly case: 'notRead' } | { readonly case: 'read' } | { readonly case: 'missing' } | { readonly case: 'failed'; readonly message: string }

/** Where a record's raw conversation stands, and whose: the Mac's `RawView`. */
type Raw = { readonly record: string } & ({ readonly case: 'reading' } | { readonly case: 'shown'; readonly conversation: RawConversation } | { readonly case: 'failed'; readonly message: string })

const branchWords: ReadonlyArray<readonly [string, string]> = [
  ['', 'Every branch'],
  ...KnowledgeRecord.statuses.map((status): readonly [string, string] => [status, status.charAt(0).toUpperCase() + status.slice(1)]),
]

const noRaw: RawFace = { shown: false, open: false, text: '', note: '', failure: '', command: '' }

/**
 * The Knowledge place's records (design 1o): the Mac app's `RecordsViewModel`
 * (`RecordsViewModel.swift`) -- the decision records `knowledge list` reads,
 * a question answered by `knowledge query --json`, the list by branch, a
 * record open, and its raw conversation from `knowledge info --raw` -- drawn
 * as the old prototype's Records tab draws them. As the old one does, a
 * branch picked clears a standing question (the Mac asks it again: its 6h),
 * and capture, export, link, move and delete stay the old toasts, naming the
 * command (the document's default; the Mac's forms are new design). Plain
 * TypeScript.
 */
export class RecordsViewModel extends Observable {
  records: readonly KnowledgeRecord[] = []
  listing: Listing = { case: 'notRead' }
  /** The branch the list shows; `''` for every branch. */
  branch = ''
  question = ''
  query: KnowledgeQuery | undefined = undefined
  /** Why the last question was not answered, in the CLI's words. */
  failure: string | undefined = undefined
  selected: string | undefined = undefined
  raw: Raw | undefined = undefined
  /** The old branch choices, its words: `Every branch`, then the three `knowledge status` takes. */
  readonly branches = branchWords

  /** The toast, the composition's. */
  say?: (text: string) => void
  /** What Ask asks with nothing typed, the composition's: the old `kno.ask`'s question, filled in, where the Mac asks nothing. */
  emptyQuestion = ''

  private binary: string | undefined = undefined
  /** Counts the questions asked: a reply lands only if none was asked, or cleared, since. */
  private asks = 0

  constructor(private readonly runner: CommandRunning) {
    super()
  }

  connect(binary: string | undefined): void {
    this.binary = binary
    if (binary === undefined) this.listing = { case: 'missing' }
    this.changed()
  }

  /** The place came into view: the list read again. */
  async appeared(): Promise<void> {
    await this.refresh()
  }

  /** `knowledge list`, read again; a record no longer listed is no longer open. */
  async refresh(): Promise<void> {
    const binary = this.binary
    if (binary === undefined) return
    const outcome = await this.runner.read(binary, ['knowledge', 'list'])
    const document = ReadOutcome.documentOf(outcome)
    if (document) {
      this.records = KnowledgeRecord.list(document)
      this.listing = { case: 'read' }
    } else this.listing = { case: 'failed', message: ReadOutcome.failure(outcome) ?? 'the records could not be read' }
    if (this.selected !== undefined && !this.records.some((record) => record.id === this.selected)) this.selected = undefined
    this.changed()
  }

  setQuestion(question: string): void {
    this.question = question
    this.changed()
  }

  /**
   * Ask: `knowledge query --status <branch> --json -- <question>`, run as
   * typed, its stdout the document (the query takes `--json`, never
   * `--output-format`); its reply lands only while it is still the question
   * standing. With nothing typed it asks `emptyQuestion`, filling the field,
   * as the old `kno.ask` always shows its question asked.
   */
  async ask(): Promise<void> {
    const question = this.question.trim() || this.emptyQuestion
    const binary = this.binary
    if (binary === undefined || question === '') return
    if (this.question.trim() === '') {
      this.question = question
      this.changed()
    }
    this.asks += 1
    const mine = this.asks
    const outcome = await this.runner.command(binary, ['knowledge', 'query', '--status', this.branch, '--json', '--', question])
    if (this.asks !== mine) return
    const document = outcome.case === 'done' ? ReadDocument.from(outcome.stdout) : undefined
    const query = document ? KnowledgeQuery.read(document) : undefined
    this.query = query
    this.failure = query ? undefined : (CommandOutcome.failure(outcome) ?? 'the question could not be answered')
    this.changed()
  }

  /** The branch picked: the list filtered to it, and a standing question cleared, as the old `onBranch` sets `asked: false`. */
  setBranch(branch: string): void {
    this.branch = branch
    this.clearQuestion()
  }

  /** The question and its answer gone; a reply still coming is dropped. */
  clearQuestion(): void {
    this.asks += 1
    this.query = undefined
    this.failure = undefined
    this.question = ''
    this.changed()
  }

  /** The records the list shows: the chosen branch, or all. */
  get shown(): readonly KnowledgeRecord[] {
    return this.branch === '' ? this.records : this.records.filter((record) => record.status === this.branch)
  }

  /** A record opened; another's raw conversation closed. */
  select(id: string): void {
    if (this.selected !== id) this.raw = undefined
    this.selected = id
    this.changed()
  }

  get selectedRecord(): KnowledgeRecord | undefined {
    const id = this.selected
    if (id === undefined) return undefined
    return this.records.find((record) => record.id === id) ?? this.query?.matches.find((match) => match.record.id === id)?.record
  }

  /**
   * The open record's raw conversation: `knowledge info <id> --raw`, its text
   * after the CLI's marker -- never the archive's path read. A reply lands
   * only while its record is still the one whose conversation was asked.
   */
  async showRaw(): Promise<void> {
    const record = this.selectedRecord
    const binary = this.binary
    if (!record || binary === undefined) return
    this.raw = { record: record.id, case: 'reading' }
    this.changed()
    const outcome = await this.runner.command(binary, ['knowledge', 'info', record.id, '--raw'])
    if (this.raw?.record !== record.id) return
    if (outcome.case === 'done') {
      const conversation = RawConversation.printed(outcome.stdout)
      this.raw = conversation ? { record: record.id, case: 'shown', conversation } : { record: record.id, case: 'failed', message: 'apogee printed no raw conversation' }
    } else this.raw = { record: record.id, case: 'failed', message: CommandOutcome.failure(outcome) ?? 'apogee printed no raw conversation' }
    this.changed()
  }

  hideRaw(): void {
    this.raw = undefined
    this.changed()
  }

  /** Capture…: the old toast. */
  capture(): void {
    this.say?.('apogee knowledge capture: the clerk distils the conversation (prototype)')
  }

  /** Export…: the old toast. */
  exportRecords(): void {
    this.say?.('apogee knowledge export --format markdown -o knowledge.md (prototype)')
  }

  /** Link: the old toast. */
  link(): void {
    this.say?.('apogee knowledge link (prototype)')
  }

  /** Move to…: the old toast. */
  move(): void {
    this.say?.('apogee knowledge status (prototype)')
  }

  /** Delete…: the old toast. */
  remove(): void {
    this.say?.('apogee knowledge delete: confirmed first (prototype)')
  }

  /**
   * Why the list is empty, in the Mac's words; `''` when it is not. With no
   * apogee, no words: the old Records tab has no missing state, so neither
   * does this one (the document's default; the Mac's line is a follow-up's).
   */
  get emptyText(): string {
    switch (this.listing.case) {
      case 'notRead':
        return ''
      case 'missing':
        return ''
      case 'failed':
        return `The records could not be read: ${this.listing.message}`
      case 'read':
        break
    }
    if (this.query) return ''
    if (this.records.length === 0) return 'No decision records yet. Capture one from a conversation: the clerk distils its reasoning and its decision.'
    return this.shown.length === 0 ? `No ${this.branch} records.` : ''
  }

  face(): RecordsFace {
    const query = this.query
    const rows = query
      ? query.matches.map((match) => ({ record: match.record, score: KnowledgeQuery.score(match) }))
      : this.shown.map((record) => ({ record, score: '' }))
    const record = this.selectedRecord
    return {
      question: this.question,
      branch: this.branch,
      found: query ? `${query.matches.length} found · ${query.retriever}${query.reranked ? ' · reranked' : ''}` : '',
      failure: this.failure ?? '',
      rows: rows.map(({ record: row, score }) => ({ id: row.id, decision: row.decision, score, meta: `${row.status} · ${row.discipline} · ${row.timestamp.slice(0, 10)}`, current: row.id === this.selected })),
      empty: this.emptyText,
      record: record ? this.recordFace(record) : { shown: false, id: '', decision: '', facts: [], raw: noRaw },
      noSelection: record ? '' : 'Choose a record to read its reasoning and its decision.',
    }
  }

  private recordFace(record: KnowledgeRecord): RecordFace {
    const raw = this.raw?.record === record.id ? this.raw : undefined
    return {
      shown: true,
      id: record.id,
      decision: record.decision,
      facts: [
        { label: 'WHY', value: record.intent },
        { label: 'BRANCH', value: record.status },
        { label: 'DISCIPLINE', value: record.discipline },
        { label: 'FROM', value: `${record.source} · ${record.attribution}` },
        { label: 'LINKED TO', value: record.link === '' ? 'nothing yet' : record.link },
        { label: 'CAPTURED', value: record.timestamp },
      ],
      raw:
        record.rawRef === undefined
          ? noRaw
          : {
              shown: true,
              // Open once read, as the Mac's Hide shows: the conversation, the archive unreadable, or why it could not be read.
              open: raw !== undefined && raw.case !== 'reading',
              text: raw?.case === 'shown' && raw.conversation.case === 'text' ? raw.conversation.text : '',
              note: raw?.case === 'shown' && raw.conversation.case === 'unavailable' ? "apogee can't read its archive: the raw conversation is unavailable." : '',
              failure: raw?.case === 'failed' ? raw.message : '',
              command: `apogee knowledge info ${record.id} --raw`,
            },
    }
  }
}
