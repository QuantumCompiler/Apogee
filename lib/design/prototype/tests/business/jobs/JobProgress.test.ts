import { describe, expect, test } from 'vitest'
import pullChainDone from '@recorded/business/jobs/Fixtures/pull-chain-done-handmade.jsonl?raw'
import datasetsPull from '@recorded/business/jobs/Fixtures/datasets-pull-handmade.jsonl?raw'
import trainPipelineRun from '@recorded/business/jobs/Fixtures/train-pipeline-run.jsonl?raw'
import trainPipelineResume from '@recorded/business/jobs/Fixtures/train-pipeline-resume.jsonl?raw'
import trainCyclePassed from '@recorded/business/jobs/Fixtures/train-cycle-run-passed.jsonl?raw'
import generateRun from '../../../fixtures/business/media/video-generate-run-handmade.jsonl?raw'
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

  test("train-pipeline-run.jsonl (design 1j, the Mac's 55h parts): each stage its run, its gate's score, the run's id, the trainer's latest iteration", () => {
    const progress = folded(events(trainPipelineRun).slice(0, 24))
    expect(progress.pipelineRunID).toBe('pipe-20261026-074112')
    expect(progress.orderedStages().map(({ index, stage }) => [index, stage.label, stage.state, stage.runID, stage.cumulativeScore])).toEqual([
      [1, 'greetings', 'passed', 'pipe-20261026-074112-s0', 1],
      [2, 'refusals', 'training', 'pipe-20261026-074112-s1', undefined],
    ])
    expect(progress.iteration).toEqual({ iteration: 2, iterations: 4, loss: 0.62, lr: 0.0001, throughput: 10, stage: 2 })
    expect(progress.lastSaid).toBe('[train] mock trainer: 4 iteration(s), method lora')
    const ended = folded([...events(trainPipelineRun), JobUpdate.ended(JobEnd.of(ChildExit.exited(2), new BoundedTail(), false))])
    expect(ended.stages.get(2)).toEqual({ label: 'refusals', state: 'failed', runID: 'pipe-20261026-074112-s1', cumulativeScore: 0.5 })
    expect(ended.documents).toHaveLength(1)
    expect(ended.status.case).toBe('failed')
  })

  test("a stage event that names no run keeps the run its last one named (a resume's first writes repeat the last attempt's)", () => {
    const progress = folded(events(trainPipelineResume).slice(0, 5))
    expect(progress.stages.get(2)).toEqual({ label: 'refusals', state: 'training', runID: 'pipe-20261026-074112-s1', cumulativeScore: 0.5 })
    progress.apply(JobUpdate.event(JobEvent.decode('{"count":2,"index":2,"label":"refusals","state":"evaluating","type":"stage"}')))
    expect(progress.stages.get(2)?.runID).toBe('pipe-20261026-074112-s1')
  })

  test("train-cycle-run-passed.jsonl: a cycle pass's narration in order, its latest line what it last said", () => {
    const progress = folded(events(trainCyclePassed))
    expect(progress.passes.map((line) => line.state)).toEqual(['collecting', 'merging', 'training', 'gating', 'promoting', 'passed', 'passed'])
    expect(progress.pass).toEqual({ state: 'passed', text: '[cycle] complete -- nightly-model promoted to v1 (/Users/someone/.apogee/models/tiny/gguf/052b295c3e9b/nightly-model-v1.gguf)' })
    expect(progress.lastSaid).toBe(progress.pass?.text)
    expect(progress.pipelineRunID).toBe('cycle-20261026-074119')
  })

  test("video-generate-run-handmade.jsonl (design 1k, the Mac's media parts): the device that loaded, the latest step, the frames written and the assembly, then the record", () => {
    const lines = events(generateRun)
    const loading = folded(lines.slice(0, 4))
    expect(loading.device).toBeUndefined()
    expect(loading.step).toBeUndefined()
    expect(loading.notices).toEqual([
      'from /Users/someone/Movies/Apogee/lighthouse-dusk-draft.mp4.json: model Wan2.2-TI2V-5B/pipeline/9b1e47c0a2f6, the prompt, 1280x704, 24 fps, 5 s, 16 steps, seed 4242 (held)',
      'changed: 50 steps (--final)',
      'Wan2.2-TI2V-5B/pipeline/9b1e47c0a2f6 (the record): 1280x704, 121 frames (5 s at 24 fps), 50 steps, seed 4242',
    ])
    const denoising = folded(lines.slice(0, 43))
    expect(denoising.ready).toEqual({ device: 'mps', dtype: 'bfloat16', mode: 't2v' })
    expect(denoising.device).toBe('mps')
    expect(denoising.step).toEqual({ done: 38, of: 50 })
    expect(denoising.frames).toBeUndefined()
    expect(denoising.assembling).toBeUndefined()
    const done = folded([...lines, ended(0)])
    expect(done.frames).toEqual({ frames: 121, width: 1280, height: 704 })
    expect(done.assembling).toBe(121)
    expect(done.documents).toHaveLength(1)
    expect(done.status).toEqual({ case: 'done' })
  })

  test("a composition's scene and its speech (design 1l, the Mac's media parts): the scene and what it is doing, a new scene forgetting the last one's step, and the speech's segments", () => {
    const lines = events(
      [
        '{"type":"job","verb":"video compose","protocol_version":1,"schema":"2026-11-01"}',
        '{"type":"scene","scene":1,"scenes":2,"state":"speaking"}',
        '{"type":"segment","index":1,"of":1,"seconds":1.42,"scene":1}',
        '{"type":"scene","scene":1,"scenes":2,"state":"rendering"}',
        '{"type":"step","step":25,"steps":50,"scene":1}',
        '{"type":"scene","scene":2,"scenes":2,"state":"asset"}',
      ].join('\n'),
    )
    const speaking = folded(lines.slice(0, 3))
    expect(speaking.scene).toEqual({ scene: 1, scenes: 2, state: 'speaking' })
    expect(speaking.segment).toEqual({ done: 1, of: 1 })
    const rendering = folded(lines.slice(0, 5))
    expect(rendering.scene).toEqual({ scene: 1, scenes: 2, state: 'rendering' })
    expect(rendering.step).toEqual({ done: 25, of: 50 })
    const next = folded(lines)
    expect(next.scene).toEqual({ scene: 2, scenes: 2, state: 'asset' })
    expect(next.step).toBeUndefined()
  })
})
