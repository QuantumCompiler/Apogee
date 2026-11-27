import type { JobEnd, JobUpdate } from '../../../business/jobs/SupervisedJob'
import { TaskView } from '../../../business/reads/Tasks'
import type { TaskCheck, TaskRound } from '../../../business/reads/Tasks'
import { JSONValue } from '../../../data/protocol/JSONValue'
import type { JSONObject } from '../../../data/protocol/JSONValue'
import type { JobEvent } from '../../../data/protocol/JobEvent'

const { int, object, string } = JSONValue

/** One of the ledger's transitions, as the stream says it. */
export interface TaskTransition {
  readonly at: string
  readonly event: string
  readonly status: string
  readonly round: number | undefined
  readonly detail: string | undefined
}

/** A call a grant let through in the round in flight, as `task_grant` said it; kept only until that round's `task_round` lands. */
export interface TaskGrant {
  readonly round: number
  readonly tool: string
  readonly target: string
  /** On whose authority, as the event says: `grant`. */
  readonly by: string
}

/** A tool call the run made, as its `tool_call` lines say it: its last state (`pending`, `running`, `done`, `failed`). */
export interface TaskCall {
  readonly id: string
  readonly tool: string
  readonly target: string
  readonly state: string
}

/**
 * A live run folded from its stream, and nothing else (design 1p): the Mac
 * app's `TaskRunState` (`TasksViewModel.swift`) -- the ledger's transitions
 * as they arrive, the plan, each round, the checks, the grants, and what the
 * turn in flight is saying -- and the run's tool calls by their last state,
 * which the old prototype's Recent activity draws. Plain TypeScript.
 */
export class TaskRunState {
  taskID: string | undefined = undefined
  /** The view as `task_started` gave it, and `task_finished` last. */
  view: TaskView | undefined = undefined
  readonly transitions: TaskTransition[] = []
  plan: string | undefined = undefined
  /** Every turn by its round: those `task_started` carried, then each a `task_round` or `task_plan` brought. */
  private readonly rounds = new Map<number, TaskRound>()
  checks: readonly TaskCheck[] = []
  /** The round in flight's `task_grant` lines. */
  grants: TaskGrant[] = []
  roundsUsed: number | undefined = undefined
  /** The answer streaming in the turn now. */
  answer = ''
  /** Lines the run said: notices, tool lines, a turn's error, a failed end. */
  readonly activity: string[] = []
  /** The tool calls, in the order they were first made. */
  readonly calls: TaskCall[] = []
  finished: { readonly status: string; readonly reason: string } | undefined = undefined
  /** How the child ended, once it has. */
  exit: JobEnd | undefined = undefined

  /** The status now: the last transition's. */
  get status(): string {
    return this.transitions.at(-1)?.status ?? this.view?.status ?? 'starting'
  }

  /** When the ledger last changed, as the run's transitions stamp it: what Time so far runs to. */
  get lastAt(): string | undefined {
    return this.transitions.at(-1)?.at
  }

  /**
   * Why the CLI refused the run before it started, in its words: a run that
   * ended without success, not stopped by its Stop, whose stream never named
   * its task -- machine mode writes such a refusal to stderr alone -- so it is
   * the tail's last line, `apogee task: ` and all.
   */
  get refusal(): string | undefined {
    const end = this.exit
    if (!end || TaskRunState.succeeded(end) || end.interrupted || this.taskID !== undefined) return undefined
    return TaskRunState.endLine(end)
  }

  /** The rounds in order: the plan first. */
  get orderedRounds(): TaskRound[] {
    return [...this.rounds.keys()].sort((a, b) => a - b).map((round) => this.rounds.get(round) as TaskRound)
  }

  private static succeeded(end: JobEnd): boolean {
    return end.exit !== undefined && end.exit.reason === 'exited' && end.exit.status === 0
  }

  /** The one line a failed end says: stderr's last, else why it never started, else its exit. */
  static endLine(end: JobEnd): string {
    return end.stderrTail.lastLine ?? end.detail ?? `the task's apogee ended (${end.exit?.status ?? 'did not start'})`
  }

  apply(update: JobUpdate): void {
    switch (update.case) {
      case 'ended':
        this.exit = update.end
        if (this.finished === undefined && !TaskRunState.succeeded(update.end)) this.activity.push(TaskRunState.endLine(update.end))
        return
      case 'said':
        this.activity.push(update.line)
        return
      case 'busy':
        return
      case 'event':
        this.applyEvent(update.event)
        return
    }
  }

  private applyEvent(event: JobEvent): void {
    switch (event.case) {
      case 'notice':
        this.activity.push(event.text)
        return
      case 'error':
        this.activity.push(event.message)
        return
      case 'other':
        this.applyOther(event.type, event.fields)
        return
      default:
        return
    }
  }

  private applyOther(type: string, fields: JSONObject): void {
    const id = string(fields.task_id)
    if (id !== undefined) this.taskID = id
    const transition = object(fields.transition)
    if (transition) this.record(transition)
    const round = object(fields.round)
    if (round) {
      const parsed = TaskView.round(round)
      this.rounds.set(parsed.round, parsed)
      // Landed: its `allowed` now carries what its grants let through.
      if (parsed.outcome !== 'in_flight') this.grants = this.grants.filter((grant) => grant.round !== parsed.round)
    }
    if (fields.checks !== undefined) this.checks = TaskView.checks(fields.checks)
    const used = int(fields.rounds_used)
    if (used !== undefined) this.roundsUsed = used
    switch (type) {
      case 'task_started': {
        const history = JSONValue.array(fields.history) ?? []
        const at = this.transitions.length - (transition ? 1 : 0)
        this.transitions.splice(at, 0, ...history.flatMap((entry) => (object(entry) ? [TaskRunState.transition(object(entry) as JSONObject)] : [])))
        this.view = TaskView.read(fields.task)
        if (this.view && this.checks.length === 0) this.checks = this.view.checks
        // A resumed task's plan and earlier rounds, keyed by round: the stream's own rounds add to them, never replace them.
        for (const turn of this.view?.rounds ?? []) if (!this.rounds.has(turn.round)) this.rounds.set(turn.round, turn)
        return
      }
      case 'task_plan': {
        const plan = string(fields.plan)
        if (plan !== undefined) this.plan = plan
        return
      }
      case 'task_round':
        if (string(object(fields.transition)?.event) === 'round_started') this.answer = ''
        return
      case 'task_grant':
        this.grants.push({ round: int(fields.round) ?? 0, tool: string(fields.tool) ?? '', target: string(fields.target) ?? '', by: string(fields.by) ?? 'grant' })
        return
      case 'task_finished':
        this.finished = { status: string(fields.status) ?? this.status, reason: string(fields.reason) ?? '' }
        this.view = TaskView.read(fields.task) ?? this.view
        return
      case 'answer_start':
        this.answer = ''
        return
      case 'answer_delta':
        this.answer += string(fields.text) ?? ''
        return
      case 'tool_status': {
        const text = string(fields.text)
        if (text !== undefined) this.activity.push(text)
        return
      }
      case 'tool_call': {
        const call = string(fields.id) ?? ''
        const known = this.calls.findIndex((each) => each.id === call)
        const made: TaskCall = { id: call, tool: string(fields.name) ?? '', target: string(fields.target) ?? '', state: string(fields.state) ?? '' }
        if (known < 0) this.calls.push(made)
        else this.calls[known] = made
        return
      }
      default:
        return
    }
  }

  private record(value: JSONObject): void {
    this.transitions.push(TaskRunState.transition(value))
  }

  private static transition(value: JSONObject): TaskTransition {
    return { at: string(value.at) ?? '', event: string(value.event) ?? '', status: string(value.status) ?? '', round: int(value.round), detail: string(value.detail) }
  }
}
