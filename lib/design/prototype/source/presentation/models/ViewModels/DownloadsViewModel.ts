import { JobProgress } from '../../../business/jobs/JobProgress'
import { JobEnd, JobUpdate } from '../../../business/jobs/SupervisedJob'
import type { JobRunning, JobStarting } from '../../../business/jobs/SupervisedJob'
import { CommandOutcome } from '../../../business/reads/CommandRunning'
import type { CommandRunning } from '../../../business/reads/CommandRunning'
import { MLXKind, ModelInfo, ModelRow } from '../../../business/reads/Models'
import type { ModelInfoDocument } from '../../../business/reads/Models'
import { ReadOutcome } from '../../../business/reads/ReadRunner'
import { ReadDocument } from '../../../data/protocol/ReadDocument'
import type { Clock } from '../../../infrastructure/clock/Clock'
import { BoundedTail } from '../../../infrastructure/process/BoundedTail'
import { Observable } from '../../styles/Observation/Observable'
import { modelBaseName, storeSize } from './StoreForms'
import type { ConvertForm, QuantizeForm, StoreStep } from './StoreForms'
import type { Landed } from './StoreViewModel'

/** What a job is. */
export type DownloadKind = 'pull' | 'convert' | 'quantize' | 'check'

/**
 * The download sheet's form (design 1h): the prototype's -- a repository,
 * GGUF or MLX, the level -- as the CLI's one pull takes it. A GGUF is
 * `--safetensors --register-with <level>` (`--register` for F16), the CLI's
 * chain; the CLI's pull makes no MLX model, so MLX takes the snapshot alone,
 * to convert under Stored weights once it lands.
 */
export class DownloadForm {
  static readonly examples = ['Qwen/Qwen3-8B', 'Wan-AI/Wan2.1-T2V-1.3B-Diffusers', 'hexgrad/Kokoro-82M']
  static readonly levels = ['Q4_K_M', 'Q5_K_M', 'Q8_0', 'F16']

  format: 'gguf' | 'mlx' = 'gguf'
  quant = 'Q4_K_M'

  constructor(public repo: string) {}

  setFormat(format: 'gguf' | 'mlx'): void {
    this.format = format
    if (format === 'gguf') this.quant = 'Q4_K_M'
  }

  get reference(): string {
    return this.repo.trim()
  }

  get canStart(): boolean {
    const ref = this.reference
    return ref.indexOf('/') > 0 && !ref.includes(' ')
  }

  get arguments(): string[] {
    const pull = ['models', 'pull', this.reference, '--safetensors']
    if (this.format === 'mlx') return pull
    return this.quant === 'F16' ? [...pull, '--register'] : [...pull, '--register-with', this.quant]
  }

  /** The steps as the sheet lists them: the prototype's words. */
  get steps(): string[] {
    if (this.format === 'mlx') return ['Download the snapshot', 'Add it to the library']
    if (this.quant === 'F16') return ['Download the snapshot', 'Convert to GGUF', 'Add it to the library']
    return ['Download the snapshot', 'Convert to GGUF F16', `Quantize to ${this.quant}`, 'Add it to the library']
  }

  /** What the sheet says of the repository before anything is fetched: the CLI states nothing of one before its pull. */
  get facts(): string {
    return this.canStart ? 'Size and license unknown until Apogee checks the repository.' : 'Use the owner/name form, e.g. Qwen/Qwen3-8B.'
  }

  get summary(): string {
    return this.format === 'mlx' ? 'The pull takes the snapshot; convert it to MLX under Stored weights once it lands. Apogee shows the sizes before anything downloads.' : 'Apogee shows the sizes before anything downloads.'
  }

  /** The card's target: the model's name and what it becomes. */
  get target(): string {
    const name = this.reference.split('/').at(-1) ?? this.reference
    return this.format === 'mlx' ? `${name} · snapshot` : `${name} · ${this.quant}`
  }
}

/** One run, and what it has said: the Mac's `DownloadsViewModel.Download`. */
export class Download {
  readonly progress = new JobProgress()
  /** The sets a conversion's own run named as it stored them, by handle, in order. */
  named: string[] = []
  /** The set a quantization's own run says it made. */
  made: string | undefined = undefined
  /** The documents of the sets its run named, by handle. */
  readonly infos = new Map<string, ModelInfoDocument>()
  /** The last word on it, in the CLI's words: a check's result, a registration, why nothing ran. */
  outcome: string | undefined = undefined
  /** The tick it ended on. */
  endedAt: number | undefined = undefined
  logOpen = false
  job: JobRunning | undefined = undefined
  stoppedBeforeRun = false
  /** The store's names before it first ran; `undefined` until read. */
  before: Set<string> | undefined = undefined
  /** The backend the app registered after its run: a quantization's. */
  registeredAfter: string | undefined = undefined
  /** Settled once its run has started, or it ended before one could: what a preset waits on before its clock starts. */
  readonly ready: Promise<void>
  settle: () => void = () => {}

  constructor(
    readonly id: number,
    /** What a person would type after `apogee`. */
    readonly args: readonly string[],
    readonly reference: string,
    readonly kind: DownloadKind,
    /** Its steps, as its sheet listed them: a pull's planned ones, a store job's own. */
    readonly steps: readonly StoreStep[],
    readonly target: string,
    readonly registerAfter: QuantizeForm | undefined = undefined,
    /** The model folder its command works in, where what it makes lands; `undefined` for a pull. */
    readonly folder: string | undefined = undefined,
  ) {
    this.ready = new Promise((resolve) => (this.settle = resolve))
  }

  get isRunning(): boolean {
    return this.progress.status.case === 'running'
  }

  get command(): string {
    return ['apogee', ...this.args].join(' ')
  }

  /** The backends its own run registered: those its words name, and the one the app registered after it. */
  get ownBackends(): string[] {
    const names = this.progress.notices.flatMap((line) => DownloadsViewModel.registeredName(line) ?? [])
    return this.registeredAfter === undefined ? names : [...names, this.registeredAfter]
  }

  /** Whether step `index` of a store job is done: a step that makes a set once its run has named as many; the registration once it ended well. */
  isDone(index: number): boolean {
    const step = this.steps[index]
    if (!step) return false
    if (!step.makesSet) return this.progress.status.case === 'done' && (this.registerAfter === undefined || this.registeredAfter !== undefined)
    if (this.kind === 'quantize') return this.made !== undefined
    if (this.kind === 'convert') return this.steps.slice(0, index).filter((earlier) => earlier.makesSet).length < this.named.length
    return false
  }
}

/** One step of a card, as its view draws it. */
export interface StepFace {
  readonly n: string
  readonly label: string
  readonly state: 'waiting' | 'running' | 'done' | 'stopped' | 'failed'
  readonly pct: number
  readonly pulse: boolean
  readonly barColor: string
  readonly labelColor: string
  readonly detail: string
  readonly detailColor: string
}

/** One job's card, as its view draws it: the prototype's. */
export interface CardFace {
  readonly id: number
  readonly repo: string
  readonly target: string
  readonly status: string
  readonly statusColor: string
  readonly running: boolean
  readonly steps: readonly StepFace[]
  readonly outcome: string
  /** The buttons it offers, in order: Stop; Resume from …; Show the weights; Run again and Dismiss. */
  readonly buttons: readonly string[]
  readonly resume: string
  readonly openLabel: string
  readonly logOpen: boolean
  readonly log: string
}

/**
 * The Jobs tab (design 1h): a pull run as the supervised job it is -- its
 * steps, its bytes, Stop -- and the store's long runs: a conversion, a
 * quantization, a check of the files. The Mac app's `DownloadsViewModel`,
 * drawn as the prototype's cards (`main.html` at `b375fac9`, `isJobs`).
 *
 * A pull's progress is the 52b stream; a store job's steps tick on the sets
 * its own run names as it stores them -- a conversion's each as its output
 * prints it, a quantization's once it ended well -- each placed by `models
 * info` (GUI 4e, 6d), never the store's difference and never the clock. The
 * store is read before a store job runs, and a Stop pressed while that read
 * runs ends the card with nothing started (GUI 6d). What a job made lands in
 * Stored weights from what it reported (`landed`): a pull's `document`
 * events, a store job's named sets, each with the backend its run registered
 * on it, in the order the CLI names both.
 */
export class DownloadsViewModel extends Observable {
  static readonly stoppedBeforeRun = 'Stopped before it ran: nothing ran'

  downloads: Download[] = []
  /** Told when a job ended well: the sets it made, for Stored weights. */
  landed: ((sets: readonly Landed[]) => void) | undefined = undefined
  /** Opens a set under Stored weights: Show the weights. */
  showWeights: ((handle: string | undefined) => void) | undefined = undefined
  /** Says one line in the window's toast: a job that ended well, in the old prototype's words. */
  say: ((text: string) => void) | undefined = undefined

  private binary: string | undefined = undefined
  private nextID = 0

  constructor(
    private readonly starter: JobStarting,
    private readonly reader: CommandRunning,
    private readonly clock: Pick<Clock, 'ticks' | 'now'>,
  ) {
    super()
  }

  connect(binary: string | undefined): void {
    this.binary = binary
  }

  /** How many jobs run now: the Jobs tab's count. */
  get running(): number {
    return this.downloads.filter((download) => download.isRunning).length
  }

  /** The job working on a set, by its handle: what Stored weights says of it. */
  busyOn(handle: string): DownloadKind | undefined {
    return this.downloads.find((download) => download.isRunning && download.kind !== 'pull' && download.reference === handle)?.kind
  }

  // MARK: - Starting

  /** Whether a job can start: a binary was found (the Mac's `canStart`). With none, nothing is added and nothing is said to run. */
  get canStart(): boolean {
    return this.binary !== undefined
  }

  /** The download sheet's pull: its chain's stream, as the CLI's 52b says it; `undefined` with no binary, no card added. */
  pull(form: DownloadForm): Download | undefined {
    const steps = form.steps.map((label) => ({ label, estimate: undefined, makesSet: false }))
    return this.run([...form.arguments, '--output-format', 'stream-json'], form.reference, steps, form.target)
  }

  private run(args: readonly string[], reference: string, steps: readonly StoreStep[], target: string): Download | undefined {
    // The Mac's guard: no binary, no card -- a card would say a job runs that nothing started.
    const binary = this.binary
    if (binary === undefined) return undefined
    const download = this.add(new Download(this.next(), args, reference, 'pull', steps, target))
    download.settle()
    const streaming = args.includes('stream-json')
    const job = this.starter.startJob(binary, args, streaming)
    download.job = job
    void this.follow(download, job)
    return download
  }

  /** `models convert …`, the convert sheet's command as typed. */
  convert(form: ConvertForm): Download | undefined {
    const names = form.names
    const target = `${form.mlx ? 'MLX' : 'GGUF'} ${[form.mlx ? form.written : form.written.toUpperCase(), ...form.levels.filter((level) => form.levelChoices.includes(level))].join(', ')}${names.length > 0 ? ', registered' : ''}`
    return this.startStore(new Download(this.next(), form.arguments, form.handle, 'convert', form.steps, target, undefined, DownloadsViewModel.folder(form.handle)))
  }

  /** `models quantize …`, then -- asked -- the `config add-backend` that registers what it made. */
  quantize(form: QuantizeForm): Download | undefined {
    const target = `${form.level}${form.register ? `, registered as ${form.name.trim()}` : ''}`
    return this.startStore(new Download(this.next(), form.arguments, form.handle, 'quantize', form.steps, target, form.register ? form : undefined, DownloadsViewModel.folder(form.handle)))
  }

  /** `models repair <handle>`: the files checked against their record, as a job. */
  check(handle: string): Download | undefined {
    return this.startStore(new Download(this.next(), ['models', 'repair', handle], handle, 'check', [], 'checking its files', undefined, DownloadsViewModel.folder(handle)))
  }

  private next(): number {
    this.nextID += 1
    return this.nextID
  }

  private add(download: Download): Download {
    this.downloads = [download, ...this.downloads]
    this.changed()
    return download
  }

  /** A store job: the store read first -- what is there before it runs (GUI 4e) -- then its run, unless Stop came first. With no binary, no card. */
  private startStore(download: Download): Download | undefined {
    const binary = this.binary
    if (binary === undefined) return undefined
    this.add(download)
    if (download.before !== undefined) {
      this.launch(download, binary)
      return download
    }
    void (async () => {
      const outcome = await this.reader.read(binary, ['models', 'list', '--all'])
      if (download.stoppedBeforeRun) {
        download.settle()
        return
      }
      const document = ReadOutcome.documentOf(outcome)
      if (!document) {
        download.progress.apply(JobUpdate.ended(JobEnd.notStarted(`Couldn’t read the store first, so nothing ran -- apogee models list --all: ${ReadOutcome.failure(outcome) ?? 'no document'}`)))
        download.endedAt = this.clock.ticks
        download.settle()
        this.changed()
        return
      }
      download.before = new Set(ModelRow.list(document).map((row) => row.name))
      this.launch(download, binary)
    })()
    return download
  }

  private launch(download: Download, binary: string): void {
    const job = this.starter.startJob(binary, download.args, false)
    download.job = job
    download.settle()
    this.changed()
    void this.followStore(download, job)
  }

  private async follow(download: Download, job: JobRunning): Promise<void> {
    for await (const update of job.updates) {
      download.progress.apply(update)
      this.changed()
    }
    download.job = undefined
    download.endedAt = this.clock.ticks
    if (download.progress.status.case === 'done') {
      const sets = this.pulled(download)
      this.landed?.(sets)
      this.say?.(DownloadsViewModel.pulledWords(download, sets))
    }
    this.changed()
  }

  /**
   * A pull's done toast, the old prototype's (`<name> · <quant> is ready in
   * the library`, `main.html:2891` at `b375fac9`): the model the folder its
   * `document` events name, the level the newest quantized set's lineage
   * states, else the card's own (F16, the snapshot).
   */
  static pulledWords(download: Download, sets: readonly Landed[]): string {
    const handle = sets.map((set) => set.info.data?.handle).find((name) => name !== undefined)
    const model = (handle === undefined ? undefined : DownloadsViewModel.folder(handle)) ?? download.reference
    const quantized = sets
      .map((set) => set.info.data?.lineage[0])
      .filter((step) => step?.kind === 'quantized' && (step.detail ?? '') !== '')
      .at(-1)?.detail
    return `${model} · ${quantized ?? download.target.split(' · ').at(-1) ?? download.target} is ready in the library`
  }

  /** What a pull stored: one `document` a set, in the order stored, each runnable one with the backend its chain registered on it, in its order. */
  private pulled(download: Download): Landed[] {
    const infos = download.progress.documents.flatMap((value) => {
      const document = ReadDocument.from(JSON.stringify(value))
      return document ? [ModelInfo.read(document)] : []
    })
    return DownloadsViewModel.paired(infos, download.ownBackends)
  }

  /** Sets and the backends a run registered, paired in the order the CLI names both: each runnable set, the next name. */
  static paired(infos: readonly ModelInfoDocument[], names: readonly string[]): Landed[] {
    const queue = [...names]
    return infos.map((info) => {
      const format = info.data?.format
      const runnable = format === 'gguf' || (format === 'mlx' && MLXKind.registers(MLXKind.reading(info.data?.kind)))
      return { info, backend: runnable ? queue.shift() : undefined }
    })
  }

  private async followStore(download: Download, job: JobRunning): Promise<void> {
    for await (const update of job.updates) {
      download.progress.apply(update)
      if (update.case === 'said' && download.kind === 'convert') {
        const path = DownloadsViewModel.storedPath(update.line)
        if (path !== undefined) await this.name(download, path)
      }
      this.changed()
    }
    download.job = undefined
    if (download.progress.status.case === 'done') {
      // Each kind's done toast is the old prototype's (`doneToast`, `main.html:3707-3725` at `b375fac9`), its words what the run reported.
      const model = download.folder ?? download.reference
      if (download.kind === 'check') {
        download.outcome = download.progress.notices.at(-1)
        if (download.outcome !== undefined) this.say?.(`${download.reference}: ${download.outcome}`)
      }
      if (download.kind === 'quantize') {
        const made = await this.identify(download)
        if (download.registerAfter) await this.register(download, download.registerAfter, made)
        const info = made.case === 'listed' ? made.info : undefined
        if (info) {
          this.landed?.([{ info, backend: download.registeredAfter }])
          const step = info.data?.lineage[0]
          this.say?.(`${model} · ${step?.kind === 'quantized' && step.detail ? step.detail : (download.steps[0]?.level ?? '')} is ready in the library`)
        }
      }
      if (download.kind === 'convert') {
        this.landed?.(DownloadsViewModel.paired(download.named.flatMap((handle) => download.infos.get(handle) ?? []), download.ownBackends))
        const made = download.named.length
        const registered = download.ownBackends.length
        this.say?.(`${model}: ${made} ${made === 1 ? 'set' : 'sets'} made${registered > 0 ? `, ${registered} registered` : ''}`)
      }
    }
    download.endedAt = this.clock.ticks
    this.changed()
  }

  /** Takes a set a conversion's own run named: `models info <path>` answers with its handle -- a GGUF or MLX set in the job's own folder that is not its source -- each once. */
  private async name(download: Download, path: string): Promise<void> {
    const info = await this.infoAt(path)
    const handle = info?.data?.handle
    const format = info?.data?.format ?? ''
    if (!info || handle === undefined || !['gguf', 'mlx'].includes(format) || handle === download.reference) return
    if (download.folder !== undefined && DownloadsViewModel.folder(handle) !== download.folder) return
    if (download.named.includes(handle)) return
    download.named.push(handle)
    download.infos.set(handle, info)
  }

  private async infoAt(path: string): Promise<ModelInfoDocument | undefined> {
    const binary = this.binary
    if (binary === undefined) return undefined
    const document = ReadOutcome.documentOf(await this.reader.read(binary, ['models', 'info', path]))
    return document ? ModelInfo.read(document) : undefined
  }

  /** What a quantization made, by its own run: the GGUF it printed, placed by `models info`, in the job's own folder. */
  private async identify(download: Download): Promise<{ readonly case: 'listed'; readonly info: ModelInfoDocument } | { readonly case: 'unlisted'; readonly path: string } | { readonly case: 'unnamed' }> {
    const path = DownloadsViewModel.madePath(download.progress.notices)
    if (path === undefined) return { case: 'unnamed' }
    const info = await this.infoAt(path)
    const handle = info?.data?.handle
    if (!info || handle === undefined || info.data?.format !== 'gguf' || (download.folder !== undefined && DownloadsViewModel.folder(handle) !== download.folder)) return { case: 'unlisted', path }
    download.made = handle
    download.infos.set(handle, info)
    return { case: 'listed', info }
  }

  /** Registers the GGUF a quantization made, as its own run named it; a refusal leaves it made and is said in the CLI's words. */
  private async register(download: Download, form: QuantizeForm, made: Awaited<ReturnType<DownloadsViewModel['identify']>>): Promise<void> {
    if (made.case === 'unlisted') {
      download.outcome = `Not registered: the store doesn’t list ${made.path}, which its run named, as a GGUF of ${download.folder ?? 'its model'}.`
      return
    }
    if (made.case === 'unnamed') {
      download.outcome = 'Not registered: its output names no GGUF it made.'
      return
    }
    const path = ModelInfo.field(made.info, 'model_path')
    const args = path === undefined ? undefined : form.registration(path)
    const binary = this.binary
    if (!args || binary === undefined) {
      download.outcome = `Not registered: the store names no file for ${download.made ?? 'what it made'}.`
      return
    }
    const outcome = await this.reader.command(binary, args)
    if (outcome.case !== 'done') {
      download.outcome = `The GGUF is made; registering it was refused: ${CommandOutcome.failure(outcome) ?? ''}`
      return
    }
    download.registeredAfter = args[2]
    download.outcome = outcome.stdout.split('\n')[0]
  }

  // MARK: - Stopping and again

  /** Stops a run as Ctrl-C does; what it finished stands. A store job still reading the store ends stopped at once, and the read's answer starts nothing. */
  async stop(download: Download): Promise<void> {
    if (download.job) {
      await download.job.stop()
      return
    }
    if (!download.isRunning || download.kind === 'pull') return
    download.stoppedBeforeRun = true
    download.progress.apply(JobUpdate.ended(JobEnd.of(undefined, new BoundedTail(), true, DownloadsViewModel.stoppedBeforeRun)))
    download.outcome = DownloadsViewModel.stoppedBeforeRun
    download.endedAt = this.clock.ticks
    download.settle()
    this.changed()
  }

  /** Runs the same job again, as itself: its kind, its steps, its registration; its card replaced. */
  again(download: Download): void {
    if (download.isRunning || this.binary === undefined) return
    this.downloads = this.downloads.filter((candidate) => candidate.id !== download.id)
    if (download.kind === 'pull') {
      this.run(download.args, download.reference, download.steps, download.target)
      return
    }
    this.startStore(new Download(this.next(), download.args, download.reference, download.kind, download.steps, download.target, download.registerAfter, download.folder))
  }

  /**
   * Runs the command the CLI named for carrying a stopped or failed run on.
   * A store verb it names -- a pull's chain names `models convert <the
   * snapshot> …` once its snapshot is stored -- runs as the store job it is,
   * its store read first and its sets placed as its own run names them, so
   * what it makes lands; anything else runs as the pull did.
   */
  resume(download: Download): void {
    const command = download.progress.resumeCommand
    if (download.isRunning || !command) return
    const [noun, verb, handle] = command
    if (noun === 'models' && (verb === 'convert' || verb === 'quantize') && handle !== undefined) {
      this.startStore(new Download(this.next(), command, handle, verb, [], download.target, undefined, DownloadsViewModel.folder(handle)))
      return
    }
    this.run(command, download.reference, [], download.target)
  }

  dismiss(download: Download): void {
    if (download.isRunning) return
    this.downloads = this.downloads.filter((candidate) => candidate.id !== download.id)
    this.changed()
  }

  toggleLog(id: number): void {
    const download = this.downloads.find((candidate) => candidate.id === id)
    if (download) download.logOpen = !download.logOpen
    this.changed()
  }

  // MARK: - Reading what a run said

  /** The backend a run's line says it registered: `registered <name>` or `already registered: <name>`, a name being one word. */
  static registeredName(line: string): string | undefined {
    const trimmed = line.trim()
    for (const opening of ['already registered: ', 'registered ']) {
      if (!trimmed.startsWith(opening)) continue
      const name = trimmed.slice(opening.length)
      return name === '' || name.includes(' ') ? undefined : name
    }
    return undefined
  }

  /** The model folder a store handle names -- `<model>/<format>/<id>`'s first part -- or `undefined` for a name that is no handle. */
  static folder(name: string): string | undefined {
    const parts = name.split('/')
    return parts.length === 3 ? parts[0] : undefined
  }

  /** The stored GGUF a quantization's output names: the first line that is a path to a `.gguf` file. */
  static madePath(said: readonly string[]): string | undefined {
    return said.map((line) => line.trim()).find((line) => line.startsWith('/') && line.endsWith('.gguf'))
  }

  /** A set a store verb names as it stores it: a line that is an absolute path alone -- a GGUF, never its projector, or an MLX directory. */
  static storedPath(line: string): string | undefined {
    const path = line.trim()
    if (!path.startsWith('/')) return undefined
    if (path.endsWith('.gguf')) return path.endsWith('-mmproj.gguf') ? undefined : path
    return path.includes('/mlx/') ? path : undefined
  }

  // MARK: - What the views draw

  face(): { readonly cards: readonly CardFace[]; readonly running: number } {
    return { cards: this.downloads.map((download) => this.card(download)), running: this.running }
  }

  private time(tick: number): string {
    const at = new Date(this.clock.now.getTime() + tick * 400)
    return `${String(at.getUTCHours()).padStart(2, '0')}:${String(at.getUTCMinutes()).padStart(2, '0')}`
  }

  private card(download: Download): CardFace {
    const status = download.progress.status
    const steps = download.kind === 'pull' ? this.pullSteps(download) : this.storeSteps(download)
    const current = steps.findIndex((step) => step.state === 'running')
    const unfinished = steps.find((step) => step.state !== 'done')
    const word = (unfinished?.label.split(' ')[0] ?? '').toLowerCase()
    const resume = !download.isRunning && download.progress.resumeCommand !== undefined && !download.stoppedBeforeRun ? `Resume from ${word || 'here'}` : ''
    let text: string
    let color: string
    switch (status.case) {
      case 'running':
        text = current >= 0 ? `running · step ${current + 1} of ${steps.length}` : 'running'
        color = 'var(--status-accent)'
        break
      case 'done':
        text = `done · ${this.time(download.endedAt ?? this.clock.ticks)}`
        color = 'var(--text-secondary)'
        break
      case 'stopped':
        text = download.stoppedBeforeRun ? 'stopped' : `stopped at ${word || 'its start'}`
        color = 'var(--status-warning)'
        break
      case 'failed':
        text = 'failed'
        color = 'var(--status-error)'
        break
    }
    const buttons: string[] = []
    if (download.isRunning) buttons.push('Stop')
    else if (status.case === 'done') buttons.push('Show the weights')
    else if (resume !== '') buttons.push(resume)
    else buttons.push('Run again', 'Dismiss')
    const outcome = download.outcome ?? (status.case === 'failed' ? status.message : '')
    return {
      id: download.id,
      repo: download.reference,
      target: download.target,
      status: text,
      statusColor: color,
      running: download.isRunning,
      steps,
      outcome,
      buttons,
      resume,
      openLabel: 'Show the weights',
      logOpen: download.logOpen,
      log: [download.command, ...download.progress.notices].join('\n'),
    }
  }

  /** The first set a job made, for Show the weights: a pull's or a conversion's first, a quantization's own. */
  firstLanded(download: Download): string | undefined {
    if (download.kind === 'quantize') return download.made
    if (download.kind === 'convert') return download.named[0]
    if (download.kind === 'pull') return this.pulled(download).find((set) => set.backend !== undefined)?.info.data?.handle ?? this.pulled(download)[0]?.info.data?.handle
    return download.reference
  }

  private stepFace(n: number, label: string, state: StepFace['state'], detail: string): StepFace {
    const open = state === 'running' || state === 'stopped' || state === 'failed'
    return {
      n: String(n),
      label,
      state,
      pct: state === 'done' || open ? 100 : 0,
      pulse: state === 'running',
      barColor: state === 'stopped' || state === 'failed' ? 'var(--status-warning)' : state === 'done' ? 'var(--text-secondary)' : 'var(--status-accent)',
      labelColor: state === 'waiting' ? 'var(--text-tertiary)' : 'var(--text-primary)',
      detail,
      detailColor: state === 'stopped' || state === 'failed' ? 'var(--status-warning)' : 'var(--text-tertiary)',
    }
  }

  /** A pull's steps: the stream's stages, each its word; one not yet begun is the sheet's planned step. */
  private pullSteps(download: Download): StepFace[] {
    const progress = download.progress
    const count = progress.stageCount ?? download.steps.length
    const out: StepFace[] = []
    for (let index = 1; index <= count; index += 1) {
      const stage = progress.stages.get(index)
      const label = stage?.label ?? download.steps[index - 1]?.label ?? `step ${index}`
      if (!stage) {
        out.push(this.stepFace(index, label, 'waiting', ''))
        continue
      }
      const state: StepFace['state'] = stage.state === 'done' ? 'done' : stage.state === 'stopped' ? 'stopped' : stage.state === 'failed' ? 'failed' : 'running'
      let detail = ''
      const bytes = progress.bytes
      if (state === 'running' && bytes) detail = `${bytes.index !== undefined && bytes.count !== undefined ? `file ${bytes.index} of ${bytes.count} · ` : ''}${storeSize(bytes.done)}${bytes.total !== undefined ? ` of ${storeSize(bytes.total)}` : ''}`
      else if (state === 'running') detail = 'working'
      else if (state === 'stopped') detail = 'stopped by you · runs again from its start'
      out.push(this.stepFace(index, label, state, detail))
    }
    return out
  }

  /** A store job's steps, as its sheet listed them: each done once its run named its set, the registration once it ended well. */
  private storeSteps(download: Download): StepFace[] {
    const status = download.progress.status.case
    const firstOpen = download.steps.findIndex((_, index) => !download.isDone(index))
    return download.steps.map((step, index) => {
      // A conversion's registration reads, on its card, as the prototype's: "Register 2 backends", the names once done.
      const names = step.names ?? []
      const label = !step.makesSet && download.kind === 'convert' ? `Register ${names.length} ${names.length === 1 ? 'backend' : 'backends'}` : step.label
      if (download.isDone(index)) {
        const handle = download.kind === 'quantize' ? download.made : download.named[download.steps.slice(0, index).filter((earlier) => earlier.makesSet).length]
        const bytes = handle === undefined ? undefined : download.infos.get(handle)?.data?.bytes
        const detail = step.makesSet ? [...(step.level ? [step.level] : []), ...(bytes !== undefined ? [storeSize(bytes)] : [])].join(' · ') : download.kind === 'quantize' ? names.map((name) => `backend ${name}`).join(', ') : names.join(', ')
        return this.stepFace(index + 1, label, 'done', detail)
      }
      if (index !== firstOpen) return this.stepFace(index + 1, label, 'waiting', '')
      if (status === 'running') return this.stepFace(index + 1, label, download.job ? 'running' : 'waiting', download.job ? 'working' : '')
      if (status === 'stopped') return this.stepFace(index + 1, label, download.stoppedBeforeRun ? 'waiting' : 'stopped', download.stoppedBeforeRun ? '' : 'stopped by you · runs again from its start')
      if (status === 'failed') return this.stepFace(index + 1, label, 'failed', '')
      return this.stepFace(index + 1, label, 'waiting', '')
    })
  }
}

export { modelBaseName }
