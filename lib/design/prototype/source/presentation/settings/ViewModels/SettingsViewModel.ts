import { JobProgress } from '../../../business/jobs/JobProgress'
import type { JobRunning, JobStarting } from '../../../business/jobs/SupervisedJob'
import { RecallListing } from '../../../business/reads/Chats'
import { CheckReport } from '../../../business/reads/Check'
import type { CheckRow } from '../../../business/reads/Check'
import { CommandOutcome } from '../../../business/reads/CommandRunning'
import type { CommandRunning } from '../../../business/reads/CommandRunning'
import { AllowedHosts, Permissions, ServerTools } from '../../../business/reads/ConfigVerbs'
import type { PermissionLevel } from '../../../business/reads/ConfigVerbs'
import { Field, ModelRow, RoleAssignment } from '../../../business/reads/Models'
import { KeyedProviders, Provider, ProviderRegistration } from '../../../business/reads/Providers'
import { ReadOutcome } from '../../../business/reads/ReadRunner'
import { keyboardLines, KeyMap } from '../../shell/ViewModels/KeyMap'
import type { BoundKey, KeyReference } from '../../shell/ViewModels/KeyMap'
import type { ThemeService } from '../../styles/Appearance/ThemeService'
import { Observable } from '../../styles/Observation/Observable'
import { ThemeSettings } from './ThemeSettings'
import type { AppearanceFace, Choice } from './ThemeSettings'
import { XcodeSettings } from './XcodeSettings'
import type { ConfirmWords, Said, XcodeFace } from './XcodeSettings'

/**
 * The ten panes, in the old prototype's order and words (its `panes` list,
 * `main.html:6203` at `b375fac9`). The Mac app's `SettingsViewModel.Pane` has
 * six -- general, appearance, backends, media, permissions, xcode -- and keeps
 * the Keyboard list in General, the allowed hosts and recall in Permissions,
 * and the roles in Models: the prototype is the source of truth (1i).
 */
export type SettingsPane = 'general' | 'appearance' | 'roles' | 'media' | 'backends' | 'permissions' | 'web' | 'memory' | 'keyboard' | 'xcode'

export const settingsPanes: ReadonlyArray<readonly [SettingsPane, string]> = [
  ['general', 'General'],
  ['appearance', 'Appearance'],
  ['roles', 'Models & roles'],
  ['media', 'Video & voice'],
  ['backends', 'Backends & keys'],
  ['permissions', 'Permissions'],
  ['web', 'Web access'],
  ['memory', 'Memory'],
  ['keyboard', 'Keyboard'],
  ['xcode', 'Xcode'],
]

/** An entry of a permission list: what was allowed, and from where. */
export interface AllowedEntry {
  readonly label: string
  readonly from: string
  /** The tool it allows, where it is one. */
  readonly tool?: string
}

/**
 * The app's own settings, never the CLI's -- the counterpart of the Mac's
 * `Preferences`, as a preset starts from them: the session defaults
 * (thinking, effort, saved or private, what opens), the media defaults (the
 * voice, captions, the save folders), the binary the person pointed the app
 * at, and the permission cards' allowances. The Mac differs: of these its
 * `Preferences` keeps only `binaryOverride` (its `videoFolders` and
 * `voiceFolders` are folders added to the libraries, not where things are
 * saved), and its Settings draws none of the others -- they are the old
 * prototype's, kept because it is the source of truth (1i). Nothing persists
 * across a reload (1i's default), so these are the start, changed in memory.
 */
export interface SettingsPreferences {
  readonly thinking: 'off' | 'on' | 'auto'
  readonly effort: string
  readonly newSessions: 'saved' | 'private'
  readonly onLaunch: 'new' | 'last'
  readonly voice: string
  readonly captions: boolean
  readonly saveTo: { readonly video: string; readonly speech: string }
  /** The binary the person pointed the app at; the next launch looks there first. */
  readonly binaryOverride: string | undefined
  readonly always: readonly AllowedEntry[]
  readonly session: readonly AllowedEntry[]
}

/** The override this launch's discovery passed over while another binary answered, and why, in discovery's words (the Mac's `skippedOverride`). */
export interface PassedOver {
  readonly path: string
  readonly reason: string
}

/** The confirms the panes raise, by kind: Memory's forget (`settingsForget`, apart from the inspector's `forget`), and Xcode's remove. */
export type SettingsConfirm = 'settingsForget' | 'xcodeRemove'

/** The sheet the panes raise: Register… on a scanned provider. */
export type SettingsSheet = 'registerProvider'

/** A button of a row: its words, the old prototype's class, and what it does. */
export interface RowAction {
  readonly label: string
  readonly kind: 'btn' | 'btn-g'
  readonly act: { readonly case: 'editKey' | 'removeKey'; readonly provider: string } | { readonly case: 'installRuntime'; readonly row: string }
}

/** One row of Backends & keys: the old `backendRow`. */
export interface BackendRowFace {
  readonly kind: 'local' | 'api' | 'subscription'
  readonly name: string
  readonly status: string
  readonly statusColor: string
  readonly detail: string
  /** Lines under the detail: a vendor's consent, or what mends a runtime. */
  readonly consent: readonly string[]
  readonly consentColor: string
  readonly actions: readonly RowAction[]
}

/** One API key row: a backend row with its provider and the key's editor. */
export interface KeyRowFace extends BackendRowFace {
  readonly provider: string
  readonly editing: boolean
  readonly placeholder: string
}

/** Everything the Settings window draws, as words: what the per-preset walk reads. */
export interface SettingsFace {
  readonly pane: SettingsPane
  readonly panes: ReadonlyArray<{ readonly id: SettingsPane; readonly label: string; readonly current: boolean }>
  readonly title: string
  /** The red line under the title: why nothing could be read, or the last change's failure, in the CLI's words. */
  readonly failure: string
  /** The nav's foot: the install, as `check` describes it. */
  readonly foot: readonly string[]
  /** What runs now, in a few words. */
  readonly working: string
  readonly general: {
    readonly model: { readonly value: string; readonly options: readonly Choice[] }
    readonly thinking: ReadonlyArray<Choice<'off' | 'on' | 'auto'> & { readonly current: boolean }>
    readonly effort: { readonly value: string; readonly options: readonly Choice[] }
    readonly newSessions: ReadonlyArray<Choice<'saved' | 'private'> & { readonly current: boolean }>
    readonly onLaunch: ReadonlyArray<Choice<'new' | 'last'> & { readonly current: boolean }>
    readonly cli: { readonly lines: readonly string[]; readonly passedOver: string; readonly useInstalled: boolean; readonly next: string }
  }
  readonly appearance: AppearanceFace
  readonly media: {
    readonly video: { readonly value: string; readonly options: readonly Choice[] }
    readonly voiceModel: string
    readonly voice: string
    readonly voices: readonly Choice[]
    readonly captions: boolean
    readonly captionsNote: string
    readonly saveTo: readonly string[]
    readonly runtimes: ReadonlyArray<{ readonly name: string; readonly status: string; readonly color: string }>
  }
  readonly backends: {
    /** The sections above the API keys: the backends on this Mac, the Python environment's runtimes; each only where it has a row. */
    readonly sections: ReadonlyArray<{ readonly title: string; readonly rows: readonly BackendRowFace[] }>
    /** CLOUD APIS · BILLED PER USE: one row a provider `auth add` takes. */
    readonly keys: readonly KeyRowFace[]
    /** The section below them: the subscriptions, through the vendor's app; none where nothing is configured. */
    readonly subscriptions: ReadonlyArray<{ readonly title: string; readonly rows: readonly BackendRowFace[] }>
    readonly keyDraft: string
    readonly keyEmpty: boolean
    readonly providers: ReadonlyArray<{ readonly id: string; readonly label: string; readonly state: string; readonly canRegister: boolean }>
    readonly registered: { readonly shown: boolean; readonly title: string; readonly text: string }
    readonly health: {
      readonly summary: string
      readonly summaryColor: string
      readonly rows: ReadonlyArray<{ readonly text: string; readonly color: string; readonly remedy: string }>
      readonly fixing: boolean
      readonly busy: string
      readonly command: string
      readonly canStop: boolean
    }
  }
  readonly register: { readonly shown: boolean; readonly title: string; readonly subtitle: string; readonly needsModel: boolean; readonly model: string; readonly command: string; readonly canRegister: boolean }
  readonly permissions: {
    readonly tools: ReadonlyArray<{ readonly name: string; readonly desc: string; readonly level: string; readonly levels: readonly Choice[] }>
    readonly always: readonly AllowedEntry[]
    readonly session: readonly AllowedEntry[]
    readonly noAlways: boolean
    readonly noSession: boolean
  }
  readonly web: {
    readonly sites: readonly string[]
    readonly siteDraft: string
    readonly siteEmpty: boolean
    readonly search: string
    readonly searchShownColor: string
    readonly searchDraft: string
    readonly searchEmpty: boolean
    readonly searchNote: string
    readonly searchColor: string
  }
  readonly memory: { readonly recall: boolean; readonly recallLabel: string; readonly holds: string; readonly canForget: boolean }
  readonly keyboard: readonly KeyReference[]
  readonly xcode: XcodeFace
}

/** The old pane's four tools, its words for each (the Permissions pane at `b375fac9`); any other the config lists follows, with none. */
const toolWords: ReadonlyArray<readonly [string, string]> = [
  ['write_file', 'Create or change files.'],
  ['run_command', 'Run shell commands.'],
  ['fetch_url', 'Read a web page. Ask means the first time for each website.'],
  ['web_search', 'Search through your SearXNG.'],
]

/** A key's provider as its company writes it, and what its keys look like: the old pane's `keyRow` words. */
const keyWords: Readonly<Record<string, { readonly name: string; readonly placeholder: string }>> = {
  anthropic: { name: 'Anthropic API', placeholder: 'sk-ant-…' },
  openai: { name: 'OpenAI API', placeholder: 'sk-…' },
  google: { name: 'Google API', placeholder: 'AIza…' },
}

/** The voice the CLI speaks with when none is named, as the pane's note says it (`af_heart is Kokoro’s own recommendation…`). */
const defaultVoice = 'af_heart'

/** The vendor CLIs' backend types: a subscription, through the vendor's app. */
const subscriptionTypes: readonly string[] = ['claude-cli', 'codex-cli', 'gemini-cli', 'ollama-cli']

/**
 * Whether a runtime's row is the one the Video place installs (design 1k):
 * its remedy the video set's install, as the old "Install peft…" sat on the
 * video row. The voice runtime's is Voice's (1m), which holds its install.
 */
function installsVideo(row: CheckRow): boolean {
  return /\bapogee train setup (--rebuild )?--with video\b/.test(row.remedy ?? '')
}

/** The runtimes the Python environment holds, as `check`'s Video section names its rows: what Backends & keys and Video & voice list. */
const runtimeRows: readonly string[] = ['runtime', 'voice runtime', 'ffmpeg captions', 'ffmpeg h264']

const statusColor = (status: string) => (status === 'ok' ? 'var(--status-accent)' : status === 'warn' ? 'var(--status-warning)' : status === 'fail' ? 'var(--status-error)' : 'var(--text-tertiary)')

/**
 * The Settings window (design 1i): the old prototype's ten panes over the
 * CLI, the Mac app's `SettingsViewModel`, its names -- every pane's reads
 * when the window opens, each change run through the verb a person types and
 * read back, its words said. It holds `ThemeSettings` (Appearance, through
 * the theme service in `styles`) and `XcodeSettings` (Xcode), as the Mac's
 * does. **A key is never kept**: Save key writes it to `auth add <provider>
 * --stdin`'s input and drops it; no field holds it after (the Mac's secrets
 * law, and the old pane's "nothing you type here is stored or sent").
 * The keyboard list is the shell's key map, handed in, never a copy. Plain
 * TypeScript: no React.
 */
export class SettingsViewModel extends Observable {
  readonly name = 'Settings'
  pane: SettingsPane = 'general'
  readonly theme: ThemeSettings
  readonly xcode: XcodeSettings

  /** The window's doors, as closures the composition wires: the toast, the shell's sheet and confirm hosts, Models' Backends. */
  say: ((text: string) => void) | undefined = undefined
  present: ((kind: SettingsSheet) => void) | undefined = undefined
  dismissSheet: (() => void) | undefined = undefined
  ask: ((kind: SettingsConfirm) => void) | undefined = undefined
  openLibrary: (() => void) | undefined = undefined
  /** Told each time a pane is shown: how Models & roles' slot has its place read the roles (the app root wires it). */
  showing: ((pane: SettingsPane) => void) | undefined = undefined
  /**
   * Install… on the video runtime's row (design 1k, the old "Install
   * peft…"): the shared consent sheet asked, with `check`'s words for the row
   * and its remedy, the install; the composition hands it to the Video place,
   * which holds the install.
   */
  askInstall: ((said: string, remedy: string) => void) | undefined = undefined

  private binary: string | undefined = undefined
  private passedOver: PassedOver | undefined = undefined
  private binaryOverride: string | undefined
  private failure: string | undefined = undefined
  private working: string | undefined = undefined
  private rows: readonly ModelRow[] = []
  private roles: readonly RoleAssignment[] = []
  private keyed: readonly string[] = []
  private providers: readonly Provider[] = []
  private serverTools: Readonly<Record<string, string[] | { readonly unreadable: string }>> = {}
  private report: CheckReport | undefined = undefined
  private levels: readonly PermissionLevel[] = []
  private webSearch: string | undefined = undefined
  private recall: string | undefined = undefined
  private hosts: readonly string[] = []
  private recallListing: RecallListing | undefined = undefined
  private recallUnread: string | undefined = undefined
  private defaultVideo: string | undefined = undefined
  private voices: readonly string[] = []
  private registered: Said | undefined = undefined
  private registering: Provider | undefined = undefined
  private registerModel = ''
  private keyFor: string | undefined = undefined
  /** The key as it is typed, until Save or Cancel: the only place one is ever held, and only while its editor is open. */
  private keyDraft = ''
  private siteDraft = ''
  private searchDraft = ''
  private preferences: SettingsPreferences
  private fixing: JobProgress | undefined = undefined
  private fixJob: JobRunning | undefined = undefined
  private fixCommand = ''
  private forgetting = false

  constructor(
    private readonly runner: CommandRunning,
    private readonly starter: JobStarting,
    themeService: ThemeService,
    /** Every key the window binds, the shell's and the places': what Keyboard lists. */
    private readonly keys: () => readonly BoundKey[],
    preferences: SettingsPreferences,
  ) {
    super()
    this.preferences = preferences
    this.binaryOverride = preferences.binaryOverride
    this.theme = new ThemeSettings(themeService)
    this.xcode = new XcodeSettings(runner)
    this.theme.subscribe(() => this.changed())
    this.xcode.subscribe(() => this.changed())
    this.xcode.checkRows = () => this.report?.rows
    this.xcode.rereadCheck = () => this.readCheck()
    this.xcode.say = (text) => this.say?.(text)
    this.xcode.ask = (kind) => this.ask?.(kind)
  }

  /** The binary this launch found, and the override its discovery passed over, if it did. */
  connect(binary: string | undefined, passedOver?: PassedOver): void {
    this.binary = binary
    this.passedOver = passedOver
    this.xcode.use(binary)
    this.changed()
  }

  /** The window opened: every pane's reads, and the shown pane's own. */
  async appeared(): Promise<void> {
    this.showing?.(this.pane)
    await this.refresh()
    if (this.pane === 'xcode') await this.xcode.read()
  }

  /** A pane chosen in the sidebar; Xcode reads its own as it is chosen, as the Mac's does. */
  pick(pane: SettingsPane): void {
    this.pane = pane
    this.changed()
    this.showing?.(pane)
    if (pane === 'xcode' && this.binary !== undefined) void this.xcode.read()
  }

  /** Done: the window closed, the key being typed and the failure line with it, as the old `close` drops `keyEdit` and `fixFailure`. */
  closed(): void {
    this.keyFor = undefined
    this.keyDraft = ''
    this.failure = undefined
    this.changed()
  }

  // MARK: - Reading

  /** Every pane's reads, again: the Mac's `refresh`, and what the old panes draw beside it. */
  async refresh(): Promise<void> {
    const binary = this.binary
    if (binary === undefined) {
      this.failure = 'Apogee isn’t installed, so there are no settings to read.'
      this.changed()
      return
    }
    this.failure = undefined
    const list = await this.runner.read(binary, ['models', 'list'])
    if (list.case === 'document') this.rows = ModelRow.list(list.document)
    await this.readServerTools(binary)
    const auth = await this.runner.read(binary, ['auth', 'list'])
    if (auth.case === 'document') this.keyed = KeyedProviders.of(auth.document)
    const scan = await this.runner.read(binary, ['providers', 'scan'])
    if (scan.case === 'document') this.providers = Provider.list(scan.document)
    await this.readCheck()
    const permissions = await this.runner.command(binary, ['config', 'get', 'permissions'])
    if (permissions.case === 'done') this.levels = Permissions.levels(permissions.stdout)
    this.webSearch = Permissions.value(await this.runner.command(binary, ['config', 'get', 'tools.search.url']))
    this.recall = Permissions.value(await this.runner.command(binary, ['config', 'get', 'memory.recall']))
    const hosts = await this.runner.command(binary, ['config', 'get', AllowedHosts.key])
    if (hosts.case === 'done') this.hosts = AllowedHosts.list(hosts.stdout)
    await this.readRecall(binary)
    this.defaultVideo = Permissions.value(await this.runner.command(binary, ['config', 'get', 'models.default_video']))
    const status = await this.runner.read(binary, ['models', 'status'])
    if (status.case === 'document') this.roles = RoleAssignment.list(status.document)
    await this.readVoices(binary)
    this.changed()
  }

  /** `check`, its document whatever its exit; a refusal is the window's red line, in the CLI's words. */
  private async readCheck(): Promise<void> {
    const binary = this.binary
    if (binary === undefined) return
    const outcome = await this.runner.report(binary, ['check'])
    if (outcome.case === 'document') this.report = CheckReport.of(outcome.document)
    else this.failure ??= ReadOutcome.failure(outcome)
    this.changed()
  }

  /** Each vendor backend's consent: `config get backends.<name>.server_tools`, one tool a line; a refusal is said, never read as none. */
  private async readServerTools(binary: string): Promise<void> {
    const read: Record<string, string[] | { readonly unreadable: string }> = {}
    for (const row of this.backendRows().filter((candidate) => ServerTools.held(candidate.backendType))) {
      const outcome = await this.runner.command(binary, ['config', 'get', ServerTools.key(row.backend)])
      read[row.backend] = outcome.case === 'done' ? ServerTools.list(outcome.stdout) : { unreadable: CommandOutcome.failure(outcome) ?? 'it could not be read' }
    }
    this.serverTools = read
  }

  /** `chats recall`, its listing: what Forget would erase. */
  private async readRecall(binary: string): Promise<void> {
    const outcome = await this.runner.read(binary, ['chats', 'recall'])
    this.recallListing = outcome.case === 'document' ? RecallListing.of(outcome.document) : undefined
    this.recallUnread = outcome.case === 'document' ? undefined : ReadOutcome.failure(outcome)
  }

  /** The voices each listed voice model speaks, as its `models info` names them (`American English: af_heart, am_adam`). */
  private async readVoices(binary: string): Promise<void> {
    const voices: string[] = []
    for (const model of this.voiceModels()) {
      const outcome = await this.runner.read(binary, ['models', 'info', model])
      if (outcome.case !== 'document') continue
      const said = Field.list(outcome.document).find((field) => field.label === 'voices')?.value ?? ''
      for (const line of said.split('\n')) {
        for (const voice of (line.split(':')[1] ?? '').split(',').map((word) => word.trim())) if (voice !== '' && !voices.includes(voice)) voices.push(voice)
      }
    }
    this.voices = voices
  }

  /** The configured backends: neither stored weights nor a roster's models (the Mac's `backends`). */
  private backendRows(): ModelRow[] {
    return this.rows.filter((row) => !ModelRow.isStored(row) && row.format !== 'pipeline' && row.format !== 'voice' && row.source !== 'built in' && row.source !== 'roster')
  }

  /** The stored sets video generation runs: a pipeline, or an MLX video model (what `config set-default-video` takes). */
  private videoModels(): string[] {
    return this.rows.filter((row) => ModelRow.isStored(row) && (row.format === 'pipeline' || row.mlxKind === 'video')).map((row) => row.name)
  }

  /** The stored voice models: a voice set, or an MLX voice model. */
  private voiceModels(): string[] {
    return this.rows.filter((row) => ModelRow.isStored(row) && (row.format === 'voice' || row.mlxKind === 'voice')).map((row) => row.name)
  }

  // MARK: - Changing

  /**
   * One change, run as a person types it, every pane read back, its result
   * said: the line given, else the last line it printed; a failure the red
   * line, in the CLI's words. What it printed, when it succeeded.
   */
  private async change(args: readonly string[], done?: string, input?: string): Promise<string | undefined> {
    const binary = this.binary
    if (binary === undefined) return undefined
    this.working = args.slice(0, 3).join(' ')
    this.changed()
    const outcome = await this.runner.command(binary, args, input)
    this.working = undefined
    await this.refresh()
    const failure = CommandOutcome.failure(outcome)
    if (failure !== undefined || outcome.case !== 'done') {
      this.failure = failure
      this.say?.(`That didn't work: ${failure ?? 'apogee said nothing'}`)
      this.changed()
      return undefined
    }
    this.say?.(done ?? outcome.stdout.split('\n').filter((line) => line.trim() !== '').at(-1) ?? 'Done')
    return outcome.stdout
  }

  /** General's model for new sessions: `config set-default <backend>`. */
  async setDefaultModel(backend: string): Promise<void> {
    if (backend !== '') await this.change(['config', 'set-default', backend])
  }

  setThinking(thinking: SettingsPreferences['thinking']): void {
    this.prefer({ thinking })
  }

  setEffort(effort: string): void {
    this.prefer({ effort })
  }

  setNewSessions(newSessions: SettingsPreferences['newSessions']): void {
    this.prefer({ newSessions })
  }

  setOnLaunch(onLaunch: SettingsPreferences['onLaunch']): void {
    this.prefer({ onLaunch })
  }

  /** Use another apogee…: the Mac opens a file picker; a page cannot hand over a path, so it says so, as the old pane's Change… does. */
  useAnother(): void {
    this.say?.('Opens a file picker for another apogee (prototype)')
  }

  /** Use the installed one: the override cleared, from the next launch (the Mac's `setBinaryOverride(nil)`). */
  useInstalled(): void {
    this.binaryOverride = undefined
    this.changed()
    this.say?.('The app looks for apogee where it is installed, from its next launch')
  }

  /** Video & voice's pipeline: `config set-default-video <name>`, or `off` for none. */
  async setDefaultVideo(value: string): Promise<void> {
    await this.change(['config', 'set-default-video', value === '' ? 'off' : value])
  }

  setVoice(voice: string): void {
    this.prefer({ voice })
  }

  toggleCaptions(): void {
    this.prefer({ captions: !this.preferences.captions })
  }

  /** Save to's Change…: a page opens no folder picker, said as the old pane says it. */
  changeFolders(): void {
    this.say?.('Opens a folder picker (prototype)')
  }

  /** Add key… / Replace key…: the editor opens, empty. */
  editKey(provider: string): void {
    this.keyFor = provider
    this.keyDraft = ''
    this.changed()
  }

  typeKey(text: string): void {
    this.keyDraft = text
    this.changed()
  }

  cancelKey(): void {
    this.keyFor = undefined
    this.keyDraft = ''
    this.changed()
  }

  /**
   * Save key: the key handed to `auth add <provider> --stdin` on its
   * standard input -- never on a command line -- and dropped before anything
   * runs, so no field holds it afterwards.
   */
  async saveKey(): Promise<void> {
    const provider = this.keyFor
    const key = this.keyDraft.trim()
    this.keyFor = undefined
    this.keyDraft = ''
    this.changed()
    if (provider === undefined || key === '' || !KeyedProviders.providers.includes(provider)) return
    await this.change(['auth', 'add', provider, '--stdin'], undefined, `${key}\n`)
  }

  /** Remove: `auth clear <provider>`. */
  async removeKey(provider: string): Promise<void> {
    await this.change(['auth', 'clear', provider])
  }

  /** Register… on a scanned provider: the sheet, asked in the TUI's words. */
  askToRegister(id: string): void {
    const provider = this.providers.find((candidate) => candidate.provider === id)
    if (!provider || !Provider.registers(provider)) return
    this.registering = provider
    this.registerModel = ''
    this.changed()
    this.present?.('registerProvider')
  }

  typeRegisterModel(text: string): void {
    this.registerModel = text
    this.changed()
  }

  cancelRegister(): void {
    this.registering = undefined
    this.changed()
    this.dismissSheet?.()
  }

  /**
   * Register: what the sheet names -- `providers register <id>`, its
   * document read, its first line said and every line kept where it said
   * more; or, a model named for Ollama, `config add-backend`, its last line
   * said and its lines kept where it moved a role (the Mac's `register`).
   */
  async register(): Promise<void> {
    const provider = this.registering
    const binary = this.binary
    this.registering = undefined
    this.dismissSheet?.()
    if (!provider || binary === undefined) return
    const args = Provider.registration(provider, this.registerModel)
    const title = `Registered ${provider.provider}`
    this.registered = undefined
    if (args[0] !== 'providers') {
      const stdout = await this.change(args)
      const lines = (stdout ?? '').split('\n').filter((line) => line.trim() !== '')
      this.registered = lines.some((line) => line.includes(' set to ')) ? { title, lines } : undefined
      this.changed()
      return
    }
    this.working = args.slice(0, 3).join(' ')
    this.changed()
    const outcome = await this.runner.read(binary, args)
    this.working = undefined
    await this.refresh()
    const done = outcome.case === 'document' ? ProviderRegistration.of(outcome.document) : undefined
    if (!done) {
      const failure = ReadOutcome.failure(outcome) ?? 'apogee printed no registration'
      this.failure = failure
      this.say?.(`That didn't work: ${failure}`)
    } else {
      this.registered = done.lines.length > 1 ? { title, lines: done.lines } : undefined
      this.say?.(done.lines[0] ?? title)
    }
    this.changed()
  }

  dismissRegistered(): void {
    this.registered = undefined
    this.changed()
  }

  /**
   * `check --fix` or `bootstrap`, supervised -- its job kept for Stop -- and
   * Health read again when it ends; every end gives both buttons back: done
   * said finished, a stop stopped, a failure failed in the CLI's words, which
   * stay the red line until the next read (the Mac's `fix`, GUI 6c).
   */
  fix(args: readonly string[]): void {
    const binary = this.binary
    if (binary === undefined || this.fixing !== undefined) return
    const command = args.join(' ')
    const progress = new JobProgress()
    const job = this.starter.startJob(binary, args, false)
    this.fixing = progress
    this.fixJob = job
    this.fixCommand = command
    this.changed()
    void (async () => {
      for await (const update of job.updates) {
        progress.apply(update)
        this.changed()
      }
      this.fixJob = undefined
      await this.refresh()
      this.fixing = undefined
      const status = progress.status
      if (status.case === 'done') this.say?.(`${command} finished`)
      else if (status.case === 'failed') {
        this.failure = status.message
        this.say?.(`${command} failed: ${status.message}`)
      } else if (status.case === 'stopped') this.say?.(`${command} stopped`)
      else this.say?.(`${command} ended without saying how`)
      this.changed()
    })()
  }

  /** Install… on a runtime's row: the consent asked with the row in `check`'s words, its remedy the install (design 1k). */
  installRuntime(name: string): void {
    const row = this.report?.rows.find((candidate) => candidate.section === 'Video' && candidate.name === name)
    if (row?.remedy === undefined) return
    this.askInstall?.(`${row.section} · ${row.name}: ${row.detail}`, row.remedy)
  }

  /** Stop: the running fix ended as Ctrl-C does. */
  async stopFix(): Promise<void> {
    await this.fixJob?.stop(10_000)
  }

  /** A tool's level: `config set-permission <tool> <level>`. Off allow, an "always" entry for the tool goes, as the old pane drops it. */
  async setPermission(tool: string, level: string): Promise<void> {
    if (level !== 'allow') this.prefer({ always: this.preferences.always.filter((entry) => entry.tool !== tool) })
    await this.change(['config', 'set-permission', tool, level])
  }

  /** An "always" allowance removed: its tool asks again, as the old pane sets it -- `config set-permission <tool> ask`. */
  async removeAlways(index: number): Promise<void> {
    const tool = this.preferences.always[index]?.tool
    this.prefer({ always: this.preferences.always.filter((_, at) => at !== index) })
    if (tool !== undefined) await this.change(['config', 'set-permission', tool, 'ask'])
  }

  /** A session's allowance revoked. */
  revokeSession(index: number): void {
    this.prefer({ session: this.preferences.session.filter((_, at) => at !== index) })
  }

  typeSite(text: string): void {
    this.siteDraft = text
    this.changed()
  }

  /** Add: the host alone, as typed -- a scheme and a path dropped, as the old pane drops them -- `config add-allowed-host <host>`. */
  async addSite(): Promise<void> {
    const host = this.siteDraft.trim().replace(/^https?:\/\//, '').replace(/\/.*$/, '')
    if (host === '') return
    this.siteDraft = ''
    await this.change(['config', 'add-allowed-host', host])
  }

  /** Remove: `config delete-allowed-host <host>`. */
  async removeSite(host: string): Promise<void> {
    await this.change(['config', 'delete-allowed-host', host])
  }

  typeSearch(text: string): void {
    this.searchDraft = text
    this.changed()
  }

  /** Set: `config set tools.search.url <address>` as typed; never an empty address, which would clear it with no word (GUI 6c). */
  async setSearch(): Promise<void> {
    const value = this.searchDraft
    if (value.trim() === '') return
    await this.change(['config', 'set', 'tools.search.url', value])
  }

  /** Recall on or off: `config set memory.recall <true|false>`. */
  async toggleRecall(): Promise<void> {
    await this.change(['config', 'set', 'memory.recall', this.recallOn ? 'false' : 'true'])
  }

  /** Forget all summaries…: what recall holds read first, so it is seen before it goes; asked where it holds any. */
  async askToForget(): Promise<void> {
    const binary = this.binary
    if (binary === undefined) return
    await this.readRecall(binary)
    this.forgetting = this.recallListing?.holdsAny ?? this.recallUnread !== undefined
    this.changed()
    if (this.forgetting) this.ask?.('settingsForget')
  }

  /** What a confirm says: the old prototype's words (`forget`, `xcodeRemove` at `b375fac9`). */
  confirmWords(kind: SettingsConfirm): ConfirmWords | undefined {
    if (kind === 'xcodeRemove') return this.xcode.confirmWords
    if (!this.forgetting) return undefined
    const now = this.recallUnread !== undefined ? `Recall could not be read: ${this.recallUnread}. ` : this.recallListing ? `Now: ${this.recallListing.countLine}. ` : ''
    return { title: 'Forget what recall holds?', body: `${now}apogee chats recall --forget --yes -- every chat's summary goes, so no later session recalls them; every transcript stays.`, confirm: 'Forget every summary' }
  }

  /** A confirm answered yes. */
  async confirm(kind: SettingsConfirm): Promise<void> {
    if (kind === 'xcodeRemove') {
      await this.xcode.remove()
      return
    }
    if (!this.forgetting) return
    this.forgetting = false
    await this.change(['chats', 'recall', '--forget', '--yes'])
  }

  /** A confirm's Cancel: nothing asked stays pending. */
  dismissPending(kind: SettingsConfirm): void {
    if (kind === 'settingsForget') this.forgetting = false
    this.changed()
  }

  /** Models & roles' Open models library: Models, on Backends. */
  showLibrary(): void {
    this.openLibrary?.()
  }

  private prefer(patch: Partial<SettingsPreferences>): void {
    this.preferences = { ...this.preferences, ...patch }
    this.changed()
  }

  /** Recall's flag, as `config get memory.recall` reads it: on unless it says false (the CLI's default is on). */
  private get recallOn(): boolean {
    return this.recall !== 'false'
  }

  // MARK: - The face

  face(): SettingsFace {
    const report = this.report
    const install = report?.install
    const foot = install?.version !== undefined ? [`apogee ${install.version}${install.channel ? ` · ${install.channel}` : ''}`, ...(install.root ? [`data in ${install.root}`] : [])] : []
    const runtimes = (report?.rows ?? []).filter((row) => row.section === 'Video' && runtimeRows.includes(row.name))
    const runtimeName = (row: CheckRow) => `${row.section} · ${row.name}`
    // A row's detail is its state, then after ` -- ` why: the state is the status word drawn, the rest the line under it.
    const at = (row: CheckRow) => row.detail.indexOf(' -- ')
    const runtimeStatus = (row: CheckRow) => (at(row) < 0 ? row.detail : row.detail.slice(0, at(row)))
    const runtimeDetail = (row: CheckRow) => (at(row) < 0 ? '' : row.detail.slice(at(row) + 4))
    const fixing = this.fixing
    const busy = fixing ? (fixing.busy ?? fixing.lastSaid ?? 'working') : ''
    const registering = this.registering
    const registerArgs = registering ? Provider.registration(registering, this.registerModel) : []
    return {
      pane: this.pane,
      panes: settingsPanes.map(([id, label]) => ({ id, label, current: id === this.pane })),
      title: settingsPanes.find(([id]) => id === this.pane)?.[1] ?? '',
      failure: this.failure ?? '',
      foot,
      working: this.working ?? '',
      general: this.generalFace(),
      appearance: this.theme.face(),
      media: {
        video: this.videoFace(),
        voiceModel: this.voiceModels().length ? this.voiceModels().join(', ') : 'none in the library',
        voice: this.preferences.voice,
        voices: (this.voices.includes(this.preferences.voice) || this.voices.length === 0 ? this.voices : [this.preferences.voice, ...this.voices]).map((voice) => ({ value: voice, label: voice === defaultVoice ? `${voice} · default` : voice })),
        captions: this.preferences.captions,
        captionsNote: report?.rows.find((row) => row.section === 'Video' && row.name === 'ffmpeg captions')?.detail ?? '',
        saveTo: [`video · ${this.preferences.saveTo.video}`, `speech · ${this.preferences.saveTo.speech}`],
        runtimes: runtimes.map((row) => ({ name: runtimeName(row), status: runtimeStatus(row), color: statusColor(row.status) })),
      },
      backends: {
        sections: [
          { title: 'ON THIS MAC', rows: this.backendRows().filter((row) => row.section === 'local').map((row) => this.backendFace(row, 'local')) },
          {
            title: 'VIDEO AND VOICE · IN THE SAME PYTHON ENVIRONMENT',
            rows: runtimes.map((row) => ({ kind: 'local' as const, name: runtimeName(row), status: runtimeStatus(row), statusColor: statusColor(row.status), detail: runtimeDetail(row), consent: row.remedy ? [row.remedy] : [], consentColor: 'var(--text-tertiary)', actions: installsVideo(row) ? [{ label: 'Install…', kind: 'btn' as const, act: { case: 'installRuntime' as const, row: row.name } }] : [] })),
          },
        ].filter((section) => section.rows.length > 0),
        keys: KeyedProviders.providers.map((provider) => this.keyFace(provider)),
        subscriptions: [{ title: 'SUBSCRIPTIONS · THROUGH THE VENDOR’S APP', rows: this.backendRows().filter((row) => subscriptionTypes.includes(row.backendType)).map((row) => this.backendFace(row, 'subscription')) }].filter((section) => section.rows.length > 0),
        keyDraft: this.keyDraft,
        keyEmpty: this.keyDraft.trim() === '',
        providers: this.providers.map((provider) => ({ id: provider.provider, label: provider.label, state: `${provider.tier}${provider.backend ? ` · ${provider.backend}` : ''}`, canRegister: Provider.registers(provider) })),
        registered: { shown: this.registered !== undefined, title: this.registered?.title ?? '', text: this.registered?.lines.join('\n') ?? '' },
        health: {
          summary: report?.summary ?? '',
          summaryColor: (report?.failures ?? 0) > 0 ? 'var(--status-error)' : 'var(--text-primary)',
          rows: (report?.health ?? []).map((row) => ({
            text: `${row.status === 'skipped' ? 'Not set up · ' : ''}${row.section} · ${row.name}: ${row.detail}`,
            color: row.status === 'fail' ? 'var(--status-error)' : row.status === 'warn' ? 'var(--status-warning)' : 'var(--text-secondary)',
            remedy: row.remedy ?? '',
          })),
          fixing: fixing !== undefined,
          busy,
          command: fixing ? this.fixCommand : '',
          canStop: this.fixJob !== undefined,
        },
      },
      register: {
        shown: registering !== undefined,
        title: registering ? `Found ${registering.provider} -- register it as a backend?` : '',
        subtitle: registering ? `${registering.label} · ${registering.tier}` : '',
        needsModel: registering ? Provider.needsModel(registering) : false,
        model: this.registerModel,
        command: registering ? `apogee ${registerArgs.join(' ')}` : '',
        canRegister: registering !== undefined,
      },
      permissions: {
        tools: [...toolWords, ...this.levels.filter((entry) => !toolWords.some(([tool]) => tool === entry.tool)).map((entry) => [entry.tool, ''] as const)].map(([name, desc]) => ({
          name,
          desc,
          level: Permissions.level(this.levels, name),
          levels: Permissions.choices.map((level) => ({ value: level, label: level.charAt(0).toUpperCase() + level.slice(1) })),
        })),
        always: this.preferences.always,
        session: this.preferences.session,
        noAlways: this.preferences.always.length === 0,
        noSession: this.preferences.session.length === 0,
      },
      web: {
        sites: this.hosts,
        siteDraft: this.siteDraft,
        siteEmpty: this.siteDraft.trim() === '',
        search: this.webSearch ?? 'not set',
        searchShownColor: this.webSearch ? 'var(--text-primary)' : 'var(--text-tertiary)',
        searchDraft: this.searchDraft,
        searchEmpty: this.searchDraft.trim() === '',
        searchNote: this.webSearch ? 'On. Searches go to this address and nowhere else.' : 'Off until an address is set. web_search never pretends to succeed.',
        searchColor: this.webSearch ? 'var(--status-accent)' : 'var(--text-tertiary)',
      },
      memory: {
        recall: this.recallOn,
        recallLabel: this.recallOn ? 'On' : 'Off',
        holds: this.recallListing
          ? `${this.recallListing.summaries.length} sessions summarized · in ${this.recallListing.index}`
          : this.recallUnread !== undefined
            ? `Recall could not be read: ${this.recallUnread}`
            : '',
        canForget: this.recallListing?.holdsAny ?? false,
      },
      keyboard: keyboardLines(KeyMap.reference, this.keys()),
      xcode: this.xcode.face(),
    }
  }

  private generalFace(): SettingsFace['general'] {
    // The chat role's own choice, as models status names it: its backend where set, else models.default, named on a role that falls back to it (1h's rule).
    const chat = this.roles.find((role) => role.role === 'chat')
    const fallback = this.roles.find((role) => role.said.includes('(via models.default)'))
    const own = chat && RoleAssignment.isSet(chat) ? RoleAssignment.backend(chat) : fallback ? RoleAssignment.backend(fallback) : undefined
    const chats = ModelRow.chatBackends(this.backendRows())
    const value = own ?? ''
    const options = [...(value === '' ? [{ value: '', label: 'Not set' }] : []), ...(value === '' || chats.includes(value) ? chats : [value, ...chats]).map((name) => ({ value: name, label: name }))]
    const install = this.report?.install
    const pending = this.binaryOverride !== undefined && this.binaryOverride !== this.binary && this.binaryOverride !== this.passedOver?.path ? this.binaryOverride : undefined
    const passedOver = this.passedOver ? `Not using ${this.passedOver.path}: ${this.passedOver.reason.replace(/\.$/, '')}. This launch runs the one above.` : ''
    const prefs = this.preferences
    return {
      model: { value, options },
      thinking: (['off', 'on', 'auto'] as const).map((choice) => ({ value: choice, label: choice.charAt(0).toUpperCase() + choice.slice(1), current: prefs.thinking === choice })),
      effort: { value: prefs.effort, options: [{ value: 'default', label: 'the model’s default' }, ...['low', 'medium', 'high', 'xhigh', 'max'].map((level) => ({ value: level, label: level }))] },
      newSessions: [
        { value: 'saved' as const, label: 'Saved', current: prefs.newSessions === 'saved' },
        { value: 'private' as const, label: 'Private', current: prefs.newSessions === 'private' },
      ],
      onLaunch: [
        { value: 'new' as const, label: 'Start a new session', current: prefs.onLaunch === 'new' },
        { value: 'last' as const, label: 'Reopen the last session', current: prefs.onLaunch === 'last' },
      ],
      cli: {
        lines: this.binary === undefined ? ['not found'] : [[this.binary, install?.version, install?.channel === undefined ? undefined : `${install.channel} channel`].filter((part) => part !== undefined).join(' · '), ...(install?.root ? [`data in ${install.root}`] : [])],
        passedOver,
        useInstalled: this.binaryOverride !== undefined,
        next: pending === undefined ? '' : `From the next launch: ${pending}`,
      },
    }
  }

  private videoFace(): SettingsFace['media']['video'] {
    const listed = this.videoModels()
    const value = this.defaultVideo ?? ''
    const offered = value === '' || listed.includes(value) ? listed : [value, ...listed]
    return { value, options: [...offered.map((name) => ({ value: name, label: name })), { value: '', label: 'None: each clip names its pipeline' }] }
  }

  private backendFace(row: ModelRow, kind: 'local' | 'subscription'): BackendRowFace {
    const ok = row.state === 'ok' || row.state === 'installed'
    return {
      kind,
      name: row.backend,
      status: row.state,
      statusColor: ok ? (kind === 'subscription' ? 'var(--model-subscription)' : 'var(--status-accent)') : 'var(--text-tertiary)',
      detail: [row.backendType, row.model].filter((part) => part !== '').join(' · '),
      consent: row.note ? [row.note] : [],
      consentColor: 'var(--text-tertiary)',
      actions: [],
    }
  }

  private keyFace(provider: string): KeyRowFace {
    const set = this.keyed.includes(provider)
    const words = keyWords[provider] ?? { name: provider, placeholder: '' }
    const scanned = this.providers.find((candidate) => candidate.provider === provider)
    const backends = this.backendRows().filter((row) => row.backendType === provider)
    const consent = backends.flatMap((row) => {
      const tools = this.serverTools[row.backend]
      if (tools === undefined) return []
      if (!Array.isArray(tools)) return [`Vendor-run tools: this apogee can't read them -- ${tools.unreadable}`]
      return [tools.length === 0 ? 'Vendor-run tools: none consented' : `Vendor-run tools consented: ${tools.join(', ')} -- the vendor runs them under your key`]
    })
    const consented = backends.some((row) => {
      const tools = this.serverTools[row.backend]
      return Array.isArray(tools) && tools.length > 0
    })
    return {
      kind: 'api',
      provider,
      name: scanned?.label ?? words.name,
      status: set ? 'key set' : 'not set',
      statusColor: set ? 'var(--model-api)' : 'var(--text-tertiary)',
      detail: backends.map((row) => [row.backend, row.model].filter((part) => part !== '').join(' · ')).join(', '),
      consent,
      consentColor: consented ? 'var(--model-api)' : 'var(--text-tertiary)',
      actions: set
        ? [
            { label: 'Replace key…', kind: 'btn', act: { case: 'editKey', provider } },
            { label: 'Remove', kind: 'btn-g', act: { case: 'removeKey', provider } },
          ]
        : [{ label: 'Add key…', kind: 'btn', act: { case: 'editKey', provider } }],
      editing: this.keyFor === provider,
      placeholder: words.placeholder,
    }
  }
}
