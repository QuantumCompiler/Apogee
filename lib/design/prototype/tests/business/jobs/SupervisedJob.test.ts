import { describe, expect, test, vi } from 'vitest'
import agentsListBadConfig from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'
import trainPipelineRun from '@recorded/business/jobs/Fixtures/train-pipeline-run.jsonl?raw'
import agentsList from '@recorded/business/reads/Fixtures/agents-list.json?raw'
import datasetsPull from '@recorded/business/reads/Fixtures/datasets-pull-handmade.txt?raw'
import chatsCompact from '@recorded/business/reads/Fixtures/chats-compact.txt?raw'
import { JobEnd, JobStarter, JobUpdate } from '../../../source/business/jobs/SupervisedJob'
import { ReadRunner } from '../../../source/business/reads/ReadRunner'
import type { JobRunning } from '../../../source/business/jobs/SupervisedJob'
import { JobEvent } from '../../../source/data/protocol/JobEvent'
import { Clock } from '../../../source/infrastructure/clock/Clock'
import type { ClockSettings } from '../../../source/infrastructure/clock/Clock'
import { BoundedTail } from '../../../source/infrastructure/process/BoundedTail'
import { ChildExit } from '../../../source/infrastructure/process/ChildExit'
import { StandInChild } from '../../../source/infrastructure/process/StandInChild'
import type { ApogeeMode, Rules } from '../../../source/infrastructure/process/StandInChild'

const binary = StandInChild.binary
const lines = (text: string) => text.split('\n').filter((line, index, all) => !(line === '' && index === all.length - 1))

function starter(rules: Rules, clockSettings: ClockSettings = {}, apogee: ApogeeMode = 'installed') {
  const child = new StandInChild({ rules, apogee }, { configTypoRefusal: agentsListBadConfig })
  const clock = new Clock(clockSettings)
  return { jobs: new JobStarter(child, clock), clock, child }
}

/** What the job has said so far. */
const said = (job: JobRunning) => job.updates.yielded

// The Mac app's training tests end this run with exit 2: its second stage failed the gate (TrainingStreamTests.swift:65).
const pipelineRules: Rules = { jobs: [{ arguments: ['train', 'pipeline', 'run'], answer: { stdout: trainPipelineRun, exit: 2 } }] }

describe("a job's end held back by its rule's delay (design 1v)", () => {
  const compact = (exitAfter?: number): Rules => ({ jobs: [{ arguments: ['chats', 'compact'], answer: exitAfter === undefined ? { stdout: chatsCompact } : { stdout: chatsCompact, exitAfter } }] })

  test('chats-compact.txt (one line) with no delay ends on the tick after its line, as today', () => {
    const { jobs, clock } = starter(compact())
    const job = jobs.startJob(binary, ['chats', 'compact', '20261026-142409-5159', '--yes'], false)
    clock.advance(1)
    expect(said(job)).toEqual([JobUpdate.said(lines(chatsCompact)[0] ?? '')])
    clock.advance(1)
    expect(said(job).at(-1)?.case).toBe('ended')
  })

  test('a delay of 0 is the same: the next tick', () => {
    const { jobs, clock } = starter(compact(0))
    const job = jobs.startJob(binary, ['chats', 'compact', '20261026-142409-5159', '--yes'], false)
    clock.advance(2)
    expect(said(job).at(-1)?.case).toBe('ended')
  })

  test('a delay of 5 plays .ended 5 ticks after that: the job runs, its line said, until then', () => {
    const { jobs, clock } = starter(compact(5))
    const job = jobs.startJob(binary, ['chats', 'compact', '20261026-142409-5159', '--yes'], false)
    clock.advance(6)
    expect(said(job).map((update) => update.case)).toEqual(['said'])
    clock.advance(1)
    expect(said(job).at(-1)).toEqual(JobUpdate.ended(JobEnd.of(ChildExit.exited(0), new BoundedTail(), false)))
  })

  test('a still frame stands mid-job: its start ticks short of the delay leave it running, and Stop still ends it at once', async () => {
    vi.useFakeTimers()
    try {
      const { jobs, clock } = starter(compact(30), { start: 15, still: true })
      const job = jobs.startJob(binary, ['chats', 'compact', '20261026-142409-5159', '--yes'], false)
      clock.start()
      vi.advanceTimersByTime(60_000)
      expect(said(job).map((update) => update.case)).toEqual(['said'])
      await job.stop()
      expect(said(job).at(-1)).toEqual(JobUpdate.ended(JobEnd.of(ChildExit.exited(130), new BoundedTail(), true)))
    } finally {
      vi.useRealTimers()
    }
  })
})

describe('a job replays its recording on the clock', () => {
  test('train-pipeline-run.jsonl (33 lines) replays in order, one line a tick, then .ended with its rule\'s exit', () => {
    const { jobs, clock } = starter(pipelineRules)
    const job = jobs.startJob(binary, ['train', 'pipeline', 'run', 'skills', '--output-format', 'stream-json'], true)
    const recorded = lines(trainPipelineRun)
    expect(recorded.length).toBe(33)
    expect(said(job)).toEqual([])
    clock.advance(1)
    expect(said(job)).toEqual([JobUpdate.event(JobEvent.decode(recorded[0] ?? ''))])
    clock.advance(32)
    expect(said(job)).toEqual(recorded.map((line) => JobUpdate.event(JobEvent.decode(line))))
    clock.advance(1)
    expect(said(job).at(-1)).toEqual(JobUpdate.ended(JobEnd.of(ChildExit.exited(2), new BoundedTail(), false)))
    expect(said(job).length).toBe(34)
    clock.advance(10)
    expect(said(job).length).toBe(34)
  })

  test('it can be read with for await, .ended last', async () => {
    const { jobs, clock } = starter(pipelineRules)
    const job = jobs.startJob(binary, ['train', 'pipeline', 'run'], true)
    clock.advance(40)
    const seen: string[] = []
    for await (const update of job.updates) seen.push(update.case)
    expect(seen.length).toBe(34)
    expect(seen.at(-1)).toBe('ended')
  })

  test('stop() ends it as interrupted, and nothing more is said', async () => {
    const { jobs, clock } = starter(pipelineRules)
    const job = jobs.startJob(binary, ['train', 'pipeline', 'run'], true)
    clock.advance(5)
    await job.stop()
    expect(said(job).length).toBe(6)
    expect(said(job).at(-1)).toEqual(JobUpdate.ended(JobEnd.of(ChildExit.exited(130), new BoundedTail(), true)))
    clock.advance(40)
    await job.stop()
    expect(said(job).length).toBe(6)
  })

  test('a .txt replays as said lines, on a build before the stream', () => {
    const { jobs, clock } = starter({ jobs: [{ arguments: ['datasets', 'pull'], answer: { stdout: datasetsPull } }] })
    const job = jobs.startJob(binary, ['datasets', 'pull', 'x'], false)
    clock.advance(100)
    const recorded = lines(datasetsPull)
    expect(said(job)).toEqual([...recorded.map((line) => JobUpdate.said(line)), JobUpdate.ended(JobEnd.of(ChildExit.exited(0), new BoundedTail(), false))])
  })

  test('a faster pace replays several lines a tick', () => {
    const { jobs, clock } = starter(pipelineRules, { pace: 10 })
    const job = jobs.startJob(binary, ['train', 'pipeline', 'run'], true)
    clock.advance(1)
    expect(said(job).length).toBe(10)
    clock.advance(3)
    expect(said(job).length).toBe(33)
    clock.advance(1)
    expect(said(job).at(-1)?.case).toBe('ended')
  })

  test('a still preset applies its start ticks at once, then does not move', () => {
    vi.useFakeTimers()
    try {
      const { jobs, clock } = starter(pipelineRules, { start: 4, still: true })
      const job = jobs.startJob(binary, ['train', 'pipeline', 'run'], true)
      clock.start()
      expect(said(job).length).toBe(4)
      vi.advanceTimersByTime(60_000)
      expect(said(job).length).toBe(4)
    } finally {
      vi.useRealTimers()
    }
  })

  test('a still frame is its start ticks in for a job opened after an awaited read, and does not move', async () => {
    vi.useFakeTimers()
    try {
      const rules: Rules = { ...pipelineRules, reads: [{ arguments: ['agents', 'list'], answer: { stdout: agentsList } }] }
      const child = new StandInChild({ rules, apogee: 'installed' }, { configTypoRefusal: agentsListBadConfig })
      const clock = new Clock({ start: 4, still: true })
      const jobs = new JobStarter(child, clock)
      clock.start()
      // A view-model's appeared(): a read awaited, then the job started.
      await new ReadRunner(child).read(binary, ['agents', 'list'])
      const job = jobs.startJob(binary, ['train', 'pipeline', 'run'], true)
      expect(said(job).length).toBe(4)
      vi.advanceTimersByTime(60_000)
      expect(said(job).length).toBe(4)
    } finally {
      vi.useRealTimers()
    }
  })

  test('a still frame shorter than its start ticks ends a late job once, as the clock would have', async () => {
    vi.useFakeTimers()
    try {
      const short: Rules = { jobs: [{ arguments: ['datasets', 'pull'], answer: { stdout: datasetsPull } }] }
      const { jobs, clock } = starter(short, { start: 20, still: true })
      clock.start()
      await Promise.resolve()
      const job = jobs.startJob(binary, ['datasets', 'pull', 'x'], false)
      expect(said(job)).toEqual([
        ...lines(datasetsPull).map((line) => JobUpdate.said(line)),
        JobUpdate.ended(JobEnd.of(ChildExit.exited(0), new BoundedTail(), false)),
      ])
    } finally {
      vi.useRealTimers()
    }
  })

  test('on a live clock a job opened after start() begins at its first line', async () => {
    vi.useFakeTimers()
    try {
      const { jobs, clock } = starter(pipelineRules, { start: 4 })
      clock.start()
      await Promise.resolve()
      const job = jobs.startJob(binary, ['train', 'pipeline', 'run'], true)
      expect(said(job).length).toBe(0)
      vi.advanceTimersByTime(400 * 3)
      expect(said(job).length).toBe(3)
      clock.stop()
    } finally {
      vi.useRealTimers()
    }
  })

  test('a live preset runs on by itself, a line every 400 ms', () => {
    vi.useFakeTimers()
    try {
      const { jobs, clock } = starter(pipelineRules)
      const job = jobs.startJob(binary, ['train', 'pipeline', 'run'], true)
      clock.start()
      vi.advanceTimersByTime(400 * 10)
      expect(said(job).length).toBe(10)
      clock.stop()
    } finally {
      vi.useRealTimers()
    }
  })
})

describe('a job that never started', () => {
  test('nobody answered it: it ends at once, saying why', () => {
    const { jobs, child } = starter({})
    const job = jobs.startJob(binary, ['video', 'generate', 'a lighthouse'], true)
    expect(said(job)).toEqual([JobUpdate.ended(JobEnd.notStarted('no recorded answer for apogee video generate a lighthouse'))])
    expect(child.unanswered).toEqual(['jobs: apogee video generate a lighthouse'])
  })

  test('missing: nothing runs; config-typo: it exits 1 with the refusal', () => {
    expect(said(starter(pipelineRules, {}, 'missing').jobs.startJob(binary, ['train', 'pipeline', 'run'], true))).toEqual([
      JobUpdate.ended(JobEnd.notStarted('apogee is not installed')),
    ])
    const { jobs, clock } = starter(pipelineRules, {}, 'config-typo')
    const job = jobs.startJob(binary, ['train', 'pipeline', 'run'], true)
    clock.advance(1)
    expect(said(job)).toEqual([
      JobUpdate.ended(JobEnd.of(ChildExit.exited(1), BoundedTail.of(agentsListBadConfig.replace('apogee agents:', 'apogee train:')), false)),
    ])
    expect(JobEnd.succeeded(JobEnd.of(ChildExit.exited(0), new BoundedTail(), false))).toBe(true)
    expect(JobEnd.succeeded(JobEnd.notStarted('x'))).toBe(false)
  })
})
