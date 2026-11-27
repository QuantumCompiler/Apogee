import agentsListBadConfig from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'
import chatsCompactMissing from '@recorded/business/reads/Fixtures/chats-compact-missing.err?raw'
import chatsCompact from '@recorded/business/reads/Fixtures/chats-compact.txt?raw'
import { describe, expect, test } from 'vitest'
import { JobStarter } from '../../../source/business/jobs/SupervisedJob'
import { Clock } from '../../../source/infrastructure/clock/Clock'
import type { ClockSettings } from '../../../source/infrastructure/clock/Clock'
import { StandInChild } from '../../../source/infrastructure/process/StandInChild'
import type { Rules } from '../../../source/infrastructure/process/StandInChild'
import { Compactions } from '../../../source/presentation/session/ViewModels/Compactions'

const settled = () => new Promise((resolve) => setTimeout(resolve, 0))
const retry = '20261119-111127-5d29'
const kv = '20261119-111551-8425'

function compactions(rules: Rules, clock: ClockSettings = {}) {
  const child = new StandInChild({ rules, apogee: 'installed' }, { configTypoRefusal: agentsListBadConfig })
  const ticks = new Clock(clock)
  const said: string[] = []
  const model = new Compactions(new JobStarter(child, ticks), () => StandInChild.binary)
  model.onSay = (text) => said.push(text)
  return { model, clock: ticks, said, child }
}

describe("a saved chat's compaction, run as a job (GUI 6b; the Mac app's compactions)", () => {
  const rules: Rules = {
    jobs: [
      { arguments: ['chats', 'compact', retry], answer: { stdout: chatsCompact, exitAfter: 2 } },
      { arguments: ['chats', 'compact', kv], answer: { stdout: chatsCompact, exitAfter: 30 } },
    ],
  }

  test('two run side by side, each busy; one ends and says its last line, the other still running', async () => {
    const { model, clock, said, child } = compactions(rules)
    expect(model.canCompact(retry)).toBe(true)
    void model.compact(retry)
    void model.compact(kv)
    expect(child.calls.map((call) => call.arguments.join(' '))).toEqual([`chats compact ${retry} --yes`, `chats compact ${kv} --yes`])
    expect(model.isCompacting(retry) && model.isCompacting(kv)).toBe(true)
    expect(model.canCompact(retry), 'not while its compaction runs').toBe(false)
    clock.advance(5)
    await settled()
    expect(said).toEqual(['compacted 20261026-142409-5159: 4 messages into 2, by compactor'])
    expect(model.isCompacting(retry)).toBe(false)
    expect(model.isCompacting(kv)).toBe(true)
    expect(model.canCompact(retry)).toBe(true)
  })

  test('Stop interrupts it, and it says so', async () => {
    const { model, clock, said } = compactions(rules)
    void model.compact(kv)
    clock.advance(3)
    await model.stopCompacting(kv)
    await settled()
    expect(said).toEqual(['Not compacted: stopped'])
    expect(model.isCompacting(kv)).toBe(false)
  })

  test("a failure is said in the CLI's words: chats-compact-missing.err", async () => {
    const { model, clock, said } = compactions({ jobs: [{ arguments: ['chats', 'compact'], answer: { stderr: chatsCompactMissing, exit: 1 } }] })
    void model.compact(retry)
    clock.advance(3)
    await settled()
    expect(said).toEqual([`Not compacted: ${chatsCompactMissing.trim()}`])
  })

  test("a still frame stands with both running: chat:compacting's", async () => {
    const { model, clock } = compactions(rules.jobs ? { jobs: rules.jobs.map((rule) => ({ ...rule, answer: { stdout: chatsCompact, exitAfter: 30 } })) } : rules, { start: 15, still: true })
    void model.compact(retry)
    void model.compact(kv)
    clock.start()
    clock.advance(10)
    await settled()
    expect([model.isCompacting(retry), model.isCompacting(kv)]).toEqual([true, true])
  })

  test('with no binary nothing can be compacted', () => {
    const child = new StandInChild({ rules: {}, apogee: 'installed' }, { configTypoRefusal: agentsListBadConfig })
    const model = new Compactions(new JobStarter(child, new Clock()), () => undefined)
    expect(model.canCompact(retry)).toBe(false)
  })
})
