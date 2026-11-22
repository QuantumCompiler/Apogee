// apogee_complete as Claude meets it: offered only for a local, unmetered
// backend and saying so, bounded and saying so, the one-shot CLI run or the
// served complete beneath, and a long answer going on as a job (launch, toast,
// poll) rather than holding the turn.
import { expect, test } from 'claude-code/testing'

import { apogee, callTool, cliPath, either, scripted, served, servedListWith } from '../../support/apogee'
import { COMPLETE_ANSWER_HANDMADE, SERVED_COMPLETE_TOOL_HANDMADE, servedCompleteToolHandmade } from '../../support/handmade'
import { BIN, RELEASE_VERSION, START, machine } from '../../support/machine'
import type { Machine } from '../../support/machine'
import { MODELS_LIST_KINDS_REGISTERED, MODELS_LIST_SERVER_TOOLS } from '../../support/recorded'

const FOUND = { [`${BIN}/apogee`]: { stdout: RELEASE_VERSION } }
const MODELS = 'models list --output-format json'
const ONE_SHOT = 'complete -m q4 -n 1024 --rag  -- What is the capital of France?'

/** The listing where the config lists complete under q4 alone. */
const LISTED_WITH_COMPLETE = servedListWith(SERVED_COMPLETE_TOOL_HANDMADE)

/** Why apogee_complete is not offered where no backend is local and unmetered, and chats. */
const NONE_LOCAL =
  'no backend here is local and unmetered and chats (a configured llama.cpp backend, or an MLX one whose kind is chat or vision, by apogee models list)'

test('offered on the CLI path, the description says local, unmetered, bounded and what a slow answer does', async ($, on) => {
  const made = machine(on, FOUND, undefined, { script: cliPath() })

  await $.session.start(START)

  const tool = made.tools.find(one => one.name === 'apogee_complete')
  expect(tool?.description).toContain('backend may name: based, q4, tuned, each at most 1024 tokens a call')
  expect(tool?.description).toContain('a call spends this machine\'s time, never money')
  expect(tool?.description).toContain('max_tokens asks for fewer, never more; left out, 1024')
  expect(tool?.description).toContain('not ready within 30 seconds goes on as a job')
  expect(tool?.description).toContain('apogee_job')
  expect(tool?.description).toContain('run llama.cpp or MLX on this machine')
  expect(tool?.inputSchema).toEqual({
    type: 'object',
    properties: {
      prompt: { type: 'string', description: 'What the model is asked' },
      backend: { type: 'string', enum: ['based', 'q4', 'tuned'], description: 'The backend that answers: one of those this tool names' },
      max_tokens: { type: 'integer', minimum: 1, maximum: 1024, description: 'The most tokens the reply may run to; the bound, 1024, when left out' },
    },
    required: ['prompt', 'backend'],
  })
})

test('with no local, unmetered backend it is not offered, and /apogee says why', async ($, on) => {
  const made = machine(on, FOUND, undefined, { script: either(served(), scripted({ [MODELS]: { stdout: MODELS_LIST_SERVER_TOOLS } })) })
  await $.session.start(START)

  expect(made.tools.some(tool => tool.name === 'apogee_complete')).toBe(false)
  expect((await apogee($)).text).toContain(`apogee_complete: not offered: ${NONE_LOCAL}`)
})

test('on the CLI path an MLX backend that cannot chat is never offered: an embedding model registered as a backend', async ($, on) => {
  const made = machine(on, FOUND, undefined, { script: cliPath(undefined, MODELS_LIST_KINDS_REGISTERED) })
  await $.session.start(START)

  const tool = made.tools.find(one => one.name === 'apogee_complete')
  expect((tool?.inputSchema?.properties as { backend?: { enum?: unknown } } | undefined)?.backend?.enum).toEqual(['Gemma4-E4B-4bit', 'Qwen3-8B-4bit'])
  expect(tool?.description).not.toContain('EmbeddingGemma')
})

test('a listing whose only local backend is an embedding model offers no apogee_complete, and /apogee says why', async ($, on) => {
  // The recorded listing cut to its embedding backend's row.
  const recorded = JSON.parse(MODELS_LIST_KINDS_REGISTERED) as { data: { backend: string }[] }
  const embeddingOnly = JSON.stringify({ ...recorded, data: recorded.data.filter(row => row.backend === 'EmbeddingGemma-300M-bf16') })
  const made = machine(on, FOUND, undefined, { script: cliPath(undefined, embeddingOnly) })
  await $.session.start(START)

  expect(made.tools.some(one => one.name === 'apogee_complete')).toBe(false)
  expect((await apogee($)).text).toContain(`apogee_complete: not offered: ${NONE_LOCAL}`)
})

test('a call runs the one-shot on the CLI, bounded by the longest a run may take, and answers its reply', async ($, on) => {
  const made = machine(on, FOUND, undefined, { script: cliPath(scripted({ [ONE_SHOT]: { stdout: COMPLETE_ANSWER_HANDMADE } })) })
  await $.session.start(START)

  const answered = await callTool($, 'apogee_complete', { prompt: 'What is the capital of France?', backend: 'q4' })

  expect(answered).toEqual({ result: 'The capital of France is Paris.' })
  expect(made.runs.at(-1)).toEqual({
    argv: [`${BIN}/apogee`, 'complete', '-m', 'q4', '-n', '1024', '--rag', '', '--', 'What is the capital of France?'],
    init: { timeoutMs: 600_000 },
  })
})

test("a backend whose entry sets a lower max_tokens is offered at it: the one-shot passes it, and a call asking more is refused", async ($, on) => {
  const made = machine(on, FOUND, undefined, {
    script: cliPath(
      scripted({
        'config get backends.q4.max_tokens': { stdout: '200\n' },
        'complete -m q4 -n 200 --rag  -- Hi': { stdout: COMPLETE_ANSWER_HANDMADE },
      }),
    ),
  })
  await $.session.start(START)

  expect(made.runs.filter(run => run.argv[1] === 'config').map(run => run.argv.slice(1))).toEqual([
    ['config', 'get', 'backends.based.max_tokens'],
    ['config', 'get', 'backends.q4.max_tokens'],
    ['config', 'get', 'backends.tuned.max_tokens'],
  ])
  const tool = made.tools.find(one => one.name === 'apogee_complete')
  expect(tool?.description).toContain('backend may name: based (at most 1024 tokens a call), q4 (at most 200 tokens a call), tuned (at most 1024 tokens a call).')

  expect(await callTool($, 'apogee_complete', { prompt: 'Hi', backend: 'q4' })).toEqual({ result: 'The capital of France is Paris.' })
  expect(made.runs.at(-1)?.argv).toEqual([`${BIN}/apogee`, 'complete', '-m', 'q4', '-n', '200', '--rag', '', '--', 'Hi'])
  const before = made.runs.length
  expect(await callTool($, 'apogee_complete', { prompt: 'Hi', backend: 'q4', max_tokens: 201 })).toEqual({
    deny: 'max_tokens must be a whole number from 1 to 200 for q4',
  })
  expect(made.runs.length).toBe(before)
})

test("an entry's max_tokens that cannot be read keeps the bound at 1024", async ($, on) => {
  const made = machine(on, FOUND, undefined, {
    script: cliPath(scripted({ 'config get backends.q4.max_tokens': { exitCode: 1, stderr: "error: no such config key: 'backends.q4.max_tokens'\n" } })),
  })
  await $.session.start(START)

  expect(made.tools.find(one => one.name === 'apogee_complete')?.description).toContain('backend may name: based, q4, tuned, each at most 1024 tokens a call')
})

test("a request outside the offer is refused before anything runs, in the tool's own words", async ($, on) => {
  const made = machine(on, FOUND, undefined, { script: cliPath() })
  await $.session.start(START)
  const before = made.runs.length

  expect(await callTool($, 'apogee_complete', { prompt: 'Hi', backend: 'claude' })).toEqual({
    deny: "'claude' is not a backend apogee_complete runs on here: it may name based, q4, tuned",
  })
  expect(made.runs.length).toBe(before)
})

test('a one-shot that fails is the call\'s error, with its status and last words', async ($, on) => {
  machine(on, FOUND, undefined, {
    script: cliPath(args => (args[0] === 'complete' ? { exitCode: 1, stderr: "error: backend 'q4' failed to load: model file not found\n" } : undefined)),
  })
  await $.session.start(START)

  expect(await callTool($, 'apogee_complete', { prompt: 'Hi', backend: 'q4' })).toEqual({
    deny: "apogee complete on q4 failed (exit status 1): error: backend 'q4' failed to load: model file not found",
  })
})

test("where the config lists the served complete, the call goes to the server's complete, its bound and backends", async ($, on) => {
  const made = machine(on, FOUND, undefined, { script: served({ complete: { text: 'Paris.' } }, LISTED_WITH_COMPLETE) })
  await $.session.start(START)

  const tool = made.tools.find(one => one.name === 'apogee_complete')
  expect(tool?.description).toContain('backend may name: q4, each at most 1024 tokens a call')
  expect(tool?.description).toContain("Apogee's own complete tool serves it, because the person's config lists complete under these backends' mcp_serve")
  expect(made.runs.some(run => run.argv[1] === 'models')).toBe(false)

  const answered = await callTool($, 'apogee_complete', { prompt: 'Capital of France?', backend: 'q4', max_tokens: 64 })

  expect(answered).toEqual({ result: 'Paris.' })
  expect(made.runs.at(-1)?.init?.stdin).toContain(
    '"method":"tools/call","params":{"name":"complete","arguments":{"prompt":"Capital of France?","backend":"q4","max_tokens":64}}',
  )
  // The served complete gets the one-shot's ten minutes, not a read's 120 s:
  // the server loads the backend's model at the call, then generates.
  expect(made.runs.at(-1)?.init?.timeoutMs).toBe(600_000)
})

test('served on two backends whose bounds differ: each said at its own bound, and a bound left out never sent', async ($, on) => {
  const made = machine(on, FOUND, undefined, {
    script: served(
      { complete: { text: 'Paris.' } },
      servedListWith(
        servedCompleteToolHandmade([
          { name: 'q4', bound: 256 },
          { name: 'big', bound: 1024 },
        ]),
      ),
    ),
  })
  await $.session.start(START)

  const tool = made.tools.find(one => one.name === 'apogee_complete')
  expect(tool?.description).toContain('backend may name: q4 (at most 256 tokens a call), big (at most 1024 tokens a call).')
  expect(tool?.description).toContain("max_tokens asks for fewer, never more; left out, the backend's bound.")

  expect(await callTool($, 'apogee_complete', { prompt: 'Hi', backend: 'q4' })).toEqual({ result: 'Paris.' })
  expect(made.runs.at(-1)?.init?.stdin).toContain('"method":"tools/call","params":{"name":"complete","arguments":{"prompt":"Hi","backend":"q4"}}')
  expect(made.runs.at(-1)?.init?.stdin).not.toContain('max_tokens')

  const before = made.runs.length
  expect(await callTool($, 'apogee_complete', { prompt: 'Hi', backend: 'q4', max_tokens: 300 })).toEqual({
    deny: 'max_tokens must be a whole number from 1 to 256 for q4',
  })
  expect(made.runs.length).toBe(before)
  expect((await apogee($)).text).toContain("apogee_complete: on q4 (at most 256 tokens), big (at most 1024 tokens), by Apogee's served complete")
})

test("the served complete's refusal is the call's error, in the server's words", async ($, on) => {
  machine(on, FOUND, undefined, {
    script: served({ complete: { text: "Error: 'q4' no longer lists complete under mcp_serve -- the config was edited", isError: true } }, LISTED_WITH_COMPLETE),
  })
  await $.session.start(START)

  expect(await callTool($, 'apogee_complete', { prompt: 'Hi', backend: 'q4' })).toEqual({
    deny: "Error: 'q4' no longer lists complete under mcp_serve -- the config was edited",
  })
})

test('a completion that outlasts the window goes on as a job: its id at once, a toast at its end, its answer by apogee_job', async ($, on) => {
  let made: Machine | undefined
  made = machine(on, FOUND, undefined, {
    script: cliPath(async args => {
      if (args[0] !== 'complete') return undefined
      await (made as Machine).clock.sleep(50_000)

      return { stdout: COMPLETE_ANSWER_HANDMADE }
    }),
  })
  await $.session.start(START)

  const call = callTool($, 'apogee_complete', { prompt: 'What is the capital of France?', backend: 'q4' })
  await made.clock.advance(30_000)
  const launched = await call

  expect(launched).toEqual({
    result:
      'The answer is not ready yet: it goes on as job complete-1 (apogee_complete on q4). A toast tells the person when it ends; ' +
      'read it with apogee_job {"id":"complete-1"}.',
  })
  expect(await callTool($, 'apogee_job', { id: 'complete-1' })).toEqual({
    result: 'complete-1 is still running (apogee_complete on q4), 30 s so far. Read it again later; a toast tells the person when it ends.',
  })

  await made.clock.advance(20_000)

  expect(made.toasts.map(toast => toast.text)).toEqual(['Apogee job complete-1 ended: apogee_complete on q4 answered; apogee_job reads it.'])
  expect(await callTool($, 'apogee_job', { id: 'complete-1' })).toEqual({ result: 'The capital of France is Paris.' })
})

test('apogee_job refuses an id it never launched', async ($, on) => {
  machine(on, FOUND, undefined, { script: cliPath() })
  await $.session.start(START)

  expect(await callTool($, 'apogee_job', { id: 'complete-9' })).toEqual({
    deny: "No job 'complete-9': give the id the call that launched it said (complete-1, promote-2).",
  })
})
