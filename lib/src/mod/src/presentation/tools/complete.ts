// apogee_complete as Claude reads it: what one call is, the backends it may
// name with their bound, that a call spends this machine's time and never
// money, why it is offered, and what a slow answer does (the async law).
import type { EngineInterface, ToolCallResult } from 'claude-code'

import type { ApogeeJob } from '../../../types'
import type { FoundBinary } from '../../business/client/client'
import { checkRequest, runComplete } from '../../business/complete/complete'
import type { CompleteOffer, CompleteOutcome } from '../../business/complete/complete'
import { ASYNC_WINDOW_MS, launchJob } from '../../business/jobs/jobs'

/** How long a job's end stays up. */
const JOB_TOAST_MS = 10_000

/** Registers apogee_complete over the offer. */
export async function registerComplete($: EngineInterface, offer: CompleteOffer): Promise<void> {
  const widest = Math.max(...offer.backends.map(one => one.maxTokens))
  const isOneBound = offer.backends.every(one => one.maxTokens === widest && one.isBoundUnsaid !== true)
  await $.tool.register({
    name: 'apogee_complete',
    description: completeDescription(offer, widest, isOneBound),
    inputSchema: {
      type: 'object',
      properties: {
        prompt: { type: 'string', description: 'What the model is asked' },
        backend: { type: 'string', enum: offer.backends.map(one => one.name), description: 'The backend that answers: one of those this tool names' },
        max_tokens: {
          type: 'integer',
          minimum: 1,
          maximum: widest,
          description: `The most tokens the reply may run to; ${isOneBound ? `the bound, ${widest},` : "the backend's bound"} when left out`,
        },
      },
      required: ['prompt', 'backend'],
    },
  })
}

/** Answers one call: checked, run, and past the window, a job. */
export async function answerComplete(
  $: EngineInterface,
  binary: FoundBinary,
  offer: CompleteOffer,
  args: Readonly<Record<string, unknown>>,
): Promise<ToolCallResult> {
  const request = checkRequest(offer, args)
  if (request.kind === 'refused') {
    return { deny: request.reason }
  }
  const title = `apogee_complete on ${request.backend}`
  const launched = await launchJob($, { kind: 'complete', title }, runComplete($, binary, offer, request), endOf, (job, outcome) => {
    $.ui.toast(endText(job, outcome), { timeoutMs: JOB_TOAST_MS })
  })
  if (launched.kind === 'running') {
    return {
      result:
        `The answer is not ready yet: it goes on as job ${launched.job.id} (${title}). A toast tells the person when it ends; ` +
        `read it with apogee_job ${JSON.stringify({ id: launched.job.id })}.`,
    }
  }

  return launched.outcome.kind === 'answered' ? { result: launched.outcome.text } : { deny: launched.outcome.reason }
}

function completeDescription(offer: CompleteOffer, widest: number, isOneBound: boolean): string {
  const named = isOneBound
    ? `${offer.backends.map(one => one.name).join(', ')}, each at most ${widest} tokens a call`
    : offer.backends
        .map(one => (one.isBoundUnsaid === true ? `${one.name} (its own bound, which the server does not say, at most ${one.maxTokens} tokens a call)` : `${one.name} (at most ${one.maxTokens} tokens a call)`))
        .join(', ')
  const why =
    offer.plumbing === 'served'
      ? "Apogee's own complete tool serves it, because the person's config lists complete under these backends' mcp_serve."
      : 'Offered because these backends run llama.cpp or MLX on this machine (apogee models list says so).'

  return [
    "Hand a prompt to one of Apogee's local models on this machine and answer with its reply.",
    'One call is one generation: the one-shot `apogee complete -m <backend> "<prompt>"`, with the backend\'s own system prompt and sampling, no tools and no retrieval.',
    `backend may name: ${named}.`,
    "Each runs on this machine and bills nothing per call, so a call spends this machine's time, never money.",
    `max_tokens asks for fewer, never more; left out, ${isOneBound ? widest : "the backend's bound"}.`,
    `An answer not ready within ${ASYNC_WINDOW_MS / 1000} seconds goes on as a job: the call answers its id at once, a toast tells the person when it ends, and apogee_job reads its answer.`,
    why,
  ].join(' ')
}

function endOf(outcome: CompleteOutcome): { state: 'done' | 'failed'; text?: string; reason?: string } {
  return outcome.kind === 'answered' ? { state: 'done', text: outcome.text } : { state: 'failed', reason: outcome.reason }
}

function endText(job: ApogeeJob, outcome: CompleteOutcome): string {
  return outcome.kind === 'answered' ? `Apogee job ${job.id} ended: ${job.title} answered; apogee_job reads it.` : `Apogee job ${job.id} failed: ${outcome.reason}`
}
