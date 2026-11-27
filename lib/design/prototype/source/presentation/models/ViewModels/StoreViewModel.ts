import { CommandOutcome } from '../../../business/reads/CommandRunning'
import type { CommandRunning } from '../../../business/reads/CommandRunning'
import { MLXKind, ModelInfo, ModelRow } from '../../../business/reads/Models'
import type { ModelInfoDocument } from '../../../business/reads/Models'
import { ReadOutcome } from '../../../business/reads/ReadRunner'
import { Observable } from '../../styles/Observation/Observable'
import { DeleteNarration, DeletePlan } from './DeletePlan'
import { ConvertForm, QuantizeForm, RegisterForm, RenameForm, modelBaseName, stepLine, storeSize } from './StoreForms'

/** One set in the model store, named by the handle every store verb takes (`<model>/<format>/<id>`): the Mac's `StoredSet`. */
export interface StoredSet {
  readonly handle: string
  /** `safetensors`, `gguf`, `mlx`, `pipeline`, `voice`. */
  readonly format: string
  /** The precision the list states. */
  readonly precision: string | undefined
  /** The CLI's own state for its row. */
  readonly cliState: string
  /** The backends that run it, in the list's order. */
  readonly backends: readonly string[]
  /** Listed only by `models list --all`: a conversion consumed it. */
  readonly consumed: boolean
  /** An MLX set's kind, as its row states it. */
  readonly kind: string | undefined
}

/** What a job reported it made (design 1h): the set's document, and the backend its run registered on it. */
export interface Landed {
  readonly info: ModelInfoDocument
  readonly backend?: string | undefined
}

/** What Stored weights offers on a set. */
export type StoreAction = 'convertToGGUF' | 'convertToMLX' | 'quantize' | 'register' | 'useInSession' | 'removeBackend' | 'newClip' | 'makeDefaultVideo' | 'openVoice' | 'checkFiles' | 'showInFinder' | 'delete'

/** A change waiting on the person's word. */
export type StorePending =
  | { readonly case: 'deleteSet'; readonly set: StoredSet; readonly plan: DeletePlan }
  | { readonly case: 'deleteModel'; readonly model: string; readonly plan: DeletePlan }
  | { readonly case: 'removeBackend'; readonly name: string }
  | { readonly case: 'registerAll'; readonly handles: readonly string[] }

/** What a confirm says, the prototype's words with the CLI's plan. */
export interface ConfirmWords {
  readonly kind: 'storeDelete' | 'storeModelDelete' | 'unregister' | 'registerAll'
  readonly title: string
  readonly body: string
  readonly detail: string
  readonly confirm: string
  /** The second door, "…and the backends", only where the plan names one; empty otherwise. */
  readonly alternative: string
  readonly destructive: boolean
}

/** A sheet open on a set, as its view draws it. */
export interface StoreSheet {
  readonly kind: 'convert' | 'quantize' | 'register' | 'rename'
  readonly title: string
  readonly source: string
  readonly intro: string
  readonly steps: readonly string[]
  readonly notes: readonly string[]
  readonly errors: readonly string[]
  readonly command: string
  readonly goLabel: string
  readonly canGo: boolean
}

const formatNames: Readonly<Record<string, string>> = { safetensors: 'SafeTensors', gguf: 'GGUF', mlx: 'MLX', pipeline: 'Pipeline', voice: 'Voice' }
const formatName = (format: string) => formatNames[format] ?? format
const stateColors = {
  accent: { color: 'var(--status-accent)', dot: 'var(--status-accent)' },
  quiet: { color: 'var(--text-secondary)', dot: 'var(--text-tertiary)' },
  used: { color: 'var(--text-tertiary)', dot: 'var(--text-tertiary)' },
  weights: { color: 'var(--text-secondary)', dot: 'var(--status-warning)' },
  backend: { color: 'var(--text-primary)', dot: 'var(--status-accent)' },
  unregistered: { color: 'var(--status-warning)', dot: 'var(--status-warning)' },
} as const

/**
 * The Models place's Stored weights (design 1h): every set by handle,
 * grouped by model, its facts, what it was made from and what was made from
 * it, and the store's verbs, each the CLI's own and read back. The Mac app's
 * `StoreViewModel`, drawn as the prototype's tab (`main.html` at `b375fac9`,
 * section `stored weights`).
 *
 * Sets come from `models list` and `models list --all` (their difference is
 * what a conversion consumed); a registered set from its backend's `models
 * info` and then `models info <its path>`, which names its handle. Nothing is
 * predicted: a name comes from `config scan`, what a delete strands from its
 * own plan, and what it did from its words. The stand-in keeps no state, so
 * what a job reports it made (`land`) is kept beside what the store read
 * says, never from the clock.
 */
export class StoreViewModel extends Observable {
  sets: StoredSet[] = []
  /** How many sets the plain list folded; `--all` shows them. */
  folded = 0
  showingAll = false
  readonly infos = new Map<string, ModelInfoDocument>()
  /** Why a set's `models info` could not be read, by its handle. */
  readonly unread = new Map<string, string>()
  selected: string | undefined = undefined
  convertForm: ConvertForm | undefined = undefined
  quantizeForm: QuantizeForm | undefined = undefined
  register: RegisterForm | undefined = undefined
  renaming: RenameForm | undefined = undefined
  pending: StorePending | undefined = undefined
  working: string | undefined = undefined
  failure: string | undefined = undefined
  narration: DeleteNarration | undefined = undefined
  /** Every backend the list names: what a new name may not be. */
  backendNames: string[] = []
  /** The registered sets' handles, by their backend: Show its weights, a backend's size and memory. */
  readonly registered = new Map<string, string>()

  /** Says one line in the window's toast. */
  say: ((text: string) => void) | undefined = undefined
  /** Opens one of the place's sheets in the shell's host, by kind. */
  present: ((kind: string) => void) | undefined = undefined
  dismissSheet: (() => void) | undefined = undefined
  /** Asks a confirm in the shell's host, by kind. */
  ask: ((kind: string) => void) | undefined = undefined
  /** Starts a store job on the Jobs tab: the place's way to it. */
  startConvert: ((form: ConvertForm) => void) | undefined = undefined
  startQuantize: ((form: QuantizeForm) => void) | undefined = undefined
  startCheck: ((handle: string) => void) | undefined = undefined
  /** The open session on a backend: the composition's way to Sessions. */
  useInSession: ((backend: string) => void) | undefined = undefined
  /** Another place: Voice, Video. */
  goTo: ((place: 'voice' | 'video') => void) | undefined = undefined
  /** The job working on a set, by its handle: `convert`, `quantize`, `check`. */
  busy: (handle: string) => string | undefined = () => undefined

  private binary: string | undefined = undefined
  private listed: StoredSet[] = []
  private readonly landed = new Map<string, Landed>()

  constructor(private readonly runner: CommandRunning) {
    super()
  }

  connect(binary: string | undefined): void {
    this.binary = binary
  }

  /** The sets: the lists, the registered sets' handles, then each set's details. */
  async refresh(): Promise<void> {
    const binary = this.binary
    if (binary === undefined) return
    const all = ReadOutcome.documentOf(await this.runner.read(binary, ['models', 'list', '--all']))
    const plain = ReadOutcome.documentOf(await this.runner.read(binary, ['models', 'list']))
    if (!all || !plain) return
    this.folded = ModelRow.folded(plain)
    const shown = new Set(ModelRow.list(plain).map((row) => row.name))
    const rows = ModelRow.list(all)
    this.backendNames = rows.filter((row) => !ModelRow.isStored(row)).map((row) => row.backend)
    const found: StoredSet[] = rows.filter(ModelRow.isStored).flatMap((row) => {
      const consumed = !shown.has(row.name)
      if (consumed && !this.showingAll) return []
      return [{ handle: row.model, format: row.format ?? row.group, precision: row.quant, cliState: row.state, backends: [], consumed, kind: row.mlxKind }]
    })
    for (const row of rows) {
      if (ModelRow.isStored(row) || (row.backendType !== 'llamacpp' && row.backendType !== 'mlx')) continue
      const handle = await this.handleOf(row.backend)
      if (handle === undefined) continue
      this.registered.set(row.backend, handle)
      const at = found.findIndex((set) => set.handle === handle)
      const existing = found[at]
      if (existing) found[at] = { ...existing, backends: [...existing.backends, row.backend] }
      else found.push({ handle, format: row.format ?? (row.backendType === 'mlx' ? 'mlx' : 'gguf'), precision: row.quant, cliState: row.state, backends: [row.backend], consumed: false, kind: row.mlxKind })
    }
    this.listed = found
    for (const set of found) if (!this.infos.has(set.handle) || set.handle === this.selected) await this.readInfo(set.handle)
    this.compose()
  }

  /** The table's sets: what the store read says, and what jobs reported they made. */
  private compose(): void {
    const sets = [...this.listed]
    for (const [handle, report] of this.landed) {
      const at = sets.findIndex((set) => set.handle === handle)
      const backend = report.backend
      const existing = sets[at]
      if (existing) {
        if (backend !== undefined && !existing.backends.includes(backend)) sets[at] = { ...existing, backends: [...existing.backends, backend] }
        continue
      }
      const data = report.info.data
      sets.push({ handle, format: data?.format ?? 'gguf', precision: ModelInfo.field(report.info, 'quantization'), cliState: 'ok', backends: backend === undefined ? [] : [backend], consumed: false, kind: data?.kind })
    }
    // A snapshot a reported conversion was made from is used by it, as the plain list then folds it.
    const used = new Set([...this.landed.values()].flatMap((report) => report.info.data?.lineage.filter((step) => step.kind === 'converted').map((step) => step.from ?? '') ?? []))
    let newlyFolded = 0
    const out: StoredSet[] = []
    for (const set of sets) {
      if (set.format === 'safetensors' && !set.consumed && used.has(set.handle)) {
        newlyFolded += 1
        if (this.showingAll || set.handle === this.selected) out.push({ ...set, consumed: true })
        continue
      }
      out.push(set)
    }
    this.sets = out.sort((a, b) => compareKeys([this.model(a), a.format, a.handle], [this.model(b), b.format, b.handle]))
    this.foldedByJobs = newlyFolded
    if (this.selected !== undefined && !this.sets.some((set) => set.handle === this.selected)) this.selected = undefined
    this.changed()
  }

  private foldedByJobs = 0

  /** A backend's stored set: its `models info` names its file, and `models info <that path>` names the set's handle. */
  private async handleOf(backend: string): Promise<string | undefined> {
    const binary = this.binary
    if (binary === undefined) return undefined
    const document = ReadOutcome.documentOf(await this.runner.read(binary, ['models', 'info', backend]))
    const path = document ? ModelInfo.field(ModelInfo.read(document), 'model_path') : undefined
    if (path === undefined) return undefined
    const set = ReadOutcome.documentOf(await this.runner.read(binary, ['models', 'info', path]))
    if (!set) return undefined
    const info = ModelInfo.read(set)
    const handle = info.data?.handle
    if (handle !== undefined) this.infos.set(handle, info)
    return handle
  }

  private async readInfo(handle: string): Promise<void> {
    const binary = this.binary
    if (binary === undefined) return
    const outcome = await this.runner.read(binary, ['models', 'info', handle])
    const document = ReadOutcome.documentOf(outcome)
    if (document) {
      this.infos.set(handle, ModelInfo.read(document))
      this.unread.delete(handle)
    } else this.unread.set(handle, ReadOutcome.failure(outcome) ?? 'apogee printed nothing')
  }

  /** What a job reported it made: kept beside the store's read, which the stand-in never changes. */
  land(sets: readonly Landed[]): void {
    for (const report of sets) {
      const handle = report.info.data?.handle
      if (handle === undefined) continue
      this.infos.set(handle, report.info)
      const before = this.landed.get(handle)
      this.landed.set(handle, { info: report.info, backend: report.backend ?? before?.backend })
    }
    this.compose()
  }

  /** Show them / Fold them: `models list --all`, or the plain list. */
  async setShowingAll(on: boolean): Promise<void> {
    this.showingAll = on
    await this.refresh()
  }

  // MARK: - What a set is

  private model(set: StoredSet): string {
    return set.handle.split('/')[0] ?? set.handle
  }

  private id(set: StoredSet): string {
    return set.handle.split('/').slice(1).join('/')
  }

  /** What an MLX set is for the app's offers: its stated kind, else a chat model's. */
  private mlxKind(set: StoredSet): string | undefined {
    return set.format === 'mlx' ? MLXKind.reading(set.kind) : undefined
  }

  /** Whether it runs once a backend points at it: a GGUF, or an MLX set of a kind an `mlx` entry runs. */
  isRunnable(set: StoredSet): boolean {
    const kind = this.mlxKind(set)
    return set.format === 'gguf' || (kind !== undefined && MLXKind.registers(kind))
  }

  /** Whether a GGUF is unquantized -- F16, BF16 or F32 -- by its stated precision, else by the level its file is named with. */
  isUnquantized(set: StoredSet): boolean {
    if (set.format !== 'gguf') return false
    const levels = ['F16', 'BF16', 'F32']
    if (set.precision !== undefined) return levels.includes(set.precision.toUpperCase())
    const path = this.infoField(set, 'model_path')
    const stem = path?.split('/').at(-1)?.replace(/\.gguf$/, '').toUpperCase()
    return stem !== undefined && levels.some((level) => stem.endsWith(`-${level}`))
  }

  /** Whether a SafeTensors set converts to GGUF, or to MLX, as its `models info` states it; offered where it does not say. */
  converts(set: StoredSet, mlx: boolean): boolean {
    if (set.format !== 'safetensors') return false
    const formats = this.infos.get(set.handle)?.data?.convertsTo
    return formats === undefined || formats.includes(mlx ? 'mlx' : 'gguf')
  }

  private infoField(set: StoredSet, label: string): string | undefined {
    const info = this.infos.get(set.handle)
    return info ? ModelInfo.field(info, label) : undefined
  }

  private bytes(set: StoredSet): number | undefined {
    return this.infos.get(set.handle)?.data?.bytes
  }

  /** The state the table shows, its color and its dot: the prototype's `stateOf`, in the CLI's words where it has them. */
  private state(set: StoredSet): { readonly text: string; readonly color: string; readonly dot: string } {
    const busy = this.busy(set.handle)
    if (busy !== undefined) return { text: busy === 'check' ? 'checking its files' : busy === 'quantize' ? 'quantizing' : 'converting', ...stateColors.accent }
    const kind = this.mlxKind(set)
    if (set.format === 'pipeline') return { text: 'pipeline', ...stateColors.quiet }
    if (set.format === 'voice') return { text: 'voice model', ...stateColors.quiet }
    if (kind === 'drafter') return { text: 'drafts beside its base', ...stateColors.quiet }
    if (kind === 'voice') return { text: 'for voice say', ...stateColors.quiet }
    if (kind === 'video') return { text: 'for video generate', ...stateColors.quiet }
    if (set.format === 'safetensors') return set.consumed ? { text: 'used by a conversion', ...stateColors.used } : { text: 'weights only · convert to run', ...stateColors.weights }
    if (set.backends.length > 0) return { text: `backend ${set.backends.join(', ')}`, ...stateColors.backend }
    if (kind !== undefined && !MLXKind.registers(kind)) return { text: 'nothing here runs it', ...stateColors.quiet }
    return { text: 'not registered', ...stateColors.unregistered }
  }

  /** The made-from line under a row: its first lineage step, when it was made here. */
  private madeFrom(set: StoredSet): string | undefined {
    const step = this.infos.get(set.handle)?.data?.lineage[0]
    return step && step.from !== undefined && (step.kind === 'converted' || step.kind === 'quantized') ? `${step.kind} from ${step.from}` : undefined
  }

  /** The unregistered sets `config scan --register` would register: runnable, no backend, not consumed. */
  get unregistered(): StoredSet[] {
    return this.sets.filter((set) => this.isRunnable(set) && set.backends.length === 0 && !set.consumed)
  }

  get current(): StoredSet | undefined {
    return this.sets.find((set) => set.handle === this.selected)
  }

  /** What Stored weights offers on `set`, in the order the details draw them: the prototype's. */
  actions(set: StoredSet): StoreAction[] {
    const out: StoreAction[] = []
    if (set.format === 'safetensors') {
      if (this.converts(set, false)) out.push('convertToGGUF')
      if (this.converts(set, true)) out.push('convertToMLX')
    } else if (this.isUnquantized(set)) {
      out.push('quantize', set.backends.length > 0 ? 'useInSession' : 'register')
    } else if (this.isRunnable(set)) {
      out.push(set.backends.length > 0 ? 'useInSession' : 'register')
    } else if (set.format === 'pipeline') {
      out.push('newClip', 'makeDefaultVideo')
    } else if (set.format === 'voice') {
      out.push('openVoice')
    }
    if (this.isRunnable(set) && set.backends.length > 0) out.push('removeBackend')
    return [...out, 'checkFiles', 'showInFinder', 'delete']
  }

  // MARK: - Intents

  /** A row's click: selects it, or closes it when it is the one shown; a used snapshot shows the folded ones. */
  async toggle(handle: string): Promise<void> {
    if (this.selected === handle) {
      this.close()
      return
    }
    await this.select(handle)
  }

  async select(handle: string): Promise<void> {
    this.selected = handle
    const set = this.sets.find((candidate) => candidate.handle === handle)
    if (set?.consumed && !this.showingAll) this.showingAll = true
    this.changed()
    if (!this.infos.has(handle)) {
      await this.readInfo(handle)
      this.changed()
    }
  }

  close(): void {
    this.selected = undefined
    this.changed()
  }

  /** An action on the selected set; the asynchronous ones are `actAsync`'s. */
  act(action: StoreAction): void {
    void this.actAsync(action)
  }

  async actAsync(action: StoreAction): Promise<void> {
    const set = this.current
    if (!set || this.busy(set.handle) !== undefined) return
    switch (action) {
      case 'convertToGGUF':
      case 'convertToMLX':
        this.convertForm = action === 'convertToMLX' ? ConvertForm.mlx(set.handle, this.bytes(set)) : ConvertForm.gguf(set.handle, this.bytes(set))
        this.open('storeConvert')
        return
      case 'quantize':
        this.quantizeForm = new QuantizeForm(set.handle, modelBaseName(this.model(set)), this.bytes(set))
        this.open('storeQuantize')
        return
      case 'register':
        await this.askToRegister(set)
        return
      case 'useInSession':
        if (set.backends[0] !== undefined) this.useInSession?.(set.backends[0])
        return
      case 'removeBackend':
        if (set.backends[0] !== undefined) this.askToRemoveBackend(set.backends[0])
        return
      case 'newClip':
        this.goTo?.('video')
        return
      case 'makeDefaultVideo':
        await this.run(['config', 'set-default-video', set.handle], `Video generates with ${set.handle}`)
        return
      case 'openVoice':
        this.goTo?.('voice')
        return
      case 'checkFiles':
        this.startCheck?.(set.handle)
        return
      case 'showInFinder':
        this.say?.(`Revealed ~/.apogee/models/${set.handle} in Finder (prototype)`)
        return
      case 'delete':
        await this.askToDelete(set)
        return
    }
  }

  private open(kind: string): void {
    this.changed()
    this.present?.(kind)
  }

  /** Opens the register sheet: the name `config scan` lists the set's path under; the path, any projector and the window from `models info`. */
  async askToRegister(set: StoredSet): Promise<void> {
    const binary = this.binary
    const path = this.infoField(set, 'model_path')
    if (binary === undefined || path === undefined || !this.isRunnable(set)) return
    let name = ''
    const scanned = await this.runner.command(binary, ['config', 'scan'])
    if (scanned.case === 'done') name = StoreViewModel.scanName(path, scanned.stdout) ?? ''
    this.register = new RegisterForm(set.handle, set.format === 'mlx' ? 'mlx' : 'llamacpp', path, name, this.infoField(set, 'projector'), this.infos.get(set.handle)?.data?.window?.tokens)
    this.open('storeRegister')
  }

  /** The name `config scan` lists a stored model's path under: each entry is its name and state, then its path. */
  static scanName(path: string, report: string): string | undefined {
    const lines = report.split('\n')
    const at = lines.findIndex((line, index) => index > 0 && line.trim() === path)
    return at > 0 ? lines[at - 1]?.trim().split(' ')[0] : undefined
  }

  /** Rename… on the model folder: `models rename`. */
  askToRename(model: string): void {
    const sets = this.sets.filter((set) => this.model(set) === model)
    this.renaming = new RenameForm(model, sets.length, sets.flatMap((set) => set.backends))
    this.open('storeRename')
  }

  editConvert(change: (form: ConvertForm) => void): void {
    if (this.convertForm) change(this.convertForm)
    this.changed()
  }

  /** The convert sheet's Make choice: GGUF or MLX, each opening as the prototype opens it. */
  retarget(mlx: boolean): void {
    if (this.convertForm && this.convertForm.mlx !== mlx) this.convertForm = this.convertForm.retarget(mlx)
    this.changed()
  }

  editQuantize(change: (form: QuantizeForm) => void): void {
    if (this.quantizeForm) change(this.quantizeForm)
    this.changed()
  }

  editRegister(change: (form: RegisterForm) => void): void {
    if (this.register) change(this.register)
    this.changed()
  }

  editRename(change: (form: RenameForm) => void): void {
    if (this.renaming) change(this.renaming)
    this.changed()
  }

  /** The sheet's Cancel. */
  cancelSheet(): void {
    this.convertForm = undefined
    this.quantizeForm = undefined
    this.register = undefined
    this.renaming = undefined
    this.changed()
    this.dismissSheet?.()
  }

  /** The sheet's go button. */
  go(): void {
    void this.goAsync()
  }

  async goAsync(): Promise<void> {
    const sheet = this.sheet()
    if (!sheet?.canGo) return
    const convert = this.convertForm
    const quantize = this.quantizeForm
    const register = this.register
    const renaming = this.renaming
    this.cancelSheet()
    if (convert) this.startConvert?.(convert)
    else if (quantize) this.startQuantize?.(quantize)
    else if (register) await this.run(register.arguments, `Registered ${register.name.trim()}`)
    else if (renaming) await this.run(renaming.arguments, `${renaming.model} is now ${renaming.name.trim()}`)
  }

  /** Asks before `models delete <handle>`: the CLI's own plan read first. A plan the CLI refuses is its failure, and nothing is asked. */
  async askToDelete(set: StoredSet): Promise<void> {
    const plan = await this.plan(set.handle)
    if (!plan) return
    this.pending = { case: 'deleteSet', set, plan }
    this.changed()
    this.ask?.('storeDelete')
  }

  /** Asks before `models delete <model>`, every set of it. */
  async askToDeleteModel(model: string): Promise<void> {
    const plan = await this.plan(model)
    if (!plan) return
    this.pending = { case: 'deleteModel', model, plan }
    this.changed()
    this.ask?.('storeModelDelete')
  }

  askToRemoveBackend(name: string): void {
    this.pending = { case: 'removeBackend', name }
    this.changed()
    this.ask?.('unregister')
  }

  /** Asks before `config scan --register`, naming every set it registers. */
  async askToRegisterAll(): Promise<void> {
    const handles = this.unregistered.map((set) => set.handle)
    if (handles.length === 0) return
    this.pending = { case: 'registerAll', handles }
    this.changed()
    this.ask?.('registerAll')
  }

  private async plan(name: string): Promise<DeletePlan | undefined> {
    const binary = this.binary
    if (binary === undefined || this.working !== undefined) return undefined
    this.working = DeletePlan.reading
    this.changed()
    const outcome = await this.runner.command(binary, ['models', 'delete', name])
    this.working = undefined
    if (outcome.case !== 'done') {
      this.failure = CommandOutcome.failure(outcome)
      this.changed()
      return undefined
    }
    this.failure = undefined
    return DeletePlan.of(outcome.stdout)
  }

  /** What the confirm asks: the prototype's words with the CLI's plan. */
  confirmWords(): ConfirmWords | undefined {
    const pending = this.pending
    if (!pending) return undefined
    switch (pending.case) {
      case 'deleteSet':
        return { kind: 'storeDelete', title: `Delete ${pending.set.handle}?`, body: '', detail: DeletePlan.question(pending.plan, `apogee models delete ${pending.set.handle} --yes`), confirm: 'Delete', alternative: pending.plan.backends.length > 0 ? 'Delete it and the backends that run from it' : '', destructive: true }
      case 'deleteModel':
        return { kind: 'storeModelDelete', title: `Delete all of ${pending.model}?`, body: '', detail: DeletePlan.question(pending.plan, `apogee models delete ${pending.model} --yes`), confirm: 'Delete', alternative: pending.plan.backends.length > 0 ? 'Delete it and the backends that run from it' : '', destructive: true }
      case 'removeBackend':
        return { kind: 'unregister', title: `Remove the backend ${pending.name}?`, body: 'Its config entry goes. The weights stay under Stored weights, ready to register again.', detail: `apogee config delete-backend ${pending.name}`, confirm: 'Remove', alternative: '', destructive: true }
      case 'registerAll':
        return {
          kind: 'registerAll',
          title: `Register ${pending.handles.length} stored ${pending.handles.length === 1 ? 'model' : 'models'}?`,
          body: 'Each becomes a backend, named as config scan names it: the model’s name and its precision. Nothing is copied or loaded.',
          detail: `${pending.handles.join('\n')}\n\napogee config scan --register`,
          confirm: 'Register',
          alternative: '',
          destructive: false,
        }
    }
  }

  /** The person's yes -- `andBackends` the second door -- each read back, what it printed kept. */
  async confirm(andBackends: boolean): Promise<void> {
    const pending = this.pending
    if (!pending) return
    this.pending = undefined
    this.changed()
    switch (pending.case) {
      case 'deleteSet':
      case 'deleteModel': {
        const name = pending.case === 'deleteSet' ? pending.set.handle : pending.model
        const withBackends = andBackends && pending.plan.backends.length > 0
        await this.run(['models', 'delete', name, '--yes', ...(withBackends ? ['--and-backends'] : [])], `Deleted ${name}`, 'deleted')
        if (pending.case === 'deleteSet' && this.selected === name) this.close()
        return
      }
      case 'removeBackend':
        await this.run(['config', 'delete-backend', pending.name], `Removed the backend ${pending.name}; its weights stay`, 'roles')
        return
      case 'registerAll':
        await this.run(['config', 'scan', '--register'], undefined, 'registered')
        return
    }
  }

  dismissPending(): void {
    this.pending = undefined
    this.changed()
  }

  dismissNarration(): void {
    this.narration = undefined
    this.changed()
  }

  dismissFailure(): void {
    this.failure = undefined
    this.changed()
  }

  /** Runs a change and reads the store back, its result said: the `done` line, else the CLI's last; what a delete printed kept. */
  private async run(args: readonly string[], done: string | undefined, keep?: 'deleted' | 'roles' | 'registered'): Promise<void> {
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
    const said = outcome.stdout
    if (keep === 'deleted') this.narration = DeleteNarration.said(done ?? 'Done', said)
    else if (keep === 'roles') this.narration = DeleteNarration.movingRoles(done ?? 'Done', said)
    else if (keep === 'registered') this.narration = { title: 'Registered what the scan found', lines: said.split('\n').filter((line, index, lines) => !(line === '' && index === lines.length - 1)) }
    this.say?.(done ?? said.trim().split('\n').at(-1) ?? 'Done')
    this.changed()
  }

  // MARK: - What the views draw

  /** The open sheet, as its view draws it; `undefined` with none open. */
  sheet(): StoreSheet | undefined {
    const taken = this.backendNames
    const setOf = (handle: string) => this.sets.find((set) => set.handle === handle)
    const sourceOf = (handle: string, withPrecision = false) => {
      const set = setOf(handle)
      const bytes = set ? this.bytes(set) : undefined
      const arch = set ? this.infos.get(set.handle)?.data?.architecture : undefined
      return [handle, ...(withPrecision && set?.precision ? [set.precision] : []), ...(bytes !== undefined ? [storeSize(bytes)] : []), ...(arch ? [arch] : [])].join(' · ')
    }
    if (this.convertForm) {
      const form = this.convertForm
      const errors = form.errors(taken)
      const arch = this.infos.get(form.handle)?.data?.architecture
      return {
        kind: 'convert',
        title: `Convert ${form.handle.split('/')[0] ?? form.handle}`,
        source: sourceOf(form.handle),
        intro: form.mlx ? 'An MLX model, made by mlx-lm’s converter on this Mac and kept beside these weights in the model’s mlx/ folder.' : 'A GGUF, made by llama.cpp’s converter on this Mac and kept beside these weights in the model’s gguf/ folder. Quantized copies are made from it.',
        steps: form.steps.map(stepLine),
        notes: ['Sizes are estimated from bits per weight.', ...(form.aloneNote ? [form.aloneNote] : []), `Whether the converter takes ${arch ?? 'this architecture'} is learned by converting. If it can’t, the job stops with the converter’s own reason.`],
        errors,
        command: form.command,
        goLabel: 'Convert',
        canGo: errors.length === 0,
      }
    }
    if (this.quantizeForm) {
      const form = this.quantizeForm
      const errors = form.errors(taken)
      const set = setOf(form.handle)
      const level = set?.precision ?? 'F16'
      return {
        kind: 'quantize',
        title: `Quantize ${form.handle.split('/')[0] ?? form.handle}`,
        source: sourceOf(form.handle, true),
        intro: `A smaller copy beside it, made by llama.cpp. The ${level} stays.`,
        steps: form.steps.map(stepLine),
        notes: ['Sizes are estimated from bits per weight against its 16.'],
        errors,
        command: form.command,
        goLabel: 'Quantize',
        canGo: errors.length === 0,
      }
    }
    if (this.register) {
      const form = this.register
      const errors = form.errors(taken)
      const set = setOf(form.handle)
      return {
        kind: 'register',
        title: `Register ${form.handle.split('/')[0] ?? form.handle}${set?.precision ? ` · ${set.precision}` : ''}`,
        source: sourceOf(form.handle),
        intro: 'A backend is one config entry pointing at these weights, so sessions, suites and roles can use them. Nothing is copied or loaded.',
        steps: [],
        notes: [form.type === 'mlx' ? 'The window sizes its attention cache; a larger one needs more memory while it runs.' : 'q8_0 halves the attention cache’s memory against f16; q4_0 halves it again, at some cost to long answers.'],
        errors,
        command: form.command,
        goLabel: 'Register',
        canGo: errors.length === 0,
      }
    }
    if (this.renaming) {
      const form = this.renaming
      const models = [...new Set(this.sets.map((set) => this.model(set)))]
      const follow = form.follow
      return {
        kind: 'rename',
        title: `Rename ${form.model}`,
        source: `~/.apogee/models/${form.model} · ${form.sets} ${form.sets === 1 ? 'set' : 'sets'}`,
        intro: `Its folder moves, and every backend named after it, ${modelBaseName(form.model)}-…, is renamed with it.`,
        steps: [],
        notes: [follow.length > 0 ? `Renamed with it: ${follow.join(', ')}. Roles, suites, saved sessions and the default video pipeline follow the new names.` : 'No backend is named after it, so only the folder and its handles change. Backends running from it keep their names and follow the new path.'],
        errors: form.errors(models),
        command: form.command,
        goLabel: 'Rename',
        canGo: form.canRename(models),
      }
    }
    return undefined
  }

  face(): StoreFace {
    const models = [...new Set(this.sets.map((set) => this.model(set)))]
    const dirs: string[] = []
    for (const set of this.sets) if (!dirs.includes(this.model(set))) dirs.push(this.model(set))
    const folded = this.folded + this.foldedByJobs
    const unregistered = this.unregistered.length
    return {
      summary: `${models.length} ${models.length === 1 ? 'model' : 'models'}, ${this.sets.length} ${this.sets.length === 1 ? 'set' : 'sets'}, in the model store`,
      registerAll: { shown: unregistered > 0, label: `Register the ${unregistered} not registered…` },
      groups: dirs.map((dir) => {
        const mine = this.sets.filter((set) => this.model(set) === dir)
        const sizes = mine.map((set) => this.bytes(set))
        const known = sizes.every((size) => size !== undefined) ? sizes.reduce<number>((sum, size) => sum + (size ?? 0), 0) : undefined
        const pulled = mine.flatMap((set) => this.infos.get(set.handle)?.data?.lineage.filter((step) => step.kind === 'pulled') ?? [])[0]?.from
        return {
          dir,
          origin: pulled === undefined ? '' : `from ${pulled}`,
          summary: `${mine.length} ${mine.length === 1 ? 'set' : 'sets'}${known !== undefined ? ` · ${storeSize(known)}` : ''}`,
          sets: mine.map((set) => {
            const state = this.state(set)
            const bytes = this.bytes(set)
            const from = this.madeFrom(set)
            return {
              handle: set.handle,
              short: this.id(set),
              fromText: from ?? '',
              format: formatName(set.format),
              formatColor: set.format === 'safetensors' ? 'var(--text-secondary)' : 'var(--text-primary)',
              level: set.precision ?? '—',
              size: bytes === undefined ? '—' : storeSize(bytes),
              state: state.text,
              stateColor: state.color,
              dot: state.dot,
              busy: this.busy(set.handle) !== undefined,
              current: this.selected === set.handle,
            }
          }),
        }
      }),
      fold: {
        shown: folded > 0,
        text: this.showingAll
          ? `${folded} SafeTensors ${folded === 1 ? 'set a conversion has used is shown.' : 'sets a conversion has used are shown.'}`
          : `${folded} SafeTensors ${folded === 1 ? 'set a conversion has used is folded away, as models list folds it.' : 'sets a conversion has used are folded away, as models list folds them.'}`,
        button: this.showingAll ? 'Fold them' : 'Show them',
      },
      narration: this.narration ? { shown: true, title: this.narration.title, text: this.narration.lines.join('\n') } : { shown: false, title: '', text: '' },
      failure: this.failure ?? '',
      working: this.working ?? '',
      detail: this.detail(),
    }
  }

  private detail(): StoreDetail {
    const set = this.current
    if (!set) return { shown: false, kicker: '', handle: '', state: '', stateColor: '', dot: '', actions: [], note: '', facts: [], voices: [], parents: [], children: [], dir: '', dirSummary: '', command: '' }
    const info = this.infos.get(set.handle)
    const field = (label: string) => (info ? ModelInfo.field(info, label) : undefined)
    const state = this.state(set)
    const busy = this.busy(set.handle) !== undefined
    const labels: Readonly<Record<StoreAction, readonly [string, 'btn' | 'btn-p' | 'btn-g', string]>> = {
      convertToGGUF: ['Convert to GGUF…', 'btn-p', 'apogee models convert'],
      convertToMLX: ['Convert to MLX…', 'btn', 'apogee models convert --mlx'],
      quantize: ['Quantize…', 'btn-p', 'apogee models quantize'],
      register: ['Register…', 'btn-p', 'apogee config add-backend'],
      useInSession: ['Use in a new session', 'btn-p', ''],
      removeBackend: ['Remove the backend…', 'btn-g', 'apogee config delete-backend'],
      newClip: ['New clip with it', 'btn-p', ''],
      makeDefaultVideo: ['Make it the default', 'btn', 'apogee config set-default-video'],
      openVoice: ['Open Voice', 'btn-p', ''],
      checkFiles: ['Check the files', 'btn-g', 'apogee models repair'],
      showInFinder: ['Show in Finder', 'btn-g', ''],
      delete: ['Delete…', 'btn-g', 'apogee models delete'],
    }
    const actions = this.actions(set)
    // After the first, a second runnable action is the prototype's secondary button.
    const actionFaces = actions.map((action, index) => {
      const [label, kind, title] = labels[action]
      const secondary = index > 0 && (action === 'register' || action === 'useInSession') ? 'btn' : kind
      return { action, label, kind: secondary, title, disabled: busy }
    })
    const bytes = this.bytes(set)
    const window = info?.data?.window
    const runnable = this.isRunnable(set)
    const lineage = field('lineage')?.split('\n')[0]
    const formatWords = field('format') ?? formatName(set.format)
    let note = ''
    if (busy) note = 'A job is working on it; see Jobs.'
    else if (set.format === 'safetensors' && set.consumed) note = `A conversion has used these weights. They stay, so you can convert them again at another precision or to MLX; deleting them frees ${bytes === undefined ? 'its space' : storeSize(bytes)} and keeps what was made.`
    else if (set.format === 'safetensors' && (info?.data?.convertsTo === undefined || info.data.convertsTo.length > 0)) note = 'SafeTensors weights don’t run as they are. Convert them to GGUF for llama.cpp, or to MLX for Apple silicon.'
    else if (runnable && set.backends.length === 0) note = 'Stored but not registered, so no session can use it yet. Registering writes one config entry; nothing is copied or loaded.'
    const parentStep = info?.data?.lineage[0]
    const parent = parentStep && (parentStep.kind === 'converted' || parentStep.kind === 'quantized') ? parentStep.from : undefined
    const parentSet = parent === undefined ? undefined : this.sets.find((candidate) => candidate.handle === parent)
    const kids = this.sets.filter((candidate) => this.infos.get(candidate.handle)?.data?.lineage[0]?.from === set.handle)
    const dirSets = this.sets.filter((candidate) => this.model(candidate) === this.model(set))
    const dirBackends = dirSets.flatMap((candidate) => candidate.backends)
    const dirBytes = dirSets.map((candidate) => this.bytes(candidate))
    const dirSize = dirBytes.every((size) => size !== undefined) ? `, ${storeSize(dirBytes.reduce<number>((sum, size) => sum + (size ?? 0), 0))}` : ''
    const voices = (field('voices') ?? '')
      .split('\n')
      .filter((line) => line.includes(': '))
      .map((line) => {
        const [lang = '', names = ''] = line.split(': ')
        const list = names.split(', ').filter((name) => name !== '')
        return { lang, count: String(list.length), names: list.join(' ') }
      })
    return {
      shown: true,
      kicker: [formatName(set.format).toUpperCase(), ...(set.precision ? [set.precision] : [])].join(' · '),
      handle: set.handle,
      state: state.text,
      stateColor: state.color,
      dot: state.dot,
      actions: actionFaces,
      note,
      facts: [
        { label: 'Handle', value: set.handle, mono: true },
        { label: 'Path', value: field('model_path') ?? field('path') ?? `~/.apogee/models/${set.handle}`, mono: true },
        { label: 'Format', value: formatWords, mono: false },
        { label: 'Precision', value: set.precision ?? field('quantization') ?? '—', mono: false },
        { label: 'On disk', value: bytes === undefined ? (field('size') ?? '—') : storeSize(bytes), mono: false },
        { label: 'Architecture', value: info?.data?.architecture ?? field('architecture') ?? 'read when it was made', mono: info?.data?.architecture !== undefined },
        { label: 'Window', value: window ? `${window.tokens.toLocaleString('en-US')} tokens here${window.trained !== undefined ? ` · trained ${window.trained.toLocaleString('en-US')}` : ''}` : set.format === 'safetensors' ? 'none until it is converted' : '—', mono: false },
        { label: 'Record', value: field('record') ?? field('files') ?? field('header') ?? '—', mono: false },
        { label: 'Came from', value: lineage ?? 'unknown', mono: false },
        { label: 'Backend', value: runnable ? (set.backends.length > 0 ? set.backends.join(', ') : 'not registered') : '—', mono: set.backends.length > 0 },
      ],
      voices,
      parents: parent === undefined ? [] : [{ label: parent, isLink: parentSet !== undefined, how: parentSet ? `${parentStep?.kind === 'quantized' ? 'quantized from it' : 'converted from it'} · ${formatName(parentSet.format)}${parentSet.precision ? ` ${parentSet.precision}` : ''}` : 'no longer in the store' }],
      children: kids.map((kid) => {
        const kidBytes = this.bytes(kid)
        const how = this.infos.get(kid.handle)?.data?.lineage[0]?.kind === 'quantized' ? 'quantized to' : 'converted to'
        return { label: kid.handle, how: [`${how} ${formatName(kid.format)}${kid.precision ? ` ${kid.precision}` : ''}`, ...(kidBytes !== undefined ? [storeSize(kidBytes)] : []), ...(kid.backends.length > 0 ? [`backend ${kid.backends.join(', ')}`] : [])].join(' · ') }
      }),
      dir: this.model(set),
      dirSummary: `${dirSets.length} ${dirSets.length === 1 ? 'set' : 'sets'}${dirSize}${dirBackends.length > 0 ? `. Backends running from it: ${dirBackends.join(', ')}.` : '. No backend runs from it.'}`,
      command: `apogee models info ${set.handle} --output-format json`,
    }
  }
}

/** Strings compared as Swift compares a tuple of them: the first that differs decides, by code unit. */
function compareKeys(a: readonly string[], b: readonly string[]): number {
  for (let index = 0; index < a.length; index += 1) {
    const left = a[index] ?? ''
    const right = b[index] ?? ''
    if (left !== right) return left < right ? -1 : 1
  }
  return 0
}

/** One row of the table, as its view draws it. */
export interface StoreRow {
  readonly handle: string
  readonly short: string
  readonly fromText: string
  readonly format: string
  readonly formatColor: string
  readonly level: string
  readonly size: string
  readonly state: string
  readonly stateColor: string
  readonly dot: string
  readonly busy: boolean
  readonly current: boolean
}

export interface StoreDetail {
  readonly shown: boolean
  readonly kicker: string
  readonly handle: string
  readonly state: string
  readonly stateColor: string
  readonly dot: string
  readonly actions: ReadonlyArray<{ readonly action: StoreAction; readonly label: string; readonly kind: 'btn' | 'btn-p' | 'btn-g'; readonly title: string; readonly disabled: boolean }>
  readonly note: string
  readonly facts: ReadonlyArray<{ readonly label: string; readonly value: string; readonly mono: boolean }>
  readonly voices: ReadonlyArray<{ readonly lang: string; readonly count: string; readonly names: string }>
  readonly parents: ReadonlyArray<{ readonly label: string; readonly isLink: boolean; readonly how: string }>
  readonly children: ReadonlyArray<{ readonly label: string; readonly how: string }>
  readonly dir: string
  readonly dirSummary: string
  readonly command: string
}

/** Everything Stored weights draws, as words, absence said explicitly. */
export interface StoreFace {
  readonly summary: string
  readonly registerAll: { readonly shown: boolean; readonly label: string }
  readonly groups: ReadonlyArray<{ readonly dir: string; readonly origin: string; readonly summary: string; readonly sets: readonly StoreRow[] }>
  readonly fold: { readonly shown: boolean; readonly text: string; readonly button: string }
  readonly narration: { readonly shown: boolean; readonly title: string; readonly text: string }
  readonly failure: string
  readonly working: string
  readonly detail: StoreDetail
}
