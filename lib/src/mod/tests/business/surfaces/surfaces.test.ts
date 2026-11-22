// What the session is offered, decided once at its start and kept: the
// served tools, and apogee_complete by the served complete or the CLI path,
// or why not, each said where /apogee shows them.
import { expect, test } from 'claude-code/testing'

import { apogee, callTool, either, scripted, served } from '../../support/apogee'
import { MODELS_LIST_MIXED_HANDMADE, modelsStatusHandmade } from '../../support/handmade'
import { BIN, RELEASE_VERSION, START, machine } from '../../support/machine'
import { MODELS_STATUS_SUITE_CLOUD, SERVED_LIST, TRAIN_STATUS_CYCLE } from '../../support/recorded'

const FOUND = { [`${BIN}/apogee`]: { stdout: RELEASE_VERSION } }
const MODELS = 'models list --output-format json'
const STATUS = 'models status --output-format json'

/** The tail of each refusal over spend: where the served complete runs, calling no model but its backend. */
const SERVE_IT =
  "list complete under a local backend's mcp_serve (apogee config set-backend <backend> --mcp-serve complete) to serve it with no call but that backend's"

test('the server is listed once a session start; calls after it never list again', async ($, on) => {
  const made = machine(on, FOUND, undefined, { script: served({ train_status: { text: TRAIN_STATUS_CYCLE } }) })
  await $.session.start(START)

  await callTool($, 'train_status')
  await callTool($, 'train_status')

  expect(made.runs.filter(run => run.init?.stdin?.includes('"method":"tools/list"') === true).length).toBe(1)
})

test('neither the listing nor the models answering, both are said with why', async ($, on) => {
  machine(on, FOUND, undefined, {
    script: scripted({ 'mcp serve': { refuses: 'permission denied' }, [MODELS]: { exitCode: 1, stderr: 'error: no config\n' } }),
  })
  await $.session.start(START)

  const { text } = await apogee($)

  expect(text).toMatch(new RegExp(`Apogee's served tools: none registered: ${BIN}/apogee mcp serve could not run: .*permission denied`))
  expect(text).toContain(`apogee_complete: not offered: ${BIN}/apogee models list --output-format json answered no document: exited with status 1: error: no config`)
})

test("a served complete that names no backend is not offered, said as the server's", async ($, on) => {
  const listing = SERVED_LIST.replace(
    '"name":"check"}',
    '"name":"check"},{"annotations":{"readOnlyHint":false},"description":"x","inputSchema":{"type":"object","properties":{"backend":{"enum":[]}}},"name":"complete"}',
  )
  const made = machine(on, FOUND, undefined, { script: either(served({}, listing)) })
  await $.session.start(START)

  expect((await apogee($)).text).toContain("apogee_complete: not offered: the server's complete names no backend it may run")
  expect(made.runs.some(run => run.argv[1] === 'models' && run.argv[2] === 'list')).toBe(false)
})

// The CLI path's one-shot asks the utility model whether to think, where a
// backend's thinking is auto (complete.cpp one_shot: thinking_judge is the
// named utility; the served complete sets utility off). Over the mixed
// listing: gemma-mlx is MLX on this machine, ollama an Ollama backend, claude
// a cloud one.
const UTILITY_MATRIX: { utility: string; offered: boolean; said?: string }[] = [
  { utility: "(unset -- the chat's own backend)", offered: true },
  { utility: 'gemma-mlx', offered: true },
  { utility: 'gemma-mlx   (via suite pair)', offered: true },
  {
    utility: 'claude',
    offered: false,
    said: `the utility model (claude) is not local and unmetered, and the one-shot apogee complete asks it whether to think where a backend's thinking is auto, a call that may be billed; ${SERVE_IT}`,
  },
  { utility: 'claude   (via suite cloud)', offered: false, said: 'the utility model (claude) is not local and unmetered' },
  { utility: 'ollama', offered: false, said: 'the utility model (ollama) is not local and unmetered' },
]

for (const row of UTILITY_MATRIX) {
  test(`on the CLI path a utility model of ${row.utility} ${row.offered ? 'offers' : 'withholds'} apogee_complete`, async ($, on) => {
    const made = machine(on, FOUND, undefined, {
      script: either(served(), scripted({ [MODELS]: { stdout: MODELS_LIST_MIXED_HANDMADE }, [STATUS]: { stdout: modelsStatusHandmade('gemma-mlx', row.utility) } })),
    })
    await $.session.start(START)

    expect(made.tools.some(tool => tool.name === 'apogee_complete')).toBe(row.offered)
    const { text } = await apogee($)
    if (row.offered) {
      expect(text).toContain('apogee_complete: on gemma-mlx (at most 1024 tokens), by the one-shot apogee complete; local and unmetered')
    } else {
      expect(text).toContain(`apogee_complete: not offered: ${row.said ?? ''}`)
    }
  })
}

test('on the CLI path, models status not answering, or naming no utility, withholds apogee_complete, said with why', async ($, on) => {
  const answers = [{ exitCode: 1, stderr: 'error: no config\n' }, { stdout: JSON.stringify({ fields: [{ field: 'chat', value: 'gemma-mlx' }] }) }]
  const made = machine(on, FOUND, undefined, {
    script: either(served(), scripted({ [MODELS]: { stdout: MODELS_LIST_MIXED_HANDMADE } }), args => (args.join(' ') === STATUS ? answers.shift() : undefined)),
  })

  await $.session.start(START)
  expect(made.tools.some(tool => tool.name === 'apogee_complete')).toBe(false)
  expect((await apogee($)).text).toContain(
    `apogee_complete: not offered: ${BIN}/apogee models status --output-format json answered no document: exited with status 1: error: no config, ` +
      `so whether the one-shot apogee complete would call a billed model beside its backend cannot be told; ${SERVE_IT}`,
  )

  await $.session.start(START)
  expect((await apogee($)).text).toContain(
    `apogee_complete: not offered: models status names no utility model, so whether the one-shot apogee complete would ask a billed one cannot be told; ${SERVE_IT}`,
  )
})

test('on the CLI path an active suite withholds apogee_complete: the one-shot runs under it, and its validation may ask a member model', async ($, on) => {
  // complete.cpp gives the one-shot the suite's member calls, where its
  // validate: block checks an answer (27g); the served complete runs none.
  const made = machine(on, FOUND, undefined, {
    script: either(served(), scripted({ [MODELS]: { stdout: MODELS_LIST_MIXED_HANDMADE }, [STATUS]: { stdout: MODELS_STATUS_SUITE_CLOUD } })),
  })
  await $.session.start(START)

  expect(made.tools.some(tool => tool.name === 'apogee_complete')).toBe(false)
  expect((await apogee($)).text).toContain(
    `apogee_complete: not offered: the suite cloud is active, and the one-shot apogee complete runs under it, where its validation may ask a member model that may be billed; ${SERVE_IT}`,
  )
})
