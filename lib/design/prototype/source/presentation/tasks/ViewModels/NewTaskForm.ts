import { TaskRecipe } from '../../../business/reads/Tasks'

/** A grant the sheet's two segmented choices give: Refused or Granted. */
export type Grant = 'none' | 'allow'

/** What the sheet holds, field by field: the old `newTask` state's shape. */
export interface NewTaskFields {
  readonly goal: string
  readonly suite: string
  /** `4`, `8`, `16` or `32`: the old select's words. */
  readonly rounds: string
  readonly writes: Grant
  readonly cmds: Grant
  readonly require: string
  readonly requireFile: string
  readonly input: string
  /** The recipe's suggested grants the person ticked. */
  readonly grants: readonly string[]
}

/**
 * The New task sheet (design 1p): the goal or a recipe, its input, the
 * checks, the folder, the suite, the rounds and what it may do -- and the
 * command line it shows, which is the run it starts, and a recipe's "The
 * same run as typing". The line is ported word for word from the old
 * prototype's `tasks` section (`main.html` at `b375fac9`, `ntParts` and
 * `expParts`): its order (the goal first, as `task run "say hello" …` was
 * typed for the recording), its quoting and its flags; `arguments` are the
 * same words, unquoted, the ones the run is started with. Where the Mac's
 * `NewTaskForm` differs -- a model field, several checks a line, any rounds,
 * the goal after `--` -- the old sheet stands. Plain TypeScript, immutable.
 */
export class NewTaskForm implements NewTaskFields {
  private constructor(
    readonly goal: string,
    readonly suite: string,
    readonly rounds: string,
    readonly writes: Grant,
    readonly cmds: Grant,
    readonly require: string,
    readonly requireFile: string,
    readonly input: string,
    readonly grants: readonly string[],
    /** The recipe the run is, `task recipes show`'s document; `undefined` for a goal written here. */
    readonly recipe: TaskRecipe | undefined,
  ) {}

  /** The sheet as New task… opens it (the old `openNew`): nothing written, writing files granted, commands refused, 8 rounds, under `suite`. */
  static opened(suite: string): NewTaskForm {
    return new NewTaskForm('', suite, '8', 'allow', 'none', '', '', '', [], undefined)
  }

  /** The same sheet with `change` made. */
  with(change: Partial<NewTaskFields>): NewTaskForm {
    const next = { ...this.fields, ...change }
    return new NewTaskForm(next.goal, next.suite, next.rounds, next.writes, next.cmds, next.require, next.requireFile, next.input, next.grants, this.recipe)
  }

  /** A recipe picked, or none (the old `onRecipe`): its input and grants cleared, the rounds back to 8. */
  withRecipe(recipe: TaskRecipe | undefined): NewTaskForm {
    const next = this.fields
    return new NewTaskForm(next.goal, next.suite, '8', next.writes, next.cmds, next.require, next.requireFile, '', [], recipe)
  }

  private get fields(): NewTaskFields {
    return { goal: this.goal, suite: this.suite, rounds: this.rounds, writes: this.writes, cmds: this.cmds, require: this.require, requireFile: this.requireFile, input: this.input, grants: this.grants }
  }

  /** The old `shq`: a word of the safe characters as it is, any other single-quoted, its quotes escaped. */
  static shq(word: string): string {
    return /^[A-Za-z0-9_./:=@%+,-]+$/.test(word) ? word : `'${word.replace(/'/g, "'\\''")}'`
  }

  /** The recipe's grants the person ticked, in the recipe's order: each an `--allow`, never one it only suggests. */
  get given(): readonly string[] {
    const recipe = this.recipe
    return recipe ? recipe.allow.filter((grant) => this.grants.includes(grant)) : []
  }

  /** The grants the line asks, in its `--allow` order. */
  get asked(): readonly string[] {
    if (this.recipe) return this.given
    return [...(this.writes === 'allow' ? ['write_file'] : []), ...(this.cmds === 'allow' ? ['run_command'] : [])]
  }

  /** The line's parts: each as the line shows it and the words it runs. */
  private get parts(): ReadonlyArray<readonly [string, readonly string[]]> {
    const recipe = this.recipe
    const input = this.input.trim()
    const parts: Array<readonly [string, readonly string[]]> = recipe
      ? [[`apogee task run --recipe ${recipe.name}`, ['task', 'run', '--recipe', recipe.name]]]
      : [
          ['apogee task run', ['task', 'run']],
          [`"${(this.goal.trim() || '…').replace(/"/g, '\\"')}"`, [this.goal.trim()]],
          ['--tools', ['--tools']],
        ]
    if (recipe?.takesInput) parts.push([`--input ${NewTaskForm.shq(input || '…')}`, ['--input', input]])
    const require = this.require.trim()
    if (require !== '') parts.push([`--require "${require.replace(/"/g, '\\"')}"`, ['--require', require]])
    const requireFile = this.requireFile.trim()
    if (requireFile !== '') parts.push([`--require-file ${requireFile}`, ['--require-file', requireFile]])
    if (this.rounds !== '8') parts.push([`--rounds ${this.rounds}`, ['--rounds', this.rounds]])
    for (const grant of this.asked) parts.push([`--allow ${grant}`, ['--allow', grant]])
    // No suite read (no apogee): none named, never an empty --suite.
    if (this.suite !== '') parts.push([`--suite ${this.suite}`, ['--suite', this.suite]])
    parts.push(['--output-format stream-json', ['--output-format', 'stream-json']])
    return parts
  }

  /** The command line the sheet shows: the old `nt.command`. */
  get command(): string {
    return this.parts.map(([shown]) => shown).join(' ')
  }

  /** The words the run is started with, after `apogee`: the line's, unquoted. */
  get arguments(): string[] {
    return this.parts.flatMap(([, words]) => [...words])
  }

  /**
   * A recipe as the hand-typed run it is (the old `nt.expansion`): its goal
   * rendered, its checks first, then the typed ones, `--tools`, its rounds,
   * the grants and the suite. Empty without a recipe. Shown, never run.
   */
  get expansion(): string {
    const recipe = this.recipe
    if (!recipe) return ''
    const input = this.input.trim()
    const parts = ['apogee task run', NewTaskForm.shq(TaskRecipe.renderedGoal(recipe, input || '…') ?? '')]
    for (const text of recipe.require) parts.push(`--require ${NewTaskForm.shq(text)}`)
    for (const file of recipe.requireFiles) parts.push(`--require-file ${NewTaskForm.shq(file)}`)
    if (this.require.trim() !== '') parts.push(`--require ${NewTaskForm.shq(this.require.trim())}`)
    if (this.requireFile.trim() !== '') parts.push(`--require-file ${NewTaskForm.shq(this.requireFile.trim())}`)
    if (recipe.tools) parts.push('--tools')
    parts.push(`--rounds ${this.rounds}`)
    for (const grant of this.given) parts.push(`--allow ${grant}`)
    if (this.suite !== '') parts.push(`--suite ${this.suite}`)
    return parts.join(' ')
  }

  /** Start stays off with no goal, or with a recipe that takes input and has none. */
  get cantStart(): boolean {
    const recipe = this.recipe
    return recipe ? recipe.takesInput && this.input.trim() === '' : this.goal.trim() === ''
  }
}
