import type { JobEnd, JobRunning, JobStarting } from '../../../business/jobs/SupervisedJob'
import type { CommandRunning } from '../../../business/reads/CommandRunning'
import { ChildExit } from '../../../infrastructure/process/ChildExit'
import { Observable } from '../../styles/Observation/Observable'

/** What a line of the transcript is: the command, its stdout, its stderr kept at the end, the console's own word, a refusal or failure. */
export type ConsoleLineKind = 'command' | 'out' | 'err' | 'note' | 'problem'

/** One line of the transcript: the Mac app's `ConsoleViewModel.Line`. */
export interface ConsoleLine {
  readonly id: number
  readonly kind: ConsoleLineKind
  readonly text: string
}

/** A line split as the CLI's exec line splits it, with the word being typed apart: the Mac app's `Split`. */
export interface Split {
  readonly words: readonly string[]
  /** The word being typed, as the binary is given it: empty when the line ends in a blank. */
  readonly partial: string
  /** How many characters of the line come before the word being typed, its opening quote counted. */
  readonly partialStart: number
}

/** Everything the console's view draws. */
export interface ConsoleFace {
  readonly lines: readonly ConsoleLine[]
  /** Nothing run yet: the console says what it is. */
  readonly empty: boolean
  readonly running: { readonly shown: boolean; readonly busy: string }
  readonly input: string
  readonly suggestions: readonly string[]
  readonly canRun: boolean
}

/** The CLI's blanks, and only these: a space, a tab, a line feed and a carriage return. */
const isBlank = (character: string) => character === ' ' || character === '\t' || character === '\n' || character === '\r'

/** What a backslash escapes inside double quotes: these four alone, as the CLI's splitter has it. */
const escapedInDoubleQuotes = new Set(['"', '\\', '$', '`'])

/** The root options that take the word after them as their value, unless written `--config=PATH`. */
const valuedRootOptions = new Set(['--config', '--custom'])

/**
 * The console's refusals: the prototype's three words and its reasons
 * (`main.html`'s console section at `b375fac9`), kept here as a small table.
 * The Mac app refuses through the shell's whole parity table
 * (`ParityTable.refusal`), which is not ported (design 1g's default).
 */
const refusals: Readonly<Record<string, string>> = {
  serve: 'a daemon the app would orphan when it quits -- run it at a prompt, or under a service manager',
  reset: 'deletes the data directory the running app stands on -- run it at a prompt, with the app closed',
  uninstall: 'removes Apogee and the data directory the app drives -- the app never uninstalls the binary it runs; run it at a prompt, with the app closed',
}

/**
 * The console (design 1g): any `apogee` command run as typed -- the parity
 * law's catch-all while screens accrete. The Mac app's `ConsoleViewModel`
 * (`PresentationConsole`), its names: a line is split as the CLI's exec line
 * splits it and run with every word it passes, as a job, its stdout said line
 * by line and its stderr kept at the end; Stop is Ctrl-C; Tab asks the binary
 * (`apogee __complete <words> <partial>`), and its answer applies only to the
 * line it was asked about. A command the table refuses is refused with its
 * reason and never started, whatever root options precede it. Plain
 * TypeScript over `CommandRunning` and `JobStarting`: it never imports React.
 */
export class ConsoleViewModel extends Observable {
  /** What a bare `apogee` is told. */
  static readonly nothingToRun = 'Nothing to run: type a command after apogee -- Tab offers them, and --help lists them all.'

  input = ''
  lines: readonly ConsoleLine[] = []
  /** The binary's candidates for the word being typed. */
  suggestions: readonly string[] = []
  /** The command line running now, and its busy line. */
  running: string | undefined = undefined
  busy: string | undefined = undefined
  /** The lines run, newest last, for the arrow keys. */
  history: readonly string[] = []

  private binary: string | undefined = undefined
  private job: JobRunning | undefined = undefined
  private next = 0

  constructor(
    private readonly runner: CommandRunning,
    private readonly starter: JobStarting,
  ) {
    super()
  }

  connect(binary: string | undefined): void {
    this.binary = binary
  }

  get isRunning(): boolean {
    return this.job !== undefined
  }

  /** The person typed: the line, its offered words let go (the old `onInput`). */
  setInput(text: string): void {
    this.update(() => {
      this.input = text
      this.suggestions = []
    })
  }

  // MARK: - The line

  /** The line split as the CLI's splitter splits it, or why it does not split, in the CLI's words (`line_tokens.cpp`). */
  static tokens(line: string): Split | { readonly unsplittable: string } {
    const characters = [...line]
    const words: string[] = []
    let word = ''
    let inWord = false
    let start = 0
    let quote: '"' | "'" | undefined
    for (let at = 0; at < characters.length; at += 1) {
      const character = characters[at] ?? ''
      if (quote === "'") {
        if (character === "'") quote = undefined
        else word += character
      } else if (quote === '"') {
        const after = characters[at + 1]
        if (character === '"') quote = undefined
        else if (character === '\\' && after !== undefined && escapedInDoubleQuotes.has(after)) {
          word += after
          at += 1
        } else word += character
      } else if (character === '\\') {
        const after = characters[at + 1]
        if (after === undefined) return { unsplittable: 'a backslash ends the line with nothing to escape' }
        if (!inWord) start = at
        word += after
        inWord = true
        at += 1
      } else if (character === '"' || character === "'") {
        if (!inWord) start = at
        quote = character
        inWord = true
      } else if (isBlank(character)) {
        if (inWord) words.push(word)
        word = ''
        inWord = false
      } else {
        if (!inWord) start = at
        word += character
        inWord = true
      }
    }
    if (quote !== undefined) return { unsplittable: quote === "'" ? 'a single quote is left open' : 'a double quote is left open' }
    return { words, partial: inWord ? word : '', partialStart: inWord ? start : characters.length }
  }

  /** The line split, with the typed start of the word being typed; `undefined` where it does not split. */
  static split(line: string): Split | undefined {
    const split = ConsoleViewModel.tokens(line)
    return 'unsplittable' in split ? undefined : split
  }

  /** The arguments a line runs: every word the exec line would pass, the last one too whenever it was begun, without a leading `apogee`. */
  static argumentsOf(line: string): string[] | undefined {
    const split = ConsoleViewModel.split(line)
    if (!split) return undefined
    const begun = split.partialStart < [...line].length
    const all = [...split.words, ...(begun ? [split.partial] : [])]
    if (all[0] === 'apogee') all.shift()
    return all
  }

  /** The command's own words: `args` past the root options, as the binary reads them. */
  static commandPath(args: readonly string[]): string[] {
    let at = 0
    while (at < args.length && (args[at] ?? '').startsWith('-')) at += valuedRootOptions.has(args[at] ?? '') ? 2 : 1
    return args.slice(Math.min(at, args.length))
  }

  /** Why the console does not run a command path, from the prototype's three; `undefined` where it runs. */
  static refusal(path: readonly string[]): string | undefined {
    const verb = path[0]
    return verb === undefined ? undefined : refusals[verb]
  }

  /** A word as the line splits it back into that word: bare, else double-quoted with any `"` or `\` escaped. */
  static quoted(word: string): string {
    if (word !== '' && ![...word].some((character) => isBlank(character) || character === '"' || character === "'" || character === '\\')) return word
    return `"${word.replace(/["\\]/g, (character) => `\\${character}`)}"`
  }

  // MARK: - Completion

  /**
   * Asks the binary what may come next: `apogee __complete <words>
   * <partial>`. One candidate completes the word; several are offered. An
   * answer applies only to the line it was asked about.
   */
  async complete(): Promise<void> {
    const asked = this.input
    const split = ConsoleViewModel.split(asked)
    if (this.binary === undefined || !split) {
      this.update(() => (this.suggestions = []))
      return
    }
    const words = [...split.words]
    if (words[0] === 'apogee') words.shift()
    const outcome = await this.runner.command(this.binary, ['__complete', ...words, split.partial])
    if (this.input !== asked) return
    if (outcome.case !== 'done') {
      this.update(() => (this.suggestions = []))
      return
    }
    const candidates = outcome.stdout.split(/\r?\n/).filter((candidate) => candidate !== '')
    if (candidates.length === 1 && candidates[0] !== undefined) this.accept(candidates[0])
    else this.update(() => (this.suggestions = candidates))
  }

  /** Puts a candidate in place of the word being typed -- all of it as it was typed -- quoted as the line splits it back. */
  accept(candidate: string): void {
    const split = ConsoleViewModel.split(this.input)
    if (!split) return
    const kept = [...this.input].slice(0, split.partialStart).join('')
    this.update(() => {
      this.input = `${kept}${ConsoleViewModel.quoted(candidate)} `
      this.suggestions = []
    })
  }

  dismissSuggestions(): void {
    this.update(() => (this.suggestions = []))
  }

  // MARK: - Running

  /**
   * Runs the line as typed, split as typed; resolves once the run has ended
   * and said how. A line that does not split says why in the CLI's words; a
   * refused command is refused with its reason, read past any root option; a
   * bare `apogee` asks for a command. Nothing runs while a run is live:
   * `undefined` then.
   */
  run(): Promise<void> | undefined {
    const line = this.input.trim()
    if (line === '' || this.job !== undefined) return undefined
    const tokens = ConsoleViewModel.tokens(this.input)
    if ('unsplittable' in tokens) {
      this.append('problem', `${tokens.unsplittable.charAt(0).toUpperCase()}${tokens.unsplittable.slice(1)}.`)
      return Promise.resolve()
    }
    const args = ConsoleViewModel.argumentsOf(this.input) ?? []
    if (args.length === 0) {
      this.append('note', ConsoleViewModel.nothingToRun)
      return Promise.resolve()
    }
    this.update(() => {
      this.input = ''
      this.suggestions = []
      this.history = [...this.history, line]
    })
    this.append('command', `apogee ${line.replace(/^apogee[ \t\n\r]+/, '')}`)
    const refusal = ConsoleViewModel.refusal(ConsoleViewModel.commandPath(args))
    if (refusal !== undefined) {
      this.append('problem', `Not run here: ${refusal}.`)
      return Promise.resolve()
    }
    const binary = this.binary
    if (binary === undefined) {
      this.append('problem', 'Apogee isn’t installed, so there is nothing to run.')
      return Promise.resolve()
    }
    const job = this.starter.startJob(binary, args, false)
    this.update(() => {
      this.job = job
      this.running = line
      this.busy = undefined
    })
    return this.follow(job)
  }

  private async follow(job: JobRunning): Promise<void> {
    let end: JobEnd | undefined
    for await (const update of job.updates) {
      if (update.case === 'said') this.append('out', update.line)
      else if (update.case === 'busy') this.update(() => (this.busy = update.line))
      else if (update.case === 'ended') end = update.end
    }
    this.update(() => {
      this.job = undefined
      this.running = undefined
      this.busy = undefined
    })
    if (end) this.finish(end)
  }

  /** Stops the run as Ctrl-C does. */
  async stop(): Promise<void> {
    await this.job?.stop()
  }

  clear(): void {
    this.update(() => (this.lines = []))
  }

  /** The line run `back` runs ago, for the arrow keys. */
  recall(back: number): string | undefined {
    if (back <= 0 || back > this.history.length) return undefined
    return this.history[this.history.length - back]
  }

  private finish(end: JobEnd): void {
    for (const line of end.stderrTail.text.split(/\r?\n/)) if (line !== '') this.append('err', line)
    if (end.detail !== undefined) this.append('problem', `Didn’t start: ${end.detail}`)
    else if (end.interrupted) this.append('note', 'Stopped.')
    else if (end.exit) this.append(ChildExit.succeeded(end.exit) ? 'note' : 'problem', ConsoleViewModel.ending(end.exit))
  }

  /** How a run ended, said plainly. */
  static ending(exit: ChildExit): string {
    return exit.reason === 'exited' ? (exit.status === 0 ? 'Done.' : `Exited with status ${exit.status}.`) : `Ended by signal ${exit.status}.`
  }

  // MARK: - What the view reads

  face(): ConsoleFace {
    return {
      lines: this.lines,
      empty: this.lines.length === 0,
      running: { shown: this.isRunning, busy: this.busy ?? (this.running === undefined ? '' : `Running ${this.running}`) },
      input: this.input,
      suggestions: this.suggestions,
      canRun: !this.isRunning && this.input.trim() !== '',
    }
  }

  private append(kind: ConsoleLineKind, text: string): void {
    this.update(() => {
      this.lines = [...this.lines, { id: this.next, kind, text }]
      this.next += 1
    })
  }

  private update(change: () => void): void {
    change()
    this.changed()
  }
}
