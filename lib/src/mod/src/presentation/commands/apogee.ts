// The `/apogee` slash command: registered at the session's start, answered
// over the binary discovery found. Presentation registers and words it;
// Business plans and runs (ADR 0001).
import type { EngineInterface } from 'claude-code'

import { runAction } from '../../business/commands/actions'
import type { ActionOutcome } from '../../business/commands/actions'
import { sessionBinary } from '../../business/client/client'
import type { FoundBinary } from '../../business/client/client'
import { planApogee, shownCommand } from '../../business/commands/plan'
import type { ActionName } from '../../business/commands/plan'
import { runRead } from '../../business/commands/reads'
import { offeredTools } from '../../business/surfaces/surfaces'
import type { DiscoveryOptions } from '../../infrastructure/discovery/discovery'
import { absenceText } from '../notices/absence'
import { refreshStatusLineLater } from '../status/line'
import { binaryText, readText, toolsText, usageText } from './render'

/** The label that runs a confirmed command; anything else runs nothing. */
const RUN_IT = 'Run it'

/** How long a job's end stays up. */
const JOB_TOAST_MS = 10_000

/** Declares `/apogee` for the session. */
export async function registerApogee($: EngineInterface): Promise<void> {
  await $.command.register({
    name: 'apogee',
    description: "Apogee on this machine: the install check, the models, training's status; promote and rollback ask first",
    argumentHint: 'check | models | train status | promote <run> --as <backend> | rollback <backend>',
  })
}

/** Answers `/apogee <args>`: the plan's read run and formatted, or its refusal said. */
export async function answerApogee($: EngineInterface, args: string, discovery: DiscoveryOptions): Promise<{ text: string }> {
  const binary = await sessionBinary($)
  if (binary === undefined) {
    return { text: 'Apogee has not been looked for yet: the session has not started.' }
  }
  if (binary.kind === 'not-found') {
    return { text: absenceText(binary.skipped, discovery) }
  }
  const plan = planApogee(args)
  if (plan.kind === 'overview') {
    return { text: `${binaryText(binary)}\n${toolsText(await offeredTools($))}\n\n${usageText()}` }
  }
  if (plan.kind === 'refused') {
    return { text: `${plan.reason}.\n\n${usageText()}` }
  }
  if (plan.kind === 'read') {
    return { text: readText(await runRead($, binary, plan.read), shownCommand(binary.path, plan.args)) }
  }

  return { text: await confirmAndRun($, binary, plan.action, plan.args) }
}

/**
 * Shows the exact command and runs it only on the yes: the confirmation is the
 * one gate, since the CLI's promote and rollback take no --yes.
 */
async function confirmAndRun($: EngineInterface, binary: FoundBinary, action: ActionName, args: readonly string[]): Promise<string> {
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
  // Answered at once: the command's dispatch never waits on a run that writes the store.
  const job = await runAction($, binary, action, args, shown, (ended, outcome) => {
    $.ui.toast(`Apogee job ${ended.id}: ${outcomeText(shown, outcome)}`, { timeoutMs: JOB_TOAST_MS })
    refreshStatusLineLater($, binary)
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
