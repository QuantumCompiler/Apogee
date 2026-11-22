// The `/apogee` slash command: registered at the session's start, answered
// over the binary discovery found. Presentation registers and words it;
// Business plans and runs (ADR 0001).
import type { EngineInterface } from 'claude-code'

import { sessionBinary } from '../../business/client/client'
import { planApogee, shownCommand } from '../../business/commands/plan'
import { runRead } from '../../business/commands/reads'
import { offeredTools } from '../../business/surfaces/surfaces'
import type { DiscoveryOptions } from '../../infrastructure/discovery/discovery'
import { absenceText } from '../notices/absence'
import { openPane } from '../pane/open'
import { confirmAndRun } from './confirm'
import { binaryText, readText, toolsText, usageText } from './render'

/** Declares `/apogee` for the session. */
export async function registerApogee($: EngineInterface): Promise<void> {
  await $.command.register({
    name: 'apogee',
    description: "Apogee on this machine: the install check, the models, training's status, the pane; promote and rollback ask first",
    argumentHint: 'check | models | train status | promote <run> --as <backend> | rollback <backend> | pane',
  })
}

/** Answers `/apogee <args>`: the plan's read run and formatted, or its refusal said. */
export async function answerApogee($: EngineInterface, args: string, discovery: DiscoveryOptions): Promise<{ text: string }> {
  const plan = planApogee(args)
  if (plan.kind === 'pane') {
    // Opened whatever discovery found: the pane says an absence itself.
    return { text: await openPane($) }
  }
  const binary = await sessionBinary($)
  if (binary === undefined) {
    return { text: 'Apogee has not been looked for yet: the session has not started.' }
  }
  if (binary.kind === 'not-found') {
    return { text: absenceText(binary.skipped, discovery) }
  }
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
