import { JSONValue } from '../../data/protocol/JSONValue'
import type { ReadDocument } from '../../data/protocol/ReadDocument'

/**
 * The Tasks place's reads (design 1p): the Mac app's `Tasks.swift` and
 * `Recipes.swift` (`BusinessReads`), their names and rules, over the
 * documents `task list`, `task status <id>`, `task recipes` and `task
 * recipes show <name>` print with `--output-format json`, and the task view
 * `task_started` and `task_finished` carry. Fields this client does not know
 * are passed over.
 */

const { array, bool, int, object, string } = JSONValue

const field = (value: JSONValue | undefined, key: string): JSONValue | undefined => object(value)?.[key]
const words = (value: JSONValue | undefined, key: string): string => string(field(value, key)) ?? ''
const strings = (value: JSONValue | undefined): string[] => (array(value) ?? []).flatMap((each) => (typeof each === 'string' ? [each] : []))

/** One row of `task list`. */
export interface TaskSummary {
  readonly id: string
  readonly goal: string
  /** `planning`, `running`, `done`, `exhausted`, `stalled`, `failed`, `halted`, `cancelled` -- a string that may grow. */
  readonly status: string
  readonly roundsUsed: number
  readonly roundsBudget: number
  /**
   * Whether the row says its task was killed mid-round, as the CLI's M32
   * puts it on every row; `undefined` from a binary before it, whose rows
   * don't say (the task's page still does).
   */
  readonly interrupted: boolean | undefined
}

export const TaskSummary = {
  list(document: ReadDocument): TaskSummary[] {
    return (document.entries ?? []).flatMap((entry): TaskSummary[] => {
      const id = string(field(entry, 'id'))
      if (id === undefined) return []
      return [{ id, goal: words(entry, 'goal'), status: words(entry, 'status'), roundsUsed: int(field(entry, 'rounds_used')) ?? 0, roundsBudget: int(field(entry, 'rounds_budget')) ?? 0, interrupted: bool(field(entry, 'interrupted')) }]
    })
  },

  /** Whether a process runs it now, as far as its row says: under way by its ledger, and not killed mid-round. */
  isRunning(task: TaskSummary): boolean {
    return TaskView.running.includes(task.status) && task.interrupted !== true
  },
}

/** A task's check: `require` or `require_file`, and how it last ran. */
export interface TaskCheck {
  readonly kind: string
  readonly value: string
  readonly description: string
  readonly ran: boolean
  readonly passed: boolean
  readonly detail: string
}

/**
 * One gated tool call a round let through or refused, as its document
 * carries it (`{"tool","target","by"}`); an entry that is a bare string is
 * kept as the tool's name.
 */
export interface TaskUse {
  readonly tool: string
  readonly target: string
  /** On whose authority: `grant`, `person` or `config` for a call let through; `config`, `nobody` or `person` for a refusal. */
  readonly by: string
}

/** One turn of the task: the plan (round 0) or a round. */
export interface TaskRound {
  readonly round: number
  /** `plan`, `execute`, `correct`. */
  readonly kind: string
  /** `in_flight`, `completed`, `interrupted` … */
  readonly outcome: string
  readonly checksPassed: number
  readonly selfReport: string
  readonly allowed: readonly TaskUse[]
  readonly denied: readonly TaskUse[]
  /** How many tool calls ran. */
  readonly tools: number
  readonly tokens: number
  readonly tokensEstimated: boolean
  readonly startedAt: string
  readonly endedAt: string
}

/** A task's whole view: what `task status <id> --output-format json` prints, and `task_started` and `task_finished` carry. */
export interface TaskView {
  readonly id: string
  readonly goal: string
  readonly status: string
  readonly reason: string
  readonly plan: string
  readonly roundsUsed: number
  readonly roundsBudget: number
  readonly checks: readonly TaskCheck[]
  readonly rounds: readonly TaskRound[]
  readonly conversation: string
  readonly folder: string
  readonly tools: boolean
  /** The tools granted up front: `policy.grants`, which the CLI writes only for a task with tools. */
  readonly grants: readonly string[]
  readonly createdAt: string
  readonly updatedAt: string
  /** The process running it, when one is. */
  readonly process: number | undefined
  /** Planning or running by its ledger with no process running it: killed mid-round, and resumable. */
  readonly interrupted: boolean
}

export const TaskView = {
  /** The statuses a task is still under way in, by its ledger. */
  running: ['planning', 'running'] as readonly string[],
  /** The stopped statuses `task resume` continues; never done or exhausted. */
  resumable: ['halted', 'cancelled', 'stalled', 'failed'] as readonly string[],

  read(value: JSONValue | undefined): TaskView | undefined {
    const id = string(field(value, 'id'))
    if (id === undefined) return undefined
    return {
      id,
      goal: words(value, 'goal'),
      status: words(value, 'status'),
      reason: words(value, 'reason'),
      plan: words(value, 'plan'),
      roundsUsed: int(field(value, 'rounds_used')) ?? 0,
      roundsBudget: int(field(value, 'rounds_budget')) ?? 0,
      checks: TaskView.checks(field(value, 'checks')),
      rounds: (array(field(value, 'turns')) ?? []).map(TaskView.round),
      conversation: words(value, 'conversation'),
      folder: words(value, 'folder'),
      tools: bool(field(value, 'tools')) ?? false,
      grants: strings(field(field(value, 'policy'), 'grants')),
      createdAt: words(value, 'created_at'),
      updatedAt: words(value, 'updated_at'),
      process: int(field(value, 'process')),
      interrupted: bool(field(value, 'interrupted')) ?? false,
    }
  },

  checks(value: JSONValue | undefined): TaskCheck[] {
    return (array(value) ?? []).map((check) => ({
      kind: words(check, 'kind'),
      value: words(check, 'value'),
      description: words(check, 'description'),
      ran: bool(field(check, 'ran')) ?? false,
      passed: bool(field(check, 'passed')) ?? false,
      detail: words(check, 'detail'),
    }))
  },

  round(value: JSONValue): TaskRound {
    return {
      round: int(field(value, 'round')) ?? 0,
      kind: words(value, 'kind'),
      outcome: words(value, 'outcome'),
      checksPassed: int(field(value, 'checks_passed')) ?? 0,
      selfReport: words(value, 'self_report'),
      allowed: TaskView.uses(field(value, 'allowed')),
      denied: TaskView.uses(field(value, 'denied')),
      tools: int(field(value, 'tools')) ?? 0,
      tokens: int(field(value, 'tokens')) ?? 0,
      tokensEstimated: bool(field(value, 'tokens_estimated')) ?? false,
      startedAt: words(value, 'started_at'),
      endedAt: words(value, 'ended_at'),
    }
  },

  /** The entries of a turn's `allowed` or `denied`: an object by its fields, a bare string as the tool's name; any other shape names no tool. */
  uses(value: JSONValue | undefined): TaskUse[] {
    return (array(value) ?? []).flatMap((entry): TaskUse[] => {
      if (typeof entry === 'string') return [{ tool: entry, target: '', by: '' }]
      if (object(entry) === undefined) return []
      return [{ tool: words(entry, 'tool'), target: words(entry, 'target'), by: words(entry, 'by') }]
    })
  },

  /** Whether a process runs it now: under way by its ledger, and not killed mid-round. */
  isRunning(view: TaskView): boolean {
    return TaskView.running.includes(view.status) && !view.interrupted
  },

  /** Whether `task resume` continues it: halted, cancelled, stalled, failed, or killed mid-round -- never done or exhausted. */
  isResumable(view: TaskView): boolean {
    return TaskView.resumable.includes(view.status) || (TaskView.running.includes(view.status) && view.interrupted)
  },
}

/**
 * A task recipe, as `task recipes [show <name>]` carries it (the CLI's 40c):
 * a named goal with its checks baked in, which `task run --recipe <name>
 * [--input …]` expands into the flags a hand-typed run takes. Its grants are
 * suggestions, given only when the line gives them.
 */
export interface TaskRecipe {
  readonly name: string
  readonly description: string
  /** `shipped`, `seeded` or `user`. */
  readonly source: string
  readonly path: string | undefined
  /** What `--input` is for; empty when the goal reads none. */
  readonly input: string
  readonly tools: boolean
  readonly require: readonly string[]
  readonly requireFiles: readonly string[]
  /** The grants it suggests, never assumes. */
  readonly allow: readonly string[]
  readonly agent: string
  readonly rounds: number | undefined
  /** The goal's template, `{{input}}` where the input goes -- `show`'s alone. */
  readonly goal: string | undefined
  /** Whether the goal reads an input. */
  readonly takesInput: boolean
}

export const TaskRecipe = {
  of(value: JSONValue): TaskRecipe | undefined {
    const name = string(field(value, 'name'))
    if (name === undefined) return undefined
    const input = words(value, 'input')
    return {
      name,
      description: words(value, 'description'),
      source: words(value, 'source'),
      path: string(field(value, 'path')),
      input,
      tools: bool(field(value, 'tools')) ?? false,
      require: strings(field(value, 'require')),
      requireFiles: strings(field(value, 'require_files')),
      allow: strings(field(value, 'allow')),
      agent: words(value, 'agent'),
      rounds: int(field(value, 'rounds')),
      goal: string(field(value, 'goal')),
      takesInput: input !== '',
    }
  },

  /** `task recipes show <name>`'s document: one `task.recipe`; `undefined` for anything else. */
  read(document: ReadDocument): TaskRecipe | undefined {
    return document.object === 'task.recipe' ? TaskRecipe.of(document.value) : undefined
  },

  list(document: ReadDocument): TaskRecipe[] {
    return (document.entries ?? []).flatMap((entry) => {
      const recipe = TaskRecipe.of(entry)
      return recipe ? [recipe] : []
    })
  },

  /** The goal with every `{{…}}` read as the input, as the CLI renders it; `undefined` without a goal. */
  renderedGoal(recipe: TaskRecipe, input: string): string | undefined {
    return recipe.goal?.replace(/\{\{[^}]*\}\}/g, () => input)
  },
}
