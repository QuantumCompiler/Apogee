import { JobProgress } from '../../../business/jobs/JobProgress'
import type { JobRunning, JobStarting } from '../../../business/jobs/SupervisedJob'
import type { CommandRunning } from '../../../business/reads/CommandRunning'
import { CommandOutcome } from '../../../business/reads/CommandRunning'
import { ReadOutcome } from '../../../business/reads/ReadRunner'
import { CycleHistory, PipelineRun, PipelineRunRow, PipelineSpec, TrainingDataset, TrainingKit, TrainingRegime, TrainingRun, TrainingStatus, VersionLedger } from '../../../business/reads/Training'
import type { CycleHeadline, PipelineStage } from '../../../business/reads/Training'
import { Observable } from '../../styles/Observation/Observable'
import { PromoteForm, PulledDataset } from './TrainingForms'
import type { PromoteChange, RefusedPromotion } from './TrainingForms'
import type { CycleFace, DatasetsFace, PageFace, PipelinesFace, PromoteSheetFace, RegimesFace, RunsFace, StageRowFace, TrainingFace } from './TrainingFaces'
import { TrainingWords } from './TrainingWords'

export { PromoteForm, PulledDataset } from './TrainingForms'
export type { PromoteChange, RefusedPromotion } from './TrainingForms'

/** The place's five tabs, in the Mac's order and the old prototype's words (TrainingViewModel.swift:48-53). */
export type TrainingTab = 'runs' | 'pipelines' | 'cycle' | 'regimes' | 'datasets'
export const trainingTabs: readonly (readonly [TrainingTab, string])[] = [
  ['runs', 'Runs'],
  ['pipelines', 'Pipelines'],
  ['cycle', 'Cycle'],
  ['regimes', 'Regimes'],
  ['datasets', 'Datasets'],
]

/** The place's sheets (the shell's sheet host frames them): the promote and the pull sheets. */
export type TrainingSheet = 'promote' | 'pull'

/** The place's confirms: the five `train` acts, and a promotion the CLI refused. */
export type TrainingConfirm = 'train' | 'promoteRefused'
export const trainingConfirms: readonly TrainingConfirm[] = ['train', 'promoteRefused']

/**
 * An act waiting on the person's word -- one that trains, or changes what the
 * person's scheduler does: the Mac's `Pending`, the old `train` confirm's
 * five acts. A rollback is the prototype's at once (its default); it never
 * waits here.
 */
export type Pending =
  | { readonly kind: 'runPipeline'; readonly name: string }
  | { readonly kind: 'resumePipeline'; readonly id: string; readonly pipeline: string | undefined }
  | { readonly kind: 'runCycle' }
  | { readonly kind: 'haltCycle' }
  | { readonly kind: 'resumeCycle' }

export const Pending = {
  /** The command as a person types it, after `apogee`. */
  arguments(pending: Pending): string[] {
    switch (pending.kind) {
      case 'runPipeline':
        return ['train', 'pipeline', 'run', '--pipeline', pending.name]
      case 'resumePipeline':
        return ['train', 'pipeline', 'resume', pending.id, ...(pending.pipeline === undefined ? [] : ['--pipeline', pending.pipeline])]
      case 'runCycle':
        return ['train', 'cycle', 'run']
      case 'haltCycle':
        return ['train', 'cycle', 'halt']
      case 'resumeCycle':
        return ['train', 'cycle', 'resume']
    }
  },
}

/** Where one of the place's reads stands: not read yet, read, no apogee, or a failed read, in the CLI's words. */
export type Listing = { readonly case: 'notRead' } | { readonly case: 'read' } | { readonly case: 'missing' } | { readonly case: 'failed'; readonly message: string }

const notRead: Listing = { case: 'notRead' }
const missingListing: Listing = { case: 'missing' }
const listingOf = (outcome: ReadOutcome): Listing => (outcome.case === 'document' ? { case: 'read' } : { case: 'failed', message: ReadOutcome.failure(outcome) ?? 'apogee printed nothing to read' })

/** A pipeline stage as the page draws it: from the run going now, live, or from its manifest. */
export interface StageRow {
  /** From 1. */
  readonly index: number
  readonly name: string
  readonly state: string
  readonly runID: string | undefined
  /** The cumulative gate's score, only once this attempt is past its gate. */
  readonly score: number | undefined
  /** The trainer's latest iteration, while this stage trains. */
  readonly iteration: JobProgress['iteration']
}

/** What a supervised act is, for what it shows live and what its end leaves behind (the Mac's `Act`). */
type Act =
  | { readonly kind: 'other' }
  | { readonly kind: 'promote'; readonly form: PromoteForm }
  | { readonly kind: 'pull' }
  | { readonly kind: 'prepare' }
  | { readonly kind: 'pipeline'; readonly name: string }
  | { readonly kind: 'cycle' }

/** The reason the CLI writes for a halt by hand (`train-cycle-status-halted-by-hand.json`, `cycle.cpp`). */
const haltedByHand = "halted by 'apogee train cycle halt'"

/**
 * The Training place (design 1j): the Mac app's `TrainingViewModel`
 * (`presentation/training`, 1,435 lines), its names, drawn as the old
 * prototype's place -- Runs with its promoted versions, Pipelines run,
 * resumed and watched stage by stage, the unattended Cycle with one pass, a
 * halt and a resume, the Regimes, and the Datasets with their kits and a pull
 * from Hugging Face -- each view a document the CLI printed, each act the
 * person's command, a long one supervised on the jobs core, one at a time.
 *
 * The stand-in keeps no state (1g's rule), so what the CLI reported in this
 * session is kept beside its reads, as 1h's `land` keeps what a pull made: a
 * pipeline run's closing manifest, a cycle pass's closing history, and a halt
 * or a resume the CLI said it made, applied as it says.
 *
 * The binary is today's: a halt taken during a pass is kept and a pull
 * streams, as the old prototype draws them; the Mac's older-binary ways (its
 * vocabulary date, `__complete` for the specs) are not ported. Plain
 * TypeScript: it never imports React.
 */
export class TrainingViewModel extends Observable {
  readonly name = 'Training'
  tab: TrainingTab = 'runs'
  status: TrainingStatus | undefined = undefined
  ledgers: VersionLedger[] = []
  regimes: TrainingRegime[] = []
  kits: TrainingKit[] = []
  datasets: TrainingDataset[] = []
  selectedRun: string | undefined = undefined
  selectedRegime: string | undefined = undefined
  selectedDataset: string | undefined = undefined
  /** The named pipelines, as `train pipeline list` states them. */
  specs: PipelineSpec[] = []
  /** Every pipeline run, newest first, as `train pipeline status` lists them. */
  pipelineRuns: PipelineRunRow[] = []
  /** The pipeline run whose page is open; undefined shows the one running here. */
  selectedPipelineRun: string | undefined = undefined
  /** The open run's manifest, as `train pipeline status <id>` last read it. */
  pipelineRun: PipelineRun | undefined = undefined
  pipelineRunFailure: string | undefined = undefined
  /** The cycle's history as `train cycle status` last read it. */
  cycleRead: CycleHistory | undefined = undefined
  runsListing: Listing = notRead
  versionsListing: Listing = notRead
  regimesListing: Listing = notRead
  kitsListing: Listing = notRead
  datasetsListing: Listing = notRead
  pipelinesListing: Listing = notRead
  cycleListing: Listing = notRead
  /** What the act running now is, in the old bar's words, and its progress. */
  running: string | undefined = undefined
  run: JobProgress | undefined = undefined
  /** What the act running now shows live, besides its line. */
  live: 'pipeline' | 'cycle' | undefined = undefined
  pending: Pending | undefined = undefined
  promoteForm = new PromoteForm()
  /** The CLI's refusal the promote sheet was opened again with. */
  promoteRefusal: string | undefined = undefined
  refusedPromotion: RefusedPromotion | undefined = undefined
  pullRef = ''
  /** The last pull that finished, until dismissed: where its files are, for Prepare it. */
  pulled: PulledDataset | undefined = undefined

  /** The doors the composition fills: the toast, the shell's sheet and confirm hosts. */
  say: ((text: string) => void) | undefined = undefined
  present: ((sheet: TrainingSheet) => void) | undefined = undefined
  dismissSheet: (() => void) | undefined = undefined
  ask: ((kind: TrainingConfirm) => void) | undefined = undefined

  private binary: string | undefined = undefined
  private job: JobRunning | undefined = undefined
  /** The notice a pass run here ended with when a halt taken while it ran was kept. */
  private haltNotice: string | undefined = undefined
  /** The reason of a halt the CLI said it made while this place's pass ran: kept on top of the pass's closing history. */
  private haltDuringPass: string | undefined = undefined
  /** Each pipeline manifest a run here reported last, by id: kept beside the reads (the stand-in keeps no state). */
  private readonly landedRuns = new Map<string, PipelineRun>()
  /** The cycle's history as the CLI last reported it here: a pass's closing document, or a halt or resume applied. */
  private reportedCycle: CycleHistory | undefined = undefined

  constructor(
    private readonly runner: CommandRunning,
    private readonly starter: JobStarting,
  ) {
    super()
  }

  /** With no apogee, the place is its empty state, said where its lists are -- never an alert (GUI 4h). */
  connect(binary: string | undefined): void {
    this.binary = binary
    if (binary === undefined) {
      for (const listing of ['runsListing', 'versionsListing', 'regimesListing', 'kitsListing', 'datasetsListing', 'pipelinesListing', 'cycleListing'] as const) this[listing] = missingListing
    }
    this.changed()
  }

  async appeared(): Promise<void> {
    await this.refresh()
  }

  pick(tab: TrainingTab): void {
    this.tab = tab
    this.changed()
  }

  /** Every document the place shows, read again; each failed read said where what it reads is drawn, the rows read before kept. */
  async refresh(): Promise<void> {
    const binary = this.binary
    if (binary === undefined) return
    const read = await this.runner.read(binary, ['train', 'status'])
    const status = ReadOutcome.documentOf(read)
    if (status) this.status = new TrainingStatus(status)
    this.runsListing = listingOf(read)
    await this.readCycle(binary, read)
    await this.readPipelines(binary)
    const versions = await this.runner.read(binary, ['train', 'versions'])
    const ledgers = ReadOutcome.documentOf(versions)
    if (ledgers) this.ledgers = VersionLedger.list(ledgers)
    this.versionsListing = listingOf(versions)
    const regimes = await this.runner.read(binary, ['train', 'regime', 'list'])
    const regimeList = ReadOutcome.documentOf(regimes)
    if (regimeList) this.regimes = TrainingRegime.list(regimeList)
    this.regimesListing = listingOf(regimes)
    const kits = await this.runner.read(binary, ['datasets', 'kits'])
    const kitList = ReadOutcome.documentOf(kits)
    if (kitList) this.kits = TrainingKit.list(kitList)
    this.kitsListing = listingOf(kits)
    const datasets = await this.runner.read(binary, ['datasets', 'list'])
    const datasetList = ReadOutcome.documentOf(datasets)
    if (datasetList) this.datasets = TrainingDataset.list(datasetList)
    this.datasetsListing = listingOf(datasets)
    this.changed()
  }

  /** The named pipelines, the runs, and the open page's manifest again. */
  private async readPipelines(binary: string): Promise<void> {
    const specs = ReadOutcome.documentOf(await this.runner.read(binary, ['train', 'pipeline', 'list']))
    if (specs) this.specs = PipelineSpec.list(specs)
    const read = await this.runner.read(binary, ['train', 'pipeline', 'status'])
    const runs = ReadOutcome.documentOf(read)
    if (runs) this.pipelineRuns = PipelineRunRow.list(runs)
    this.pipelinesListing = listingOf(read)
    if (this.selectedPipelineRun !== undefined) await this.readPipelineRun(this.selectedPipelineRun)
  }

  /** A pipeline run's page opened, its manifest read; undefined opens the one running here. */
  async openPipelineRun(id: string | undefined): Promise<void> {
    this.selectedPipelineRun = id
    this.pipelineRunFailure = undefined
    this.changed()
    if (id !== undefined) await this.readPipelineRun(id)
  }

  private async readPipelineRun(id: string): Promise<void> {
    const binary = this.binary
    if (binary === undefined || this.landedRuns.has(id)) return
    const read = await this.runner.read(binary, ['train', 'pipeline', 'status', id])
    if (this.selectedPipelineRun !== id) return
    const document = ReadOutcome.documentOf(read)
    const manifest = document ? PipelineRun.fromDocument(document) : undefined
    if (manifest) {
      this.pipelineRun = manifest
      this.pipelineRunFailure = undefined
    } else {
      this.pipelineRunFailure = ReadOutcome.failure(read) ?? `apogee printed no pipeline run ${id}`
    }
    this.changed()
  }

  /**
   * The cycle, read only once `train status` says it has a history: before
   * the first pass writes one, `train cycle status` exits 1 (55h). A history
   * that cannot be read is `cycle` null too, `cycle_error` saying why (57h),
   * and the read is not asked either.
   */
  private async readCycle(binary: string, statusRead: ReadOutcome): Promise<void> {
    if (statusRead.case !== 'document') {
      this.cycleListing = listingOf(statusRead)
      return
    }
    if (this.status?.cycle === undefined) {
      this.cycleRead = undefined
      this.cycleListing = { case: 'read' }
      return
    }
    const read = await this.runner.read(binary, ['train', 'cycle', 'status'])
    const document = ReadOutcome.documentOf(read)
    if (document) this.cycleRead = CycleHistory.fromDocument(document)
    this.cycleListing = listingOf(read)
  }

  // MARK: - Runs

  /** What is running first, then the recent runs -- each once, a running id older than the five newest a row of its id (GUI 6i). */
  get runs(): TrainingRun[] {
    const recent = this.status?.recent ?? []
    const listed = new Set<string>()
    const running = (this.status?.runningIDs ?? []).flatMap((id) => {
      if (listed.has(id)) return []
      listed.add(id)
      return [recent.find((run) => run.id === id) ?? TrainingRun.runningID(id)]
    })
    return [...running, ...recent.filter((run) => !listed.has(run.id))]
  }

  /** The run shown: the one chosen, else the first, as the old place shows its first. */
  get currentRun(): TrainingRun | undefined {
    const runs = this.runs
    return runs.find((run) => run.id === this.selectedRun) ?? runs[0]
  }

  selectRun(id: string): void {
    this.selectedRun = id
    this.changed()
  }

  get currentRegime(): TrainingRegime | undefined {
    return this.regimes.find((regime) => regime.name === this.selectedRegime) ?? this.regimes[0]
  }

  selectRegime(name: string): void {
    this.selectedRegime = name
    this.changed()
  }

  get currentDataset(): TrainingDataset | undefined {
    return this.datasets.find((dataset) => dataset.name === this.selectedDataset) ?? this.datasets[0]
  }

  selectDataset(name: string): void {
    this.selectedDataset = name
    this.changed()
  }

  get isRunning(): boolean {
    return this.job !== undefined
  }

  /**
   * New run…: the old place starts a run at once, with no sheet. Here it
   * trains again what the run shown trained, on its data -- `train run
   * <base> --dataset <data>`, the run's own recorded facts -- so the run
   * replayed is the one the CLI recorded (`train-run.err`).
   */
  newRun(): void {
    const run = this.currentRun
    if (run === undefined || run.baseModel === '' || run.dataset === '') return
    this.start(['train', 'run', run.baseModel, '--dataset', run.dataset], 'Training', { kind: 'other' })
  }

  /** Gate it: the prototype's toast. */
  gate(): void {
    const run = this.currentRun
    if (run) this.say?.(`apogee train eval ${run.id} (prototype)`)
  }

  /** Roll back…: at once, as the prototype rolls back (its default; the Mac asks first) -- `train rollback <b>`, its line said. */
  async rollback(backend: string): Promise<void> {
    await this.change(['train', 'rollback', backend])
  }

  // MARK: - Promote

  /** Promote…: the sheet for the run shown, or the one named. */
  newPromotion(run?: string): void {
    const id = run ?? this.currentRun?.id
    if (id === undefined) return
    this.promoteForm = new PromoteForm()
    this.promoteForm.run = id
    this.promoteRefusal = undefined
    this.changed()
    this.present?.('promote')
  }

  setPromote(change: PromoteChange): void {
    Object.assign(this.promoteForm, change)
    this.changed()
  }

  /** Promote: the form's command, supervised; a refusal asks, with Change the promotion…. */
  promote(): void {
    const args = this.promoteForm.arguments()
    if (args === undefined) return
    this.dismissSheet?.()
    this.promoteRefusal = undefined
    this.refusedPromotion = undefined
    this.start(args, 'Promoting', { kind: 'promote', form: this.promoteForm.copy() })
  }

  /** The promote sheet's Cancel. */
  cancelPromote(): void {
    this.promoteRefusal = undefined
    this.dismissSheet?.()
    this.changed()
  }

  /** Change the promotion…: the refused form again, the CLI's words above it, in the mode its refusal names (`PromoteForm.door`). */
  changePromotion(): void {
    const refused = this.refusedPromotion
    if (refused === undefined) return
    this.refusedPromotion = undefined
    this.promoteForm = refused.form.door(refused.refusal)
    this.promoteRefusal = refused.refusal
    this.changed()
    this.present?.('promote')
  }

  /** The refusal's OK: the refused promotion let go. */
  dismissRefusal(): void {
    this.refusedPromotion = undefined
    this.changed()
  }

  // MARK: - Datasets

  newPull(): void {
    this.pullRef = ''
    this.changed()
    this.present?.('pull')
  }

  setPullRef(ref: string): void {
    this.pullRef = ref
    this.changed()
  }

  /** `datasets pull <ref>`, as typed; undefined without one. */
  get pullArguments(): string[] | undefined {
    const ref = this.pullRef.trim()
    return ref === '' ? undefined : ['datasets', 'pull', ref]
  }

  cancelPull(): void {
    this.dismissSheet?.()
  }

  /** Pull: streamed (the CLI's M30), on the Datasets tab, as the old sheet's Pull goes there; its end the banner. */
  pull(): void {
    const args = this.pullArguments
    if (args === undefined) return
    this.dismissSheet?.()
    this.pulled = undefined
    this.tab = 'datasets'
    this.start([...args, ...TrainingViewModel.stream], 'Pulling', { kind: 'pull' }, true)
  }

  dismissPulled(): void {
    this.pulled = undefined
    this.changed()
  }

  /** Prepare it: `datasets prepare <the pulled folder>`, supervised. */
  prepare(): void {
    const pulled = this.pulled
    if (pulled) this.start(['datasets', 'prepare', pulled.path], 'Preparing', { kind: 'prepare' })
  }

  /** The prototype's toasts: New dataset…, Synthesize…, Prepare a file…, Delete…, Run this regime. */
  newDataset(): void {
    this.say?.('apogee datasets create <name> --from template (prototype)')
  }

  synthesize(): void {
    this.say?.('apogee datasets synth <name> --teacher <backend> --kit <kit> (prototype)')
  }

  prepareFile(): void {
    this.say?.('apogee datasets prepare <file> (prototype)')
  }

  deleteDataset(): void {
    const dataset = this.currentDataset
    if (dataset) this.say?.(`apogee datasets delete ${dataset.name} --yes: confirmed first (prototype)`)
  }

  runRegime(): void {
    const regime = this.currentRegime
    if (regime) this.say?.(`apogee train regime run ${regime.name} (prototype)`)
  }

  // MARK: - Pipelines

  /** The open run's manifest: what a run here reported last, else what the read said. */
  get openPipeline(): PipelineRun | undefined {
    const id = this.selectedPipelineRun
    if (id === undefined) return undefined
    return this.landedRuns.get(id) ?? (this.pipelineRun?.id === id ? this.pipelineRun : undefined)
  }

  /** The pipeline run going here now, once its stream has named it. */
  get livePipelineID(): string | undefined {
    return this.live === 'pipeline' ? this.run?.pipelineRunID : undefined
  }

  /** Whether the open page is the run going here: none is chosen, or the chosen one is it. */
  get showsLivePipeline(): boolean {
    if (this.live !== 'pipeline' || this.run === undefined) return false
    const open = this.selectedPipelineRun
    return open === undefined || this.run.pipelineRunID === undefined || this.run.pipelineRunID === open
  }

  /** The runs listed: the read's, with each run this place ran or opened that the stand-in's list lacks, newest first. */
  get listedPipelineRuns(): PipelineRunRow[] {
    const rows = new Map<string, PipelineRunRow>()
    for (const row of this.pipelineRuns) rows.set(row.id, row)
    const opened = this.pipelineRun
    if (opened && !rows.has(opened.id)) rows.set(opened.id, PipelineRunRow.of(opened))
    for (const [id, run] of this.landedRuns) rows.set(id, PipelineRunRow.of(run))
    return [...rows.values()].sort((a, b) => (a.startedAt < b.startedAt ? 1 : a.startedAt > b.startedAt ? -1 : 0))
  }

  /** A run's stages as its stream moved them; a score only once past its gate, the iteration on the stage that trains. */
  static stagesOf(progress: JobProgress): StageRow[] {
    return progress.orderedStages().map(({ index, stage }) => ({
      index,
      name: stage.label,
      state: stage.state,
      runID: stage.runID,
      score: PipelineRun.isGated(stage.state) ? stage.cumulativeScore : undefined,
      iteration: stage.state === 'training' && progress.iteration?.stage === index ? progress.iteration : undefined,
    }))
  }

  /** A run's stages as its manifest records them. */
  static stagesOfRun(run: PipelineRun): StageRow[] {
    return run.stages.map((stage: PipelineStage) => ({ index: stage.index, name: stage.name, state: stage.status, runID: stage.runID === '' ? undefined : stage.runID, score: stage.score, iteration: undefined }))
  }

  /**
   * Where a resume of `run` starts, as the CLI says it: its document's
   * `resume` (57h), or -- on a document before 57h -- the first stage that
   * has not passed of a run that stopped short. Never a cycle pass's.
   */
  static resumePoint(run: PipelineRun): { readonly stage: number; readonly name: string; readonly note: string | undefined } | undefined {
    if (run.isCyclePass) return undefined
    if (run.hasResumeField) return run.resume === undefined ? undefined : { stage: run.resume.stage, name: run.resume.name, note: run.resume.note }
    const stage = run.resumesAt
    return run.isResumable && stage !== undefined ? { stage: stage.index, name: stage.name, note: undefined } : undefined
  }

  /** Why `run`'s Resume is held though it has a stage to resume, in the CLI's words; empty when Resume would run. */
  resumeHold(run: PipelineRun): string {
    if (TrainingViewModel.resumePoint(run) === undefined) return ''
    if (run.holder !== undefined) return TrainingWords.heldResume(run.holder)
    const found = run.hasResumeField ? run.resume?.pipeline !== undefined : this.specs.some((spec) => spec.name === run.specName)
    return found ? '' : TrainingWords.resumeNeedsSpec(run.id)
  }

  canResume(run: PipelineRun): boolean {
    return TrainingViewModel.resumePoint(run) !== undefined && this.resumeHold(run) === '' && !this.isRunning && this.binary !== undefined
  }

  /** The resume `run` would start: the bare form, or with `--pipeline` where its document names the spec (57h). */
  static resume(run: PipelineRun): Pending {
    return { kind: 'resumePipeline', id: run.id, pipeline: run.hasResumeField ? run.resume?.pipeline : undefined }
  }

  /** Run… on a named pipeline: asked first. */
  requestRun(name: string): void {
    if (this.isRunning || this.binary === undefined) return
    this.ask_({ kind: 'runPipeline', name })
  }

  /** Resume… on the open page: asked, only when it would run. */
  requestResume(): void {
    const run = this.openPipeline
    if (run && this.canResume(run)) this.ask_(TrainingViewModel.resume(run))
  }

  /** Show the cycle: a cycle pass's page's door. */
  showCycle(): void {
    this.pick('cycle')
  }

  // MARK: - The cycle

  /** Show in Finder, beside a dead pass's lock: the prototype's toast -- this place never removes it (the CLI's 27h). */
  showLockInFinder(): void {
    const lock = this.staleLock
    if (lock !== undefined) this.say?.(`Revealed ${lock} in Finder (prototype)`)
  }

  /** The history shown: what the CLI reported here last, else what it read. */
  get cycle(): CycleHistory | undefined {
    return this.reportedCycle ?? this.cycleRead
  }

  /** Whether a pass holds the cycle now: this place's own, or -- as last read -- another's. */
  get cycleHeld(): boolean {
    return this.live === 'cycle' || this.cycle?.active === true || this.status?.cycleActive === true
  }

  get cycleHalted(): boolean {
    const cycle: CycleHeadline | undefined = this.cycle ?? this.status?.cycle
    return cycle?.halted ?? false
  }

  /** A dead pass's lock, by its path (57g). */
  get staleLock(): string | undefined {
    return this.cycle?.staleLock ?? this.status?.staleLock
  }

  /** Why the history cannot be read (57h). */
  get cycleError(): string | undefined {
    return this.status?.cycleError
  }

  private get statesLockLiveness(): boolean {
    return this.status?.statesLockLiveness === true || this.cycle?.statesLockLiveness === true
  }

  /** One pass can start: nothing runs here, no pass holds the cycle, no dead pass's lock, the history reads, not halted. */
  get canRunCycle(): boolean {
    return this.binary !== undefined && !this.isRunning && !this.cycleHeld && !this.cycleHalted && this.staleLock === undefined && this.cycleError === undefined
  }

  /** A halt can be taken: there is a binary, the history reads and it isn't halted already (a pass's halt is kept, the CLI's M30). */
  get canHaltCycle(): boolean {
    return this.binary !== undefined && this.cycleError === undefined && !this.cycleHalted
  }

  requestPass(): void {
    if (this.canRunCycle) this.ask_({ kind: 'runCycle' })
  }

  requestHalt(): void {
    if (this.canHaltCycle) this.ask_({ kind: 'haltCycle' })
  }

  requestResumeCycle(): void {
    if (this.binary !== undefined && this.cycleError === undefined && this.cycleHalted) this.ask_({ kind: 'resumeCycle' })
  }

  /** What the cycle's controls say about its state: a dead pass's lock, then a halt and its reason, then an older binary's remedy. */
  get cycleHoldText(): string {
    const lock = this.staleLock === undefined ? undefined : TrainingWords.staleLock(this.staleLock)
    let held: string | undefined
    if (this.cycleHalted) {
      const reason = this.cycle?.haltReason ?? this.status?.cycle?.haltReason ?? ''
      const halted = `Halted: ${reason === '' ? "'apogee train cycle halt' was run" : reason}.`
      held = this.cycleHeld ? `${halted} ${TrainingWords.kept}: every pass after it refuses -- your scheduler’s too -- until Resume clears it.` : `${halted} Every pass refuses -- your scheduler’s too -- until Resume clears it.`
    }
    const remedy = this.cycleHeld && this.live !== 'cycle' && !this.statesLockLiveness ? TrainingWords.lockRemedy : undefined
    return [lock, held, remedy].filter((words): words is string => words !== undefined).join(' ')
  }

  // MARK: - The person's word

  private ask_(pending: Pending): void {
    this.pending = pending
    this.changed()
    this.ask?.('train')
  }

  /** What a confirm says: its title, body, the command, its buttons. */
  confirmWords(kind: TrainingConfirm): { readonly title: string; readonly body: string; readonly detail: string | undefined; readonly confirm: string; readonly cancel: string } | undefined {
    if (kind === 'promoteRefused') {
      const refused = this.refusedPromotion
      return refused === undefined ? undefined : { title: "That didn't work", body: refused.refusal, detail: undefined, confirm: 'Change the promotion…', cancel: 'OK' }
    }
    const pending = this.pending
    if (pending === undefined) return undefined
    const detail = `apogee ${Pending.arguments(pending).join(' ')}`
    switch (pending.kind) {
      case 'runPipeline':
        return { title: `Run the pipeline ${pending.name}?`, body: TrainingWords.eachStage, detail, confirm: 'Start', cancel: 'Cancel' }
      case 'resumePipeline': {
        const run = this.openPipeline
        const point = run?.id === pending.id ? TrainingViewModel.resumePoint(run) : undefined
        return {
          title: point ? `Resume ${pending.id} from stage ${point.stage}, ${point.name}?` : `Resume ${pending.id}?`,
          body: `${point?.note === undefined ? '' : `${TrainingWords.sentence(point.note)} `}What passed stays; each stage from there is a training run, held to the cumulative gate -- this machine is held until it ends.`,
          detail,
          confirm: 'Resume',
          cancel: 'Cancel',
        }
      }
      case 'runCycle':
        return { title: 'Run the training cycle once?', body: 'It trains, gates and may promote -- this machine is held until it ends.', detail, confirm: 'Run one pass', cancel: 'Cancel' }
      case 'haltCycle':
        return { title: 'Halt the training cycle?', body: `Every 'cycle run' refuses until 'cycle resume' -- your scheduler’s passes too.${this.cycleHeld ? ` ${TrainingWords.kept}.` : ''}`, detail, confirm: 'Halt', cancel: 'Cancel' }
      case 'resumeCycle':
        return { title: 'Resume the training cycle?', body: 'It clears the halt and the failure count: your scheduler’s next pass runs.', detail, confirm: 'Resume', cancel: 'Cancel' }
    }
  }

  /** A confirm answered yes: a run or a pass started supervised, its stream asked for; a halt or a resume run and read back. */
  async confirm(kind: TrainingConfirm): Promise<void> {
    if (kind === 'promoteRefused') {
      this.changePromotion()
      return
    }
    const pending = this.pending
    if (pending === undefined) return
    this.pending = undefined
    const args = Pending.arguments(pending)
    switch (pending.kind) {
      case 'runPipeline':
        this.selectedPipelineRun = undefined
        this.start([...args, ...TrainingViewModel.stream], `Running ${pending.name}`, { kind: 'pipeline', name: pending.name }, true)
        return
      case 'resumePipeline':
        this.selectedPipelineRun = pending.id
        this.landedRuns.delete(pending.id)
        this.start([...args, ...TrainingViewModel.stream], `Resuming ${pending.id}`, { kind: 'pipeline', name: pending.id }, true)
        return
      case 'runCycle':
        this.haltNotice = undefined
        this.haltDuringPass = undefined
        this.start([...args, ...TrainingViewModel.stream], 'Running one pass of the cycle', { kind: 'cycle' }, true)
        return
      case 'haltCycle':
        if (await this.change(args)) {
          // Taken while this place's pass runs: the CLI keeps it when the pass ends (M30, `cycle.cpp`'s `halt_kept`).
          if (this.live === 'cycle') this.haltDuringPass = haltedByHand
          this.report({ halted: true, haltReason: haltedByHand })
        }
        return
      case 'resumeCycle':
        if (await this.change(args)) {
          this.haltNotice = undefined
          this.haltDuringPass = undefined
          this.report({ halted: false, haltReason: '', consecutiveFails: 0 })
        }
        return
    }
  }

  /** A confirm's Cancel (or the refusal's OK). */
  dismissPending(kind: TrainingConfirm): void {
    if (kind === 'promoteRefused') this.dismissRefusal()
    else this.pending = undefined
    this.changed()
  }

  /**
   * A halt or a resume the CLI said it made, applied to the history shown:
   * the stand-in keeps no state, so its read would say the cycle as it was.
   * The reason a halt by hand records is the CLI's own.
   */
  private report(change: Partial<CycleHistory>): void {
    const base: CycleHistory = this.cycle ?? { backend: this.status?.cycle?.backend ?? '', anchorVersion: 0, anchorScore: undefined, consecutiveFails: 0, totalRuns: 0, halted: false, haltReason: '', sessionsUntil: '', runs: [], active: false, staleLock: undefined, statesLockLiveness: false }
    this.reportedCycle = { ...base, ...change }
    this.changed()
  }

  /** The stream a pipeline, a cycle pass or a pull is run with: its events, its read's document last. */
  static readonly stream = ['--output-format', 'stream-json']

  /** Runs a change, reads back, and says the answer's last line; whether it was done. */
  private async change(args: readonly string[]): Promise<boolean> {
    const binary = this.binary
    if (binary === undefined) return false
    const outcome = await this.runner.command(binary, args)
    await this.refresh()
    const failure = CommandOutcome.failure(outcome)
    if (failure !== undefined) {
      this.say?.(failure)
      return false
    }
    if (outcome.case === 'done') this.say?.(outcome.stdout.split('\n').map((line) => line.trim()).filter((line) => line !== '').at(-1) ?? 'Done')
    return true
  }

  /** Stop: the act running now ended as Ctrl-C does. */
  async stop(): Promise<void> {
    await this.job?.stop(10_000)
  }

  private start(args: readonly string[], doing: string, act: Act, streaming = false): void {
    const binary = this.binary
    if (binary === undefined || this.job !== undefined) return
    this.running = doing
    const progress = new JobProgress()
    this.run = progress
    this.live = act.kind === 'pipeline' ? 'pipeline' : act.kind === 'cycle' ? 'cycle' : undefined
    const job = this.starter.startJob(binary, args, streaming)
    this.job = job
    this.changed()
    void this.follow(job, progress, doing, act)
  }

  private async follow(job: JobRunning, progress: JobProgress, doing: string, act: Act): Promise<void> {
    for await (const update of job.updates) {
      progress.apply(update)
      this.changed()
    }
    this.job = undefined
    // A pipeline's closing manifest, and a pass's closing history: what the CLI reported, kept beside its reads.
    for (const document of progress.documents) {
      const manifest = PipelineRun.fromValue(document)
      if (manifest) this.landedRuns.set(manifest.id, manifest)
      if (act.kind === 'cycle') {
        // A halt taken while the pass ran is kept on top of its closing history, the reason by hand standing over the
        // breaker's, as the CLI leaves it (`cycle.cpp`: the breaker never trips over a kept halt); a recording made
        // with no halt never says so itself.
        const history = CycleHistory.fromValue(document)
        if (history) this.reportedCycle = this.haltDuringPass === undefined ? history : { ...history, halted: true, haltReason: this.haltDuringPass }
      }
    }
    // The run's page stays open on it.
    const id = progress.pipelineRunID
    if (act.kind === 'pipeline' && id !== undefined && (this.selectedPipelineRun === undefined || this.selectedPipelineRun === id)) this.selectedPipelineRun = id
    if (act.kind === 'cycle') {
      // The pass's own word that the halt is kept, else the CLI's words for it (`halt_kept_notice`).
      const said = [...progress.notices].reverse().find((notice) => notice.startsWith('[cycle] halted while this pass ran'))
      this.haltNotice = said ?? (this.haltDuringPass === undefined ? undefined : TrainingWords.haltKept(this.haltDuringPass))
      this.haltDuringPass = undefined
    }
    this.run = undefined
    this.running = undefined
    this.live = undefined
    await this.refresh()
    this.ended(progress, doing, act)
  }

  /** What an act's end says: the old prototype's toast, in the CLI's words where it gave some. */
  private ended(progress: JobProgress, doing: string, act: Act): void {
    const status = progress.status
    if (status.case === 'done') {
      if (act.kind === 'pull') {
        const pulled = [...progress.documents].reverse().reduce<PulledDataset | undefined>((found, document) => found ?? PulledDataset.fromDocument(document, progress.notices), undefined) ?? PulledDataset.fromSaid(progress.notices)
        if (pulled) {
          this.pulled = pulled
          this.say?.(`Pulled: ${pulled.summary === '' ? pulled.path : pulled.summary}`)
        }
      } else if (act.kind === 'prepare') {
        this.pulled = undefined
        this.say?.(progress.notices[0] ?? `${doing}: done`)
      } else if (act.kind === 'pipeline') {
        // The finished run's own manifest, whatever page is open now.
        const id = progress.pipelineRunID
        const finished = id === undefined ? undefined : (this.landedRuns.get(id) ?? (this.pipelineRun?.id === id ? this.pipelineRun : undefined))
        this.say?.(`Pipeline ${act.name}: ${finished?.status ?? 'done'}`)
      } else if (act.kind === 'cycle') {
        this.say?.(this.haltNotice ?? progress.pass?.text ?? 'The pass ended')
      } else {
        this.say?.(progress.notices[0] ?? `${doing}: done`)
      }
    } else if (status.case === 'failed') {
      if (act.kind === 'promote') {
        this.refusedPromotion = { form: act.form, refusal: status.message }
        this.changed()
        this.ask?.('promoteRefused')
      } else {
        this.say?.(status.message)
      }
    } else if (status.case === 'stopped') {
      this.say?.(act.kind === 'pipeline' ? 'Stopped: what passed stays -- Resume continues it' : 'Stopped: what was finished stays')
    }
    this.changed()
  }

  // MARK: - The face

  /** Everything the place's views draw, as words: absence said explicitly, so the per-preset walk reads it whole. */
  face(): TrainingFace {
    const installed = this.binary !== undefined
    const busy = this.isRunning
    return {
      tab: this.tab,
      tabs: trainingTabs.map(([tab, label]) => ({ tab, label, current: tab === this.tab })),
      header: {
        newRun: { shown: this.tab === 'runs', enabled: installed && !busy && this.currentRun !== undefined },
        cycle: { shown: this.tab === 'cycle', canRun: this.canRunCycle, halted: this.cycleHalted && this.cycleError === undefined, canHalt: this.canHaltCycle },
        datasets: { shown: this.tab === 'datasets', enabled: installed, canPull: installed && !busy },
      },
      bar: this.barFace(),
      runs: this.runsFace(),
      pipelines: this.pipelinesFace(),
      cycle: this.cycleFace(),
      regimes: this.regimesFace(),
      datasets: this.datasetsFace(),
      promoteSheet: this.promoteFace(),
      pullSheet: { ref: this.pullRef, command: `apogee datasets pull ${this.pullRef.trim() === '' ? 'owner/name' : this.pullRef.trim()}`, canPull: this.pullArguments !== undefined && installed },
    }
  }

  private barFace(): TrainingFace['bar'] {
    const run = this.run
    if (this.running === undefined || run === undefined) return { shown: false, label: '', line: '', canStop: false }
    return { shown: true, label: this.running, line: run.busy ?? run.lastSaid ?? TrainingWords.starting, canStop: true }
  }

  /** A list's line when its read failed, in the CLI's words (no apogee is each tab's own state, as the old prototype draws it). */
  private static listingText(listing: Listing, what: string): string {
    return listing.case === 'failed' ? `The ${what} could not be read: ${listing.message}` : ''
  }

  private runsFace(): RunsFace {
    const current = this.currentRun
    const missing = this.runsListing.case === 'missing'
    return {
      rows: this.runs.map((run) => ({ id: run.id, status: run.status, meta: TrainingWords.summary(run), failed: run.evalPassed === false, current: run.id === current?.id })),
      missing: missing ? TrainingWords.missing : '',
      failure: TrainingViewModel.listingText(this.runsListing, 'runs'),
      versionsShown: !missing,
      versionsFailure: TrainingViewModel.listingText(this.versionsListing, 'promoted versions'),
      versions: this.ledgers.map((ledger) => {
        const last = ledger.versions.at(-1)
        return {
          backend: ledger.backend,
          canRoll: !busy(this) && ledger.versions.length >= 2 && last !== undefined && last.version === ledger.active,
          lines: ledger.versions.map((version) => `v${version.version} · run ${version.runID}${version.isAdapter ? ' · adapter' : ''}${version.evalScoredOn ? ` · scored on ${version.evalScoredOn}` : ''}${version.version === ledger.active ? ' · active' : ''}`),
        }
      }),
      noneChosen: 'Choose a run to see what it trained and how it did.',
      detail:
        current === undefined || missing
          ? { shown: false, id: '', gate: '', failed: false, facts: [] }
          : {
              shown: true,
              id: current.id,
              gate: current.listedByID ? TrainingWords.listedByID : TrainingWords.gate(current),
              failed: current.evalPassed === false,
              facts: current.listedByID
                ? []
                : [
                    { label: 'TRAINED', value: current.baseModel },
                    { label: 'ON', value: current.dataset },
                    { label: 'HOW', value: [current.method, current.trainer, current.iterations === undefined ? '' : `${current.iterations} iterations`].filter((part) => part !== '').join(' · ') },
                    { label: 'FINAL LOSS', value: current.finalLoss === undefined ? 'not reported' : current.finalLoss.toFixed(4) },
                    { label: 'RAN', value: `${current.startedAt} → ${current.finishedAt}` },
                  ],
            },
    }
  }

  private stageFace(row: StageRow): StageRowFace {
    return {
      index: String(row.index),
      name: row.name,
      line: TrainingWords.stage(row),
      tone: row.state === 'passed' ? 'passed' : row.state === 'failed' ? 'failed' : row.state === 'pending' ? 'pending' : 'active',
      runLine: row.runID === undefined ? '' : `run ${row.runID}`,
    }
  }

  private pageFace(): PageFace {
    const none = { id: '', line: '', failed: false, when: '', stages: [], noStages: false, resume: { shown: false, line: '', command: '', enabled: false, hold: '' }, endLine: '', isCyclePass: false, failure: '' }
    if (this.showsLivePipeline && this.run) {
      const stages = TrainingViewModel.stagesOf(this.run).map((row) => this.stageFace(row))
      return { ...none, kind: 'live', id: this.livePipelineID ?? TrainingWords.starting, stages, noStages: stages.length === 0 }
    }
    const run = this.openPipeline
    if (run === undefined) {
      const failure = this.selectedPipelineRun !== undefined ? (this.pipelineRunFailure ?? '') : ''
      return { ...none, kind: 'none', failure }
    }
    const point = TrainingViewModel.resumePoint(run)
    const stages = TrainingViewModel.stagesOfRun(run).map((row) => this.stageFace(row))
    const resumable = (run.status === 'aborted' || run.status === 'failed') && run.stages.some((stage) => stage.status !== 'passed')
    const pending = TrainingViewModel.resume(run)
    return {
      kind: 'run',
      id: run.id,
      line: [run.specName, run.status, `on ${run.student}`].join(' · '),
      failed: resumable,
      when: run.startedAt + (run.completedAt ? ` → ${run.completedAt}` : ''),
      stages,
      noStages: stages.length === 0,
      resume:
        point === undefined
          ? none.resume
          : {
              shown: true,
              line: `Resume from stage ${point.stage}, ${point.name}${point.note === undefined ? '' : `; ${point.note}`}`,
              command: `apogee ${Pending.arguments(pending).join(' ')}${run.hasResumeField ? ': after the last stage that passed' : ': from the first stage that has not passed'}`,
              enabled: this.canResume(run),
              hold: this.resumeHold(run),
            },
      endLine: TrainingWords.pageLine(run),
      isCyclePass: run.isCyclePass,
      failure: '',
    }
  }

  private pipelinesFace(): PipelinesFace {
    const liveID = this.livePipelineID
    const live = this.live === 'pipeline'
    const page = this.pageFace()
    const rows = this.listedPipelineRuns.filter((row) => !live || row.id !== liveID)
    return {
      specs: this.specs.map((spec) => ({ name: spec.name, meta: TrainingWords.spec(spec), command: `apogee train pipeline run --pipeline ${spec.name}`, label: `Run the pipeline ${spec.name}`, canRun: this.binary !== undefined && !this.isRunning })),
      noSpecs: this.specs.length === 0 ? TrainingWords.noSpecs : '',
      live: { shown: live, id: liveID ?? (live ? TrainingWords.starting : ''), current: this.showsLivePipeline },
      rows: rows.map((row) => ({ id: row.id, line: TrainingWords.row(row), failed: PipelineRun.resumes(row.status) && row.passed < row.stages, current: page.kind === 'run' && page.id === row.id })),
      noRuns: rows.length === 0 && !live ? TrainingWords.noRuns : '',
      failure: TrainingViewModel.listingText(this.pipelinesListing, 'pipeline runs'),
      page,
    }
  }

  private cycleFace(): CycleFace {
    const cycle = this.cycle
    const error = this.cycleError
    const liveCycle = this.live === 'cycle' && this.run ? this.run : undefined
    return {
      schedule: TrainingWords.schedule,
      hold: this.cycleHoldText,
      lock: this.staleLock ?? '',
      live: liveCycle
        ? {
            shown: true,
            lines: liveCycle.passes.map((line, index) => ({ state: line.state, text: line.text, latest: index === liveCycle.passes.length - 1 })),
            starting: liveCycle.passes.length === 0,
            stages: TrainingViewModel.stagesOf(liveCycle).map((row) => this.stageFace(row)),
          }
        : { shown: false, lines: [], starting: false, stages: [] },
      error: error === undefined ? '' : `The cycle history could not be read: ${error}`,
      errorHeld: error === undefined ? '' : TrainingWords.unreadable,
      failure: TrainingViewModel.listingText(this.cycleListing, 'cycle'),
      // The old prototype says no history whenever none is read, with no apogee too (it draws no missing state here).
      noHistory: cycle === undefined && error === undefined && this.cycleListing.case !== 'failed' ? TrainingWords.noHistory : '',
      facts:
        cycle === undefined
          ? []
          : [
              { label: 'PROMOTES INTO', value: cycle.backend === '' ? '—' : cycle.backend },
              { label: 'ANCHOR', value: TrainingWords.anchor(cycle) },
              { label: 'STATE', value: this.cycleHeld ? 'a pass is running' : this.cycleHalted ? 'halted' : this.staleLock !== undefined ? 'stopped, its lock left behind' : 'idle' },
              { label: 'FAILED IN A ROW', value: String(cycle.consecutiveFails) },
              { label: 'PASSES', value: String(cycle.runs.length) },
            ],
      hasHistory: cycle !== undefined,
      passes: (cycle?.runs ?? [])
        .slice()
        .reverse()
        .map((pass) => ({ at: pass.runAt, pipeline: pass.pipelineID, line: TrainingWords.pass(pass), failed: pass.gate === 'fail', passed: pass.gate === 'pass', reason: pass.gateReason, note: pass.note ?? '' })),
      keptHalt: this.cycleHalted ? (this.haltNotice ?? '') : '',
    }
  }

  private regimesFace(): RegimesFace {
    const current = this.currentRegime
    return {
      rows: this.regimes.map((regime) => ({ name: regime.name, meta: `${regime.kits.length} ${regime.kits.length === 1 ? 'kit' : 'kits'} · ${regime.source}`, current: regime.name === current?.name })),
      failure: TrainingViewModel.listingText(this.regimesListing, 'regimes'),
      detail: current === undefined ? { shown: false, name: '', description: '', kits: [] } : { shown: true, name: current.name, description: current.description, kits: current.kits.map((kit, index) => `${index + 1}. ${kit}`) },
    }
  }

  private datasetsFace(): DatasetsFace {
    const current = this.currentDataset
    const missing = this.datasetsListing.case === 'missing'
    const pulled = this.pulled
    return {
      pulled: pulled
        ? { shown: true, line: `Pulled: ${pulled.summary === '' ? pulled.path : pulled.summary}`, path: pulled.path, command: `apogee datasets prepare ${pulled.path}: the files made trainer-ready`, canPrepare: this.binary !== undefined && !this.isRunning }
        : { shown: false, line: '', path: '', command: '', canPrepare: false },
      rows: this.datasets.map((dataset) => ({ name: dataset.name, meta: `${dataset.lines} ${dataset.lines === 1 ? 'example' : 'examples'} · ${dataset.shape}`, current: dataset.name === current?.name })),
      missing: missing ? TrainingWords.missing : '',
      failure: TrainingViewModel.listingText(this.datasetsListing, 'datasets'),
      kits: this.kits.map((kit) => ({ name: kit.name, meta: `${kit.evalItems} eval items` })),
      kitsFailure: TrainingViewModel.listingText(this.kitsListing, 'kits'),
      noneChosen: 'Choose a dataset.',
      detail: current === undefined || missing ? { shown: false, name: '', line: '', path: '' } : { shown: true, name: current.name, line: `${current.lines} ${current.lines === 1 ? 'example' : 'examples'} · ${current.shape} · ${current.bytes} bytes`, path: current.path },
    }
  }

  private promoteFace(): PromoteSheetFace {
    const form = this.promoteForm
    const run = this.runs.find((entry) => entry.id === form.run)
    const backend = form.backend.trim()
    const ledger = this.ledgers.find((entry) => entry.backend === backend)
    const active = ledger?.versions.find((version) => version.version === ledger.active)
    return {
      title: `Promote ${form.run}`,
      error: this.promoteRefusal ?? '',
      verdict: run === undefined ? { text: '', failed: false } : { text: `This run ${TrainingWords.gate(run)}.`, failed: run.evalPassed === false },
      adapter: form.adapter,
      backend: form.backend,
      entries: this.ledgers.map((entry) => entry.backend),
      ledger: ledger && active && ledger.holdsAdapters !== undefined ? `${backend} holds ${ledger.holdsAdapters ? 'adapter' : 'merged'} versions (active v${active.version} of ${ledger.versions.length})` : '',
      suite: form.suite,
      judge: form.judge ?? '',
      target: form.target ?? '',
      quantize: form.quantize ?? '',
      keepFused: form.keepFused,
      trainer: form.trainer ?? '',
      force: form.force,
      command: `apogee ${form.argumentsAs(backend === '' ? '<backend>' : backend).join(' ')}`,
      canPromote: backend !== '' && this.binary !== undefined && !this.isRunning,
    }
  }
}

const busy = (model: TrainingViewModel) => model.isRunning
