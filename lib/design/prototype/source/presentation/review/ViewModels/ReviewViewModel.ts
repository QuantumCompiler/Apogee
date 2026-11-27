import { JobProgress } from '../../../business/jobs/JobProgress'
import type { JobRunning, JobStarting } from '../../../business/jobs/SupervisedJob'
import { CommandOutcome } from '../../../business/reads/CommandRunning'
import type { CommandRunning } from '../../../business/reads/CommandRunning'
import { ReadOutcome } from '../../../business/reads/ReadRunner'
import { ReviewRun, ReviewSkill, ReviewSweep, ReviewTarget } from '../../../business/reads/Review'
import type { ReviewFinding } from '../../../business/reads/Review'
import { ReadDocument } from '../../../data/protocol/ReadDocument'
import type { FolderReading } from '../../../infrastructure/process/StandInFolders'
import { tilde } from '../../styles/Media/MediaWords'
import { Observable } from '../../styles/Observation/Observable'
import type { FindingFace, PostWords, ReviewFace, ReviewTab, RunFace, SkillsFace, SweepFace, TargetSheetFace, TargetsFace } from './ReviewFaces'
import { RunForm, TargetForm } from './ReviewForms'
import { defaultSkill, missingWords, noSkillWords } from './ReviewWords'

export type { ReviewTab } from './ReviewFaces'

/** Where the targets or the skills stand: not read yet, read, no apogee, or a failed read -- each said where it is drawn (the Mac app's `ReviewViewModel.Listing`). */
export type Listing = { readonly case: 'notRead' } | { readonly case: 'read' } | { readonly case: 'missing' } | { readonly case: 'failed'; readonly message: string }

const tabs: ReadonlyArray<readonly [ReviewTab, string]> = [
  ['runs', 'Reviews'],
  ['targets', 'Targets and sweeps'],
  ['skills', 'Skills'],
]

const listingText = (listing: Listing, what: string): string => (listing.case === 'missing' ? missingWords : listing.case === 'failed' ? `The ${what} could not be read: ${listing.message}` : '')

/**
 * The Review place (design 1n): the Mac app's `ReviewViewModel`
 * (`ReviewViewModel.swift`) -- a merge request reviewed, a dry run first, its
 * document read from the run's stdout; the post asked first, its batch said;
 * the sweep targets read, and one added from the sheet's fields as typed;
 * the last sweep the CLI kept; and the review skills, one shown whole, a
 * reply landing only while its skill is chosen. As the old prototype draws
 * it, the Reviews tab's Review, Sweep… and a target's Remove… do nothing,
 * and the post sheet's Post closes it (the document's defaults: the run, the
 * posting, the sweep and the removal are new design). Plain TypeScript.
 */
export class ReviewViewModel extends Observable {
  tab: ReviewTab = 'runs'
  form: RunForm = RunForm.empty
  targets: readonly ReviewTarget[] = []
  skills: readonly ReviewSkill[] = []
  targetsListing: Listing = { case: 'notRead' }
  skillsListing: Listing = { case: 'notRead' }
  /** The skill open in full, `review skills show`. */
  shownSkill: ReviewSkill | undefined = undefined
  selectedSkill: string = defaultSkill
  /** The review on screen: the last run's document, and the arguments it ran with -- what a post repeats with `--yes`. */
  run: ReviewRun | undefined = undefined
  runArguments: readonly string[] | undefined = undefined
  /** The last sweep's document, as the CLI kept it in its sweeps folder. */
  sweep: ReviewSweep | undefined = undefined
  /** What waits on the person's word: the post. */
  pending: 'post' | undefined = undefined
  /** The add-target sheet's fields, while it is open. */
  targetForm: TargetForm | undefined = undefined
  /** The act running now, and its progress -- the CLI's narration its busy line. */
  running: string | undefined = undefined
  progress: JobProgress | undefined = undefined
  /** The targets the CLI said it added: the stand-in keeps no state, so its read lacks them, and what it reported is kept beside the read. */
  private added: readonly ReviewTarget[] = []
  private binary: string | undefined = undefined
  private job: JobRunning | undefined = undefined

  /** The doors the composition fills: the toast, the shell's sheet and confirm hosts, and where the CLI keeps its sweeps. */
  say?: (text: string) => void
  present?: (sheet: string) => void
  dismissSheet?: () => void
  ask?: (confirm: string) => void
  sweeps: () => readonly string[] = () => []

  constructor(
    private readonly runner: CommandRunning,
    private readonly starter: JobStarting,
    private readonly folders: FolderReading,
  ) {
    super()
  }

  /** With no apogee, the place is its empty state -- said in each tab -- never an alert (GUI 4h). */
  connect(binary: string | undefined): void {
    this.binary = binary
    if (binary === undefined) {
      this.targetsListing = { case: 'missing' }
      this.skillsListing = { case: 'missing' }
    }
    this.changed()
  }

  /** The place came into view: the targets, the skills, the lens shown, and the last sweep, read again. */
  async appeared(): Promise<void> {
    await this.refresh()
    this.readSweep()
    if (this.binary !== undefined && this.skills.some((skill) => skill.name === this.selectedSkill)) await this.show(this.selectedSkill)
  }

  /** The targets and the skills, read again; a failure is said where each is drawn, what was read before kept. */
  async refresh(): Promise<void> {
    const binary = this.binary
    if (binary === undefined) return
    const targetsRead = await this.runner.read(binary, ['review', 'targets'])
    const targets = ReadOutcome.documentOf(targetsRead)
    if (targets) this.targets = ReviewTarget.list(targets)
    this.targetsListing = targets ? { case: 'read' } : { case: 'failed', message: ReadOutcome.failure(targetsRead) ?? 'apogee printed nothing to read' }
    const skillsRead = await this.runner.read(binary, ['review', 'skills', 'list'])
    const skills = ReadOutcome.documentOf(skillsRead)
    if (skills) this.skills = ReviewSkill.list(skills)
    this.skillsListing = skills ? { case: 'read' } : { case: 'failed', message: ReadOutcome.failure(skillsRead) ?? 'apogee printed nothing to read' }
    this.changed()
  }

  /**
   * The last sweep: the newest document the CLI kept in its sweeps folder
   * (`review sweep` writes each there, its `document` path; no verb lists
   * them). It is the person's file, read whatever the config says; with no
   * apogee the place is its empty state.
   */
  readSweep(): void {
    const kept = this.sweeps()
      .flatMap((folder) => this.folders.files(folder).filter((file) => file.name.endsWith('.json') && file.text !== undefined))
      .sort((a, b) => (a.name < b.name ? 1 : -1))
    for (const file of kept) {
      const document = ReadDocument.from(file.text ?? '')
      const sweep = document ? ReviewSweep.read(document) : undefined
      if (sweep) {
        this.sweep = sweep
        break
      }
    }
    this.changed()
  }

  pick(tab: ReviewTab): void {
    this.tab = tab
    this.changed()
  }

  /** The Reviews tab's form, as typed: its merge request, lenses and model. */
  setForm(patch: Partial<RunForm>): void {
    this.form = { ...this.form, ...patch }
    this.changed()
  }

  /** The Review button: does nothing, as the old prototype's (the document's default -- a run from the tab is new design). */
  pressReview(): void {}

  /** Sweep…: does nothing, as the old prototype's (the document's default). */
  pressSweep(): void {}

  /** A target's Remove…: does nothing, as the old prototype's (the document's default). */
  pressRemove(name: string): void {
    void name
  }

  /**
   * The Mac's `review()`: reviews the merge request and posts nothing,
   * `--dry-run`, supervised, its document read from its stdout. What a
   * preset runs to draw the old frame's review; the tab's button stays dead.
   */
  review(): void {
    const args = RunForm.arguments(this.form, false)
    if (args === undefined) return
    this.start(args, `Reviewing ${this.form.ref.trim()}`, (document) => {
      const run = ReviewRun.read(document)
      if (!run) return false
      this.run = run
      this.runArguments = args
      return true
    })
  }

  /** A run's end, awaited: what a preset waits on before it asks to post. */
  ran: Promise<void> = Promise.resolve()

  /** Asks before posting what the shown run found -- never for a silent one, which has nothing to post. */
  askToPost(): void {
    const run = this.run
    if (!run || run.findings.length === 0 || !run.dryRun) return
    this.pending = 'post'
    this.changed()
    this.ask?.('reviewPost')
  }

  /** The post sheet's words while it stands: what goes, that it is a fresh run, and the command that posts it. */
  get postWords(): PostWords | undefined {
    const run = this.run
    if (this.pending !== 'post' || !run) return undefined
    const inline = ReviewRun.inlineFindings(run).length
    const over = ReviewRun.overCap(run).length
    const body = `The dry run found ${inline} ${inline === 1 ? 'finding' : 'findings'} to post inline${over > 0 ? ` and ${over} for the summary alone` : ''}. Posting runs the review again with --yes and posts what that run finds, as one batch on ${run.mergeRequest}: its inline comments and a summary.`
    const posting = (this.runArguments ?? []).map((word) => (word === '--dry-run' ? '--yes' : word))
    return { title: `Post the review to ${run.mergeRequest}?`, body, command: `apogee ${posting.join(' ')}` }
  }

  /** The post sheet's Post: closes it, as the old one does (the document's default -- the posted and refused runs are recorded, not drawn). */
  confirmPost(): void {
    this.pending = undefined
    this.changed()
  }

  /** The post sheet's Cancel. */
  dismissPost(): void {
    this.pending = undefined
    this.changed()
  }

  // MARK: - Targets

  /** Add target…: the sheet, as the old prototype opens it. */
  newTarget(): void {
    if (this.binary === undefined) return
    this.targetForm = TargetForm.starting()
    this.changed()
    this.present?.('target')
  }

  editTarget(patch: Partial<TargetForm>): void {
    if (!this.targetForm) return
    this.targetForm = { ...this.targetForm, ...patch }
    this.changed()
  }

  toggleLensMenu(): void {
    if (this.targetForm) this.editTarget({ lensOpen: !this.targetForm.lensOpen })
  }

  /** A lens pinned or unpinned: kept in the order picked; the command sorts them, as the CLI's argv does. */
  toggleLens(name: string): void {
    const form = this.targetForm
    if (!form) return
    this.editTarget({ lenses: form.lenses.includes(name) ? form.lenses.filter((lens) => lens !== name) : [...form.lenses, name] })
  }

  cancelTarget(): void {
    this.targetForm = undefined
    this.changed()
    this.dismissSheet?.()
  }

  /** Add: `review targets add …` as typed, read back; the target the CLI said it added kept beside the read, its first line said. */
  async addTarget(): Promise<void> {
    const form = this.targetForm
    const binary = this.binary
    const args = form ? TargetForm.arguments(form) : undefined
    if (!form || args === undefined || binary === undefined) return
    this.targetForm = undefined
    this.changed()
    this.dismissSheet?.()
    const outcome = await this.runner.command(binary, args)
    await this.refresh()
    const failure = CommandOutcome.failure(outcome)
    if (failure !== undefined) this.say?.(failure)
    else if (outcome.case === 'done') {
      const target = TargetForm.target(form)
      this.added = [...this.added.filter((kept) => kept.name !== target.name), target]
      this.say?.(outcome.stdout.split('\n')[0] || 'Done')
    }
    this.changed()
  }

  /** The targets listed: the read, and those the CLI said it added that the read lacks. */
  get listedTargets(): readonly ReviewTarget[] {
    return [...this.targets, ...this.added.filter((target) => !this.targets.some((read) => read.name === target.name))]
  }

  // MARK: - Skills

  /**
   * `review skills show <name>`: the lens whole, its prompt read only. Another
   * skill's detail is cleared at the click, and the reply -- or its failure --
   * lands only while `name` is still the one chosen.
   */
  async show(name: string): Promise<void> {
    this.selectedSkill = name
    if (this.shownSkill?.name !== name) this.shownSkill = undefined
    this.changed()
    const binary = this.binary
    if (binary === undefined) return
    const outcome = await this.runner.read(binary, ['review', 'skills', 'show', name])
    if (this.selectedSkill !== name) return
    const document = ReadOutcome.documentOf(outcome)
    const skill = document ? ReviewSkill.show(document) : undefined
    if (skill) this.shownSkill = skill
    else this.say?.(ReadOutcome.failure(outcome) ?? 'apogee printed nothing to read')
    this.changed()
  }

  // MARK: - Running

  get isRunning(): boolean {
    return this.job !== undefined
  }

  /** Runs `args` supervised; its stdout's document goes to `read`. A run that leaves none failed, in the CLI's own last words, said. */
  private start(args: readonly string[], doing: string, read: (document: ReadDocument) => boolean): void {
    const binary = this.binary
    if (binary === undefined || this.job !== undefined) return
    this.running = doing
    const progress = new JobProgress()
    this.progress = progress
    const job = this.starter.startJob(binary, args, false)
    this.job = job
    this.changed()
    this.ran = (async () => {
      const stdout: string[] = []
      for await (const update of job.updates) {
        if (update.case === 'said') stdout.push(update.line)
        progress.apply(update)
        this.changed()
      }
      this.job = undefined
      this.progress = undefined
      this.running = undefined
      const document = ReadDocument.from(stdout.join('\n'))
      const landed = document ? read(document) : false
      if (progress.status.case === 'failed') this.say?.(progress.status.message)
      else if (!landed && progress.status.case === 'stopped') this.say?.('Stopped')
      this.changed()
    })()
  }

  // MARK: - The face

  face(): ReviewFace {
    return {
      tabs: tabs.map(([tab, label]) => ({ tab, label, current: tab === this.tab })),
      targetButtons: { shown: this.tab === 'targets', enabled: this.binary !== undefined },
      runs: {
        form: { ref: this.form.ref, lenses: RunForm.lensesLabel(this.form), model: RunForm.modelLabel(this.form), command: RunForm.command(this.form, false) },
        missing: this.binary === undefined ? missingWords : '',
        run: this.runFace(),
      },
      targets: this.targetsFace(),
      skills: this.skillsFace(),
    }
  }

  private runFace(): RunFace {
    const run = this.run
    if (!run) return { shown: false, title: '', where: '', lenses: '', posted: '', canPost: false, findings: [], counts: '' }
    const posted = run.posted.case === 'posted' ? `Posted ${run.posted.comments} ${run.posted.comments === 1 ? 'comment' : 'comments'}${run.posted.summary ? ' and a summary' : ''} to ${run.mergeRequest}` : ReviewRun.isSilent(run) ? 'Nothing to say: the lenses found nothing in this change. That is a complete review.' : run.posted.reason
    return {
      shown: true,
      title: run.title,
      where: `${run.mergeRequest} · head ${run.head.slice(0, 8)} · ${run.mode}`,
      lenses: `lenses: ${run.skills.join(', ')} (${run.plan}) · ${run.model}`,
      posted,
      canPost: run.dryRun && run.findings.length > 0 && !this.isRunning,
      findings: run.findings.map(findingFace),
      counts: run.counts.map(([name, count]) => `${name.replace(/_/g, ' ')} ${count}`).join(' · '),
    }
  }

  private targetsFace(): TargetsFace {
    const missing = this.binary === undefined
    return {
      note: listingText(this.targetsListing, 'targets'),
      rows: this.listedTargets.map((target) => ({
        name: target.name,
        facts: [target.forge, target.repo !== undefined ? `repository ${target.repo}` : `owner ${target.owner ?? ''}`, ...(target.host === undefined ? [] : [`host ${target.host}`]), target.skills.length > 0 ? `lenses ${target.skills.join(', ')}` : 'lenses by the change'].join(' · '),
        post: target.post ? 'posts when swept' : 'documents only',
        postColor: target.post ? 'var(--status-warning)' : 'var(--text-secondary)',
      })),
      sweep: missing ? noSweep : sweepFace(this.sweep),
    }
  }

  private skillsFace(): SkillsFace {
    const shown = this.shownSkill
    return {
      note: listingText(this.skillsListing, 'review skills'),
      rows: this.skills.map((skill) => ({ name: skill.name, meta: `${skill.source} · ${appliesTo(skill) || 'every file'}`, current: skill.name === this.selectedSkill })),
      empty: shown ? '' : noSkillWords,
      skill: shown
        ? { shown: true, name: shown.name, description: shown.description, applies: appliesTo(shown) === '' ? 'Applies to every file.' : `Applies to: ${appliesTo(shown)}`, prompt: shown.prompt ?? '', command: `apogee review skills show ${shown.name}` }
        : { shown: false, name: '', description: '', applies: '', prompt: '', command: '' },
    }
  }

  /** The add-target sheet's face, while it is open. */
  targetSheet(): TargetSheetFace {
    const form = this.targetForm
    if (!form) return { shown: false, name: '', watch: 'repo', repo: '', owner: '', forge: 'github', host: '', lensesLabel: '', lensOpen: false, lenses: [], post: false, drafts: false, model: '', command: '', canAdd: false }
    return {
      shown: true,
      name: form.name,
      watch: form.watch,
      repo: form.repo,
      owner: form.owner,
      forge: form.forge,
      host: form.host,
      lensesLabel: TargetForm.lensesLabel(form),
      lensOpen: form.lensOpen,
      lenses: this.skills.map((skill) => ({ name: skill.name, on: form.lenses.includes(skill.name) })),
      post: form.post,
      drafts: form.drafts,
      model: form.model,
      command: TargetForm.command(form),
      canAdd: TargetForm.canAdd(form),
    }
  }
}

/** What a lens applies to, as the old row says it: its extensions, then its paths, then its languages; `''` everywhere. */
function appliesTo(skill: ReviewSkill): string {
  return skill.everywhere ? '' : [...skill.extensions, ...skill.paths, ...skill.languages].join(', ')
}

function findingFace(finding: ReviewFinding): FindingFace {
  return {
    severity: finding.severity,
    severityColor: finding.severity === 'high' || finding.severity === 'critical' ? 'var(--status-error)' : finding.severity === 'medium' ? 'var(--status-warning)' : 'var(--text-secondary)',
    kind: finding.kind,
    at: `${finding.file}${finding.line === undefined ? '' : `:${finding.line}`}`,
    by: `${finding.confidence} confidence · ${finding.foundBy.join(', ')}${finding.inline ? '' : ' · summary only'}`,
    claim: finding.claim,
    fix: finding.suggestedFix,
  }
}

const noSweep: SweepFace = { shown: false, summary: '', targets: [], kept: '' }

/** The last sweep, in the Mac's `SweepPage` lines, which are the old page's. */
function sweepFace(sweep: ReviewSweep | undefined): SweepFace {
  if (!sweep) return noSweep
  const line = (run: ReviewRun) => {
    const found = ReviewRun.isSilent(run) ? 'nothing to say' : `${run.findings.length} ${run.findings.length === 1 ? 'finding' : 'findings'}`
    return run.posted.case === 'posted' ? `${run.mergeRequest} -- ${run.title}: ${found}, ${run.posted.comments} posted` : `${run.mergeRequest} -- ${run.title}: ${found}; ${run.posted.reason}`
  }
  return {
    shown: true,
    summary: `${sweep.dryRun ? 'dry run · ' : ''}${sweep.reviewed} reviewed · ${sweep.posted} posted · ${sweep.deferred} deferred · ${sweep.errors} ${sweep.errors === 1 ? 'error' : 'errors'} · started ${sweep.started}`,
    targets: sweep.targets.map((target) => ({
      heading: `${target.name} · ${target.kind} · grant ${target.grant}`,
      error: target.error ?? '',
      projects: target.projects.map((project) => ({
        project: project.project,
        watermark: project.firstSweep ? 'first sweep: started at now -- history stays unreviewed' : `watermark ${project.watermarkBefore ?? 'none'} → ${project.watermarkAfter ?? 'unchanged'}`,
        error: project.error ?? '',
        lines: project.reviewed.map(line),
      })),
    })),
    kept: sweep.document === '' ? '' : `kept at ${tilde(sweep.document)}`,
  }
}
