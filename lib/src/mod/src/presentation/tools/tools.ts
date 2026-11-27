// The tools Claude is given (`mcp__apogee__<name>`): Apogee's served tools,
// registered by their own names, descriptions and schemas from the found
// binary's listing, and apogee_job, the poll of the async law. Presentation
// registers and answers; Business decides what runs (ADR 0001).
import type { EngineInterface, ToolCallResult } from 'claude-code'

import type { ApogeeJob } from '../../../types'
import { sessionBinary } from '../../business/client/client'
import { shownCommand } from '../../business/commands/plan'
import { jobOf } from '../../business/jobs/jobs'
import { SERVE_ARGS, callServed } from '../../business/served/served'
import { keepRegistered, offeredTools } from '../../business/surfaces/surfaces'
import type { Offered } from '../../business/surfaces/surfaces'
import { now } from '../../infrastructure/timer/timer'
import { answerComplete, registerComplete } from './complete'

/** Every tool the plugin registers is listed under this prefix. */
export const TOOL_PREFIX = 'mcp__apogee__'

/** The poll: one job, by its id. */
export const JOB_TOOL = 'apogee_job'

/** One local completion: the CLI's one-shot, or the server's complete. */
export const COMPLETE_TOOL = 'apogee_complete'

/** What a call carries beside the tool's own arguments, none of which the tool sees. */
const ENVELOPE = new Set(['tool', 'tool_use_id', 'agentId', 'requestMeta', 'consent'])

/** What the engine refused of the session's tools: each name, out of how many, and its first reason. */
export type Refused = { names: string[]; total: number; reason: string }

/**
 * Registers the session's tools: every served tool offered, apogee_complete
 * where it is, then apogee_job, each on its own, so one the engine refuses
 * never drops those after it. What took is kept (`keepRegistered`); what was
 * refused is answered, for the session start to say once.
 */
export async function registerTools($: EngineInterface, offered: Offered): Promise<Refused | undefined> {
  const served: string[] = []
  const refused: { name: string; reason: string }[] = []
  for (const tool of offered.served) {
    const reason = await refusalOf($.tool.register({ name: tool.name, description: tool.description, inputSchema: tool.inputSchema }))
    if (reason === undefined) served.push(tool.name)
    else refused.push({ name: tool.name, reason })
  }
  const servedRefusal = refused[0]?.reason
  const completeRefusal = offered.complete === undefined ? undefined : await refusalOf(registerComplete($, offered.complete))
  if (completeRefusal !== undefined) refused.push({ name: COMPLETE_TOOL, reason: completeRefusal })
  const jobRefusal = await refusalOf(registerJob($))
  if (jobRefusal !== undefined) refused.push({ name: JOB_TOOL, reason: jobRefusal })
  await keepRegistered($, offered, {
    served,
    ...(servedRefusal === undefined ? {} : { servedRefusal }),
    ...(completeRefusal === undefined ? {} : { completeRefusal }),
  })
  const first = refused[0]

  return first === undefined ? undefined : { names: refused.map(one => one.name), total: offered.served.length + (offered.complete === undefined ? 1 : 2), reason: first.reason }
}

/** Why a registration was refused, in the engine's words; undefined when it took. */
async function refusalOf(registering: Promise<unknown>): Promise<string | undefined> {
  try {
    await registering

    return undefined
  } catch (error) {
    return error instanceof Error ? error.message : String(error)
  }
}

/** Registers apogee_job, the async law's poll. */
async function registerJob($: EngineInterface): Promise<void> {
  await $.tool.register({
    name: 'apogee_job',
    description:
      'A job Apogee runs for you that outlasted its call: an apogee_complete answer not ready within its call, or a promote or ' +
      'rollback the person confirmed. Give the id the call said; the answer says whether it still runs, and once it ended, ' +
      'its answer or why it failed. Reading a job runs nothing.',
    inputSchema: {
      type: 'object',
      properties: { id: { type: 'string', description: "The job's id, as the call that launched it said it (complete-1, promote-2)" } },
      required: ['id'],
    },
  })
}

/**
 * Answers a call of one of the plugin's tools; undefined for a tool of the
 * prefix the plugin did not register, which the root passes on.
 */
export async function answerTool($: EngineInterface, call: Readonly<Record<string, unknown>>): Promise<ToolCallResult | undefined> {
  const tool = typeof call.tool === 'string' ? call.tool : ''
  if (!tool.startsWith(TOOL_PREFIX)) {
    return undefined
  }
  const name = tool.slice(TOOL_PREFIX.length)
  const args = Object.fromEntries(Object.entries(call).filter(([key]) => !ENVELOPE.has(key)))
  if (name === JOB_TOOL) {
    return answerJob($, args)
  }
  const offered = await offeredTools($)
  const complete = name === COMPLETE_TOOL ? offered.complete : undefined
  if (complete === undefined && !offered.served.includes(name)) {
    return undefined
  }
  const binary = await sessionBinary($)
  if (binary?.kind !== 'found') {
    return { deny: 'Apogee is not installed where the mod looks: /apogee says where it looked.' }
  }
  if (complete !== undefined) {
    return answerComplete($, binary, complete, args)
  }
  const called = await callServed($, binary, name, args)
  const server = `Apogee's server (${shownCommand(binary.path, SERVE_ARGS)})`
  if (called.kind === 'failed') {
    return { deny: `${server} ${called.stage === 'run' ? 'could not run' : 'answered no reply'}: ${called.reason}` }
  }

  return called.isError ? { deny: called.text } : { result: called.text }
}

/** apogee_job: one job's state and, once it ended, what it left. */
async function answerJob($: EngineInterface, args: Readonly<Record<string, unknown>>): Promise<ToolCallResult> {
  const id = typeof args.id === 'string' ? args.id : ''
  const job = id === '' ? undefined : await jobOf($, id)
  if (job === undefined) {
    return { deny: `No job ${id === '' ? 'was named' : `'${id}'`}: give the id the call that launched it said (complete-1, promote-2).` }
  }

  return { result: await jobText($, job) }
}

async function jobText($: EngineInterface, job: ApogeeJob): Promise<string> {
  if (job.state === 'running') {
    const seconds = Math.max(0, Math.round(((await now($)) - job.startedAt) / 1000))

    return `${job.id} is still running (${job.title}), ${seconds} s so far. Read it again later; a toast tells the person when it ends.`
  }
  if (job.state === 'done') return job.text === undefined || job.text === '' ? `${job.id} ended (${job.title}) and printed nothing.` : job.text
  if (job.state === 'lost') return `${job.id} was lost (${job.title}): ${job.reason ?? 'its run ended unread'}`

  return `${job.id} failed (${job.title}): ${job.reason ?? 'no reason was kept'}`
}
