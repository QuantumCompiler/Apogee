// The status line's reads, on the kit's mocked clock: `models status` and
// `train status` read at the start and then once a minute (the item's
// recorded default: conservative), never two at once, and again straight
// after a confirmed command ends (read back, never mirrored).
import { expect, test } from 'claude-code/testing'

import { apogee, either, scripted, served } from '../../support/apogee'
import { modelsStatusHandmade, trainStatusHandmade } from '../../support/handmade'
import { BIN, RELEASE_VERSION, START, machine } from '../../support/machine'
import type { Machine } from '../../support/machine'

const FOUND = { [`${BIN}/apogee`]: { stdout: RELEASE_VERSION } }
const STATUS = 'models status --output-format json'
const TRAINING = 'train status --output-format json'

function reads(made: Machine): string[] {
  return made.runs.map(run => run.argv.slice(1).join(' ')).filter(words => words === STATUS || words === TRAINING)
}

test('the reads run once at the start, then once a minute, models status and train status as documents', async ($, on) => {
  const made = machine(on, FOUND, undefined, {
    script: either(served(), scripted({ [STATUS]: { stdout: modelsStatusHandmade('q4') }, [TRAINING]: { stdout: trainStatusHandmade('q4', 1) } })),
  })
  await $.session.start(START)

  await made.clock.settle()
  expect(reads(made)).toEqual([STATUS, TRAINING])
  expect(made.runs.filter(run => run.argv[1] === 'models' && run.argv[2] === 'status')[0]?.init).toEqual({ timeoutMs: 30_000 })

  await made.clock.advance(59_999)
  expect(reads(made).length).toBe(2)
  await made.clock.advance(1)
  expect(reads(made)).toEqual([STATUS, TRAINING, STATUS, TRAINING])
  await made.clock.advance(60_000)
  expect(reads(made).length).toBe(6)
})

test('a read still running when the next minute comes is not started twice', async ($, on) => {
  let made: Machine | undefined
  made = machine(on, FOUND, undefined, {
    script: either(served(), async args => {
      const words = args.join(' ')
      if (words === STATUS) {
        await (made as Machine).clock.sleep(90_000)

        return { stdout: modelsStatusHandmade('q4') }
      }

      return words === TRAINING ? { stdout: trainStatusHandmade('q4', 1) } : undefined
    }),
  })
  await $.session.start(START)

  await made.clock.advance(60_000)
  expect(reads(made)).toEqual([STATUS])
  await made.clock.advance(60_000)
  expect(reads(made)).toEqual([STATUS, TRAINING, STATUS])
})

test('a confirmed rollback that ends is read back at once, not at the next minute', async ($, on) => {
  const made = machine(on, FOUND, undefined, {
    script: either(
      served(),
      scripted({ [STATUS]: { stdout: modelsStatusHandmade('tuned') }, [TRAINING]: { stdout: trainStatusHandmade('tuned', 1) }, 'train rollback tuned': { stdout: 'rolled back tuned: v2 -> v1\n' } }),
    ),
    ask: () => 'Run it',
  })
  await $.session.start(START)
  await made.clock.settle()
  const before = reads(made).length

  await apogee($, 'rollback tuned')
  await made.clock.settle()

  expect(reads(made).length).toBe(before + 2)
})

test('the read-back after a command waits for a poll read in flight, so the line ends on what the command left', async ($, on) => {
  // A rollback ends while a minute's read of train status, begun before it
  // wrote, is still running: the read-back must come after it, never beside it.
  let made: Machine | undefined
  let version = 2
  let isNextSlow = false
  let inFlight = 0
  let mostAtOnce = 0
  made = machine(on, FOUND, undefined, {
    script: either(served(), async args => {
      const words = args.join(' ')
      if (words === 'train rollback q4') {
        await (made as Machine).clock.sleep(1_000)
        version = 1

        return { stdout: 'rolled back q4: v2 -> v1\n' }
      }
      if (words !== STATUS && words !== TRAINING) return undefined
      inFlight += 1
      mostAtOnce = Math.max(mostAtOnce, inFlight)
      try {
        if (words === STATUS) return { stdout: modelsStatusHandmade('q4') }
        const seen = version
        if (isNextSlow) {
          isNextSlow = false
          await (made as Machine).clock.sleep(5_000)
        }

        return { stdout: trainStatusHandmade('q4', seen) }
      } finally {
        inFlight -= 1
      }
    }),
    ask: () => 'Run it',
  })
  await $.session.start(START)
  await made.clock.settle()
  expect(made.statuses.at(-1)).toBe('Apogee: q4 v2 · idle')

  isNextSlow = true
  await made.clock.advance(61_000)
  await apogee($, 'rollback q4')
  await made.clock.advance(1_000)
  await made.clock.advance(4_000)

  expect(made.statuses.at(-1)).toBe('Apogee: q4 v1 · idle')
  expect(mostAtOnce).toBe(1)
})

test('with no binary nothing is read and no status line is set', async ($, on) => {
  const made = machine(on, {})
  await $.session.start(START)

  await made.clock.advance(180_000)

  expect(reads(made)).toEqual([])
  expect(made.statuses).toEqual([])
})
