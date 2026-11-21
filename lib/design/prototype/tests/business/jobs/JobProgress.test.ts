import { describe, expect, test } from 'vitest'
import pullChainDone from '@recorded/business/jobs/Fixtures/pull-chain-done-handmade.jsonl?raw'
import datasetsPull from '@recorded/business/jobs/Fixtures/datasets-pull-handmade.jsonl?raw'
import { JobProgress } from '../../../source/business/jobs/JobProgress'
import { JobEnd, JobUpdate } from '../../../source/business/jobs/SupervisedJob'
import { JobEvent } from '../../../source/data/protocol/JobEvent'
import { BoundedTail } from '../../../source/infrastructure/process/BoundedTail'
import { ChildExit } from '../../../source/infrastructure/process/ChildExit'

const events = (text: string) =>
  text
    .split('\n')
    .filter((line) => line.trim() !== '')
    .map((line) => JobUpdate.event(JobEvent.decode(line)))

const ended = (status: number, interrupted = false, stderr = '') => JobUpdate.ended(JobEnd.of(ChildExit.exited(status), BoundedTail.of(stderr), interrupted))

function folded(updates: readonly JobUpdate[]): JobProgress {
  const progress = new JobProgress()
  for (const update of updates) progress.apply(update)
  return progress
}

describe("a job's progress, folded from what it said (the Mac's JobProgress)", () => {
  test('pull-chain-done-handmade.jsonl: four stages, each done, its notices kept in order, done once it ended well', () => {
    const progress = folded([...events(pullChainDone), ended(0)])
    expect(progress.status).toEqual({ case: 'done' })
    expect(progress.streamed).toBe(true)
    expect(progress.stageCount).toBe(4)
    expect(progress.orderedStages().map(({ index, stage }) => [index, stage.label, stage.state])).toEqual([
      [1, "pull org/model's full weights", 'done'],
      [2, 'convert to F16', 'done'],
      [3, 'quantize to Q4_K_M', 'done'],
      [4, 'register 2 backends', 'done'],
    ])
    expect(progress.notices.at(0)).toBe('from here on, if this stops, resume with:')
    expect(progress.notices.at(-1)).toBe('chat with one:  apogee chat -m model-Q4_K_M')
    expect(progress.lastSaid).toBe('chat with one:  apogee chat -m model-Q4_K_M')
    expect(progress.documents).toEqual([])
  })

  test('a run still going names no resume, though its chain said one mid-run; a done run names none either', () => {
    const running = folded(events(pullChainDone).slice(0, 6))
    expect(running.status).toEqual({ case: 'running' })
    expect(running.resumeCommand).toBeUndefined()
    expect(folded([...events(pullChainDone), ended(0)]).resumeCommand).toBeUndefined()
  })

  test('stopped by its Stop after the pull: stopped, the stage that was running said stopped, and the resume its chain named', () => {
    const progress = folded([...events(pullChainDone).slice(0, 6), ended(130, true)])
    expect(progress.status).toEqual({ case: 'stopped' })
    expect(progress.stages.get(2)?.state).toBe('stopped')
    expect(progress.unfinishedStage).toBe(2)
    expect(progress.resumeCommand).toEqual(['models', 'convert', 'org--model/safetensors/a', '--register-with', 'Q4_K_M'])
  })

  test('a failure says the stream’s error first, else stderr’s last line, else how it ended; a started stage is failed', () => {
    const errored = folded([...events(pullChainDone).slice(0, 2), JobUpdate.event(JobEvent.decode('{"type":"error","message":"cancelled -- nothing was written"}')), ended(1)])
    expect(errored.status).toEqual({ case: 'failed', message: 'cancelled -- nothing was written' })
    expect(errored.error).toBe('cancelled -- nothing was written')
    expect(errored.stages.get(1)?.state).toBe('failed')
    expect(folded([ended(1, false, 'apogee models: no such model\n')]).status).toEqual({ case: 'failed', message: 'apogee models: no such model' })
    expect(folded([JobUpdate.ended(JobEnd.notStarted('no recorded answer for apogee models pull x'))]).status).toEqual({ case: 'failed', message: 'no recorded answer for apogee models pull x' })
  })

  test('datasets-pull-handmade.jsonl: its bytes as last said, its document kept', () => {
    const progress = folded(events(datasetsPull))
    expect(progress.bytes?.done).toBeGreaterThan(0)
    expect(progress.documents).toHaveLength(1)
  })

  test('a line a run printed for people is a notice; a blank one is not; a busy line is shown and decides nothing', () => {
    const progress = folded([JobUpdate.said('converting it to F16'), JobUpdate.said('   '), JobUpdate.busy('hashing 40%')])
    expect(progress.notices).toEqual(['converting it to F16'])
    expect(progress.busy).toBe('hashing 40%')
    expect(progress.streamed).toBe(false)
  })

  test("bytes' fraction only when the total is known, and a stage's start clears the last file's bytes", () => {
    const progress = folded(events('{"type":"bytes","done":5,"total":10,"file":"a","index":1,"count":2}'))
    expect(progress.bytes?.fraction).toBe(0.5)
    expect(folded(events('{"type":"bytes","done":5}')).bytes?.fraction).toBeUndefined()
    progress.apply(JobUpdate.event(JobEvent.decode('{"type":"stage","index":2,"count":3,"label":"convert to F16","state":"started"}')))
    expect(progress.bytes).toBeUndefined()
  })
})
