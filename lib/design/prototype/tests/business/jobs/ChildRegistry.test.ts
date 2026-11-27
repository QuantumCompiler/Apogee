import { describe, expect, test } from 'vitest'
import agentsListBadConfig from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'
import chatsCompact from '@recorded/business/reads/Fixtures/chats-compact.txt?raw'
import { ChildRegistry } from '../../../source/business/jobs/ChildRegistry'
import { JobStarter } from '../../../source/business/jobs/SupervisedJob'
import { Clock } from '../../../source/infrastructure/clock/Clock'
import { StandInChild } from '../../../source/infrastructure/process/StandInChild'

describe('the shared list of every child the app runs (the Mac’s ChildRegistry)', () => {
  test('a child registered is listed, in order, by kind; it leaves at its end, and leaving twice is leaving once', () => {
    const registry = new ChildRegistry()
    const job = registry.register('job', 'apogee models pull Qwen/Qwen3-8B', async () => {})
    const session = registry.register('session', 'Session: root', async () => {})
    expect(registry.children().map((child) => [child.id, child.kind, child.label])).toEqual([
      [1, 'job', 'apogee models pull Qwen/Qwen3-8B'],
      [2, 'session', 'Session: root'],
    ])
    expect(registry.children('job').map((child) => child.label)).toEqual(['apogee models pull Qwen/Qwen3-8B'])
    job.relabel('apogee models pull Qwen/Qwen3-8B (stage 2)')
    expect(registry.children('job')[0]?.label).toBe('apogee models pull Qwen/Qwen3-8B (stage 2)')
    job.leave()
    job.leave()
    expect(registry.children().map((child) => child.id)).toEqual([2])
    session.leave()
    expect(registry.children()).toEqual([])
  })

  test('whoever watches is told each change: a registration, a relabel, a leave', () => {
    const registry = new ChildRegistry()
    let told = 0
    const stop = registry.onChange(() => (told += 1))
    const child = registry.register('job', 'apogee models convert x', async () => {})
    child.relabel('y')
    child.leave()
    expect(told).toBe(3)
    stop()
    registry.register('job', 'z', async () => {})
    expect(told).toBe(3)
  })

  test("a job's label is the command as typed: the words before its first option, then the text after --, at most 120 characters", () => {
    expect(ChildRegistry.label(['models', 'convert', 'Llama3.2-1B-Instruct/safetensors/edaba290ce7f', '--register-with', 'Q4_K_M'])).toBe('apogee models convert Llama3.2-1B-Instruct/safetensors/edaba290ce7f')
    expect(ChildRegistry.label(['--model', 'x', 'chat'])).toBe('apogee --model x chat')
    expect(ChildRegistry.label(['chats', 'title', 'id', '--', 'A name'])).toBe('apogee chats title id A name')
    const long = ChildRegistry.label(['video', 'generate', '--', 'x'.repeat(200)])
    expect(long).toHaveLength(120)
    expect(long.endsWith('…')).toBe(true)
  })

  test('the job starter registers each job it starts, labelled by its command; the job leaves the list at its end, and one that never started is never listed', () => {
    const child = new StandInChild({ rules: { jobs: [{ arguments: ['chats', 'compact'], answer: { stdout: chatsCompact } }] }, apogee: 'installed' }, { configTypoRefusal: agentsListBadConfig })
    const clock = new Clock({})
    const registry = new ChildRegistry()
    const jobs = new JobStarter(child, clock, registry)
    jobs.startJob(StandInChild.binary, ['chats', 'compact', '20261026-142409-5159', '--yes'], false)
    jobs.startJob(StandInChild.binary, ['models', 'pull', 'nobody/recorded'], true)
    expect(registry.children('job').map((job) => job.label)).toEqual(['apogee chats compact 20261026-142409-5159'])
    clock.advance(2)
    expect(registry.children('job')).toEqual([])
  })

  test("a registered job's Stop is its supervisor's: stopping every job ends it, interrupted, and it leaves", async () => {
    const child = new StandInChild({ rules: { jobs: [{ arguments: ['chats', 'compact'], answer: { stdout: chatsCompact } }] }, apogee: 'installed' }, { configTypoRefusal: agentsListBadConfig })
    const clock = new Clock({})
    const registry = new ChildRegistry()
    const job = new JobStarter(child, clock, registry).startJob(StandInChild.binary, ['chats', 'compact', 'x', '--yes'], false)
    expect(await registry.stop('job')).toBe(true)
    expect(job.updates.yielded.at(-1)).toMatchObject({ case: 'ended', end: { interrupted: true } })
    expect(registry.children()).toEqual([])
  })

  test("one child's Stop, by its id: the monitor's Stop on a run (design 1k), the others left running", async () => {
    const registry = new ChildRegistry()
    const stopped: number[] = []
    const one = registry.register('job', 'apogee video generate a', async () => (stopped.push(1), one.leave()))
    registry.register('job', 'apogee train setup', async () => void stopped.push(2))
    expect(await registry.stopChild(one.id)).toBe(true)
    expect(stopped).toEqual([1])
    expect(registry.children().map((child) => child.label)).toEqual(['apogee train setup'])
    expect(await registry.stopChild(99)).toBe(false)
  })

  test("a job's arguments are kept beside its label (design 1k): what a place reads of another's run, a generate's model among them", () => {
    const registry = new ChildRegistry()
    registry.register('job', 'apogee video generate', async () => {}, ['video', 'generate', '-o', 'x.mp4', '-m', 'm'])
    expect(registry.children('job')[0]?.arguments).toEqual(['video', 'generate', '-o', 'x.mp4', '-m', 'm'])
  })
})
