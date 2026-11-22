// The Apogee client's reads: a document is read whatever the exit status (the
// check exits 1 on a failure and still prints its report), and a run that
// answers none says why, from its stderr or from what it printed.
import { expect, test } from 'claude-code/testing'

import { apogee, scripted } from '../../support/apogee'
import { BIN, RELEASE_VERSION, START, machine } from '../../support/machine'

const FOUND = { [`${BIN}/apogee`]: { stdout: RELEASE_VERSION } }

test('a document printed with a non-zero exit is read all the same', async ($, on) => {
  machine(on, FOUND, undefined, {
    script: scripted({ 'check --output-format json': { exitCode: 1, stdout: '{"rows":[{"section":"Config","name":"x","status":"fail","detail":"broken"}],"ok":false,"failures":1,"warnings":0}\n' } }),
  })
  await $.session.start(START)

  expect((await apogee($, 'check')).text).toStartWith('apogee check: 1 failure, 0 warnings')
})

test('a run that exits 0 with no document says what it printed instead', async ($, on) => {
  machine(on, FOUND, undefined, { script: scripted({ 'train status --output-format json': { stdout: 'No training runs yet.\n' } }) })
  await $.session.start(START)

  expect((await apogee($, 'train status')).text).toBe(
    `${BIN}/apogee train status --output-format json answered no document: not a JSON document: "No training runs yet."`,
  )
})

test('a run that exits non-zero with no document and nothing on stderr says its status alone', async ($, on) => {
  machine(on, FOUND, undefined, { script: scripted({ 'models list --output-format json': { exitCode: 2 } }) })
  await $.session.start(START)

  expect((await apogee($, 'models')).text).toBe(`${BIN}/apogee models list --output-format json answered no document: exited with status 2`)
})
