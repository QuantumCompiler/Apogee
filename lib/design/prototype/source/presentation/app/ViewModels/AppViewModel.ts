import { ChildRegistry } from '../../../business/jobs/ChildRegistry'
import { JobStarter } from '../../../business/jobs/SupervisedJob'
import { ReadRunner } from '../../../business/reads/ReadRunner'
import { ApogeeClient } from '../../../business/session/ApogeeClient'
import { Clock } from '../../../infrastructure/clock/Clock'
import type { Timers } from '../../../infrastructure/clock/Clock'
import type { Preset } from '../../../infrastructure/process/Preset'
import { StandInChild } from '../../../infrastructure/process/StandInChild'
import type { ModeRules, Rules } from '../../../infrastructure/process/StandInChild'
import { ConsoleViewModel } from '../../console/ViewModels/ConsoleViewModel'
import { handles } from '../../models/Previews/ModelsWorlds'
import { ModelsViewModel } from '../../models/ViewModels/ModelsViewModel'
import type { ModelsConfirm, ModelsTab } from '../../models/ViewModels/ModelsViewModel'
import { ConvertForm, QuantizeForm } from '../../models/ViewModels/StoreForms'
import { MonitorViewModel } from '../../monitor/ViewModels/MonitorViewModel'
import type { Compactions } from '../../session/ViewModels/Compactions'
import type { SessionViewModel } from '../../session/ViewModels/SessionViewModel'
import type { AttachChoice, FolderListing, SessionLeave } from '../../session/ViewModels/SessionViewModel'
import { SessionLibraryViewModel } from '../../sessions/ViewModels/SessionLibraryViewModel'
import type { ExecuteForm } from '../../sessions/ViewModels/ExecuteForm'
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

/** The execute door as the `execute` preset fills it: the old `ex: { suite: 'coding', folder: '~/code/ingest', orchestrate: true }`. */
export const executeDoorForm: Partial<ExecuteForm> = { suite: 'coding', folder: '~/code/ingest', orchestrate: true }

/** The console as the `console` preset leaves it: the old `con`'s two runs, then `models ` typed (`main.html:2737-2744` at `b375fac9`). */
export const consoleLines = { runs: ['models list', 'reset'], typed: 'models ' } as const

/**
 * The application's view-model: the composition root (design 1e, ADR 0001
 * §3), the Mac app's `AppViewModel`. It builds the stand-in's services once
 * from the preset -- the child, the clock, the reads, the jobs, the chat
 * starter, and the console's own child (1g) -- builds each view-model over
 * them, wires the links between places
 * with closures, and calls `appeared()` on the place each route change shows.
 * The entry file only mounts it. Plain TypeScript: it never imports React.
 *
 * The Sessions place is its library (1g): the saved chats, the one session
 * open, the inspector and the execute door, which reads the model facts, the
 * suites and a saved chat's earlier turns for the session, carries a leave
 * out and holds the compactions -- what this composition held until 1g.
 */
export class AppViewModel extends Observable {
  readonly name = 'Apogee'
  readonly clock: Clock
  readonly child: StandInChild
  /**
   * The console's own stand-in (1g): the preset's rules with the console's
   * beside them (`Preset.console`), apart from `child`, so the console's
   * catch-all "Done." answers only a line typed there, never a place's job.
   */
  readonly consoleChild: StandInChild
  readonly reads: ReadRunner
  /** Every child the app runs, in one place (design 1h): what the sidebar's running counts read, without importing a place. */
  readonly registry = new ChildRegistry()
  readonly jobs: JobStarter
  readonly chats: ApogeeClient
  readonly theme: ThemeService
  readonly shell: ShellViewModel
  readonly monitor: MonitorViewModel
  readonly models: ModelsViewModel
  /** The Sessions place: the list, the open session, the inspector and the door (1g). */
  readonly library: SessionLibraryViewModel
  /** The console (1g). */
  readonly console: ConsoleViewModel

  /** Each place's view-model, by its destination: the ones built so far. */
  private readonly places: Partial<Record<Destination, Place>>
  private settingUp: Promise<void> | undefined
  /** What the preset does once its clock has started: the console's runs after its first has played. */
  private afterStart: (() => Promise<void>) | undefined = undefined
  /** Whether the app is wanted running: a stop before its setup has finished keeps the clock stopped. */
  private running = false

  constructor(
    readonly preset: Preset,
    modes: ModeRules,
    timers?: Timers,
    samples: SessionSamples = { listing: { folder: '', files: [] }, attachChoices: [] },
  ) {
    super()
    this.child = new StandInChild(preset, modes)
    this.clock = new Clock(preset.clock, timers)
    this.reads = new ReadRunner(this.child)
    this.jobs = new JobStarter(this.child, this.clock, this.registry)
    this.chats = new ApogeeClient(this.child, this.clock)
    this.theme = new ThemeService()
    this.shell = new ShellViewModel(this.clock, this.theme)
    this.monitor = new MonitorViewModel(this.reads, this.clock)
    this.models = new ModelsViewModel(this.reads, this.jobs, this.clock)
    this.library = new SessionLibraryViewModel(this.chats, this.reads, this.jobs, this.clock, samples)
    this.consoleChild = new StandInChild({ rules: AppViewModel.consoleRules(preset), apogee: preset.apogee }, modes)
    this.console = new ConsoleViewModel(new ReadRunner(this.consoleChild), new JobStarter(this.consoleChild, this.clock, this.registry))
    this.places = { models: this.models }

    // The library's doors, as closures: the toast, the shell's hosts, Sessions (an overlay closed, or left open), Models and the scrim.
    this.library.say = (text) => this.shell.say(text)
    this.library.present = (sheet) => this.shell.openSheet(sheet)
    this.library.dismissSheet = () => this.shell.closeSheet()
    this.library.ask = (kind) => this.shell.ask(kind)
    this.library.showSessions = () => {
      this.shell.closeOverlay()
      this.shell.go('sessions')
    }
    this.library.goToSessions = () => this.shell.go('sessions')
    // Open in library lands on Backends at the session's model, Change helpers on Roles (design 1h).
    this.library.openModels = (tab, model) => this.openModels(tab, model)
    // The list draws the open session's row current only on Sessions, as the old list does.
    this.library.showing(this.shell.route === 'sessions')
    this.library.menuChanged = (owner) => (owner === undefined ? this.shell.closeMenus() : this.shell.openMenu(owner))
    this.library.subscribe(() => {
      this.watchSession()
      this.changed()
    })
    this.watchSession()
    // The places' own keys: Sessions' ⌘N, ⇧⌘N, ⌥⌘N, ⌥⌘I and ⇧⌘F.
    this.shell.placeKeys = (keys) => this.library.press(keys)

    // The Models place's doors (design 1h): the toast, the shell's hosts, the open session, Settings, the places.
    this.models.say = (text) => this.shell.say(text)
    this.models.present = (sheet) => this.shell.openSheet(sheet)
    this.models.dismissSheet = () => this.shell.closeSheet()
    this.models.ask = (kind) => this.shell.ask(kind)
    this.models.useInSession = (backend) => {
      this.session.pickModel(backend)
      this.library.showSessions?.()
    }
    this.models.openSettings = () => this.shell.perform({ kind: 'openSettings' })
    this.models.goTo = (place) => this.shell.go(place)
    this.models.subscribe(() => this.changed())
    this.registry.onChange(() => this.changed())

    // The links between places, as closures: the monitor's two doors.
    this.monitor.openLibrary = () => {
      this.shell.closeOverlay()
      this.shell.go('models')
      this.models.showStore()
    }
    // Until Video (1k), Compose (1l) and Voice (1m) hold what a row unloads
    // or stops, Unload says what the old prototype says: the open session's
    // model is held by its own child, which the CLI unloads with it.
    this.monitor.unload = (name) => this.shell.say(`Unloaded ${name}`)
    this.shell.routeChanged = (destination) => {
      this.library.showing(destination === 'sessions')
      this.places[destination]?.appeared?.()
    }
    // The monitor reads while it, or the strip that shows its numbers, is open;
    // the scrim the shell draws over an open menu closes the session's and the list's too.
    this.shell.subscribe(() => {
      this.watch()
      if (this.shell.menu === undefined && this.session.menu !== undefined) this.session.closeMenus()
      if (this.shell.menu === undefined && this.library.rowMenu !== undefined) this.library.closeRowMenu()
    })
  }

  /**
   * What the console's child answers: the preset's rules, the console's
   * beside them. Of two jobs as long, the later wins, so a job the preset
   * recorded beats the console's of the same arguments; the console's
   * completions are the later commands.
   */
  private static consoleRules(preset: Preset): Rules {
    const console = preset.console ?? {}
    return {
      ...preset.rules,
      jobs: [...(console.jobs ?? []), ...(preset.rules.jobs ?? [])],
      commands: [...(preset.rules.commands ?? []), ...(console.commands ?? [])],
    }
  }

  /** The open session: what Sessions shows, the library's. */
  get session(): SessionViewModel {
    return this.library.current
  }

  /** Saved chats compacting as jobs (GUI 6b): the library's. */
  get compactions(): Compactions {
    return this.library.compactions
  }

  /**
   * The places' running counts, the sidebar's badges (design 1h): read from
   * the shared registry of children, never a place's own list -- Models'
   * is every store run going on, a pull, a conversion, a quantization or a
   * check of the files, whoever started it.
   */
  get placeRuns(): { readonly models: number } {
    const store = ['pull', 'convert', 'quantize', 'repair']
    return { models: this.registry.children('job').filter((job) => store.some((verb) => job.label.startsWith(`apogee models ${verb} `))).length }
  }

  /** Models, on the tab a door names: Backends at a model, or Roles (the inspector's two doors, design 1h). */
  openModels(tab: ModelsTab = 'backends', model?: string): void {
    this.shell.closeOverlay()
    this.shell.go('models')
    if (tab === 'backends') void this.models.openBackend(model)
    else this.models.pick(tab)
  }

  /** A Models confirm answered: yes, or its second door; the host closed first. */
  async confirmModels(kind: ModelsConfirm, alternative: boolean): Promise<void> {
    this.shell.dismissConfirm()
    await this.models.confirm(kind, alternative)
  }

  /** A Models confirm's Cancel. */
  dismissModels(kind: ModelsConfirm): void {
    this.shell.dismissConfirm()
    this.models.dismissPending(kind)
  }

  /** The sidebar strip's numbers: the monitor's. */
  get strip(): SystemStrip | undefined {
    return this.monitor.strip
  }

  /**
   * The app started: every place handed the binary the stand-in finds, the
   * list and the model facts read, the preset's screen set up -- a chat
   * preset's session located, opened and its sends written, awaited -- then
   * the clock started, its start ticks applied at once, so they play the sent
   * turn; then what the preset does once they have played. Set up once.
   */
  start(): Promise<void> {
    this.running = true
    this.settingUp ??= this.setUp()
    return this.settingUp.then(async () => {
      if (!this.running) return
      this.clock.start()
      const after = this.afterStart
      this.afterStart = undefined
      await after?.()
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

  /** A library confirm answered: yes, then the host closed. */
  confirmLibrary(): void {
    this.shell.dismissConfirm()
    void this.library.confirm()
  }

  /** A library confirm's Cancel. */
  dismissLibrary(): void {
    this.shell.dismissConfirm()
    this.library.dismissPending()
  }

  private async setUp(): Promise<void> {
    const found = this.child.locateBinary()
    const binary = found.case === 'found' ? found.binary : undefined
    this.monitor.connect(binary)
    this.models.connect(binary)
    // The console's child shares the preset's mode, so it finds the binary where `child` does.
    this.console.connect(binary)
    await this.library.start()
    await this.applyStart(this.preset.route)
    this.watch()
  }

  /** What a preset's `start` opens, as the old `applyStart()` sets it up. */
  private async applyStart(route: string): Promise<void> {
    const [where, sub] = route.split(':')
    const library = this.library
    const session = () => this.session
    if (where === 'monitor') {
      this.shell.go('sessions')
      this.shell.open('monitor')
      this.shell.setSystemOpen(true)
      // Behind it, the Streaming session at chat's frame (1v; the old progress 30).
      await this.openStreaming()
      return
    }
    if (where === 'console') {
      // The console over the Streaming session at its starting frame (the old progress 15): models list run, then once it has played, reset refused and `models ` offered its next words.
      this.shell.go('sessions')
      await this.openStreaming()
      this.shell.open('console')
      this.console.setInput(consoleLines.runs[0])
      const listed = this.console.run()
      this.afterStart = async () => {
        await listed
        for (const line of consoleLines.runs.slice(1)) {
          this.console.setInput(line)
          await this.console.run()
        }
        this.console.setInput(consoleLines.typed)
        await this.console.complete()
      }
      return
    }
    if (where === 'models' || where === 'download') {
      await this.openModelsPreset(where === 'download' ? 'download' : (sub ?? ''))
      return
    }
    if (where === 'execute') {
      // The door over the play session, filled as the old frame fills it.
      this.shell.go('sessions')
      await library.replace({ execute: { suite: 'coding' }, folder: '~/code/ingest' })
      session().writeAhead(playLine)
      await library.openExecuteDoor(executeDoorForm)
      return
    }
    if (where !== 'chat') return
    this.shell.go('sessions')
    switch (sub) {
      case undefined:
        await this.openStreaming()
        return
      case 'new':
        await library.replace({})
        return
      case 'private':
        await library.replace({ privately: true })
        return
      case 'play':
        await library.replace({ execute: { suite: 'coding' }, folder: '~/code/ingest' })
        // A still chat preset writes its sends before the clock starts (DEVELOPER.md → the CLI stand-in).
        session().writeAhead(playLine)
        return
      case 'suite':
        await library.replace({})
        await library.chooseSuite('vast')
        return
      case 'retry':
        await library.replace({})
        session().writeAhead(scenarioTurns.retry)
        return
      case 'wal':
        // The capture's own lines before its turn: moved to the billed claude, the notes and the config attached.
        await library.replace({})
        session().apply({ model: 'claude' })
        session().attach('storage-engine-design.pdf')
        session().attach('postgresql.conf')
        // The old frame opens citation 1 (`wal: { cite: 1 }`).
        session().citeTurn(1, 1)
        session().writeAhead(scenarioTurns.wal)
        return
      case 'crash':
        await library.replace({})
        session().attach('crash-0912.png')
        session().writeAhead(scenarioTurns.crash)
        return
      case 'pr':
        await library.replace({})
        // The old frame opens the card with Per batch picked (`pr: { choice: 'batch' }`): a pick only; nothing is written until Answer.
        session().preselect('Per batch')
        session().writeAhead(scenarioTurns.pr)
        return
      case 'restarting':
        // GUI 6a on the kv chat, its inspector off: moved to the suite pair, its child being stopped, an attach made meanwhile.
        session().inspectorOpen = false
        await library.replace({ resume: this.resumedChat() })
        await library.chooseSuite('pair')
        this.switchSuite()
        session().attach('storage-engine-design.pdf')
        return
      case 'notsent':
        // A turn its closed input never took: written while a restart had closed the child's input; the session comes back.
        session().inspectorOpen = false
        await library.replace({ resume: this.resumedChat() })
        await session().restart()
        session().writeAhead(scenarioTurns.kv)
        return
      case 'ended':
        // A child that died mid-answer, two lines made while its turn ran held for Start again.
        session().inspectorOpen = false
        await library.replace({ resume: this.resumedChat() })
        session().writeAhead(scenarioTurns.kv)
        session().apply({ temperature: 0.4 })
        session().compact()
        return
      case 'compacting': {
        // GUI 6b: two saved chats compacting as jobs, retry's first, kv's row menu open; the open session Streaming at its starting frame.
        await this.openStreaming()
        const compacting = this.compactingChats()
        for (const id of compacting) void library.compactions.compact(id)
        const last = compacting.at(-1)
        if (last !== undefined) library.toggleRowMenu(last)
        return
      }
    }
  }

  /**
   * A Models preset (design 1h), as the old `applyStart('models:…')` sets it
   * up: the Streaming session at its starting frame behind it, the place
   * read, then its tab, its selection, and the sheet, confirm or job the old
   * frame shows.
   */
  private async openModelsPreset(sub: string): Promise<void> {
    await this.openStreaming()
    const models = this.models
    const store = models.store
    this.shell.go('models')
    await models.refresh()
    const tab: Readonly<Record<string, ModelsTab>> = { downloads: 'jobs', store: 'store', convert: 'store', register: 'store', roles: 'roles', delete: 'store', notran: 'jobs' }
    models.pick(tab[sub] ?? 'backends')
    const bytes = (handle: string) => store.infos.get(handle)?.data?.bytes
    switch (sub) {
      case '':
        await models.select('llama3.2-1b-Q4KM')
        return
      case 'downloads':
        // The Llama conversion with --register-with Q4_K_M, its store read first, standing where the preset's ticks leave it.
        await models.downloads.convert(ConvertForm.gguf(handles.llamaSnapshot, bytes(handles.llamaSnapshot)))?.ready
        return
      case 'store':
        await store.select(handles.llamaSnapshot)
        return
      case 'convert':
        await store.select(handles.llamaSnapshot)
        await store.actAsync('convertToGGUF')
        return
      case 'register':
        await store.select(handles.gemmaMlx)
        await store.actAsync('register')
        return
      case 'delete':
        await store.select(handles.llamaQ4)
        await store.actAsync('delete')
        return
      case 'notran': {
        // GUI 6d: a quantization stopped while its first store read ran; no child ever started.
        const download = models.downloads.quantize(new QuantizeForm(handles.llamaF16, 'Llama3.2-1B-Instruct', bytes(handles.llamaF16)))
        if (download) await models.downloads.stop(download)
        return
      }
      case 'download':
        await models.select('Gemma4-E4B-4bit')
        models.openDownload('meta-llama/Llama-3.2-1B-Instruct')
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
    await this.library.replace({})
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

  /** The open session's header menus raise the shell's scrim while one is open, as 1u's did. */
  private watchedSession: SessionViewModel | undefined = undefined
  private watchSession(): void {
    const session = this.session
    if (session === this.watchedSession) return
    this.watchedSession = session
    session.subscribe(() => {
      if (session !== this.session) return
      if (session.menu !== undefined && this.shell.menu !== 'session') this.shell.openMenu('session')
      if (session.menu === undefined && this.shell.menu === 'session') this.shell.closeMenus()
    })
  }

  private watch(): void {
    if (!this.settingUp) return
    if (this.shell.overlay === 'monitor' || this.shell.systemOpen) this.monitor.opened()
    else this.monitor.closed()
  }
}
