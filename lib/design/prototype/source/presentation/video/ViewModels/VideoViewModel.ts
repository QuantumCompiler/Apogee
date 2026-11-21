import type { Child } from '../../../business/jobs/ChildRegistry'
import { JobProgress } from '../../../business/jobs/JobProgress'
import { RuntimeInstall } from '../../../business/jobs/RuntimeInstall'
import type { JobRunning, JobStarting } from '../../../business/jobs/SupervisedJob'
import { InstallConsent } from '../../../business/media/InstallConsent'
import { MediaEngine, MediaModel } from '../../../business/media/MediaEngine'
import { ClipRecord, MediaLibrary, baseName } from '../../../business/media/MediaRecords'
import type { FolderReading, Skipped } from '../../../business/media/MediaRecords'
import type { CommandRunning } from '../../../business/reads/CommandRunning'
import { ReadOutcome } from '../../../business/reads/ReadRunner'
import type { Clock } from '../../../infrastructure/clock/Clock'
import { installSheetWords } from '../../styles/Media/Install'
import type { InstallCardFace } from '../../styles/Media/Install'
import { phaseStatus } from '../../styles/Media/Phase'
import { Playback } from '../../styles/Media/Playback'
import { Observable } from '../../styles/Observation/Observable'
import type { ClipAction, ClipFace, ClipRowFace, FormFace, RunFace, VideoFace, VideoTab } from './VideoFaces'
import { VideoForm, framesOf, moviesFolder, secs, thisPlatform, tilde } from './VideoForm'
import type { FormContext, FormLoRA, FormPreset, SeedMode } from './VideoForm'

export type { VideoTab }

/** The sheet the place asks the shell's host to frame: the install consent. */
export type VideoSheet = 'runtimeConsent'

/** What the place needs of the clock: its ticks (the player's), its fixed now (a record's "Today"), and each tick. */
export type VideoClock = Pick<Clock, 'ticks' | 'now' | 'onTick'>

/** What the place reads of the shared job list: the children another place runs (an install, a bootstrap). */
export interface ChildList {
  children(kind?: 'session' | 'job'): Child[]
}

/** A run of `video generate`: its arguments, what it writes, its plan, and what it has said. */
interface ClipRun {
  readonly args: readonly string[]
  readonly output: string
  readonly prompt: string
  /** The model it runs, as the form named it, for its phases' words. */
  readonly model: string
  /** Its steps and frames as the form planned them when it started: what its phases say until the run reports its own. */
  readonly plan: { readonly steps: number; readonly frames: number }
  readonly progress: JobProgress
  job: JobRunning | undefined
}

/** What a run needs to start, or start again: everything but what it says. */
type PlannedRun = Omit<ClipRun, 'progress' | 'job'>

/** The LoRAs the old prototype's "Add a LoRA file…" offers in turn: a browser picks no file, so they are handed in, as the session's attach samples are. */
export const loraSamples: readonly string[] = ['/Users/someone/loras/ink-wash-wan22.safetensors', '/Users/someone/loras/film-grain-wan22.safetensors', '/Users/someone/loras/watercolor-v2/watercolor-v2.safetensors', '/Users/someone/loras/slow-dolly-wan22.safetensors']

/** The seed image the old prototype's "Add a seed image…" offers: a browser picks no file. */
export const imageSample = '/Users/someone/Pictures/harbor-sketch.png'

/** The old prototype's four frame sizes. */
const frameSizes: ReadonlyArray<readonly [string, string]> = [
  ['1280x704', '1280 × 704 · landscape, the pipeline’s own'],
  ['704x1280', '704 × 1280 · portrait'],
  ['960x544', '960 × 544 · smaller'],
  ['1920x1056', '1920 × 1056 · the largest side allowed'],
]

/** A size the way the CLI writes it: bytes, then KiB, MiB, GiB to a tenth. */
function sizeText(bytes: number): string {
  const units = ['B', 'KiB', 'MiB', 'GiB', 'TiB']
  let value = bytes
  let unit = 0
  while (value >= 1024 && unit < units.length - 1) {
    value /= 1024
    unit += 1
  }
  return unit === 0 ? `${bytes} B` : `${Math.round(value * 10) / 10} ${units[unit]}`
}

/** How long a run took, as the old prototype says it: `7 min 48 s`, `52 s`. */
function took(seconds: number | undefined): string {
  if (seconds === undefined) return 'not recorded'
  const whole = Math.round(seconds)
  const minutes = Math.floor(whole / 60)
  return minutes > 0 ? `${minutes} min ${whole - minutes * 60} s` : `${whole} s`
}

const months = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec']

/** When a record was made, against the clock's now, in UTC as the stand-in's now is: `Today 12:58`, `Yesterday 18:20`, `Oct 16`. */
function when(createdAt: string | undefined, now: Date): string {
  const at = createdAt === undefined ? undefined : new Date(createdAt)
  if (at === undefined || Number.isNaN(at.getTime())) return 'when it was made is not recorded'
  const day = (date: Date) => Date.UTC(date.getUTCFullYear(), date.getUTCMonth(), date.getUTCDate())
  const time = `${String(at.getUTCHours()).padStart(2, '0')}:${String(at.getUTCMinutes()).padStart(2, '0')}`
  const days = Math.round((day(now) - day(at)) / 86_400_000)
  if (days === 0) return `Today ${time}`
  if (days === 1) return `Yesterday ${time}`
  return `${months[at.getUTCMonth()]} ${at.getUTCDate()}`
}

/** `-- prompt` moved last: the 52b stream asked for before `--`, as the Mac's `Argv.adding` puts it. */
function streaming(args: readonly string[]): string[] {
  const at = args.indexOf('--')
  return at < 0 ? [...args, '--output-format', 'stream-json'] : [...args.slice(0, at), '--output-format', 'stream-json', ...args.slice(at)]
}

/**
 * The Video place's clips (design 1k): the Mac app's `VideoViewModel`, its
 * names, drawn as the old prototype's place -- the clips list with the run
 * and the install card, a clip's page with its player, facts, LoRAs, chain,
 * files and command, and the generate form beside them -- over the stand-in.
 *
 * The library is records, not a database: the `video.generation` documents
 * the CLI wrote beside its clips in `~/Movies/Apogee`, read through a
 * `FolderReading` on coming into view; a clip a run reports it made is kept
 * beside that read, since the stand-in keeps no state (1h's `land`). Every
 * run is the command a person would type, built from the form; a run is a
 * job on the shared job list, so the sidebar and the monitor see it. The
 * runtime is installed only on an explicit yes, as a supervised job of its
 * own: its card, Stop, its failure said, the refused clip run again once it
 * is in. Plain TypeScript: it never imports React.
 */
export class VideoViewModel extends Observable {
  tab: VideoTab = 'clips'
  /** The clips: the folder's records, newest first, and those a run reported it made. */
  clips: ClipRecord[] = []
  skipped: Skipped[] = []
  /** The clip shown, by its path, or `run` for the run. */
  selected: string | undefined = undefined
  form: VideoForm
  /** The store's video models, as `models list` names them; empty before the read and where the store holds none. */
  models: MediaModel[] = []
  /** `models.default_video`, as `config get` prints it; `undefined` when unset. */
  defaultModel: string | undefined = undefined
  /** The install a refused run named, waiting on the person's yes. */
  consent: InstallConsent | undefined = undefined
  /** The install the person said yes to, as it runs and, ended, until dismissed. */
  installing: RuntimeInstall | undefined = undefined
  /** The one player, off the clock. */
  readonly playback: Playback

  /** The toast. */
  say: ((text: string) => void) | undefined = undefined
  /** The shell's sheet host, by kind; and its dismissal. */
  present: ((sheet: VideoSheet) => void) | undefined = undefined
  dismissSheet: (() => void) | undefined = undefined
  /** Models' Stored weights at a set: the header's pipeline line. */
  openModels: ((handle: string) => void) | undefined = undefined
  /** The Video place shown: where an install from Settings draws its card. */
  showPlace: (() => void) | undefined = undefined
  /** New composition: the compositions tab's, 1l's; until it lands, none. */
  newComposition: (() => void) | undefined = undefined
  /** How many compositions the compositions tab lists, once 1l reads them; `undefined` draws no count. */
  compositionsCount: () => number | undefined = () => undefined
  /** The shared job list (design 1h): an install another place runs, which every install waits for. */
  registry: ChildList | undefined = undefined

  private binary: string | undefined = undefined
  private scanned: ClipRecord[] = []
  /** What runs reported they made, kept beside the folder's read. */
  private readonly landed = new Map<string, ClipRecord>()
  private run: ClipRun | undefined = undefined
  private installJob: JobRunning | undefined = undefined
  /** The refused run, run again once its install is in -- and only then. */
  private retryAfterInstall: PlannedRun | undefined = undefined
  /** The CLI refused a LoRA for peft since the last install went in. */
  private peftRefused = false

  constructor(
    private readonly runner: CommandRunning,
    private readonly starter: JobStarting,
    private readonly folders: FolderReading,
    private readonly clock: VideoClock,
  ) {
    super()
    this.playback = new Playback(() => clock.ticks)
    this.form = VideoForm.fresh('')
    this.playback.subscribe(() => this.changed())
    clock.onTick(() => {
      if (this.playback.on) this.changed()
    })
  }

  connect(binary: string | undefined): void {
    this.binary = binary
    this.refresh()
  }

  /** Coming into view: the library read again, the store's video models and the default. */
  async appeared(): Promise<void> {
    this.refresh()
    await this.readModels()
  }

  /** The records, read again from the folder, with what runs reported they made. */
  refresh(): void {
    const scan = MediaLibrary.clips([moviesFolder], this.folders)
    this.scanned = scan.records
    this.skipped = scan.skipped
    this.compose()
    this.changed()
  }

  /** The store's video models (`models list`) and the default (`config get models.default_video`): what the form offers. */
  async readModels(): Promise<void> {
    const binary = this.binary
    if (binary === undefined) return
    const list = ReadOutcome.documentOf(await this.runner.read(binary, ['models', 'list']))
    if (list) this.models = MediaModel.videoModels(list)
    const outcome = await this.runner.command(binary, ['config', 'get', 'models.default_video'])
    if (outcome.case === 'done') this.defaultModel = outcome.stdout.trim() || undefined
    // A new form with no pipeline yet takes the default, as the old freshClipForm does.
    if (this.form.pipeline === '') this.form = { ...this.form, pipeline: this.defaultModel ?? this.models[0]?.handle ?? '' }
    this.changed()
  }

  /** The header's pipeline line: the default in Models' Stored weights. */
  openDefault(): void {
    const handle = this.defaultModel ?? this.form.pipeline
    if (handle !== '') this.openModels?.(handle)
  }

  pick(tab: VideoTab): void {
    this.tab = tab
    this.changed()
  }

  /** A clip by its path, or the run. */
  select(id: string): void {
    this.selected = id
    this.changed()
  }

  /** The clip shown: the one selected, else the newest; none while the run is selected. */
  get shownClip(): ClipRecord | undefined {
    if (this.selected === 'run' && this.run) return undefined
    return this.clips.find((clip) => clip.output === this.selected) ?? this.clips[0]
  }

  /** Where a clip came from, back to the first. */
  chain(clip: ClipRecord): ClipRecord[] {
    return MediaLibrary.chain(clip, this.clips)
  }

  // MARK: - The form

  /** The record the form runs again or continues. */
  get formRecord(): ClipRecord | undefined {
    const from = this.form.from
    return from === undefined ? undefined : this.clips.find((clip) => clip.output === from)
  }

  /** What the form is checked and named against. */
  get context(): FormContext {
    const taken = this.clips.flatMap((clip) => [clip.name, `${clip.name}.json`])
    if (this.run && this.isRunning) taken.push(baseName(this.run.output))
    for (const file of this.folders.files(moviesFolder)) taken.push(file.name)
    return { models: this.models, defaultModel: this.defaultModel, taken, platform: thisPlatform }
  }

  /** The form set whole: a preset's, as the old `applyStart` sets `cf`, said nowhere. */
  setForm(form: VideoForm): void {
    this.form = form
    this.changed()
  }

  /** The form, changed. */
  edit(patch: Partial<VideoForm>): void {
    this.form = { ...this.form, ...patch }
    this.changed()
  }

  /** Steps typed: 16 is the draft, 50 the final, anything else by hand. */
  setSteps(steps: string): void {
    this.edit({ steps, preset: steps === '16' ? 'draft' : steps === '50' ? 'final' : 'custom' })
  }

  /** A quality picked: the draft's 16 steps, the final's 50, or the steps kept by hand. */
  setPreset(preset: FormPreset): void {
    this.edit({ preset, steps: preset === 'draft' ? '16' : preset === 'final' ? '50' : this.form.steps })
  }

  /** A seed mode picked: a given one keeps what was typed, or a new one; held is the record's; drawn is none. */
  setSeedMode(seedMode: SeedMode): void {
    const form = this.form
    const seed = seedMode === 'given' ? (form.seedMode === 'given' && form.seed !== '' ? form.seed : String(this.nextSeed())) : seedMode === 'held' ? String(this.formRecord?.seed ?? '') : ''
    this.edit({ seedMode, seed })
  }

  /** "Add a seed image…": the old prototype's sample, since a browser picks no file. */
  addImage(): void {
    this.edit({ image: imageSample })
  }

  dropImage(): void {
    this.edit({ image: undefined })
  }

  /** "Add a LoRA file…": the next sample not on the form, at strength 1. */
  addLora(): void {
    const have = this.form.loras.map((entry) => entry.path)
    const next = loraSamples.find((path) => !have.includes(path))
    if (next !== undefined && this.form.loras.length < 4) this.edit({ loraEdited: true, loras: [...this.form.loras, { path: next, sha256: undefined, scale: '1' }] })
  }

  setLoraScale(index: number, scale: string): void {
    this.edit({ loraEdited: true, loras: this.form.loras.map((entry, at): FormLoRA => (at === index ? { ...entry, scale } : entry)) })
  }

  removeLora(index: number): void {
    this.edit({ loraEdited: true, loras: this.form.loras.filter((_, at) => at !== index) })
  }

  /** Start fresh: a new clip on the form's pipeline. */
  startFresh(): void {
    this.form = VideoForm.fresh(this.form.pipeline)
    this.changed()
  }

  /** The header's New clip: the form fresh, said where it is; on Compositions, 1l's New composition. */
  newPrimary(): void {
    if (this.tab === 'compositions') {
      this.newComposition?.()
      return
    }
    this.startFresh()
    this.say?.('Describe the clip on the right. Drafts first, then the final.')
  }

  /** A clip page's action, as the old prototype's buttons set the form up and say so. */
  act(action: ClipAction, output: string): void {
    const clip = this.clips.find((candidate) => candidate.output === output)
    if (!clip) return
    const fill = (form: VideoForm, said: string) => {
      this.form = form
      this.changed()
      this.say?.(said)
    }
    switch (action) {
      case 'final':
        fill({ ...VideoForm.from(clip, 'from'), preset: 'final', steps: '50' }, 'Set up on the right: this draft at 50 steps, its seed held. Generate when ready.')
        return
      case 'again':
        fill(VideoForm.from(clip, 'from'), `Set up on the right from ${clip.name}.json. Change anything, then Generate.`)
        return
      case 'reroll':
        fill({ ...VideoForm.from(clip, 'from'), seedMode: 'given', seed: String(this.nextSeed(clip.seed)) }, 'A new seed, everything else held. Generate when ready.')
        return
      case 'extend':
        fill(VideoForm.from(clip, 'extend'), `Set up on the right: continuing ${clip.name} from its last frame.`)
        return
      case 'finder':
        this.say?.(`Revealed ~/Movies/Apogee/${clip.name} in Finder (prototype)`)
        return
    }
  }

  /** A seed of its own: the old prototype draws one at random; here it is drawn from the clock, so a frame draws the same one twice. */
  private nextSeed(from = 0): number {
    return (from * 1103515245 + 12345 + this.clock.ticks * 2654435761) % 2147483647
  }

  // MARK: - The run

  /** Whether a clip is being made now. */
  get isRunning(): boolean {
    return this.run?.job !== undefined
  }

  /** Whether the install the person said yes to runs now. */
  get isInstalling(): boolean {
    return this.installJob !== undefined
  }

  /**
   * The clip being made now, as the monitor lists it: the arguments its job
   * runs, by which the composition knows the job its own, and -- once its
   * pipeline has loaded, as the old monitor lists `vrun` past its `load` --
   * what it is, its pipeline generating, and the device and type it said it
   * loaded on. `loaded` is `undefined` while the pipeline loads and while a
   * refusal plays; the whole is `undefined` with no clip being made.
   */
  get generating(): { readonly arguments: readonly string[]; readonly loaded: { readonly name: string; readonly runtime: string | undefined } | undefined } | undefined {
    const run = this.run
    if (!run?.job) return undefined
    const ready = run.progress.ready
    if (ready === undefined) return { arguments: streaming(run.args), loaded: undefined }
    const runtime = [ready.device, ready.dtype].filter((part) => part !== undefined && part !== '').join(' · ')
    return { arguments: streaming(run.args), loaded: { name: run.model === '' ? 'apogee video generate' : `${run.model.split('/')[0]} · generating`, runtime: runtime === '' ? undefined : runtime } }
  }

  /** The form's errors, its run's arguments and what it writes. */
  private planned(): { readonly errors: string[]; readonly output: string; readonly args: string[] | undefined } {
    const record = this.formRecord
    const context = this.context
    const output = VideoForm.output(this.form, record, context.taken)
    return { errors: VideoForm.errors(this.form, context), output, args: VideoForm.clipForm(this.form, record, context).arguments(`${moviesFolder}/${output}`) }
  }

  /** Generate: the form's command, supervised -- never beside an install, whose runtime the clip may need. */
  generate(): void {
    const { errors, output, args } = this.planned()
    if (errors.length || args === undefined || this.isRunning || this.binary === undefined) return
    if (this.installing && this.isInstalling) {
      this.say?.(`Installing first: the clip can be made once ${this.installing.command} has finished`)
      return
    }
    const form = this.form
    this.start({
      args,
      output: `${moviesFolder}/${output}`,
      prompt: form.prompt.trim() || (this.formRecord?.prompt ?? ''),
      model: form.pipeline,
      plan: { steps: Number(form.steps), frames: framesOf(Number(form.dur), Number(form.fps)) },
    })
    this.form = { ...this.form, out: undefined }
    this.tab = 'clips'
    this.changed()
  }

  private start(planned: PlannedRun): void {
    const binary = this.binary
    if (binary === undefined || this.isRunning || this.isInstalling) return
    const { args, output, prompt, model, plan } = planned
    const job = this.starter.startJob(binary, streaming(args), true)
    const run: ClipRun = { args, output, prompt, model, plan, progress: new JobProgress(), job }
    this.run = run
    this.selected = 'run'
    this.changed()
    void (async () => {
      for await (const update of job.updates) {
        run.progress.apply(update)
        this.changed()
      }
      run.job = undefined
      this.finished(run)
    })()
  }

  private finished(run: ClipRun): void {
    const status = run.progress.status
    if (status.case === 'done') {
      const record = ClipRecord.read(run.progress.documents.at(-1) ?? null)
      if (record) {
        this.landed.set(record.output, record)
        this.compose()
        this.selected = record.output
      }
      if (this.run === run) this.run = undefined
      this.say?.(`${baseName(run.output)} is ready, with its record beside it`)
    } else if (status.case === 'failed') {
      // A run refused because the runtime is not installed names the command that installs it: the app asks, never assumes.
      const asked = InstallConsent.fromRun(run.progress.error, status.message, run.progress.stderr)
      if (asked) {
        if (asked.said.includes('peft')) this.peftRefused = true
        // The CLI refused before it ran anything: no run to draw, the refusal said in the sheet.
        if (this.run === run) this.run = undefined
        this.selected = this.formRecord?.output ?? this.clips[0]?.output
        const elsewhere = this.installElsewhere()
        if (elsewhere !== undefined) this.say?.(`${elsewhere} is already running: generate the clip again once it has finished`)
        else this.ask(asked, run)
      }
    }
    this.changed()
  }

  /** Stop: the run ends as Ctrl-C ends it -- nothing written, no record -- or, with no run, the install. */
  async stop(): Promise<void> {
    if (this.run?.job) await this.run.job.stop(10_000)
    else await this.installJob?.stop(10_000)
  }

  /** Run it again: the stopped or failed run's command, once more. */
  runAgain(): void {
    const run = this.run
    if (!run || this.isRunning) return
    this.start(run)
  }

  /** Dismiss: the ended run put away, the newest clip shown. */
  dismissRun(): void {
    if (this.isRunning) return
    this.run = undefined
    this.selected = this.clips[0]?.output
    this.changed()
  }

  // MARK: - Consent and the install

  /** The consent asked: the shell's sheet, the refused run kept -- its prompt, model and plan with it -- to run again once the install is in. Settings' Install… asks with nothing to run again. */
  ask(consent: InstallConsent, retry?: PlannedRun): void {
    this.consent = consent
    this.retryAfterInstall = retry === undefined ? undefined : { args: retry.args, output: retry.output, prompt: retry.prompt, model: retry.model, plan: retry.plan }
    this.changed()
    this.present?.('runtimeConsent')
  }

  /** The install another place runs now, as the job list labels it: a bootstrap, or another place's `train setup`. */
  private installElsewhere(): string | undefined {
    const mine = this.installJob !== undefined
    return this.registry
      ?.children('job')
      .map((child) => child.label)
      .find((label) => label.startsWith('apogee bootstrap') || (!mine && label.startsWith('apogee train setup')))
  }

  /** A run on the environment's Python -- this place's clip, or another place's run -- which a rebuild waits for. */
  private usingEnvironment(): string | undefined {
    return this.registry
      ?.children('job')
      .map((child) => child.label)
      .find((label) => /^apogee (video (generate|compose)|voice say|train run)\b/.test(label))
  }

  /**
   * Install: the person said yes. The install, supervised -- its card, Stop
   * -- and once it is in, what was refused run again. While another install
   * runs -- this place's own, or another place's -- it starts nothing: what
   * runs is said and the consent kept, so Install works once it has finished
   * (GUI 6c); a rebuild waits for a run on the environment too, this place's
   * clip among them. A clip being made is no install: an add starts beside
   * it, as the old `rtc.install()` checks for a bootstrap alone.
   */
  install(): void {
    const consent = this.consent
    const binary = this.binary
    if (!consent || binary === undefined) return
    const own = this.isInstalling ? (this.installing?.command ?? 'apogee train setup') : undefined
    const waits = InstallConsent.waits(consent, own ?? this.installElsewhere(), this.usingEnvironment())
    if (waits !== undefined) {
      this.say?.(waits)
      return
    }
    this.consent = undefined
    this.dismissSheet?.()
    const retry = this.retryAfterInstall
    this.retryAfterInstall = undefined
    const installing = new RuntimeInstall(consent.arguments)
    this.installing = installing
    const job = this.starter.startJob(binary, consent.arguments, false)
    this.installJob = job
    this.tab = 'clips'
    this.showPlace?.()
    this.changed()
    void (async () => {
      for await (const update of job.updates) {
        installing.apply(update)
        this.changed()
      }
      this.installJob = undefined
      switch (installing.phase) {
        case 'installed':
          this.peftRefused = false
          this.say?.(`Installed: ${installing.command}${retry === undefined ? '' : ' -- making the clip again'}`)
          if (retry !== undefined) this.start(retry)
          break
        case 'failed': {
          const next = InstallConsent.afterFailed(consent.arguments, installing.failure, installing.progress.stderr)
          const elsewhere = this.installElsewhere()
          if (next && elsewhere !== undefined) this.say?.(InstallConsent.heldBack(next, elsewhere))
          else if (next) {
            this.installing = undefined
            this.ask(next, retry)
          } else this.say?.(`Not installed: ${(installing.failure ?? installing.command).split('\n')[0]}`)
          break
        }
        case 'stopped':
          this.say?.(`The install was stopped before it finished: ${installing.command} runs it again`)
          break
        case 'running':
          break
      }
      this.changed()
    })()
  }

  /** Not now: nothing is installed, and the run stays refused. */
  decline(): void {
    const consent = this.consent
    this.consent = undefined
    this.retryAfterInstall = undefined
    this.dismissSheet?.()
    this.say?.(`Not installed. ${consent ? InstallConsent.command(consent) : 'apogee train setup'} installs it whenever you want it`)
    this.changed()
  }

  /** The install card's button: Stop while it runs, Dismiss once its end is said. */
  installAct(): void {
    if (this.isInstalling) void this.installJob?.stop(10_000)
    else {
      this.installing = undefined
      this.changed()
    }
  }

  // MARK: - The player

  /** Play or pause the clip shown. */
  togglePlayer(): void {
    const clip = this.shownClip
    if (clip) this.playback.toggle(`clip:${clip.output}`, clip.duration ?? 0)
  }

  // MARK: - The face

  face(): VideoFace {
    const run = this.run
    const clip = this.shownClip
    const runFace = run ? this.runFace(run) : undefined
    const list: ClipRowFace[] = []
    if (run && runFace) {
      const failed = run.progress.status.case === 'failed'
      const stopped = run.progress.status.case === 'stopped'
      const warn = failed ? 'var(--status-error)' : stopped ? 'var(--status-warning)' : 'var(--status-accent)'
      list.push({ id: 'run', file: baseName(run.output), prompt: run.prompt, tag: failed ? 'FAILED' : stopped ? 'STOPPED' : 'RUNNING', tagColor: warn, meta: runFace.ended ? 'nothing written' : runFace.status, metaColor: warn, running: runFace.live, current: this.selected === 'run' })
    }
    for (const record of this.clips) {
      list.push({
        id: record.output,
        file: record.name,
        prompt: record.prompt,
        tag: record.preset === 'draft' ? 'DRAFT' : record.preset === 'final' ? 'FINAL' : `${record.steps ?? 0} STEPS`,
        tagColor: record.preset === 'final' ? 'var(--status-accent)' : 'var(--text-secondary)',
        meta: `${secs(record.duration ?? 0)} s · seed ${record.seed ?? 'not recorded'} · ${when(record.createdAt, this.clock.now)}`,
        metaColor: 'var(--text-tertiary)',
        running: false,
        current: clip !== undefined && clip.output === record.output && this.selected !== 'run',
      })
    }
    const count = this.compositionsCount()
    const consent = this.consent
    const words = consent ? installSheetWords(InstallConsent.set(consent), InstallConsent.rebuilds(consent) ? MediaLibrary.rebuildNote : undefined) : undefined
    const defaultModel = this.defaultModel
    return {
      tab: this.tab,
      tabs: [
        { tab: 'clips', label: 'Clips', count: String(this.clips.length), current: this.tab === 'clips' },
        { tab: 'compositions', label: 'Compositions', count: count === undefined ? '' : String(count), current: this.tab === 'compositions' },
      ],
      pipelineLine: defaultModel === undefined ? 'no default pipeline' : `${this.modelLabel(defaultModel)} · default`,
      newLabel: this.tab === 'clips' ? 'New clip' : 'New composition',
      canNew: this.tab === 'clips' || this.newComposition !== undefined,
      install: this.installFace() ?? { shown: false, state: 'running', headline: '', detail: '', command: '' },
      list,
      skipped: this.skipped.map((skip) => `${baseName(skip.file)}: ${skip.reason}`),
      empty: list.length === 0 && this.installing === undefined ? 'No clips yet. A clip is made from a prompt or a seed image, with its record beside it in ~/Movies/Apogee.' : '',
      showRun: runFace !== undefined && clip === undefined,
      run: runFace ? { shown: true, ...runFace } : { shown: false, file: '', prompt: '', live: false, ended: false, status: '', statusColor: '', phases: [], log: '', foot: '' },
      clip: clip ? { shown: true, ...this.clipFace(clip) } : { shown: false, ...VideoViewModel.noClip },
      form: this.formFace(),
      consent: consent && words ? { shown: true, title: words.title, said: consent.said, body: words.body, command: InstallConsent.command(consent), confirm: words.confirm, cancel: words.cancel } : { shown: false, title: '', said: '', body: '', command: '', confirm: '', cancel: '' },
    }
  }

  /** A store model as the form names it: its folder and engine, or the handle as given. */
  private modelLabel(handle: string): string {
    const model = MediaModel.named(handle, this.models)
    return model ? MediaModel.label(model, this.models) : handle
  }

  private installFace(): (InstallCardFace & { readonly shown: true }) | undefined {
    const installing = this.installing
    if (!installing) return undefined
    return { shown: true, state: installing.phase, headline: installing.headline, detail: installing.detail ?? '', command: installing.command }
  }

  /** No clip to show: its page drawn empty, said so by `shown`. */
  private static readonly noClip: ClipFace = {
    id: '',
    file: '',
    preset: '',
    presetColor: '',
    modeText: '',
    prompt: '',
    player: new Playback(() => 0).face('', 0, { width: 16, height: 9 }),
    actions: [],
    note: '',
    facts: [],
    loras: [],
    chain: [],
    files: [],
    command: '',
  }

  private runFace(run: ClipRun): RunFace {
    const progress = run.progress
    const status = progress.status.case
    const ended = status === 'stopped' || status === 'failed'
    const steps = progress.step?.of ?? run.plan.steps
    const phase = status === 'done' ? 3 : progress.frames !== undefined || progress.assembling !== undefined ? 2 : progress.ready !== undefined ? 1 : 0
    const step = phase > 1 ? steps : phase === 1 ? (progress.step?.done ?? 0) : 0
    const frames = progress.frames?.frames ?? progress.assembling
    const model = this.modelLabel(run.model)
    // What the run says: the CLI's notices, then its narration as its text mode says it -- the runtime loaded, a step at every tenth of the way, the frames and their assembly.
    const lines = [...progress.notices]
    const ready = progress.ready
    if (ready !== undefined) {
      const device = ready.device ?? 'an unstated device'
      const expect = ready.device === 'mps' ? 'expect minutes per clip on Apple silicon' : ready.device === 'cuda' ? 'expect a minute or more per clip on a GPU' : 'expect hours per clip on the CPU -- a GPU or Apple silicon is far faster'
      lines.push(`running on ${device}${ready.dtype ? ` (${ready.dtype})` : ''}${ready.mode === 'i2v' ? ', from the seed image' : ''} -- ${expect}`)
    }
    let tenth = -1
    for (let k = 1; k <= step && steps > 0; k += 1) {
      const now = Math.floor((k * 10) / steps)
      if (now !== tenth || k === steps) {
        tenth = now
        lines.push(`step ${k}/${steps}`)
      }
    }
    if (progress.frames) lines.push(`${progress.frames.frames} frames written, ${progress.frames.width ?? 0}x${progress.frames.height ?? 0}`)
    if (progress.assembling !== undefined) lines.push(`assembling ${progress.assembling} frames into the clip (ffmpeg)`)
    if (status === 'stopped') lines.push(`cancelled -- nothing was written (${run.output} untouched)`)
    if (progress.status.case === 'failed') lines.push(progress.status.message)
    const at = Math.min(phase, 3)
    const statusText = status === 'stopped' ? 'stopped by you' : status === 'failed' ? 'failed' : phase === 0 ? 'loading the pipeline' : phase === 1 ? `step ${step} of ${steps}` : 'assembling'
    const expectedFrames = frames ?? run.plan.frames
    return {
      file: baseName(run.output),
      prompt: run.prompt,
      live: !ended && status !== 'done',
      ended,
      status: statusText,
      statusColor: status === 'failed' ? 'var(--status-error)' : status === 'stopped' ? 'var(--status-warning)' : 'var(--status-accent)',
      phases: [
        { label: 'Load the pipeline', status: phaseStatus(0, at, ended), detail: model },
        { label: 'Denoise', status: phaseStatus(1, at, ended), detail: `step ${step} of ${steps}`, bar: steps > 0 ? Math.round((step / steps) * 100) : 0 },
        { label: 'Assemble the clip', status: phaseStatus(2, at, ended), detail: `${expectedFrames} frames · H.264` },
      ],
      log: lines.join('\n'),
      foot: ended ? 'Nothing was left behind: no clip, no record, and the working folder is gone.' : 'Stopping leaves the output untouched and writes no record. The working folder is removed however the run ends.',
    }
  }

  private clipFace(clip: ClipRecord): ClipFace {
    const frames = clip.frames ?? framesOf(clip.duration ?? 0, clip.fps ?? 0)
    const duration = clip.duration ?? 0
    const draft = clip.preset === 'draft'
    const actions: ClipFace['actions'] = [
      ...(draft ? [{ action: 'final' as const, label: 'Make the final', kind: 'btn-p' as const, title: 'This draft’s frame and seed at 50 steps' }] : []),
      { action: 'again', label: 'Run again…', kind: draft ? 'btn' : 'btn-p', title: 'Every setting from this record; change what you like' },
      { action: 'reroll', label: 'Re-roll the seed', kind: 'btn', title: 'The same settings with a new seed' },
      { action: 'extend', label: 'Extend…', kind: 'btn', title: 'Continue this clip from its last frame' },
      { action: 'finder', label: 'Show in Finder', kind: 'btn-g', title: '' },
    ]
    const device = clip.device
    const platform = device.platform ?? 'a platform not recorded'
    const ran = Object.entries(clip.versions)
      .filter(([key, value]) => key !== 'apogee' && value !== '')
      .sort(([a], [b]) => (a < b ? -1 : 1))
      .map(([key, value]) => `${key.replaceAll('_', '-')} ${value}`)
    const chain = [clip, ...this.chain(clip)].map((step, index) => {
      const base = `${step.steps ?? 0} steps · ${when(step.createdAt, this.clock.now)}`
      const how =
        step.parentMove === 'extend'
          ? `continues ${baseName(step.parentClip ?? '')} from its last frame, seed ${step.seed ?? 0} ${step.seedSource ?? ''} · ${when(step.createdAt, this.clock.now)}`
          : step.parentMove === 'from'
            ? `run again from ${baseName(step.parentDocument ?? `${step.parentClip ?? ''}.json`)}${step.seedSource === 'held' ? ', seed held' : `, seed ${step.seed ?? 0}`}, ${base}`
            : `${step.mode === 'i2v' && step.image ? `from ${tilde(step.image)}` : 'from text'}, seed ${step.seed ?? 0} ${step.seedSource ?? ''}, ${base}`
      return { label: step.name, how, go: index === 0 ? '' : step.output }
    })
    const stem = clip.name.replace(/\.mp4$/, '').replace(/-\d+$/, '')
    return {
      id: clip.output,
      file: clip.name,
      preset: draft ? 'DRAFT' : clip.preset === 'final' ? 'FINAL' : `${clip.steps ?? 0} STEPS`,
      presetColor: clip.preset === 'final' ? 'var(--status-accent)' : 'var(--text-secondary)',
      // A continuation by the CLI's own marker, `continued`: an extension, and a record run again from one, which continues that clip again.
      modeText: clip.parentContinued !== undefined ? `continues ${baseName(clip.parentContinued)}` : clip.mode === 'i2v' ? 'from an image' : 'from text',
      prompt: clip.prompt,
      player: this.playback.face(`clip:${clip.output}`, duration, {
        width: clip.width ?? 16,
        height: clip.height ?? 9,
        dims: `${clip.width ?? 0}×${clip.height ?? 0} · ${clip.fps ?? 0} fps`,
        badge: draft ? `draft · ${clip.steps ?? 16} steps` : '',
        right: `${frames} frames`,
        pausedLabel: `${secs(duration)} s · ${frames} frames`,
        playingLabel: (at) => `frame ${Math.min(frames, Math.floor(at * (clip.fps ?? 0)) + 1)} of ${frames}`,
      }),
      actions,
      note: platform !== thisPlatform ? `Recorded on ${platform} (${device.device ?? 'device not recorded'} · ${device.dtype ?? 'type not recorded'}). This is ${thisPlatform}, so run here, the same seed may not make the same clip.` : '',
      facts: [
        { label: 'Pipeline', value: `${clip.model}${clip.modelClass ? ` · ${clip.modelClass}` : ''}`, mono: false },
        { label: 'Made', value: clip.parentContinued !== undefined ? `from the last frame of ${baseName(clip.parentContinued)}` : clip.mode === 'i2v' && clip.image ? `from ${tilde(clip.image)}` : 'from text', mono: false },
        { label: 'Seed', value: `${clip.seed ?? 'not recorded'} · ${clip.seedSource ?? 'not recorded'}`, mono: true },
        { label: 'Frame', value: `${clip.width ?? 0}×${clip.height ?? 0} · ${clip.fps ?? 0} fps`, mono: false },
        { label: 'Length', value: `${secs(duration)} s · ${frames} frames`, mono: false },
        { label: 'Steps', value: `${clip.steps ?? 0}${draft ? ' · the draft preset' : clip.preset === 'final' ? ' · the final preset' : ''}`, mono: false },
        { label: 'Guidance', value: clip.guidance === undefined ? 'the pipeline’s own default' : String(clip.guidance), mono: false },
        { label: 'Device', value: `${device.device ?? 'not recorded'} · ${device.dtype ?? 'not recorded'}`, mono: false },
        { label: 'Versions', value: [MediaEngine.name(clip.engine), ...ran].join(' · '), mono: true },
        { label: 'Codec', value: clip.codec ?? 'not recorded', mono: false },
        { label: 'Built by', value: `apogee ${clip.versions.apogee ?? 'version not recorded'} · ${platform}`, mono: true },
        { label: 'Took', value: took(clip.seconds), mono: false },
      ],
      loras: clip.loras.map((entry, index) => ({ path: tilde(entry.path), scale: String(entry.scale ?? 1), sha: entry.sha256 ? `${entry.sha256.slice(0, 8)}…${entry.sha256.slice(-4)}` : 'not recorded', adapter: `lora${index + 1}` })),
      chain,
      files: [
        { path: clip.name, size: 'the clip' },
        { path: `${clip.name}.json`, size: 'the record' },
      ],
      command: `apogee video generate --from ~/Movies/Apogee/${clip.name}.json -o ~/Movies/Apogee/${MediaLibrary.freeName(this.context.taken, stem, 'mp4')}`,
    }
  }

  private formFace(): FormFace {
    const form = this.form
    const record = this.formRecord
    const context = this.context
    const errors = VideoForm.errors(form, context)
    const isFrom = form.mode === 'from' && record !== undefined
    const isExtend = form.mode === 'extend' && record !== undefined
    // Continuing by the CLI's own marker, as `VideoForm.told` reads it: a record run again from a continuation continues that clip again.
    const continuing = isExtend || (isFrom && record.parentContinued !== undefined)
    const continues = isExtend ? record.name : continuing ? baseName(record.parentContinued ?? '') : ''
    const dur = Number(form.dur)
    const fps = Number(form.fps)
    const sizes = frameSizes.some(([value]) => value === form.size) ? frameSizes : [[form.size, `${form.size.replace('x', ' × ')} · from the record`] as const, ...frameSizes]
    const choices = this.models.map((model) => ({ value: model.handle, label: `${MediaModel.label(model, this.models)}${model.handle === this.defaultModel ? ' · default' : ''}` }))
    if (form.pipeline !== '' && !choices.some((choice) => choice.value === form.pipeline)) choices.unshift({ value: form.pipeline, label: `${form.pipeline}${form.pipeline === this.defaultModel ? ' · default' : ''}` })
    const picked = MediaModel.named(form.pipeline, this.models)
    const engine = isFrom && form.pipeline === record.model ? record.engine : picked?.engine
    const running = this.isRunning
    const installing = this.isInstalling
    // The field shows what was typed, as typed -- emptied, it is empty, and the form asks for a name -- else the form's name for the file.
    const out = form.out ?? VideoForm.clipName(form, record, context.taken)
    return {
      heading: isFrom ? `From ${record.name}` : isExtend ? `Continue ${record.name}` : 'New clip',
      notNew: isFrom || isExtend,
      // What the CLI will say of where the run came from: drawn for a record run again or continued, as the old form draws it; a new clip's preset line the run says itself.
      told: isFrom || isExtend ? VideoForm.told(form, record, context).join('\n') : '',
      prompt: form.prompt,
      continuing,
      continues,
      image: !continuing && form.image !== undefined ? tilde(form.image) : '',
      canAddImage: !continuing && form.image === undefined,
      pipeline: form.pipeline,
      pipelines: choices,
      lockFrame: isExtend,
      preset: form.preset,
      presets: [
        { value: 'draft', label: 'Draft · 16' },
        { value: 'final', label: 'Final · 50' },
        { value: 'custom', label: form.preset === 'custom' ? `Custom · ${form.steps}` : 'Custom' },
      ],
      presetNote: form.preset === 'draft' ? 'A draft is 16 steps over the same frame and seed as its final, so a draft that moves right becomes its final in one click.' : form.preset === 'final' ? 'The final is 50 steps, the pipeline’s default.' : 'Steps set by hand. The record keeps them; no preset is named.',
      dur: form.dur,
      fps: form.fps,
      steps: form.steps,
      size: form.size,
      sizes: sizes.map(([value, label]) => ({ value, label })),
      framesText: dur > 0 && fps > 0 ? `${framesOf(dur, fps)} frames: ${form.dur} s at ${form.fps} fps, rounded to the pipeline’s 4k+1${isExtend ? ' · frame, rate and pipeline match the clip' : ''}` : '',
      seedMode: form.seedMode,
      seedModes: isFrom
        ? [
            { value: 'held', label: `Hold ${record.seed ?? ''}` },
            { value: 'given', label: 'Another' },
          ]
        : [
            { value: 'draw', label: 'Draw one' },
            { value: 'given', label: 'Give one' },
          ],
      seedGiven: form.seedMode === 'given',
      seed: form.seed,
      seedNote: isFrom ? (form.seedMode === 'held' ? 'Held from the record. With the same frame and steps, the same seed starts from the same noise.' : 'A new seed is a new take on the same settings.') : isExtend ? 'A continuation draws a new seed unless you give one.' : form.seedMode === 'draw' ? 'Drawn when the run starts, and kept in the record.' : 'Recorded as given.',
      loraCount: `${form.loras.length} of 4`,
      loras: form.loras.map((entry, index) => ({
        path: tilde(entry.path),
        name: baseName(entry.path),
        state: isFrom && !form.loraEdited && entry.sha256 ? `sha256 ${entry.sha256.slice(0, 8)}…${entry.sha256.slice(-4)} · held, checked by its hash` : `checked by its sha256 when the run starts · adapter lora${index + 1}`,
        scale: entry.scale,
      })),
      loraFull: form.loras.length >= 4,
      out,
      errors: errors.filter((error) => !error.startsWith('A prompt is required')),
      runsOn:
        engine === 'mlx'
          ? 'mlx-video in a Python child process, on MLX. Nothing leaves this Mac.'
          : engine === 'torch'
            ? 'diffusers in a Python child process, on mps at bfloat16. Nothing leaves this Mac.'
            : 'what its model’s directory is: diffusers for a pipeline, mlx-video for an MLX video model, in a Python child process. Nothing leaves this Mac.',
      expect: 'Minutes per clip on Apple silicon. Nothing here has been measured, so no time is quoted.',
      memory: picked?.bytes !== undefined ? `Its weights are ${sizeText(picked.bytes)} on disk, as the store counts them.` : 'The store doesn’t say how big its weights are.',
      command: VideoForm.command(form, record, context),
      canGenerate: errors.length === 0 && !running && !installing && this.binary !== undefined,
      goNote: this.binary === undefined ? 'apogee is not installed' : running && this.run ? `One clip at a time: ${baseName(this.run.output)} is generating.` : form.loras.length > 0 && this.peftRefused ? 'LoRAs need peft; Generate asks first.' : '',
    }
  }

  /** The folder's read and what runs reported, newest first. */
  private compose(): void {
    const scanned = this.scanned.filter((clip) => !this.landed.has(clip.output))
    this.clips = [...this.landed.values(), ...scanned].sort((a, b) => ((a.createdAt ?? '') > (b.createdAt ?? '') ? -1 : (a.createdAt ?? '') < (b.createdAt ?? '') ? 1 : 0))
  }
}
