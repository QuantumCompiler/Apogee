import { describe, expect, test, vi } from 'vitest'
import agentsListBadConfig from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'
import trainPipelineRun from '@recorded/business/jobs/Fixtures/train-pipeline-run.jsonl?raw'
import agentsList from '@recorded/business/reads/Fixtures/agents-list.json?raw'
import datasetsPull from '@recorded/business/reads/Fixtures/datasets-pull-handmade.txt?raw'
import chatsCompact from '@recorded/business/reads/Fixtures/chats-compact.txt?raw'
import trainRun from '@recorded/business/reads/Fixtures/train-run.err?raw'
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

  test('held at a line (design 1k’s `holdAt`): its lines up to there, the delay, then the rest and its end -- at the pace, never past the hold in one tick', () => {
    const rules: Rules = { jobs: [{ arguments: ['train', 'setup'], answer: { stdout: 'one\ntwo\nthree\nfour\nfive\n', holdAt: 3, exitAfter: 2 } }] }
    const { jobs, clock } = starter(rules, { pace: 2 })
    const job = jobs.startJob(binary, ['train', 'setup'], false)
    const lines = () => said(job).flatMap((update) => (update.case === 'said' ? [update.line] : []))
    clock.advance(1)
    expect(lines()).toEqual(['one', 'two'])
    clock.advance(1)
    // Two lines a tick, but the hold is at the third.
    expect(lines()).toEqual(['one', 'two', 'three'])
    clock.advance(2)
    expect(lines()).toEqual(['one', 'two', 'three'])
    clock.advance(1)
    expect(lines()).toEqual(['one', 'two', 'three', 'four', 'five'])
    expect(said(job).at(-1)?.case).toBe('said')
    clock.advance(1)
    expect(said(job).at(-1)?.case).toBe('ended')
  })

  test('a Stop tells the stand-in the job did not run to its end (design 1k): a rule waiting on its success waits again; an end of its own does not', async () => {
    const rules: Rules = {
      jobs: [
        { arguments: ['train', 'setup'], answer: { stdout: 'installed\n' } },
        { arguments: ['video', 'generate'], until: { ran: ['train', 'setup'], answer: { exit: 1 } }, answer: { stdout: 'made\n' } },
      ],
    }
    const { jobs, clock, child } = starter(rules)
    const install = jobs.startJob(binary, ['train', 'setup'], false)
    await install.stop()
    expect(child.run('jobs', ['video', 'generate'])).toMatchObject({ started: true, run: { stdout: '' } })
    jobs.startJob(binary, ['train', 'setup'], false)
    clock.advance(2)
    expect(child.run('jobs', ['video', 'generate'])).toMatchObject({ started: true, run: { stdout: 'made\n' } })
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

describe("a run that speaks on stderr: its busy line, one a tick (design 1j's `busy` rule)", () => {
  // `train run` takes no machine stream and prints its progress on stderr, which the Mac samples as the busy line.
  const run: Rules = { jobs: [{ arguments: ['train', 'run'], answer: { stderr: trainRun, busy: true } }] }

  test('train-run.err plays one line a tick as the busy line, then ends with the whole stderr as its tail', () => {
    const { jobs, clock } = starter(run)
    const job = jobs.startJob(binary, ['train', 'run', '/Users/someone/work/student', '--dataset', '/Users/someone/work/data.jsonl'], false)
    clock.advance(4)
    expect(said(job)).toEqual(lines(trainRun).slice(0, 4).map((line) => JobUpdate.busy(line)))
    clock.advance(3)
    expect(said(job).map((update) => update.case)).toEqual(['busy', 'busy', 'busy', 'busy', 'busy', 'busy', 'busy'])
    clock.advance(1)
    expect(said(job).at(-1)).toEqual(JobUpdate.ended(JobEnd.of(ChildExit.exited(0), BoundedTail.of(trainRun), false)))
  })

  test("its stderr's lines come before its stdout's, as a promote narrates on stderr and says its result on stdout", () => {
    const { jobs, clock } = starter({ jobs: [{ arguments: ['train', 'promote'], answer: { stderr: '[promote] one\n[promote] two\n', stdout: 'done\n', busy: true } }] })
    const job = jobs.startJob(binary, ['train', 'promote', 'x', '--as', 'y'], false)
    clock.advance(4)
    expect(said(job)).toEqual([JobUpdate.busy('[promote] one'), JobUpdate.busy('[promote] two'), JobUpdate.said('done'), JobUpdate.ended(JobEnd.of(ChildExit.exited(0), BoundedTail.of('[promote] one\n[promote] two\n'), false))])
  })

  test('without the rule, stderr is only the tail at the end, as before', () => {
    const { jobs, clock } = starter({ jobs: [{ arguments: ['train', 'run'], answer: { stderr: trainRun } }] })
    const job = jobs.startJob(binary, ['train', 'run'], false)
    clock.advance(1)
    expect(said(job).map((update) => update.case)).toEqual(['ended'])
  })
})

describe("a job that writes a folder's file as it runs, and is resumed by the same command (design 1l's `writes`)", () => {
  const folder = '/Users/someone/Movies/Apogee'
  const saved = (made: number, status = 'rendering') => ({ name: 'tides.mp4.json', text: `{"object":"video.project","output":"${folder}/tides.mp4","status":"${status}","made":${made}}` })
  const render: Rules = {
    jobs: [
      {
        arguments: ['video', 'compose', '-o', `${folder}/tides.mp4`],
        answer: {
          stdout: 'job\nscene 1\nstep 1\nscene 2\nstep 2\ndone\n',
          writes: [
            { after: 1, folder, file: saved(0), stopped: saved(0, 'cancelled') },
            { after: 3, folder, file: saved(1), stopped: saved(1, 'cancelled') },
            { after: 5, folder, file: saved(2), stopped: saved(2, 'cancelled') },
            { after: 6, folder, file: saved(2, 'done') },
          ],
        },
      },
    ],
  }
  const args = ['video', 'compose', '-o', `${folder}/tides.mp4`]
  const lines = (job: JobRunning) => said(job).flatMap((update) => (update.case === 'said' ? [update.line] : []))
  const project = (child: StandInChild) => child.folders.files(folder).find((file) => file.name === 'tides.mp4.json')?.text ?? 'none'

  test('each file is written once its line has played -- the project the composer saves after every step -- and the last, done, before its end', async () => {
    const { jobs, clock, child } = starter(render)
    const job = jobs.startJob(binary, args, false)
    expect(project(child)).toBe('none')
    clock.advance(1)
    expect(project(child)).toContain('"status":"rendering","made":0')
    clock.advance(2)
    expect(project(child)).toContain('"made":1')
    clock.advance(3)
    expect(project(child)).toContain('"status":"done","made":2')
    clock.advance(1)
    expect(said(job).at(-1)?.case).toBe('ended')
  })

  test('Stop writes what its last write says a stop leaves -- the project cancelled, its finished scenes kept -- and the same command resumes from that write, its first line then the rest', async () => {
    const { jobs, clock, child } = starter(render)
    const job = jobs.startJob(binary, args, false)
    clock.advance(4)
    await job.stop()
    expect(project(child)).toContain('"status":"cancelled","made":1')
    const resumed = jobs.startJob(binary, args, false)
    clock.advance(1)
    expect(lines(resumed)).toEqual(['job'])
    expect(project(child)).toContain('"status":"rendering","made":1')
    clock.advance(3)
    expect(lines(resumed)).toEqual(['job', 'scene 2', 'step 2', 'done'])
    expect(project(child)).toContain('"status":"done","made":2')
  })

  test('a job stopped before its first write leaves the folder as it was and resumes nothing; one that ends of itself leaves nothing to resume', async () => {
    const { jobs, clock, child } = starter(render)
    await jobs.startJob(binary, args, false).stop()
    expect(project(child)).toBe('none')
    const whole = jobs.startJob(binary, args, false)
    clock.advance(7)
    expect(said(whole).at(-1)?.case).toBe('ended')
    const again = jobs.startJob(binary, args, false)
    clock.advance(2)
    expect(lines(again)).toEqual(['job', 'scene 1'])
  })

  test('a write after no line is there as the job starts -- what the composer has beside the video before its first line -- so a Stop at once leaves its stopped twin, and a resume is never written over by it', async () => {
    const early: Rules = { jobs: [{ arguments: args, answer: { stdout: 'job\nscene 1\nstep 1\ndone\n', writes: [{ after: 0, folder, file: saved(0), stopped: saved(0, 'cancelled') }, { after: 3, folder, file: saved(1), stopped: saved(1, 'cancelled') }, { after: 4, folder, file: saved(1, 'done') }] } }] }
    const { jobs, clock, child } = starter(early)
    const job = jobs.startJob(binary, args, false)
    expect(project(child)).toContain('"status":"rendering","made":0')
    await job.stop()
    expect(project(child)).toContain('"status":"cancelled","made":0')
    jobs.startJob(binary, args, false)
    expect(project(child)).toContain('"status":"cancelled","made":0')
    clock.advance(1)
    expect(project(child)).toContain('"status":"rendering","made":0')
    clock.advance(3)
    expect(project(child)).toContain('"status":"done","made":1')
    // Stopped after a later write, the resume starts without the first write laid over it.
    const { jobs: later, clock: laterClock, child: laterChild } = starter(early)
    const first = later.startJob(binary, args, false)
    laterClock.advance(3)
    await first.stop()
    later.startJob(binary, args, false)
    expect(project(laterChild)).toContain('"status":"cancelled","made":1')
  })

  test('the folders start as the preset says, and a write replaces the file of its name', () => {
    const child = new StandInChild({ rules: render, apogee: 'installed', folders: { [folder]: [{ name: 'tides.mp4' }, saved(7, 'planned')] } }, { configTypoRefusal: agentsListBadConfig })
    expect(child.folders.files(folder).map((file) => file.name)).toEqual(['tides.mp4', 'tides.mp4.json'])
    child.folders.write(folder, saved(0))
    expect(child.folders.files(folder).map((file) => file.text ?? file.name)).toEqual(['tides.mp4', saved(0).text])
    expect(child.folders.files('/elsewhere')).toEqual([])
  })
})
