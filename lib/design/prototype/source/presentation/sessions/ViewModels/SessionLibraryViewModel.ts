import type { JobStarting } from '../../../business/jobs/SupervisedJob'
import { ChatDetails, ChatSummary, RecallListing } from '../../../business/reads/Chats'
import { CommandOutcome } from '../../../business/reads/CommandRunning'
import type { CommandRunning } from '../../../business/reads/CommandRunning'
import { Field, ModelInfo, ModelKind, ModelRow } from '../../../business/reads/Models'
import { ReadOutcome } from '../../../business/reads/ReadRunner'
import { SuiteFootprint, Suites } from '../../../business/reads/SuiteFootprint'
import { Symphony } from '../../../business/reads/Symphonies'
import type { ChatLaunch } from '../../../business/session/ChatLaunch'
import type { ChatStarting, SessionEvent } from '../../../business/session/ChatSession'
import { Compactions } from '../../session/ViewModels/Compactions'
import type { PlayChoice } from '../../session/ViewModels/Play'
import type { SuiteChoice } from '../../session/ViewModels/SessionActions'
import { SessionViewModel } from '../../session/ViewModels/SessionViewModel'
import type { AttachChoice, FolderListing, SessionAction, SessionLeave, Ticks } from '../../session/ViewModels/SessionViewModel'
import type { KeyChord } from '../../shell/ViewModels/KeyMap'
import { Observable } from '../../styles/Observation/Observable'
import { ChatGroup } from './ChatGroup'
import type { ChatGroupTitle } from './ChatGroup'
import type { ChatInfo, ChatRowFace, ConfirmWords, DoorFace, LibraryConfirm, LibrarySheet, ListFace, Listing, Pending, Renaming, RowAct } from './LibraryFaces'
import { ExecuteForm } from './ExecuteForm'
import { Inspector, inspectorSections } from './Inspector'
import type { InspectorFace, InspectorSection } from './Inspector'
import { InspectorTurn } from './InspectorTurn'
import { SessionKeys } from './SessionKeys'

/** The door's note: what opening in a folder brings, in the CLI's own terms (the old prototype's words). */
const doorNote =
  'Opened in a project -- a git repository, or a folder holding AGENTS.md or CLAUDE.md -- the session reads the file into every request, carries the coder persona, and attaches the folder; its notices say so.'

/**
 * The Sessions place (design 1g): the saved conversations, the one open, the
 * inspector beside it and the execute door. The Mac app's
 * `SessionLibraryViewModel` (`PresentationSessions`), its names, over the
 * stand-in's services: plain TypeScript that never imports React.
 *
 * Two laws, kept from the Mac: one live chat child -- opening another ends the
 * open one's first -- and read back, don't mirror: the list is `chats list`
 * as last read, read again after every turn and every change the place makes.
 * The model facts, the suites and a saved chat's earlier turns, which 1u's
 * composition read for the session, are read here, as the Mac's library reads
 * them (`SessionLibraryViewModel.swift:506-533`).
 */
export class SessionLibraryViewModel extends Observable {
  chats: readonly ChatSummary[] = []
  listing: Listing = { case: 'notRead' }
  /** What the list is filtered by: a part of a name, any case. */
  query = ''
  /** The open session. */
  current: SessionViewModel
  /** Which session this is, counted from the first: a new session is a new view (the Mac's `.id(ObjectIdentifier(current))`). */
  generation = 0
  pending: Pending | undefined = undefined
  /** The roles, as `models status` reads them under the open session's own suite. */
  roles: readonly Field[] = []
  rolesFailure: string | undefined = undefined
  /** Moves when the person asked to search (⇧⌘F): the field takes focus. */
  searchRequests = 0
  /** The saved conversation being renamed, and the name typed: the rename sheet's. */
  private renamingState: { readonly chat: ChatSummary; readonly name: string } | undefined = undefined
  /** A saved conversation's details, read for its sheet. */
  info: ChatInfo | undefined = undefined
  executeDoorOpen = false
  executeForm: ExecuteForm = ExecuteForm.empty()
  /** The configured suites, `config get suites` as last read. */
  suites: readonly string[] = []
  /** `models.default_suite`: empty when none is set, `undefined` unread. */
  defaultSuite: string | undefined = undefined
  recall: RecallListing | undefined = undefined
  recallFailure: string | undefined = undefined
  recallSelection: string | undefined = undefined
  /** Saved chats compacting as jobs (GUI 6b): a row in it is busy, and can be stopped. */
  readonly compactions: Compactions

  /** The list's own folds, as the old sidebar keeps them: SESSIONS open, Today and Yesterday open. */
  listOpen = true
  groupsOpen: Readonly<Record<ChatGroupTitle, boolean>> = { Today: true, Yesterday: true, 'Previous 7 days': false, Earlier: false }
  /** The inspector's sections, open as the old `insOpen` opens them. */
  sections: Readonly<Record<InspectorSection, boolean>> = { session: true, turn: true, context: true, model: true, helpers: false, recall: false }
  /** The row whose menu is open, by its chat. */
  rowMenu: string | undefined = undefined
  /** Whether Sessions is the place shown: only there is the open session's row current, as the old `s.screen === 'chat'` test has it. */
  onSessions = true

  /** Says one line in the toast. */
  say: ((text: string) => void) | undefined = undefined
  /** Opens a sheet in the shell's host. */
  present: ((sheet: LibrarySheet) => void) | undefined = undefined
  /** Closes the sheet the shell's host shows. */
  dismissSheet: (() => void) | undefined = undefined
  /** Asks a confirm in the shell's host: the library's, and the session's `endPrivate`. */
  ask: ((kind: LibraryConfirm | 'endPrivate') => void) | undefined = undefined
  /** Shows Sessions, any overlay closed, as the old `sessionGo` does. */
  showSessions: (() => void) | undefined = undefined
  /** Shows Sessions, an open overlay (the console, the monitor) left as it is, as the old row menu's `act` does. */
  goToSessions: (() => void) | undefined = undefined
  /** The Models place, from the inspector and the session's model menu. */
  /** Models, on the tab a door names: Backends at a model (Open in library), or Roles (Change helpers) (design 1h). */
  openModels: ((tab?: 'backends' | 'roles', model?: string) => void) | undefined = undefined
  /** Told when a row menu opens or closes, so the shell's scrim covers the window while it is open. */
  menuChanged: ((owner: string | undefined) => void) | undefined = undefined

  private binary: string | undefined = undefined
  private kinds: Readonly<Record<string, ModelKind>> = {}
  private rows: readonly ModelRow[] = []
  private modelsRead: Promise<void> | undefined = undefined
  /** The open session's latest turn, as its events said it. */
  private latest: InspectorTurn | undefined = undefined
  /** `models info <model>`'s fields for the model the open session runs on. */
  private modelInfo: { readonly model: string; readonly fields: readonly Field[] } | undefined = undefined
  /** The symphonies an execute session's `/play` offers. */
  private plays: readonly PlayChoice[] = []
  /** The session whose inspector's reads were made, and the suite its roles were read under. */
  private inspected: { readonly session: SessionViewModel; readonly suite: string | undefined } | undefined = undefined

  constructor(
    private readonly starter: ChatStarting,
    private readonly runner: CommandRunning,
    jobs: JobStarting,
    private readonly clock: Ticks & { readonly now: Date },
    private readonly samples: { readonly listing: FolderListing; readonly attachChoices: readonly AttachChoice[] } = { listing: { folder: '', files: [] }, attachChoices: [] },
  ) {
    super()
    this.compactions = new Compactions(jobs, () => this.binary)
    this.compactions.onSay = (text) => this.say?.(text)
    this.compactions.subscribe(() => this.changed())
    this.current = this.make({})
  }

  // MARK: - The place

  /** The app started: the binary found, the list and the model facts read. No session is opened here: the preset opens its own. */
  async start(): Promise<void> {
    this.update(() => (this.listing = { case: 'reading' }))
    const found = await this.starter.locateBinary()
    if (found.case === 'notFound') {
      this.update(() => (this.listing = { case: 'missing' }))
      return
    }
    this.binary = found.binary
    await this.refresh()
    await this.readModels()
  }

  /** Reads `chats list` again, and names the open session from it. */
  async refresh(): Promise<void> {
    const binary = this.binary
    if (binary === undefined) return
    const read = await this.runner.read(binary, ['chats', 'list'])
    this.update(() => {
      if (read.case === 'document') {
        this.chats = ChatSummary.list(read.document)
        this.listing = { case: 'read' }
      } else this.listing = { case: 'failed', message: ReadOutcome.failure(read) ?? 'the list could not be read' }
    })
    this.nameCurrent()
  }

  /** `models list`, once: each backend's kind (the glyphs) and the backends a chat runs on, handed to every session. */
  private readModels(): Promise<void> {
    const binary = this.binary
    if (binary === undefined) return Promise.resolve()
    this.modelsRead ??= this.runner.read(binary, ['models', 'list']).then((read) => {
      if (read.case !== 'document') return
      const rows = ModelRow.list(read.document)
      const chat = ModelRow.chatBackends(rows)
      this.kinds = ModelKind.byBackend(read.document)
      this.rows = rows.filter((row) => chat.includes(row.backend))
      this.changed()
    })
    return this.modelsRead
  }

  // MARK: - Opening

  /**
   * Opens `launch` in place of the open session: the one before it ended
   * first -- never two children at once -- then the new one started, its
   * model facts handed in; a new or private chat's default suite read, so its
   * header names the suite it runs under; an execute session given its
   * symphonies. The inspector stays as it was.
   */
  async replace(launch: ChatLaunch): Promise<void> {
    const old = this.current
    const session = this.make(launch)
    session.inspectorOpen = old.inspectorOpen
    this.update(() => {
      this.current = session
      this.generation += 1
      this.latest = undefined
      this.modelInfo = undefined
    })
    await old.stop()
    await session.start()
    if (this.binary === undefined || session.connection.case !== 'connected') return
    await this.readModels()
    if (session !== this.current) return
    session.modelKinds = this.kinds
    session.modelRows = this.rows
    if (launch.resume === undefined && launch.execute === undefined) {
      const named = await this.runner.command(this.binary, ['config', 'get', 'models.default_suite'])
      if (session === this.current) session.knowDefaultSuite(named.case === 'done' ? named.stdout.trim() : undefined)
    }
    if (launch.execute) {
      if (this.plays.length === 0) await this.readPlays()
      session.plays = this.plays
    }
    this.nameCurrent()
  }

  /** `symphonies list`: what an execute session's `/play` offers. */
  private async readPlays(): Promise<void> {
    if (this.binary === undefined) return
    const read = await this.runner.read(this.binary, ['symphonies', 'list'])
    if (read.case === 'document') this.plays = Symphony.list(read.document)
  }

  /** A session over the chat starter, its acts handed to the library and its events read. */
  private make(launch: ChatLaunch): SessionViewModel {
    const session = new SessionViewModel(this.starter, this.clock, launch, this.samples.listing, this.samples.attachChoices, this.runner)
    session.onAction = (action) => this.act(action)
    session.observer = (event) => this.observe(session, event)
    return session
  }

  /** The open session's name: the list's, for the chat its session event names. */
  private nameCurrent(): void {
    const chat = this.currentChat
    if (chat) this.current.knowName(chat.name)
  }

  /** The open session's saved row, once the list holds the chat its session names. */
  get currentChat(): ChatSummary | undefined {
    if (this.current.privateMode === 'nothingWritten') return undefined
    const id = this.current.transcript.chatID ?? this.current.launchSettings.resume
    return id === undefined ? undefined : this.chats.find((chat) => chat.id === id)
  }

  /** Whether `chat` is the open session. */
  isOpen(chat: ChatSummary): boolean {
    return this.currentChat?.id === chat.id
  }

  /**
   * What the session says, after it has folded it: its latest turn kept for
   * the inspector; its model's window read (`models info <model>`) when its
   * `session` event names the model, and again when an answer names another;
   * the list read back once a turn has ended, and the roles again when the
   * session's suite moved.
   */
  private observe(session: SessionViewModel, event: SessionEvent): void {
    if (session !== this.current) return
    this.update(() => (this.latest = InspectorTurn.apply(this.latest, event)))
    if (event.case !== 'event' || this.binary === undefined) return
    const kind = event.event.kind
    if (kind.case === 'session') {
      this.nameCurrent()
      // The inspector's reads, once the session has said the suite it runs under; again for the roles when it moved.
      if (session.inspectorOpen && this.inspected?.session !== session) void this.inspectorAppeared()
      else if (this.inspected?.session === session && this.inspected.suite !== session.runningSuite) void this.refreshRoles()
    }
    const model = kind.case === 'session' ? kind.session.model : kind.case === 'result' ? kind.result.model : undefined
    if (kind.case === 'result' || kind.case === 'error') void this.refresh()
    if (model === undefined || model === '' || (kind.case === 'result' && model === session.windowOf)) return
    session.windowOf = model
    void this.runner.read(this.binary, ['models', 'info', model]).then((read) => {
      if (session.transcript.model !== model) return
      session.contextWindow = read.case === 'document' ? ModelInfo.window(read.document) : undefined
      if (session === this.current) this.update(() => (this.modelInfo = { model, fields: read.case === 'document' ? Field.list(read.document) : [] }))
    })
  }

  /** An act the open session hands up. */
  private act(action: SessionAction): void {
    switch (action.case) {
      case 'chooseSuite':
        void this.chooseSuite()
        return
      case 'openLibrary':
        this.openModels?.('backends', this.current.face().header.model.name)
        return
      case 'say':
        this.say?.(action.text)
        return
      case 'ask':
        this.ask?.(action.kind)
        return
      case 'leave':
        void this.carryOut(action.then)
        return
    }
  }

  /**
   * A leave carried out (the old `sessionGo`): a new chat, a private one or a
   * saved one opened on Sessions, any overlay closed; the execute door opened
   * over the place shown, fresh.
   */
  async carryOut(then: SessionLeave): Promise<void> {
    if (then.type === 'execute') {
      await this.openExecuteDoor()
      return
    }
    this.showSessions?.()
    switch (then.type) {
      case 'new':
        await this.replace(then.suite === undefined ? {} : { suite: then.suite, forceSuite: then.force ?? false })
        return
      case 'private':
        await this.replace({ privately: true })
        return
      case 'open':
        if (this.currentChat?.id === then.id) return
        await this.replace({ resume: then.id })
        return
    }
  }

  /** ⌘N, ⇧⌘N, a row: asked through the open session first, as the old `leaveFor` asks, where a private conversation would be lost. */
  newSession(): void {
    this.current.leave({ type: 'new' })
  }

  newPrivateSession(): void {
    this.current.leave({ type: 'private' })
  }

  /** A row clicked: its chat opened, asked first where a private conversation would be lost. */
  open(id: string): void {
    this.closeRowMenu()
    this.current.leave({ type: 'open', id })
  }

  /** The private row clicked: the private session open now, on Sessions. */
  showPrivate(): void {
    this.showSessions?.()
  }

  // MARK: - The suite chooser

  /**
   * The suite chooser for the open session (`/suite`, the header, Choose a
   * suite): the suites and the default read (`config get suites`, `config get
   * models.default_suite`), the CLI's verdict on each (`models status --suite
   * <name>`), then shown -- the Mac library's `chooseSuite()`. A failed list is
   * said in the toast in the CLI's words, never drawn as none configured.
   */
  async chooseSuite(choosing?: string): Promise<void> {
    const binary = this.binary
    const session = this.current
    if (binary === undefined || !session.canSwitchSuite) return
    const listed = await this.runner.command(binary, ['config', 'get', 'suites'])
    if (listed.case !== 'done') {
      this.say?.(CommandOutcome.failure(listed) ?? 'apogee could not list its suites')
      return
    }
    const names = Suites.names(listed.stdout)
    const named = await this.runner.command(binary, ['config', 'get', 'models.default_suite'])
    const defaultSuite = named.case === 'done' ? named.stdout.trim() : undefined
    const choices: SuiteChoice[] = []
    for (const name of names) {
      const read = await this.runner.read(binary, ['models', 'status', '--suite', name])
      choices.push({ name, footprint: read.case === 'document' ? SuiteFootprint.of(read.document) : undefined, isDefault: name === defaultSuite })
    }
    if (session !== this.current) return
    session.presentSuites(choices, defaultSuite, choosing)
    this.present?.('suite')
  }

  // MARK: - The list

  /** The place shown changed: whether it is Sessions, where the open session's row and the private row are drawn current. */
  showing(onSessions: boolean): void {
    if (onSessions !== this.onSessions) this.update(() => (this.onSessions = onSessions))
  }

  setQuery(query: string): void {
    this.update(() => (this.query = query))
  }

  /** ⇧⌘F: the list opens and its field takes focus. */
  requestSearch(): void {
    this.update(() => {
      this.listOpen = true
      this.searchRequests += 1
    })
  }

  toggleList(): void {
    this.update(() => (this.listOpen = !this.listOpen))
  }

  toggleGroup(title: ChatGroupTitle): void {
    this.update(() => (this.groupsOpen = { ...this.groupsOpen, [title]: !this.groupsOpen[title] }))
  }

  /** The old `collapseDates`: every date group closed if any is open, else every one opened; the list opened. */
  toggleDates(): void {
    const open = !ChatGroup.order.some((title) => this.groupsOpen[title])
    this.update(() => {
      this.listOpen = true
      this.groupsOpen = { Today: open, Yesterday: open, 'Previous 7 days': open, Earlier: open }
    })
  }

  /** The kind of model a saved conversation runs on, when its row names its backend and that backend's kind is known. */
  kind(chat: ChatSummary): ModelKind | undefined {
    return chat.backend === undefined ? undefined : this.kinds[chat.backend]
  }

  toggleRowMenu(id: string): void {
    const owner = this.rowMenu === id ? undefined : id
    this.update(() => (this.rowMenu = owner))
    this.menuChanged?.(owner === undefined ? undefined : `row:${owner}`)
  }

  closeRowMenu(): void {
    if (this.rowMenu === undefined) return
    this.update(() => (this.rowMenu = undefined))
    this.menuChanged?.(undefined)
  }

  /**
   * A row menu's act, its menu closed: Sessions shown with the console or the
   * monitor left open, as the old row menu's `act` sets `{ screen: 'chat',
   * menu: null }`; Stop compacting only says, on the place shown, as the old
   * `stopCompacting` does.
   */
  rowAct(id: string, act: RowAct): void {
    const chat = this.chats.find((entry) => entry.id === id)
    this.closeRowMenu()
    if (!chat) return
    if (act === 'stopCompacting') {
      void this.compactions.stopCompacting(chat.id)
      return
    }
    this.goToSessions?.()
    switch (act) {
      case 'details':
        void this.showInfo(chat)
        return
      case 'rename':
        this.askToRename(chat)
        return
      case 'compact':
        this.askToCompact(chat)
        return
      case 'delete':
        this.askToDelete(chat)
        return
    }
  }

  /** Everything the list draws. */
  listFace(): ListFace {
    const needle = this.query.trim().toLowerCase()
    // A private session on the fallback is saved until it closes: never listed.
    const listed = this.current.privateMode === 'deletedOnClose' ? this.chats.filter((chat) => chat.id !== this.current.transcript.chatID) : this.chats
    const shown = needle === '' ? listed : listed.filter((chat) => chat.name.toLowerCase().includes(needle))
    // Current only on Sessions, as the old list draws it; the Mac highlights the open row on every place.
    const current = this.onSessions ? this.currentChat?.id : undefined
    const groups = ChatGroup.grouping(shown, this.clock.now).map((group) => {
      const open = needle !== '' || this.groupsOpen[group.title]
      return {
        title: group.title,
        count: group.chats.length,
        open,
        hasCurrent: !open && group.chats.some((chat) => chat.id === current),
        rows: group.chats.map((chat) => this.rowFace(chat, current)),
      }
    })
    const anyDateOpen = ChatGroup.order.some((title) => this.groupsOpen[title])
    const matching = groups.reduce((count, group) => count + group.count, 0)
    return {
      open: this.listOpen,
      count: listed.length,
      anyDateOpen,
      datesLabel: anyDateOpen ? 'Collapse every date group' : 'Expand every date group',
      query: this.query,
      privateRow: { shown: this.current.privateMode !== undefined && this.current.connection.case !== 'idle', current: this.onSessions },
      groups,
      noResults: needle !== '' && matching === 0 ? `No sessions match “${this.query.trim()}”.` : '',
      matchText: needle !== '' && matching > 0 ? `${matching} matching · groups open while you search` : '',
      empty: this.emptyText,
      searchRequests: this.searchRequests,
    }
  }

  /** What the list says when it has no rows for a reason other than a search: the Mac app's `emptyText`. */
  private get emptyText(): string {
    switch (this.listing.case) {
      case 'missing':
        return 'Apogee isn’t installed, so there are no sessions to list.'
      case 'failed':
        return `The sessions could not be read: ${this.listing.message}`
      case 'read':
        return this.chats.length === 0 ? 'No saved sessions yet. Each one is listed here after its first turn.' : ''
      default:
        return ''
    }
  }

  private rowFace(chat: ChatSummary, current: string | undefined): ChatRowFace {
    const busy = this.compactions.isCompacting(chat.id)
    return {
      id: chat.id,
      title: chat.name,
      time: busy ? '' : ChatGroup.when(chat, this.clock.now),
      busy,
      current: chat.id === current,
      kind: this.kind(chat) ?? 'unknown',
      help: busy ? `${chat.name} · compacting: apogee chats compact ${chat.id} --yes` : `${chat.name} · ${chat.backend ?? 'the default backend'}`,
      menuOpen: this.rowMenu === chat.id,
      items: [
        { label: 'Details…', act: 'details', destructive: false },
        { label: 'Rename…', act: 'rename', destructive: false },
        busy ? { label: 'Stop compacting', act: 'stopCompacting', destructive: false } : { label: 'Compact…', act: 'compact', destructive: false },
        { label: 'Delete…', act: 'delete', destructive: true },
      ],
    }
  }

  // MARK: - Changing

  /** `chats info <id>`: the details the CLI states of a saved conversation, for its sheet. */
  async showInfo(chat: ChatSummary): Promise<void> {
    const binary = this.binary
    if (binary === undefined) return
    const read = await this.runner.read(binary, ['chats', 'info', chat.id])
    const details = read.case === 'document' ? ChatDetails.of(read.document) : undefined
    this.update(() => {
      this.info = {
        chat,
        title: details?.title || chat.name,
        fields: details?.fields ?? [],
        failure: details ? '' : (ReadOutcome.failure(read) ?? 'apogee did not say what it holds of this chat'),
        command: `apogee chats info ${chat.id}`,
      }
    })
    this.present?.('chatInfo')
  }

  closeInfo(): void {
    this.update(() => (this.info = undefined))
    this.dismissSheet?.()
  }

  /** Rename…: the sheet opens on the chat's name. */
  askToRename(chat: ChatSummary): void {
    this.update(() => (this.renamingState = { chat, name: chat.name }))
    this.present?.('rename')
  }

  get renaming(): Renaming | undefined {
    const state = this.renamingState
    if (!state) return undefined
    return { chat: state.chat, name: state.name, canRename: state.name.trim() !== '', command: `apogee chats title ${state.chat.id} …` }
  }

  setRenameName(name: string): void {
    const state = this.renamingState
    if (!state) return
    this.update(() => (this.renamingState = { ...state, name }))
  }

  cancelRename(): void {
    this.update(() => (this.renamingState = undefined))
    this.dismissSheet?.()
  }

  /**
   * Rename: `chats title <id> -- <name>`, read back -- the name after `--`, so
   * the binary never reads it as a flag -- and said. A running session keeps
   * a rename made beside it (the CLI's 55b), so nothing is paused.
   */
  async rename(): Promise<void> {
    const state = this.renamingState
    const binary = this.binary
    if (!state || binary === undefined) return
    const name = state.name.trim()
    this.update(() => (this.renamingState = undefined))
    this.dismissSheet?.()
    if (name === '' || name === state.chat.name) return
    const outcome = await this.runner.command(binary, ['chats', 'title', state.chat.id, '--', name])
    await this.refresh()
    const failure = CommandOutcome.failure(outcome)
    this.say?.(failure === undefined ? `Renamed to ${name}` : `Not renamed: ${failure}`)
  }

  askToDelete(chat: ChatSummary): void {
    this.update(() => (this.pending = { case: 'delete', chat }))
    this.ask?.('deleteChat')
  }

  /** Whether `chat` compacts in its own session: the open one's chat, its child running. */
  private compactsInSession(chat: ChatSummary): boolean {
    return chat.id === this.currentChat?.id && this.current.connection.case === 'connected' && this.current.transcript.status.case !== 'ended'
  }

  /** Whether `chat` can be compacted from here: not while a compaction of it runs. */
  canCompact(chat: ChatSummary): boolean {
    return this.binary !== undefined && !this.compactions.isCompacting(chat.id)
  }

  askToCompact(chat: ChatSummary): void {
    if (!this.canCompact(chat)) return
    this.update(() => (this.pending = { case: 'compact', chat }))
    this.ask?.('compactChat')
  }

  /** Forget every summary…: what recall holds read first, so it is seen before it goes. */
  async askToForgetRecall(): Promise<void> {
    await this.refreshRecall()
    if (!(this.recall?.holdsAny ?? this.recallFailure !== undefined)) return
    this.update(() => (this.pending = { case: 'forgetRecall' }))
    this.ask?.('forget')
  }

  /** What a confirm says: the old prototype's words, which are the Mac's. */
  confirmWords(kind: LibraryConfirm): ConfirmWords | undefined {
    const pending = this.pending
    if (kind === 'deleteChat' && pending?.case === 'delete') {
      return { title: `Delete “${pending.chat.name}”?`, body: `apogee chats delete ${pending.chat.id} -- the conversation and its turns are removed, and this can't be undone.`, confirm: 'Delete', cancel: 'Cancel' }
    }
    if (kind === 'compactChat' && pending?.case === 'compact') {
      const body = this.compactsInSession(pending.chat)
        ? "Its session compacts it, as /compact does: the history so far is folded into a summary the model carries on from. The turns before it aren't kept word for word."
        : `apogee chats compact ${pending.chat.id} --yes -- its history is folded into a summary the model carries on from. The turns before it aren't kept word for word, and this can't be undone.`
      return { title: `Compact “${pending.chat.name}”?`, body, confirm: 'Compact it', cancel: 'Cancel' }
    }
    if (kind === 'forget' && pending?.case === 'forgetRecall') {
      const now = this.recallFailure !== undefined ? `Recall could not be read: ${this.recallFailure}. ` : this.recall ? `Now: ${this.recall.countLine}. ` : ''
      return { title: 'Forget what recall holds?', body: `${now}apogee chats recall --forget --yes -- every chat's summary goes, so no later session recalls them; every transcript stays.`, confirm: 'Forget every summary', cancel: 'Cancel' }
    }
    return undefined
  }

  /** The person said yes: what was asked, done. */
  async confirm(): Promise<void> {
    const pending = this.pending
    if (!pending) return
    this.update(() => (this.pending = undefined))
    switch (pending.case) {
      case 'delete':
        await this.delete(pending.chat)
        return
      case 'compact':
        await this.compact(pending.chat)
        return
      case 'forgetRecall':
        await this.forgetRecall()
        return
    }
  }

  /** Keeps things as they are. */
  dismissPending(): void {
    this.update(() => (this.pending = undefined))
  }

  /** `chats delete <id>`, read back. The open session, if it is the one, ends first and a new one opens in its place. */
  private async delete(chat: ChatSummary): Promise<void> {
    const binary = this.binary
    if (binary === undefined) return
    if (this.currentChat?.id === chat.id) await this.replace({})
    const outcome = await this.runner.command(binary, ['chats', 'delete', chat.id])
    await this.refresh()
    const failure = CommandOutcome.failure(outcome)
    this.say?.(failure === undefined ? `Deleted ${chat.name}` : `Not deleted: ${failure}`)
  }

  /** In its own session when it is the one running (its `compact` line), else `chats compact <id> --yes` as a supervised job, its row busy until it ends. */
  private async compact(chat: ChatSummary): Promise<void> {
    if (this.compactsInSession(chat)) {
      this.current.compact()
      this.say?.(`Compacting ${chat.name} in its session`)
      return
    }
    await this.compactions.compact(chat.id)
    await this.refresh()
  }

  /** `chats recall --forget --yes`, read back, its own line said. */
  private async forgetRecall(): Promise<void> {
    const binary = this.binary
    if (binary === undefined) return
    const outcome = await this.runner.command(binary, ['chats', 'recall', '--forget', '--yes'])
    await this.refreshRecall()
    if (outcome.case === 'done') this.say?.(outcome.stdout.split('\n').filter((line) => line.trim() !== '').at(-1)?.trim() ?? 'Recall forgotten')
    else this.say?.(`Not forgotten: ${CommandOutcome.failure(outcome) ?? 'apogee said nothing'}`)
  }

  // MARK: - The inspector

  /** ⌥⌘I: the inspector shown or hidden; shown, its reads made. */
  toggleInspector(): void {
    this.current.toggleInspector()
    if (this.current.inspectorOpen) void this.inspectorAppeared()
  }

  /** The inspector appeared: its reads -- the roles, what recall holds -- made once for the open session (the Mac's `.task { refreshInspector() }`). */
  async inspectorAppeared(): Promise<void> {
    if (this.binary === undefined || this.inspected?.session === this.current) return
    await this.refreshRoles()
    await this.refreshRecall()
  }

  /** The roles, as `models status` reads them under the suite the open session runs under; plain where only the CLI knows. */
  async refreshRoles(): Promise<void> {
    const binary = this.binary
    if (binary === undefined) return
    const session = this.current
    const suite = session.runningSuite
    this.inspected = { session, suite }
    const read = await this.runner.read(binary, ['models', 'status', ...(suite === undefined ? [] : ['--suite', suite])])
    if (session !== this.current || this.inspected.suite !== suite) return
    this.update(() => {
      this.roles = read.case === 'document' ? Field.list(read.document).filter((field) => field.label !== 'suite' && field.label !== 'footprint') : []
      this.rolesFailure = read.case === 'document' ? undefined : ReadOutcome.failure(read)
    })
  }

  /** `chats recall`: each summary, and the count line said from it. A failed read clears what an earlier one listed. */
  async refreshRecall(): Promise<void> {
    const binary = this.binary
    if (binary === undefined) return
    const read = await this.runner.read(binary, ['chats', 'recall'])
    const listing = read.case === 'document' ? RecallListing.of(read.document) : undefined
    this.update(() => {
      this.recall = listing
      this.recallFailure = listing ? undefined : (ReadOutcome.failure(read) ?? 'apogee did not say what recall holds')
      if (!listing?.summaries.some((summary) => summary.chat === this.recallSelection)) this.recallSelection = undefined
    })
  }

  /** One summary's text shown, chosen by its chat; again, hidden. */
  selectRecall(chat: string | undefined): void {
    this.update(() => (this.recallSelection = this.recallSelection === chat ? undefined : chat))
  }

  toggleSection(section: InspectorSection): void {
    this.update(() => (this.sections = { ...this.sections, [section]: !this.sections[section] }))
  }

  /** Expand all, or Collapse all once every section is open. */
  toggleAllSections(): void {
    const open = !inspectorSections.every((section) => this.sections[section])
    this.update(() => (this.sections = { session: open, turn: open, context: open, model: open, helpers: open, recall: open }))
  }

  /** Everything the inspector draws. */
  inspector(): InspectorFace {
    const session = this.current
    const model = session.transcript.model
    const info = this.modelInfo
    return Inspector.face({
      session,
      chat: this.currentChat,
      turn: this.latest,
      now: this.clock.now,
      sections: this.sections,
      compacting: this.compactions.isCompacting(this.currentChat?.id ?? ''),
      modelInfo: info !== undefined && info.model === model ? info.fields : [],
      modelRow: this.rows.find((row) => row.backend === model),
      roles: this.roles,
      rolesFailure: this.rolesFailure,
      recall: this.recall,
      recallFailure: this.recallFailure,
      recallSelection: this.recallSelection,
    })
  }

  // MARK: - The execute door

  /** ⌥⌘N: the door, fresh -- or as a preset fills it -- with the suites read for it. */
  async openExecuteDoor(form: Partial<ExecuteForm> = {}): Promise<void> {
    this.update(() => {
      this.executeForm = { ...ExecuteForm.empty(), ...form }
      this.executeDoorOpen = true
    })
    this.present?.('execute')
    await this.readSuites()
  }

  /** `config get suites` and `config get models.default_suite`, for the door. */
  private async readSuites(): Promise<void> {
    const binary = this.binary
    if (binary === undefined) return
    const listed = await this.runner.command(binary, ['config', 'get', 'suites'])
    const named = await this.runner.command(binary, ['config', 'get', 'models.default_suite'])
    this.update(() => {
      if (listed.case === 'done') this.suites = Suites.names(listed.stdout)
      if (named.case === 'done') this.defaultSuite = named.stdout.trim()
    })
  }

  setExecuteForm(change: Partial<ExecuteForm>): void {
    this.update(() => (this.executeForm = { ...this.executeForm, ...change }))
  }

  /** Whether the door's session has a suite to orchestrate: one named, or the default. Bare, the CLI refuses `--orchestrate`. */
  get canOrchestrate(): boolean {
    return this.executeForm.suite !== undefined || (this.defaultSuite ?? '') !== ''
  }

  /** Everything the door draws. */
  door(): DoorFace {
    const form = this.executeForm
    const configured = this.defaultSuite === undefined ? 'As the config says' : this.defaultSuite === '' ? 'No suite: bare on one model' : `The default suite (${this.defaultSuite})`
    return {
      suites: [{ value: '', label: configured }, ...this.suites.map((name) => ({ value: name, label: name }))],
      suite: form.suite ?? '',
      models: [{ value: '', label: form.suite === undefined ? 'The default backend' : 'The suite’s chat member' }, ...this.rows.map((row) => ({ value: row.backend, label: row.backend }))],
      model: form.model,
      folder: form.folder ?? 'Your home folder',
      orchestrate: form.orchestrate && this.canOrchestrate,
      canOrchestrate: this.canOrchestrate,
      note: this.canOrchestrate ? doorNote : `${doorNote} Orchestrate offers a suite's symphonies, and this session would run under none.`,
      command: ExecuteForm.command(form, this.canOrchestrate),
    }
  }

  closeExecuteDoor(): void {
    this.update(() => (this.executeDoorOpen = false))
    this.dismissSheet?.()
  }

  /** Open: the catalog read first, for `/play`; then the door's session, as every opening is. */
  async openExecute(): Promise<void> {
    const launch = ExecuteForm.launch(this.executeForm, this.canOrchestrate)
    this.closeExecuteDoor()
    await this.readPlays()
    this.showSessions?.()
    await this.replace(launch)
  }

  // MARK: - Keys

  /** The Sessions place's keys (the Mac's `SessionCommands`): whether one was its own. */
  press(keys: KeyChord): boolean {
    const command = SessionKeys.commandFor(keys)
    switch (command) {
      case undefined:
        return false
      case 'newSession':
        this.newSession()
        return true
      case 'newPrivateSession':
        this.newPrivateSession()
        return true
      case 'newExecuteSession':
        this.current.leave({ type: 'execute' })
        return true
      case 'toggleInspector':
        this.toggleInspector()
        return true
      case 'search':
        this.requestSearch()
        return true
    }
  }

  private update(change: () => void): void {
    change()
    this.changed()
  }
}
