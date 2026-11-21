import type { TaskUse } from '../../../business/reads/Tasks'
import type { TaskTone } from './TasksFaces'

/**
 * Where a halt asked of a run in flight lands (design 1p): as the turn
 * `after` names ends -- the plan (0) or a round -- or, sent while a resume
 * started up, `before` the turn it would run next, nothing run.
 */
export type TaskHalt = { readonly after: number } | { readonly before: number }

/** The plan (0), or round N. */
const turn = (round: number): string => (round === 0 ? 'the plan' : `round ${round}`)

/**
 * The Tasks place's words (design 1p): the old prototype's where it has
 * them (`taskView()`, `main.html` at `b375fac9`), and `task status`'s own
 * for a gated call (`task_cmd.cpp`'s `on_target`, `authority_words` and
 * `refusal_words`, the Mac's `TaskUseWords`) and a round's report
 * (`task.cpp`'s `report_words`). Plain TypeScript.
 */
export const TaskWords = {
  /**
   * A task's status in the old words: a run in flight by the round it is in
   * -- planning too, as the old one counts the plan in round 1 -- or by the
   * turn it halts after once asked; a stopped one by how it stopped and
   * whether it resumes. A status the old one never drew (stalled, failed) is
   * the CLI's word with the halted one's tail, since it resumes the same way;
   * so is a halt kept from a resume's start-up, which the old one ends at
   * once ("halts before round 3", the CLI's "halted ... before round 3").
   */
  status(status: string, roundsUsed: number, budget: number, halting?: TaskHalt): string {
    switch (status) {
      case 'planning':
      case 'running':
        if (halting !== undefined) return 'after' in halting ? `running · halts when ${turn(halting.after)} ends` : `running · halts before ${turn(halting.before)}`
        return `running · round ${roundsUsed + 1} of ${budget}`
      case 'done':
        return 'done · every check passed'
      case 'exhausted':
        return `exhausted · ${roundsUsed} of ${budget} rounds used`
      case 'halted':
        return 'halted · resumes from its ledger'
      case 'cancelled':
        return 'cancelled by you'
      case 'stalled':
      case 'failed':
        return `${status} · resumes from its ledger`
      default:
        return status
    }
  },

  /** The status's color: a run in flight the accent, a stop that left work undone a warning, an end the secondary text. */
  tone(status: string): TaskTone {
    if (status === 'planning' || status === 'running') return 'accent'
    if (status === 'done' || status === 'cancelled') return 'plain'
    return 'warning'
  },

  /** A round's report in `task status`'s words. */
  report(selfReport: string): string {
    return selfReport === 'done' ? 'reported done' : selfReport === 'not_done' ? 'reported not done' : 'no status reported'
  },

  /** The tool, and what it was called on when it names something. */
  onTarget(use: TaskUse): string {
    return use.target === '' ? use.tool : `${use.tool} on ${use.target}`
  },

  /** On whose authority a call ran, in `task status`'s words; an authority the CLI has no words for, as written. */
  authority(by: string): string {
    return by === 'grant' ? "this task's grant" : by === 'person' ? 'the person at the terminal' : by === 'config' ? 'the config' : by
  },

  /** Why a call was refused, in `task status`'s words; a reason it has no words for, left out as it leaves it out. */
  refusal(by: string): string {
    return by === 'config' ? 'the config denies it' : by === 'nobody' ? 'nobody present to allow it' : by === 'person' ? 'refused at the prompt' : ''
  },

  /** `run_command on ls -- this task's grant`. */
  allowed(use: TaskUse): string {
    const authority = TaskWords.authority(use.by)
    return TaskWords.onTarget(use) + (authority === '' ? '' : ` -- ${authority}`)
  },

  /** `write_file on report.txt -- nobody present to allow it`. */
  denied(use: TaskUse): string {
    const why = TaskWords.refusal(use.by)
    return TaskWords.onTarget(use) + (why === '' ? '' : ` -- ${why}`)
  },

  /** The plan's steps: each line of the plan the CLI recorded, its own numbering taken off, numbered again as the old list does. */
  steps(plan: string): Array<{ readonly n: string; readonly text: string }> {
    return plan
      .split(/\r?\n/)
      .map((line) => line.trim().replace(/^\d+[.)]\s+/, ''))
      .filter((line) => line !== '')
      .map((text, index) => ({ n: String(index + 1), text }))
  },

  /** A count of tokens as the old prototype writes it: `2.1K`. */
  kilo(tokens: number): string {
    return `${Math.round(tokens / 100) / 10}K`
  },

  /** `m:ss`, the old Time so far. */
  minutes(seconds: number): string {
    const whole = Math.max(0, Math.floor(seconds))
    return `${Math.floor(whole / 60)}:${String(whole % 60).padStart(2, '0')}`
  },

  /** When a task was made, as the old meta line says it: `today 14:10`, or `Nov 9 02:12`, in the clock's zone, UTC. */
  started(at: string, now: Date): string {
    const when = new Date(at)
    if (Number.isNaN(when.getTime())) return at
    const time = `${String(when.getUTCHours()).padStart(2, '0')}:${String(when.getUTCMinutes()).padStart(2, '0')}`
    if (when.toISOString().slice(0, 10) === now.toISOString().slice(0, 10)) return `today ${time}`
    const months = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec']
    return `${months[when.getUTCMonth()] ?? ''} ${when.getUTCDate()} ${time}`
  },

  /** The seconds between two of the ledger's stamps; 0 when either can't be read. */
  between(from: string, to: string): number {
    const start = Date.parse(from)
    const end = Date.parse(to)
    return Number.isNaN(start) || Number.isNaN(end) ? 0 : Math.max(0, (end - start) / 1000)
  },
}
