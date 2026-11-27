// The two commands of the set that change the store, run after the person's
// yes (Presentation asks): supervised, with no time of their own, and a job
// from their start, begun outside the command's dispatch, so neither a time
// nor an interrupt of the dispatch cuts a promotion off.
import type { EngineInterface } from 'claude-code'

import type { ApogeeJob } from '../../../types'
import { lastLine, superviseBinary } from '../../infrastructure/runner/runner'
import type { Ran } from '../../infrastructure/runner/runner'
import type { FoundBinary } from '../client/client'
import { detachJob } from '../jobs/jobs'
import type { ActionName } from './plan'

/** How a confirmed command ended: what it printed, or its status and last words, or why it never ran. */
export type ActionOutcome =
  | { kind: 'done'; printed: string }
  | { kind: 'failed'; exitCode: number; said: string }
  | { kind: 'refused'; reason: string }

/** Runs a confirmed command, `args` the plan's, as a job answered at once; `onLater` hears its end. */
export async function runAction(
  $: EngineInterface,
  binary: FoundBinary,
  action: ActionName,
  args: readonly string[],
  title: string,
  onLater: (job: ApogeeJob, outcome: ActionOutcome) => void,
): Promise<ApogeeJob> {
  return detachJob($, { kind: action, title }, () => superviseBinary($, binary.path, args).then(outcomeOf), endOf, onLater)
}

function outcomeOf(ran: Ran): ActionOutcome {
  if (ran.kind === 'refused') {
    return { kind: 'refused', reason: ran.reason }
  }
  if (ran.exitCode === 0) {
    return { kind: 'done', printed: ran.stdout.trimEnd() }
  }

  return { kind: 'failed', exitCode: ran.exitCode, said: lastLine(ran.stderr) ?? lastLine(ran.stdout) ?? 'it said nothing' }
}

function endOf(outcome: ActionOutcome): { state: 'done' | 'failed'; text?: string; reason?: string } {
  if (outcome.kind === 'done') return { state: 'done', text: outcome.printed }
  if (outcome.kind === 'failed') return { state: 'failed', reason: `exit status ${outcome.exitCode}: ${outcome.said}` }

  return { state: 'failed', reason: `could not run: ${outcome.reason}` }
}
