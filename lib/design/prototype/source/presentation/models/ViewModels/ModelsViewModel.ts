import type { JobStarting } from '../../../business/jobs/SupervisedJob'
import { AdapterCheck, BackendAdapter } from '../../../business/reads/Adapters'
import { CommandOutcome } from '../../../business/reads/CommandRunning'
import type { CommandRunning } from '../../../business/reads/CommandRunning'
import { ModelInfo, ModelKind, ModelRow, RoleAssignment } from '../../../business/reads/Models'
import type { ModelInfoDocument } from '../../../business/reads/Models'
import { ReadOutcome } from '../../../business/reads/ReadRunner'
import type { Clock } from '../../../infrastructure/clock/Clock'
import { Observable } from '../../styles/Observation/Observable'
import { AdaptersForm } from './BackendAdapters'
import type { AdaptersView } from './BackendAdapters'
import { DeleteNarration, DeletePlan } from './DeletePlan'
import { DownloadForm, DownloadsViewModel } from './DownloadsViewModel'
import { StoreViewModel } from './StoreViewModel'
import type { ConfirmWords, StoreFace } from './StoreViewModel'
import { storeSize } from './StoreForms'
import type { CardFace } from './DownloadsViewModel'

/** The place's tabs: the prototype's `modelsTab` values, the Mac's `ModelsViewModel.Tab`. */
export type ModelsTab = 'backends' | 'store' | 'jobs' | 'roles'

/** What a backend's details offer. */
export type BackendAction = 'useInSession' | 'assignRole' | 'showWeights' | 'removeBackend' | 'deleteWithWeights' | 'manageKey' | 'addKey'

/** A confirm the place asks, by the prototype's kind. */
export type ModelsConfirm = 'model' | 'storeDelete' | 'storeModelDelete' | 'unregister' | 'registerAll'

/** A backend's drafter, as its `models info` states it (the CLI's 53i): the Mac's `BackendDraft`. */
export interface BackendDraft {
  /** The drafter's directory. */
  readonly path: string
  /** The CLI's words after it: drafting, or `cannot draft beside it: …`. */
  readonly said: string
  /** The CLI's line for the drafter itself. */
  readonly drafter: string | undefined
}

export const BackendDraft = {
  cannot: 'cannot draft beside it: ',
  of(info: ModelInfoDocument): BackendDraft | undefined {
    const line = ModelInfo.field(info, 'draft')
    if (line === undefined || line === '') return undefined
    const at = line.indexOf(' -- ')
    return { path: at >= 0 ? line.slice(0, at) : line, said: at >= 0 ? line.slice(at + 4) : '', drafter: ModelInfo.field(info, 'drafter') }
  },
  drafts(draft: BackendDraft): boolean {
    return !draft.said.startsWith(BackendDraft.cannot)
  },
}

/** The pair sheet on a base: the drafters the binary's own completion offers it (the Mac's `DrafterPairing`). */
export interface DrafterPairing {
  readonly backend: string
  readonly offers: ReadonlyArray<{ readonly handle: string; readonly path: string | undefined; readonly said: string | undefined }>
  chosen: string | undefined
}

const roleWords: ReadonlyArray<readonly [string, string, string]> = [
  ['chat', 'Chat', 'Answers your messages; the model for new sessions.'],
  ['embedding', 'Embedding', 'Searches attachments and past sessions by meaning.'],
  ['extraction', 'Extraction', 'Pulls structured facts out of documents for the knowledge graph.'],
  ['vision', 'Vision', 'Describes images for chat models that can’t see.'],
  ['transcription', 'Transcription', 'Turns audio and video into text.'],
  ['utility', 'Utility', 'Small chores: session titles, search rewrites, summaries, the auto-thinking call.'],
]

const groupTitles: Readonly<Record<string, string>> = { safetensors: 'SafeTensors', gguf: 'GGUF', mlx: 'MLX', pipeline: 'Pipelines', voice: 'Voices', 'ollama-cli': 'Ollama', anthropic: 'Anthropic', openai: 'OpenAI', google: 'Google', 'claude-cli': 'Claude Code', 'codex-cli': 'Codex', 'gemini-cli': 'Gemini CLI', other: 'Other' }
const formatNames: Readonly<Record<string, string>> = { safetensors: 'SafeTensors', gguf: 'GGUF', mlx: 'MLX', pipeline: 'Pipeline', voice: 'Voice' }

/**
 * The Models place (design 1h): the library of every configured and stored
 * model, one model's details, the roles, and the place's other two tabs --
 * Stored weights (`store`) and Jobs (`downloads`), whose view-models it holds
 * and wires as the Mac's composition wires them. The Mac app's
 * `ModelsViewModel`, drawn as the prototype's Models screen (`main.html` at
 * `b375fac9`, section `models screen`).
 *
 * Every change is the command a person would type, asked first where it
 * destroys, and read back after (`models list`, `models status`); nothing is
 * mirrored. Unknowns are said, never guessed. Plain TypeScript: it never
 * imports React. Links to other places are closures the composition fills
 * in: the toast, the shell's sheets and confirms, the open session, Settings.
 */
export class ModelsViewModel extends Observable {
  readonly name = 'Models'
  tab: ModelsTab = 'backends'
  /** How many times Models came into view. */
  appearances = 0
  readonly store: StoreViewModel
  readonly downloads: DownloadsViewModel

  rows: ModelRow[] = []
  listing: 'notRead' | 'reading' | 'read' | 'failed' | 'missing' = 'notRead'
  selected: string | undefined = undefined
  info: ModelInfoDocument | undefined = undefined
  infoFailure: string | undefined = undefined
  roles: RoleAssignment[] = []
  rolesSuite: string | undefined = undefined
  rolesFailure: string | undefined = undefined
  readonly drafts = new Map<string, BackendDraft>()
  adapters: AdaptersView | undefined = undefined
  editingAdapters: AdaptersForm | undefined = undefined
  pairingDrafter: DrafterPairing | undefined = undefined
  pendingDelete: { readonly row: ModelRow; readonly plan: DeletePlan } | undefined = undefined
  removing: string | undefined = undefined
  working: string | undefined = undefined
  failure: string | undefined = undefined
  narration: DeleteNarration | undefined = undefined
  downloadForm: DownloadForm | undefined = undefined

  /** Says one line in the window's toast. */
  say: ((text: string) => void) | undefined = undefined
  /** Opens a sheet in the shell's host, by kind. */
  present: ((kind: string) => void) | undefined = undefined
  dismissSheet: (() => void) | undefined = undefined
  /** Asks a confirm in the shell's host, by kind. */
  ask: ((kind: ModelsConfirm) => void) | undefined = undefined
  /** The open session on a backend: the composition's way to Sessions. */
  useInSession: ((backend: string) => void) | undefined = undefined
  /** Settings, where a key is added (1i's Backends & keys): with a provider, its key editor open, as the old Add an API key… opens it. */
  openSettings: ((provider?: string) => void) | undefined = undefined
  /** Another place: Video, Voice. */
  goTo: ((place: 'video' | 'voice') => void) | undefined = undefined

  private binary: string | undefined = undefined

  constructor(
    private readonly runner: CommandRunning,
    jobs: JobStarting,
    clock: Pick<Clock, 'ticks' | 'now'>,
  ) {
    super()
    this.store = new StoreViewModel(runner)
    this.downloads = new DownloadsViewModel(jobs, runner, clock)
    // The place's three, wired as the Mac's composition wires them.
    this.store.startConvert = (form) => this.startStoreJob(() => this.downloads.convert(form))
    this.store.startQuantize = (form) => this.startStoreJob(() => this.downloads.quantize(form))
    this.store.startCheck = (handle) => this.startStoreJob(() => this.downloads.check(handle))
    this.store.busy = (handle) => this.downloads.busyOn(handle)
    this.store.say = (text) => this.say?.(text)
    this.store.present = (kind) => this.present?.(kind)
    this.store.dismissSheet = () => this.dismissSheet?.()
    this.store.ask = (kind) => this.ask?.(kind as ModelsConfirm)
    this.store.useInSession = (backend) => this.useInSession?.(backend)
    this.store.goTo = (place) => this.goTo?.(place)
    this.downloads.landed = (sets) => this.store.land(sets)
    this.downloads.say = (text) => this.say?.(text)
    this.downloads.showWeights = (handle) => {
      this.tab = 'store'
      this.changed()
      if (handle !== undefined) void this.store.select(handle)
    }
    this.store.subscribe(() => this.changed())
    this.downloads.subscribe(() => this.changed())
  }

  /** A store job started, and Jobs shown -- only when one did: with no binary nothing starts, and the tab stays. */
  private startStoreJob(start: () => unknown): void {
    if (start() === undefined) return
    this.tab = 'jobs'
    this.changed()
  }

  // MARK: - Reading

  /** The binary was found, or not: the place can read. */
  connect(binary: string | undefined): void {
    this.binary = binary
    this.store.connect(binary)
    this.downloads.connect(binary)
    if (binary === undefined) this.listing = 'missing'
  }

  /** Models came into view: read again. */
  async appeared(): Promise<void> {
    this.appearances += 1
    if (this.binary === undefined) return
    await this.refresh()
  }

  /** `models list`, each mlx entry's drafter, `models status`, the store, and the selected model's details. */
  async refresh(): Promise<void> {
    const binary = this.binary
    if (binary === undefined) return
    if (this.listing !== 'read') this.listing = 'reading'
    const outcome = await this.runner.read(binary, ['models', 'list'])
    const document = ReadOutcome.documentOf(outcome)
    if (document) {
      this.rows = ModelRow.list(document)
      this.listing = 'read'
    } else this.listing = 'failed'
    await this.readDrafts()
    await this.readRoles()
    await this.store.refresh()
    if (this.selected !== undefined) {
      if (this.rows.some((row) => row.name === this.selected)) {
        await this.readInfo(this.selected)
        await this.readAdapters()
      } else this.close()
    }
    this.changed()
  }

  /** Each `mlx` entry's drafter: its row's where it states one, else its `models info`'s `draft:` line. */
  private async readDrafts(): Promise<void> {
    const binary = this.binary
    if (binary === undefined) return
    this.drafts.clear()
    for (const row of this.rows) {
      if (ModelRow.isStored(row) || row.backendType !== 'mlx' || row.mlxKind === undefined) continue
      if (row.draft) {
        this.drafts.set(row.backend, { path: row.draft.path, said: row.draft.canDraft ? '' : `${BackendDraft.cannot}${row.draft.refusal ?? ''}`, drafter: undefined })
        continue
      }
      const document = ReadOutcome.documentOf(await this.runner.read(binary, ['models', 'info', row.backend]))
      const draft = document ? BackendDraft.of(ModelInfo.read(document)) : undefined
      if (draft) this.drafts.set(row.backend, draft)
    }
  }

  private async readRoles(): Promise<void> {
    const binary = this.binary
    if (binary === undefined) return
    const outcome = await this.runner.read(binary, ['models', 'status'])
    const document = ReadOutcome.documentOf(outcome)
    if (document) {
      this.roles = RoleAssignment.list(document)
      this.rolesSuite = RoleAssignment.suite(document)
      this.rolesFailure = undefined
    } else this.rolesFailure = ReadOutcome.failure(outcome)
  }

  private async readInfo(name: string): Promise<void> {
    const binary = this.binary
    if (binary === undefined) return
    const outcome = await this.runner.read(binary, ['models', 'info', name])
    if (this.selected !== name) return
    const document = ReadOutcome.documentOf(outcome)
    if (document) {
      this.info = ModelInfo.read(document)
      this.infoFailure = undefined
    } else {
      this.info = undefined
      this.infoFailure = ReadOutcome.failure(outcome)
    }
  }

  /** Whether a backend takes adapters: a llama.cpp or mlx entry (the CLI's `takes_adapters`). */
  static takesAdapters(row: ModelRow): boolean {
    return !ModelRow.isStored(row) && (row.backendType === 'llamacpp' || row.backendType === 'mlx')
  }

  /** The selected backend's adapters: `config get backends.<name>.adapters`, and where it lists any, `check`'s rows for their pins. */
  private async readAdapters(): Promise<void> {
    const binary = this.binary
    const row = this.selectedRow
    if (binary === undefined || !row || !ModelsViewModel.takesAdapters(row)) {
      this.adapters = undefined
      return
    }
    const listed = await this.runner.command(binary, ['config', 'get', `backends.${row.backend}.adapters`])
    if (listed.case !== 'done') {
      this.adapters = undefined
      return
    }
    const list = BackendAdapter.list(listed.stdout)
    if (list.length === 0) {
      this.adapters = { backend: row.backend, rows: [], checkFailure: undefined }
      return
    }
    const report = await this.runner.report(binary, ['check'])
    const document = ReadOutcome.documentOf(report)
    const checks = document ? AdapterCheck.rows(list, row.backend, document.value) : {}
    this.adapters = { backend: row.backend, rows: list.map((adapter) => ({ adapter, check: checks[adapter.path] })), checkFailure: document ? undefined : `The doctor could not be read: ${ReadOutcome.failure(report) ?? 'apogee printed nothing'}` }
  }

  // MARK: - Tabs and selection

  pick(tab: ModelsTab): void {
    this.tab = tab
    this.changed()
  }

  /** Stored weights, as the monitor's "Open models library" opens it. */
  showStore(): void {
    this.pick('store')
  }

  /** A backend shown on Backends, as the inspector's Open in library opens it; a model the list does not name opens it with nothing selected, as the old `selModel: modelId` does. */
  async openBackend(name: string | undefined): Promise<void> {
    this.pick('backends')
    if (this.listing !== 'read') await this.refresh()
    if (name !== undefined && this.rows.some((row) => row.name === name)) await this.select(name)
    else this.close()
  }

  get selectedRow(): ModelRow | undefined {
    return this.rows.find((row) => row.name === this.selected)
  }

  /** A row's click: its details, or closes them when they are the ones shown. */
  async toggle(name: string): Promise<void> {
    if (this.selected === name) this.close()
    else await this.select(name)
  }

  async select(name: string): Promise<void> {
    this.selected = name
    this.info = undefined
    this.infoFailure = undefined
    this.adapters = undefined
    this.changed()
    await this.readInfo(name)
    await this.readAdapters()
    this.changed()
  }

  close(): void {
    this.selected = undefined
    this.info = undefined
    this.adapters = undefined
    this.changed()
  }

  // MARK: - A backend's actions

  /** What the details offer for `row`, the prototype's by its kind. */
  actions(row: ModelRow): BackendAction[] {
    const kind = ModelKind.of(row.backendType)
    const handle = this.store.registered.get(row.backend)
    if (kind === 'api') return row.state === 'no key' ? ['addKey'] : ['useInSession', 'manageKey']
    if (kind === 'subscription') return ['useInSession']
    const out: BackendAction[] = []
    if (kind === 'local' && ModelRow.chats(row)) out.push('useInSession')
    out.push('assignRole')
    if (handle !== undefined) out.push('showWeights')
    out.push('removeBackend')
    if (handle !== undefined) out.push('deleteWithWeights')
    return out
  }

  async act(action: BackendAction): Promise<void> {
    const row = this.selectedRow
    if (!row) return
    switch (action) {
      case 'useInSession':
        this.useInSession?.(row.backend)
        return
      case 'assignRole':
        this.pick('roles')
        return
      case 'showWeights': {
        const handle = this.store.registered.get(row.backend)
        this.pick('store')
        if (handle !== undefined) await this.store.select(handle)
        return
      }
      case 'removeBackend':
        this.removing = row.backend
        this.changed()
        this.ask?.('unregister')
        return
      case 'deleteWithWeights':
        await this.askToDelete(row)
        return
      case 'manageKey':
        this.openSettings?.()
        return
      // An API backend's type is the provider `auth add` takes: anthropic, openai or google.
      case 'addKey':
        this.openSettings?.(row.backendType)
        return
    }
  }

  /** Asks before `models delete <name>`: the CLI's own plan read first. A plan the CLI refuses is its failure, and nothing is asked. */
  private async askToDelete(row: ModelRow): Promise<void> {
    const binary = this.binary
    if (binary === undefined || this.working !== undefined) return
    this.working = DeletePlan.reading
    const outcome = await this.runner.command(binary, ['models', 'delete', row.name])
    this.working = undefined
    if (outcome.case !== 'done') {
      this.failure = CommandOutcome.failure(outcome)
      this.changed()
      return
    }
    this.pendingDelete = { row, plan: DeletePlan.of(outcome.stdout) }
    this.changed()
    this.ask?.('model')
  }

  /** What a confirm of the place asks: the Backends tab's own, else Stored weights'. */
  confirmWords(kind: ModelsConfirm): ConfirmWords | undefined {
    if (kind === 'model' && this.pendingDelete) {
      const { row, plan } = this.pendingDelete
      return { kind: 'storeDelete', title: `Delete ${row.backend}?`, body: '', detail: DeletePlan.question(plan, `apogee models delete ${row.name} --yes`), confirm: 'Delete its files', alternative: plan.backends.length > 0 ? 'Delete its files and the backends that use them' : '', destructive: true }
    }
    if (kind === 'unregister' && this.removing !== undefined) {
      const usedBy = this.roles.filter((role) => role.role !== 'chat' && RoleAssignment.isSet(role) && RoleAssignment.backend(role) === this.removing).map((role) => role.role)
      return {
        kind: 'unregister',
        title: `Remove the backend ${this.removing}?`,
        body: `Its config entry goes. The weights stay under Stored weights, ready to register again.${usedBy.length > 0 ? ` The ${usedBy.join(' and ')} ${usedBy.length === 1 ? 'role is' : 'roles are'} cleared.` : ''}`,
        detail: `apogee config delete-backend ${this.removing}`,
        confirm: 'Remove',
        alternative: '',
        destructive: true,
      }
    }
    const words = this.store.confirmWords()
    return words && words.kind === kind ? words : undefined
  }

  /** A confirm answered: yes, or its second door. */
  async confirm(kind: ModelsConfirm, alternative: boolean): Promise<void> {
    if (kind === 'model' && this.pendingDelete) {
      const { row, plan } = this.pendingDelete
      this.pendingDelete = undefined
      const withBackends = alternative && plan.backends.length > 0
      await this.run(['models', 'delete', row.name, '--yes', ...(withBackends ? ['--and-backends'] : [])], `Deleted ${row.backend}`, 'deleted')
      if (this.selected === row.name) this.close()
      return
    }
    if (kind === 'unregister' && this.removing !== undefined) {
      const name = this.removing
      this.removing = undefined
      await this.run(['config', 'delete-backend', name], `Removed the backend ${name}; its weights stay`, 'roles')
      if (this.selected === name) this.close()
      return
    }
    await this.store.confirm(alternative)
    await this.refresh()
  }

  dismissPending(kind: ModelsConfirm): void {
    if (kind === 'model') this.pendingDelete = undefined
    else if (kind === 'unregister' && this.removing !== undefined) this.removing = undefined
    else this.store.dismissPending()
    this.changed()
  }

  dismissNarration(): void {
    this.narration = undefined
    this.changed()
  }

  // MARK: - The drafter and the adapters

  /** Opens the pair sheet on the selected base: the drafters the binary's own completion offers it, each with its `models info`. */
  async askToPairDrafter(): Promise<void> {
    const binary = this.binary
    const row = this.selectedRow
    if (binary === undefined || !row || !this.offersDrafterPairing(row)) return
    const completed = await this.runner.command(binary, ['__complete', 'config', 'set-backend', row.backend, '--draft', ''])
    if (completed.case !== 'done') {
      this.failure = CommandOutcome.failure(completed)
      this.changed()
      return
    }
    const offers: Array<{ handle: string; path: string | undefined; said: string | undefined }> = []
    for (const handle of completed.stdout.split('\n').map((line) => line.trim()).filter((line) => line !== '')) {
      const document = ReadOutcome.documentOf(await this.runner.read(binary, ['models', 'info', handle]))
      const info = document ? ModelInfo.read(document) : undefined
      offers.push({ handle, path: info ? ModelInfo.field(info, 'model_path') : undefined, said: info ? ModelInfo.field(info, 'drafter') : undefined })
    }
    this.pairingDrafter = { backend: row.backend, offers, chosen: offers[0]?.handle }
    this.changed()
    this.present?.('pairDrafter')
  }

  choosePairing(handle: string): void {
    if (this.pairingDrafter) this.pairingDrafter.chosen = handle
    this.changed()
  }

  /** Pair: `config set-backend <base> --draft <the drafter's directory>`, read back. */
  async pairDrafter(): Promise<void> {
    const form = this.pairingDrafter
    const offer = form?.offers.find((candidate) => candidate.handle === form.chosen)
    if (!form || !offer) return
    this.closePairing()
    await this.run(['config', 'set-backend', form.backend, '--draft', offer.path ?? offer.handle], `${form.backend} drafts with ${offer.handle}`)
  }

  closePairing(): void {
    this.pairingDrafter = undefined
    this.changed()
    this.dismissSheet?.()
  }

  /** Unpair: `config set-backend <backend> --draft ""`, read back. */
  async unpair(): Promise<void> {
    const row = this.selectedRow
    if (row) await this.run(['config', 'set-backend', row.backend, '--draft', ''], `${row.backend} drafts with nothing now`)
  }

  /** Whether Pair a drafter… is offered: an `mlx` entry over a `vision` model with no drafter paired. */
  offersDrafterPairing(row: ModelRow): boolean {
    return !ModelRow.isStored(row) && row.backendType === 'mlx' && row.mlxKind === 'vision' && !this.drafts.has(row.backend)
  }

  /** Edit adapters…: the sheet over what the backend applies now. */
  editAdapters(): void {
    const row = this.selectedRow
    if (!row || !ModelsViewModel.takesAdapters(row)) return
    this.editingAdapters = new AdaptersForm(row.backend, row.backendType, this.adapters?.rows.map((entry) => entry.adapter) ?? [])
    this.changed()
    this.present?.('adapters')
  }

  editAdaptersForm(change: (form: AdaptersForm) => void): void {
    if (this.editingAdapters) change(this.editingAdapters)
    this.changed()
  }

  closeAdapters(): void {
    this.editingAdapters = undefined
    this.changed()
    this.dismissSheet?.()
  }

  /** Apply: `config set-backend <name> --adapters …`, read back, the CLI's own line said. */
  async applyAdapters(): Promise<void> {
    const form = this.editingAdapters
    if (!form) return
    this.closeAdapters()
    await this.run(form.arguments, undefined)
  }

  // MARK: - Roles

  /**
   * What a role's picker offers: the configured backends the CLI lets the
   * role name (its `role_refusal`, M33) -- the embedding role what embeds,
   * every other role what chats, an `mlx` entry by the kind `models list`
   * states -- and the one it names now.
   */
  choices(role: RoleAssignment): string[] {
    const current = RoleAssignment.backend(role)
    const fits = (row: ModelRow) => (role.role === 'embedding' ? row.mlxKind === undefined || row.mlxKind === 'embedding' : ModelRow.chats(row))
    return this.rows.filter((row) => !ModelRow.isStored(row) && (fits(row) || row.backend === current)).map((row) => row.backend)
  }

  /** Points a role at a backend: its `config set-default…` verb, read back through `models status`. */
  async assign(roleName: string, backend: string): Promise<void> {
    const role = this.roles.find((candidate) => candidate.role === roleName)
    if (!role || backend === '') return
    const label = roleWords.find(([key]) => key === roleName)?.[1] ?? roleName
    await this.run([...RoleAssignment.verb(role), backend], `${label}: ${backend}`)
  }

  // MARK: - The download sheet

  /** Download from Hugging Face…: the sheet on the prototype's own default. */
  openDownload(repo = 'Qwen/Qwen3-8B'): void {
    this.downloadForm = new DownloadForm(repo)
    this.changed()
    this.present?.('download')
  }

  editDownload(change: (form: DownloadForm) => void): void {
    if (this.downloadForm) change(this.downloadForm)
    this.changed()
  }

  cancelDownload(): void {
    this.downloadForm = undefined
    this.changed()
    this.dismissSheet?.()
  }

  /** Whether Start download can run: a binary found and a reference the pull takes (the Mac's `canStart`). */
  get canStartDownload(): boolean {
    return this.downloads.canStart && this.downloadForm?.canStart === true
  }

  /** Start download: the pull, the sheet closed, Jobs shown; with no binary nothing starts and the sheet stays. */
  startDownload(): void {
    const form = this.downloadForm
    if (!form || !this.canStartDownload) return
    this.cancelDownload()
    this.downloads.pull(form)
    this.pick('jobs')
  }

  // MARK: - Running a change

  private async run(args: readonly string[], done: string | undefined, keep?: 'deleted' | 'roles'): Promise<void> {
    const binary = this.binary
    if (binary === undefined || this.working !== undefined) return
    this.working = 'Working'
    this.changed()
    const outcome = await this.runner.command(binary, args)
    this.working = undefined
    await this.refresh()
    if (outcome.case !== 'done') {
      this.failure = CommandOutcome.failure(outcome)
      this.say?.(`That didn’t work: ${this.failure ?? ''}`)
      this.changed()
      return
    }
    this.failure = undefined
    if (keep === 'deleted') this.narration = DeleteNarration.said(done ?? 'Done', outcome.stdout)
    else if (keep === 'roles') this.narration = DeleteNarration.movingRoles(done ?? 'Done', outcome.stdout)
    this.say?.(done ?? outcome.stdout.trim().split('\n').at(-1) ?? 'Done')
    this.changed()
  }

  // MARK: - What the views draw

  /** The set a backend runs, as Stored weights found it, and its document. */
  private setOf(row: ModelRow): ModelInfoDocument | undefined {
    const handle = this.store.registered.get(row.backend)
    return handle === undefined ? undefined : this.store.infos.get(handle)
  }

  private static windowWord(tokens: number): string {
    return tokens % 1024 === 0 ? `${tokens / 1024}K` : tokens.toLocaleString('en-US')
  }

  private kindOf(row: ModelRow): 'local' | 'api' | 'subscription' | 'none' {
    return ModelKind.of(row.backendType) ?? 'none'
  }

  private status(row: ModelRow): { readonly text: string; readonly color: string; readonly dot: string } {
    const kind = this.kindOf(row)
    const draft = this.drafts.get(row.backend)
    if (draft && !BackendDraft.drafts(draft)) return { text: row.state, color: 'var(--status-warning)', dot: 'var(--status-warning)' }
    if (kind === 'api') return row.state === 'no key' ? { text: row.state, color: 'var(--text-tertiary)', dot: 'var(--line-strong)' } : { text: row.state, color: 'var(--model-api)', dot: 'var(--model-api)' }
    if (kind === 'subscription') return { text: row.state, color: 'var(--model-subscription)', dot: 'var(--model-subscription)' }
    if (kind === 'none') return { text: row.state, color: 'var(--text-tertiary)', dot: 'var(--line-strong)' }
    if (row.state === 'ok') return { text: row.state, color: 'var(--text-secondary)', dot: 'var(--text-tertiary)' }
    return { text: row.state, color: 'var(--status-warning)', dot: 'var(--status-warning)' }
  }

  private formatOf(row: ModelRow): { readonly format: string; readonly sub: string; readonly color: string } {
    if (row.backendType === 'mlx' && row.mlxKind !== undefined) return { format: `MLX · ${row.mlxKind}`, sub: row.quant ?? '', color: 'var(--text-primary)' }
    if (row.format !== undefined) return { format: [formatNames[row.format] ?? row.format, ...(row.quant ? [row.quant] : [])].join(' · '), sub: '', color: 'var(--text-secondary)' }
    return { format: groupTitles[row.group] ?? row.group, sub: '', color: 'var(--text-secondary)' }
  }

  face(): ModelsFace {
    const backends = this.rows.filter((row) => !ModelRow.isStored(row))
    const running = this.downloads.running
    const tabs: ReadonlyArray<readonly [ModelsTab, string, string]> = [
      ['backends', 'Backends', backends.length > 0 ? String(backends.length) : ''],
      ['store', 'Stored weights', this.store.sets.length > 0 ? String(this.store.sets.length) : ''],
      ['jobs', 'Jobs', running > 0 ? `${running} running` : ''],
      ['roles', 'Roles', ''],
    ]
    const sizes = this.store.sets.map((set) => this.store.infos.get(set.handle)?.data?.bytes)
    const storeLine = sizes.length > 0 && sizes.every((size) => size !== undefined) ? `${storeSize(sizes.reduce<number>((sum, size) => sum + (size ?? 0), 0))} on disk` : sizes.length > 0 ? 'sizes not stated for every set' : ''
    const rowFace = (row: ModelRow): BackendRow => {
      const set = this.setOf(row)
      const kind = this.kindOf(row)
      const format = this.formatOf(row)
      const status = this.status(row)
      const draft = this.drafts.get(row.backend)
      const memory = set ? ModelInfo.memory(set) : undefined
      const window = set?.data?.window?.tokens
      return {
        name: row.backend,
        kind,
        current: this.selected === row.name,
        roles: row.roles,
        format: format.format,
        formatSub: format.sub,
        formatColor: format.color,
        size: set?.data?.bytes !== undefined ? storeSize(set.data.bytes) : '—',
        window: window !== undefined ? ModelsViewModel.windowWord(window) : kind === 'local' || kind === 'none' ? '—' : '?',
        memory: kind === 'api' || kind === 'subscription' ? 'none' : memory !== undefined ? storeSize(memory) : '—',
        status: status.text,
        statusColor: status.color,
        dot: status.dot,
        draft: draft ? { shown: true, ok: BackendDraft.drafts(draft), tip: `Drafter: ${draft.path}${draft.said ? ` -- ${draft.said}` : ''}`, aria: BackendDraft.drafts(draft) ? `drafts with ${draft.path}` : `its drafter cannot draft: ${draft.said}` } : { shown: false, ok: true, tip: '', aria: '' },
      }
    }
    const sections = [
      { title: 'ON THIS MAC', rows: backends.filter((row) => row.section !== 'cloud').map(rowFace) },
      { title: 'CLOUD AND SUBSCRIPTIONS', rows: backends.filter((row) => row.section === 'cloud').map(rowFace) },
    ].filter((section) => section.rows.length > 0)
    return {
      tab: this.tab,
      tabs: tabs.map(([tab, label, count]) => ({ tab, label, current: this.tab === tab, count, running: tab === 'jobs' && running > 0 })),
      storeLine,
      backends: {
        sections,
        empty: sections.length === 0 ? 'Nothing here yet.' : '',
        narration: this.narration ? { shown: true, title: this.narration.title, text: this.narration.lines.join('\n') } : { shown: false, title: '', text: '' },
        failure: this.failure ?? '',
        detail: this.detail(),
      },
      store: this.store.face(),
      jobs: this.downloads.face(),
      roles: this.rolesFace(),
      pairSheet: this.pairFace(),
      adaptersSheet: this.adaptersFace(),
      downloadSheet: this.downloadFace(),
    }
  }

  private detail(): BackendDetail {
    const row = this.selectedRow
    const none: BackendDetail = { shown: false, name: '', tag: '', tagColor: '', status: '', statusColor: '', dot: '', actions: [], note: '', noteColor: '', facts: [], drafter: { shown: false, paired: false, ok: true, path: '', said: '', line: '', canPair: false, pairNone: '', pairTip: '', unpairTip: '' }, adapters: { shown: false, none: true, noneText: '', rows: [], tip: '', failure: '' }, lineage: [], files: [], failure: '' }
    if (!row) return none
    const info = this.info
    const field = (label: string) => (info ? ModelInfo.field(info, label) : undefined)
    const kind = this.kindOf(row)
    const status = this.status(row)
    const set = this.setOf(row)
    const handle = this.store.registered.get(row.backend)
    const format = this.formatOf(row)
    const groupTitle = groupTitles[row.group] ?? row.group
    const tag = kind === 'local' ? 'local · this Mac' : kind === 'api' ? 'API · billed per use' : kind === 'subscription' ? `subscription · ${groupTitle}` : `${row.backendType} · this Mac`
    const tagColor = kind === 'api' ? 'var(--model-api)' : kind === 'subscription' ? 'var(--model-subscription)' : 'var(--model-local)'
    const labels: Readonly<Record<BackendAction, readonly [string, 'btn' | 'btn-p' | 'btn-g']>> = {
      useInSession: ['Use in the open session', 'btn-p'],
      assignRole: ['Assign a role…', 'btn-g'],
      showWeights: ['Show its weights', 'btn-g'],
      removeBackend: ['Remove the backend…', 'btn-g'],
      deleteWithWeights: ['Delete with its weights…', 'btn-g'],
      manageKey: ['Manage key…', 'btn-g'],
      addKey: ['Add an API key…', 'btn-p'],
    }
    const type = field('type') ?? row.backendType
    const facts: Array<{ readonly label: string; readonly value: string; readonly mono: boolean }> = []
    if (kind === 'local' || kind === 'none') {
      facts.push({ label: 'Backend', value: row.backend, mono: true })
      facts.push({ label: 'Format', value: [format.format, ...(format.sub ? [format.sub] : [])].join(' · '), mono: true })
      const size = set?.data?.bytes !== undefined ? storeSize(set.data.bytes) : field('size')
      if (size !== undefined) facts.push({ label: 'On disk', value: size, mono: false })
      const window = field('window')
      if (window !== undefined) facts.push({ label: 'Window', value: window, mono: false })
      const bytes = set?.data?.bytes
      const cache = set?.data?.cache?.bytes
      if (bytes !== undefined) facts.push({ label: 'Memory', value: cache !== undefined ? `weights ${storeSize(bytes)} + cache ${storeSize(cache)} = ${storeSize(bytes + cache)}` : 'not known: the files state no cache', mono: false })
      facts.push({ label: 'Runs on', value: type === '' ? 'this Mac' : `${type} on this Mac`, mono: false })
      const path = field('model_path')
      if (path !== undefined) facts.push({ label: 'Path', value: path, mono: true })
      if (handle !== undefined) facts.push({ label: 'Weights', value: handle, mono: true })
    } else if (kind === 'api') {
      facts.push(
        { label: 'Provider', value: `the ${type} API`, mono: false },
        { label: 'Billing', value: 'per use, from your API key', mono: false },
        { label: 'Key', value: field('provider') ?? row.state, mono: false },
        { label: 'Window', value: field('window') ?? 'not reported', mono: false },
        // The old pane's Data row: an API backend sends what it is asked off this Mac.
        { label: 'Data', value: `sent to the ${type} API`, mono: false },
      )
    } else {
      facts.push({ label: 'Through', value: `${groupTitle} on this Mac`, mono: false }, { label: 'Billing', value: 'your plan', mono: false }, { label: 'Usage', value: 'not reported by the app', mono: false }, { label: 'Window', value: field('window') ?? 'not reported', mono: false })
    }
    const draft = this.drafts.get(row.backend)
    const drafts = draft ? BackendDraft.drafts(draft) : true
    const canPair = this.offersDrafterPairing(row)
    // Each line of the CLI's lineage is a step back: this set first, then each one it names.
    const lineageLines = (field('lineage') ?? '').split('\n').filter((line) => line !== '' && !line.startsWith('unknown'))
    const stepFroms = set?.data?.lineage.map((step) => step.from ?? '') ?? []
    const lineage = lineageLines.map((how, index, all) => {
      const label = index === 0 ? (handle ?? row.backend) : (stepFroms[index - 1] ?? '')
      const isLink = index > 0 && this.store.sets.some((candidate) => candidate.handle === label)
      return { label, how, isLink, dot: index === 0 ? 'var(--status-accent)' : 'var(--text-tertiary)', fill: index === 0 ? 'var(--status-accent)' : 'transparent', line: index === all.length - 1 ? 'transparent' : 'var(--line-strong)' }
    })
    const path = field('model_path')
    const sizeWords = set?.data?.bytes !== undefined ? storeSize(set.data.bytes) : (field('size') ?? '')
    const adapters = this.adapters
    return {
      shown: true,
      name: row.backend,
      tag,
      tagColor,
      status: [status.text, ...(row.roles ? [row.roles] : [])].join(' · '),
      statusColor: status.color,
      dot: status.dot,
      actions: this.actions(row).map((action) => {
        const [label, cls] = labels[action]
        return { action, label, kind: cls, disabled: this.working !== undefined }
      }),
      // The old pane's precedence (`main.html:5787-5789` at `b375fac9`): a drafter that cannot draft first, in the warning color; else the CLI's own note on the row.
      note: draft && !drafts ? `its draft cannot run beside it: ${draft.said.slice(BackendDraft.cannot.length)}` : (row.note ?? ''),
      noteColor: draft && !drafts ? 'var(--status-warning)' : 'var(--text-secondary)',
      facts,
      drafter: {
        shown: draft !== undefined || canPair,
        paired: draft !== undefined,
        ok: drafts,
        path: draft?.path ?? '',
        said: draft?.said ?? '',
        line: draft?.drafter ?? '',
        canPair,
        pairNone: `None. A drafter proposes the next tokens and ${row.backend} checks them, so every answer is still its own; it runs beside it through mlx-vlm's speculative path.`,
        pairTip: `apogee config set-backend ${row.backend} --draft <a drafter the binary offers it>`,
        unpairTip: `apogee config set-backend ${row.backend} --draft "" -- the backend answers alone again; the drafter stays under Stored weights`,
      },
      adapters: {
        shown: ModelsViewModel.takesAdapters(row),
        none: (adapters?.rows.length ?? 0) === 0,
        noneText: `None. An adapter applies over ${row.backend}’s weights at load, never merged into them.`,
        rows: (adapters?.rows ?? []).map((entry) => ({ path: entry.adapter.path, scale: `scale ${entry.adapter.scale}`, detail: entry.check?.detail ?? '', color: entry.check && entry.check.status !== 'ok' ? 'var(--status-warning)' : 'var(--text-secondary)' })),
        tip: `apogee config set-backend ${row.backend} --adapters <path>[:<scale>],… -- each pinned to the weights it runs now`,
        failure: adapters?.checkFailure ?? '',
      },
      lineage,
      files: path !== undefined && (kind === 'local' || kind === 'none') ? [{ path, size: sizeWords }] : [],
      failure: this.infoFailure ?? '',
    }
  }

  private rolesFace(): RolesFace {
    const name = (backend: string) => backend
    // models.default, the chat role's own choice, as the CLI names it on a role that falls back to it (`plain   (via models.default)`).
    const fallback = this.roles.find((candidate) => candidate.said.includes('(via models.default)'))
    const modelsDefault = fallback ? RoleAssignment.backend(fallback) : undefined
    return {
      suiteLine: this.rolesSuite === undefined ? '' : `Roles resolve under suite ${this.rolesSuite} (Suites changes it)`,
      failure: this.rolesFailure ?? '',
      rows: roleWords.flatMap(([key, label, desc]) => {
        const role = this.roles.find((candidate) => candidate.role === key)
        if (!role) return []
        const backend = RoleAssignment.backend(role)
        const set = RoleAssignment.isSet(role)
        const said = role.said.replace(/ {2,}/g, ' ').trim()
        const note = backend !== undefined && set ? `Set: ${name(backend)}` : said
        // The picker keeps the config's own choice for the role (`config set-default…`), as the prototype's does: the chat role's is models.default, even while a suite fills it; any other role a suite or models.default fills reads Not set, its note saying who fills it.
        const own = set ? backend : key === 'chat' ? modelsDefault : undefined
        const value = own ?? ''
        const choices = this.choices(role)
        const offered = value === '' || choices.includes(value) ? choices : [value, ...choices]
        return [{ key, label, desc, value, note, noteColor: backend === undefined ? 'var(--text-tertiary)' : 'var(--text-secondary)', options: [...(value === '' ? [{ value: '', label: 'Not set' }] : []), ...offered.map((choice) => ({ value: choice, label: choice }))] }]
      }),
    }
  }

  private pairFace(): PairSheetFace {
    const form = this.pairingDrafter
    if (!form) return { shown: false, title: '', offers: [], line: '', warning: '', command: '', canPair: false }
    const chosen = form.offers.find((offer) => offer.handle === form.chosen)
    return {
      shown: true,
      title: `Pair a drafter with ${form.backend}`,
      offers: form.offers.map((offer) => ({ handle: offer.handle, current: offer.handle === form.chosen })),
      line: chosen?.said ?? '',
      warning: form.offers.length === 0 ? `No stored drafter pairs with ${form.backend}. A drafter is a draft head converted to MLX for a base of its family and width -- Stored weights converts one with Convert to MLX…, then it is offered here.` : '',
      command: form.offers.length > 0 ? `apogee config set-backend ${form.backend} --draft ${chosen ? (chosen.path ?? chosen.handle) : '<a drafter>'}` : '',
      canPair: chosen !== undefined,
    }
  }

  private adaptersFace(): AdaptersSheetFace {
    const form = this.editingAdapters
    if (!form) return { shown: false, title: '', intro: '', none: '', rows: [], scaled: false, addLabel: '', canAdd: false, command: '', applyLabel: '' }
    return {
      shown: true,
      title: `Adapters on ${form.backend}`,
      intro: form.intro,
      none: form.entries.length === 0 ? 'None: applying takes every adapter off (--adapters "").' : '',
      rows: form.entries.map((entry) => ({ path: entry.path, scale: entry.scale })),
      scaled: !form.isMLX,
      addLabel: form.addLabel,
      canAdd: form.canAdd,
      command: form.command,
      applyLabel: form.applyLabel,
    }
  }

  private downloadFace(): DownloadSheetFace {
    const form = this.downloadForm
    if (!form) return { shown: false, repo: '', intro: '', facts: '', examples: [], format: 'gguf', quant: '', quants: [], steps: [], summary: '', canStart: false }
    return {
      shown: true,
      repo: form.repo,
      intro: 'Apogee downloads the snapshot, converts it, quantizes it and adds it to your library. A step that stops can be resumed.',
      facts: form.facts,
      examples: DownloadForm.examples,
      format: form.format,
      quant: form.quant,
      quants: form.format === 'gguf' ? DownloadForm.levels.map((level) => ({ value: level, label: level === 'F16' ? 'F16 (keep full precision)' : level })) : [],
      steps: form.steps,
      summary: form.summary,
      canStart: this.canStartDownload,
    }
  }
}

export interface BackendRow {
  readonly name: string
  readonly kind: 'local' | 'api' | 'subscription' | 'none'
  readonly current: boolean
  readonly roles: string
  readonly format: string
  readonly formatSub: string
  readonly formatColor: string
  readonly size: string
  readonly window: string
  readonly memory: string
  readonly status: string
  readonly statusColor: string
  readonly dot: string
  readonly draft: { readonly shown: boolean; readonly ok: boolean; readonly tip: string; readonly aria: string }
}

export interface BackendDetail {
  readonly shown: boolean
  readonly name: string
  readonly tag: string
  readonly tagColor: string
  readonly status: string
  readonly statusColor: string
  readonly dot: string
  readonly actions: ReadonlyArray<{ readonly action: BackendAction; readonly label: string; readonly kind: 'btn' | 'btn-p' | 'btn-g'; readonly disabled: boolean }>
  readonly note: string
  readonly noteColor: string
  readonly facts: ReadonlyArray<{ readonly label: string; readonly value: string; readonly mono: boolean }>
  readonly drafter: { readonly shown: boolean; readonly paired: boolean; readonly ok: boolean; readonly path: string; readonly said: string; readonly line: string; readonly canPair: boolean; readonly pairNone: string; readonly pairTip: string; readonly unpairTip: string }
  readonly adapters: { readonly shown: boolean; readonly none: boolean; readonly noneText: string; readonly rows: ReadonlyArray<{ readonly path: string; readonly scale: string; readonly detail: string; readonly color: string }>; readonly tip: string; readonly failure: string }
  readonly lineage: ReadonlyArray<{ readonly label: string; readonly how: string; readonly isLink: boolean; readonly dot: string; readonly fill: string; readonly line: string }>
  readonly files: ReadonlyArray<{ readonly path: string; readonly size: string }>
  readonly failure: string
}

export interface RolesFace {
  readonly suiteLine: string
  readonly failure: string
  readonly rows: ReadonlyArray<{ readonly key: string; readonly label: string; readonly desc: string; readonly value: string; readonly note: string; readonly noteColor: string; readonly options: ReadonlyArray<{ readonly value: string; readonly label: string }> }>
}

export interface PairSheetFace {
  readonly shown: boolean
  readonly title: string
  readonly offers: ReadonlyArray<{ readonly handle: string; readonly current: boolean }>
  readonly line: string
  readonly warning: string
  readonly command: string
  readonly canPair: boolean
}

export interface AdaptersSheetFace {
  readonly shown: boolean
  readonly title: string
  readonly intro: string
  readonly none: string
  readonly rows: ReadonlyArray<{ readonly path: string; readonly scale: string }>
  readonly scaled: boolean
  readonly addLabel: string
  readonly canAdd: boolean
  readonly command: string
  readonly applyLabel: string
}

export interface DownloadSheetFace {
  readonly shown: boolean
  readonly repo: string
  readonly intro: string
  readonly facts: string
  readonly examples: readonly string[]
  readonly format: 'gguf' | 'mlx'
  readonly quant: string
  readonly quants: ReadonlyArray<{ readonly value: string; readonly label: string }>
  readonly steps: readonly string[]
  readonly summary: string
  readonly canStart: boolean
}

/** Everything the Models place draws, as words, absence said explicitly. */
export interface ModelsFace {
  readonly tab: ModelsTab
  readonly tabs: ReadonlyArray<{ readonly tab: ModelsTab; readonly label: string; readonly current: boolean; readonly count: string; readonly running: boolean }>
  readonly storeLine: string
  readonly backends: {
    readonly sections: ReadonlyArray<{ readonly title: string; readonly rows: readonly BackendRow[] }>
    readonly empty: string
    readonly narration: { readonly shown: boolean; readonly title: string; readonly text: string }
    readonly failure: string
    readonly detail: BackendDetail
  }
  readonly store: StoreFace
  readonly jobs: { readonly cards: readonly CardFace[]; readonly running: number }
  readonly roles: RolesFace
  readonly pairSheet: PairSheetFace
  readonly adaptersSheet: AdaptersSheetFace
  readonly downloadSheet: DownloadSheetFace
}
