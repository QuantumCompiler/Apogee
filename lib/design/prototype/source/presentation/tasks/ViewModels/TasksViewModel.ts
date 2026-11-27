import type { JobRunning, JobStarting } from '../../../business/jobs/SupervisedJob'
import { CommandOutcome } from '../../../business/reads/CommandRunning'
import type { CommandRunning } from '../../../business/reads/CommandRunning'
import { Field } from '../../../business/reads/Models'
import { ReadOutcome } from '../../../business/reads/ReadRunner'
import { Suites } from '../../../business/reads/SuiteFootprint'
import { TaskRecipe, TaskSummary, TaskView } from '../../../business/reads/Tasks'
import type { TaskRound } from '../../../business/reads/Tasks'
import { Observable } from '../../styles/Observation/Observable'
import { NewTaskForm } from './NewTaskForm'
import type { NewTaskFields } from './NewTaskForm'
import { TaskRunState } from './TaskRunState'
import type { NewTaskFace, TaskActivityFace, TaskDetailFace, TaskRoundFace, TaskRowFace, TasksConfirm, TasksFace, TaskTone } from './TasksFaces'
import { TaskWords } from './TaskWords'
import type { TaskHalt } from './TaskWords'

/** Where the list stands: the Mac app's `Listing`. */
type Listing = { readonly case: 'notRead' } | { readonly case: 'read' } | { readonly case: 'missing' } | { readonly case: 'failed'; readonly message: string }

/** One run the app started (`task run` / `task resume`), followed live: the Mac app's `TasksViewModel.Run`. */
export class TaskRun {
  readonly state = new TaskRunState()
  job: JobRunning | undefined = undefined
  /**
   * Where the `task halt` asked of it and answered lands, which the page says
   * meanwhile -- fixed as it is asked, as the old prototype's `haltAt` is:
   * the turn whose end it stops at (the plan, 0, or a round), or `'start'`,
   * sent while a resume started up, so the CLI honours it as the run begins,
   * before its next turn (57u's `stop_before_start`). `undefined`, none asked.
   */
  haltAt: number | 'start' | undefined = undefined
  /** The task a resume continues, read from its arguments: known from the click, before its stream names it. */
  readonly resumes: string | undefined

  constructor(readonly args: readonly string[]) {
    this.resumes = args[0] === 'task' && args[1] === 'resume' ? args[2] : undefined
  }

  get isRunning(): boolean {
    return this.job !== undefined
  }

  /** Whether `task halt` was asked of it and answered. */
  get haltAsked(): boolean {
    return this.haltAt !== undefined
  }

  /** The task this run is for: the one its stream names, else the one a resume was started on. */
  get target(): string | undefined {
    return this.state.taskID ?? this.resumes
  }
}

const noDetail: TaskDetailFace = {
  shown: false,
  id: '',
  goal: '',
  meta: '',
  status: '',
  tone: 'plain',
  isRunning: false,
  canPause: false,
  canStop: false,
  canResume: false,
  isResuming: false,
  showWhere: false,
  resumeWhere: '',
  canRerun: false,
  rerunLabel: '',
  interrupted: false,
  interruptedLine: '',
  budget: [],
  grants: { shown: false, text: '' },
  plan: [],
  rounds: [],
  allowed: [],
  denied: [],
  checks: [],
  noChecks: false,
  activity: [],
  refused: '',
  perms: '',
}

/**
 * The Tasks place (design 1p): the Mac app's `TasksViewModel`
 * (`TasksViewModel.swift`) -- the tasks the ledger holds, a live run's plan,
 * rounds, checks and grants, the controls, and the new-task sheet -- drawn
 * as the old prototype's place (`main.html` at `b375fac9`, `ui.isTasks`,
 * `ui.newTaskOpen` and the `tasks` section of `renderVals()`).
 *
 * A run's view is its events -- the ledger's transitions, one for one --
 * never inference; tasks started elsewhere are reads, refreshed on coming
 * into view, never polled; every control is the verb a person types: `task
 * halt`, `task cancel`, `task resume`, `task run`. A refusal is the CLI's
 * stderr line, word for word, with OK alone (the `taskRefused` alert).
 *
 * Where it differs from the Mac's, by the document: the old prototype's
 * Run again with twice the rounds (at most 32) is kept, a new run with the
 * same goal, checks and grants (prototype-only: the Mac has none); the sheet
 * is the old one's -- its suite, its four round budgets, its two grants, the
 * line it shows the run it starts (`NewTaskForm`), its folder read-only --
 * and a task is always shown, the first listed until one is chosen, as the
 * old one shows its first. Since the stand-in keeps no state, the app's own
 * run is the last word on its task where its stamps tie a read's (the Mac
 * takes the read on a tie): a read of the same moment is a recording made
 * before the run. Plain TypeScript.
 */
export class TasksViewModel extends Observable {
  tasks: readonly TaskSummary[] = []
  /** How many tasks the ledger holds in all, as `task list` says. */
  listTotal: number | undefined = undefined
  /** Whether the list reads `task list --all`: once Show all is asked, for the life of the place. */
  showingAll = false
  listing: Listing = { case: 'notRead' }
  selected: string | undefined = undefined
  /** The selected task's `task status`. */
  status: TaskView | undefined = undefined
  /** Why the selected task's `task status` could not be read, in the CLI's words. */
  statusFailure: string | undefined = undefined
  /** The app's runs, newest first. */
  runs: TaskRun[] = []
  /** The sheet's form while the sheet is open. */
  form: NewTaskForm | undefined = undefined
  recipes: readonly TaskRecipe[] = []
  recipeFailure: string | undefined = undefined
  /** The suites the sheet offers (`config get suites`), the default first chosen. */
  suiteNames: readonly string[] = []
  private defaultSuite: string | undefined = undefined
  /** The refusal the alert says, the CLI's words; `undefined` when none stands. */
  failure: string | undefined = undefined
  /** The folder a run works in, the sheet's read-only Folder: the composition's (the preset's). */
  folder = ''

  /** The toast, the composition's. */
  say?: (text: string) => void
  /** The sheet host: the New task sheet. */
  present?: (kind: 'newtask') => void
  dismissSheet?: () => void
  /** The confirm host: a refusal's alert. */
  ask?: (kind: TasksConfirm) => void

  private binary: string | undefined = undefined

  constructor(
    private readonly runner: CommandRunning,
    private readonly starter: JobStarting,
    private readonly now: () => Date = () => new Date(),
  ) {
    super()
  }

  connect(binary: string | undefined): void {
    this.binary = binary
    if (binary === undefined) this.listing = { case: 'missing' }
    this.changed()
  }

  /** Tasks came into view: read again. */
  async appeared(): Promise<void> {
    await this.refresh()
  }

  /**
   * `task list` -- with `--all` once Show all was asked -- and the shown
   * task's `task status`: the first listed until one is chosen, unless a run
   * of the app's is starting, whose task is shown once its stream names it.
   */
  async refresh(): Promise<void> {
    const binary = this.binary
    if (binary === undefined) return
    const outcome = await this.runner.read(binary, ['task', 'list', ...(this.showingAll ? ['--all'] : [])])
    const document = ReadOutcome.documentOf(outcome)
    if (document) {
      this.tasks = TaskSummary.list(document)
      this.listTotal = document.total
      this.listing = { case: 'read' }
    } else this.listing = { case: 'failed', message: ReadOutcome.failure(outcome) ?? 'the list could not be read' }
    if (this.selected === undefined && !this.runs.some((run) => run.isRunning)) this.selected = this.listed[0]?.id
    this.changed()
    if (this.selected !== undefined) await this.readStatus(this.selected)
  }

  /** Show all: every task the ledger holds, `task list --all`, from now on. */
  async showAll(): Promise<void> {
    this.showingAll = true
    await this.refresh()
  }

  private async readStatus(id: string): Promise<void> {
    const binary = this.binary
    if (binary === undefined) return
    const outcome = await this.runner.read(binary, ['task', 'status', id])
    if (this.selected !== id) return
    const document = ReadOutcome.documentOf(outcome)
    const view = document ? TaskView.read(document.value) : undefined
    if (view) this.status = view
    this.statusFailure = view ? undefined : (ReadOutcome.failure(outcome) ?? "the task's status could not be read")
    this.changed()
  }

  async select(id: string): Promise<void> {
    if (this.selected !== id) {
      this.status = undefined
      this.statusFailure = undefined
    }
    this.selected = id
    this.changed()
    await this.readStatus(id)
  }

  // MARK: - The app's runs

  /** The app's newest run for `id`, in flight or ended, a refused resume among them. */
  run(id: string): TaskRun | undefined {
    return this.runs.find((run) => run.target === id)
  }

  /** The app's run of `id` while its child still runs -- a resume from the click on, before its stream names the task. */
  private live(id: string): TaskRun | undefined {
    return this.runs.find((run) => run.isRunning && run.target === id)
  }

  /** The app's run of `id` in flight with its stream under way: its own view is the newest word on the task. */
  following(id: string): TaskRun | undefined {
    const live = this.live(id)
    return live?.state.view === undefined ? undefined : live
  }

  /** Whether the app's resume of `id` is starting up: its child running, its stream not yet at `task_started`. */
  resuming(id: string): boolean {
    const live = this.live(id)
    return live !== undefined && live.resumes === id && live.state.view === undefined
  }

  /**
   * The last word on `id` when no run of the app's follows it: the newer of
   * its `task status` and the view the app's newest run of it last held, by
   * `updated_at` -- the run's on a tie, the stand-in's reads keeping no state.
   */
  lastView(id: string): TaskView | undefined {
    const read = this.status?.id === id ? this.status : undefined
    const ran = this.runs.find((run) => run.target === id && run.state.view !== undefined)?.state.view
    if (!ran) return read
    if (!read) return ran
    return Date.parse(read.updatedAt) > Date.parse(ran.updatedAt) ? read : ran
  }

  /** Whether the page offers the stops: the app's run in flight -- a resume starting up among them, whose stop the CLI keeps -- or a task a process runs now. */
  stoppable(id: string): boolean {
    const live = this.live(id)
    if (live) return live.state.view === undefined ? live.resumes === id : TaskView.running.includes(live.state.status)
    const view = this.lastView(id)
    return view !== undefined && TaskView.isRunning(view)
  }

  /** Whether the page offers Resume: the states `task resume` continues, with its folder known, and no run of the app's in flight. */
  resumable(id: string): boolean {
    if (this.live(id)) return false
    const view = this.lastView(id)
    return view !== undefined && TaskView.isResumable(view) && view.folder !== ''
  }

  /** The task shown: the one chosen, else the first listed. */
  get shownID(): string | undefined {
    return this.selected ?? this.listed[0]?.id
  }

  /** Starts a run of `args`, followed live: the preset's run typed as it was recorded. */
  startRun(args: readonly string[]): void {
    this.follow(args, this.folder)
  }

  private follow(args: readonly string[], folder: string): void {
    const binary = this.binary
    if (binary === undefined) return
    const run = new TaskRun(args)
    this.runs = [run, ...this.runs]
    const job = this.starter.startJob(binary, args, true, folder)
    run.job = job
    this.changed()
    void (async () => {
      let shown = false
      for await (const update of job.updates) {
        run.state.apply(update)
        // A new run's task shown once its stream names it, as the old start and Run again show theirs.
        if (!shown && run.resumes === undefined && run.state.taskID !== undefined) {
          shown = true
          this.selected = run.state.taskID
        }
        this.changed()
      }
      run.job = undefined
      this.changed()
      await this.refresh()
      // Refused before it started: machine mode writes the refusal to stderr alone, so the run has no task and leaves no row.
      // Its words go to the alert; a refused resume's stay on its page.
      const refusal = run.state.refusal
      if (refusal !== undefined) this.refuse(refusal)
    })()
  }

  private refuse(refusal: string): void {
    this.failure = refusal
    this.say?.(`That didn't work: ${refusal}`)
    this.ask?.('taskRefused')
    this.changed()
  }

  // MARK: - Controls, each on the task shown

  /**
   * Stop after this round: `task halt <id>`. Where it lands is taken as it
   * is asked, before the command, as the old `haltAt` is: the turn the run's
   * stream is in, else -- between turns, or a resume not yet begun -- as
   * `haltAt` says.
   */
  async halt(): Promise<void> {
    const id = this.shownID
    if (id === undefined || !this.stoppable(id)) return
    const live = this.live(id)
    const view = live?.state.view
    const at = live === undefined || view === undefined ? 'start' : TasksViewModel.nextEnd(live.state.orderedRounds, live.state.roundsUsed ?? view.roundsUsed)
    if (await this.command(['task', 'halt', id])) {
      if (live?.isRunning) live.haltAt = at
      this.changed()
    }
  }

  /** Stop now: `task cancel <id>`. */
  async cancel(): Promise<void> {
    const id = this.shownID
    if (id === undefined || !this.stoppable(id)) return
    await this.command(['task', 'cancel', id])
  }

  /** Runs `args`, the list read back; a failure is the alert's, in the CLI's words. */
  private async command(args: readonly string[]): Promise<boolean> {
    const binary = this.binary
    if (binary === undefined) return false
    const outcome = await this.runner.command(binary, args)
    await this.refresh()
    if (outcome.case !== 'done') {
      this.refuse(CommandOutcome.failure(outcome) ?? `apogee ${args.join(' ')} did not succeed`)
      return false
    }
    return true
  }

  /** Resume: `task resume <id>`, in the folder the task was started in -- the CLI resumes a task nowhere else. */
  resume(): void {
    const id = this.shownID
    const view = id === undefined ? undefined : this.lastView(id)
    if (id === undefined || !view || !this.resumable(id)) return
    this.follow(['task', 'resume', id, '--output-format', 'stream-json'], view.folder)
  }

  /**
   * Run again with twice the rounds (the old prototype's, prototype-only): a
   * new `task run` with the same goal, checks, tools and grants and its
   * round budget doubled, at most 32, as a person would type it.
   */
  rerun(): void {
    const id = this.shownID
    const view = id === undefined ? undefined : this.lastView(id)
    if (id === undefined || !view || view.status !== 'exhausted' || this.live(id)) return
    const checks = view.checks.flatMap((check) => (check.kind === 'require' ? ['--require', check.value] : check.kind === 'require_file' ? ['--require-file', check.value] : []))
    const grants = view.grants.flatMap((grant) => ['--allow', grant])
    this.follow(['task', 'run', view.goal, ...checks, ...(view.tools ? ['--tools'] : []), ...grants, '--rounds', String(TasksViewModel.doubled(view.roundsBudget)), '--output-format', 'stream-json'], view.folder || this.folder)
  }

  /** The rounds Run again asks for: twice the budget, at most 32. */
  static doubled(budget: number): number {
    return Math.min(32, (budget || 8) * 2)
  }

  /** The alert's OK. */
  dismissFailure(): void {
    this.failure = undefined
    this.changed()
  }

  confirmWords(kind: TasksConfirm): { readonly title: string; readonly body: string; readonly confirm: string } | undefined {
    if (kind !== 'taskRefused' || this.failure === undefined) return undefined
    return { title: "That didn't work", body: this.failure, confirm: 'OK' }
  }

  // MARK: - The sheet

  /** New task…: the sheet opened fresh, then `task recipes` and the suites read for its pickers. */
  async openNew(): Promise<void> {
    this.form = NewTaskForm.opened(this.defaultSuite ?? this.suiteNames[0] ?? '')
    this.present?.('newtask')
    this.changed()
    await this.readSheet()
  }

  /** The sheet's pickers: `task recipes`, `config get suites` and the default `models status` names, which the suite starts on. */
  private async readSheet(): Promise<void> {
    const binary = this.binary
    if (binary === undefined) return
    const recipes = await this.runner.read(binary, ['task', 'recipes'])
    const document = ReadOutcome.documentOf(recipes)
    if (document) this.recipes = TaskRecipe.list(document)
    this.recipeFailure = document ? undefined : ReadOutcome.failure(recipes)
    const listed = await this.runner.command(binary, ['config', 'get', 'suites'])
    this.suiteNames = listed.case === 'done' ? Suites.names(listed.stdout) : []
    const status = ReadOutcome.documentOf(await this.runner.read(binary, ['models', 'status']))
    const said = status ? Field.list(status).find((field) => field.label === 'suite')?.value : undefined
    this.defaultSuite = said === undefined ? undefined : this.suiteNames.find((name) => said === name || said.startsWith(`${name}   (`))
    if (this.form && this.form.suite === '') this.form = this.form.with({ suite: this.defaultSuite ?? this.suiteNames[0] ?? '' })
    this.changed()
  }

  setForm(change: Partial<NewTaskFields>): void {
    if (!this.form) return
    this.form = this.form.with(change)
    this.changed()
  }

  /** A recipe picked -- `task recipes show <name>`, for its goal -- or none (`''`), for a goal written here. */
  async pickRecipe(name: string): Promise<void> {
    const binary = this.binary
    if (!this.form || binary === undefined) return
    if (name === '') {
      this.form = this.form.withRecipe(undefined)
      this.changed()
      return
    }
    const outcome = await this.runner.read(binary, ['task', 'recipes', 'show', name])
    const document = ReadOutcome.documentOf(outcome)
    const recipe = document ? TaskRecipe.read(document) : undefined
    if (!recipe || !this.form) {
      this.recipeFailure = ReadOutcome.failure(outcome) ?? 'the recipe could not be read'
      this.changed()
      return
    }
    this.recipeFailure = undefined
    this.form = this.form.withRecipe(recipe)
    this.changed()
  }

  /** Start task: the sheet closed, the line it shows run and followed live. */
  start(): void {
    const form = this.form
    if (!form || form.cantStart) return
    this.form = undefined
    this.dismissSheet?.()
    this.follow(form.arguments, this.folder)
  }

  /** Cancel: the sheet closed, nothing run. */
  cancelNew(): void {
    this.form = undefined
    this.dismissSheet?.()
    this.changed()
  }

  // MARK: - What the views draw

  /** The rows to list: the runs the ledger's last read has not caught, then the ledger's, each the app's run's word where it ran it. */
  private get listed(): Array<{ readonly id: string; readonly goal: string; readonly status: string; readonly words: string; readonly tone: TaskTone }> {
    const known = new Set(this.tasks.map((task) => task.id))
    const seen = new Set<string>()
    const fresh = this.runs.flatMap((run) => {
      const view = run.state.view
      if (!view || known.has(view.id) || seen.has(view.id)) return []
      seen.add(view.id)
      return [this.rowOf(view.id, view.goal, run)]
    })
    const ledger = this.tasks.map((task) => {
      const run = this.runs.find((each) => each.target === task.id && each.state.view !== undefined)
      if (run) return this.rowOf(task.id, task.goal, run)
      return { id: task.id, goal: task.goal, status: task.status, words: TaskWords.status(task.status, task.roundsUsed, task.roundsBudget), tone: TaskWords.tone(task.status) }
    })
    return [...fresh, ...ledger]
  }

  private rowOf(id: string, goal: string, run: TaskRun) {
    const view = run.state.view as TaskView
    const status = run.state.status
    const used = run.state.roundsUsed ?? view.roundsUsed
    const halting = run.isRunning ? TasksViewModel.halting(run, run.state.orderedRounds, used) : undefined
    return { id, goal, status, words: TaskWords.status(status, used, view.roundsBudget, halting), tone: TaskWords.tone(status) }
  }

  /**
   * The turn a halt asked now stops at the end of: the turn in flight -- the
   * plan (0) until it lands, else a round -- and, between turns, the next
   * one, as the CLI reads a halt only right after a turn's end line
   * (`runner.cpp`'s `stop_if_asked`) and starts the next round unchecked;
   * the old prototype's `haltAt` (the first turn's end past now) likewise.
   */
  private static nextEnd(rounds: readonly TaskRound[], used: number): number {
    const plan = rounds.find((round) => round.round === 0)
    return plan === undefined || plan.outcome === 'in_flight' ? 0 : used + 1
  }

  /** What the page says of `run`'s halt: where it was fixed to land, or, sent while a resume started up, before the turn it would run next. */
  private static halting(run: TaskRun, rounds: readonly TaskRound[], used: number): TaskHalt | undefined {
    const at = run.haltAt
    if (at === undefined) return undefined
    return at === 'start' ? { before: TasksViewModel.nextEnd(rounds, used) } : { after: at }
  }

  face(): TasksFace {
    const shown = this.shownID
    const list: TaskRowFace[] = this.listed.map((row) => ({ id: row.id, goal: row.goal, status: row.words, tone: row.tone, current: row.id === shown }))
    const detail = shown === undefined ? noDetail : this.detail(shown)
    const truncated = this.listing.case === 'read' && !this.showingAll && this.listTotal !== undefined && this.listTotal > this.tasks.length
    return {
      list,
      truncation: truncated ? `The newest ${this.tasks.length} of ${this.listTotal} tasks` : '',
      empty: this.listing.case === 'failed' ? `The tasks could not be read: ${this.listing.message}` : '',
      detail,
      failure: detail.shown ? '' : (this.statusFailure ?? ''),
    }
  }

  private detail(id: string): TaskDetailFace {
    const live = this.following(id)
    const view = live?.state.view ?? this.lastView(id)
    if (!view) return noDetail
    const state = live?.state
    const status = state ? state.status : view.status
    const used = state ? (state.roundsUsed ?? view.roundsUsed) : view.roundsUsed
    const budget = view.roundsBudget
    const rounds = state && state.orderedRounds.length > 0 ? state.orderedRounds : view.rounds
    const checks = state && state.checks.length > 0 ? state.checks : view.checks
    const plan = TaskWords.steps(state ? (state.plan ?? view.plan) : view.plan)
    const running = state ? TaskView.running.includes(status) : TaskView.isRunning(view)
    const interrupted = !this.live(id) && view.interrupted
    const halting = live ? TasksViewModel.halting(live, rounds, used) : undefined
    const resuming = this.resuming(id)
    const resumable = this.resumable(id)
    const lastAt = state?.lastAt ?? view.updatedAt
    const tokens = rounds.reduce((sum, round) => sum + round.tokens, 0)
    const latest = this.run(id)
    const grants = view.grants
    const allowedUses = rounds.flatMap((round) => round.allowed.map((use) => `${round.kind === 'plan' ? 'plan' : `round ${round.round}`}: ${TaskWords.allowed(use)}`))
    const liveGrants = (state?.grants ?? []).map((grant) => `${grant.round === 0 ? 'plan' : `round ${grant.round}`}: ${TaskWords.allowed(grant)}`)
    return {
      shown: true,
      id,
      goal: view.goal,
      meta: [`in ${view.folder || this.folder}`, `started ${TaskWords.started(view.createdAt, this.now())}`, grants.length > 0 ? `${grants.join(' and ')} granted` : 'nothing that writes granted', view.id].join(' · '),
      status: interrupted ? 'running, interrupted' : TaskWords.status(status, used, budget, halting),
      tone: interrupted ? 'warning' : TaskWords.tone(status),
      isRunning: running,
      canPause: this.stoppable(id) && !(live?.haltAsked ?? this.live(id)?.haltAsked ?? false),
      canStop: this.stoppable(id),
      canResume: resumable,
      isResuming: resuming,
      showWhere: resumable || resuming,
      resumeWhere: `in ${view.folder || this.folder}, where it was started`,
      canRerun: view.status === 'exhausted' && status === 'exhausted' && !this.live(id),
      rerunLabel: `Run again with ${TasksViewModel.doubled(budget)} rounds`,
      interrupted,
      interruptedLine: 'Its process is gone: it was killed mid-round, and Resume runs that round again.',
      budget: [
        { label: 'Rounds', value: `${used} of ${budget}`, pct: budget > 0 ? Math.min(100, Math.round((used / budget) * 100)) : 0, hasBar: true, tone: budget > 0 && used / budget >= 0.75 ? 'warning' : 'plain' },
        { label: 'Time so far', value: TaskWords.minutes(TaskWords.between(view.createdAt, lastAt)), pct: 0, hasBar: false, tone: 'plain' },
        { label: 'Tokens · local, no charge', value: tokens > 0 ? TaskWords.kilo(tokens) : '0', pct: 0, hasBar: false, tone: 'plain' },
      ],
      grants: { shown: view.tools, text: grants.length > 0 ? grants.join(', ') : 'none -- with nobody present, a tool that asks is denied' },
      plan,
      rounds: rounds.map((round) => this.roundFace(round, rounds, status, running, interrupted, plan.length)),
      allowed: [...allowedUses, ...liveGrants],
      denied: rounds.flatMap((round) => round.denied.map((use) => `${round.kind === 'plan' ? 'plan' : `round ${round.round}`}: ${TaskWords.denied(use)}`)),
      checks: checks.map((check) => ({
        cmd: `${check.kind === 'require' ? 'require · ' : 'require-file · '}${check.description}`,
        result: check.passed ? `passed${check.detail === '' ? '' : ` · ${check.detail}`}` : check.ran ? check.detail || 'not yet' : 'not run yet',
        tone: check.passed ? 'accent' : check.ran ? 'warning' : 'quiet',
      })),
      noChecks: checks.length === 0,
      activity: this.activity(latest, rounds),
      refused: latest && !latest.isRunning ? (latest.state.refusal ?? '') : '',
      perms: `A task never stops to ask. ${grants.length > 0 ? `${grants.join(' and ')}${grants.length === 1 ? ' was' : ' were'} granted when it started; ` : 'Nothing that writes was granted; '}anything else is refused, the task is told, and every use is recorded in its ledger.`,
    }
  }

  private roundFace(round: TaskRound, rounds: readonly TaskRound[], status: string, running: boolean, interrupted: boolean, steps: number): TaskRoundFace {
    const stopped = status === 'cancelled' ? 'cancelled' : 'stopped'
    const tokens = round.outcome === 'completed' && round.tokens > 0 ? TaskWords.kilo(round.tokens) : ''
    if (round.kind === 'plan') {
      if (round.outcome === 'completed') return { n: 'Plan', kind: 'plan', text: `the plan turn wrote ${steps} ${steps === 1 ? 'step' : 'steps'}`, outcome: 'recorded', tone: 'plain', tokens, denied: '' }
      const inFlight = round.outcome === 'in_flight' && (running || interrupted)
      return { n: 'Plan', kind: 'plan', text: 'writing the plan', outcome: inFlight ? 'in flight' : stopped, tone: round.outcome === 'in_flight' && running ? 'accent' : 'warning', tokens, denied: '' }
    }
    const n = `Round ${round.round}`
    if (round.outcome === 'completed') {
      const last = Math.max(...rounds.filter((each) => each.kind !== 'plan' && each.outcome === 'completed').map((each) => each.round))
      const done = status === 'done' && round.round === last
      const denied = round.denied.length > 0 ? `, ${round.denied.length} denied` : ''
      return {
        n,
        kind: round.kind,
        text: `${TaskWords.report(round.selfReport)}; tools: ${round.tools} ran${denied}`,
        outcome: done ? 'done' : 'not done',
        tone: done ? 'accent' : 'plain',
        tokens,
        denied: round.denied.length > 0 ? `denied: ${round.denied.map(TaskWords.denied).join('; ')}` : '',
      }
    }
    const inFlight = round.outcome === 'in_flight' && (running || interrupted)
    return { n, kind: round.kind, text: round.outcome === 'in_flight' && running ? 'working…' : '', outcome: inFlight ? 'in flight' : stopped, tone: round.outcome === 'in_flight' && running ? 'accent' : 'warning', tokens, denied: '' }
  }

  /** Recent activity: the app's run's tool calls by their last state; for a task it never ran, the ledger's gated calls, round by round, on whose authority each ran or why it was refused. */
  private activity(run: TaskRun | undefined, rounds: readonly TaskRound[]): TaskActivityFace[] {
    if (run) return run.state.calls.map((call) => ({ tool: call.tool, target: call.target, result: call.state, tone: call.state === 'running' ? 'accent' : call.state === 'failed' ? 'plain' : 'quiet' }))
    return rounds.flatMap((round) => [
      ...round.allowed.map((use): TaskActivityFace => ({ tool: use.tool, target: use.target, result: TaskWords.authority(use.by), tone: 'quiet' })),
      ...round.denied.map((use): TaskActivityFace => ({ tool: use.tool, target: use.target, result: TaskWords.refusal(use.by), tone: 'plain' })),
    ])
  }

  /** The New task sheet, as the old `ntv` draws it. */
  sheetFace(): NewTaskFace {
    const form = this.form ?? NewTaskForm.opened(this.defaultSuite ?? '')
    const recipe = form.recipe
    const seg = (current: 'none' | 'allow') => (['none', 'allow'] as const).map((value) => ({ value, label: value === 'none' ? 'Refused' : 'Granted', current: current === value }))
    return {
      shown: this.form !== undefined,
      goal: form.goal,
      suite: form.suite,
      suiteOptions: this.suiteNames,
      rounds: form.rounds,
      roundsOptions: ['4', '8', '16', '32'],
      require: form.require,
      requireFile: form.requireFile,
      folder: this.folder,
      writes: seg(form.writes),
      cmds: seg(form.cmds),
      command: form.command,
      recipe: recipe?.name ?? '',
      recipeOptions: [{ value: '', label: 'None: write the goal' }, ...this.recipes.map((each) => ({ value: each.name, label: each.name }))],
      hasRecipe: recipe !== undefined,
      recipeDesc: recipe?.description ?? '',
      recipeTakesInput: recipe?.takesInput ?? false,
      recipeInput: recipe?.input ?? '',
      input: form.input,
      grants: (recipe?.allow ?? []).map((name) => ({ name, on: form.given.includes(name) })),
      expansion: form.expansion,
      recipeFailure: this.recipeFailure ?? '',
      cantStart: form.cantStart,
    }
  }
}
