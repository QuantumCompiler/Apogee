// The one confirmation of a command that changes the store, shared by
// `/apogee promote|rollback` and the pane's buttons: the exact command shown,
// run only on the yes (the CLI's promote and rollback take no --yes, so this
// is the one gate), then a job from its start, its end toasted, and the status
// line and the pane (while it is open) read back.
import type { EngineInterface } from 'claude-code'

import { runAction } from '../../business/commands/actions'
import type { ActionOutcome } from '../../business/commands/actions'
import type { FoundBinary } from '../../business/client/client'
import { shownCommand } from '../../business/commands/plan'
import type { ActionName } from '../../business/commands/plan'
import { readPaneBackLater } from '../pane/open'
import { refreshStatusLineLater } from '../status/line'

/** The label that runs a confirmed command; anything else runs nothing. */
export const RUN_IT = 'Run it'

/** How long a job's end stays up. */
const JOB_TOAST_MS = 10_000

/**
 * Shows the exact command and runs it only on the yes, as a job answered at
 * once: the words say what became of it. Its end is toasted, and the status
 * line and the pane are read back (read back, never mirrored).
 */
export async function confirmAndRun($: EngineInterface, binary: FoundBinary, action: ActionName, args: readonly string[]): Promise<string> {
  const shown = shownCommand(binary.path, args)
  let answer: string | undefined
  try {
    answer = await $.ui.ask(questionOf(action, args, shown), { options: [RUN_IT, 'Cancel'], header: 'Apogee' })
  } catch {
    answer = undefined
  }
  if (answer !== RUN_IT) {
    const why = answer === undefined ? 'no answer' : answer === 'Cancel' ? 'you chose Cancel' : `you answered "${answer}"`

    return `Not run: ${shown} (${why})`
  }
  // Answered at once: the dispatch never waits on a run that writes the store.
  const job = await runAction($, binary, action, args, shown, (ended, outcome) => {
    $.ui.toast(`Apogee job ${ended.id}: ${outcomeText(shown, outcome)}`, { timeoutMs: JOB_TOAST_MS })
    refreshStatusLineLater($, binary)
    readPaneBackLater($, binary)
  })

  return `${shown} is running as job ${job.id}; a toast says when it ends.`
}

/** The question: what the command does, then the command itself. */
function questionOf(action: ActionName, args: readonly string[], shown: string): string {
  return action === 'promote'
    ? `Promote run ${args[2] ?? ''} as ${args[4] ?? ''}'s next version by running ${shown}?`
    : `Roll back ${args[2] ?? ''} to its previous promoted version by running ${shown}?`
}

/** How a confirmed command ended, as the person reads it. */
function outcomeText(shown: string, outcome: ActionOutcome): string {
  if (outcome.kind === 'refused') return `${shown} could not run: ${outcome.reason}`
  if (outcome.kind === 'failed') return `${shown} failed (exit status ${outcome.exitCode}): ${outcome.said}`

  return outcome.printed === '' ? `Ran ${shown}.` : `Ran ${shown}:\n${outcome.printed}`
}
