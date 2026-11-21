import { JobProgress } from '../../../business/jobs/JobProgress'
import type { JobRunning, JobStarting } from '../../../business/jobs/SupervisedJob'
import { CaptionsAbility } from '../../../business/media/CaptionsAbility'
import { ComposeForm } from '../../../business/media/ComposeForm'
import { MediaModel } from '../../../business/media/MediaEngine'
import { baseName, MediaLibrary } from '../../../business/media/MediaRecords'
import type { FolderReading, Skipped } from '../../../business/media/MediaRecords'
import { narrationNeeds, ProjectLibrary, ProjectRecord } from '../../../business/media/Projects'
import type { ProjectScene } from '../../../business/media/Projects'
import type { CommandRunning } from '../../../business/reads/CommandRunning'
import { Field, ModelKind, ModelRow } from '../../../business/reads/Models'
import { ReadOutcome } from '../../../business/reads/ReadRunner'
import { JSONValue } from '../../../data/protocol/JSONValue'
import type { Clock } from '../../../infrastructure/clock/Clock'
import type { MediaPreferences } from '../../styles/Media/MediaPreferences'
import { secs, tilde, when } from '../../styles/Media/MediaWords'
import { clockText } from '../../styles/Media/Playback'
import type { Playback } from '../../styles/Media/Playback'
import type { PhaseFace, PhaseStatus } from '../../styles/Media/Phase'
import { Observable } from '../../styles/Observation/Observable'
import type { ChangeRowFace, ComposeChoice, ComposeFace, EditPaneFace, NewCompositionFace, ProjectPageFace, ProjectRowFace } from './ComposeFaces'

/** What the place reads of the clock: its ticks (the player's), its fixed now (a record's age), and each tick. */
export type ComposeClock = Pick<Clock, 'ticks' | 'now' | 'onTick'>

/** The voice the CLI speaks with when none is named (`af_heart`): the old command names `--voice` only for another. */
export const cliDefaultVoice = 'af_heart'
/** The most a brief holds, as the CLI's planner takes it. */
const briefLimit = 2000
/** The music bed and the assets folder the old form offers, since a browser picks no file. */
const sampleMusic = '/Users/someone/Music/beds/warm-kitchen.m4a'
const sampleAssets = '/Users/someone/Pictures/bakery/'
/** The bed the edit pane offers besides a project's own, in the folder the old form names. */
const otherBed = 'soft-piano.m4a'
const bedsFolder = '/Users/someone/Music/beds'

/** The new composition: its form, how far its plan is, and the plan the dry run printed. */
export interface ComposeDraft {
  readonly form: ComposeForm
  readonly stage: 'brief' | 'planning' | 'planned'
  readonly plan: ProjectRecord | undefined
  /** Save as, as typed; `undefined` is the free name for the brief. */
  readonly out: string | undefined
}

/** The edit pane's form for a project: the Mac's edit sheet's `form`, and the settings the old pane changes for the new video. */
export interface ComposeEdit {
  readonly output: string
  readonly form: ComposeForm
  readonly voice: string
  readonly model: string
  /** The bed by its file name; empty for none. */
  readonly music: string
  readonly captions: boolean
  readonly out: string | undefined
  readonly stage: 'idle' | 'previewing' | 'shown'
  readonly failure: string | undefined
}

/** The render going now: its project's video, the command, its progress. */
interface Render {
  readonly output: string
  readonly args: readonly string[]
  readonly progress: JobProgress
}

/** The planner a backend is, as the place says it. */
interface Planner {
  readonly name: string
  readonly kind: ModelKind | undefined
  /** Who bills it or whose subscription it is: `Anthropic`, `Claude`. */
  readonly provider: string
}

const providers: Readonly<Record<string, string>> = { anthropic: 'Anthropic', openai: 'OpenAI', google: 'Google', 'claude-cli': 'Claude', 'codex-cli': 'Codex', 'gemini-cli': 'Gemini' }

/** `video compose`'s arguments with the 52b stream asked for, before the brief's `--` (the Mac's `Argv.adding`). */
function streaming(args: readonly string[]): string[] {
  const at = args.indexOf('--')
  return at < 0 ? [...args, '--output-format', 'stream-json'] : [...args.slice(0, at), '--output-format', 'stream-json', ...args.slice(at)]
}

/** A word quoted as the old command line quotes it. */
const quoted = (text: string) => `"${text.replace(/"/g, '\\"')}"`

/** The CLI's word for a record that changed, as its diff says it (`compose.cpp`'s `diff_lines`). */
function changeWords(record: string): string {
  if (record === 'duration') return 'its length changed'
  if (record === 'seed') return 'a new seed'
  return `its ${record} changed`
}

/** A video's stem: its name without `.mp4`. */
const stemOf = (output: string) => baseName(output).replace(/\.mp4$/, '')

/**
 * Compositions (design 1l): the Mac app's `ComposeViewModel`, its names,
 * drawn as the old prototype's Compositions tab -- the list with the draft
 * and the passed-over files, a new composition planned on paper and then
 * rendered, a project's page (its status, Stop and Resume, its phases, the
 * stop note, its changes, the player, the timeline, the scenes, its files,
 * settings and chain) and the edit pane (an instruction, re-rolls and the
 * new video's settings, previewed on paper, then rendered as a new video).
 *
 * The project document is the progress: while a composition renders, the
 * place reads again the project the composer saves beside its video after
 * every step -- never an accounting of its own (the Mac's rule). Every run
 * is the command a person would type, a job on the shared job list; one
 * renders at a time, stopping keeps the finished scenes, and the same
 * command again renders only what the project lacks. It draws no install and
 * raises no consent (the document's default). Plain TypeScript: it never
 * imports React.
 */
export class ComposeViewModel extends Observable {
  projects: ProjectRecord[] = []
  /** The documents the scan passed over, and those the stand-in's list names. */
  skipped: Skipped[] = []
  /** The project shown, by its video, or `new` for the draft. */
  selected: string | undefined = undefined
  draft: ComposeDraft | undefined = undefined
  /** The edit pane's form, for the project shown. */
  editing: ComposeEdit | undefined = undefined
  /** The edit's preview: the edited project its dry run printed, while the form still makes its command. */
  preview: ProjectRecord | undefined = undefined
  voiceModels: MediaModel[] = []
  videoModels: MediaModel[] = []
  defaultVideo: string | undefined = undefined
  voices: string[] = []
  captions: CaptionsAbility | undefined = undefined
  checkRead = false

  /** The toast. */
  say: ((text: string) => void) | undefined = undefined
  /** The library's folders: where compositions are written, and read (the Mac's `folders`). */
  folders: () => readonly string[] = () => []
  /** What a scan of the person's folders passed over that the stand-in's folders cannot hold (design 1l): its hand-made list. */
  passedOver: () => readonly Skipped[] = () => []

  private binary: string | undefined = undefined
  private rows: ModelRow[] = []
  private kinds: Readonly<Record<string, ModelKind>> = {}
  private render: Render | undefined = undefined
  private job: JobRunning | undefined = undefined
  private planJob: JobRunning | undefined = undefined
  private previewJob: JobRunning | undefined = undefined
  private previewedArguments: readonly string[] | undefined = undefined
  /** The arguments of each composition's last run, to resume it exactly. */
  private readonly commands = new Map<string, readonly string[]>()
  /** What a Stop pressed here said of where the render stood, by its video, until it is resumed. */
  private readonly stopNotes = new Map<string, string>()
  /** Each edit rendered from the preview drawn beside its Render, by its video: kept in memory for this run of the app alone (GUI 6j). */
  private readonly previewedRenders = new Map<string, ProjectRecord>()

  constructor(
    private readonly runner: CommandRunning,
    private readonly starter: JobStarting,
    private readonly reader: FolderReading,
    private readonly clock: ComposeClock,
    /** The one player of the app's media, shared with Video and Voice (design 1k). */
    readonly playback: Playback,
    /** The voice and captions new compositions start from, the window's preferences (design 1l). */
    private readonly media: MediaPreferences,
  ) {
    super()
    this.playback.subscribe(() => this.changed())
    clock.onTick(() => {
      if (this.playback.on) this.changed()
    })
    // A preference changed elsewhere: drawn again. A fresh draft starts from it; an open one keeps its own, as the old `nc` does (main.html 6256).
    media.subscribe(() => this.changed())
    // Voice's Use this voice for compositions (1m) reaches the open draft too, as the old one sets `nc.voice` (7692).
    media.onUseForCompositions((voice) => {
      if (this.draft) this.draft = { ...this.draft, form: this.draft.form.with({ voice }) }
    })
  }

  connect(binary: string | undefined): void {
    this.binary = binary
    this.refresh()
  }

  /** Coming into view: the projects read again, and the store the forms offer. */
  async appeared(): Promise<void> {
    this.refresh()
    await this.readStore()
  }

  /** The projects, read again from the folders, and those passed over. */
  refresh(): void {
    const scan = ProjectLibrary.projects(this.folders(), this.reader)
    this.projects = scan.records
    this.skipped = [...scan.skipped, ...this.passedOver()]
    if (this.selected === undefined || (this.selected !== 'new' && !this.projects.some((project) => project.output === this.selected))) this.selected = this.projects[0]?.output
    this.changed()
  }

  /**
   * What the forms offer of the store: its voice and video models and the
   * backends that plan (`models list`), the default pipeline (`config get
   * models.default_video`), the voices each voice model speaks (its `models
   * info`), and whether this ffmpeg burns the captions (`check`'s row, the
   * CLI's 52e).
   */
  async readStore(): Promise<void> {
    const binary = this.binary
    if (binary === undefined) return
    const list = ReadOutcome.documentOf(await this.runner.read(binary, ['models', 'list']))
    if (list) {
      this.rows = ModelRow.list(list)
      this.kinds = ModelKind.byBackend(list)
      this.voiceModels = MediaModel.voiceModels(list)
      this.videoModels = MediaModel.videoModels(list)
    }
    const defaulted = await this.runner.command(binary, ['config', 'get', 'models.default_video'])
    if (defaulted.case === 'done') this.defaultVideo = defaulted.stdout.trim() || undefined
    const voices: string[] = []
    for (const model of this.voiceModels) {
      const info = ReadOutcome.documentOf(await this.runner.read(binary, ['models', 'info', model.handle]))
      const said = info ? (Field.list(info).find((field) => field.label === 'voices')?.value ?? '') : ''
      for (const line of said.split('\n')) for (const voice of (line.split(':')[1] ?? '').split(',').map((word) => word.trim())) if (voice !== '' && !voices.includes(voice)) voices.push(voice)
    }
    this.voices = voices
    const check = ReadOutcome.documentOf(await this.runner.report(binary, ['check']))
    if (check) {
      this.captions = CaptionsAbility.of(check.value)
      this.checkRead = true
    }
    if (this.draft && this.draft.form.voiceModel === '' && this.voiceModels.length > 1) this.draft = { ...this.draft, form: this.draft.form.with({ voiceModel: this.voiceModels[0]?.handle ?? '' }) }
    this.changed()
  }

  /** A project, by its video, or the draft (`new`). */
  select(output: string): void {
    this.selected = output
    this.changed()
  }

  get selectedProject(): ProjectRecord | undefined {
    return this.selected === undefined ? undefined : this.projects.find((project) => project.output === this.selected)
  }

  /** The project an edit came from, when it is in the library. */
  parent(of: ProjectRecord): ProjectRecord | undefined {
    const parent = of.parent
    return parent === undefined ? undefined : this.projects.find((project) => project.output === parent || `${project.output}.json` === parent)
  }

  /** What the shell's sheet would hold in the Mac: the new composition, or the edit of the project shown. */
  get sheet(): { readonly case: 'new' } | { readonly case: 'edit'; readonly project: ProjectRecord } | undefined {
    if (this.selected === 'new' && this.draft) return { case: 'new' }
    const project = this.selectedProject
    return project ? { case: 'edit', project } : undefined
  }

  // MARK: - The player

  /** The player's key for a project. */
  private key(project: ProjectRecord): string {
    return `proj:${project.output}`
  }

  /** Play or pause the project shown. */
  toggle(): void {
    const project = this.selectedProject
    if (project) this.playback.toggle(this.key(project), this.timing(project).total)
  }

  /** Play the project shown from its scene `index` (0-based). */
  seek(index: number): void {
    const project = this.selectedProject
    const at = project ? this.timing(project).scenes[index]?.start : undefined
    if (project && at !== undefined) this.playback.seek(this.key(project), at)
  }

  /** The project shown paused `at` seconds in: where a preset's frame stands. */
  hold(at: number): void {
    const project = this.selectedProject
    if (project) this.playback.hold(this.key(project), at)
  }

  // MARK: - Making

  /** New composition: the draft opened -- kept as it was if there is one, else fresh: the recorded brief, the chosen voice, captioned as the window says (the old `freshCompose`). */
  newComposition(): void {
    this.draft ??= {
      form: new ComposeForm().with({ brief: this.startingBrief, voice: this.media.voice, captions: this.media.captions, voiceModel: this.voiceModels.length > 1 ? (this.voiceModels[0]?.handle ?? '') : '' }),
      stage: 'brief',
      plan: undefined,
      out: undefined,
    }
    this.selected = 'new'
    this.changed()
  }

  /**
   * The brief a new composition starts from, handed in (design 1l): the one
   * the plan was recorded from (`compose-planned.jsonl`), where the old form
   * started from its own sourdough brief, which nothing recorded.
   */
  startingBrief = ''

  /** The draft changed: a new brief or assets drop its plan, as the old form does; anything else keeps it. */
  editDraft(patch: Partial<Pick<ComposeForm, 'brief' | 'assets' | 'music' | 'captions' | 'voice' | 'model' | 'voiceModel' | 'planner'>> & { readonly out?: string }): void {
    const draft = this.draft
    if (!draft) return
    const { out, ...fields } = patch
    const replans = 'brief' in fields || 'assets' in fields
    this.draft = {
      form: draft.form.with(fields),
      stage: replans && draft.stage === 'planned' ? 'brief' : draft.stage,
      plan: replans ? undefined : draft.plan,
      out: 'out' in patch ? out : draft.out,
    }
    this.changed()
  }

  /** Add a sound file…: the old form's sample bed, since a browser picks no file. */
  addMusic(): void {
    this.editDraft({ music: sampleMusic })
  }

  dropMusic(): void {
    this.editDraft({ music: undefined })
  }

  /** Add a folder…: the old form's sample folder. */
  addAssets(): void {
    this.editDraft({ assets: sampleAssets })
  }

  dropAssets(): void {
    this.editDraft({ assets: undefined })
  }

  /** The names the folders hold, and the videos runs are writing: what a new name must miss. */
  private taken(): string[] {
    const names = this.folders().flatMap((folder) => this.reader.files(folder).map((file) => file.name))
    return this.render ? [...names, baseName(this.render.output)] : names
  }

  /** Where the draft's composition is written: Save as, else a free name for its brief in the first folder (the Mac's `plannedOutput`). */
  private draftOutput(draft: ComposeDraft): string {
    const folder = this.folders()[0] ?? ''
    const typed = draft.out?.trim()
    if (typed !== undefined && typed !== '') return `${folder}/${typed}`
    const stem = MediaLibrary.stem(draft.form.brief)
    return `${folder}/${MediaLibrary.freeName(this.taken(), stem === '' ? 'composition' : stem, 'mp4', draft.form.captions ? ['srt'] : [])}`
  }

  /** The draft's form as it runs: the voice, the pipeline and the planner named only where they are not what the CLI takes unnamed, the voice model only where the store holds two or more. */
  private draftForm(draft: ComposeDraft): ComposeForm {
    const form = draft.form
    return form.with({
      voice: form.voice === cliDefaultVoice ? '' : form.voice,
      model: form.model === '' || form.model === this.defaultVideo ? '' : form.model,
      planner: form.planner === '' || form.planner === this.chatRole ? '' : form.planner,
      voiceModel: this.voiceModels.length > 1 ? form.voiceModel || (this.voiceModels[0]?.handle ?? '') : '',
    })
  }

  /** Whether a render runs now. */
  get isRendering(): boolean {
    return this.job !== undefined
  }

  /**
   * The draft composed: planned first and shown (`planFirst`, its dry run,
   * 52c) -- Plan, and Plan again -- or its plan rendered by the same command
   * without `--dry-run`. One composition renders at a time.
   */
  compose(planFirst = false): void {
    const draft = this.draft
    const binary = this.binary
    if (!draft || binary === undefined) return
    const output = this.draftOutput(draft)
    const args = this.draftForm(draft).arguments(output, planFirst)
    if (!args) return
    if (planFirst) {
      if (this.planJob) return
      // The plan is kept beside the video it names: Save as is that name from here on, so Render renders the plan kept there.
      this.draft = { ...draft, stage: 'planning', plan: undefined, out: draft.out ?? baseName(output) }
      void this.plan(binary, args, output, draft.form)
      this.changed()
      return
    }
    if (this.job) {
      this.say?.('One composition renders at a time. Wait for the other, or stop it.')
      return
    }
    this.draft = undefined
    this.previewedRenders.delete(output)
    this.start(binary, args, output)
  }

  /**
   * The dry run: its plan, the project it prints, kept in the draft -- the
   * old `nc.plan` -- and said planned. A brief or assets typed while it
   * planned make it the earlier words' plan: dropped and said, as the edit
   * pane drops a preview for a changed form, so Render never runs a brief no
   * plan was shown for (the old planned the words in the box when it landed,
   * which a recording cannot).
   */
  private async plan(binary: string, args: readonly string[], output: string, planned: ComposeForm): Promise<void> {
    const job = this.starter.startJob(binary, streaming(args), true)
    this.planJob = job
    const progress = new JobProgress()
    for await (const update of job.updates) progress.apply(update)
    this.planJob = undefined
    const draft = this.draft
    if (!draft) return
    const value = progress.documents.at(-1)
    const plan = value === undefined ? undefined : ProjectRecord.read(value)
    if (draft.form.brief !== planned.brief || draft.form.assets !== planned.assets) {
      this.draft = { ...draft, stage: 'brief', plan: undefined }
      if (progress.status.case === 'done') this.say?.('The brief changed while it was planned: plan it again')
      else if (progress.status.case === 'failed') this.say?.(`That didn't work: ${progress.status.message}`)
    } else if (progress.status.case === 'done' && plan) {
      this.draft = { ...draft, stage: 'planned', plan }
      this.say?.(`Planned ${baseName(output)}: nothing is rendered yet -- Render it, on its page, renders the plan`)
    } else {
      this.draft = { ...draft, stage: 'brief', plan: undefined }
      if (progress.status.case === 'failed') this.say?.(`That didn't work: ${progress.status.message}`)
    }
    this.changed()
  }

  /** Renders a planned composition, or resumes a stopped one: its last command again, which renders only what the project lacks. */
  resume(project: ProjectRecord): void {
    const binary = this.binary
    if (binary === undefined) return
    if (this.job) {
      this.say?.('One composition renders at a time. Stop the other first.')
      return
    }
    this.stopNotes.delete(project.output)
    this.start(binary, this.commands.get(project.output) ?? ComposeViewModel.resumeArguments(project), project.output)
  }

  /** For a project the app did not run: its output and its brief, after `--` -- the composer reads the rest from the project it keeps. */
  static resumeArguments(project: ProjectRecord): string[] {
    return ['video', 'compose', '-o', project.output, '--', project.brief]
  }

  /** The render: a job on the shared list, its project read again beside its video after each thing it says. */
  private start(binary: string, args: readonly string[], output: string): void {
    const progress = new JobProgress()
    this.commands.set(output, args)
    this.render = { output, args, progress }
    this.selected = output
    const job = this.starter.startJob(binary, streaming(args), true)
    this.job = job
    this.reread(output)
    this.changed()
    void (async () => {
      for await (const update of job.updates) {
        progress.apply(update)
        this.reread(output)
        this.changed()
      }
      this.job = undefined
      this.render = undefined
      this.refresh()
      const status = progress.status
      if (status.case === 'done') {
        const made = this.projects.find((project) => project.output === output)
        this.say?.(`${baseName(output)} is ready${made?.captionsFile !== undefined && made.captionsBurned !== true ? ', its captions beside it as an .srt' : ''}`)
      } else if (status.case === 'failed') this.say?.(`That didn't work: ${status.message}`)
      this.changed()
    })()
  }

  /** The project beside `output`, as the composer last saved it: what the list and the page draw. */
  private reread(output: string): void {
    const saved = ProjectLibrary.beside(output, this.reader)
    if (!saved) return
    const at = this.projects.findIndex((project) => project.output === output)
    this.projects = at < 0 ? [saved, ...this.projects] : this.projects.map((project, index) => (index === at ? saved : project))
  }

  /** Stop, on the render, as Ctrl-C does: every finished scene stays, and where it stood is said on its page. */
  async stop(): Promise<void> {
    const render = this.render
    const job = this.job
    if (!render || !job) return
    const scene = render.progress.scene
    const step = render.progress.step
    this.stopNotes.set(
      render.output,
      scene?.state === 'rendering'
        ? `Stopped at scene ${scene.scene}’s clip, step ${step?.done ?? 0} of ${step?.of ?? 50}. Every finished scene is kept, and resuming renders only what’s missing. The partial clip isn’t kept: scene ${scene.scene} renders again with its seed held.`
        : 'Stopped. Every finished scene is kept, and resuming renders only what’s missing.',
    )
    await job.stop()
  }

  // MARK: - Editing

  /** The edit pane's form for `project`, fresh: the old `freshEdit` -- Discard. */
  edit(project: ProjectRecord): void {
    this.editing = this.freshEdit(project)
    this.preview = undefined
    this.previewedArguments = undefined
    this.changed()
  }

  private freshEdit(project: ProjectRecord): ComposeEdit {
    return { output: project.output, form: new ComposeForm().with({ from: project.output }), voice: project.voice ?? cliDefaultVoice, model: project.model ?? '', music: project.music === undefined ? '' : baseName(project.music), captions: project.captions, out: undefined, stage: 'idle', failure: undefined }
  }

  /** The edit pane's form for the project shown. */
  private currentEdit(): ComposeEdit | undefined {
    const project = this.selectedProject
    if (!project) return undefined
    return this.editing?.output === project.output ? this.editing : this.freshEdit(project)
  }

  /** The edit changed: any change drops the preview drawn for the earlier form, as the old pane does. */
  editForm(patch: Partial<Pick<ComposeEdit, 'voice' | 'model' | 'music' | 'captions'>> & { readonly instruction?: string; readonly out?: string }): void {
    const edit = this.currentEdit()
    if (!edit) return
    const { instruction, out, ...settings } = patch
    const form = instruction === undefined ? edit.form : edit.form.with({ instruction })
    // Save as alone keeps the preview, as the old pane's Save as does; anything else is another edit.
    const another = instruction !== undefined || Object.keys(settings).length > 0
    this.editing = { ...edit, ...settings, form, out: 'out' in patch ? out : edit.out, ...(another ? { stage: 'idle' as const, failure: undefined } : {}) }
    if (another) {
      this.preview = undefined
      this.previewedArguments = undefined
    }
    this.changed()
  }

  /** Re-roll on a scene (its number): a new seed for it, everything else held. */
  toggleRedo(scene: number): void {
    const edit = this.currentEdit()
    if (!edit) return
    const redo = new Set(edit.form.redo)
    if (redo.has(scene)) redo.delete(scene)
    else redo.add(scene)
    this.editing = { ...edit, form: edit.form.with({ redo }), stage: 'idle', failure: undefined }
    this.preview = undefined
    this.previewedArguments = undefined
    this.changed()
  }

  /** Where the edit's new video is written: Save as, else `<name>-edit.mp4`, free (the Mac's `plannedOutput`). */
  private editOutput(edit: ComposeEdit): string {
    const folder = this.folders()[0] ?? ''
    const typed = edit.out?.trim()
    if (typed !== undefined && typed !== '') return `${folder}/${typed}`
    return `${folder}/${MediaLibrary.freeName(this.taken(), `${stemOf(edit.output)}-edit`, 'mp4', edit.captions ? ['srt'] : [])}`
  }

  /** The edit's form as it runs: the new video's settings passed only where they change the project's. */
  private editArguments(edit: ComposeEdit, project: ProjectRecord, dryRun: boolean): string[] | undefined {
    const form = edit.form.with({
      voice: edit.voice !== (project.voice ?? cliDefaultVoice) ? edit.voice : '',
      model: edit.model !== (project.model ?? '') ? edit.model : '',
      music: edit.music !== (project.music === undefined ? '' : baseName(project.music)) && edit.music !== '' ? `${bedsFolder}/${edit.music}` : undefined,
      captions: edit.captions || !project.captions,
    })
    return form.arguments(this.editOutput(edit), dryRun)
  }

  /**
   * The edit planned on paper: `--dry-run`, its project printed as JSON, its
   * diff from its own comparison (52c). A run beside the render, as the old
   * pane previews while another composition renders; Stop reaches it.
   */
  async previewEdit(): Promise<void> {
    const edit = this.currentEdit()
    const project = this.selectedProject
    const binary = this.binary
    if (!edit || !project || binary === undefined || this.previewJob || project.status !== 'done') return
    const args = this.editArguments(edit, project, true)
    if (!args) return
    this.editing = { ...edit, stage: 'previewing', failure: undefined }
    this.preview = undefined
    this.changed()
    const job = this.starter.startJob(binary, [...args, '--output-format', 'json'], false)
    this.previewJob = job
    const progress = new JobProgress()
    const lines: string[] = []
    let detail: string | undefined
    for await (const update of job.updates) {
      if (update.case === 'said') lines.push(update.line)
      if (update.case === 'ended') detail = update.end.detail ?? update.end.stderrTail.lastLine
      progress.apply(update)
    }
    this.previewJob = undefined
    const now = this.editing
    // Its pane changed meanwhile -- another project, Discard, or the words typed again: the plan is the earlier form's, and dropped.
    if (now?.output !== edit.output || now.stage !== 'previewing') {
      this.changed()
      return
    }
    if (progress.status.case === 'stopped') {
      this.editing = { ...now, stage: 'idle' }
      this.say?.('Preview stopped: nothing was changed')
      this.changed()
      return
    }
    const value = JSONValue.parse(lines.join('\n'))
    const planned = value === undefined ? undefined : ProjectRecord.read(value)
    if (progress.status.case === 'done' && planned) {
      this.preview = planned
      this.previewedArguments = this.editArguments(now, project, false)
      this.editing = { ...now, stage: 'shown' }
    } else {
      this.editing = { ...now, stage: 'idle', failure: progress.status.case === 'failed' ? progress.status.message : (detail ?? 'the edit could not be planned') }
    }
    this.changed()
  }

  /** The preview's Stop. */
  async stopPreview(): Promise<void> {
    await this.previewJob?.stop()
  }

  /** Render as a new video: the same command without the dry run, which plans the edit again; the preview drawn beside it kept for the page. One composition renders at a time. */
  renderEdit(): void {
    const edit = this.currentEdit()
    const project = this.selectedProject
    const binary = this.binary
    if (!edit || !project || binary === undefined || this.preview === undefined) return
    if (this.job) {
      this.say?.('One composition renders at a time. Wait for the other, or stop it.')
      return
    }
    const args = this.editArguments(edit, project, false)
    if (!args) return
    const output = this.editOutput(edit)
    const plansAgain = edit.form.instruction.trim() !== ''
    if (plansAgain && this.previewedArguments?.join('\u0000') === args.join('\u0000')) this.previewedRenders.set(output, this.preview)
    else this.previewedRenders.delete(output)
    this.preview = undefined
    this.previewedArguments = undefined
    this.editing = undefined
    this.start(binary, args, output)
  }

  /** What the monitor names the render going now (design 1l): its arguments, the video it is making -- where its Open shows it -- and its name there. */
  get rendering(): { readonly arguments: readonly string[]; readonly output: string; readonly name: string; readonly runtime: string | undefined } | undefined {
    const render = this.render
    if (!render) return undefined
    const ready = render.progress.ready
    return { arguments: streaming(render.args), output: render.output, name: `${baseName(render.output)} · rendering`, runtime: ready?.device === undefined ? undefined : [ready.device, ...(ready.dtype !== undefined ? [ready.dtype] : [])].join(' · ') }
  }

  // MARK: - What it draws

  /** The backend the chat role names: what plans unless another is picked. */
  private get chatRole(): string | undefined {
    return this.rows.find((row) => !ModelRow.isStored(row) && row.roles.split(',').map((role) => role.trim()).includes('chat'))?.backend
  }

  private planner(name: string | undefined): Planner {
    const backend = name ?? ''
    const row = this.rows.find((candidate) => candidate.backend === backend)
    return { name: backend === '' ? 'the planner' : backend, kind: this.kinds[backend], provider: row ? (providers[row.backendType] ?? row.backendType) : '' }
  }

  /**
   * A scene's length and where it starts, and the whole: a clip's as
   * measured -- unless its narration, spoken, now needs more, when the CLI
   * renders it again at the scene's length -- else the plan's, room made for a
   * narration spoken (the CLI's `scene_length`).
   */
  timing(project: ProjectRecord): { readonly scenes: ReadonlyArray<{ readonly start: number; readonly length: number }>; readonly total: number } {
    const crossfade = project.crossfade ?? 0.5
    let at = 0
    const scenes = project.scenes.map((scene, index) => {
      const planned = scene.seconds ?? 0
      const measured = scene.voiceFile === undefined || ProjectRecord.keepsClip(scene, crossfade) ? scene.clipSeconds : undefined
      const length = measured ?? (scene.voiceSeconds !== undefined ? Math.min(10, Math.max(2, planned, narrationNeeds(scene.voiceSeconds, crossfade))) : planned)
      const start = at
      at += length - (index < project.scenes.length - 1 ? crossfade : 0)
      return { start, length }
    })
    return { scenes, total: Math.round(at * 10) / 10 }
  }

  /** The first scene a stopped project has not made: where it stopped. */
  private stoppedAt(project: ProjectRecord): { readonly scene: number; readonly clip: boolean } {
    const unspoken = project.scenes.find((scene) => scene.voiceFile === undefined)
    if (unspoken) return { scene: unspoken.index, clip: false }
    const unmade = project.scenes.find((scene) => !ProjectRecord.isMade(scene, project.crossfade ?? 0.5))
    return { scene: unmade?.index ?? project.scenes.length, clip: unmade !== undefined }
  }

  private status(project: ProjectRecord): { readonly text: string; readonly color: string } {
    const rendering = this.render?.output === project.output || project.status === 'rendering'
    if (rendering) return { text: 'rendering', color: 'var(--status-accent)' }
    if (project.status === 'cancelled' || project.status === 'failed') return { text: `${project.status === 'failed' ? 'failed' : 'stopped'} at scene ${this.stoppedAt(project).scene} · resumable`, color: project.status === 'failed' ? 'var(--status-error)' : 'var(--status-warning)' }
    if (project.status === 'planned') return { text: 'planned · nothing rendered', color: 'var(--text-tertiary)' }
    return { text: `rendered · ${Math.round(this.timing(project).total)} s · ${when(project.created, this.clock.now)}`, color: 'var(--text-secondary)' }
  }

  private row(project: ProjectRecord): ProjectRowFace {
    const status = this.status(project)
    return { output: project.output, title: project.title, file: project.name, tag: project.edits.length > 0 ? `EDIT ${project.edits.length}` : '', status: status.text, statusColor: status.color, rendering: status.text === 'rendering', current: this.selected === project.output }
  }

  /** The pipelines a form offers: the store's video models, the default marked, and a project's own where the store lacks it. */
  private pipelines(current: string): ComposeChoice[] {
    const choices = this.videoModels.map((model) => ({ value: model.handle, label: `${MediaModel.label(model, this.videoModels)}${model.handle === this.defaultVideo ? ' · default' : ''}` }))
    return current === '' || choices.some((choice) => choice.value === current) ? choices : [{ value: current, label: current }, ...choices]
  }

  /** The voices a form offers: each its voice models speak, the CLI's default marked, and the form's own where they lack it. */
  private voiceChoices(current: string): ComposeChoice[] {
    const voices = this.voices.includes(current) || current === '' ? this.voices : [current, ...this.voices]
    return voices.map((voice) => ({ value: voice, label: voice === cliDefaultVoice ? `${voice} · default` : voice }))
  }

  /** A pipeline as the page names it: its folder and engine where the store holds it, else its handle. */
  private pipelineName(handle: string | undefined): string {
    if (handle === undefined) return 'the default'
    const model = this.videoModels.find((candidate) => candidate.handle === handle)
    return model ? MediaModel.label(model, this.videoModels) : handle
  }

  /** The words and color the captions setting says, by what `check` says this ffmpeg can do. */
  private get burns(): boolean {
    return this.captions?.burns === true
  }

  face(): ComposeFace {
    const isNew = this.selected === 'new' && this.draft !== undefined
    const draft = this.draft
    return {
      draft: {
        shown: draft !== undefined,
        current: isNew,
        meta: draft === undefined ? '' : draft.stage === 'planned' ? 'planned · nothing rendered' : draft.stage === 'planning' ? 'planning…' : 'a brief, not planned yet',
      },
      isNew,
      list: this.projects.map((project) => this.row(project)),
      skipped: this.skipped.map((skip) => `${baseName(skip.file)}: ${skip.reason}`),
      paneLabel: isNew ? 'Composition settings' : 'Make changes',
      fresh: this.freshFace(),
      page: this.pageFace(isNew ? undefined : this.selectedProject),
      edit: this.editFace(isNew ? undefined : this.selectedProject),
    }
  }

  private freshFace(): NewCompositionFace {
    const draft = this.draft
    const empty: NewCompositionFace = { brief: '', count: '', countColor: '', planning: false, planningText: '', cantPlan: true, planLabel: 'Plan', planNote: '', planned: false, title: '', lengthText: '', scenes: [], spend: [], out: '', planner: '', planners: [], plannerNote: '', plannerColor: '', pipeline: '', pipelines: [], voice: '', voices: [], hasVoiceModel: false, voiceModel: '', voiceModels: [], music: '', assets: '', assetsMeta: '', captions: true, captionsLine: '', captionsRemedy: '', command: '' }
    if (!draft) return empty
    const form = draft.form
    const brief = form.brief
    const output = this.draftOutput(draft)
    const out = baseName(output)
    const stem = stemOf(output)
    const plannerName = form.planner === '' ? this.chatRole : form.planner
    const planner = this.planner(plannerName)
    const api = planner.kind === 'api'
    const plan = draft.stage === 'planned' ? draft.plan : undefined
    const planBy = this.planner(plan?.planner)
    const crossfade = plan?.crossfade ?? 0.5
    const planTotal = plan ? Math.round((plan.scenes.reduce((sum, scene) => sum + (scene.seconds ?? 0), 0) - crossfade * Math.max(0, plan.scenes.length - 1)) * 10) / 10 : 0
    const taken = plan ? plan.scenes.filter(ProjectRecord.takesAsItIs).length : 0
    const runs = this.draftForm(draft)
    const parts = ['apogee video compose', quoted(brief.trim() || '…')]
    if (form.music !== undefined) parts.push(`--music ${tilde(form.music)}`)
    if (form.assets !== undefined) parts.push(`--assets ${tilde(form.assets)}`)
    if (!form.captions) parts.push('--no-captions')
    if (runs.voice !== '') parts.push(`--voice ${runs.voice}`)
    if (runs.voiceModel !== '') parts.push(`--voice-model ${runs.voiceModel}`)
    if (runs.model !== '') parts.push(`-m ${runs.model}`)
    if (runs.planner !== '') parts.push(`--planner ${runs.planner}`)
    parts.push(`-o ${tilde(output)}`)
    const kindWords = (kind: ModelKind | undefined, provider: string) => (kind === 'api' ? ' · API, billed for one call' : kind === 'subscription' ? ` · through your ${provider} subscription` : kind === 'local' ? ' · on this Mac' : '')
    return {
      brief,
      count: `${brief.length.toLocaleString('en-US')} / 2,000`,
      countColor: brief.length > briefLimit ? 'var(--status-error)' : 'var(--text-tertiary)',
      planning: draft.stage === 'planning',
      planningText: `planning the scenes with ${planner.name} · one structured call, thinking off`,
      cantPlan: brief.trim() === '' || brief.length > briefLimit || this.binary === undefined,
      planLabel: draft.stage === 'planned' ? 'Plan again' : 'Plan',
      planNote: draft.stage === 'planned' ? '' : api ? `The brief goes to ${planner.provider} for one billed call. Nothing renders yet.` : 'Planning renders nothing. You see the scenes and what they take first.',
      planned: plan !== undefined,
      title: plan?.title ?? '',
      lengthText: plan ? `${plan.scenes.length} scenes · about ${Math.round(planTotal)} s of video` : '',
      scenes: (plan?.scenes ?? []).map((scene) => ({ n: String(scene.index), narration: scene.narration, shows: this.shows(scene), secs: `${secs(scene.seconds ?? 0)} s` })),
      spend: plan
        ? [
            { label: 'PLAN', value: `planned by ${planBy.name}${kindWords(planBy.kind, planBy.provider)} · ${plan.attempts ?? 1} attempt${(plan.attempts ?? 1) === 1 ? '' : 's'}`, color: planBy.kind === 'api' ? 'var(--model-api)' : 'var(--text-primary)' },
            { label: 'SPEND', value: `${plan.scenes.length} narration runs (faster than real time), then ${plan.scenes.length - taken} clip runs at 1280×704, 50 steps. Each clip takes minutes on Apple silicon.${taken ? ` ${taken} asset taken as it is.` : ''}`, color: 'var(--text-primary)' },
            { label: 'CAPTIONS', value: !form.captions ? 'off' : this.burns ? 'burned into the frames' : `kept beside the video as ${stem}.srt; this ffmpeg has no libass to burn them`, color: 'var(--text-secondary)' },
            { label: 'MUSIC', value: form.music !== undefined ? `${baseName(form.music)}, looped, lowered and ducked under the narration` : 'none', color: 'var(--text-secondary)' },
          ]
        : [],
      out: draft.out ?? out,
      planner: plannerName ?? '',
      planners: ModelRow.chatBackends(this.rows).map((name) => {
        const kind = this.kinds[name]
        return { value: name, label: `${name}${kind === 'api' ? ' · API, billed' : kind === 'subscription' ? ' · subscription' : ''}` }
      }),
      plannerNote: api ? `One structured call to ${planner.provider}, thinking off, billed per use. The brief is sent; the video is made here.` : planner.kind === 'subscription' ? `One structured call through your ${planner.provider} subscription. The brief is sent; the video is made here.` : 'One structured call on this Mac, thinking off. The chat model plans unless you pick another.',
      plannerColor: api ? 'var(--model-api)' : 'var(--text-tertiary)',
      pipeline: form.model === '' ? (this.defaultVideo ?? '') : form.model,
      pipelines: this.pipelines(form.model === '' ? (this.defaultVideo ?? '') : form.model),
      voice: form.voice,
      voices: this.voiceChoices(form.voice),
      hasVoiceModel: this.voiceModels.length > 1,
      voiceModel: runs.voiceModel,
      voiceModels: this.voiceModels.map((model) => ({ value: model.handle, label: MediaModel.label(model, this.voiceModels) })),
      music: form.music === undefined ? '' : baseName(form.music),
      assets: form.assets === undefined ? '' : tilde(form.assets),
      assetsMeta: form.assets === sampleAssets ? '2 files: crumb-close.jpg, oven-door.mov' : '',
      captions: form.captions,
      captionsLine: !form.captions || !this.checkRead ? '' : this.captions === undefined ? 'This apogee’s check doesn’t say whether its ffmpeg burns captions in: the composition’s page says once it renders.' : this.captions.burns ? 'Captions will be burned into the frames: this ffmpeg has libass.' : `Captions will be kept beside the video as ${stem}.srt, not burned in: ${CaptionsAbility.reason(this.captions)}.`,
      captionsRemedy: form.captions && this.captions !== undefined && !this.captions.burns && this.captions.remedy !== undefined ? `To burn them in: ${this.captions.remedy}` : '',
      command: parts.join(' '),
    }
  }

  /** What a scene shows: its prompt, from an asset where it starts from a still, or the asset taken as it is (the CLI's rule, by its extension). */
  private shows(scene: ProjectScene): string {
    if (ProjectRecord.takesAsItIs(scene)) return `takes ${scene.asset} as it is`
    return `shows: ${scene.visual}${scene.asset !== undefined ? ` (from ${scene.asset})` : ''}`
  }

  private pageFace(project: ProjectRecord | undefined): ProjectPageFace {
    const silent = this.playback.face('', 0, { width: 16, height: 9 })
    if (!project) return { shown: false, title: '', tag: '', meta: '', brief: '', status: '', statusColor: '', rendering: false, stopped: false, showProgress: false, phases: [], stopNote: '', isEdit: false, changes: [], differs: '', showPlayer: false, player: silent, timeline: { pictures: [], narration: [], cues: [], bed: false, head: 0, legend: '', end: '', note: '' }, scenes: [], folder: '', files: [], facts: [], chain: [], file: '' }
    const render = this.render?.output === project.output ? this.render : undefined
    const progress = render?.progress
    const n = project.scenes.length
    const times = this.timing(project)
    const total = times.total
    const crossfade = project.crossfade ?? 0.5
    // A clip is made once the composer keeps it: one carried over, or cut short, for a narration that now needs more is rendered again (the CLI's clip pass).
    const kept = (scene: ProjectScene) => ProjectRecord.keepsClip(scene, crossfade)
    const spoken = project.scenes.filter((scene) => scene.voiceFile !== undefined).length
    const made = project.scenes.filter(kept).length
    const done = project.status === 'done'
    const stopped = render === undefined && (project.status === 'cancelled' || project.status === 'failed')
    const state = progress?.scene?.state
    const sceneNow = progress?.scene?.scene
    const step = progress?.step?.done ?? 0
    const assembling = render !== undefined && spoken === n && made === n && project.status === 'rendering'
    const stem = stemOf(project.output)
    const captionsFile = project.captionsFile === undefined ? `${stem}.srt` : baseName(project.captionsFile)
    const key = this.key(project)
    const cues = project.scenes.flatMap((scene, index) => scene.sentences.map((sentence) => ({ start: (times.scenes[index]?.start ?? 0) + 0.25 + sentence.offset, seconds: sentence.seconds, text: sentence.text })))
    const sceneAt = (at: number) => times.scenes.reduce((found, scene, index) => (at >= scene.start ? index : found), 0)
    const player = this.playback.face(key, total, {
      width: 1280,
      height: 704,
      dims: '1280×704 · 24 fps',
      badge: project.captions ? `captions from ${captionsFile}` : 'no captions',
      right: `${n} scenes`,
      marks: times.scenes.slice(1).map((scene) => scene.start),
      pausedLabel: `about ${Math.round(total)} s · ${n} scenes`,
      playingLabel: (at) => {
        const index = sceneAt(at)
        const scene = project.scenes[index]
        return `scene ${index + 1} of ${n}${scene && ProjectRecord.takesAsItIs(scene) ? ` · ${scene.asset}, as it is` : ''}`
      },
      caption: (at) => (project.captions && at > 0 ? (cues.find((cue) => at >= cue.start && at < cue.start + cue.seconds)?.text ?? '') : ''),
    })
    const position = this.playback.position(key, total)
    const playingScene = player.playing || position > 0 ? sceneAt(position) : -1
    const share = (seconds: number) => (total > 0 ? Math.round((seconds / total) * 1000) / 10 : 0)
    const status = render
      ? state === 'speaking' && sceneNow !== undefined
        ? `speaking scene ${sceneNow}`
        : state === 'rendering' && sceneNow !== undefined && !assembling
          ? `rendering scene ${sceneNow} · step ${Math.max(1, step)} of 50`
          : assembling
            ? 'assembling'
            : 'rendering'
      : project.status === 'rendering'
        ? 'rendering'
        : stopped
          ? project.status === 'failed'
            ? 'failed'
            : 'stopped'
          : project.status === 'planned'
            ? 'planned'
            : 'rendered'
    const stoppedAt = this.stoppedAt(project)
    const phase = (statusOf: PhaseStatus, label: string, detail: string, bar?: number): PhaseFace => ({ label, status: statusOf, detail, ...(bar === undefined ? {} : { bar }) })
    const speaking = render !== undefined && state === 'speaking' && spoken < n
    const rendering = render !== undefined && state === 'rendering' && made < n
    const parent = this.parent(project)
    const diff = project.parent === undefined ? undefined : ProjectLibrary.diff(project, parent)
    const previewed = this.previewedRenders.get(project.output)
    const planner = this.planner(project.planner)
    const music = project.music === undefined ? undefined : baseName(project.music)
    return {
      shown: true,
      title: project.title,
      tag: project.edits.length > 0 ? `EDIT ${project.edits.length}` : '',
      meta: `${n} scenes · about ${Math.round(total)} s · narrated by ${project.voice ?? cliDefaultVoice} · ${!project.captions ? 'no captions' : project.captionsBurned === true ? 'captions burned in' : 'captions beside it'}${music !== undefined ? ` · over ${music}` : ''}`,
      brief: project.brief,
      status,
      statusColor: render || project.status === 'rendering' ? 'var(--status-accent)' : stopped ? 'var(--status-warning)' : 'var(--text-secondary)',
      rendering: render !== undefined,
      stopped,
      showProgress: !done,
      phases: [
        phase(spoken === n ? 'done' : speaking ? 'running' : stopped ? 'stopped' : 'pending', 'Speak every narration', `${spoken} of ${n} spoken`, n === 0 ? 0 : Math.round((spoken / n) * 100)),
        phase(made === n ? 'done' : rendering ? 'running' : stopped && spoken === n ? 'stopped' : 'pending', 'Render every clip', `${made} of ${n} made${rendering && sceneNow !== undefined ? ` · scene ${sceneNow}, step ${Math.max(1, step)} of 50` : ''}`, n === 0 ? 0 : Math.round(((made + (rendering ? step / 50 : 0)) / n) * 100)),
        phase(done ? 'done' : assembling ? 'running' : 'pending', 'Assemble', `${secs(project.crossfade ?? 0.5)} s crossfades · narration${music !== undefined ? ' over the bed' : ''}${project.captions ? ' · captions beside' : ''}`),
      ],
      stopNote: stopped
        ? (this.stopNotes.get(project.output) ??
          (project.status === 'failed'
            ? `Failed: ${project.error ?? 'the run did not finish'}. Every finished scene is kept, and resuming renders only what’s missing.`
            : stoppedAt.clip
              ? `Stopped at scene ${stoppedAt.scene}’s clip. Every finished scene is kept, and resuming renders only what’s missing. The partial clip isn’t kept: scene ${stoppedAt.scene} renders again with its seed held.`
              : 'Stopped. Every finished scene is kept, and resuming renders only what’s missing.'))
        : '',
      isEdit: diff !== undefined,
      changes: (diff?.changes ?? []).map(
        (change): ChangeRowFace => ({
          n: String(change.index),
          text: change.kind.case === 'added' ? 'new' : change.kind.case === 'removed' ? 'removed' : change.kind.case === 'unchanged' ? 'kept as it is' : `changed: ${change.kind.words.join(', ')}`,
          color: change.kind.case === 'unchanged' ? 'var(--text-tertiary)' : 'var(--text-primary)',
          narration: change.narration,
        }),
      ),
      differs: previewed !== undefined && !ProjectLibrary.sameChanges(project, previewed) ? 'The render planned again, and its changes differ from the preview' : '',
      showPlayer: done,
      player,
      timeline: {
        pictures: times.scenes.map((scene, index) => ({ n: String(index + 1), at: clockText(scene.start), len: `${secs(scene.length)} s`, left: share(scene.start), width: share(scene.length), current: playingScene === index, made: project.scenes[index] !== undefined && kept(project.scenes[index]) })),
        narration: project.scenes.map((scene, index) => ({ left: share((times.scenes[index]?.start ?? 0) + 0.25), width: share(scene.voiceSeconds ?? 0), made: scene.voiceFile !== undefined })),
        cues: project.captions ? cues.map((cue) => ({ left: share(cue.start), width: share(cue.seconds), made: true })) : [],
        bed: music !== undefined,
        head: share(position),
        legend: music !== undefined ? `${music}, ducked under the narration` : `${secs(project.crossfade ?? 0.5)} s crossfades`,
        end: clockText(total),
        note: 'to change it, type what to change',
      },
      scenes: project.scenes.map((scene, index) => {
        const assetClip = ProjectRecord.takesAsItIs(scene)
        const here = render !== undefined && sceneNow === scene.index
        const stoppedHere = stopped && stoppedAt.clip && stoppedAt.scene === scene.index
        return {
          n: String(scene.index),
          narration: scene.narration,
          shows: this.shows(scene),
          voiceText: scene.voiceFile !== undefined ? `narration ${secs(scene.voiceSeconds ?? 0)} s, measured` : here && state === 'speaking' ? 'speaking…' : 'not spoken yet',
          voiceColor: here && state === 'speaking' && scene.voiceFile === undefined ? 'var(--status-accent)' : scene.voiceFile !== undefined ? 'var(--text-secondary)' : 'var(--text-tertiary)',
          clipText: assetClip ? 'clip taken as it is' : kept(scene) ? 'clip made' : here && state === 'rendering' ? `clip · step ${Math.max(1, step)} of 50` : stoppedHere ? 'clip stopped; renders again, seed held' : 'clip not made yet',
          clipColor: here && state === 'rendering' && !kept(scene) ? 'var(--status-accent)' : stoppedHere ? 'var(--status-warning)' : kept(scene) ? 'var(--text-secondary)' : 'var(--text-tertiary)',
          seedText: assetClip ? '' : `seed ${scene.seed ?? 'not recorded'}`,
          secs: `${secs(times.scenes[index]?.length ?? 0)} s`,
          playing: playingScene === index && player.playing,
          canRedo: done && !assetClip,
          redo: this.currentEdit()?.form.redo.has(scene.index) ?? false,
        }
      }),
      folder: tilde(project.output.slice(0, project.output.lastIndexOf('/'))),
      files: [
        { path: project.name, what: done ? 'the video' : 'written once, at the end' },
        { path: `${project.name}.json`, what: 'the project' },
        { path: `${stem}.scenes/`, what: `${n} scenes, each with its records` },
        ...(project.captions ? [{ path: captionsFile, what: 'captions' }] : []),
      ],
      facts: [
        { label: 'Voice', value: `${project.voiceModel === undefined ? 'Kokoro' : project.voiceModel.split('/')[0]} · ${project.voice ?? cliDefaultVoice}`, mono: true, color: 'var(--text-primary)' },
        { label: 'Pipeline', value: this.pipelineName(project.model), mono: false, color: 'var(--text-primary)' },
        { label: 'Music', value: music ?? 'none', mono: false, color: 'var(--text-primary)' },
        { label: 'Assets', value: project.assets === undefined ? 'none' : tilde(project.assets), mono: true, color: 'var(--text-primary)' },
        { label: 'Captions', value: !project.captions ? 'off' : project.captionsBurned === true ? 'burned in' : `kept beside it as ${captionsFile}`, mono: false, color: 'var(--text-primary)' },
        { label: 'Planner', value: `${planner.name}${planner.kind === 'api' ? ' · API, billed' : planner.kind === 'subscription' ? ' · subscription' : planner.kind === 'local' ? ' · on this Mac' : ''} · ${project.attempts ?? 1} ${(project.attempts ?? 1) === 1 ? 'attempt' : 'attempts'}`, mono: false, color: planner.kind === 'api' ? 'var(--model-api)' : 'var(--text-primary)' },
        { label: 'Made', value: when(project.created, this.clock.now), mono: false, color: 'var(--text-primary)' },
      ],
      chain: this.chain(project),
      file: project.name,
    }
  }

  /** Where the project came from, back to the first: each link its video and how it was made. */
  private chain(project: ProjectRecord): ProjectPageFace['chain'] {
    const links: Array<{ output: string; label: string; how: string }> = []
    const seen = new Set<string>()
    let node: ProjectRecord | undefined = project
    while (node && !seen.has(node.output) && links.length < 6) {
      seen.add(node.output)
      const last = node.edits.at(-1)
      const made = when(node.created, this.clock.now)
      let how: string
      if (node.parent === undefined) how = `planned from the brief by ${this.planner(node.planner).name} · ${made}`
      else {
        const bits = [...(last?.instruction !== undefined ? [`“${last.instruction}”`] : []), ...(last && last.redo.length > 0 ? [`re-rolled scene ${last.redo.join(', ')}`] : [])]
        how = `edited from ${last?.from !== undefined ? baseName(last.from) : 'its parent'}: ${bits.join(' · ') || 'settings only'} · ${made}`
      }
      links.push({ output: node.output, label: node.name, how })
      node = this.parent(node)
    }
    return links.map((link, index) => ({ ...link, link: index > 0, first: index === 0, last: index === links.length - 1 }))
  }

  private editFace(project: ProjectRecord | undefined): EditPaneFace {
    const edit = this.currentEdit()
    const empty: EditPaneFace = { locked: true, lockedText: '', text: '', textNote: '', redo: { text: '', color: '' }, voice: '', voices: [], pipeline: '', pipelines: [], music: '', musicChoices: [], captions: true, captionsLabel: '', previewing: false, previewingText: '', cantPreview: true, previewNote: '', failure: '', shown: false, head: '', diff: [], notes: [], spend: '', replans: { text: '', color: '' }, renderTitle: '', out: '', command: '' }
    if (!project || !edit) return empty
    const locked = project.status !== 'done'
    const rendering = this.render?.output === project.output || project.status === 'rendering'
    const redo = [...edit.form.redo].sort((a, b) => a - b)
    const projectMusic = project.music === undefined ? '' : baseName(project.music)
    const voiceChanged = edit.voice !== (project.voice ?? cliDefaultVoice)
    const pipeChanged = edit.model !== (project.model ?? '')
    const musicChanged = edit.music !== projectMusic
    const captionsChanged = edit.captions !== project.captions
    const instruction = edit.form.instruction.trim()
    const anyChange = instruction !== '' || redo.length > 0 || voiceChanged || pipeChanged || musicChanged || captionsChanged
    const shown = edit.stage === 'shown' && this.preview !== undefined
    const preview = shown ? this.preview : undefined
    const output = this.editOutput(edit)
    let spend = ''
    const notes: string[] = []
    let diff: ChangeRowFace[] = []
    let head = ''
    if (preview) {
      const count = preview.scenes.length
      head = `the edit: ${count} scene${count === 1 ? '' : 's'}, were ${project.scenes.length}`
      diff = [
        ...preview.scenes.map((scene, index): ChangeRowFace => {
          const n = `${index + 1}.`
          if (scene.from === 'new') return { n, text: 'new', color: 'var(--status-accent)', narration: '' }
          const said = (scene.changes ?? []).map(changeWords)
          const moved = scene.moved ? ', moved' : ''
          return said.length > 0 ? { n, text: `scene ${scene.from ?? index + 1}${moved}; ${said.join(', ')}`, color: 'var(--text-primary)', narration: '' } : { n, text: `scene ${scene.from ?? index + 1}${moved}${scene.moved ? '' : ', untouched'}`, color: 'var(--text-tertiary)', narration: '' }
        }),
        ...(preview.removed ?? []).map((removed): ChangeRowFace => ({ n: '', text: `scene ${removed} removed`, color: 'var(--status-warning)', narration: '' })),
      ]
      if (voiceChanged) notes.push('The voice isn’t the project’s, so every narration is spoken again.')
      if (pipeChanged) notes.push('The pipeline isn’t the project’s, so every clip renders again.')
      if (musicChanged) notes.push(edit.music !== '' ? 'The music bed changes; only the assembly runs again for it.' : 'No music bed; only the assembly runs again for it.')
      if (captionsChanged) notes.push(edit.captions ? 'Captions on, kept beside the video as an .srt.' : 'Captions off.')
      const assetClip = ProjectRecord.takesAsItIs
      const speak = preview.scenes.filter((scene) => voiceChanged || scene.from === 'new' || (scene.changes ?? []).includes('narration')).length
      const renders = preview.scenes.filter((scene) => !assetClip(scene) && (pipeChanged || scene.from === 'new' || (scene.changes ?? []).some((change) => change !== 'narration'))).length
      const kept = preview.scenes.filter((scene) => !assetClip(scene)).length - renders
      const parts: string[] = []
      if (speak) parts.push(`${speak} narration run${speak === 1 ? '' : 's'} (faster than real time)`)
      if (renders) parts.push(`${speak ? 'then ' : ''}${renders} clip run${renders === 1 ? '' : 's'} at 1280×704, 50 steps, minutes each on Apple silicon`)
      spend = `Spend: ${parts.length > 0 ? parts.join(', ') : 'no narration or clip runs, the assembly alone'}${kept ? `; ${kept} clip${kept === 1 ? '' : 's'} already made` : ''}.`
    }
    // Who plans the edit: the shown preview's planner, else the chat role -- the pane passes no --planner, so the CLI's `video compose --edit` resolves the chat role (`resolve_chat_backend`), never the project's own planner.
    const replanner = this.planner(preview?.planner ?? this.chatRole ?? project.planner)
    const command = [`apogee video compose --from ${tilde(project.output)}`]
    if (instruction !== '') command.push(`--edit ${quoted(instruction)}`)
    for (const scene of redo) command.push(`--redo-scene ${scene}`)
    if (voiceChanged) command.push(`--voice ${edit.voice}`)
    if (pipeChanged) command.push(`-m ${edit.model}`)
    if (musicChanged && edit.music !== '') command.push(`--music ~/Music/beds/${edit.music}`)
    if (captionsChanged && !edit.captions) command.push('--no-captions')
    command.push(`${shown ? '' : '--dry-run '}-o ${tilde(output)}`)
    return {
      locked,
      lockedText: rendering ? 'It’s still rendering. Changes open once it’s done.' : 'Resume it first: a stopped composition picks up where it stopped, and then it can be changed.',
      text: edit.form.instruction,
      textNote: `One pass by ${replanner.name}${replanner.kind === 'api' ? ', billed per use' : ''}: it reads the brief, the numbered scenes and your words, and answers with the whole plan.`,
      redo: redo.length > 0 ? { text: `re-roll: scene ${redo.join(', ')}, a new seed each`, color: 'var(--text-primary)' } : { text: 'Tick Re-roll on a scene for a new take of it, everything else held.', color: 'var(--text-tertiary)' },
      voice: edit.voice,
      voices: this.voiceChoices(edit.voice),
      pipeline: edit.model,
      pipelines: this.pipelines(edit.model),
      music: edit.music,
      musicChoices: [{ value: '', label: 'None' }, ...(projectMusic !== '' ? [{ value: projectMusic, label: projectMusic }] : []), ...(projectMusic === otherBed ? [] : [{ value: otherBed, label: otherBed }])],
      captions: edit.captions,
      captionsLabel: edit.captions ? (this.burns ? 'burned in' : 'beside, as .srt') : 'off',
      previewing: edit.stage === 'previewing',
      previewingText: 'Planning on paper -- at most 5 minutes',
      cantPreview: locked || !anyChange || this.binary === undefined,
      previewNote: shown ? '' : instruction !== '' ? 'A dry run: nothing renders and nothing is written.' : 'Without an instruction there’s no model call.',
      failure: edit.failure ?? '',
      shown,
      head,
      diff,
      notes,
      spend,
      replans: shown && instruction !== '' ? { text: `Render plans the edit again with ${replanner.name}: what renders can differ from this preview, and the planner is called a second time.`, color: replanner.kind === 'api' ? 'var(--model-api)' : 'var(--text-secondary)' } : { text: '', color: '' },
      renderTitle: instruction !== '' ? `Plans the edit again with ${replanner.name}, then renders it: it can differ from this preview` : 'Renders the scenes chosen again on fresh seeds; nothing is planned',
      out: edit.out ?? baseName(output),
      command: command.join(' '),
    }
  }
}
