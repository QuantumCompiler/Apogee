import { BoundedTail } from './BoundedTail'
import { ChildExit } from './ChildExit'
import { StandInFolders } from './StandInFolders'
import type { FolderWrite, Folders } from './StandInFolders'

/** What one recorded call printed. Each part is the binary's own: a recording, or a slice of one. */
export interface Answer {
  /** What it wrote on stdout: a read's document, a job's stream, a chat's lines. */
  readonly stdout?: string
  /** What it wrote on stderr: a recorded `.err`. */
  readonly stderr?: string
  /** How it exited: 0 unless the rule names another (a recorded `.exit`, read by `recordedExit`). */
  readonly exit?: number
  /** It ran past its time and was stopped: no exit of its own, terminated. */
  readonly timedOut?: boolean
  /**
   * Ticks the child takes to exit (design 1v): a chat's once its input
   * closes, so a still frame can stand between a stop and the next child; a
   * job's after its last line's next tick, so one can stand mid-job. 0, at
   * once (1d's), unless named.
   */
  readonly exitAfter?: number
  /**
   * Where a job's `exitAfter` ticks stand (design 1k): after this many of its
   * stdout lines, the rest played after them, so a run can rest on a line
   * mid-way -- an install on its `installing …` line while pip runs, its
   * `installed` and where it went said only at its end. All of them unless
   * named.
   */
  readonly holdAt?: number
  /** Where a chat's still frame stands, and where its live replay starts (design 1v). */
  readonly freeze?: Freeze
  /** A chat's choice point: the recording each line the person can write there plays from then on (design 1v). */
  readonly continuations?: Continuations
  /**
   * A chat's Stop (design 1v): a recording of the same session whose turn a
   * `cancel` ended. A cancel written while a turn is open drops that turn's
   * unplayed lines and plays this recording's ending of the same turn in
   * their place: its `result` with `finish_reason: "cancelled"`, and the
   * calls reported `failed` just before it.
   */
  readonly cancelled?: string
  /**
   * Its stderr plays as it runs (design 1j): one line a tick as the busy
   * line, before its stdout's lines, as the Mac samples stderr's last line
   * while a run goes -- `train run`, which takes no machine stream, says its
   * progress there, and a promote narrates there before its result. Unless
   * named, stderr is only the tail its end leaves (1d's).
   */
  readonly busy?: boolean
  /**
   * The files a job writes into the stand-in's folders as it plays (design
   * 1l): the composer's project, saved after every step, so a screen reads
   * its progress where the Mac reads it, beside the video. A job its Stop
   * ends leaves its last write's `stopped` file, and the same command again
   * resumes from that write, as the CLI resumes a stopped composition.
   */
  readonly writes?: readonly FolderWrite[]
  /**
   * The stops another call asks of this job while it runs (design 1p): `task
   * halt <id>` and `task cancel <id>`, each a command the CLI answers at
   * once while the running process stops on its own -- a halt when the round
   * in flight ends, a cancel now -- so the job plays the ending of its own
   * run stopped there.
   */
  readonly asked?: readonly AskedStop[]
}

/**
 * A stop another call asks of a running job (design 1p): once a call of `by`
 * has been answered with a success while the job runs, the job ends as
 * `endings` says from the line it has reached -- the first ending whose
 * `before` is past the stdout lines played. Past them all, it plays on as
 * recorded: the run's own end comes first, as a task's last round decides
 * done or exhausted before a halt is read.
 */
export interface AskedStop {
  /** The call that asks it, whole: `['task', 'halt', '<id>']`. */
  readonly by: readonly string[]
  readonly endings: readonly AskedEnding[]
}

/** How a job asked to stop ends, by where its replay stands (design 1p). */
export interface AskedEnding {
  /** It applies while fewer than this many stdout lines have played. */
  readonly before: number
  /** The job plays its own lines through this many first -- the end of the turn it lands at, for a halt; none, for a cancel, which ends it now. */
  readonly through?: number
  /** Then this in place of the rest: the closing lines the CLI writes for the run stopped there. */
  readonly stdout: string
  /** How it then exits: 0 unless named. */
  readonly exit?: number
}

/**
 * A named event of a chat's recording (design 1v): the `nth` line of type
 * `event`, counted from the recording's start. A still frame stands right
 * after it, however many ticks it has; a live clock plays straight to it,
 * then on at its pace.
 */
export interface Freeze {
  /** What the preset calls the frame: `mid-reasoning`. */
  readonly name: string
  readonly event: string
  readonly nth: number
}

/**
 * A chat's choice point (design 1v): once the `nth` line of type `after` has
 * played (the first unless named), the first answer, turn or setting written
 * picks its branch -- `by` its key: an `answer` line's text, `user` for a
 * turn sent again, and a setting by what it sets (`set model=claude`, each
 * field `key=value` in the line's order). Each branch is a whole recording
 * that shares the main one's lines up to there (its own capture from the
 * same opening turns, or hand-made); the replay plays it from the line it
 * has reached. A line no branch names is noted in `unanswered`.
 */
export interface Continuations {
  readonly after: string
  readonly nth?: number
  readonly by: Readonly<Record<string, string>>
}

/** How a recorded child's replay moves, past what it printed: its rule's (design 1v). */
export interface ReplayRules {
  readonly exitAfter: number
  /** The stdout line count the `exitAfter` ticks stand after (design 1k); `undefined`, after the last. */
  readonly holdAt: number | undefined
  readonly freeze: Freeze | undefined
  readonly continuations: Continuations | undefined
  readonly cancelled: string | undefined
  readonly busy: boolean
  /** The files the job writes as it plays (design 1l); none unless named. */
  readonly writes: readonly FolderWrite[]
  /** Resumed (design 1l): its first line, then on from the write its last Stop left, that write's file written again first. */
  readonly resumeFrom: FolderWrite | undefined
  /** The stops another call may ask of it (design 1p); none unless named. */
  readonly asked: readonly AskedStop[]
}

/**
 * One rule of the table: an argument prefix, and what the binary printed for
 * it. Several answers come in order, one a call, the last repeating: an
 * install that fails, then succeeds. A rule may wait on another call's
 * success first (`until`, design 1k).
 */
export interface Rule {
  /** The arguments after `apogee`, as the caller gives them (a read without its `--output-format`). */
  readonly arguments: readonly string[]
  /**
   * Its arguments answer a call only whole (design 1h): no longer line is
   * answered by it, so a recording of `models quantize <h>` never answers
   * `models quantize <h> -t Q8_0`, which printed something else.
   */
  readonly exact?: boolean
  /**
   * It waits on another call (design 1k): until a call of `ran`, of the same
   * kind, has been answered with a success (exit 0, in time) and not stopped
   * before its end, every call is answered by `answer` -- the CLI's refusal,
   * uncounted -- and the rule's own answers play only after it, from its
   * first. A generate the CLI refuses for peft until `train setup --with
   * video` has gone in, however often it is pressed.
   */
  readonly until?: { readonly ran: readonly string[]; readonly answer: Answer }
  /**
   * It judges a file by what it holds (design 1n): while the stand-in's
   * folder file `name` in `folder` holds the text `holds`, every call is
   * answered by `answer` -- the CLI's refusal, uncounted -- and the rule's
   * own answers play once it does not. `symphonies show` refuses a spec with
   * an unknown key however often it is saved, as the old prototype's save
   * judges the text it saved (`main.html:3011-3019` at `b375fac9`).
   */
  readonly refusedWhile?: { readonly folder: string; readonly name: string; readonly holds: string; readonly answer: Answer }
  readonly answer: Answer | readonly [Answer, ...Answer[]]
}

/** A preset's rule tables, one a kind, as `PreviewRunner` keeps reads and commands apart. */
export interface Rules {
  readonly reads?: readonly Rule[]
  readonly commands?: readonly Rule[]
  readonly jobs?: readonly Rule[]
  readonly chats?: readonly Rule[]
}

export type RuleKind = keyof Rules

/** The `apogee` prop of the old prototype (`main.html:2512`, read at `:4817-4819`, at `b375fac9`). */
export type ApogeeMode = 'installed' | 'missing' | 'config-typo'

/**
 * What the modes answer with, shared by every preset: handed in, since a
 * recording is imported only by a `Previews/` file or a test
 * (`presentation/app/Previews/ModeRules.ts`).
 */
export interface ModeRules {
  /** The CLI's refusal of a config it cannot read: `agents-list-bad-config.err`, word for word. */
  readonly configTypoRefusal: string
}

/** A child run to its end, with everything it said kept: the Mac app's `CapturedRun`. */
export interface CapturedRun {
  readonly exit: ChildExit
  readonly stdout: string
  /** Whether stdout ran past the limit. A recording never does. */
  readonly stdoutTruncated: boolean
  readonly stderrTail: BoundedTail
  readonly timedOut: boolean
}

/** A call's end: it ran and printed `run`, replayed as `replay` says, or it never started, and `reason` says why. */
export type ChildAnswer = { readonly started: true; readonly run: CapturedRun; readonly replay: ReplayRules } | { readonly started: false; readonly reason: string }

/** Where the binary is, as the mode says: the Mac app's `DiscoveryOutcome`, whose place the three modes take. */
export type DiscoveryOutcome = { readonly case: 'found'; readonly binary: string } | { readonly case: 'notFound' }

/** One call the child ran, kept for the tests as the Mac app's scripted runners keep theirs. */
export interface Call {
  readonly kind: RuleKind
  readonly arguments: readonly string[]
  readonly input?: string
}

/** The status a recorded `.exit` file holds. */
export function recordedExit(text: string): number {
  const trimmed = text.trim()
  if (!/^-?\d+$/.test(trimmed)) throw new Error(`a recorded .exit holds one status, not ${JSON.stringify(text)}`)
  return Number(trimmed)
}

/**
 * The stand-in for running `apogee` (design 1d): it takes an argument list and
 * answers from its rule table with what the CLI printed, in one of the three
 * modes. It never starts a process, reads a file or reaches the network: its
 * rules are handed to it, each answer a bundled recording.
 *
 * - The longest prefix wins; of two as long, the later rule (`PreviewStore`'s rule).
 * - A rule with `until` answers its refusal until the call it names has
 *   succeeded (design 1k); a job stopped after it wrote a file is resumed
 *   from that write by the same command (design 1l): what the child
 *   remembers between calls, beside the folders its jobs write.
 * - A rule with `refusedWhile` answers its refusal while a folder file holds
 *   its text (design 1n).
 * - `missing`: no binary is found, and nothing runs.
 * - `config-typo`: every call exits 1 with the recorded refusal, its verb
 *   swapped, as the old prototype's `cfgRefusal` does (`main.html:4820` at
 *   `b375fac9`) -- but a call `configFree` names, which the rules answer.
 */
export class StandInChild {
  /** Where the binary is said to be, as the Mac app's previews say it (`previewBinary`); never run. */
  static readonly binary = '/Users/someone/.local/bin/apogee'

  /**
   * The argument prefixes the CLI answers without reading its config, so
   * `config-typo` passes them to the rules (design 1n): `review skills`,
   * whose `list` and `show` read only the skills folder
   * (`review_cmd.cpp:74-75,106-107`), as the Mac's `ReviewPreview.badConfig`
   * lists them over a bad config (`ReviewPreviews.swift:22-28`) and the old
   * prototype draws them. `mcp presets` and `config path` read none either
   * and are still refused: a stand-in limit, listed for 1s.
   */
  static readonly configFree: readonly (readonly string[])[] = [['review', 'skills']]

  readonly rules: Rules
  readonly apogee: ApogeeMode
  /** Every call that ran -- answered by a rule, or refused by the config -- in order. */
  readonly calls: Call[] = []
  /** Each call no rule answered, as `<kind>: apogee <arguments>`: what a per-preset test holds empty. */
  readonly unanswered: string[] = []
  private readonly answered = new Map<Rule, number>()
  /** The calls answered with a success, as `<kind> <arguments>`, and not stopped since: what a rule's `until` waits on (design 1k). */
  private readonly succeeded = new Set<string>()
  /** Each stopped job's last write, by `<kind> <arguments>`: where the same command resumes it (design 1l). */
  private readonly resumes = new Map<string, FolderWrite>()
  /** The folders the CLI writes into (design 1k): the preset's files, and what the jobs it replays write as they run (1l). */
  readonly folders: StandInFolders
  /** Who hears a call answered with a success (design 1p): a running job a stop is asked of. */
  private readonly hearers = new Set<{ readonly by: readonly string[]; readonly hear: () => void }>()

  constructor(
    settings: { readonly rules: Rules; readonly apogee: ApogeeMode; readonly folders?: Folders },
    readonly modes: ModeRules,
  ) {
    this.rules = settings.rules
    this.apogee = settings.apogee
    this.folders = new StandInFolders(settings.folders === undefined ? { apogee: settings.apogee } : { folders: settings.folders, apogee: settings.apogee })
  }

  /** Where the binary is: found, unless the mode is `missing`. */
  locateBinary(): DiscoveryOutcome {
    return this.apogee === 'missing' ? { case: 'notFound' } : { case: 'found', binary: StandInChild.binary }
  }

  /** Runs `apogee <args>` as `kind`, `input` on its stdin, and answers what it printed. */
  run(kind: RuleKind, args: readonly string[], input?: string): ChildAnswer {
    if (this.apogee === 'missing') return { started: false, reason: 'apogee is not installed' }
    const said = `apogee ${args.join(' ')}`
    this.calls.push(StandInChild.call(kind, args, input))
    if (this.apogee === 'config-typo' && !StandInChild.configFree.some((prefix) => prefix.every((word, index) => args[index] === word))) {
      const verb = args[0]
      const refusal = verb === undefined ? this.modes.configTypoRefusal : this.modes.configTypoRefusal.replace(/^apogee [^:\s]+:/, `apogee ${verb}:`)
      return { started: true, run: StandInChild.captured({ stderr: refusal, exit: 1 }), replay: StandInChild.replay({}) }
    }
    const rule = StandInChild.longestPrefix(this.rules[kind] ?? [], args)
    if (!rule) {
      this.note(kind, said)
      return { started: false, reason: `no recorded answer for ${said}` }
    }
    // Still waiting on the call it names: the refusal, and nothing counted.
    if (rule.until && !this.succeeded.has(StandInChild.key(kind, rule.until.ran))) return { started: true, run: StandInChild.captured(rule.until.answer), replay: StandInChild.replay(rule.until.answer) }
    // The file it judges holds the refused text: the refusal, and nothing counted (design 1n).
    const judged = rule.refusedWhile
    if (judged && this.folders.files(judged.folder).find((file) => file.name === judged.name)?.text?.includes(judged.holds) === true) return { started: true, run: StandInChild.captured(judged.answer), replay: StandInChild.replay(judged.answer) }
    const answers: readonly Answer[] = Array.isArray(rule.answer) ? rule.answer : [rule.answer as Answer]
    const count = this.answered.get(rule) ?? 0
    this.answered.set(rule, count + 1)
    const answer = answers[Math.min(count, answers.length - 1)] ?? {}
    if ((answer.exit ?? 0) === 0 && answer.timedOut !== true) {
      this.succeeded.add(StandInChild.key(kind, args))
      // A stop it asks of a running job reaches it once the CLI has answered (design 1p).
      for (const hearer of [...this.hearers]) if (hearer.by.length === args.length && hearer.by.every((word, index) => word === args[index])) hearer.hear()
    }
    // Stopped before: the same command resumes from the write its Stop left (design 1l).
    const resumeFrom = this.resumes.get(StandInChild.key(kind, args))
    this.resumes.delete(StandInChild.key(kind, args))
    const replay = StandInChild.replay(answer)
    return { started: true, run: StandInChild.captured(answer), replay: resumeFrom && replay.writes.includes(resumeFrom) ? { ...replay, resumeFrom } : replay }
  }

  /**
   * Hears every call of exactly `by` answered with a success from now on
   * (design 1p): how a running job hears a stop asked of it. Returns its
   * leave.
   */
  hearing(by: readonly string[], hear: () => void): () => void {
    const hearer = { by, hear }
    this.hearers.add(hearer)
    return () => this.hearers.delete(hearer)
  }

  /**
   * A call stopped before its end (its Stop, design 1k): what it was answered
   * never finished, so it is no success, and a rule waiting on it waits again.
   * A job that had written a file (design 1l) is resumed from that write by
   * the same command.
   */
  stopped(kind: RuleKind, args: readonly string[], lastWrite?: FolderWrite): void {
    this.succeeded.delete(StandInChild.key(kind, args))
    if (lastWrite !== undefined) this.resumes.set(StandInChild.key(kind, args), lastWrite)
  }

  private static key(kind: RuleKind, args: readonly string[]): string {
    return `${kind} ${args.join(' ')}`
  }

  /** An answer's replay rules, each as 1d replayed it unless the answer names it. */
  static replay(answer: Answer): ReplayRules {
    return { exitAfter: answer.exitAfter ?? 0, holdAt: answer.holdAt, freeze: answer.freeze, continuations: answer.continuations, cancelled: answer.cancelled, busy: answer.busy ?? false, writes: answer.writes ?? [], resumeFrom: undefined, asked: answer.asked ?? [] }
  }

  /** Logs what nobody recorded, as `<kind>: <what>`: a call, or a chat's turn past its recording. */
  note(kind: RuleKind, what: string): void {
    this.unanswered.push(`${kind}: ${what}`)
  }

  /** The rule whose arguments are the longest prefix of `args`; of two as long, the later. */
  static longestPrefix(rules: readonly Rule[], args: readonly string[]): Rule | undefined {
    let best: Rule | undefined
    for (const rule of rules) {
      const prefix = rule.arguments
      if (prefix.length > args.length || prefix.some((word, index) => word !== args[index])) continue
      if (rule.exact === true && prefix.length !== args.length) continue
      if (!best || prefix.length >= best.arguments.length) best = rule
    }
    return best
  }

  private static call(kind: RuleKind, args: readonly string[], input: string | undefined): Call {
    return input === undefined ? { kind, arguments: [...args] } : { kind, arguments: [...args], input }
  }

  /** What an answer leaves: a timed-out child was terminated, so its end is signal 15. */
  private static captured(answer: Answer): CapturedRun {
    const timedOut = answer.timedOut ?? false
    return {
      exit: answer.exit !== undefined ? ChildExit.exited(answer.exit) : timedOut ? ChildExit.signalled(15) : ChildExit.exited(0),
      stdout: answer.stdout ?? '',
      stdoutTruncated: false,
      stderrTail: BoundedTail.of(answer.stderr ?? ''),
      timedOut,
    }
  }
}
