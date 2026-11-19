import type { ModelKind, ModelRow } from '../../../business/reads/Models'
import type { ChatLaunch } from '../../../business/session/ChatLaunch'
import type { ChatSession, ChatStarting, SessionEvent } from '../../../business/session/ChatSession'
import type { RunSettings } from '../../../data/protocol/DriverLine'
import type { Clock } from '../../../infrastructure/clock/Clock'
import { Observable } from '../../styles/Observation/Observable'
import { PlayNarration } from './Play'
import type { PlayChoice } from './Play'
import { ContextMeter, noSuite, PrivateMode, SlashCommand, SuiteChoice } from './SessionActions'
import type { HeldLine } from './SessionActions'
import { Transcript } from './Transcript'
import type { TranscriptItem } from './Transcript'

/** What the session needs of the clock: its ticks, for how long a turn has waited. */
export type Ticks = Pick<Clock, 'ticks' | 'onTick'>

/** Where the binary stands for this session: the Mac app's `SessionViewModel.Connection`. */
export type Connection =
  | { readonly case: 'idle' }
  | { readonly case: 'locating' }
  /** None found. */
  | { readonly case: 'missing' }
  | { readonly case: 'connected'; readonly binary: string }

/** Where the person goes when this session is left: what the old `leaveFor(then)` carries, and the Sessions place's `SessionLibraryViewModel.pending` on the Mac. */
export type SessionLeave =
  | { readonly type: 'new'; readonly suite?: string; readonly force?: boolean }
  | { readonly type: 'private' }
  | { readonly type: 'open'; readonly id: string }
  | { readonly type: 'execute' }

/** An act the window hands to its owner -- the app root until 1g's library: the Mac app's `SessionAction`, and the old prototype's doors. */
export type SessionAction =
  /** Choose the suite: the owner reads the suites and the CLI's verdict on each, then presents the chooser. */
  | { readonly case: 'chooseSuite' }
  /** The Models library (⇧⌘L), at the session's model. */
  | { readonly case: 'openLibrary' }
  /** Say `text` in the toast: a slash command the window does not act on, in the old prototype's words. */
  | { readonly case: 'say'; readonly text: string }
  /** Ask before leaving: the `endPrivate` confirm. */
  | { readonly case: 'ask'; readonly kind: 'endPrivate' }
  /** Carry the leave out: open what `then` names. */
  | { readonly case: 'leave'; readonly then: SessionLeave }

/** A file the `@` menu offers: its path in the session's folder, and its size as listed. */
export interface FolderFile {
  readonly path: string
  readonly size: string
}

/** What `@` completes from: the session's folder and its files (`Mentions.candidates(for:in:)` on the Mac, which reads the folder; a browser reads none, so it is handed in). */
export interface FolderListing {
  readonly folder: string
  readonly files: readonly FolderFile[]
}

/** One item of the attach menu: its label, its key, and the path it attaches. */
export interface AttachChoice {
  readonly label: string
  readonly hint: string
  readonly path: string
}

/** What the model facts say of one backend, for the header and the menu. */
export interface ModelFace {
  readonly name: string
  /** Where it runs; `unknown` for a backend whose type is none of the three (the mock), never guessed. */
  readonly kind: ModelKind | 'unknown'
  /** `local · this Mac`, `API · billed per use`; empty where the kind is not known. */
  readonly tag: string
  /** The empty state's line: where it answers. Empty where the kind is not known. */
  readonly where: string
}

/** One group of the model menu: the old prototype's three, then any backend of a kind the design has no group for, under the CLI's own group word. */
export interface ModelMenuGroup {
  readonly title: string
  readonly items: ReadonlyArray<{ readonly name: string; readonly sub: string; readonly kind: ModelKind | 'unknown'; readonly current: boolean }>
}

/** An item of the composer's `/` menu: a command, or a symphony after `/play `. */
export interface SlashItem {
  readonly name: string
  readonly args: string
  readonly summary: string
  /** A symphony `/play` names: picking it completes the line. */
  readonly play: boolean
}

/** The suite chooser's state: the choice and whether it is forced. */
export interface SuiteChooser {
  readonly chosen: string
  readonly force: boolean
}

/** The `endPrivate` confirm's words: the old prototype's (`main.html:6086-6095` at `b375fac9`). */
export interface EndPrivate {
  readonly title: string
  readonly body: string
  readonly confirm: string
  readonly cancel: string
}

/** One row of the conversation, as its view draws it. */
export type RowFace =
  | { readonly id: number; readonly kind: 'user' | 'notice' | 'quiet'; readonly text: string }
  | {
      readonly id: number
      readonly kind: 'activity'
      /** `thought`, `thinking · 2 steps`, `played summarize-verify · 2 stages`: the Mac app's words. */
      readonly summary: string
      readonly live: boolean
      readonly reasoning: string
      /** Each line as the CLI wrote it, a play's stage marked `1/2`. */
      readonly lines: ReadonlyArray<{ readonly mark: string; readonly text: string }>
    }
  | {
      readonly id: number
      readonly kind: 'answer'
      readonly text: string
      readonly finished: boolean
      /** Once finished: the model its result named and that model's kind, and the tokens where the provider reported them. */
      readonly footer: { readonly model: string; readonly kind: ModelKind | 'unknown'; readonly usage: readonly string[] }
    }
  | { readonly id: number; readonly kind: 'ended'; readonly text: string; readonly atLaunch: boolean }

/** Everything the session's views draw, as words: what the per-preset walk reads. */
export interface SessionFace {
  /** Looking for the binary; none found; nothing sent yet; the conversation. */
  readonly state: 'starting' | 'missing' | 'empty' | 'conversation'
  readonly header: {
    readonly title: string
    readonly privateBadge: boolean
    /** An `execute` session's banner; empty for a chat. */
    readonly banner: string
    /** What a chat runs under; empty where only the CLI knows and has not said. */
    readonly suiteWords: string
    readonly canSwitchSuite: boolean
    readonly model: ModelFace
    readonly think: string
    readonly meter: { readonly known: boolean; readonly text: string; readonly percent: number; readonly used: string; readonly window: string; readonly warns: boolean }
    readonly inspectorOpen: boolean
  }
  readonly empty: {
    readonly heading: string
    /** A private session's words; empty for another. */
    readonly privateWords: string
    /** What the CLI said at launch, under the empty state. */
    readonly notices: readonly string[]
    /** A session was launched: nobody launched the monitor's (until 1v), so the acts that need one are drawn off. */
    readonly launched: boolean
    readonly privateInstead: boolean
    readonly chooseSuite: boolean
  }
  readonly rows: readonly RowFace[]
  readonly pending: { readonly shown: boolean; readonly word: string; readonly seconds: string; readonly model: ModelFace }
  /** The suite chooser, while it is open: its rows with the CLI's verdict on each. */
  readonly suiteSheet: {
    readonly open: boolean
    readonly now: string
    readonly rows: ReadonlyArray<{ readonly name: string; readonly title: string; readonly verdict: string; readonly over: boolean; readonly running: boolean; readonly chosen: boolean }>
    readonly overBudget: boolean
    readonly force: boolean
    readonly total: string
    readonly canSwitch: boolean
  }
  readonly composer: {
    readonly draft: string
    readonly placeholder: string
    readonly status: string
    readonly streaming: boolean
    readonly canSend: boolean
    readonly canAttach: boolean
    readonly attachments: readonly string[]
  }
}

/** The think menu's three modes, the old prototype's words. */
export const thinkModes = [
  { mode: 'off', label: 'Off', description: 'Answer straight away.' },
  { mode: 'on', label: 'On', description: 'Think before every answer.' },
  { mode: 'auto', label: 'Auto', description: 'The utility model decides, once per question.' },
] as const

/** The effort row's six, the old prototype's: `default` sets none (`auto`). */
export const efforts = ['default', 'low', 'medium', 'high', 'xhigh', 'max'] as const

/**
 * One chat session in the window (design 1u): the binary found, the session
 * started, its events folded into the transcript, and the person's acts sent
 * back as the protocol's lines -- in the order they were made. The Mac app's
 * `SessionViewModel` (`PresentationSession`), its names, over the stand-in's
 * chat starter: plain TypeScript that never imports React.
 *
 * Everything the window shows is `connection`, `transcript`, `draft` and the
 * facts its owner hands in (the model facts, the context window, the suites);
 * everything it does is a method here. No line of the transcript is made
 * here: each is a replayed event's, or what the person sent.
 */
export class SessionViewModel extends Observable {
  /** The old pending line's tick: 0.4 s. */
  static readonly secondsPerTick = 0.4

  connection: Connection = { case: 'idle' }
  transcript = new Transcript()
  /** What the person is typing. */
  draft = ''
  /** What the session holds attached, by the path given: each `attach` sent. */
  attachments: readonly string[] = []
  /** The run settings asked for: the launch's, then each `set` sent. */
  settings: RunSettings
  /** The lines made while a turn ran, sent once it ends (the Mac app's `held`; the strip that says so is 1v's). */
  held: readonly HeldLine[] = []
  /** The inspector's column, open as the old `inspector: true` opens it (1g draws the pane, and may move this into its library). */
  inspectorOpen = true
  /** The open header or composer menu. */
  menu: 'model' | 'think' | 'attach' | undefined = undefined
  /** The `/` or `@` menu's highlighted row. */
  menuIndex = 0
  /** Escape closed the `/` and `@` menus until the draft changes. */
  menuDismissed = false
  /** The model whose window the owner read last, so an answer by the same model reads none again. */
  windowOf: string | undefined = undefined
  /** The suites the chooser offers, with the CLI's verdict on each: handed in. */
  suiteChoices: readonly SuiteChoice[] = []
  /** `models.default_suite`, once read (when a new chat opens, and again when the chooser does): empty when none is set. */
  defaultSuite: string | undefined = undefined
  /** The suite chooser, while it is open. */
  suiteChooser: SuiteChooser | undefined = undefined
  /** The session's name, as the Sessions place lists it (1g); until then the window's own words. */
  name: string | undefined = undefined
  /** Told every event, after the transcript has folded it: how the owner reads what a turn changed. */
  observer: ((event: SessionEvent) => void) | undefined = undefined
  /** Where the acts the owner carries out go. */
  onAction: ((action: SessionAction) => void) | undefined = undefined

  private launch: ChatLaunch
  private session: ChatSession | undefined = undefined
  private facts: {
    kinds: Readonly<Record<string, ModelKind>>
    rows: readonly ModelRow[]
    window: number | undefined
    plays: readonly PlayChoice[]
  } = { kinds: {}, rows: [], window: undefined, plays: [] }
  /** What stops the session's clock listener. */
  private leaveClock: () => void
  /** The leave the `endPrivate` confirm asks about. */
  private pendingLeave: SessionLeave | undefined = undefined
  /** The tick the turn in flight was sent on. */
  private sentAt = 0

  constructor(
    private readonly starter: ChatStarting,
    private readonly clock: Ticks,
    launch: ChatLaunch = {},
    /** What `@` lists: the session's folder, handed in. */
    readonly listing: FolderListing = { folder: '', files: [] },
    /** What the attach menu attaches: handed in. */
    readonly attachChoices: readonly AttachChoice[] = [],
  ) {
    super()
    this.launch = launch
    this.settings = launch.settings ?? {}
    this.leaveClock = clock.onTick(() => {
      // The pending line counts its seconds on the clock while a turn waits.
      if (this.pending) this.changed()
    })
  }

  // MARK: - The facts its owner hands in, each a redraw when it lands

  /** Each configured backend's kind, from `models list`. */
  get modelKinds(): Readonly<Record<string, ModelKind>> {
    return this.facts.kinds
  }

  set modelKinds(kinds: Readonly<Record<string, ModelKind>>) {
    this.update(() => (this.facts.kinds = kinds))
  }

  /** The configured backends a chat runs on, as `models list` lists them. */
  get modelRows(): readonly ModelRow[] {
    return this.facts.rows
  }

  set modelRows(rows: readonly ModelRow[]) {
    this.update(() => (this.facts.rows = rows))
  }

  /** The context window of the session's model in tokens, from `models info`; `undefined` where the CLI states none. */
  get contextWindow(): number | undefined {
    return this.facts.window
  }

  set contextWindow(window: number | undefined) {
    this.update(() => (this.facts.window = window))
  }

  /** The symphonies `/play` offers, as `symphonies list` says them: for an `execute` session. */
  get plays(): readonly PlayChoice[] {
    return this.facts.plays
  }

  set plays(plays: readonly PlayChoice[]) {
    this.update(() => (this.facts.plays = plays))
  }

  /** `models.default_suite`, read when a new chat opens: the suite it runs under, which the header names. Empty when none is set. */
  knowDefaultSuite(name: string | undefined): void {
    this.update(() => (this.defaultSuite = name))
  }

  // MARK: - The session

  /** An `execute` session, rather than a chat. */
  get isExecute(): boolean {
    return this.launch.execute !== undefined
  }

  /** The launch it was started with. */
  get launchSettings(): ChatLaunch {
    return this.launch
  }

  /**
   * Finds the binary and starts a session on it: resolves once its child is
   * open, so a preset's sends can be written before the clock starts
   * (DEVELOPER.md → the CLI stand-in).
   */
  async start(): Promise<void> {
    if (this.connection.case === 'locating') return
    if (this.connection.case === 'connected' && this.transcript.status.case !== 'ended') return
    this.update(() => {
      this.connection = { case: 'locating' }
      this.transcript = new Transcript()
      this.attachments = []
    })
    const located = await this.starter.locateBinary()
    if (located.case === 'notFound') {
      this.update(() => (this.connection = { case: 'missing' }))
      return
    }
    const session = this.starter.openChat(located.binary, this.launch)
    this.update(() => {
      this.connection = { case: 'connected', binary: located.binary }
      this.session = session
    })
    void this.follow(session)
  }

  /** Folds each event the session says, in order, until it ends. */
  private async follow(session: ChatSession): Promise<void> {
    for await (const event of session.events) {
      if (session !== this.session) return
      this.update(() => this.transcript.apply(event))
      if (event.case === 'event') {
        const kind = event.event.kind.case
        if (kind === 'result' || kind === 'error') this.releaseHeld()
      }
      this.observer?.(event)
    }
  }

  /** A turn written before the session has spoken, as a still preset writes its sends: drawn where the child reads it. */
  writeAhead(text: string): void {
    if (!this.session) return
    this.transcript.userWroteAhead(text)
    this.sentAt = this.clock.ticks
    void this.session.send(text).catch(() => {})
  }

  /** Ends the session: its input closes, a turn in flight cancelled first, and it stops listening to the clock. */
  async stop(): Promise<void> {
    this.leaveClock()
    const session = this.session
    if (!session) return
    await session.stop()
  }

  // MARK: - The header

  /** Whether nothing of it is written. */
  get privateMode(): PrivateMode | undefined {
    if (this.launch.privately || this.transcript.isPrivate) return 'nothingWritten'
    return this.launch.noRecall ? 'deletedOnClose' : undefined
  }

  /** The header's title: its name as the Sessions place lists it, else the window's own words. */
  get title(): string {
    return this.name ?? (this.privateMode === 'nothingWritten' ? 'Private session' : 'New session')
  }

  /** Whether a turn has been sent in what the window shows. */
  get hasTurns(): boolean {
    return this.transcript.items.some((item) => item.content.case === 'user')
  }

  /** A conversation that writes nothing, a turn sent, its child not ended: ending it loses it for good. */
  get isLostIfEnded(): boolean {
    return this.privateMode === 'nothingWritten' && this.transcript.status.case !== 'ended' && this.hasTurns
  }

  /** What an `execute` session runs as: `execute · suite coding · orchestrating · in ingest`, the CLI's word where its `session` event says. */
  get executeBanner(): string | undefined {
    const execute = this.launch.execute
    if (!execute) return undefined
    const parts = ['execute']
    const suite = this.knownSuite
    if (suite) parts.push(suite.name === undefined ? 'no suite' : `suite ${suite.name}${suite.note ? `, ${suite.note}` : ''}`)
    if ((this.transcript.orchestrating ?? (execute.orchestrate ? true : undefined)) === true) parts.push('orchestrating')
    if (this.launch.folder) parts.push(`in ${this.launch.folder.split('/').filter((part) => part !== '').at(-1) ?? this.launch.folder}`)
    return parts.join(' · ')
  }

  /** What a chat runs under: `suite pair`, `no suite`, `suite fast, the default`; `undefined` where only the CLI knows and has not said. */
  get suiteWords(): string | undefined {
    const suite = this.knownSuite
    if (!suite) return undefined
    return suite.name === undefined ? 'no suite' : `suite ${suite.name}${suite.note ? `, ${suite.note}` : ''}`
  }

  /**
   * The suite it runs under by the chooser's name, `off` for none: the CLI's
   * word, else the launch's flag, else the default a new chat runs under;
   * `undefined` where only the CLI knows (a resumed chat it has not spoken
   * of). The same facts `suiteWords` reads, so the row marked now is the
   * suite the header names. (The Mac's `runningSuite` ignores the default.)
   */
  get runningSuite(): string | undefined {
    const suite = this.knownSuite
    return suite === undefined ? undefined : (suite.name ?? noSuite)
  }

  private get knownSuite(): { readonly name: string | undefined; readonly note: string | undefined } | undefined {
    const launched = this.launch.execute ? this.launch.execute.suite : this.launch.suite
    const note = (name: string): string | undefined => {
      if (launched === name) return this.launch.forceSuite ? 'forced past its memory' : undefined
      if (launched === undefined && this.launch.resume === undefined && this.defaultSuite === name) return 'the default'
      return undefined
    }
    const said = this.transcript.suite
    if (said) return said.case === 'named' ? { name: said.name, note: note(said.name) } : { name: undefined, note: undefined }
    if (launched === noSuite) return { name: undefined, note: undefined }
    if (launched !== undefined) return { name: launched, note: note(launched) }
    if (this.launch.resume !== undefined || this.defaultSuite === undefined) return undefined
    return this.defaultSuite === '' ? { name: undefined, note: undefined } : { name: this.defaultSuite, note: 'the default' }
  }

  /** Whether the session can move to another suite: not one lost if ended, nor one with no binary, nor one nobody launched (the monitor's, until 1v). */
  get canSwitchSuite(): boolean {
    return !this.isLostIfEnded && this.connection.case !== 'missing' && this.connection.case !== 'idle'
  }

  /** What the model facts say of `name`. */
  modelFace(name: string | undefined): ModelFace {
    const label = name ?? 'apogee'
    const kind = name === undefined ? undefined : this.modelKinds[name]
    const row = this.modelRows.find((entry) => entry.backend === name)
    switch (kind) {
      case 'local':
        return { name: label, kind, tag: 'local · this Mac', where: `${label} answers on this Mac. Nothing leaves it.` }
      case 'api':
        return { name: label, kind, tag: 'API · billed per use', where: `${label} answers through the ${row?.backendType ?? 'provider'} API, billed per use.` }
      case 'subscription':
        return { name: label, kind, tag: `subscription · ${row?.backendType ?? ''}`, where: `${label} answers through your ${row?.backendType ?? ''} subscription.` }
      default:
        return { name: label, kind: 'unknown', tag: '', where: '' }
    }
  }

  /** The model the session runs on, as the CLI says it (GUI M10). */
  get model(): ModelFace {
    return this.modelFace(this.transcript.model)
  }

  /** The model menu's groups, from `models list`: the chat backends by kind, each with the CLI's own words. */
  get menuGroups(): readonly ModelMenuGroup[] {
    const current = this.settings.model ?? this.transcript.model
    const titled: ReadonlyArray<readonly [string, ModelKind | 'unknown']> = [
      ['ON THIS MAC', 'local'],
      ['CLOUD API · BILLED PER USE', 'api'],
      ['SUBSCRIPTION', 'subscription'],
    ]
    const item = (row: ModelRow) => ({
      name: row.backend,
      sub: [row.model, row.state].filter((word) => word !== '' && word !== '-').join(' · '),
      kind: this.modelKinds[row.backend] ?? ('unknown' as const),
      current: row.backend === current,
    })
    const groups: ModelMenuGroup[] = titled.flatMap(([title, kind]) => {
      const items = this.modelRows.filter((row) => (this.modelKinds[row.backend] ?? 'unknown') === kind).map(item)
      return items.length > 0 && kind !== 'unknown' ? [{ title, items }] : []
    })
    // A backend of no kind the design draws (the mock) stays listed, under the CLI's own group word.
    const others = this.modelRows.filter((row) => this.modelKinds[row.backend] === undefined)
    for (const group of [...new Set(others.map((row) => row.group))]) {
      groups.push({ title: group.toUpperCase(), items: others.filter((row) => row.group === group).map(item) })
    }
    return groups
  }

  /** The think control's words: the mode asked for, or the CLI's own default, and an effort set. */
  get thinkLabel(): string {
    const think = this.settings.think ?? 'default'
    const effort = this.settings.effort
    return think + (think !== 'off' && effort !== undefined && effort !== 'auto' ? ` · ${effort}` : '')
  }

  /** The context meter: the last finished turn's input tokens against the window. */
  get meter(): ContextMeter {
    let used: number | undefined
    for (const item of [...this.transcript.items].reverse()) {
      if (item.content.case === 'answer' && item.content.answer.finished && item.content.answer.usage) {
        used = item.content.answer.usage.inputTokens
        break
      }
    }
    return ContextMeter.of(used, this.contextWindow)
  }

  toggleInspector(): void {
    this.update(() => (this.inspectorOpen = !this.inspectorOpen))
  }

  /** The context meter's click: the inspector, open (the old `showContext`). */
  showContext(): void {
    this.update(() => (this.inspectorOpen = true))
  }

  toggleMenu(menu: 'model' | 'think' | 'attach'): void {
    this.update(() => (this.menu = this.menu === menu ? undefined : menu))
  }

  closeMenus(): void {
    this.update(() => (this.menu = undefined))
  }

  /** Models library…: the owner's door. */
  openLibrary(): void {
    this.update(() => (this.menu = undefined))
    this.onAction?.({ case: 'openLibrary' })
  }

  // MARK: - The conversation

  /**
   * The empty state: no session launched yet (a preset that opens none, as
   * `monitor` until 1v), or a session taking turns with none sent -- the
   * launch's notices under it.
   */
  get isEmpty(): boolean {
    if (this.connection.case === 'idle') return true
    return this.connection.case === 'connected' && this.transcript.status.case === 'ready' && !this.transcript.items.some((item) => item.content.case !== 'notice')
  }

  /** The turn in flight with nothing of its answer drawn yet: the pending line. */
  get pending(): boolean {
    const turn = this.transcript.turn
    if (turn === undefined) return false
    return !this.transcript.items.some((item) => item.turn === turn && item.content.case === 'answer')
  }

  /** What the pending line and the composer's status say the turn is doing. */
  get statusWord(): string {
    switch (this.transcript.status.case) {
      case 'connecting':
        return this.connection.case === 'missing' ? 'apogee not found' : this.connection.case === 'idle' ? '' : 'starting apogee'
      case 'thinking':
        return 'thinking'
      case 'working':
        return 'working'
      case 'answering':
        return 'streaming'
      case 'waiting':
        return 'waiting for you'
      case 'stopping':
        return 'stopping'
      case 'ended':
        return 'session ended'
      case 'ready':
        return ''
    }
  }

  /** How long the turn has waited, on the clock, as the old pending line says it: `2.4`. */
  get pendingSeconds(): string {
    return ((this.clock.ticks - this.sentAt) * SessionViewModel.secondsPerTick).toFixed(1)
  }

  // MARK: - The composer

  setDraft(text: string): void {
    this.update(() => {
      this.draft = text
      this.menuIndex = 0
      this.menuDismissed = false
    })
  }

  /** Whether a turn is running, so Stop stands where Send does. */
  get streaming(): boolean {
    return this.transcript.turnInFlight || this.transcript.status.case === 'stopping'
  }

  /** Whether the draft can be sent now. */
  get canSend(): boolean {
    return this.transcript.acceptsInput && this.session !== undefined && this.draft.trim() !== ''
  }

  /** Whether there is a turn to stop. */
  get canCancel(): boolean {
    return this.transcript.turnInFlight && this.session !== undefined
  }

  /** Whether a file can be attached now: a session is there, or one is starting -- never one nobody launched (the monitor's, until 1v), where it would be held for good. */
  get canAttach(): boolean {
    return this.connection.case !== 'missing' && this.connection.case !== 'idle' && this.transcript.status.case !== 'ended'
  }

  /** What the input says: the model it messages, or why it is not the person's. */
  get placeholder(): string {
    switch (this.transcript.status.case) {
      case 'waiting':
        return 'Answer the question above'
      case 'ended':
        return this.transcript.launchRefusal ? 'apogee refused to start this session' : 'The session has ended'
      case 'connecting':
        if (this.connection.case === 'missing') return 'apogee isn’t installed'
        if (this.connection.case !== 'idle') return 'Starting apogee'
        return `Message ${this.model.name}${this.privateMode === 'nothingWritten' ? ' · private' : ''}`
      default:
        return `Message ${this.model.name}${this.privateMode === 'nothingWritten' ? ' · private' : ''}`
    }
  }

  /** Sends the draft as a turn, or runs the command it names. */
  submit(): void {
    const text = this.draft.trim()
    if (text === '') return
    if (text.startsWith('/') && !(this.isExecute && /^\/play\s/.test(text))) {
      this.update(() => {
        this.draft = ''
        this.menuIndex = 0
      })
      this.runCommand(text)
      return
    }
    if (!this.canSend) return
    this.update(() => {
      this.draft = ''
      this.menu = undefined
    })
    this.send(text)
  }

  private send(text: string): void {
    const session = this.session
    if (!session) return
    this.sentAt = this.clock.ticks
    this.update(() => this.transcript.userSent(text))
    void session.send(text).catch(() => {})
  }

  /** Stops the turn in flight through the protocol: it ends in its own result, and the session goes on. */
  cancel(): void {
    const session = this.session
    if (!this.canCancel || !session) return
    this.update(() => this.transcript.cancelling())
    void session.cancel().catch(() => {})
  }

  /** Whether the session takes a line now: no turn runs. */
  get takesLinesNow(): boolean {
    return this.session !== undefined && (this.transcript.status.case === 'ready' || this.transcript.status.case === 'connecting')
  }

  /** Attaches a file, folder or glob: its outcome arrives as the session's notices. Made mid-turn, it waits for the turn's end. */
  attach(path: string): void {
    if (!this.canAttach) return
    this.update(() => (this.menu = undefined))
    this.sendOrHold({ case: 'attach', path })
  }

  /** Detaches it, where the session takes `detach`. */
  detach(path: string): void {
    if (!this.transcript.accepts.includes('detach')) return
    this.sendOrHold({ case: 'detach', path })
  }

  /** Moves the run settings from the next turn, where the session takes `set`. */
  apply(change: RunSettings): void {
    if (!this.transcript.accepts.includes('set')) return
    this.update(() => (this.menu = undefined))
    this.sendOrHold({ case: 'set', settings: change })
  }

  /** The model menu's pick. */
  pickModel(name: string): void {
    this.apply({ model: name })
  }

  /** The think menu's mode. */
  pickThink(mode: string): void {
    this.apply({ think: mode })
  }

  /** The think menu's effort: `default` sets none. */
  pickEffort(effort: string): void {
    this.apply({ effort: effort === 'default' ? 'auto' : effort })
  }

  /** What the think menu says of when a change takes: the Mac app's footer for a session that takes `set`. */
  get settingsFooter(): string {
    if (!this.transcript.accepts.includes('set')) return 'This apogee changes these by restarting the session: the conversation carries on (--resume), and a local model loads again.'
    return this.takesLinesNow ? 'Applied from the next turn; apogee says each change.' : 'A turn is running: the change is sent when it ends, and applied from the turn after; apogee says each change.'
  }

  private sendOrHold(line: HeldLine): void {
    if (this.takesLinesNow) this.sendLine(line)
    else this.update(() => (this.held = [...this.held, line]))
  }

  private releaseHeld(): void {
    if (!this.takesLinesNow || this.held.length === 0) return
    const lines = this.held
    this.update(() => (this.held = []))
    for (const line of lines) this.sendLine(line)
  }

  private sendLine(line: HeldLine): void {
    const session = this.session
    if (!session) return
    switch (line.case) {
      case 'attach':
        this.update(() => (this.attachments = this.attachments.includes(line.path) ? this.attachments : [...this.attachments, line.path]))
        void session.attach(line.path).catch(() => {})
        return
      case 'detach':
        this.update(() => (this.attachments = this.attachments.filter((path) => path !== line.path)))
        void session.detach(line.path).catch(() => {})
        return
      case 'set':
        this.update(() => (this.settings = { ...this.settings, ...line.settings }))
        void session.set(line.settings).catch(() => {})
        return
      case 'compact':
        void session.compact().catch(() => {})
        return
    }
  }

  // MARK: - The `/` and `@` menus

  /** The `/` menu's rows, while the draft names a command: the commands it starts, or the symphonies after `/play `. */
  get slashItems(): readonly SlashItem[] | undefined {
    const play = this.isExecute ? /^\/play ([^\s]*)$/.exec(this.draft) : null
    if (play) {
      const typed = (play[1] ?? '').toLowerCase()
      return this.plays.filter((choice) => choice.name.startsWith(typed)).map((choice) => ({ name: `play ${choice.name}`, args: '', summary: choice.description, play: true }))
    }
    return SlashCommand.matching(this.draft, this.isExecute)?.map((command) => ({ ...command, play: false }))
  }

  /** What the `/` menu says it matched none of. */
  get slashQuery(): string {
    return this.draft.split(' ')[0] ?? this.draft
  }

  /** The `@` mention being typed: what follows the `@`. */
  get mentionQuery(): string | undefined {
    return /(^|\s)@([^\s]*)$/.exec(this.draft)?.[2]
  }

  /** The `@` menu's rows: the folder's files whose path holds what is typed, at most 7. */
  get mentionItems(): readonly FolderFile[] | undefined {
    const query = this.mentionQuery
    if (query === undefined) return undefined
    const typed = query.toLowerCase()
    return this.listing.files.filter((file) => file.path.toLowerCase().includes(typed)).slice(0, 7)
  }

  get slashOpen(): boolean {
    return this.slashItems !== undefined && !this.menuDismissed && this.menu !== 'attach'
  }

  get mentionOpen(): boolean {
    return this.mentionItems !== undefined && !this.menuDismissed && !this.slashOpen
  }

  /** The highlighted row of the open `/` or `@` menu. */
  get activeIndex(): number {
    const count = (this.slashOpen ? this.slashItems?.length : this.mentionOpen ? this.mentionItems?.length : 0) ?? 0
    return count === 0 ? 0 : ((this.menuIndex % count) + count) % count
  }

  /** ↑ and ↓ in an open `/` or `@` menu; whether one was open to move in. */
  moveSelection(by: number): boolean {
    if (!this.slashOpen && !this.mentionOpen) return false
    this.update(() => (this.menuIndex += by))
    return true
  }

  /** ⇥ or ⏎ in an open `/` or `@` menu: its highlighted row picked; whether there was one. */
  completeSelection(): boolean {
    if (this.slashOpen) {
      const item = this.slashItems?.[this.activeIndex]
      if (!item) return false
      this.pickSlash(item)
      return true
    }
    if (this.mentionOpen) {
      const file = this.mentionItems?.[this.activeIndex]
      if (!file) return false
      this.pickMention(file)
      return true
    }
    return false
  }

  /** Escape: the menus closed. */
  dismissMenus(): void {
    this.update(() => {
      this.menuDismissed = true
      this.menu = undefined
    })
  }

  /** A `/` row picked: a symphony completes the line; a command with nothing to take runs; one that takes something waits for it. */
  pickSlash(item: SlashItem): void {
    if (item.play || item.args !== '') {
      this.setDraft(`/${item.name} `)
      return
    }
    this.setDraft(`/${item.name}`)
    this.submit()
  }

  /** An `@` row picked: the file attached, and the mention taken out of the draft (the old prototype's). */
  pickMention(file: FolderFile): void {
    this.update(() => (this.draft = this.draft.replace(/(^|\s)@[^\s]*$/, '$1')))
    this.attach(file.path)
  }

  /** A slash command, as the window acts on it: a line or a door that exists, else said as the old prototype says it. */
  private runCommand(text: string): void {
    const words = text.slice(1).trim().split(/\s+/)
    const command = (words.shift() ?? '').toLowerCase()
    const argument = words.join(' ').trim()
    const say = (said: string) => this.onAction?.({ case: 'say', text: said })
    switch (command) {
      case 'model': {
        if (argument === '') {
          this.toggleMenu('model')
          return
        }
        const hit = this.modelRows.find((row) => row.backend.toLowerCase() === argument.toLowerCase())
        if (hit) this.pickModel(hit.backend)
        else say(`no model called “${argument}” — /model alone opens the list`)
        return
      }
      case 'models':
        this.openLibrary()
        return
      case 'suite':
        this.chooseSuite()
        return
      case 'think':
        if (argument === 'on' || argument === 'off' || argument === 'auto') this.pickThink(argument)
        else this.toggleMenu('think')
        return
      case 'compact':
        if (this.transcript.accepts.includes('compact')) this.sendOrHold({ case: 'compact' })
        return
      case 'attach':
        if (argument === '') this.toggleMenu('attach')
        else this.attach(argument)
        return
      case 'detach':
        if (argument !== '') this.detach(argument)
        return
      case 'system':
        if (argument !== '') this.apply({ system: argument })
        return
      case 'temperature': {
        const value = Number(argument)
        if (argument !== '' && Number.isFinite(value)) this.apply({ temperature: value })
        return
      }
      case 'symphonies':
        if (this.isExecute) this.setDraft('/play ')
        return
      case 'help':
        say(`commands: ${SlashCommand.all(this.isExecute).map((entry) => `/${entry.name}`).join(' ')}`)
        return
    }
    if (SlashCommand.all(true).some((entry) => entry.name === command)) say(`/${command}${argument ? ` ${argument}` : ''} — noted; this prototype doesn’t act on it`)
    else say(`/${command} isn’t a command — type / to see them`)
  }

  // MARK: - The suite

  /** Choose the suite: the owner reads the suites, then presents the chooser. */
  chooseSuite(): void {
    if (!this.canSwitchSuite) return
    this.update(() => (this.menu = undefined))
    this.onAction?.({ case: 'chooseSuite' })
  }

  /** The chooser opened over the suites read, on `choosing` where the caller names one, else the suite it runs under. */
  presentSuites(choices: readonly SuiteChoice[], defaultSuite: string | undefined, choosing?: string, forcing = false): void {
    this.update(() => {
      this.suiteChoices = choices
      this.defaultSuite = defaultSuite
      this.suiteChooser = { chosen: choosing ?? this.runningSuite ?? '', force: forcing }
    })
  }

  /** A row of the chooser picked. */
  chooseRow(name: string): void {
    this.update(() => (this.suiteChooser = { chosen: name, force: false }))
  }

  /** Run it anyway (--force). */
  toggleForce(): void {
    const chooser = this.suiteChooser
    if (!chooser) return
    this.update(() => (this.suiteChooser = { ...chooser, force: !chooser.force }))
  }

  /** The chosen suite is one the CLI refuses unless forced. */
  get chosenOverBudget(): boolean {
    const chosen = this.suiteChooser?.chosen
    const choice = this.suiteChoices.find((entry) => entry.name === chosen)
    return choice !== undefined && SuiteChoice.overBudget(choice)
  }

  /**
   * Switch stays off with nothing chosen, the running suite chosen, or one
   * over this machine's memory not forced -- and, in this part, once a turn
   * has been sent: moving a conversation restarts its child under the suite
   * (`--resume`), and Restarting is 1v's.
   */
  get canSwitch(): boolean {
    const chooser = this.suiteChooser
    if (!chooser || chooser.chosen === '' || !this.canSwitchSuite || this.hasTurns) return false
    if (chooser.chosen === this.runningSuite && !(this.chosenOverBudget && chooser.force)) return false
    return !(this.chosenOverBudget && !chooser.force)
  }

  /** Cancel: the chooser closed, nothing moved. */
  dismissSuites(): void {
    this.update(() => (this.suiteChooser = undefined))
  }

  /** Switch: machine mode carries no suite, so the session starts again under it (`--suite`, and `--force` where forced). */
  async switchSuite(): Promise<void> {
    const chooser = this.suiteChooser
    if (!chooser || !this.canSwitch) return
    const forced = chooser.force && this.chosenOverBudget
    this.update(() => (this.suiteChooser = undefined))
    const old = this.session
    this.launch = this.launch.execute
      ? { ...this.launch, execute: { ...this.launch.execute, suite: chooser.chosen }, forceSuite: forced }
      : { ...this.launch, suite: chooser.chosen, forceSuite: forced }
    this.session = undefined
    this.connection = { case: 'idle' }
    await old?.close().catch(() => {})
    await this.start()
  }

  // MARK: - Leaving

  /** Leaving for `then`: asked first when a private conversation would be lost, as the old `leaveFor` asks. */
  leave(then: SessionLeave): void {
    if (this.isLostIfEnded) {
      this.update(() => (this.pendingLeave = then))
      this.onAction?.({ case: 'ask', kind: 'endPrivate' })
      return
    }
    this.onAction?.({ case: 'leave', then })
  }

  /** The `endPrivate` confirm's words, while it asks: one body while a turn runs, another while none does. */
  get endPrivate(): EndPrivate | undefined {
    if (!this.pendingLeave) return undefined
    const body = this.streaming
      ? 'Nothing of it is written: switching stops the turn running in it and ends this session\'s apogee, and the conversation goes with it. Keep it running, or end it?'
      : 'Nothing of it is written: switching ends this session\'s apogee, and the conversation goes with it. Keep it running, or end it?'
    return { title: 'This private conversation can\'t be reopened', body, confirm: 'End it and switch', cancel: 'Keep it running' }
  }

  /** End it and switch: the session ended, a turn in flight stopped, then the leave carried out. */
  async endAndLeave(): Promise<void> {
    const then = this.pendingLeave
    if (!then) return
    this.update(() => (this.pendingLeave = undefined))
    await this.stop()
    this.onAction?.({ case: 'leave', then })
  }

  /** Keep it running: everything as it was. */
  keepRunning(): void {
    this.update(() => (this.pendingLeave = undefined))
  }

  // MARK: - What the views read

  /** The rows of the conversation. */
  get items(): readonly TranscriptItem[] {
    return this.transcript.items
  }

  /** Everything the views draw, as words. */
  face(): SessionFace {
    const meter = this.meter
    const state = this.connection.case === 'missing' ? 'missing' : this.isEmpty ? 'empty' : this.connection.case !== 'connected' ? 'starting' : 'conversation'
    const privateMode = this.privateMode
    return {
      state,
      header: {
        title: this.title,
        privateBadge: privateMode === 'nothingWritten',
        banner: this.executeBanner ?? '',
        suiteWords: this.isExecute ? '' : (this.suiteWords ?? ''),
        canSwitchSuite: this.canSwitchSuite,
        model: this.model,
        think: this.thinkLabel,
        meter: {
          known: meter.percent !== undefined,
          text: meter.text,
          percent: meter.percent ?? 0,
          used: meter.used === undefined ? '' : ContextMeter.short(meter.used),
          window: meter.window === undefined ? '' : ContextMeter.short(meter.window),
          warns: meter.warns,
        },
        inspectorOpen: this.inspectorOpen,
      },
      empty: {
        heading: privateMode ? 'Private session' : 'What are we working on?',
        privateWords: privateMode ? PrivateMode.words(privateMode) : '',
        notices: this.transcript.items.flatMap((item) => (item.content.case === 'notice' ? [item.content.text] : [])),
        launched: this.connection.case !== 'idle',
        privateInstead: privateMode === undefined && this.connection.case !== 'idle',
        chooseSuite: !this.isExecute && this.canSwitchSuite,
      },
      rows: this.transcript.items.map((item) => this.rowFace(item)),
      pending: { shown: this.pending, word: this.statusWord, seconds: this.pending ? this.pendingSeconds : '0.0', model: this.model },
      suiteSheet: this.suiteSheet(),
      composer: {
        draft: this.draft,
        placeholder: this.placeholder,
        status: this.statusWord,
        streaming: this.streaming,
        canSend: this.canSend,
        canAttach: this.canAttach,
        attachments: this.attachments,
      },
    }
  }

  /** The chooser's rows: each configured suite with the CLI's verdict, then No suite for a chat (an `execute` session takes named suites only). */
  suiteSheet(): SessionFace['suiteSheet'] {
    const chooser = this.suiteChooser
    const running = this.runningSuite
    const rows = this.suiteChoices.map((choice) => ({
      name: choice.name,
      title: choice.name + (choice.isDefault ? ' (the default)' : ''),
      verdict: choice.footprint?.verdictWords ?? '',
      over: SuiteChoice.overBudget(choice),
      running: choice.name === running,
      chosen: choice.name === chooser?.chosen,
    }))
    if (!this.isExecute) {
      rows.push({ name: noSuite, title: 'No suite', verdict: 'the roles follow the global pointers', over: false, running: running === noSuite, chosen: chooser?.chosen === noSuite })
    }
    const chosen = this.suiteChoices.find((choice) => choice.name === chooser?.chosen)
    return {
      open: chooser !== undefined,
      now: `Now: ${this.suiteWords ?? 'the suite it last had, as apogee keeps it'}`,
      rows,
      overBudget: this.chosenOverBudget,
      force: chooser?.force ?? false,
      total: this.chosenOverBudget ? (chosen?.footprint?.total ?? '') : '',
      canSwitch: this.canSwitch,
    }
  }

  private rowFace(item: TranscriptItem): RowFace {
    const content = item.content
    switch (content.case) {
      case 'user':
      case 'notice':
        return { id: item.id, kind: content.case, text: content.text }
      case 'stopped':
        return { id: item.id, kind: 'quiet', text: 'stopped' }
      case 'unknown':
        return { id: item.id, kind: 'quiet', text: `apogee said something this app does not know yet: ${content.type}` }
      case 'ended':
        return { id: item.id, kind: 'ended', text: content.ending.message, atLaunch: content.ending.atLaunch }
      case 'activity': {
        const activity = content.activity
        const play = Transcript.play(activity)
        const steps = activity.lines.length
        const summary = play
          ? play.summary(activity.live)
          : [activity.live ? 'thinking' : 'thought', ...(activity.budgetReached ? ['budget reached'] : []), ...(steps > 0 ? [`${steps} ${steps === 1 ? 'step' : 'steps'}`] : [])].join(' · ')
        return {
          id: item.id,
          kind: 'activity',
          summary,
          live: activity.live,
          reasoning: activity.reasoning,
          lines: activity.lines.map((line) => {
            const stage = PlayNarration.stage(line)
            return { mark: stage ? `${stage.index}/${stage.count}` : '', text: line }
          }),
        }
      }
      case 'answer': {
        const answer = content.answer
        const usage = answer.usage
        const counts = [
          ...(usage?.inputTokens === undefined ? [] : [`in ${usage.inputTokens.toLocaleString('en-US')}`]),
          ...(usage?.outputTokens === undefined ? [] : [`out ${usage.outputTokens.toLocaleString('en-US')}`]),
        ]
        const model = this.modelFace(answer.model)
        return { id: item.id, kind: 'answer', text: answer.text, finished: answer.finished, footer: { model: answer.model ?? '', kind: model.kind, usage: counts } }
      }
    }
  }

  private update(change: () => void): void {
    change()
    this.changed()
  }
}
