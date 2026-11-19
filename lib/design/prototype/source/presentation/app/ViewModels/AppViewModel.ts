import { JobStarter } from '../../../business/jobs/SupervisedJob'
import { CommandOutcome } from '../../../business/reads/CommandRunning'
import { ModelInfo, ModelKind, ModelRow } from '../../../business/reads/Models'
import { ReadRunner } from '../../../business/reads/ReadRunner'
import { SuiteFootprint, Suites } from '../../../business/reads/SuiteFootprint'
import { Symphony } from '../../../business/reads/Symphonies'
import { ApogeeClient } from '../../../business/session/ApogeeClient'
import type { ChatLaunch } from '../../../business/session/ChatLaunch'
import type { SessionEvent } from '../../../business/session/ChatSession'
import { Clock } from '../../../infrastructure/clock/Clock'
import type { Timers } from '../../../infrastructure/clock/Clock'
import type { Preset } from '../../../infrastructure/process/Preset'
import { StandInChild } from '../../../infrastructure/process/StandInChild'
import type { ModeRules } from '../../../infrastructure/process/StandInChild'
import { ModelsViewModel } from '../../models/ViewModels/ModelsViewModel'
import { MonitorViewModel } from '../../monitor/ViewModels/MonitorViewModel'
import { Compactions } from '../../session/ViewModels/Compactions'
import type { SuiteChoice } from '../../session/ViewModels/SessionActions'
import { SessionViewModel } from '../../session/ViewModels/SessionViewModel'
import type { AttachChoice, FolderListing, SessionAction, SessionLeave } from '../../session/ViewModels/SessionViewModel'
import type { Destination } from '../../shell/ViewModels/Destination'
import { ShellViewModel } from '../../shell/ViewModels/ShellViewModel'
import type { SystemStrip } from '../../shell/ViewModels/SystemStrip'
import { ThemeService } from '../../styles/Appearance/ThemeService'
import { Observable } from '../../styles/Observation/Observable'

/** A place's view-model, as the composition calls it when the place comes into view. */
interface Place {
  appeared?(): void
}

/** What a session's `@` and attach menu offer: handed in, since a browser reads no folder. */
export interface SessionSamples {
  readonly listing: FolderListing
  readonly attachChoices: readonly AttachChoice[]
}

/**
 * The `/play` line `chat:play` sends: the old frame's words, and the line
 * `scripts/capture.ts` sent the branch build for its recording.
 */
export const playLine = '/play summarize-verify The board met on 14 October. It agreed to move the release to 3 November, and Ana Ruiz will own the checklist.'

/**
 * The turns 1v's presets write before the clock starts: the old scenarios'
 * words, and the lines `scripts/capture.ts` sent the branch build for each
 * recording.
 */
export const scenarioTurns = {
  streaming: ['Where do fetches start in this repo, and what cancels them?', 'So one slow host takes the whole batch down. I want the other fetches to finish and just log the timeouts. What’s the idiomatic fix on 3.12?'],
  retry: 'Make `_fetch()` retry timeouts and connection errors, using `settings.fetch.retries` (it’s already in config.toml). Go ahead and edit the file.',
  wal: 'Why does group commit make the WAL cheaper under load? Point me to the pages.',
  crash: 'What’s this stack trace telling me?',
  pr: 'Review the retry-budget change in PR 482 before I merge it.',
  kv: 'Would a 16-bit cache fit at 128K?',
} as const

/**
 * The application's view-model: the composition root (design 1e, ADR 0001
 * §3), the Mac app's `AppViewModel`. It builds the stand-in's services once
 * from the preset -- the child, the clock, the reads, the chat starter --
 * builds each view-model over them, wires the links between places with
 * closures, and calls `appeared()` on the place each route change shows.
 * The entry file only mounts it. Plain TypeScript: it never imports React.
 *
 * Until 1g's `SessionLibraryViewModel` exists it holds the one session and
 * does for it what that library does on the Mac: reads the model facts
 * (`models list`, `models info <model>`), the suites and a saved chat's
 * earlier turns, carries a leave out, and holds the compactions (1v).
 */
export class AppViewModel extends Observable {
  readonly name = 'Apogee'
  readonly clock: Clock
  readonly child: StandInChild
  readonly reads: ReadRunner
  readonly chats: ApogeeClient
  readonly theme: ThemeService
  readonly shell: ShellViewModel
  readonly monitor: MonitorViewModel
  readonly models: ModelsViewModel
  /** Saved chats compacting as jobs (GUI 6b): held here until 1g's library takes them. */
  readonly compactions: Compactions
  /** The one session: what Sessions shows. */
  session: SessionViewModel

  /** Each place's view-model, by its destination: the ones built so far. */
  private readonly places: Partial<Record<Destination, Place>>
  private settingUp: Promise<void> | undefined
  /** Whether the app is wanted running: a stop before its setup has finished keeps the clock stopped. */
  private running = false
  /** The binary the stand-in found; `undefined` when none was. */
  private binary: string | undefined
  /** `models list`, read once, for every session's header. */
  private modelFacts: Promise<{ readonly kinds: Readonly<Record<string, ModelKind>>; readonly rows: readonly ModelRow[] } | undefined> | undefined

  constructor(
    readonly preset: Preset,
    modes: ModeRules,
    timers?: Timers,
    private readonly samples: SessionSamples = { listing: { folder: '', files: [] }, attachChoices: [] },
  ) {
    super()
    this.child = new StandInChild(preset, modes)
    this.clock = new Clock(preset.clock, timers)
    this.reads = new ReadRunner(this.child)
    this.chats = new ApogeeClient(this.child, this.clock)
    this.theme = new ThemeService()
    this.shell = new ShellViewModel(this.clock, this.theme)
    this.monitor = new MonitorViewModel(this.reads, this.clock)
    this.models = new ModelsViewModel()
    this.compactions = new Compactions(new JobStarter(this.child, this.clock), () => this.binary)
    this.compactions.onSay = (text) => this.shell.say(text)
    this.places = { models: this.models }
    this.session = this.makeSession({})

    // The links between places, as closures: the monitor's two doors.
    this.monitor.openLibrary = () => {
      this.shell.closeOverlay()
      this.shell.go('models')
      this.models.showStore()
    }
    // Until Sessions (1g), Video (1k), Compose (1l) and Voice (1m)
    // hold what a row unloads or stops, Unload says what the old prototype says.
    this.monitor.unload = (name) => this.shell.say(`Unloaded ${name}`)
    this.shell.routeChanged = (destination) => this.places[destination]?.appeared?.()
    // The monitor reads while it, or the strip that shows its numbers, is open;
    // the scrim the shell draws over an open menu closes the session's too.
    this.shell.subscribe(() => {
      this.watch()
      if (this.shell.menu === undefined && this.session.menu !== undefined) this.session.closeMenus()
    })
  }

  /** The sidebar strip's numbers: the monitor's. */
  get strip(): SystemStrip | undefined {
    return this.monitor.strip
  }

  /**
   * The app started: every place handed the binary the stand-in finds, the
   * preset's screen set up -- a chat preset's session located, opened and
   * its sends written, awaited -- then the clock started, its start ticks
   * applied at once, so they play the sent turn. Set up once.
   */
  start(): Promise<void> {
    this.running = true
    this.settingUp ??= this.setUp()
    return this.settingUp.then(() => {
      if (this.running) this.clock.start()
    })
  }

  /** The clock stopped: nothing ticks until `start` again. */
  stop(): void {
    this.running = false
    this.clock.stop()
  }

  /** Leaving the session for `then`, asked first where a private conversation would be lost: what Suites' "Open a chat under it" (1p) calls. */
  leave(then: SessionLeave): void {
    this.session.leave(then)
  }

  /** The suite chooser closed: Cancel, or the sheet dismissed. */
  closeSuites(): void {
    this.session.dismissSuites()
    this.shell.closeSheet()
  }

  /** Switch: the session moved to the chosen suite, the chooser closed. */
  switchSuite(): void {
    this.shell.closeSheet()
    void this.session.switchSuite()
  }

  /** The endPrivate confirm's End it and switch. */
  endPrivateAndLeave(): void {
    this.shell.dismissConfirm()
    void this.session.endAndLeave()
  }

  /** The endPrivate confirm's Keep it running: everything as it was. */
  keepRunning(): void {
    this.shell.dismissConfirm()
    this.session.keepRunning()
  }

  private async setUp(): Promise<void> {
    const found = this.child.locateBinary()
    this.binary = found.case === 'found' ? found.binary : undefined
    this.monitor.connect(this.binary)
    await this.applyStart(this.preset.route)
    this.watch()
  }

  /** What a preset's `start` opens, as the old `applyStart()` sets it up. */
  private async applyStart(route: string): Promise<void> {
    const [where, sub] = route.split(':')
    if (where === 'monitor') {
      this.shell.go('sessions')
      this.shell.open('monitor')
      this.shell.setSystemOpen(true)
      // Behind it, the Streaming session at chat's frame (1v; the old progress 30).
      await this.openStreaming()
      return
    }
    if (where !== 'chat') return
    this.shell.go('sessions')
    const session = () => this.session
    switch (sub) {
      case undefined:
        await this.openStreaming()
        return
      case 'new':
        await this.openSession({})
        return
      case 'private':
        await this.openSession({ privately: true })
        return
      case 'play':
        await this.openSession({ execute: { suite: 'coding' }, folder: '~/code/ingest' })
        // A still chat preset writes its sends before the clock starts (DEVELOPER.md → the CLI stand-in).
        session().writeAhead(playLine)
        return
      case 'suite':
        await this.openSession({})
        await this.chooseSuite('vast')
        return
      case 'retry':
        await this.openSession({})
        session().writeAhead(scenarioTurns.retry)
        return
      case 'wal':
        // The capture's own lines before its turn: moved to the billed claude, the notes and the config attached.
        await this.openSession({})
        session().apply({ model: 'claude' })
        session().attach('storage-engine-design.pdf')
        session().attach('postgresql.conf')
        // The old frame opens citation 1 (`wal: { cite: 1 }`).
        session().citeTurn(1, 1)
        session().writeAhead(scenarioTurns.wal)
        return
      case 'crash':
        await this.openSession({})
        session().attach('crash-0912.png')
        session().writeAhead(scenarioTurns.crash)
        return
      case 'pr':
        await this.openSession({})
        // The old frame opens the card with Per batch picked (`pr: { choice: 'batch' }`): a pick only; nothing is written until Answer.
        session().preselect('Per batch')
        session().writeAhead(scenarioTurns.pr)
        return
      case 'restarting':
        // GUI 6a on the kv chat, its inspector off: moved to the suite pair, its child being stopped, an attach made meanwhile.
        await this.openSession({ resume: this.resumedChat() })
        session().inspectorOpen = false
        await this.chooseSuite('pair')
        this.switchSuite()
        session().attach('storage-engine-design.pdf')
        return
      case 'notsent':
        // A turn its closed input never took: written while a restart had closed the child's input; the session comes back.
        await this.openSession({ resume: this.resumedChat() })
        session().inspectorOpen = false
        await session().restart()
        session().writeAhead(scenarioTurns.kv)
        return
      case 'ended':
        // A child that died mid-answer, two lines made while its turn ran held for Start again.
        await this.openSession({ resume: this.resumedChat() })
        session().inspectorOpen = false
        session().writeAhead(scenarioTurns.kv)
        session().apply({ temperature: 0.4 })
        session().compact()
        return
      case 'compacting':
        // GUI 6b: two saved chats compacting as jobs, retry's first; the open session Streaming at its starting frame.
        await this.openStreaming()
        for (const id of this.compactingChats()) void this.compactions.compact(id)
        return
    }
  }

  /**
   * The Streaming session (1v), at whichever frame the preset's rule freezes
   * it: `chat` and `monitor` at chat's, and `chat:compacting`, `console`
   * (1g) and the `settings:*` presets (1i) at the starting one. Its capture's
   * own lines before the turns -- the folder attached -- then its two turns.
   */
  async openStreaming(): Promise<void> {
    await this.openSession({})
    this.session.attach('.')
    for (const text of scenarioTurns.streaming) this.session.writeAhead(text)
  }

  /** The saved chat the preset's chat rule resumes: the `--resume` it answers. */
  private resumedChat(): string | undefined {
    const args = this.preset.rules.chats?.find((rule) => rule.arguments.includes('--resume'))?.arguments ?? []
    const at = args.indexOf('--resume')
    return at >= 0 ? args[at + 1] : undefined
  }

  /** The saved chats the preset's jobs compact, in its order: each `chats compact <id>` it answers. */
  private compactingChats(): string[] {
    return (this.preset.rules.jobs ?? []).flatMap((rule) => (rule.arguments[0] === 'chats' && rule.arguments[1] === 'compact' && rule.arguments[2] !== undefined ? [rule.arguments[2]] : []))
  }

  /** A session over the stand-in's chat starter, its acts wired to the shell. */
  private makeSession(launch: ChatLaunch): SessionViewModel {
    // Its reader reads a saved conversation's earlier turns back, as the Mac's session reads them (`chats show <id>`).
    const session = new SessionViewModel(this.chats, this.clock, launch, this.samples.listing, this.samples.attachChoices, this.reads)
    session.onAction = (action) => this.act(action)
    session.observer = (event) => this.observe(session, event)
    session.subscribe(() => {
      if (session !== this.session) return
      if (session.menu !== undefined && this.shell.menu !== 'session') this.shell.openMenu('session')
      if (session.menu === undefined && this.shell.menu === 'session') this.shell.closeMenus()
    })
    return session
  }

  /**
   * The session replaced by one `launch` opens, started, its model facts read;
   * for a new or private chat the default suite (`config get
   * models.default_suite`), so the header names the suite it runs under; for
   * an `execute` session its symphonies. (The Mac reads the default only at
   * the chooser and the execute door.)
   */
  private async openSession(launch: ChatLaunch): Promise<void> {
    const old = this.session
    this.session = this.makeSession(launch)
    this.changed()
    // Ended, its clock listener released, whether or not it was ever launched.
    await old.stop()
    const session = this.session
    await session.start()
    if (this.binary === undefined || session.connection.case !== 'connected') return
    const facts = await this.readModelFacts()
    if (facts) {
      session.modelKinds = facts.kinds
      session.modelRows = facts.rows
    }
    if (launch.resume === undefined && launch.execute === undefined) {
      const named = await this.reads.command(this.binary, ['config', 'get', 'models.default_suite'])
      if (session === this.session) session.knowDefaultSuite(named.case === 'done' ? named.stdout.trim() : undefined)
    }
    if (launch.execute) {
      const read = await this.reads.read(this.binary, ['symphonies', 'list'])
      if (read.case === 'document') session.plays = Symphony.list(read.document)
    }
  }

  /** `models list`, once: each backend's kind, and the backends a chat runs on. */
  private readModelFacts() {
    const binary = this.binary
    if (binary === undefined) return Promise.resolve(undefined)
    this.modelFacts ??= this.reads.read(binary, ['models', 'list']).then((read) => {
      if (read.case !== 'document') return undefined
      const rows = ModelRow.list(read.document)
      const chat = ModelRow.chatBackends(rows)
      return { kinds: ModelKind.byBackend(read.document), rows: rows.filter((row) => chat.includes(row.backend)) }
    })
    return this.modelFacts
  }

  /**
   * What the session says, after it has folded it: its model's window read
   * (`models info <model>`) when its `session` event names the model, and
   * again when an answer names another -- neither `result` nor its `usage`
   * carries a window (the Mac library's `readWindow(of:for:)`).
   */
  private observe(session: SessionViewModel, event: SessionEvent): void {
    if (event.case !== 'event' || this.binary === undefined) return
    const kind = event.event.kind
    const model = kind.case === 'session' ? kind.session.model : kind.case === 'result' ? kind.result.model : undefined
    if (model === undefined || model === '' || (kind.case === 'result' && model === session.windowOf)) return
    session.windowOf = model
    void this.reads.read(this.binary, ['models', 'info', model]).then((read) => {
      if (session.transcript.model !== model) return
      session.contextWindow = read.case === 'document' ? ModelInfo.window(read.document) : undefined
    })
  }

  /** An act the session hands its owner. */
  private act(action: SessionAction): void {
    switch (action.case) {
      case 'chooseSuite':
        void this.chooseSuite()
        return
      case 'openLibrary':
        this.shell.go('models')
        return
      case 'say':
        this.shell.say(action.text)
        return
      case 'ask':
        this.shell.ask(action.kind)
        return
      case 'leave':
        this.carryOut(action.then)
        return
    }
  }

  /**
   * A leave carried out: a new chat, or a private one, opened on Sessions
   * with any overlay closed, as the old `sessionGo` does (`screen: 'chat',
   * overlay: null`), whichever place it was asked from. Opening a saved chat
   * and the execute door are 1g's.
   */
  private carryOut(then: SessionLeave): void {
    if (then.type === 'new' || then.type === 'private') {
      this.shell.closeOverlay()
      this.shell.go('sessions')
    }
    switch (then.type) {
      case 'new':
        void this.openSession(then.suite === undefined ? {} : { suite: then.suite, forceSuite: then.force ?? false })
        return
      case 'private':
        void this.openSession({ privately: true })
        return
      case 'open':
      case 'execute':
        this.shell.say('Sessions’ list and the execute door aren’t drawn here yet. Design item 1g ports them.')
        return
    }
  }

  /**
   * The suite chooser (`/suite`, the header's suite, Choose a suite): the
   * suites and the default read (`config get suites`, `config get
   * models.default_suite`), the CLI's verdict on each (`models status
   * --suite <name>`), then shown -- as the Mac library's `chooseSuite()`.
   */
  private async chooseSuite(choosing?: string): Promise<void> {
    const binary = this.binary
    const session = this.session
    if (binary === undefined || !session.canSwitchSuite) return
    const listed = await this.reads.command(binary, ['config', 'get', 'suites'])
    if (listed.case !== 'done') {
      // The CLI could not say its suites: said in its words, never drawn as none configured.
      this.shell.say(CommandOutcome.failure(listed) ?? 'apogee could not list its suites')
      return
    }
    const names = Suites.names(listed.stdout)
    const named = await this.reads.command(binary, ['config', 'get', 'models.default_suite'])
    const defaultSuite = named.case === 'done' ? named.stdout.trim() : undefined
    const choices: SuiteChoice[] = []
    for (const name of names) {
      const read = await this.reads.read(binary, ['models', 'status', '--suite', name])
      choices.push({ name, footprint: read.case === 'document' ? SuiteFootprint.of(read.document) : undefined, isDefault: name === defaultSuite })
    }
    if (session !== this.session) return
    session.presentSuites(choices, defaultSuite, choosing)
    this.shell.openSheet('suite')
  }

  private watch(): void {
    if (!this.settingUp) return
    if (this.shell.overlay === 'monitor' || this.shell.systemOpen) this.monitor.opened()
    else this.monitor.closed()
  }
}
