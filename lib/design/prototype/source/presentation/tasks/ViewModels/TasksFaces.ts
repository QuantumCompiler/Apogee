/** How a word is colored: the accent, a warning, the error, the secondary text, or the tertiary. */
export type TaskTone = 'accent' | 'warning' | 'error' | 'plain' | 'quiet'

/** One task in the list: its goal, its status in the old words, and the dot's and the words' color. */
export interface TaskRowFace {
  readonly id: string
  readonly goal: string
  readonly status: string
  readonly tone: TaskTone
  readonly current: boolean
}

export interface TaskBudgetFace {
  readonly label: string
  readonly value: string
  readonly pct: number
  readonly hasBar: boolean
  readonly tone: TaskTone
}

/** One turn on the task's page: the plan or a round, what it did, how it came out, its tokens, and the calls it refused. */
export interface TaskRoundFace {
  readonly n: string
  readonly kind: string
  readonly text: string
  readonly outcome: string
  readonly tone: TaskTone
  readonly tokens: string
  /** `denied: …` in `task status`'s words; empty when the round refused nothing. */
  readonly denied: string
}

export interface TaskCheckFace {
  readonly cmd: string
  readonly result: string
  readonly tone: TaskTone
}

/** One line of Recent activity: a tool call, what it was called on, and how it came out. */
export interface TaskActivityFace {
  readonly tool: string
  readonly target: string
  readonly result: string
  readonly tone: TaskTone
}

/** The task's page, as the old `taskView` draws it. */
export interface TaskDetailFace {
  readonly shown: boolean
  readonly id: string
  readonly goal: string
  readonly meta: string
  readonly status: string
  readonly tone: TaskTone
  /** A process runs it now: the pulse beside its status. */
  readonly isRunning: boolean
  readonly canPause: boolean
  readonly canStop: boolean
  readonly canResume: boolean
  readonly isResuming: boolean
  readonly showWhere: boolean
  readonly resumeWhere: string
  readonly canRerun: boolean
  readonly rerunLabel: string
  readonly interrupted: boolean
  readonly interruptedLine: string
  readonly budget: readonly TaskBudgetFace[]
  /** The Grants section: shown for a task with tools, as `task status` gives it the line. */
  readonly grants: { readonly shown: boolean; readonly text: string }
  readonly plan: ReadonlyArray<{ readonly n: string; readonly text: string }>
  readonly rounds: readonly TaskRoundFace[]
  readonly allowed: readonly string[]
  readonly denied: readonly string[]
  readonly checks: readonly TaskCheckFace[]
  readonly noChecks: boolean
  readonly activity: readonly TaskActivityFace[]
  /** A refused resume's line, kept on the page; empty when none was refused. */
  readonly refused: string
  readonly perms: string
}

/** Everything the Tasks place draws. */
export interface TasksFace {
  readonly list: readonly TaskRowFace[]
  /** "The newest 50 of 60 tasks", beside Show all; empty when the list shows every task. */
  readonly truncation: string
  /** The list's line when it could not be read, in the CLI's words; empty otherwise. */
  readonly empty: string
  readonly detail: TaskDetailFace
  /** Why the chosen task's page could not be read, in the CLI's words, where there is no page to draw; empty otherwise. */
  readonly failure: string
}

/** A segmented choice's option, as the old `seg` draws it. */
export interface SheetChoiceFace {
  readonly value: 'none' | 'allow'
  readonly label: string
  readonly current: boolean
}

/** The New task sheet, as the old `ntv` draws it. */
export interface NewTaskFace {
  readonly shown: boolean
  readonly goal: string
  readonly suite: string
  readonly suiteOptions: readonly string[]
  readonly rounds: string
  readonly roundsOptions: readonly string[]
  readonly require: string
  readonly requireFile: string
  readonly folder: string
  readonly writes: readonly SheetChoiceFace[]
  readonly cmds: readonly SheetChoiceFace[]
  readonly command: string
  readonly recipe: string
  readonly recipeOptions: ReadonlyArray<{ readonly value: string; readonly label: string }>
  readonly hasRecipe: boolean
  readonly recipeDesc: string
  readonly recipeTakesInput: boolean
  readonly recipeInput: string
  readonly input: string
  readonly grants: ReadonlyArray<{ readonly name: string; readonly on: boolean }>
  readonly expansion: string
  /** Why a recipe could not be read, in the CLI's words; empty otherwise. */
  readonly recipeFailure: string
  readonly cantStart: boolean
}

/** The alert's kind: a start or resume the CLI refused before it began. */
export type TasksConfirm = 'taskRefused'
