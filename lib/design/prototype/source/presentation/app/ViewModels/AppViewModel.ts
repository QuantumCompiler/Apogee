import { ChildRegistry } from '../../../business/jobs/ChildRegistry'
import { JobStarter } from '../../../business/jobs/SupervisedJob'
import { MediaLibrary } from '../../../business/media/MediaRecords'
import { ReadRunner } from '../../../business/reads/ReadRunner'
import { ApogeeClient } from '../../../business/session/ApogeeClient'
import { Clock } from '../../../infrastructure/clock/Clock'
import type { Timers } from '../../../infrastructure/clock/Clock'
import type { Preset } from '../../../infrastructure/process/Preset'
import { StandInChild } from '../../../infrastructure/process/StandInChild'
import type { ModeRules, Rules } from '../../../infrastructure/process/StandInChild'
import { heatPumpFrame, moviesFolder, passedOver, projects, recordedBrief, recordedEdit } from '../../compose/Previews/ComposeWorlds'
import { ComposeViewModel } from '../../compose/ViewModels/ComposeViewModel'
import { ConsoleViewModel } from '../../console/ViewModels/ConsoleViewModel'
import { graphWorld, overtakenNode } from '../../graph/Previews/GraphWorlds'
import type { GraphConfirm } from '../../graph/ViewModels/GraphFaces'
import { GraphViewModel } from '../../graph/ViewModels/GraphViewModel'
import { collectionQuestion, recordedSearches, recordsQuestion, rejectedRecord } from '../../knowledge/Previews/KnowledgeWorlds'
import { CollectionsViewModel } from '../../knowledge/ViewModels/CollectionsViewModel'
import type { KnowledgeTab } from '../../knowledge/ViewModels/KnowledgeFaces'
import { RecordsViewModel } from '../../knowledge/ViewModels/RecordsViewModel'
import { editorFiles, refusedSpec, sampleImage, sampleInputs, typedLine } from '../../library/Previews/LibraryWorlds'
import { LibraryViewModel } from '../../library/ViewModels/LibraryViewModel'
import type { LibraryConfirm } from '../../library/ViewModels/LibraryFaces'
import { handles } from '../../models/Previews/ModelsWorlds'
import { ModelsViewModel } from '../../models/ViewModels/ModelsViewModel'
import type { ModelsConfirm, ModelsTab } from '../../models/ViewModels/ModelsViewModel'
import { ConvertForm, QuantizeForm } from '../../models/ViewModels/StoreForms'
import { MonitorViewModel } from '../../monitor/ViewModels/MonitorViewModel'
import { reviewForm, sweepsFolder } from '../../review/Previews/ReviewWorlds'
import { ReviewViewModel } from '../../review/ViewModels/ReviewViewModel'
import { settingsPreferences } from '../../settings/Previews/SettingsWorlds'
import { SettingsViewModel } from '../../settings/ViewModels/SettingsViewModel'
import type { SettingsConfirm, SettingsPane } from '../../settings/ViewModels/SettingsViewModel'
import { SessionKeys } from '../../sessions/ViewModels/SessionKeys'
import { sweep } from '../../training/Previews/TrainingWorlds'
import { TrainingViewModel } from '../../training/ViewModels/TrainingViewModel'
import type { TrainingConfirm } from '../../training/ViewModels/TrainingViewModel'
import { KeyMap } from '../../shell/ViewModels/KeyMap'
import type { BoundKey } from '../../shell/ViewModels/KeyMap'
import type { Compactions } from '../../session/ViewModels/Compactions'
import type { SessionViewModel } from '../../session/ViewModels/SessionViewModel'
import type { AttachChoice, FolderListing, SessionLeave } from '../../session/ViewModels/SessionViewModel'
import { SessionLibraryViewModel } from '../../sessions/ViewModels/SessionLibraryViewModel'
import type { ExecuteForm } from '../../sessions/ViewModels/ExecuteForm'
import type { Destination } from '../../shell/ViewModels/Destination'
import { ShellViewModel } from '../../shell/ViewModels/ShellViewModel'
import type { SystemStrip } from '../../shell/ViewModels/SystemStrip'
import { ThemeService } from '../../styles/Appearance/ThemeService'
import { MediaPreferences } from '../../styles/Media/MediaPreferences'
import { clipPath, clips, paperBoat } from '../../video/Previews/VideoWorlds'
import { VideoForm } from '../../video/ViewModels/VideoForm'
import { VideoViewModel } from '../../video/ViewModels/VideoViewModel'
import { sampleText, secondSentence, speeches } from '../../voice/Previews/VoiceWorlds'
import { VoiceViewModel } from '../../voice/ViewModels/VoiceViewModel'
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
  /** The Training place (1j). */
  readonly training: TrainingViewModel
  /** The Settings window (1i), over the same theme service the window applies. */
  readonly settings: SettingsViewModel
  /** The Video place's clips (1k): the library over the folders the CLI writes into, a run and the install, each a job on the shared list. */
  readonly video: VideoViewModel
  /** The compositions (1l): Video's second tab, the projects beside their videos in the same folders, each render a job on the shared list. */
  readonly compose: ComposeViewModel
  /** The window's media preferences (1l): the voice new speech and compositions start from, shared by Settings, Compose and Voice. */
  readonly media: MediaPreferences
  /** The Voice place (1m): the speeches beside their WAVs in `~/Music/Apogee`, the speak card, a speech a job on the shared list, the window's chosen voice its `media`. */
  readonly voice: VoiceViewModel
  /**
   * The Library place (1n): the agents, symphonies and MCP servers, the
   * editor and its close prompt, as the Mac's three view-models -- named
   * for the nav it draws, since `library` is the Sessions place's (1g).
   */
  readonly definitions: LibraryViewModel
  /** The Review place (1n): the merge request reviewed dry run first, the targets and the last sweep, the skills. */
  readonly review: ReviewViewModel
  /** The Knowledge place's decision records (1o): the list, a question answered, a record and its raw conversation. */
  readonly records: RecordsViewModel
  /** The Knowledge place's collections (1o): the list, a collection's page, its search. */
  readonly collections: CollectionsViewModel
  /**
   * The Knowledge tab shown (1o), the route's: the Mac's screen keeps it as
   * its `@State tab`, so no third view-model holds it, and the composition
   * keeps it across visits as the old `kn.tab` stays.
   */
  knowledgeTab: KnowledgeTab = 'records'
  /** The Code graph place (1o): the recorded demo graph's Functions and Files views, the notes prose graph, the More menu. */
  readonly graph: GraphViewModel
  /** Every key the window binds: the shell's and the places' (Sessions'), what Settings › Keyboard lists. */
  readonly boundKeys: readonly BoundKey[] = [...KeyMap.shell, ...SessionKeys.bindings]

  /** Each place's view-model, by its destination: the ones built so far. */
  private readonly places: Partial<Record<Destination, Place>>
  private settingUp: Promise<void> | undefined
  /** What the preset does once its clock has started: the console's runs after its first has played. */
  private afterStart: (() => Promise<void>) | undefined = undefined
  /** Whether the app is wanted running: a stop before its setup has finished keeps the clock stopped. */
  private running = false
  /** The Settings window's reads since it last opened, so a preset waits for them before its frame. */
  private settingsReading: Promise<void> | undefined = undefined
  private settingsWasOpen = false

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
    // Every start preset pins Automatic to dark, as the old prototype draws it (effTheme()), so frames match the baseline (1i's default).
    this.theme.pin('dark')
    this.shell = new ShellViewModel(this.clock, this.theme)
    this.monitor = new MonitorViewModel(this.reads, this.clock)
    this.models = new ModelsViewModel(this.reads, this.jobs, this.clock)
    this.library = new SessionLibraryViewModel(this.chats, this.reads, this.jobs, this.clock, samples)
    this.consoleChild = new StandInChild({ rules: AppViewModel.consoleRules(preset), apogee: preset.apogee }, modes)
    this.console = new ConsoleViewModel(new ReadRunner(this.consoleChild), new JobStarter(this.consoleChild, this.clock, this.registry))
    this.media = new MediaPreferences(settingsPreferences.voice, settingsPreferences.captions)
    this.settings = new SettingsViewModel(this.reads, this.jobs, this.theme, () => this.boundKeys, settingsPreferences, this.media)
    this.training = new TrainingViewModel(this.reads, this.jobs)
    // The folders the CLI writes into are the stand-in's (1k), which its jobs write as they run (1l): Video and Compose read the same.
    this.video = new VideoViewModel(this.reads, this.jobs, this.child.folders, this.clock)
    this.compose = new ComposeViewModel(this.reads, this.jobs, this.child.folders, this.clock, this.video.playback, this.media)
    // Voice (1m) over the same folders, the one player and the window's chosen voice; its card holds the old frame's sample on every preset, as the old `vf` starts.
    this.voice = new VoiceViewModel(this.reads, this.jobs, this.child.folders, this.clock, this.video.playback, this.media)
    this.voice.setText(sampleText)
    // Library and Review (1n) over the same reads, jobs, clock and folders; the old pages' samples and the old Reviews tab's form handed in, as Voice's card is.
    this.definitions = new LibraryViewModel(this.reads, this.jobs, this.child.folders, this.clock)
    this.definitions.useSamples(sampleInputs, sampleImage)
    this.review = new ReviewViewModel(this.reads, this.jobs, this.child.folders)
    this.review.setForm(reviewForm)
    this.review.sweeps = () => [sweepsFolder]
    // Knowledge and the Code graph (1o) over the same reads and jobs; the graph's world -- the recorded graphs, the questions whose pictures it shows, where a save goes -- handed in, as Review's form is.
    this.records = new RecordsViewModel(this.reads)
    this.collections = new CollectionsViewModel(this.reads)
    this.collections.recordedSearches = recordedSearches
    // What an empty field asks: the old kno.ask's and kno.search's questions, filled in, as the old always shows them asked.
    this.records.emptyQuestion = recordsQuestion
    this.collections.emptyQuestions = { notes: collectionQuestion }
    this.graph = new GraphViewModel(this.reads, this.jobs)
    this.graph.useWorld(graphWorld)
    const knowledge: Place = {
      appeared: () => {
        void this.records.appeared()
        void this.collections.appeared()
      },
    }
    this.places = { models: this.models, training: this.training, video: this.video, voice: this.voice, library: this.definitions, review: this.review, knowledge, graph: this.graph }

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
    // Manage key… opens Settings on Backends & keys; Add an API key… opens it with that provider's key editor open, as the old one sets `keyEdit: sm.key` (1i).
    this.models.openSettings = (provider) => {
      this.openSettings('backends')
      if (provider !== undefined) this.settings.editKey(provider)
    }
    this.models.goTo = (place) => this.shell.go(place)
    this.models.subscribe(() => this.changed())
    // The Training place's doors (1j): the toast, the shell's sheet and confirm hosts.
    this.training.say = (text) => this.shell.say(text)
    this.training.present = (sheet) => this.shell.openSheet(sheet)
    this.training.dismissSheet = () => this.shell.closeSheet()
    this.training.ask = (kind) => this.shell.ask(kind)
    this.training.subscribe(() => this.changed())
    // The Video place's doors (1k): the toast, the consent sheet's host, Models' Stored weights at the default, the place itself (where an install from Settings draws its card), and the shared job list its installs wait on.
    this.video.say = (text) => this.shell.say(text)
    this.video.present = (sheet) => this.shell.openSheet(sheet)
    this.video.dismissSheet = () => this.shell.closeSheet()
    this.video.openModels = (handle) => {
      this.shell.closeOverlay()
      this.shell.go('models')
      this.models.showStore()
      void this.models.store.select(handle)
    }
    this.video.showPlace = () => this.shell.go('video')
    this.video.registry = this.registry
    // Video's second tab (1l): its count the compositions' own, its New composition theirs.
    this.video.compositionsCount = () => this.compose.projects.length
    this.video.newComposition = () => this.compose.newComposition()
    this.video.subscribe(() => this.changed())
    // The compositions' doors (1l): the toast, the folders they are written to and read from, the scan's passed-over list the stand-in serves, and the brief the recorded plan was made from.
    this.compose.say = (text) => this.shell.say(text)
    this.compose.folders = () => [moviesFolder]
    this.compose.passedOver = passedOver
    this.compose.startingBrief = recordedBrief
    this.compose.subscribe(() => this.changed())
    // The Voice place's doors (1m): the toast, and Models' Stored weights at its voice model (the header's line, the old openStoreFor('kokoro')).
    this.voice.say = (text) => this.shell.say(text)
    this.voice.openModels = (handle) => {
      this.shell.closeOverlay()
      this.shell.go('models')
      this.models.showStore()
      void this.models.store.select(handle)
    }
    this.voice.subscribe(() => this.changed())
    // Library's and Review's doors (1n): the toast, and the shell's sheet and confirm hosts -- the editor, the add-target sheet, the close prompt, a server's removal, the post.
    for (const place of [this.definitions, this.review]) {
      place.say = (text) => this.shell.say(text)
      place.present = (sheet) => this.shell.openSheet(sheet)
      place.dismissSheet = () => this.shell.closeSheet()
      place.ask = (kind) => this.shell.ask(kind)
      place.subscribe(() => this.changed())
    }
    // Knowledge's and the Code graph's doors (1o): the toast; the graph's More menu through the shell's scrim, its save panel and dedupe sheet through the sheet host, Delete and a refused explain through the confirm host,
    // and Ask about this a new session on Sessions with the node's file attached and the old draft, through the open session's own leave -- asked first where a private conversation would be lost -- never an import of `session`.
    for (const place of [this.records, this.collections]) {
      place.say = (text) => this.shell.say(text)
      place.subscribe(() => this.changed())
    }
    this.graph.say = (text) => this.shell.say(text)
    this.graph.present = (sheet) => this.shell.openSheet(sheet)
    this.graph.dismissSheet = () => this.shell.closeSheet()
    this.graph.ask = (kind) => this.shell.ask(kind)
    this.graph.menuChanged = (owner) => (owner === undefined ? this.shell.closeMenus() : this.shell.openMenu(owner))
    this.graph.askAbout = (file, draft) => this.session.leave({ type: 'new', attach: file, draft })
    this.graph.subscribe(() => this.changed())
    this.registry.onChange(() => this.changed())
    // The monitor lists the video runs going now from the shared job list (1k), and its Stop stops that run (a composition's Open shows it, 1l): the place's
    // own clip only once its pipeline has loaded (the old lists `vrun` past `load`), by what it generates on and where;
    // any other -- a generate the console runs, 1l's compositions -- by its label, as the badge counts it.
    this.monitor.running = () =>
      this.registry.children('job').flatMap((child) => {
        // A speech being spoken (1m), as the old lists `vsay` from its start: the place's own by its voice model speaking and the device it said, any other by its label; its Stop the old row's.
        if (AppViewModel.isVoiceRun(child)) {
          const speaking = this.voice.speaking
          const own = speaking !== undefined && speaking.arguments.length === (child.arguments?.length ?? 0) && speaking.arguments.every((word, index) => child.arguments?.[index] === word)
          return [{ id: child.id, name: own ? speaking.name : child.label, kind: 'voice' as const, ...(own && speaking.runtime !== undefined ? { runtime: speaking.runtime } : {}) }]
        }
        if (!AppViewModel.isVideoRun(child)) return []
        const same = (args: readonly string[]) => args.length === (child.arguments?.length ?? 0) && args.every((word, index) => child.arguments?.[index] === word)
        // A composition this place renders, by its video, as the old lists `crender` (1l): `heat-pump-2.mp4 · rendering`, with its Open.
        const render = this.compose.rendering
        if (render !== undefined && same(render.arguments)) return [{ id: child.id, name: render.name, opens: true as const, ...(render.runtime !== undefined ? { runtime: render.runtime } : {}) }]
        const clip = this.video.generating
        const own = clip !== undefined && same(clip.arguments)
        if (!own) return [{ id: child.id, name: child.label }]
        const loaded = clip.loaded
        return loaded === undefined ? [] : [{ id: child.id, name: loaded.name, ...(loaded.runtime !== undefined ? { runtime: loaded.runtime } : {}) }]
      })
    this.monitor.stopRun = (id) => void this.registry.stopChild(id)
    // A composition's Open (1l): the monitor closed onto Video's Compositions at the render, as the old crender row's `monitor: false, screen: 'video', mediaTab: 'compose', projSel`.
    this.monitor.openRun = () => {
      const output = this.compose.rendering?.output
      this.shell.closeOverlay()
      this.shell.go('video')
      this.video.pick('compositions')
      if (output !== undefined) this.compose.select(output)
    }

    // The links between places, as closures: the monitor's two doors.
    this.monitor.openLibrary = () => {
      this.shell.closeOverlay()
      this.shell.go('models')
      this.models.showStore()
    }
    // Unload says what the old prototype says: the open session's model is
    // held by its own child, which the CLI unloads with it. A video run
    // (1k) and a composition rendering (1l) are no model held: the monitor
    // lists them from the shared job list with their own Stop (above), and
    // so does a speech (1m).
    this.monitor.unload = (name) => this.shell.say(`Unloaded ${name}`)
    // The Settings window's doors (1i): the toast, the shell's sheet and confirm hosts, Models' Backends; Models & roles has the Models place read the roles its slot draws.
    this.settings.say = (text) => this.shell.say(text)
    this.settings.present = (kind) => this.shell.openSheet(kind)
    this.settings.dismissSheet = () => this.shell.closeSheet()
    this.settings.ask = (kind) => this.shell.ask(kind)
    this.settings.openLibrary = () => {
      this.closeSettings()
      this.openModels('backends')
    }
    this.settings.showing = (pane) => {
      if (pane === 'roles') void this.models.refresh()
    }
    // Install… on the video runtime's row (1k, the old "Install peft…"): the shared consent sheet, over the install its remedy names, held by the Video place.
    this.settings.askInstall = (said, remedy) => {
      const args = MediaLibrary.installCommand(remedy)
      if (args) this.video.ask({ arguments: args, said })
    }
    this.shell.routeChanged = (destination) => {
      this.library.showing(destination === 'sessions')
      this.places[destination]?.appeared?.()
      // Video's second tab reads its projects with the place (1l), so its count is theirs.
      if (destination === 'video') void this.compose.appeared()
    }
    // The monitor reads while it, or the strip that shows its numbers, is open;
    // the scrim the shell draws over an open menu closes the session's and the list's too.
    this.shell.subscribe(() => {
      this.watch()
      // The window opened, by ⌘, or a door: every pane's reads.
      if (this.shell.settingsOpen && !this.settingsWasOpen) this.settingsReading = this.settings.appeared()
      this.settingsWasOpen = this.shell.settingsOpen
      if (this.shell.menu === undefined && this.session.menu !== undefined) this.session.closeMenus()
      if (this.shell.menu === undefined && this.library.rowMenu !== undefined) this.library.closeRowMenu()
      if (this.shell.menu === undefined && this.graph.moreOpen) this.graph.closeMore()
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

  /** Whether a job is a video run: a clip, or a composition rendering (1l) -- what the Video badge and the monitor count, as the old counts `vrun` and `crender`: never a plan or an edit's preview on paper (`--dry-run`), nor an install. */
  private static isVideoRun(child: { readonly label: string; readonly arguments?: readonly string[] }): boolean {
    const { label } = child
    if (label === 'apogee video generate' || label.startsWith('apogee video generate ')) return true
    return (label === 'apogee video compose' || label.startsWith('apogee video compose ')) && !(child.arguments ?? []).includes('--dry-run')
  }

  /** Whether a job is a speech being spoken (1m): what the Voice badge and the monitor count, as the old counts `vsay`. */
  private static isVoiceRun(child: { readonly label: string }): boolean {
    return child.label === 'apogee voice say' || child.label.startsWith('apogee voice say ')
  }

  /**
   * The places' running counts, the sidebar's badges (design 1h): read from
   * the shared registry of children, never a place's own list -- Models'
   * is every store run going on, a pull, a conversion, a quantization or a
   * check of the files, whoever started it.
   */

  get placeRuns(): { readonly models: number; readonly training: number; readonly video: number; readonly voice: number } {
    const store = ['pull', 'convert', 'quantize', 'repair']
    const jobs = this.registry.children('job')
    // Training's (1j) is a run, a pipeline or a cycle pass going on, as the old badge counts `running || live`: never a promote, a pull or a prepare.
    const trains = ['apogee train run', 'apogee train pipeline run', 'apogee train pipeline resume', 'apogee train cycle run']
    return {
      models: jobs.filter((job) => store.some((verb) => job.label.startsWith(`apogee models ${verb} `))).length,
      training: jobs.filter((job) => trains.some((words) => job.label === words || job.label.startsWith(`${words} `))).length,
      // Video's (1k) is a clip being made (and 1l's compositions rendering), as the old badge counts `vrun` and `crender`: never an install.
      video: jobs.filter((job) => AppViewModel.isVideoRun(job)).length,
      // Voice's (1m) is a speech being spoken, as the old badge counts `vsay`.
      voice: jobs.filter((job) => AppViewModel.isVoiceRun(job)).length,
    }
  }

  /** Models, on the tab a door names: Backends at a model, or Roles (the inspector's two doors, design 1h). */
  openModels(tab: ModelsTab = 'backends', model?: string): void {
    this.shell.closeOverlay()
    this.shell.go('models')
    if (tab === 'backends') void this.models.openBackend(model)
    else this.models.pick(tab)
  }

  /** Settings open on `pane`: the pane shown, then the window opened, which reads it. */
  openSettings(pane: SettingsPane): void {
    this.settings.pick(pane)
    this.shell.perform({ kind: 'openSettings' })
  }

  /** Done: the window closed, the key being typed and its failure line with it. */
  closeSettings(): void {
    this.shell.closeSettings()
    this.settings.closed()
  }

  /** A Settings confirm answered yes; the host closed first. */
  async confirmSettings(kind: SettingsConfirm): Promise<void> {
    this.shell.dismissConfirm()
    await this.settings.confirm(kind)
  }

  /** A Settings confirm's Cancel. */
  dismissSettings(kind: SettingsConfirm): void {
    this.shell.dismissConfirm()
    this.settings.dismissPending(kind)
  }

  /** Register…'s sheet: Cancel, or Register. */
  cancelRegister(): void {
    this.settings.cancelRegister()
  }

  /** A Training confirm answered: the act asked about, or Change the promotion…; the host closed first (1j). */
  async confirmTraining(kind: TrainingConfirm): Promise<void> {
    this.shell.dismissConfirm()
    await this.training.confirm(kind)
  }

  /** A Training confirm's Cancel, or the refusal's OK. */
  dismissTraining(kind: TrainingConfirm): void {
    this.shell.dismissConfirm()
    this.training.dismissPending(kind)
  }

  /** A Library confirm answered (1n): the close prompt's Discard, Put it back as it was or Leave it (`alternative`), or a server's removal; the host closed first. */
  async confirmDefinitions(kind: LibraryConfirm, alternative: boolean): Promise<void> {
    this.shell.dismissConfirm()
    await this.definitions.confirm(kind, alternative)
  }

  /** A Library confirm's Cancel: Keep editing, or the removal not made. */
  dismissDefinitions(kind: LibraryConfirm): void {
    this.shell.dismissConfirm()
    this.definitions.dismissPending(kind)
  }

  /** A Knowledge tab picked (1o): Records or Collections, kept as the old `kn.tab` is. */
  pickKnowledgeTab(tab: KnowledgeTab): void {
    this.knowledgeTab = tab
    this.changed()
  }

  /** A Code graph confirm answered (1o): Delete the graph, or a refused explain's OK; the host closed first. */
  async confirmGraph(kind: GraphConfirm): Promise<void> {
    this.shell.dismissConfirm()
    if (kind === 'graphDelete') await this.graph.confirmDelete()
    else this.graph.dismissProblem()
  }

  /** A Code graph confirm's Cancel: the delete not made, or the refusal's alert gone, which is its Clear. */
  dismissGraph(kind: GraphConfirm): void {
    this.shell.dismissConfirm()
    if (kind === 'graphDelete') this.graph.dismissDelete()
    else this.graph.dismissProblem()
  }

  /** The post sheet's Post (1n): it closes, as the old one does, running nothing. */
  confirmReviewPost(): void {
    this.shell.dismissConfirm()
    this.review.confirmPost()
  }

  /** The post sheet's Cancel. */
  dismissReviewPost(): void {
    this.shell.dismissConfirm()
    this.review.dismissPost()
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
    this.training.connect(binary)
    this.video.connect(binary)
    this.compose.connect(binary)
    this.voice.connect(binary)
    this.definitions.connect(binary)
    this.review.connect(binary)
    this.records.connect(binary)
    this.collections.connect(binary)
    this.graph.connect(binary)
    // The console's child shares the preset's mode, so it finds the binary where `child` does.
    this.console.connect(binary)
    // Settings, with the override this launch's discovery passed over, as the app's preferences hold it.
    this.settings.connect(binary, settingsPreferences.passedOver)
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
    if (where === 'settings') {
      await this.openSettingsPreset(sub ?? 'general')
      return
    }
    if (where === 'training') {
      await this.openTrainingPreset(sub ?? '')
      return
    }
    if (where === 'video' || where === 'consent') {
      await this.openVideoPreset(where === 'consent' ? 'consent' : (sub ?? ''))
      return
    }
    if (where === 'compose') {
      await this.openComposePreset(sub ?? '')
      return
    }
    if (where === 'voice') {
      await this.openVoicePreset()
      return
    }
    if (where === 'library') {
      await this.openLibraryPreset(sub ?? '')
      return
    }
    if (where === 'review') {
      await this.openReviewPreset(sub ?? '')
      return
    }
    if (where === 'knowledge') {
      await this.openKnowledgePreset(sub ?? '')
      return
    }
    if (where === 'graph') {
      await this.openGraphPreset(sub ?? '')
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
   * A Training preset (design 1j), as the old `applyStart('training:…')`
   * sets it up: the Streaming session at its starting frame behind it, as the
   * Models and Settings presets keep it, the place read, then its tab and the
   * old frame's state -- a run training (`training`, New run… pressed, so
   * the run the CLI recorded), the `skills` pipeline started (its start ticks
   * stand it at stage 2, iteration 2), Regimes on `foundation`, or one run's
   * page.
   */
  private async openTrainingPreset(sub: string): Promise<void> {
    await this.openStreaming()
    const training = this.training
    this.shell.go('training')
    await training.refresh()
    const tab = ({ '': 'runs', datasets: 'datasets', pipelines: 'pipelines', cycle: 'cycle', regimes: 'regimes', stalelock: 'cycle', unreadable: 'cycle', cyclepass: 'pipelines', passrunning: 'pipelines', resumeheld: 'pipelines' } as const)[sub as 'cycle'] ?? 'runs'
    training.pick(tab)
    switch (sub) {
      case '':
        training.newRun()
        return
      case 'pipelines':
        training.requestRun('skills')
        await this.confirmTraining('train')
        return
      case 'regimes':
        training.selectRegime('foundation')
        return
      case 'cyclepass':
      case 'passrunning':
        await training.openPipelineRun(sweep.cyclePass)
        return
      case 'resumeheld':
        await training.openPipelineRun(sweep.held)
        return
    }
  }

  /**
   * A Video preset (design 1k), as the old `applyStart('video…' | 'consent')`
   * sets it up: the Streaming session at its starting frame behind it, the
   * place read and shown on Clips, the new-clip form holding the old frame's
   * paper boat, then the preset's state -- `video` the final made from its
   * draft, paused 2.1 s in; `video:run` that draft made final, running, its
   * start ticks standing it at step 38 of 50; `consent` rain run again,
   * refused for peft, the sheet asking; `video:install` the same, and once
   * the refusal has played, Install pressed, its start ticks standing it 4
   * ticks in. Each is the CLI's own run through the stand-in, never a state
   * set by hand.
   */
  private async openVideoPreset(sub: string): Promise<void> {
    await this.openStreaming()
    const video = this.video
    this.shell.go('video')
    await video.appeared()
    video.pick('clips')
    video.edit({ prompt: paperBoat })
    const record = (file: string) => video.clips.find((clip) => clip.output === clipPath(file))
    const rain = record(clips.rain)
    switch (sub) {
      case '':
        video.select(clipPath(clips.final))
        video.playback.hold(`clip:${clipPath(clips.final)}`, 2.1)
        return
      case 'run': {
        const draft = record(clips.draft)
        if (draft) video.setForm({ ...VideoForm.from(draft, 'from'), preset: 'final', steps: '50' })
        video.generate()
        return
      }
      case 'consent':
      case 'install':
        if (!rain) return
        video.select(rain.output)
        video.setForm(VideoForm.from(rain, 'from'))
        video.generate()
        if (sub === 'install') {
          // The refusal plays within the start ticks; once its consent asks, Install, which the frame's ticks catch up to 4 ticks in.
          this.afterStart = async () => {
            for (let round = 0; round < 20 && video.consent === undefined; round += 1) await new Promise((resolve) => setTimeout(resolve, 0))
            video.install()
          }
        }
        return
    }
  }

  /**
   * A Compose preset (design 1l), as the old `applyStart('compose…')` sets
   * it up: the Streaming session at its starting frame behind it, Video read
   * and shown on Compositions, read, then the preset's state -- `compose`
   * the heat pump paused 9.6 s in; `compose:edit` Tides, made, its recorded
   * edit typed and previewed (the dry run the CLI recorded); `compose:new`
   * a new composition, the recorded brief planned; `compose:tide` the tide
   * project, stopped; `compose:differs` the Tides edit previewed and then
   * rendered, once its preview has landed, its render's own plan come out
   * otherwise. Each is the CLI's own run through the stand-in, never a
   * state set by hand.
   */
  private async openComposePreset(sub: string): Promise<void> {
    await this.openStreaming()
    const compose = this.compose
    this.shell.go('video')
    await this.video.appeared()
    this.video.pick('compositions')
    await compose.appeared()
    switch (sub) {
      case '':
        compose.select(projects.heatPump)
        compose.hold(heatPumpFrame)
        return
      case 'tide':
        compose.select(projects.tidePools)
        return
      case 'new':
        compose.newComposition()
        compose.compose(true)
        return
      case 'edit':
      case 'differs': {
        compose.select(projects.tides)
        compose.editForm({ instruction: recordedEdit })
        const previewed = compose.previewEdit()
        // The render, once the preview has landed: its frame's ticks catch it up to its end.
        if (sub === 'differs')
          this.afterStart = async () => {
            await previewed
            compose.renderEdit()
          }
        return
      }
    }
  }

  /**
   * The Voice preset (design 1m), as the old `applyStart('voice')` sets it
   * up: the Streaming session at its starting frame behind it, as the Video
   * presets keep it, the place read and shown, `speech-torch.json` selected
   * and its player paused on its second sentence (the old one pauses 3.4 s
   * into its second), the card holding the old sample.
   */
  private async openVoicePreset(): Promise<void> {
    await this.openStreaming()
    this.shell.go('voice')
    await this.voice.appeared()
    this.voice.select(speeches.torch)
    this.voice.hold(secondSentence)
  }

  /**
   * A Library preset (1n), as the old `applyStart('library…')` sets it up:
   * the Streaming session at its starting frame behind it, as the Voice
   * preset keeps it, the place read and shown, then its tab and state --
   * `library` the agents, triage's page; `library:symphonies` critique-revise
   * played, the recorded play within the frame's ticks; `library:mcp` the
   * server; `library:unsaved` the triage prompt open with the old line
   * typed, Close asked; `library:refused` notes-digest saved with the
   * unknown key, apogee's refusal after the save's 4 ticks, then Close asked;
   * `library:image` describe-answer, no image chosen. Each is the CLI's own
   * answer through the stand-in, never a state set by hand.
   */
  private async openLibraryPreset(sub: string): Promise<void> {
    await this.openStreaming()
    const library = this.definitions
    this.shell.go('library')
    await library.appeared()
    switch (sub) {
      case 'symphonies':
        library.pick('symphonies')
        library.symphonies.play()
        return
      case 'mcp':
        library.pick('mcp')
        return
      case 'unsaved':
        library.agents.edit('prompts/triage.txt')
        library.setEditorText(editorFiles.triage + typedLine)
        library.requestClose()
        return
      case 'refused': {
        library.pick('symphonies')
        library.pickRow('notes-digest')
        library.symphonies.edit()
        library.setEditorText(refusedSpec(editorFiles.notesDigest))
        // The save waits out its 4 ticks, which the frame's start ticks play; then Close asks.
        const saved = library.save()
        this.afterStart = async () => {
          await saved
          library.requestClose()
        }
        return
      }
      case 'image':
        library.pick('symphonies')
        library.pickRow('describe-answer')
        return
    }
  }

  /**
   * A Review preset (1n), as the old `applyStart('review…')` sets it up: the
   * Streaming session at its starting frame behind it, the place read and
   * shown, then its tab -- `review` and `review:post` the recorded dry run of
   * the old form's merge request, run as the Mac's `review()` runs it (the
   * tab's own Review button stays dead, the document's default), and
   * `review:post`'s post sheet asked once the run has landed.
   */
  private async openReviewPreset(sub: string): Promise<void> {
    await this.openStreaming()
    const review = this.review
    this.shell.go('review')
    await review.appeared()
    switch (sub) {
      case '':
        review.review()
        return
      case 'post':
        review.review()
        this.afterStart = async () => {
          await review.ran
          review.askToPost()
        }
        return
      case 'targets':
        review.pick('targets')
        return
      case 'skills':
        review.pick('skills')
        return
    }
  }

  /**
   * A Knowledge preset (1o), as the old `applyStart('knowledge…')` sets it
   * up: the Streaming session at its starting frame behind it, as the Review
   * presets keep it, the place read and shown, then its tab and state --
   * `knowledge` Records, the old question asked (knowledge-query.json) and
   * the rejected record open; `knowledge:collections` notes searched
   * (collection-query.txt); `knowledge:mixed` mixed, its Vectors line, nothing
   * searched. Each is the CLI's own answer through the stand-in.
   */
  private async openKnowledgePreset(sub: string): Promise<void> {
    await this.openStreaming()
    this.knowledgeTab = sub === 'collections' || sub === 'mixed' ? 'collections' : 'records'
    this.shell.go('knowledge')
    await this.records.appeared()
    await this.collections.appeared()
    switch (sub) {
      case '':
        this.records.setQuestion(recordsQuestion)
        await this.records.ask()
        this.records.select(rejectedRecord)
        return
      case 'collections':
        await this.collections.select('notes')
        this.collections.setQuestion(collectionQuestion)
        await this.collections.search()
        return
      case 'mixed':
        await this.collections.select('mixed')
        return
    }
  }

  /**
   * A Code graph preset (1o), as the old `applyStart('graph…')` sets it up:
   * the Streaming session at its starting frame behind it, the place read
   * and shown -- `graph` the Functions view of the recorded demo graph,
   * Runner.run_batch open; `graph:prose` the notes prose graph;
   * `graph:refused` a picture an update elsewhere overtook (the old
   * `gStale`), Runner._fetch clicked and its explain refused by the CLI, the
   * alert up over the canvas.
   */
  private async openGraphPreset(sub: string): Promise<void> {
    await this.openStreaming()
    this.shell.go('graph')
    if (sub === 'prose') {
      await this.graph.show('prose')
      return
    }
    await this.graph.appeared()
    if (sub === 'refused') {
      this.graph.overtaken()
      await this.graph.select(overtakenNode)
    }
  }

  /**
   * A Settings preset (1i), as the old `applyStart('settings:…')` sets it up:
   * the Streaming session at its starting frame behind it (1v's setup, as
   * `chat:compacting` opens it), the window open on the pane its name says,
   * read; `fixing` and `fixfailed` run `check --fix`, the one still running
   * at the frame, the other ended failed within the preset's start ticks;
   * `nosearch` is the Web access pane over the unset address.
   */
  private async openSettingsPreset(sub: string): Promise<void> {
    await this.openStreaming()
    const pane: SettingsPane = sub === 'fixing' || sub === 'fixfailed' ? 'backends' : sub === 'nosearch' ? 'web' : (sub as SettingsPane)
    this.openSettings(pane)
    await this.settingsReading
    if (sub === 'fixing' || sub === 'fixfailed') this.settings.fix(['check', '--fix'])
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
